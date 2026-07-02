/*
 * PoseidonVK Vulkan rendering engine bootstrap implementation
 */

// this file acts as the bootstrap phase for poseidonvk the vulkan renderer backend
//
// we are shifting to vulkan on android to bypass the overhead driver bugs
// and lack of modern extensions in the latest adreno and mali opengl es drivers
// this port will allow us to manage memory pools directly using vma handle
// staging ring buffers for dynamic geometry without cpu stalls and avoid complex
// driver fallback paths like manual bcdec transcoding for s3tc and dxt formats
//
// this skeleton handles factory registration building and target linking
// before we start implementing the vulkan pipelines descriptor sets swapchains
// and render passes
//
// I DO NOT YET KNOW IF THIS WILL BE FEASIBLE AT ALL BUT LETS SEE

#include <Poseidon/Dev/Debug/DebugOverlay.hpp>
#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Core/Config/EngineConfig.hpp>
#include <Poseidon/Graphics/Shared/WindowPlacement.hpp>
#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>

namespace Poseidon
{
Engine* CreateEngineVK(int width, int height, bool windowed, int bpp)
{
    return new EngineVK(width, height, windowed, bpp);
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
}

EngineVK::~EngineVK()
{
    LOG_INFO(Graphics, "PoseidonVK: Destroying Vulkan engine");

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

    VkResult result = vkAcquireNextImageKHR(_device, _swapchain, UINT64_MAX,
                                            _imageAvailableSem[_currentFrame],
                                            VK_NULL_HANDLE, &_currentImageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        // TODO: handle swapchain recreation
    }

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

    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    if (clear)
    {
        float r = ((color >> 16) & 0xFF) / 255.0f;
        float g = ((color >> 8) & 0xFF) / 255.0f;
        float b = (color & 0xFF) / 255.0f;
        clearColor.color = {{r, g, b, 1.0f}};
    }
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

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

    VkSemaphore signalSemaphores[] = {_renderFinishedSem[_currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(_graphicsQueue, 1, &submitInfo, _inFlightFences[_currentFrame]);

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;

    VkSwapchainKHR swapchains[] = {_swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &_currentImageIndex;

    vkQueuePresentKHR(_presentQueue, &presentInfo);

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

void EngineVK::DrawDecal(Vector3Par pos, float rhw, float sizeX, float sizeY, PackedColor col, const MipInfo& mip, int specFlags)
{
}

void EngineVK::DrawPolygon(const VertexIndex* i, int n)
{
}

void EngineVK::DrawSection(const FaceArray& face, Offset beg, Offset end)
{
}

void EngineVK::DrawPoints(int beg, int end)
{
}

void EngineVK::Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip)
{
}

void EngineVK::DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int nVertices, const Rect2DAbs& clip, int specFlags)
{
}

void EngineVK::DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int nVertices, const Rect2DPixel& clip, int specFlags)
{
}

void EngineVK::DrawLine(const Line2DAbs& rect, PackedColor c0, PackedColor c1, const Rect2DAbs& clip)
{
}

void EngineVK::DrawLine(int beg, int end)
{
}

void EngineVK::PrepareMesh(const render::LegacySpec& spec)
{
}

void EngineVK::BeginMesh(TLVertexTable& mesh, const render::LegacySpec& spec)
{
}

void EngineVK::EndMesh(TLVertexTable& mesh)
{
}

void EngineVK::PrepareTriangle(const MipInfo& mip, int specFlags)
{
}

void EngineVK::FogColorChanged(ColorVal fogColor)
{
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
    return true;
}

bool EngineVK::ZBiasExclusion() const
{
    return false;
}

AbstractTextBank* EngineVK::TextBank()
{
    return nullptr;
}

void EngineVK::TextureDestroyed(Texture* tex)
{
}

void EngineVK::HandleEvents()
{
}

bool EngineVK::IsOpen() const
{
    return true;
}

void EngineVK::SetMouseGrab(bool grab)
{
}

bool EngineVK::IsMouseGrabbed() const
{
    return false;
}

} // namespace Poseidon
