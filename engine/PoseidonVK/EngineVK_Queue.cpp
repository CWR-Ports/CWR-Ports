#include <PoseidonVK/EngineVK.hpp>
#include <PoseidonVK/TextureVK.hpp>
#include <Poseidon/Graphics/Core/FanDecompose.hpp>
#include <Poseidon/Graphics/Rendering/BuildRenderPassDescriptor.hpp>
#include <Poseidon/World/Scene/Scene.hpp>
#include <Poseidon/Graphics/Rendering/Lighting/Lights.hpp>
#include <Poseidon/World/Scene/Camera/Camera.hpp>
#include <climits>
#include <cstring>

namespace Poseidon
{

QueueVK::QueueVK()
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        _triUsed[i] = false;
    }
    _usedCounter = 0;
    _vertexBufferUsed = 0;
    _indexBufferUsed = 0;
    _meshBase = 0;
    _meshSize = 0;
    _actTri = -1;
    _firstVertex = true;
    _firstIndex = true;
}

int QueueVK::Allocate(TextureVK* tex, int level, int spec, int minI, int maxI, int tip)
{
    int index = -1;
    if (tip >= minI && tip < maxI && _triUsed[tip])
    {
        TriQueue& triq = _tri[tip];
        if (tex == triq._texture && spec == triq._special)
            index = tip;
    }

    int free = -1;
    if (index < 0)
    {
        for (int i = minI; i < maxI; i++)
        {
            if (_triUsed[i])
            {
                TriQueue& triq = _tri[i];
                if (tex != triq._texture || spec != triq._special)
                    continue;
                index = i;
            }
            else if (free < 0)
            {
                free = i;
            }
        }
    }
    _usedCounter++;
    if (index >= 0)
    {
        TriQueue& triq = _tri[index];
        saturateMin(triq._level, level);
        triq._lastUsed = _usedCounter;
        return index;
    }
    if (free >= 0)
    {
        TriQueue& triq = _tri[free];
        triq._special = spec;
        triq._texture = tex;
        triq._level = level;
        triq._passId = SpecToPassId(spec);
        triq._lastUsed = _usedCounter;
        PoseidonAssert(triq._triangleQueue.Size() == 0);
        triq._triangleQueue.Resize(0);
        _triUsed[free] = true;
    }
    return free;
}

void QueueVK::Free(int i)
{
    PoseidonAssert(_tri[i]._triangleQueue.Size() == 0);
    PoseidonAssert(_triUsed[i]);
    _triUsed[i] = false;
}

WORD* EngineVK::QueueAdd(QueueVK& queue, int n)
{
    if (_instCount > 1)
        _instImpure = true;

    PoseidonAssert(queue._actTri >= 0);
    PoseidonAssert(queue._triUsed[queue._actTri]);
    TriQueue& triq = queue._tri[queue._actTri];
    if (triq._triangleQueue.Size() + n > TriQueueSize)
        FlushQueue(queue, queue._actTri);

    int index = triq._triangleQueue.Size();
    triq._triangleQueue.Resize(index + n);
    return triq._triangleQueue.Data() + index;
}

void EngineVK::QueueFan(const VertexIndex* ii, int n)
{
    const int addN = Poseidon::render::geom::FanTriangleIndexCount(n);
    if (addN == 0)
        return;

    WORD* tgt = QueueAdd(_queueNo, addN);
    if (!tgt || !ii)
        return;

    _dbgQueueFanCalls++;
    _dbgTotalFanTris += addN;

    const int offset = _queueNo._meshBase;
    PoseidonAssert(offset >= 0);

    Poseidon::render::geom::FanToTriangles(ii, n, offset, tgt);
}

void EngineVK::Queue2DPoly(const TLVertex* v0, int n)
{
    int addN = (n - 2) * 3;
    PoseidonAssert(_queueNo._actTri >= 0);
    PoseidonAssert(_queueNo._triUsed[_queueNo._actTri]);
    WORD* tgt = QueueAdd(_queueNo, addN);

    int offset = _queueNo._meshBase;
    PoseidonAssert(offset >= 0);

    for (int i = 2; i < n; i++)
    {
        *tgt++ = 0 + offset;
        *tgt++ = i - 1 + offset;
        *tgt++ = i + offset;
    }
}

extern int g_flushQueueCalls;

void EngineVK::FlushQueue(QueueVK& queue, int index)
{
    g_flushQueueCalls++;
    TriQueue& triq = queue._tri[index];
    int n = triq._triangleQueue.Size();
    if (n > 0)
    {
        UploadPendingVertices();

        if (index == MaxTriQueues - 1)
            FlushAllQueues(queue, index);

        ApplyPassState(triq._texture, triq._level, Poseidon::render::SplitLegacy(triq._special), triq._passId,
                       PipelineVertexInput::Screen);

        VkCommandBuffer cb = _commandBuffers[_currentFrame];

        PipelineKey key;
        key.vertexFormat = 0;
        
        Poseidon::render::BuildContext ctx;
        ctx.isIn3DPass = IsIn3DPass();
        ctx.isMultitexturing = IsMultitexturing();
        ctx.shadowAlphaRef = static_cast<std::uint8_t>((_shadowFactor * 7) >> 4);
        ctx.passKindHint = GetPassKindHint();
        key.desc = Poseidon::render::BuildRenderPassDescriptor(Poseidon::render::SplitLegacy(triq._special), ctx);

        AllocateUniformSpace(_uniformOffsetVS, _uniformOffsetWorld, _uniformOffsetPS);
        uint8_t* basePtr = static_cast<uint8_t*>(_uniformMapped[_currentFrame]);
        std::memcpy(basePtr + _uniformOffsetVS, &_vsConstants, sizeof(VSConstants));
        std::memcpy(basePtr + _uniformOffsetWorld, &_worldInstances, sizeof(WorldInstances));
        std::memcpy(basePtr + _uniformOffsetPS, &_psConstants, sizeof(PSConstants));

        const uint32_t alignment = 256;
        uint32_t sizeVS = (sizeof(VSConstants) + alignment - 1) & ~(alignment - 1);
        uint32_t sizeWorld = (sizeof(WorldInstances) + alignment - 1) & ~(alignment - 1);
        uint32_t sizePS = (sizeof(PSConstants) + alignment - 1) & ~(alignment - 1);
        vmaFlushAllocation(_vmaAllocator, _uniformAllocation[_currentFrame], _uniformOffsetVS, sizeVS + sizeWorld + sizePS);

        BindPipelineStateAndDescriptors(cb, key, _activeTexture0, _activeTexture1);

        VkBuffer vertexBuffers[] = { _vbo[_currentFrame] };
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cb, 0, 1, vertexBuffers, offsets);

        vkCmdBindIndexBuffer(cb, _ibo[_currentFrame], 0, VK_INDEX_TYPE_UINT16);

        int indexOffset = 0;
        int ibSize = n * sizeof(WORD);

        if (n + queue._indexBufferUsed <= IndexBufferLength && !queue._firstIndex)
        {
            indexOffset = queue._indexBufferUsed;
        }
        else
        {
            queue._firstIndex = false;
            indexOffset = 0;
        }

        if (_iboMapped[_currentFrame] != nullptr)
        {
            std::memcpy(static_cast<WORD*>(_iboMapped[_currentFrame]) + indexOffset, triq._triangleQueue.Data(), ibSize);
            vmaFlushAllocation(_vmaAllocator, _iboAllocation[_currentFrame], indexOffset * sizeof(WORD), ibSize);
        }
        queue._indexBufferUsed = indexOffset + n;

        vkCmdDrawIndexed(cb, n, 1, indexOffset, 0, 0);
        ++Poseidon::gPerfDrawCalls;

        static int s_debugDraws = 0;
        if (s_debugDraws < 5)
        {
            LOG_INFO(Graphics, "VK FlushQueue draw #{}: indices={} idxOff={} vpScale=[{},{},{},{}] shader={} blend={} depth={} cull={} tex={}",
                     s_debugDraws, n, indexOffset,
                     _vsConstants.vpScale[0], _vsConstants.vpScale[1], _vsConstants.vpScale[2], _vsConstants.vpScale[3],
                     static_cast<int>(key.desc.shader), static_cast<int>(key.desc.blend), static_cast<int>(key.desc.depth),
                     static_cast<int>(key.desc.cull), (triq._texture ? 1 : 0));
            if (_vboMirror.size() > 0 && _queueNo._vertexBufferUsed > 0)
            {
                const TLVertex& v = _vboMirror[0];
                LOG_INFO(Graphics, "  vert[0]: pos=[{},{},{}] rhw={} color=0x{:08x} uv=[{},{}]",
                         v.pos[0], v.pos[1], v.pos[2], v.rhw, static_cast<uint32_t>(v.color), v.t0.u, v.t0.v);
            }
            s_debugDraws++;
        }

        DrawItem item = {};
        item.isTLDraw = false;
        item.specFlags = Poseidon::render::SplitLegacy(triq._special);
        item.passId = triq._passId;
        _drawItems.push_back(item);

        triq._triangleQueue.Clear();
    }
}

void EngineVK::FlushAndFreeQueue(QueueVK& queue, int index)
{
    FlushQueue(queue, index);
    FreeQueue(queue, index);
}

int EngineVK::AllocateQueue(QueueVK& queue, TextureVK* tex, int level, int spec)
{
    bool alpha = (tex != nullptr && tex->IsTransparent()) || !_enableReorder;
    int minI = 0;
    int maxI = MaxTriQueues - 1;
    if (alpha)
    {
        minI = MaxTriQueues - 1;
        maxI = MaxTriQueues;
        FlushAllQueues(queue, MaxTriQueues - 1);
    }

    int index = queue.Allocate(tex, level, spec, minI, maxI, queue._actTri);
    if (index >= 0)
    {
        PoseidonAssert(queue._triUsed[index]);
        return index;
    }
    int minUsed = INT_MAX;
    for (int i = minI; i < maxI; i++)
    {
        int used = queue._tri[i]._lastUsed;
        if (used < minUsed)
        {
            minUsed = used;
            index = i;
        }
    }
    if (index < 0)
        index = 0;
    FlushAndFreeQueue(queue, index);
    index = queue.Allocate(tex, level, spec, minI, maxI, index);
    PoseidonAssert(index >= 0);
    PoseidonAssert(queue._triUsed[index]);
    return index;
}

void EngineVK::FreeQueue(QueueVK& queue, int index)
{
    PoseidonAssert(!queue._tri[index]._triangleQueue.Size());
    queue.Free(index);
}

void EngineVK::FreeAllQueues(QueueVK& queue)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (queue._triUsed[i])
        {
            queue._tri[i]._triangleQueue.Clear();
            FreeQueue(queue, i);
        }
    }
}

void EngineVK::FlushAndFreeAllQueues(QueueVK& queue, bool nonEmptyOnly)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (queue._triUsed[i] && (!nonEmptyOnly || queue._tri[i]._triangleQueue.Size() > 0))
            FlushAndFreeQueue(queue, i);
    }
}

void EngineVK::FlushAllQueues(QueueVK& queue, int skip)
{
    for (int i = 0; i < MaxTriQueues; i++)
    {
        if (i != skip && queue._triUsed[i])
            FlushQueue(queue, i);
    }
}

void EngineVK::CloseAllQueues(QueueVK& queue)
{
    FlushAndFreeAllQueues(queue);
    queue._usedCounter = 0;
    queue._firstVertex = true;
    queue._firstIndex = true;
}

void EngineVK::QueuePrepareTriangle(const Poseidon::MipInfo& absMip, int specFlags)
{
    TextureVK* tex = reinterpret_cast<TextureVK*>(absMip._texture);
    int level = absMip._level;
    _queueNo._actTri = AllocateQueue(_queueNo, tex, level, specFlags);
    PoseidonAssert(_queueNo._triUsed[_queueNo._actTri]);
}

void EngineVK::FlushQueues()
{
    FlushAndFreeAllQueues(_queueNo);
}

void EngineVK::EnableReorderQueues(bool enableReorder)
{
    if (_enableReorder == enableReorder)
    {
        return;
    }
    _enableReorder = enableReorder;
    if (!_enableReorder)
    {
        FlushQueues();
    }
}

void EngineVK::AddVertices(const TLVertex* v, int n)
{
    if (n <= 0)
        return;
    if (n > MeshBufferLength)
    {
        LOG_ERROR(Graphics, "Needed {} vertices, {} available", n, static_cast<int>(MeshBufferLength));
        return;
    }

    _dbgAddVerticesCalls++;
    _dbgTotalVertices += n;

    if (static_cast<int>(_vboMirror.size()) < MeshBufferLength)
        _vboMirror.resize(MeshBufferLength);

    if (_queueNo._vertexBufferUsed + n <= MeshBufferLength && !_queueNo._firstVertex)
    {
        std::memcpy(&_vboMirror[_queueNo._vertexBufferUsed], v, sizeof(TLVertex) * n);
        _queueNo._meshBase = _queueNo._vertexBufferUsed;
        _queueNo._meshSize = n;
        _queueNo._vertexBufferUsed += n;
    }
    else
    {
        _queueNo._firstVertex = false;
        FlushAndFreeAllQueues(_queueNo);
        _vboUploadedVerts = 0;
        std::memcpy(&_vboMirror[0], v, sizeof(TLVertex) * n);
        _queueNo._meshBase = 0;
        _queueNo._meshSize = n;
        _queueNo._vertexBufferUsed = n;
    }
}

void EngineVK::UploadPendingVertices()
{
    const int used = _queueNo._vertexBufferUsed;
    if (_vboUploadedVerts >= used)
        return;
    const int first = _vboUploadedVerts;
    const int count = used - first;
    if (count > 0 && _vboMapped[_currentFrame] != nullptr)
    {
        std::memcpy(static_cast<TLVertex*>(_vboMapped[_currentFrame]) + first, &_vboMirror[first], count * sizeof(TLVertex));
        vmaFlushAllocation(_vmaAllocator, _vboAllocation[_currentFrame], first * sizeof(TLVertex), count * sizeof(TLVertex));
    }
    _vboUploadedVerts = used;
}

void EngineVK::ApplyPassState(TextureVK* tex, int level, const Poseidon::render::LegacySpec& spec, Poseidon::PassId passId, PipelineVertexInput vertexInput)
{
    Poseidon::render::BuildContext ctx;
    ctx.isIn3DPass = vertexInput == PipelineVertexInput::Mesh
                   ? true
                   : vertexInput == PipelineVertexInput::Screen ? false : IsIn3DPass();
    ctx.isMultitexturing = IsMultitexturing();
    ctx.shadowAlphaRef = static_cast<std::uint8_t>((_shadowFactor * 7) >> 4);
    ctx.passKindHint = GetPassKindHint();

    const render::RenderPassDescriptor desc = render::BuildRenderPassDescriptor(spec, ctx);
    ApplyDescriptorPSState(desc, vertexInput);

    _activePassId = passId;
    _pipelineVertexInput = vertexInput;
    _activeTexture0 = tex;
    _activeLevel = level;
    _activeSpec = spec;
}

void EngineVK::ApplyDescriptorPSState(const render::RenderPassDescriptor& d, PipelineVertexInput vertexInput)
{
    const bool alphaTest = (d.alpha == render::AlphaMode::Test || d.alpha == render::AlphaMode::TestAndBlend);
    const bool meshVertexInput = vertexInput == PipelineVertexInput::Mesh ||
                                 (vertexInput == PipelineVertexInput::ActivePass && IsIn3DPass());
    const bool a2c = alphaTest && d.alpha == render::AlphaMode::Test && d.blend == render::BlendMode::Opaque &&
                     meshVertexInput;

    _psConstants.alphaRef[0] = static_cast<float>(d.alphaRef) / 255.0f;
    _psConstants.alphaRef[1] = alphaTest ? 1.0f : 0.0f;
    _psConstants.alphaRef[2] = a2c ? 1.0f : 0.0f;
    _psConstants.alphaRef[3] = 0.0f;
}

void EngineVK::BeginPass(Poseidon::PassId passId)
{
    if (IsIn3DPass())
    {
        LOG_DEBUG(Graphics, "VK: BeginPass({}) already in 3D (passId={}), just updating", static_cast<int>(passId),
                  static_cast<int>(_activePassId));
        _activePassId = passId;
        return;
    }
    LOG_DEBUG(Graphics, "VK: BeginPass({}) from ScreenSpace — FULL INIT", static_cast<int>(passId));
    FlushAndFreeAllQueues(_queueNo);
    _activePassId = passId;

    if (GScene)
    {
        _frameState = BuildFrameState(GScene->GetCamera(), GScene->MainLight(), _bias, _fogColor, _sunEnabled);
        _drawItems.clear();
        _currentDrawItem = {};

        std::memcpy(_vsConstants.proj, &_frameState.projection, 64);
        std::memcpy(_vsConstants.view, &_frameState.view, 64);
        std::memcpy(_vsConstants.sunDir, _frameState.sunDir, 16);
        _vsConstants.sunEn[0] = _frameState.sunEnabled ? 1.0f : 0.0f;
        std::memcpy(_vsConstants.fogParam, _frameState.fogParams, 16);
        // Geometry is rendered camera-relative (the world matrix carries
        // objectPos - cameraPos and the view translation is zeroed), so the
        // camera sits at the origin in that space. The transform shader uses
        // camPos for fog distance and specular view direction; passing the
        // absolute camera position made every fog distance huge and fogged the
        // whole world to the fog colour.
        _vsConstants.camPos[0] = 0.0f;
        _vsConstants.camPos[1] = 0.0f;
        _vsConstants.camPos[2] = 0.0f;
        _vsConstants.camPos[3] = 0.0f;

        std::memcpy(_psConstants.fogColor, _frameState.fogColor, 16);
    }
}

void EngineVK::BeginScreenPass()
{
    _vsConstants.vpScale[0] = 2.0f / static_cast<float>(_w);
    _vsConstants.vpScale[1] = 2.0f / static_cast<float>(_h);
    _vsConstants.vpScale[2] = 0.0f;
    _vsConstants.vpScale[3] = 0.0f;

    if (!IsIn3DPass())
        return;
    LOG_DEBUG(Graphics, "VK: BeginScreenPass (was passId={})", static_cast<int>(_activePassId));
    FlushAndFreeAllQueues(_queueNo);
    _activePassId = PassId::ScreenSpace;

    _psConstants.constColor[0] = 1.0f;
    _psConstants.constColor[1] = 1.0f;
    _psConstants.constColor[2] = 1.0f;
    _psConstants.constColor[3] = 1.0f;
}

void EngineVK::DoSwitchRenderMode(RenderMode mode)
{
    FlushAndFreeAllQueues(_queueNo);
    _renderMode = mode;
}

} // namespace Poseidon
