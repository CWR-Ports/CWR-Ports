#include <PoseidonVK/EngineVK.hpp>

namespace Poseidon
{

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

void EngineVK::PrepareTriangleTL(const Poseidon::MipInfo& mip, const Poseidon::render::LegacySpec& spec)
{
}

bool EngineVK::InstancedRunAdd(const Matrix4& modelToWorld)
{
    return false;
}

void EngineVK::BeginInstancedRunUpload()
{
}

void EngineVK::PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
}

void EngineVK::PrepareMeshTLImpl(const FrameState& frame, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
}

void EngineVK::BeginMeshTL(const Shape& sMesh, int spec, bool dynamic)
{
}

void EngineVK::EndMeshTL(const Shape& sMesh)
{
}

} // namespace Poseidon
