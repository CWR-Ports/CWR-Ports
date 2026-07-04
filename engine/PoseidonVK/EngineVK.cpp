/*
 * PoseidonVK Vulkan rendering engine bootstrap implementation
 */

#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>
#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>
#include <Poseidon/Graphics/Shared/ScreenshotWriter.hpp>

#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

namespace Poseidon
{
int g_emitDrawCalls = 0;
int g_flushQueueCalls = 0;

Engine* CreateEngineVK(int width, int height, bool windowed, int bpp)
{
    EngineVK* engine = new EngineVK(width, height, windowed, bpp);
    if (!engine->IsReady())
    {
        delete engine;
        return nullptr;
    }
    return engine;
}

EngineVK::EngineVK(int width, int height, bool windowed, int bpp)
{
    _w = width;
    _h = height;
    _pixelSize = bpp;
    _windowed = windowed;
    _minGuardX = 0;
    _maxGuardX = _w;
    _minGuardY = 0;
    _maxGuardY = _h;

    _textBank = new TextBankVK(this);

    LOG_INFO(Graphics, "PoseidonVK: Initializing Vulkan engine ({}x{} {}bpp)", _w, _h, _pixelSize);

    InitVulkan();
    InitShaders();
    InitPipelineLayouts();

    _activePassId = PassId::Opaque;
    BeginScreenPass();
}

EngineVK::~EngineVK()
{
    LOG_INFO(Graphics, "PoseidonVK: Destroying Vulkan engine");

    // Wait for the GPU to finish all in-flight work before tearing down any
    // resources — the pipeline cache, descriptor buffers, and shader modules
    // freed below may still be referenced by the last submitted command buffer.
    if (_device != VK_NULL_HANDLE)
        vkDeviceWaitIdle(_device);

    delete _textBank;
    _textBank = nullptr;

    ClearPipelineCache();
    DeinitPipelineLayouts();
    DeinitShaders();
    ShutdownVulkan();
}

bool EngineVK::InitDrawDone()
{
    return _frameOpen;
}

bool EngineVK::IsAbleToDraw()
{
    return _vkReady && _sdlWindow != nullptr;
}

void EngineVK::InitDraw(bool clear, PackedColor color)
{
    if (_frameOpen || !_vkReady) return;

    vkWaitForFences(_device, 1, &_inFlightFences[_currentFrame], VK_TRUE, UINT64_MAX);
    vkResetFences(_device, 1, &_inFlightFences[_currentFrame]);

    _queueNo._firstVertex = true;
    _queueNo._firstIndex = true;
    _queueNo._vertexBufferUsed = 0;
    _queueNo._indexBufferUsed = 0;
    _vboUploadedVerts = 0;

    if (_materialDescriptorPool[_currentFrame] != VK_NULL_HANDLE)
    {
        vkResetDescriptorPool(_device, _materialDescriptorPool[_currentFrame], 0);
    }
    _materialDescriptorCache[_currentFrame].clear();
    _uniformOffset = 0;

    VkResult result = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
                                            _imageAvailableSem[_currentFrame],
                                            VK_NULL_HANDLE, &_currentImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        // TODO: handle swapchain recreation
    }

    // With more swapchain images than frames in flight, the acquired image may
    // still be in use by another in-flight frame (they share one depth buffer).
    // Wait on the fence of whichever frame last rendered to this image before
    // reusing it. Without this the two frames race and the image flickers.
    if (_imagesInFlight[_currentImageIndex] != VK_NULL_HANDLE)
        vkWaitForFences(_device, 1, &_imagesInFlight[_currentImageIndex], VK_TRUE, UINT64_MAX);
    _imagesInFlight[_currentImageIndex] = _inFlightFences[_currentFrame];

    VkCommandBuffer cb = _commandBuffers[_currentFrame];
    vkResetCommandBuffer(cb, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    vkBeginCommandBuffer(cb, &beginInfo);

    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = _renderPass;
    renderPassInfo.framebuffer = _swapchainFramebuffers[_currentImageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = _swapchainExtent;

    VkClearValue clearValues[2] = {};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    if (clear)
    {
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        clearValues[0].color = {{r, g, b, 1.0f}};
    }
    renderPassInfo.clearValueCount = 2;
    renderPassInfo.pClearValues = clearValues;

    vkCmdBeginRenderPass(cb, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    base::InitDraw();
    _frameOpen = true;
}

void EngineVK::FinishDraw()
{
    if (!_frameOpen) return;

    base::FinishDraw();
    base::DrawFinishTexts();

    _frameCounter++;
    _frameOpen = false;
}

void EngineVK::NextFrame()
{
    if (!_vkReady) return;

    VkCommandBuffer cb = _commandBuffers[_currentFrame];
    vkCmdEndRenderPass(cb);
    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

    VkSemaphore waitSemaphores[] = {_imageAvailableSem[_currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;

    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;

    VkSemaphore signalSemaphores[] = {_renderFinishedSem[_currentImageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _inFlightFences[_currentFrame]);

    CaptureScreenshotIfPending();

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &_currentImageIndex;

    vkQueuePresentKHR(_presentQueue, &presentInfo);

    LOG_INFO(Graphics, "VK Frame summary: EmitDraw = {}, FlushQueue = {}", g_emitDrawCalls, g_flushQueueCalls);
    g_emitDrawCalls = 0;
    g_flushQueueCalls = 0;

    _currentFrame = (_currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

    base::NextFrame();
}

void EngineVK::DrawTestPattern(const char* name)
{
}

void EngineVK::Pause()
{
}

void EngineVK::Restore()
{
}

void EngineVK::ResetForRemount()
{
}

bool EngineVK::SwitchRes(int w, int h, int bpp)
{
    _w = w;
    _h = h;
    _pixelSize = bpp;
    return true;
}

bool EngineVK::SwitchRefreshRate(int refresh)
{
    _refreshRate = refresh;
    return true;
}

bool EngineVK::SetWindowMode(Poseidon::WindowMode mode)
{
    _windowMode = mode;
    return true;
}

Poseidon::WindowMode EngineVK::GetCurrentWindowMode() const
{
    return _windowMode;
}

void EngineVK::OnWindowResized(int w, int h)
{
    _w = w;
    _h = h;
    _vsConstants.vpScale[0] = 2.0f / static_cast<float>(_w);
    _vsConstants.vpScale[1] = 2.0f / static_cast<float>(_h);
}

void EngineVK::OnFullscreenChanged(bool windowed)
{
    _windowed = windowed;
}

void EngineVK::ListResolutions(FindArray<ResolutionInfo>& ret)
{
}

void EngineVK::ListRefreshRates(FindArray<int>& ret)
{
}

void EngineVK::ListMonitors(FindArray<MonitorInfo>& ret)
{
}

int EngineVK::GetCurrentMonitor() const
{
    return 0;
}

bool EngineVK::SwitchMonitor(int idx)
{
    return true;
}

bool EngineVK::GetDesktopDisplayMode(int& w, int& h, int& refresh) const
{
    w = _w;
    h = _h;
    refresh = _refreshRate;
    return true;
}

bool EngineVK::GetCurrentDisplayMode(int& w, int& h, int& refresh) const
{
    w = _w;
    h = _h;
    refresh = _refreshRate;
    return true;
}

bool EngineVK::GetRequestedFullscreenMode(int& w, int& h, int& refresh) const
{
    w = _w;
    h = _h;
    refresh = _refreshRate;
    return true;
}

bool EngineVK::SetSwapInterval(int interval)
{
    return true;
}

int EngineVK::GetSwapInterval() const
{
    return 0;
}

RString EngineVK::GetDebugName() const
{
    return "PoseidonVK_Debug";
}

RString EngineVK::GetRendererName() const
{
    return "Vulkan Stub Renderer";
}

bool EngineVK::GetTL() const
{
    return true; // Register hardware T&L capabilities with factory core
}

bool EngineVK::GetTLOnSurface() const
{
    return true; // Expose hardware processing on split decal terrains
}

void EngineVK::UpdateProjection()
{
    if (IsIn3DPass() && GScene)
    {
        FlushAndFreeAllQueues(_queueNo, true); // Commit pipeline queues prior to viewport modifications
        Camera* camera = GScene->GetCamera();
        ConvertProjectionMatrix(_frameState.projection, camera->ProjectionNormal(), _bias); // Re-align view frustums
        std::memcpy(_vsConstants.proj, &_frameState.projection, 64);
    }
}

bool EngineVK::IsResizable() const
{
    return true;
}

int EngineVK::AFrameTime() const
{
    return 16;
}

void EngineVK::Clear(bool clearZ, bool clear, PackedColor color)
{
}

void EngineVK::FogColorChanged(ColorVal fogColor)
{
    // Keep the cached frame state in sync so BeginPass does not overwrite the
    // fog color with a stale value. GLES does the same in its FogColorChanged;
    // without it the fog color lagged (defaulting to black) and everything fogged
    // to black, including the sky.
    _frameState.fogColor[0] = fogColor.R();
    _frameState.fogColor[1] = fogColor.G();
    _frameState.fogColor[2] = fogColor.B();
    _frameState.fogColor[3] = 1.0f;

    _psConstants.fogColor[0] = fogColor.R();
    _psConstants.fogColor[1] = fogColor.G();
    _psConstants.fogColor[2] = fogColor.B();
    _psConstants.fogColor[3] = 1.0f;
}

void EngineVK::EnableNightEye(float night)
{
    if (fabs(_nightEye - night) < 0.01f)
        return;
    FlushQueues();
    _nightEye = night;
    if (_nightEye > 0.01f)
    {
        _psConstants.rgbEyeCoef[0] = 0.2f;
        _psConstants.rgbEyeCoef[1] = 0.9f;
        _psConstants.rgbEyeCoef[2] = 0.4f;
        _psConstants.rgbEyeCoef[3] = 1.0f - _nightEye;
    }
    else
    {
        _psConstants.rgbEyeCoef[0] = 0.0f;
        _psConstants.rgbEyeCoef[1] = 0.0f;
        _psConstants.rgbEyeCoef[2] = 0.0f;
        _psConstants.rgbEyeCoef[3] = 1.0f;
    }
}

void EngineVK::SetGamma(float g)
{
    _gamma = g;
}

void EngineVK::SetBias(int value)
{
    _bias = value;
}

float EngineVK::ZShadowEpsilon() const
{
    return 0.0f;
}

float EngineVK::ZRoadEpsilon() const
{
    return 0.0f;
}

float EngineVK::ObjMipmapCoef() const
{
    return 1.0f;
}

void EngineVK::GetZCoefs(float& zAdd, float& zMult)
{
    zAdd = 0.0f;
    zMult = 1.0f;
}

bool EngineVK::CanZBias() const
{
    return false; // Force software polygon depth sorting in transLight
}

bool EngineVK::ZBiasExclusion() const
{
    return false;
}

AbstractTextBank* EngineVK::TextBank()
{
    return _textBank;
}

void EngineVK::TextureDestroyed(Texture* tex)
{
}

void EngineVK::HandleEvents()
{
    // Pump SDL events through the shared event window. Without this SDL never
    // processes input, so keyboard and mouse do nothing and the window ignores
    // close/resize/focus. Mirrors the GL33 and GLES32 backends.
    _eventWindow.HandleEvents();
}

bool EngineVK::IsOpen() const
{
    return _eventWindow.IsOpen();
}

void EngineVK::SetMouseGrab(bool grab)
{
    _eventWindow.SetMouseGrab(grab);
}

bool EngineVK::IsMouseGrabbed() const
{
    return _eventWindow.IsMouseGrabbed();
}

void EngineVK::Screenshot(RString filename)
{
    _pendingScreenshotPath = filename;
}

void EngineVK::FlushPendingScreenshot()
{
    CaptureScreenshotIfPending();
}

void EngineVK::CaptureScreenshotIfPending()
{
    if (_pendingScreenshotPath.GetLength() == 0)
        return;

    RString path = _pendingScreenshotPath;
    _pendingScreenshotPath = "";

    std::vector<uint8_t> pixels;
    int w = 0;
    int h = 0;
    if (!ReadPixelsFromSwapchain(pixels, w, h))
        return;

    std::vector<uint8_t> rgb(w * h * 3);
    const bool isBGRA = (_swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM || _swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);

    for (int i = 0; i < w * h; i += 1)
    {
        if (isBGRA)
        {
            rgb[i * 3 + 0] = pixels[i * 4 + 2];
            rgb[i * 3 + 1] = pixels[i * 4 + 1];
            rgb[i * 3 + 2] = pixels[i * 4 + 0];
        }
        else
        {
            rgb[i * 3 + 0] = pixels[i * 4 + 0];
            rgb[i * 3 + 1] = pixels[i * 4 + 1];
            rgb[i * 3 + 2] = pixels[i * 4 + 2];
        }
    }

    ScreenshotWriter::WriteRGB(path, w, h, rgb.data());
}

int EngineVK::SampleBackBufferNonBlack()
{
    if (!_vkReady)
        return -1;

    std::vector<uint8_t> pixels;
    int w = 0;
    int h = 0;
    if (!ReadPixelsFromSwapchain(pixels, w, h))
        return -1;

    int nonBlack = 0;
    for (int sy = 0; sy < 16; sy += 1)
    {
        int y = h * sy / 16;
        for (int sx = 0; sx < 16; sx += 1)
        {
            int x = w * sx / 16;
            int idx = (y * w + x) * 4;
            if (pixels[idx + 0] > 2 || pixels[idx + 1] > 2 || pixels[idx + 2] > 2)
                nonBlack += 1;
        }
    }
    return nonBlack;
}

bool EngineVK::SamplePixel(int x, int y, uint8_t* outRGB)
{
    if (!_vkReady || !outRGB)
        return false;

    std::vector<uint8_t> pixels;
    int w = 0;
    int h = 0;
    if (!ReadPixelsFromSwapchain(pixels, w, h))
        return false;

    if (x < 0 || y < 0 || x >= w || y >= h)
        return false;

    int idx = (y * w + x) * 4;
    const bool isBGRA = (_swapchainFormat == VK_FORMAT_B8G8R8A8_UNORM || _swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB);
    if (isBGRA)
    {
        outRGB[0] = pixels[idx + 2];
        outRGB[1] = pixels[idx + 1];
        outRGB[2] = pixels[idx + 0];
    }
    else
    {
        outRGB[0] = pixels[idx + 0];
        outRGB[1] = pixels[idx + 1];
        outRGB[2] = pixels[idx + 2];
    }
    return true;
}

bool EngineVK::ReadPixelsFromSwapchain(std::vector<uint8_t>& outRGBA, int& outW, int& outH)
{
    if (!_vkReady)
        return false;

    vkWaitForFences(_device, 1, &_inFlightFences[_currentFrame], VK_TRUE, UINT64_MAX);

    VkImage srcImage = _swapchainImages[_currentImageIndex];
    uint32_t width = _swapchainExtent.width;
    uint32_t height = _swapchainExtent.height;

    outW = static_cast<int>(width);
    outH = static_cast<int>(height);

    VkDeviceSize imageSize = width * height * 4;
    outRGBA.resize(imageSize);

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAlloc = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = imageSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
    allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo allocResult{};
    if (vmaCreateBuffer(_vmaAllocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAlloc, &allocResult) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "PoseidonVK: Failed to create screenshot staging buffer");
        return false;
    }

    VkCommandBufferAllocateInfo cmdAlloc{};
    cmdAlloc.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAlloc.commandPool = _commandPool;
    cmdAlloc.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(_device, &cmdAlloc, &cb) != VK_SUCCESS)
    {
        vmaDestroyBuffer(_vmaAllocator, stagingBuffer, stagingAlloc);
        return false;
    }

    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &begin);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = srcImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyImageToBuffer(cb, srcImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1, &region);

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cb);

    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cb;

    vkQueueSubmit(_graphicsQueue, 1, &submit, VK_NULL_HANDLE);
    vkQueueWaitIdle(_graphicsQueue);

    vkFreeCommandBuffers(_device, _commandPool, 1, &cb);

    vmaInvalidateAllocation(_vmaAllocator, stagingAlloc, 0, VK_WHOLE_SIZE);
    std::memcpy(outRGBA.data(), allocResult.pMappedData, imageSize);

    vmaDestroyBuffer(_vmaAllocator, stagingBuffer, stagingAlloc);
    return true;
}

} // namespace Poseidon
