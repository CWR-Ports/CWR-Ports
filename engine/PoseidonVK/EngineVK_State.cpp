/*
 * Vulkan Pipeline State Object (PSO) management
 */

#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Foundation/Logging/Logging.hpp>

namespace Poseidon
{

void EngineVK::ClearPipelineCache()
{
    if (_device == VK_NULL_HANDLE)
    {
        return;
    }
    LOG_INFO(Graphics, "Vulkan: Clearing Pipeline Cache...");
    for (auto& pair : _pipelineCache)
    {
        vkDestroyPipeline(_device, pair.second, nullptr);
    }
    _pipelineCache.clear();
}

VkPipeline EngineVK::GetOrCreatePipeline(const PipelineKey& key)
{
    auto it = _pipelineCache.find(key);
    if (it != _pipelineCache.end())
        return it->second;

    const render::RenderPassDescriptor& d = key.desc;

    // resolve shaders
    VertexShaderID vs = VSNone;
    PixelShaderID ps = PSNone;
    const bool meshVertexInput = (key.vertexFormat == 1);

    switch (d.shader)
    {
        case render::ShaderFamily::Shadow:
            if (meshVertexInput)
            {
                vs = VSShadow;
                ps = PSShadow;
            }
            else
            {
                vs = VSScreen;
                ps = PSShadow;
            }
            break;
        case render::ShaderFamily::Water:
            vs = meshVertexInput ? VSTransform : VSNone;
            ps = PSWater;
            break;
        case render::ShaderFamily::Detail:
            vs = meshVertexInput ? VSTransform : VSNone;
            ps = PSDetail;
            break;
        case render::ShaderFamily::Grass:
            vs = meshVertexInput ? VSTransform : VSNone;
            ps = PSGrass;
            break;
        case render::ShaderFamily::Flat:
            vs = VSScreen;
            ps = PSFlat;
            break;
        case render::ShaderFamily::Normal:
        default:
            vs = meshVertexInput ? VSTransform : VSNone;
            ps = PSNormal;
            break;
    }

    if (vs == VSNone || ps == PSNone)
    {
        LOG_ERROR(Graphics, "Vulkan: Invalid shader combination for PSO!");
        return VK_NULL_HANDLE;
    }

    VkPipelineShaderStageCreateInfo shaderStages[2] = {};
    
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = _vsModules[vs];
    shaderStages[0].pName = "main";
    
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = _fsModules[ps];
    shaderStages[1].pName = "main";

    // vertex Input
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    
    std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
    
    if (key.vertexFormat == 1) // SVertex
    {
        bindingDescription.stride = sizeof(SVertex);
        
        attributeDescriptions.resize(3);
        // pos
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(SVertex, pos);
        // norm
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(SVertex, norm);
        // t0
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(SVertex, t0);
    }
    else // TLVertex
    {
        bindingDescription.stride = 36;
        bindingDescription.stride = 40;
        attributeDescriptions.resize(6);
        // pos (Vector3P)
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = 0;
        // rhw (float)
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32_SFLOAT;
        attributeDescriptions[1].offset = 12;
        // color (PackedColor)
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributeDescriptions[2].offset = 16;
        // specular (PackedColor)
        attributeDescriptions[3].binding = 0;
        attributeDescriptions[3].location = 3;
        attributeDescriptions[3].format = VK_FORMAT_B8G8R8A8_UNORM;
        attributeDescriptions[3].offset = 20;
        // t0 (UVPair)
        attributeDescriptions[4].binding = 0;
        attributeDescriptions[4].location = 4;
        attributeDescriptions[4].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[4].offset = 24;
        // t1 (UVPair)
        attributeDescriptions[5].binding = 0;
        attributeDescriptions[5].location = 5;
        attributeDescriptions[5].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[5].offset = 32;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // input Assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // viewport state (dynamic)
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;

    switch (d.cull)
    {
        case render::CullMode::Back: rasterizer.cullMode = VK_CULL_MODE_BACK_BIT; break;
        case render::CullMode::Front: rasterizer.cullMode = VK_CULL_MODE_FRONT_BIT; break;
        case render::CullMode::None: rasterizer.cullMode = VK_CULL_MODE_NONE; break;
    }

    rasterizer.frontFace = (d.frontFace == render::FrontFaceMode::CW) ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = (d.surface == render::SurfaceMode::OnSurface) ? VK_TRUE : VK_FALSE;

    // multisample
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    
    // alpha to coverage
    if (d.alpha == render::AlphaMode::Test && d.blend == render::BlendMode::Opaque && meshVertexInput)
    {
        multisampling.alphaToCoverageEnable = VK_TRUE;
    }

    // depth Stencil
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    
    switch (d.depth)
    {
        case render::DepthMode::Normal:
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_TRUE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case render::DepthMode::ReadOnly:
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_FALSE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            break;
        case render::DepthMode::Disabled:
            depthStencil.depthTestEnable = VK_FALSE;
            depthStencil.depthWriteEnable = VK_FALSE;
            break;
        case render::DepthMode::Shadow:
            depthStencil.depthTestEnable = VK_TRUE;
            depthStencil.depthWriteEnable = VK_FALSE;
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
            depthStencil.stencilTestEnable = VK_TRUE;
            // Simplified shadow stencil
            depthStencil.front.compareOp = VK_COMPARE_OP_EQUAL;
            depthStencil.front.passOp = VK_STENCIL_OP_INCREMENT_AND_CLAMP;
            depthStencil.front.reference = 0;
            depthStencil.back = depthStencil.front;
            break;
    }

    // color blend
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    
    switch (d.blend)
    {
        case render::BlendMode::Opaque:
            colorBlendAttachment.blendEnable = VK_FALSE;
            break;
        case render::BlendMode::AlphaBlend:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case render::BlendMode::Additive:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case render::BlendMode::Shadow:
            colorBlendAttachment.blendEnable = VK_TRUE;
            colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };
    if (d.surface == render::SurfaceMode::OnSurface)
    {
        dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
    }
    VkPipelineDynamicStateCreateInfo dynamicStateInfo{};
    dynamicStateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicStateInfo.pDynamicStates = dynamicStates.data();

    // create pipeline
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicStateInfo;
    pipelineInfo.layout = _pipelineLayout;
    pipelineInfo.renderPass = _renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "Vulkan: Failed to create graphics pipeline!");
        return VK_NULL_HANDLE;
    }

    _pipelineCache[key] = pipeline;
    return pipeline;
}

} // namespace Poseidon
