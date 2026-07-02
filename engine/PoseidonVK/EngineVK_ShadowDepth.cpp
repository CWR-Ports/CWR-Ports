#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/TextureVK.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>
#include <PoseidonVK/VertexBufferVK.hpp>

#include <algorithm>
#include <cstring>
#include <vector>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/SPIRV/GlslangToSpv.h>

namespace Poseidon
{

namespace
{
static const char s_shadowSolidVsGLSL[] = R"(#version 450
layout(push_constant) uniform ShadowPush {
    mat4 lightVP;
} pc;

layout(location = 0) in vec3 pos;

void main() {
    gl_Position = pc.lightVP * vec4(pos, 1.0);
}
)";

static const char s_shadowSolidFsGLSL[] = R"(#version 450
void main() {}
)";

static VkShaderModule CompileLocalShader(VkDevice device, EShLanguage stage, const char* source, const char* name)
{
    glslang::TShader shader(stage);
    const char* strings[1] = {source};
    shader.setStrings(strings, 1);

    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_0);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);

    const TBuiltInResource* resources = GetDefaultResources();
    const EShMessages rules = static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);

    if (!shader.parse(resources, 450, false, rules))
    {
        LOG_ERROR(Graphics, "Vulkan: shadow shader compile error [{}]: {}", name, shader.getInfoLog());
        return VK_NULL_HANDLE;
    }

    glslang::TProgram program;
    program.addShader(&shader);
    if (!program.link(rules))
    {
        LOG_ERROR(Graphics, "Vulkan: shadow shader link error [{}]: {}", name, program.getInfoLog());
        return VK_NULL_HANDLE;
    }

    std::vector<unsigned int> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(stage), spirv);

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = spirv.size() * sizeof(unsigned int);
    createInfo.pCode = spirv.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to create shadow shader module [{}]", name);
        return VK_NULL_HANDLE;
    }

    return module;
}

static void DestroyShadowSolidPipeline(VkDevice device, VkShaderModule& vertexShader, VkShaderModule& fragmentShader,
                                       VkPipelineLayout& pipelineLayout, VkPipeline& pipeline,
                                       VkRenderPass& cachedRenderPass)
{
    if (pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(device, pipeline, nullptr);
        pipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (vertexShader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(device, vertexShader, nullptr);
        vertexShader = VK_NULL_HANDLE;
    }
    if (fragmentShader != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(device, fragmentShader, nullptr);
        fragmentShader = VK_NULL_HANDLE;
    }
    cachedRenderPass = VK_NULL_HANDLE;
}

static bool EnsureShadowSolidPipeline(VkDevice device, VkRenderPass renderPass, VkShaderModule& vertexShader,
                                      VkShaderModule& fragmentShader, VkPipelineLayout& pipelineLayout,
                                      VkPipeline& pipeline, VkRenderPass& cachedRenderPass)
{
    if (pipeline != VK_NULL_HANDLE && cachedRenderPass == renderPass)
        return true;

    DestroyShadowSolidPipeline(device, vertexShader, fragmentShader, pipelineLayout, pipeline, cachedRenderPass);

    vertexShader = CompileLocalShader(device, EShLangVertex, s_shadowSolidVsGLSL, "shadow-solid-vs");
    fragmentShader = CompileLocalShader(device, EShLangFragment, s_shadowSolidFsGLSL, "shadow-solid-fs");
    if (vertexShader == VK_NULL_HANDLE || fragmentShader == VK_NULL_HANDLE)
    {
        DestroyShadowSolidPipeline(device, vertexShader, fragmentShader, pipelineLayout, pipeline, cachedRenderPass);
        return false;
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 16;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to create shadow pipeline layout");
        DestroyShadowSolidPipeline(device, vertexShader, fragmentShader, pipelineLayout, pipeline, cachedRenderPass);
        return false;
    }

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexShader;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentShader;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(float) * 3;
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    VkVertexInputAttributeDescription attr{};
    attr.binding = 0;
    attr.location = 0;
    attr.format = VK_FORMAT_R32G32B32_SFLOAT;
    attr.offset = 0;

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 1;
    vertexInput.pVertexAttributeDescriptions = &attr;

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 0;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;

    if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to create shadow solid pipeline");
        DestroyShadowSolidPipeline(device, vertexShader, fragmentShader, pipelineLayout, pipeline, cachedRenderPass);
        return false;
    }

    cachedRenderPass = renderPass;
    return true;
}
} // namespace

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
        DestroyShadowSolidPipeline(_device, _shadowSolidVertexShader, _shadowSolidFragmentShader, _shadowPipelineLayout,
                                   _shadowSolidPipeline, _shadowPipelineRenderPass);
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
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
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
    if (factor01 < 0.0f)
        factor01 = 0.0f;
    else if (factor01 > 1.0f)
        factor01 = 1.0f;
    _shadowSunFactor = factor01;
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
    _shadowTuning.enabled = enabled;
    if (!enabled)
        _shadowMapActive = false;
}

bool EngineVK::ShadowMapsEnabled() const
{
    return _shadowTuning.enabled;
}

Engine::ShadowMapTuning EngineVK::GetShadowMapTuning() const
{
    return _shadowTuning;
}

void EngineVK::SetShadowMapTuning(const ShadowMapTuning& tuning)
{
    _shadowTuning = tuning;
    if (!_shadowTuning.enabled)
        _shadowMapActive = false;
}

void EngineVK::RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3, int numCascades, int omniCount, int res, const ShadowCasterSet& casters)
{
    if (numCascades > 4)
        numCascades = 4;

    if (numCascades < 1 || res <= 0 || !lightVPs || !splitViewDist || !camFwd3)
    {
        _shadowMapActive = false;
        return;
    }

    if (!_shadowTuning.enabled)
    {
        _shadowMapActive = false;
        return;
    }

    _shadowMapActive = false;

    if (!EnsureShadowTarget(res, numCascades))
        return;

    if (!EnsureShadowSolidPipeline(_device, _shadowRenderPass, _shadowSolidVertexShader, _shadowSolidFragmentShader,
                                   _shadowPipelineLayout, _shadowSolidPipeline, _shadowPipelineRenderPass))
        return;

    if (casters.alphaVertexCount > 0)
        LOG_WARN(Graphics, "Vulkan: alpha shadow casters are not yet rendered; using solid caster pass only");

    if (!casters.solidXYZ || casters.solidVertexCount < 3)
    {
        _shadowMapActive = false;
        return;
    }

    const VkDeviceSize vertexBytes = static_cast<VkDeviceSize>(casters.solidVertexCount) * 3u * sizeof(float);
    VkBuffer vertexBuffer = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = vertexBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(_vmaAllocator, &bufferInfo, &allocInfo, &vertexBuffer, &vertexAllocation, nullptr) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to allocate temporary shadow vertex buffer");
        return;
    }

    void* mapped = nullptr;
    if (vmaMapMemory(_vmaAllocator, vertexAllocation, &mapped) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to map temporary shadow vertex buffer");
        vmaDestroyBuffer(_vmaAllocator, vertexBuffer, vertexAllocation);
        return;
    }
    std::memcpy(mapped, casters.solidXYZ, static_cast<size_t>(vertexBytes));
    vmaUnmapMemory(_vmaAllocator, vertexAllocation);

    VkCommandBufferAllocateInfo allocCb{};
    allocCb.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocCb.commandPool = _commandPool;
    allocCb.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCb.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(_device, &allocCb, &cb) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to allocate shadow command buffer");
        vmaDestroyBuffer(_vmaAllocator, vertexBuffer, vertexAllocation);
        return;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(cb, &beginInfo) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to begin shadow command buffer");
        vkFreeCommandBuffers(_device, _commandPool, 1, &cb);
        vmaDestroyBuffer(_vmaAllocator, vertexBuffer, vertexAllocation);
        return;
    }

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(res);
    viewport.height = static_cast<float>(res);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {static_cast<uint32_t>(res), static_cast<uint32_t>(res)};

    VkDeviceSize offset = 0;
    for (int i = 0; i < numCascades; ++i)
    {
        VkClearValue clear{};
        clear.depthStencil = {1.0f, 0};

        VkRenderPassBeginInfo passInfo{};
        passInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        passInfo.renderPass = _shadowRenderPass;
        passInfo.framebuffer = _shadowFramebuffers[i];
        passInfo.renderArea.offset = {0, 0};
        passInfo.renderArea.extent = {static_cast<uint32_t>(res), static_cast<uint32_t>(res)};
        passInfo.clearValueCount = 1;
        passInfo.pClearValues = &clear;

        vkCmdBeginRenderPass(cb, &passInfo, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdSetViewport(cb, 0, 1, &viewport);
        vkCmdSetScissor(cb, 0, 1, &scissor);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _shadowSolidPipeline);
        vkCmdPushConstants(cb, _shadowPipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0,
                           static_cast<uint32_t>(sizeof(float) * 16), lightVPs + i * 16);
        vkCmdBindVertexBuffers(cb, 0, 1, &vertexBuffer, &offset);
        vkCmdDraw(cb, casters.solidVertexCount, 1, 0, 0);
        vkCmdEndRenderPass(cb);
    }

    if (vkEndCommandBuffer(cb) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to end shadow command buffer");
        vkFreeCommandBuffers(_device, _commandPool, 1, &cb);
        vmaDestroyBuffer(_vmaAllocator, vertexBuffer, vertexAllocation);
        return;
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;

    if (vkQueueSubmit(_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: failed to submit shadow command buffer");
        vkFreeCommandBuffers(_device, _commandPool, 1, &cb);
        vmaDestroyBuffer(_vmaAllocator, vertexBuffer, vertexAllocation);
        return;
    }

    vkQueueWaitIdle(_graphicsQueue);
    vkFreeCommandBuffers(_device, _commandPool, 1, &cb);
    vmaDestroyBuffer(_vmaAllocator, vertexBuffer, vertexAllocation);

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

void EngineVK::UpdateShadowMapLitState()
{
    // populate PS constants for the lit fragment shader to sample the shadow depth array
    // shadowCtl: {enable, 0, darkness, texelSize}
    // cascadeVP: 4x mat4 light view-projection matrices
    // cascadeSplits: per-tier selection distances
    // cascadeCtl: {count, fadeRange, biasBase, omniCount}
    // camFwd: camera forward direction

    float ctl[4] = {0.0f, 0.0f, 1.0f, 0.0f};
    float splits[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float cascadeCtl[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float camFwd[4] = {0.0f, 0.0f, 1.0f, 0.0f};

    if (_shadowTuning.enabled && _shadowMapActive && _shadowCascades > 0)
    {
        ctl[0] = 1.0f;
        ctl[2] = 1.0f - _shadowSunFactor * (1.0f - _shadowTuning.darkness);
        ctl[3] = (_shadowMapRes > 0) ? (1.0f / static_cast<float>(_shadowMapRes)) : 0.0f;
        cascadeCtl[0] = static_cast<float>(_shadowCascades);
        cascadeCtl[1] = _shadowTuning.fadeRange;
        cascadeCtl[2] = _shadowTuning.biasBase;
        cascadeCtl[3] = static_cast<float>(_shadowOmniCount);
        for (int i = 0; i < _shadowCascades && i < 4; i++)
            splits[i] = _shadowSplits[i];
        camFwd[0] = _shadowCamFwd[0];
        camFwd[1] = _shadowCamFwd[1];
        camFwd[2] = _shadowCamFwd[2];
        std::memcpy(_psConstants.cascadeVP, _shadowMapVP, sizeof(float) * 16 * _shadowCascades);
    }

    std::memcpy(_psConstants.shadowCtl, ctl, 16);
    std::memcpy(_psConstants.cascadeSplits, splits, 16);
    std::memcpy(_psConstants.cascadeCtl, cascadeCtl, 16);
    std::memcpy(_psConstants.camFwd, camFwd, 16);
}

bool EngineVK::DumpShadowMap(const char* path)
{
    return false;
}

} // namespace Poseidon

