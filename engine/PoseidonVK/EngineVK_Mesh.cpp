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
    ChangeClipPlanes();
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

void EngineVK::UpdateProjection()
{
    if (IsIn3DPass())
    {
        FlushAndFreeAllQueues(_queueNo, true);

        Camera* camera = GScene->GetCamera();
        int projBias = _canZBias ? 0 : _bias;
        ConvertProjectionMatrix(_frameState.projection, camera->ProjectionNormal(), projBias);
        UploadVSProjection(_frameState);
    }
}

void EngineVK::PrepareTriangle(const MipInfo& mip, int specFlags)
{
}

void EngineVK::PrepareTriangleTL(const Poseidon::MipInfo& mip, const Poseidon::render::LegacySpec& spec)
{
}

bool EngineVK::InstancedRunAdd(const Matrix4& modelToWorld)
{
    if (_instPending >= 256)
        return false;

    GfxMatrix& m = _instArray[_instPending];
    ConvertMatrix(m, modelToWorld);

    m._41 -= _frameState.cameraPos[0];
    m._42 -= _frameState.cameraPos[1];
    m._43 -= _frameState.cameraPos[2];

    ++_instPending;
    return true;
}

void EngineVK::BeginInstancedRunUpload()
{
    UploadWorldInstances(reinterpret_cast<const float*>(_instArray), _instPending);
    BeginInstancedRun(_instPending);
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
    ChangeClipPlanes();

    GfxMatrix worldMatrix;
    ConvertMatrix(worldMatrix, modelToWorld);

    worldMatrix._41 -= frame.cameraPos[0];
    worldMatrix._42 -= frame.cameraPos[1];
    worldMatrix._43 -= frame.cameraPos[2];

    _currentDrawItem = DrawItem{};
    _currentDrawItem.worldMatrix = worldMatrix;
    _currentDrawItem.specFlags = spec;
    _currentDrawItem.bias = _bias;

    UploadObjectConstants(_currentDrawItem);

    float constColor[4] = {1.0f, 1.0f, 1.0f, 1.0f};

    if (GScene && Poseidon::render::Has(spec.routing, Poseidon::render::Routing::IsColored))
    {
        ColorVal cc = GScene->GetConstantColor();
        constColor[0] = cc.R();
        constColor[1] = cc.G();
        constColor[2] = cc.B();
        constColor[3] = cc.A();
    }

    if (memcmp(constColor, _psConstants.constColor, sizeof(constColor)) != 0)
    {
        memcpy(_psConstants.constColor, constColor, sizeof(constColor));
        UploadPSConstant(PSConstants::SlotConstColor, _psConstants.constColor);
    }
}

void EngineVK::BeginMeshTL(const Shape& sMesh, int spec, bool dynamic)
{
    sMesh.GetVertexBuffer()->Update(sMesh, dynamic);
}

void EngineVK::EndMeshTL(const Shape& sMesh)
{
    ClearLights();
}

} // namespace Poseidon
