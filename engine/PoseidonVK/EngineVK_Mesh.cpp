#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Graphics/Core/MatrixConversion.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>

namespace Poseidon
{

void EngineVK::PrepareMesh(const render::LegacySpec& /*spec*/)
{
    BeginScreenPass();
}

void EngineVK::BeginMesh(TLVertexTable& mesh, const render::LegacySpec& /*spec*/)
{
    BeginScreenPass();
    _mesh = &mesh;
    AddVertices(mesh.VertexData(), mesh.NVertex());
}

void EngineVK::EndMesh(TLVertexTable& mesh)
{
    _mesh = nullptr;
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
    FlushAndFreeAllQueues(_queueNo, true);
    BeginPass(SpecToPassId(spec));
    PrepareMeshTLImpl({}, modelToWorld, spec);
}

void EngineVK::PrepareMeshTLImpl(const FrameState& frame, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
    EnableSunLight(!Poseidon::render::Has(spec.material, Poseidon::render::Material::DisableSun));

    GfxMatrix worldMatrix;
    ConvertMatrix(worldMatrix, modelToWorld);

    memcpy(_vsConstants.world, &worldMatrix, 64);

    float constColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    if (GScene && Poseidon::render::Has(spec.routing, Poseidon::render::Routing::IsColored))
    {
        ColorVal cc = GScene->GetConstantColor();
        constColor[0] = cc.R();
        constColor[1] = cc.G();
        constColor[2] = cc.B();
        constColor[3] = cc.A();
    }
    memcpy(_psConstants.constColor, constColor, sizeof(constColor));
}

void EngineVK::BeginMeshTL(const Shape& sMesh, int spec, bool dynamic)
{
    sMesh.GetVertexBuffer()->Update(sMesh, dynamic);
}

void EngineVK::EndMeshTL(const Shape& sMesh)
{
}

} // namespace Poseidon
