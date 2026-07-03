#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/TextureVK.hpp>
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
    PoseidonAssert(IsIn3DPass());
    TextureVK* tex = reinterpret_cast<TextureVK*>(mip._texture);
    int level = mip._level;
    PassId passId = SpecToPassId(spec);
    // derive/apply pass state (blend, depth, alpha-test) for this section, and
    // snapshot the bound texture into the per-object draw record so EmitDraw can
    // resolve it through the texture registry (keyed by the surface creation id).
    ApplyPassState(tex, level, spec, passId, PipelineVertexInput::Mesh);
    _currentDrawItem.backendTextureHandle = tex ? tex->_surface.GetCreationID() : 0;
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
    PrepareMeshTLImpl(_frameState, modelToWorld, spec);
}

void EngineVK::PrepareMeshTLImpl(const FrameState& frame, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec)
{
    EnableSunLight(!Poseidon::render::Has(spec.material, Poseidon::render::Material::DisableSun));

    GfxMatrix worldMatrix;
    ConvertMatrix(worldMatrix, modelToWorld);

    // convert the object transform to camera-relative coordinates before it is
    // handed to the gpu. BuildFrameState zeroes the view matrix translation, so
    // the world matrix must carry the camera offset (matches the GLES backend
    // and preserves precision for large world coordinates).
    worldMatrix._41 -= frame.cameraPos[0];
    worldMatrix._42 -= frame.cameraPos[1];
    worldMatrix._43 -= frame.cameraPos[2];

    // capture the per-object transform/spec into the draw record. EmitDraw reads
    // the world matrix from here (via WorldInstances slot 0); the vsTransform
    // shader ignores VSConstants.world, so writing it there had no effect.
    _currentDrawItem = DrawItem{};
    _currentDrawItem.worldMatrix = worldMatrix;
    _currentDrawItem.specFlags = spec;
    _currentDrawItem.bias = _bias;

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
