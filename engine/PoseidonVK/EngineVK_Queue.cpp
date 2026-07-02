#include <PoseidonVK/EngineVK.hpp>

namespace Poseidon
{

QueueVK::QueueVK()
{
}

int QueueVK::Allocate(TextureVK* tex, int level, int spec, int minI, int maxI, int tip)
{
    return -1;
}

void QueueVK::Free(int i)
{
}

WORD* EngineVK::QueueAdd(QueueVK& queue, int n)
{
    return nullptr;
}

void EngineVK::QueueFan(const VertexIndex* ii, int n)
{
}

void EngineVK::Queue2DPoly(const TLVertex* v, int n)
{
}

void EngineVK::FlushQueue(QueueVK& queue, int index)
{
}

void EngineVK::FlushAndFreeQueue(QueueVK& queue, int index)
{
}

int EngineVK::AllocateQueue(QueueVK& queue, TextureVK* tex, int level, int spec)
{
    return -1;
}

void EngineVK::FreeQueue(QueueVK& queue, int index)
{
}

void EngineVK::FreeAllQueues(QueueVK& queue)
{
}

void EngineVK::FlushAndFreeAllQueues(QueueVK& queue, bool nonEmptyOnly)
{
}

void EngineVK::FlushAllQueues(QueueVK& queue, int skip)
{
}

void EngineVK::CloseAllQueues(QueueVK& queue)
{
}

void EngineVK::QueuePrepareTriangle(const Poseidon::MipInfo& absMip, int specFlags)
{
}

void EngineVK::FlushQueues()
{
}

void EngineVK::EnableReorderQueues(bool enableReorder)
{
}

void EngineVK::AddVertices(const TLVertex* v, int n)
{
}

void EngineVK::UploadPendingVertices()
{
}

void EngineVK::ApplyPassState(TextureVK* tex, int level, const Poseidon::render::LegacySpec& spec, Poseidon::PassId passId, PipelineVertexInput vertexInput)
{
}

void EngineVK::DoSwitchRenderMode(RenderMode mode)
{
    _renderMode = mode;
}

void EngineVK::BeginPass(Poseidon::PassId passId)
{
}

void EngineVK::BeginScreenPass()
{
}

void EngineVK::DiscardVB()
{
}

} // namespace Poseidon
