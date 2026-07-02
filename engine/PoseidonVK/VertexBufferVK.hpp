#ifndef __VERTEX_BUFFER_VK_HPP
#define __VERTEX_BUFFER_VK_HPP

#include <Poseidon/Graphics/Rendering/Primitives/Vertex.hpp>
#include <Poseidon/Graphics/Rendering/Shape/Shape.hpp>
#include <Poseidon/Foundation/Containers/Array.hpp>
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

namespace Poseidon
{

struct VBSectionInfoVK
{
    int beg, end;
    int begVertex, endVertex;
};

class VertexBufferVK : public VertexBuffer
{
    friend class EngineVK;

  private:
    EngineVK* _engine = nullptr;
    VmaAllocator _allocator = VK_NULL_HANDLE;
    
    uint32_t _vao = 0; // ID for backendMeshHandle registry

    VkBuffer _vbo = VK_NULL_HANDLE;
    VmaAllocation _vboAllocation = VK_NULL_HANDLE;
    
    VkBuffer _ibo = VK_NULL_HANDLE;
    VmaAllocation _iboAllocation = VK_NULL_HANDLE;
    
    bool _dynamic = false;
    int _vertexCount = 0;
    int _indexCount = 0;
    
    AutoArray<VBSectionInfoVK> _sections;

  public:
    VertexBufferVK(EngineVK* engine, VmaAllocator allocator);
    ~VertexBufferVK() override;

    bool Init(const Shape& src, VBType type);
    void Update(const Shape& src, bool dynamic) override;

  private:
    void CopyVertices(const Shape& src);
};

} // namespace Poseidon

#endif // __VERTEX_BUFFER_VK_HPP
