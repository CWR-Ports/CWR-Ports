#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/VertexBufferVK.hpp>
#include <Poseidon/Graphics/Rendering/Primitives/Poly.hpp>

namespace Poseidon
{

VertexBufferVK::VertexBufferVK(VmaAllocator allocator)
    : _allocator(allocator)
{
}

VertexBufferVK::~VertexBufferVK()
{
    if (_ibo)
        vmaDestroyBuffer(_allocator, _ibo, _iboAllocation);
    if (_vbo)
        vmaDestroyBuffer(_allocator, _vbo, _vboAllocation);
}

void VertexBufferVK::CopyVertices(const Shape& src)
{
    if (_vertexCount <= 0)
        return;

    void* mapped = nullptr;
    if (vmaMapMemory(_allocator, _vboAllocation, &mapped) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK: VBO map failed");
        return;
    }

    SVertex* sData = static_cast<SVertex*>(mapped);
    const UVPair* uv = &src.UV(0);
    const Vector3* pos = &src.Pos(0);
    const Vector3* norm = &src.Norm(0);
    
    for (int i = src.NVertex(); --i >= 0;)
    {
        sData->pos = Vector3P(pos->X(), pos->Y(), pos->Z());
        // normals are negated in engine convention
        sData->norm = Vector3P(-norm->X(), -norm->Y(), -norm->Z());
        pos++;
        norm++;
        sData->t0 = *uv;
        uv++;
        sData++;
    }

    vmaUnmapMemory(_allocator, _vboAllocation);
}

bool VertexBufferVK::Init(const Shape& src, VBType type)
{
    if (src.NVertex() <= 0)
    {
        LOG_DEBUG(Graphics, "VK: Empty vertices.");
        return false;
    }

    _dynamic = (type == VBDynamic || type == VBSmallDiscardable);
    _vertexCount = src.NVertex();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = _vertexCount * sizeof(SVertex);
    bufferInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    // for simplicity on both UMA and NUMA, use CPU_TO_GPU memory for now.
    // dynamic buffers need it, static buffers can use it (though optimal is staging -> GPU_ONLY).
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(_allocator, &bufferInfo, &allocInfo, &_vbo, &_vboAllocation, nullptr) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK: failed to create VBO");
        return false;
    }

    CopyVertices(src);

    int indices = 0;
    for (Offset o = src.BeginFaces(); o < src.EndFaces(); src.NextFace(o))
    {
        const Poly& poly = src.Face(o);
        PoseidonAssert(poly.N() >= 3);
        indices += (poly.N() - 2) * 3;
    }
    _indexCount = indices;

    if (indices > 0)
    {
        VkBufferCreateInfo iboInfo{};
        iboInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        iboInfo.size = indices * sizeof(VertexIndex);
        iboInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
        iboInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vmaCreateBuffer(_allocator, &iboInfo, &allocInfo, &_ibo, &_iboAllocation, nullptr) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK: failed to create IBO");
            return false;
        }

        void* mapped = nullptr;
        if (vmaMapMemory(_allocator, _iboAllocation, &mapped) == VK_SUCCESS)
        {
            VertexIndex* iData = static_cast<VertexIndex*>(mapped);
            for (Offset o = src.BeginFaces(); o < src.EndFaces(); src.NextFace(o))
            {
                const Poly& poly = src.Face(o);
                for (int i = 2; i < poly.N(); i++)
                {
                    *iData++ = poly.GetVertex(0);
                    *iData++ = poly.GetVertex(i - 1);
                    *iData++ = poly.GetVertex(i);
                }
            }
            vmaUnmapMemory(_allocator, _iboAllocation);
        }

        _sections.Realloc(src.NSections());
        _sections.Resize(src.NSections());
        int start = 0;
        for (int i = 0; i < src.NSections(); i++)
        {
            const ShapeSection& sec = src.GetSection(i);
            int size = 0;
            int minV = INT_MAX;
            int maxV = 0;
            for (Offset o = sec.beg; o < sec.end; src.NextFace(o))
            {
                const Poly& face = src.Face(o);
                size += (face.N() - 2) * 3;
                for (int vv = 0; vv < face.N(); vv++)
                {
                    int vi = face.GetVertex(vv);
                    saturateMin(minV, vi);
                    saturateMax(maxV, vi);
                }
            }
            _sections[i].beg = start;
            _sections[i].end = start + size;
            _sections[i].begVertex = minV;
            _sections[i].endVertex = maxV + 1;
            start += size;
        }
    }

    return true;
}

void VertexBufferVK::Update(const Shape& src, bool dynamic)
{
    if (_dynamic || dynamic || bufferDirty)
    {
        CopyVertices(src);
        bufferDirty = false;
    }
}

VertexBuffer* EngineVK::CreateVertexBuffer(const Shape& src, VBType type)
{
    auto* buf = new VertexBufferVK(_vmaAllocator);
    if (buf->Init(src, type))
        return buf;
    delete buf;
    return nullptr;
}

int EngineVK::CompareBuffers(const Shape&, const Shape&)
{
    return 0;
}

} // namespace Poseidon
