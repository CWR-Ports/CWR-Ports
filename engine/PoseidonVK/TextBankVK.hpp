#ifndef __TEXTBANK_VK_HPP
#define __TEXTBANK_VK_HPP

#include <PoseidonVK/TextureVK.hpp>

namespace Poseidon {

class TextBankVK : public AbstractTextBank
{
    typedef AbstractTextBank base;
    
  private:
    EngineVK* _engine;
    AutoArray<Ref<TextureVK>> _textures;
    
    int _totalAllocated = 0;
    int _totalLimit = 512 * 1024 * 1024; // 512MB default limit

    VKSysCacheRoot _levelSysRoot;
    VKSysCacheRoot _smallSysRoot;
    VKSysCacheRoot _lastFrameLevelSysRoot;

    AutoArray<SurfaceInfoVK> _freeSurfaces;

  public:
    TextBankVK(EngineVK* engine);
    ~TextBankVK() override;

    void StartFrame() override;
    void FinishFrame() override;
    void Compact() override;
    void Preload() override;

    Ref<Texture> Load(RStringB name) override;
    Ref<Texture> LoadInterpolated(RStringB n1, RStringB n2, float factor) override;

    void ForceReloadAll() override;
    
    int NTextures() const override { return _textures.Size(); }
    Texture* GetTexture(int i) const override { return _textures[i]; }

    Texture* CreateDynamic(int w, int h, const void* rgba, uint32_t size, bool mipmap) override;
    void UpdateDynamic(Texture* tex, const void* rgba, uint32_t size) override;

    void FlushTextures();
    void FlushBank(QFBank* bank);

    bool VerifyChecksums();
    
    int GetTotalAllocated() const { return _totalAllocated; }

  private:
    int Find(RStringB name, TextureVK* interpolate);
    int FindFree();
    TextureVK* Copy(int from);

    void ReleaseAllTextures();
    bool ReserveMemory(VKSysCacheRoot& root, int limit);
    bool ReserveMemory(int size);
    bool ForcedReserveMemory(int size);

    int FreeTextureMemory();
    void CheckTextureMemory();

    int FindSurface(int w, int h, int nMipmaps, PacFormat format, SurfaceInfoVK& result);
    void DeleteLastReleased();
    void AddReleased(SurfaceInfoVK& surf);
    void UseReleased(SurfaceInfoVK& surf, const TextureDescVK& desc, PacFormat format);
    void Reuse(SurfaceInfoVK& surf, const TextureDescVK& desc, PacFormat format);

  public:
    int CreateGPUSurface(SurfaceInfoVK& surface, const TextureDescVK& desc, PacFormat format, int totalSize = -1);
    
    MipInfo UseMipmap(Texture* absTexture, int level, int top);
};

} // namespace Poseidon

#endif // __TEXTBANK_VK_HPP
