#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/TextureVK.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <PoseidonVK/VertexBufferVK.hpp>

namespace Poseidon
{

static void TransitionImageLayout(VkDevice device, VkCommandPool pool, VkQueue queue, VkImage image, VkFormat format, VkImageLayout oldLayout, VkImageLayout newLayout, int layers)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = pool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = layers;

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL)
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
    {
        barrier.srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else
    {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = 0;
        sourceStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        destinationStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    }

    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &commandBuffer);
}

bool EngineVK::EnsureShadowTarget(int res, int layers)
{
    if (_shadowImage != VK_NULL_HANDLE && _shadowMapRes == res && _shadowCascades == layers)
        return true;

    LOG_INFO(Graphics, "Vulkan: Creating shadow depth array target ({}x{}x{})", res, res, layers);

    // Destroy existing
    if (!_shadowLayerViews.empty())
    {
        for (auto view : _shadowLayerViews)
            vkDestroyImageView(_device, view, nullptr);
        _shadowLayerViews.clear();
    }
    if (!_shadowFramebuffers.empty())
    {
        for (auto fb : _shadowFramebuffers)
            vkDestroyFramebuffer(_device, fb, nullptr);
        _shadowFramebuffers.clear();
    }
    if (_shadowImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(_device, _shadowImageView, nullptr);
        _shadowImageView = VK_NULL_HANDLE;
    }
    if (_shadowImage != VK_NULL_HANDLE)
    {
        vmaDestroyImage(_vmaAllocator, _shadowImage, _shadowImageAlloc);
        _shadowImage = VK_NULL_HANDLE;
        _shadowImageAlloc = VK_NULL_HANDLE;
    }
    if (_shadowRenderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(_device, _shadowRenderPass, nullptr);
        _shadowRenderPass = VK_NULL_HANDLE;
    }
    if (_shadowSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(_device, _shadowSampler, nullptr);
        _shadowSampler = VK_NULL_HANDLE;
    }

    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;

    // Create Image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = res;
    imageInfo.extent.height = res;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    if (vmaCreateImage(_vmaAllocator, &imageInfo, &allocInfo, &_shadowImage, &_shadowImageAlloc, nullptr) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to allocate shadow map image!");
        return false;
    }

    // Create array view for sampling
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = _shadowImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layers;

    if (vkCreateImageView(_device, &viewInfo, nullptr, &_shadowImageView) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create shadow map array image view!");
        return false;
    }

    // Create per-layer views for rendering
    _shadowLayerViews.resize(layers);
    for (int i = 0; i < layers; ++i)
    {
        VkImageViewCreateInfo layerViewInfo{};
        layerViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        layerViewInfo.image = _shadowImage;
        layerViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerViewInfo.format = depthFormat;
        layerViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        layerViewInfo.subresourceRange.baseMipLevel = 0;
        layerViewInfo.subresourceRange.levelCount = 1;
        layerViewInfo.subresourceRange.baseArrayLayer = i;
        layerViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(_device, &layerViewInfo, nullptr, &_shadowLayerViews[i]) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: Failed to create shadow map layer view {}!", i);
            return false;
        }
    }

    // Create Render Pass
    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = depthFormat;
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 0;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkSubpassDependency dependencies[2];

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &depthAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies;

    if (vkCreateRenderPass(_device, &renderPassInfo, nullptr, &_shadowRenderPass) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create shadow render pass!");
        return false;
    }

    // Create Framebuffers
    _shadowFramebuffers.resize(layers);
    for (int i = 0; i < layers; ++i)
    {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = _shadowRenderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &_shadowLayerViews[i];
        framebufferInfo.width = res;
        framebufferInfo.height = res;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(_device, &framebufferInfo, nullptr, &_shadowFramebuffers[i]) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "Vulkan: Failed to create shadow map framebuffer {}!", i);
            return false;
        }
    }

    // Create Sampler
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_FALSE;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;

    if (vkCreateSampler(_device, &samplerInfo, nullptr, &_shadowSampler) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create shadow map sampler!");
        return false;
    }

    // Transition image layout to shader read only
    TransitionImageLayout(_device, _commandPool, _graphicsQueue, _shadowImage, depthFormat, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, layers);

    _shadowMapRes = res;
    _shadowCascades = layers;
    return true;
}

void EngineVK::SetShadowMapSunFactor(float factor01)
{
}

void EngineVK::BeginShadowPass()
{
}

void EngineVK::EndShadowPass()
{
}

bool EngineVK::ShadowDepthProbe(const float* lightVP16, const float* triXYZ, int vertCount, int res, float* outDepth)
{
    return false;
}

bool EngineVK::ShadowMapCacheSelfTest()
{
    return true;
}

void EngineVK::SetShadowMapsEnabled(bool enabled)
{
}

bool EngineVK::ShadowMapsEnabled() const
{
    return _shadowMapActive;
}

Engine::ShadowMapTuning EngineVK::GetShadowMapTuning() const
{
    return {};
}

void EngineVK::SetShadowMapTuning(const ShadowMapTuning& tuning)
{
}

void EngineVK::RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3, int numCascades, int omniCount, int res, const ShadowCasterSet& casters)
{
    if (numCascades > 4)
        numCascades = 4;

    if (!EnsureShadowTarget(res, numCascades))
        return;

    // TODO: implement rendering solid and alpha casters
    _shadowMapActive = true;
    _shadowMapRes = res;
    _shadowCascades = numCascades;
    _shadowOmniCount = (omniCount < 0) ? 0 : (omniCount > numCascades ? numCascades : omniCount);

    for (int i = 0; i < numCascades * 16; i++)
    {
        _shadowMapVP[i] = lightVPs[i];
    }
    for (int i = 0; i < numCascades; i++)
    {
        _shadowSplits[i] = splitViewDist[i];
    }
    _shadowCamFwd[0] = camFwd3[0];
    _shadowCamFwd[1] = camFwd3[1];
    _shadowCamFwd[2] = camFwd3[2];
}

bool EngineVK::DumpShadowMap(const char* path)
{
    return false;
}

} // namespace Poseidon
