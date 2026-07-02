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

namespace Poseidon
{
class TextureVK;
class TextBankVK;
class EngineVK;

// entry-point creator for GraphicsEngineFactory
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

class EngineVK : public Engine
{
    typedef Engine base;

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

    int _queueFamilyIndices[2] = {-1, -1};
    VkDebugUtilsMessengerEXT _debugMessenger = VK_NULL_HANDLE;
    bool _vkReady = false;

    VkShaderModule _vsModules[NVertexShaders] = { VK_NULL_HANDLE };
    VkShaderModule _fsModules[NPixelShaders] = { VK_NULL_HANDLE };

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

public:
    EngineVK(int width, int height, bool windowed, int bpp);
    ~EngineVK() override;

    /// engine core setup & lifecycle ///
    bool InitDrawDone() override;
    bool IsAbleToDraw() override;
    void InitDraw(bool clear = false, PackedColor color = PackedColor(0)) override;
    void FinishDraw() override;
    void NextFrame() override;
    void DrawTestPattern(const char* name) override;

    void Pause() override;
    void Restore() override;
    void ResetForRemount() override;

    bool SwitchRes(int w, int h, int bpp) override;
    bool SwitchRefreshRate(int refresh) override;
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
    void DrawDecal(Vector3Par pos, float rhw, float sizeX, float sizeY, PackedColor col, const MipInfo& mip, int specFlags) override;
    void DrawPolygon(const VertexIndex* i, int n) override;
    void DrawSection(const FaceArray& face, Offset beg, Offset end) override;
    void DrawPoints(int beg, int end) override;

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

private:
    void InitShaders();
    void DeinitShaders();

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
