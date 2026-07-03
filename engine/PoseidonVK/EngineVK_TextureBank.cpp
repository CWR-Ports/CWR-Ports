#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/EngineVK.hpp>

#include <Poseidon/Graphics/Textures/LooseTextures.hpp>
#include <Poseidon/Foundation/Framework/Log.hpp>
#include <Poseidon/Foundation/Common/FltOpts.hpp>

namespace Poseidon {

TextBankVK::TextBankVK(EngineVK* engine) : _engine(engine), _totalAllocated(0)
{
}

TextBankVK::~TextBankVK()
{
    ReleaseAllTextures();
}

void TextBankVK::StartFrame() {}
void TextBankVK::FinishFrame() {}
void TextBankVK::Compact() {}
void TextBankVK::Preload() {}

Ref<Texture> TextBankVK::Load(RStringB name)
{
    int i = Find(name, nullptr);
    if (i >= 0)
        return _textures[i].GetRef();

    RString resolved = Poseidon::Graphics::ResolveLooseTexturePath(name);
    ITextureSourceFactory* factory = SelectTextureSourceFactory(resolved);
    if (!factory || !factory->Check(resolved))
    {
        const char* cname = static_cast<const char*>(name);
        if (cname && cname[0] && cname[0] != '#')
            LOG_WARN(Graphics, "VK: Cannot load texture {}", cname);
        else
            LOG_DEBUG(Graphics, "VK: Cannot load texture {}", cname);
        return nullptr;
    }

    int iFree = FindFree();
    PoseidonAssert(iFree >= 0);

    Ref<TextureVK> texture = new TextureVK;
    if (!texture)
        return nullptr;

    texture->SetName(name);
    texture->_src = factory->Create(resolved, texture->_mipmaps, MAX_MIPMAPS);
    if (!texture->_src)
    {
        LOG_WARN(Graphics, "VK: Cannot load texture {}", static_cast<const char*>(name));
        return nullptr;
    }

    texture->_maxSize = 0x10000;
    if (texture->_src->GetFormat() == PacARGB4444 || texture->_src->GetFormat() == PacAI88 ||
        texture->_src->GetFormat() == PacARGB8888)
    {
        texture->_src->ForceAlpha();
    }

    int nMipmaps = texture->_src->GetMipmapCount();
    int iMip = 0;
    for (; iMip < nMipmaps; ++iMip)
    {
        PacLevelMem& mip = texture->_mipmaps[iMip];
        PacFormat srcFmt = texture->_src->GetFormat();
        if (srcFmt >= PacFormat::PacDXT1 && srcFmt <= PacFormat::PacDXT5)
        {
            mip.SetDestFormat(srcFmt, 8);
        }
        else if (srcFmt == PacFormat::PacP8)
        {
            mip.SetDestFormat(PacFormat::PacARGB1555, 8);
        }
        else
        {
            mip.SetDestFormat(srcFmt, 8);
        }
        if (mip._w < 4 || mip._h < 4)
            break;
    }

    texture->_initialized = true;
    texture->_nMipmaps = iMip;

    _textures.Access(iFree);
    _textures[iFree] = texture;
    return texture.GetRef();
}

Ref<Texture> TextBankVK::LoadInterpolated(RStringB n1, RStringB n2, float factor)
{
    return nullptr;
}

void TextBankVK::ForceReloadAll() {}

Texture* TextBankVK::CreateDynamic(int w, int h, const void* rgba, uint32_t size, bool mipmap)
{
    int idx = FindFree();
    TextureVK* tex = new TextureVK();
    _textures.Access(idx);
    _textures[idx] = tex;
    if (!tex->InitFromRGBA(w, h, rgba, size, mipmap))
    {
        LOG_WARN(Graphics, "VK: Failed to create dynamic texture {}x{}", w, h);
        return nullptr;
    }
    return tex;
}

void TextBankVK::UpdateDynamic(Texture* tex, const void* rgba, uint32_t size)
{
    if (!tex || !rgba)
        return;
    static_cast<TextureVK*>(tex)->UpdateRGBA(rgba, size);
}

void TextBankVK::FlushTextures() {}
void TextBankVK::FlushBank(QFBank* bank) {}

bool TextBankVK::VerifyChecksums() { return true; }

int TextBankVK::Find(RStringB name, TextureVK* interpolate)
{
    for (int i = 0; i < _textures.Size(); i++)
    {
        TextureVK* texture = _textures[i];
        if (texture && texture->GetName() == name && texture->_interpolate == interpolate)
            return i;
    }
    return -1;
}

int TextBankVK::FindFree()
{
    for (int i = 0; i < _textures.Size(); i++)
    {
        if (!_textures[i])
            return i;
    }
    return _textures.Size();
}
TextureVK* TextBankVK::Copy(int from) { return nullptr; }

void TextBankVK::ReleaseAllTextures() {}
bool TextBankVK::ReserveMemory(VKSysCacheRoot& root, int limit) { return true; }
bool TextBankVK::ReserveMemory(int size) { return true; }
bool TextBankVK::ForcedReserveMemory(int size) { return true; }

int TextBankVK::FreeTextureMemory() { return 0; }
void TextBankVK::CheckTextureMemory() {}

int TextBankVK::FindSurface(int w, int h, int nMipmaps, PacFormat format, SurfaceInfoVK& result) { return -1; }
void TextBankVK::DeleteLastReleased() {}
void TextBankVK::AddReleased(SurfaceInfoVK& surf) {}
void TextBankVK::UseReleased(SurfaceInfoVK& surf, const TextureDescVK& desc, PacFormat format) {}
void TextBankVK::Reuse(SurfaceInfoVK& surf, const TextureDescVK& desc, PacFormat format) {}

int TextBankVK::CreateGPUSurface(SurfaceInfoVK& surface, const TextureDescVK& desc, PacFormat format, int totalSize)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = desc.w;
    imageInfo.extent.height = desc.h;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = desc.nMipmaps;
    imageInfo.arrayLayers = 1;
    imageInfo.format = desc.format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    
    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;

    if (vmaCreateImage(_engine->_vmaAllocator, &imageInfo, &allocInfo, &image, &allocation, nullptr) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK: failed to create image");
        return -1;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = desc.format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.nMipmaps;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(_engine->_device, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK: failed to create image view");
        vmaDestroyImage(_engine->_vmaAllocator, image, allocation);
        return -1;
    }
    
    surface.SetHandles(image, imageView, allocation);
    return 1;
}

MipInfo TextBankVK::UseMipmap(Texture* absTexture, int level, int top)
{
    if (!absTexture)
        return MipInfo(nullptr, 0);

    TextureVK* texture = static_cast<TextureVK*>(absTexture);

    if (!texture->_src && texture->GetSurface().GetImage() != VK_NULL_HANDLE)
        return MipInfo(texture, 0);

    saturateMin(level, texture->_mipmapNeeded);
    saturateMin(top, texture->_mipmapWanted);

    if (level < 0)
        level = 0;

    saturateMin(level, texture->_nMipmaps - 1);
    saturateMax(top, texture->_largestUsed);
    saturateMin(top, level);
    saturateMax(level, top);

    if (texture->_levelLoaded > level)
    {
        if (texture->_src)
        {
            texture->LoadLevels(top);
        }
    }

    int loadedLevel = texture->_levelLoaded >= 0 ? texture->_levelLoaded : 0;
    return MipInfo(texture, loadedLevel);
}

} // namespace Poseidon
