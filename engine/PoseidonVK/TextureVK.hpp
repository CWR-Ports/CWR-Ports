#ifndef __TEXTURE_VK_HPP
#define __TEXTURE_VK_HPP

#include <Poseidon/Graphics/Textures/TextureBank.hpp>
#include <Poseidon/Graphics/Rendering/Colors.hpp>
#include <Poseidon/Foundation/Memory/MemFreeReq.hpp>
#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"

#include <Poseidon/Foundation/Containers/CacheList.hpp>

namespace Poseidon {

class TextureVK;
class EngineVK;
class TextBankVK;

class HMipCacheVK : public CLDLink
{
  public:
    TextureVK* texture;
    USE_FAST_ALLOCATOR;
};

typedef HMipCacheVK HSysCacheVK;
typedef CLList<HMipCacheVK> VKMipCacheRoot;
typedef VKMipCacheRoot VKSysCacheRoot;

struct TextureDescVK
{
    int w, h;
    int nMipmaps;
    VkFormat format;
    bool compressed;
};

class SurfaceInfoVK
{
  private:
    VkImage _image = VK_NULL_HANDLE;
    VkImageView _imageView = VK_NULL_HANDLE;
    VmaAllocation _allocation = VK_NULL_HANDLE;

    static int _nextId;
    int _id;

  public:
    int _totalSize;
    int _usedSize;
    int _w, _h;
    int _nMipmaps;
    Poseidon::PacFormat _format;
    
    SurfaceInfoVK() : _totalSize(0), _usedSize(0), _w(0), _h(0), _nMipmaps(0), _format(PacFormat::PacARGB8888) {}

    int SizeExpected() const { return _totalSize; }
    int SizeUsed() const { return _usedSize; }
    int GetCreationID() const { return _id; }

    static int CalculateSize(const TextureDescVK& desc, Poseidon::PacFormat format, int totalSize = -1);
    int CreateSurface(const TextureDescVK& desc, Poseidon::PacFormat format, int totalSize = -1);
    void Free(bool lastRef, int refValue = 0);

    VkImage GetImage() const { return _image; }
    VkImageView GetImageView() const { return _imageView; }
    void SetHandles(VkImage img, VkImageView view, VmaAllocation alloc)
    {
        _image = img;
        _imageView = view;
        _allocation = alloc;
    }
};

class TextureVK : public Texture
{
    typedef Texture base;

    friend class TextBankVK;
    friend class EngineVK;

  private:
    SRef<ITextureSource> _src;

    Ref<TextureVK> _interpolate;
    float _iFactor;

    bool _isDetail;
    bool _useDetail;
    bool _initialized;
    bool _dynamicMipmapped = false;

    int _maxSize;
    int _nMipmaps;
    PacLevelMem _mipmaps[MAX_MIPMAPS];

    SurfaceInfoVK _surface;
    SurfaceInfoVK _smallSurface;

    signed char _alphaClass = -1;
    signed char _largestUsed;
    signed char _smallLoaded;
    signed char _levelLoaded;
    signed char _levelNeededThisFrame;
    signed char _levelNeededLastFrame;
    signed char _inUse;

    HMipCacheVK* _cache;

    VkImageView GetSmallHandle() const { return _smallSurface.GetImageView(); }
    VkImageView GetBigHandle() const { return _surface.GetImageView(); }

    int LevelNeeded() const
    {
        return (_levelNeededThisFrame < _levelNeededLastFrame ? _levelNeededThisFrame : _levelNeededLastFrame);
    }

  public:
    TextureVK();
    ~TextureVK() override;

    void InitDesc(TextureDescVK& desc, int levelMin, bool enableDXT);
    int LoadLevels(int levelMin);
    int UploadToGPU(SurfaceInfoVK& surface, int levelMin);

    VkImageView GetHandle() const
    {
        VkImageView handle = GetBigHandle();
        return handle != VK_NULL_HANDLE ? handle : GetSmallHandle();
    }
    const SurfaceInfoVK& GetSurface() const { return _surface.GetImage() != VK_NULL_HANDLE ? _surface : _smallSurface; }

  private:
    void MemoryReleased();
    Poseidon::AlphaStats::Kind ScanTopMipAlphaClass();
    int TotalSize(int levelMin) const;
    void ReleaseSmall(bool store = false);
    int LoadSmall();

  public:
    void ReleaseMemory(bool store = false);
    void ReuseMemory(SurfaceInfoVK& surf);

    bool IsAlpha() const override
    {
        PoseidonAssert(_initialized);
        return _src && _src->IsAlpha();
    }
    int AMaxSize() const override { return _maxSize; }
    void SetMaxSize(int size) override;
    void SetMultitexturing(int type) override;

    bool VerifyChecksum(const Poseidon::MipInfo& mip) const override;
    
    // Abstract overrides
    int AWidth(int level = 0) const override;
    int AHeight(int level = 0) const override;
    
    int ANMipmaps() const override { return _nMipmaps; }
    Poseidon::AbstractMipmapLevel& AMipmap(int level) override;
    const Poseidon::AbstractMipmapLevel& AMipmap(int level) const override;
    void ASetNMipmaps(int n) override { _nMipmaps = n; }
    Poseidon::Color GetPixel(int level, float u, float v) const override;
    bool IsTransparent() const override { return false; }
    Poseidon::Color GetColor() override { return Poseidon::Color(0); }
    Poseidon::AlphaStats::Kind GetAlphaClass() override;

    int Size() const;
    bool InitFromRGBA(int w, int h, const void* rgba, uint32_t size, bool mipmap = false);
    void UpdateRGBA(const void* rgba, uint32_t size);
};

} // namespace Poseidon

#endif // __TEXTURE_VK_HPP
