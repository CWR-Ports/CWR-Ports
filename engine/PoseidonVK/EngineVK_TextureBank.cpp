#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/EngineVK.hpp>

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
    // normally would load from FileServer and create TextureVK.
    return nullptr;
}

Ref<Texture> TextBankVK::LoadInterpolated(RStringB n1, RStringB n2, float factor)
{
    return nullptr;
}

void TextBankVK::ForceReloadAll() {}

Texture* TextBankVK::CreateDynamic(int w, int h, const void* rgba, uint32_t size, bool mipmap)
{
    return nullptr;
}

void TextBankVK::UpdateDynamic(Texture* tex, const void* rgba, uint32_t size) {}

void TextBankVK::FlushTextures() {}
void TextBankVK::FlushBank(QFBank* bank) {}

bool TextBankVK::VerifyChecksums() { return true; }

int TextBankVK::Find(RStringB name, TextureVK* interpolate) { return -1; }
int TextBankVK::FindFree() { return -1; }
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
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
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
    return MipInfo();
}

} // namespace Poseidon
