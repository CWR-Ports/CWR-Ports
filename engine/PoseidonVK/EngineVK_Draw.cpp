#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/VertexBufferVK.hpp>
#include <Poseidon/Graphics/Rendering/Frame/Frame.hpp>

namespace Poseidon
{

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

void EngineVK::EmitDraw(const render::frame::Draw& d)
{
    if (!_vkReady || !_frameOpen) return;

    PipelineKey key;
    key.desc = d.descriptor;
    // for now, assume mesh draws (like models) use svertex (format 1),
    // and ui/screen space uses tlvertex (format 0).
    // if it's a 3d pass, it's format 1. 
    key.vertexFormat = (d.descriptor.pass == render::PassKind::ScreenSpace3D) ? 0 : 1; 

    VkPipeline pipeline = GetOrCreatePipeline(key);
    if (pipeline == VK_NULL_HANDLE)
        return;

    VkCommandBuffer cb = _commandBuffers[_currentFrame];
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // apply dynamic states
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = static_cast<float>(_h);
    viewport.width = static_cast<float>(_w);
    viewport.height = -static_cast<float>(_h);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cb, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = _swapchainExtent;
    vkCmdSetScissor(cb, 0, 1, &scissor);

    // bind uniform descriptor sets
    // uint32_t dynamicOffsets[2] = {0, 0};
    // vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, _pipelineLayout, 0, 1, &_descriptorSet, 2, dynamicOffsets);

    // resolve and bind mesh buffers
    VertexBufferVK* vbuf = GetVertexBuffer(d.mesh.vao);
    if (vbuf)
    {
        VkBuffer vertexBuffers[] = {vbuf->_vbo};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);
        
        if (vbuf->_ibo)
        {
            vkCmdBindIndexBuffer(cb, vbuf->_ibo, 0, VK_INDEX_TYPE_UINT16);
            vkCmdDrawIndexed(cb, d.indexCount, 1, d.indexBegin, 0, 0);
        }
        else
        {
            vkCmdDraw(cb, vbuf->_vertexCount, 1, 0, 0);
        }
    }
}

} // namespace Poseidon
