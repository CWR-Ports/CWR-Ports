/*
 * PoseidonVK Vulkan rendering engine bootstrap interface
 */

#ifdef _MSC_VER
#pragma once
#endif

#ifndef __ENGINE_VK_HPP
#define __ENGINE_VK_HPP

#include <Poseidon/Graphics/Core/Engine.hpp>
#include <Poseidon/Graphics/Core/TLVertex.hpp>
#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <Poseidon/Graphics/Shared/WindowMode.hpp>
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <vector>
#include <string>

using namespace Poseidon;

typedef struct VmaAllocator_T* VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

#include <Poseidon/Graphics/Rendering/RenderPassDescriptor.hpp>
#include <unordered_map>

namespace Poseidon
{
class TextureVK;
class TextBankVK;
class VertexBufferVK;

struct PipelineKey
{
    render::RenderPassDescriptor desc;
    int vertexFormat; // 0 = standard TLVertex, 1 = SVertex (mesh)

    bool operator==(const PipelineKey& o) const
    {
        return desc == o.desc && vertexFormat == o.vertexFormat;
    }
};

struct PipelineKeyHash
{
    std::size_t operator()(const PipelineKey& k) const
    {
        const char* p = reinterpret_cast<const char*>(&k.desc);
        std::size_t hash = 5381;
        for (size_t i = 0; i < sizeof(k.desc); ++i)
            hash = ((hash << 5) + hash) ^ p[i];
        hash ^= k.vertexFormat;
        return hash;
    }
};

class EngineVK;

// max number of local lights processed per vertex shader invocation
constexpr int MaxLocalLights = 8;

struct VSConstants
{
    float proj[16];
    float view[16];
    float world[16];
    float sunDir[4];
    float ambient[4];
    float diffuse[4];
    float emissive[4];
    float fogParam[4];
    float camPos[4];
    float specular[4];
    float specEn[4];
    float sunEn[4];
    float vpScale[4];
    float _pad22[4];
    float _pad23[4];
    float texMat0[16];
    float texMat1[16];
    float texCtrl[4];
    float lightCount[4];
    float lightPos[MaxLocalLights][4];
    float lightDiffuse[MaxLocalLights][4];
    float lightAmbient[MaxLocalLights][4];
    float localLightDir[MaxLocalLights][4];
    float lightVP[16];
};

struct WorldInstances
{
    float worldArr[256][16];
};

struct PSConstants
{
    float fogColor[4];
    float alphaRef[4];
    float shadowCtl[4];
    float constColor[4];
    float _pad4[4];
    float _pad5[4];
    float _pad6[4];
    float rgbEyeCoef[4];
    float cascadeVP[4][16];
    float cascadeSplits[4];
    float cascadeCtl[4];
    float camFwd[4];
};

struct SVertex
{
    Vector3P pos;
    Vector3P norm;
    Poseidon::UVPair t0;
};

// entry-point creator for graphicsenginefactory
Engine* CreateEngineVK(int width, int height, bool windowed, int bpp);

enum VertexShaderID
{
    VSScreen,
    VSTransform,
    VSShadow,
    NVertexShaders,
    VSNone = NVertexShaders
};

enum PixelShaderID
{
    PSNormal,
    PSDetail,
    PSGrass,
    PSWater,
    PSFlat,
    PSShadow,
    NPixelShaders,
    PSNone = NPixelShaders
};

struct TriQueue
{
    StaticArray<WORD> _triangleQueue;
    TextureVK* _texture;
    int _level;
    int _special;
    Poseidon::PassId _passId = Poseidon::PassId::Opaque;
    int _lastUsed;
};

enum
{
    MaxTriQueues = 32,
    TriQueueSize = 2048
};

struct QueueVK
{
    int _vertexBufferUsed;
    int _indexBufferUsed;
    int _meshBase, _meshSize;

    TriQueue _tri[MaxTriQueues];
    bool _triUsed[MaxTriQueues];
    int _actTri;

    int _usedCounter;
    bool _firstVertex;
    bool _firstIndex;

    QueueVK();
    int Allocate(TextureVK* tex, int level, int spec, int minI, int maxI, int tip);
    void Free(int i);
};

class EngineVK : public Engine
{
    typedef Engine base;

    friend class SurfaceInfoVK;
    friend class TextBankVK;

protected:
    int _w = 0;
    int _h = 0;
    int _pixelSize = 32;
    int _refreshRate = 60;
    bool _windowed = true;
    Poseidon::WindowMode _windowMode = Poseidon::WindowMode::Borderless;

    SDL_Window* _sdlWindow = nullptr;
    
    // vulkan core handles
    VkInstance _instance = VK_NULL_HANDLE;
    VkPhysicalDevice _physicalDevice = VK_NULL_HANDLE;
    VkDevice _device = VK_NULL_HANDLE;
    VkQueue _graphicsQueue = VK_NULL_HANDLE;
    VkQueue _presentQueue = VK_NULL_HANDLE;
    VkSurfaceKHR _surface = VK_NULL_HANDLE;
    VkSwapchainKHR _swapchain = VK_NULL_HANDLE;
    VkRenderPass _renderPass = VK_NULL_HANDLE;
    VkCommandPool _commandPool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> _commandBuffers;

    // registry for VertexBufferVK mapping from MeshHandle.vao ID
    uint32_t _nextVboId = 1;
    std::unordered_map<uint32_t, VertexBufferVK*> _vboRegistry;

    std::vector<VkImage> _swapchainImages;
    std::vector<VkImageView> _swapchainImageViews;
    std::vector<VkFramebuffer> _swapchainFramebuffers;
    VkFormat _swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D _swapchainExtent = {0, 0};

    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;
    std::vector<VkSemaphore> _imageAvailableSem;
    std::vector<VkSemaphore> _renderFinishedSem;
    std::vector<VkFence> _inFlightFences;
    uint32_t _currentFrame = 0;
    uint32_t _currentImageIndex = 0;
    bool _frameOpen = false;

    int _queueFamilyIndices[2] = {-1, -1};
    VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;
    VmaAllocator _vmaAllocator = VK_NULL_HANDLE;
    bool _vkReady = false;

    VkShaderModule _vsModules[NVertexShaders] = { VK_NULL_HANDLE };
    VkShaderModule _fsModules[NPixelShaders] = { VK_NULL_HANDLE };
    
    VkDescriptorSetLayout _descriptorSetLayoutGlobals = VK_NULL_HANDLE; // set 0 ubos
    VkDescriptorSetLayout _descriptorSetLayoutMaterial = VK_NULL_HANDLE; // set 1 textures
    VkPipelineLayout _pipelineLayout = VK_NULL_HANDLE;
    
    std::unordered_map<PipelineKey, VkPipeline, PipelineKeyHash> _pipelineCache;

    // active ubo structs populated by setmaterial and updateprojection
    VSConstants _vsConstants = {};
    WorldInstances _worldInstances = {};
    PSConstants _psConstants = {};
    
    // dynamic uniform buffer for ubos
    VkBuffer _uniformBuffer = VK_NULL_HANDLE;
    VmaAllocation _uniformAllocation = VK_NULL_HANDLE;
    void* _uniformMapped = nullptr;
    uint32_t _uniformOffset = 0;
    
    // default descriptor set
    VkDescriptorPool _descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet _globalDescriptorSet = VK_NULL_HANDLE;

    TextBankVK* _textBank = nullptr;

    int _bias = 0;
    float _gamma = 1.0f;
    bool _clipANearEnabled = false;
    bool _clipAFarEnabled = false;
    Plane _clipANear;
    Plane _clipAFar;

    int _minGuardX = 0;
    int _maxGuardX = 0;
    int _minGuardY = 0;
    int _maxGuardY = 0;

    // Shadow Maps
    bool EnsureShadowTarget(int res, int layers);
    VkRenderPass _shadowRenderPass = VK_NULL_HANDLE;
    VkImage _shadowImage = VK_NULL_HANDLE;
    VmaAllocation _shadowImageAlloc = VK_NULL_HANDLE;
    VkImageView _shadowImageView = VK_NULL_HANDLE; // array view for sampling
    std::vector<VkImageView> _shadowLayerViews; // per-layer views for rendering
    std::vector<VkFramebuffer> _shadowFramebuffers;
    VkSampler _shadowSampler = VK_NULL_HANDLE;
    VkShaderModule _shadowSolidVertexShader = VK_NULL_HANDLE;
    VkShaderModule _shadowSolidFragmentShader = VK_NULL_HANDLE;
    VkPipelineLayout _shadowPipelineLayout = VK_NULL_HANDLE;
    VkPipeline _shadowSolidPipeline = VK_NULL_HANDLE;
    VkRenderPass _shadowPipelineRenderPass = VK_NULL_HANDLE;
    ShadowMapTuning _shadowTuning = {};
    float _shadowSunFactor = 1.0f;
    bool _shadowMapActive = false;
    int _shadowMapRes = 0;
    int _shadowCascades = 0;
    int _shadowOmniCount = 0;
    float _shadowMapVP[4 * 16] = {0};
    float _shadowSplits[4] = {0};
    float _shadowCamFwd[3] = {0};

    QueueVK _queueNo;
    std::vector<TLVertex> _vboMirror;
    int _vboUploadedVerts = 0;
    bool _enableReorder = true;
    int _dbgQueueFanCalls = 0;
    int _dbgTotalFanTris = 0;
    int _dbgAddVerticesCalls = 0;
    int _dbgTotalVertices = 0;
    int _prepSpec = 0;
    TLVertexTable* _mesh = nullptr;

    enum RenderMode
    {
        RMUnknown,
        RM2DTris,
        RMTris,
        RMLines
    };
    RenderMode _renderMode = RMTris;
    void SwitchRenderMode(RenderMode mode)
    {
        if (_renderMode == mode)
            return;
        DoSwitchRenderMode(mode);
    }
    void DoSwitchRenderMode(RenderMode mode);
    
    void BeginPass(Poseidon::PassId passId);
    void BeginScreenPass();
    void DiscardVB();
    void AddVertices(const TLVertex* v, int n);
    void UploadPendingVertices();
    enum class PipelineVertexInput
    {
        ActivePass,
        Screen,
        Mesh
    };
    PipelineVertexInput _pipelineVertexInput = PipelineVertexInput::ActivePass;

    void ApplyPassState(TextureVK* tex, int level, const Poseidon::render::LegacySpec& spec, Poseidon::PassId passId, PipelineVertexInput vertexInput);

    WORD* QueueAdd(QueueVK& queue, int n);
    void QueueFan(const VertexIndex* ii, int n);
    void Queue2DPoly(const TLVertex* v, int n);
    void FlushQueue(QueueVK& queue, int index);
    void FlushAndFreeQueue(QueueVK& queue, int index);
    int AllocateQueue(QueueVK& queue, TextureVK* tex, int level, int spec);
    void FreeQueue(QueueVK& queue, int index);
    void FreeAllQueues(QueueVK& queue);
    void FlushAndFreeAllQueues(QueueVK& queue, bool nonEmptyOnly = false);
    void FlushAllQueues(QueueVK& queue, int skip = -1);
    void CloseAllQueues(QueueVK& queue);
    void QueuePrepareTriangle(const Poseidon::MipInfo& absMip, int specFlags);
    void FlushQueues() override;
    void EnableReorderQueues(bool enableReorder) override;

public:
    EngineVK(int width, int height, bool windowed, int bpp);
    ~EngineVK() override;

    /// engine core setup & lifecycle ///
    bool InitDrawDone() override;
    bool IsAbleToDraw() override;
    void InitDraw(bool clear = false, PackedColor color = PackedColor(0)) override;
    void FinishDraw() override;
    void NextFrame() override;

    void RegisterVertexBuffer(VertexBufferVK* buf, uint32_t& id);
    void UnregisterVertexBuffer(uint32_t id);
    VertexBufferVK* GetVertexBuffer(uint32_t id);

    void DrawTestPattern(const char* name) override;

    void Pause() override;
    void Restore() override;
    void ResetForRemount() override;

    bool SwitchRes(int w, int h, int bpp) override;
    bool SwitchRefreshRate(int refresh) override;

    void EnableSunLight(bool enable) override;
    void SetMaterial(const Poseidon::TLMaterial& mat, const LightList& lights, const Poseidon::render::LegacySpec& spec) override;
    void UpdateProjection() override {}
    
    // overrides needed for draw
    void SetGrassParams(float a1, float a2, float a3 = 0, float a4 = 0) override {}
    void SetShadowMapSunFactor(float factor01) override;
    void BeginShadowPass() override;
    void EndShadowPass() override;
    bool ShadowDepthProbe(const float* lightVP16, const float* triXYZ, int vertCount, int res, float* outDepth) override;
    bool ShadowMapCacheSelfTest() override;
    void SetShadowMapsEnabled(bool enabled) override;
    bool ShadowMapsEnabled() const override;
    ShadowMapTuning GetShadowMapTuning() const override;
    void SetShadowMapTuning(const ShadowMapTuning& tuning) override;
    void RenderShadowDepthScene(const float* lightVPs, const float* splitViewDist, const float* camFwd3, int numCascades, int omniCount, int res, const ShadowCasterSet& casters) override;
    bool DumpShadowMap(const char* path) override;
    bool SetWindowMode(Poseidon::WindowMode mode) override;
    Poseidon::WindowMode GetCurrentWindowMode() const override;
    void OnWindowResized(int w, int h) override;
    void OnFullscreenChanged(bool windowed) override;

    void ListResolutions(FindArray<ResolutionInfo>& ret) override;
    void ListRefreshRates(FindArray<int>& ret) override;
    void ListMonitors(FindArray<MonitorInfo>& ret) override;
    int GetCurrentMonitor() const override;
    bool SwitchMonitor(int idx) override;
    bool GetDesktopDisplayMode(int& w, int& h, int& refresh) const override;
    bool GetCurrentDisplayMode(int& w, int& h, int& refresh) const override;
    bool GetRequestedFullscreenMode(int& w, int& h, int& refresh) const override;
    bool SetSwapInterval(int interval) override;
    int GetSwapInterval() const override;

    /// capability queries ///
    RString GetDebugName() const override;
    RString GetRendererName() const override;
    int Width() const override { return _w; }
    int Height() const override { return _h; }
    int PixelSize() const override { return _pixelSize; }
    int RefreshRate() const override { return _refreshRate; }
    bool CanBeWindowed() const override { return true; }
    bool IsWindowed() const override { return _windowed; }
    bool IsResizable() const override;

    int MinGuardX() const override { return _minGuardX; }
    int MaxGuardX() const override { return _maxGuardX; }
    int MinGuardY() const override { return _minGuardY; }
    int MaxGuardY() const override { return _maxGuardY; }

    int MinSatX() const override { return _minGuardX; }
    int MaxSatX() const override { return _maxGuardX; }
    int MinSatY() const override { return _minGuardY; }
    int MaxSatY() const override { return _maxGuardY; }

    int AFrameTime() const override;

    /// drawing overrides ///
    void Clear(bool clearZ = true, bool clear = true, PackedColor color = PackedColor(0)) override;
    VertexBuffer* CreateVertexBuffer(const Shape& src, VBType type) override;
    int CompareBuffers(const Shape& a, const Shape& b) override;

    void DrawDecal(Vector3Par pos, float rhw, float sizeX, float sizeY, PackedColor col, const MipInfo& mip, int specFlags) override;
    void DrawPolygon(const VertexIndex* i, int n) override;
    void DrawSection(const FaceArray& face, Offset beg, Offset end) override;
    void DrawPoints(int beg, int end) override;
    void DrawPoints(const TLVertex* vs, int nVertex);
    bool CanGrass() const override;

    void Draw2D(const Draw2DPars& pars, const Rect2DAbs& rect, const Rect2DAbs& clip = Rect2DClipAbs) override;
    void DrawPoly(const MipInfo& mip, const Vertex2DAbs* vertices, int nVertices, const Rect2DAbs& clip = Rect2DClipAbs, int specFlags = DefSpecFlags2D) override;
    void DrawPoly(const MipInfo& mip, const Vertex2DPixel* vertices, int nVertices, const Rect2DPixel& clip = Rect2DClipPixel, int specFlags = DefSpecFlags2D) override;
    void DrawLine(const Line2DAbs& rect, PackedColor c0, PackedColor c1, const Rect2DAbs& clip = Rect2DClipAbs) override;
    void DrawLine(int beg, int end) override;

    /// mesh pipeline ///
    void PrepareMesh(const render::LegacySpec& spec) override;
    void BeginMesh(TLVertexTable& mesh, const render::LegacySpec& spec) override;
    void EndMesh(TLVertexTable& mesh) override;
    void PrepareTriangle(const MipInfo& mip, int specFlags) override;
    void PrepareTriangleTL(const Poseidon::MipInfo& mip, const Poseidon::render::LegacySpec& spec) override;
    
    bool InstancedRunAdd(const Matrix4& modelToWorld) override;
    void BeginInstancedRunUpload() override;
    void PrepareMeshTL(const LightList& lights, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec) override;
    void PrepareMeshTLImpl(const FrameState& frame, const Matrix4& modelToWorld, const Poseidon::render::LegacySpec& spec);
    void BeginMeshTL(const Shape& sMesh, int spec, bool dynamic) override;
    void EndMeshTL(const Shape& sMesh) override;

    /// settings & states ///
    void FogColorChanged(ColorVal fogColor) override;
    void SetGamma(float g) override;
    float GetGamma() const override { return _gamma; }
    void SetBias(int value) override;
    int GetBias() override { return _bias; }
    float ZShadowEpsilon() const override;
    float ZRoadEpsilon() const override;
    float ObjMipmapCoef() const override;
    void GetZCoefs(float& zAdd, float& zMult) override;
    bool CanZBias() const override;
    bool ZBiasExclusion() const override;

    /// resources & text bank ///
    AbstractTextBank* TextBank() override;
    void TextureDestroyed(Texture* tex) override;

    /// event handling ///
    void HandleEvents() override;
    bool IsOpen() const override;
    void SetMouseGrab(bool grab) override;
    bool IsMouseGrabbed() const override;

    /// optional overrides ///
    void EmitDraw(const render::frame::Draw& d) override;

    /// State Management ///
    VkPipeline GetOrCreatePipeline(const PipelineKey& key);
    void ClearPipelineCache();

private:
    void InitShaders();
    void DeinitShaders();
    
    void InitPipelineLayouts();
    void DeinitPipelineLayouts();

    void InitVulkan();
    void ShutdownVulkan();
    bool CreateVkInstance();
    bool CreateVkSurface();
    bool PickPhysicalDevice();
    bool CreateLogicalDevice();
    bool CreateSwapchain();
    bool CreateRenderPass();
    bool CreateFramebuffers();
    bool CreateCommandPool();
    bool CreateSyncObjects();
};
}

#endif // __ENGINE_VK_HPP
