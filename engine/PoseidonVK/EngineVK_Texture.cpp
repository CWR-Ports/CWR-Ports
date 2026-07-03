#include <PoseidonVK/TextureVK.hpp>
#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Core/Application.hpp>
#include <Poseidon/Graphics/Core/MipmapLayout.hpp>
#include <vector>
#include <cstring>

namespace Poseidon {

int SurfaceInfoVK::_nextId = 1;

int SurfaceInfoVK::CalculateSize(const TextureDescVK& desc, Poseidon::PacFormat format, int totalSize)
{
    if (totalSize > 0)
        return totalSize;
    int bpp = (format == PacFormat::PacDXT1) ? 4 : 
              (format == PacFormat::PacDXT5) ? 8 : 32;
    int size = 0;
    int w = desc.w;
    int h = desc.h;
    for (int i = 0; i < desc.nMipmaps; i++)
    {
        size += (w * h * bpp) / 8;
        if (w > 1) w /= 2;
        if (h > 1) h /= 2;
    }
    return size;
}

int SurfaceInfoVK::CreateSurface(const TextureDescVK& desc, Poseidon::PacFormat format, int totalSize)
{
    _id = _nextId++;
    _w = desc.w;
    _h = desc.h;
    _nMipmaps = desc.nMipmaps;
    _format = format;
    _totalSize = CalculateSize(desc, format, totalSize);
    _usedSize = _totalSize;

    EngineVK* engine = static_cast<EngineVK*>(Poseidon::GEngine);
    TextBankVK* bank = static_cast<TextBankVK*>(engine->TextBank());
    
    return bank->CreateGPUSurface(*this, desc, format, totalSize);
}

void SurfaceInfoVK::Free(bool lastRef, int refValue)
{
    if (_image != VK_NULL_HANDLE)
    {
        EngineVK* engine = static_cast<EngineVK*>(Poseidon::GEngine);
        if (engine && engine->_vmaAllocator)
        {
            vkDestroyImageView(engine->_device, _imageView, nullptr);
            vmaDestroyImage(engine->_vmaAllocator, _image, _allocation);
        }
        _image = VK_NULL_HANDLE;
        _imageView = VK_NULL_HANDLE;
        _allocation = VK_NULL_HANDLE;
    }
}

TextureVK::TextureVK()
    : _iFactor(0), _isDetail(false), _useDetail(false), _initialized(false), _maxSize(4096),
      _nMipmaps(0), _alphaClass(-1), _largestUsed(-1), _smallLoaded(-1), _levelLoaded(-1),
      _levelNeededThisFrame(-1), _levelNeededLastFrame(-1), _inUse(0), _cache(nullptr)
{
}

TextureVK::~TextureVK()
{
    ReleaseMemory();
}

void TextureVK::InitDesc(TextureDescVK& desc, int levelMin, bool enableDXT)
{
    desc.w = _mipmaps[levelMin]._w;
    desc.h = _mipmaps[levelMin]._h;
    desc.nMipmaps = _src->GetMipmapCount() - levelMin;
    
    PacFormat dFormat = _mipmaps[levelMin].DstFormat();
    desc.compressed = enableDXT && (dFormat >= PacFormat::PacDXT1 && dFormat <= PacFormat::PacDXT5);
    
    if (desc.compressed)
    {
        desc.format = (dFormat == PacFormat::PacDXT1) ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
    }
    else
    {
        if (dFormat == PacFormat::PacARGB1555)
            desc.format = VK_FORMAT_A1R5G5B5_UNORM_PACK16;
        else if (dFormat == PacFormat::PacRGB565)
            desc.format = VK_FORMAT_R5G6B5_UNORM_PACK16;
        else if (dFormat == PacFormat::PacARGB4444)
            desc.format = VK_FORMAT_B4G4R4A4_UNORM_PACK16; // Use BGRA4444 which matches little-endian 4444
        else if (dFormat == PacFormat::PacAI88)
            desc.format = VK_FORMAT_R8G8_UNORM; // Assuming we use a swizzle or just use RG
        else
            desc.format = VK_FORMAT_R8G8B8A8_UNORM; // PacARGB8888
    }
}

int TextureVK::LoadLevels(int levelMin)
{
    if (levelMin < 0)
        return 0;
    if (levelMin >= _nMipmaps)
        levelMin = _nMipmaps - 1;
    if (levelMin < 0)
        return -1;

   LOG_INFO(Graphics, "[TextureVK::LoadLevels] Name: {} | Requested levelMin: {} | Current _levelLoaded: {}", 
            GetName() ? static_cast<const char*>(GetName()) : "Unknown", levelMin, _levelLoaded);
    if (_levelLoaded >= 0 && _levelLoaded <= levelMin)
        return 0;
        
    TextureDescVK desc;
    InitDesc(desc, levelMin, true);
    
    _surface.CreateSurface(desc, _src->GetFormat(), TotalSize(levelMin));
    EngineVK* engine = static_cast<EngineVK*>(Poseidon::GEngine);
    if (engine)
    {
        engine->_textureRegistry[_surface.GetCreationID()] = this;
    }
    int ret = UploadToGPU(_surface, levelMin);
    
    _levelLoaded = levelMin;
    _initialized = true;
    
    return ret;
}

int TextureVK::UploadToGPU(SurfaceInfoVK& surface, int levelMin)
{
    if (!_src)
    {
        LOG_ERROR(Graphics, "No texture source for upload");
        return -1;
    }

    VkImage image = surface.GetImage();
    if (image == VK_NULL_HANDLE)
        return -1;

    EngineVK* engine = static_cast<EngineVK*>(Poseidon::GEngine);
    VkDevice device = engine->_device;
    VkQueue queue = engine->_graphicsQueue;
    VkCommandPool pool = engine->_commandPool;
    VmaAllocator allocator = engine->_vmaAllocator;

    int totalSize = TotalSize(levelMin);
    if (totalSize <= 0)
        return -1;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = totalSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo stagingAllocInfo{};
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &stagingAllocInfo) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK: Failed to create staging buffer for texture upload");
        return -1;
    }

    uint8_t* mappedData = static_cast<uint8_t*>(stagingAllocInfo.pMappedData);
    if (!mappedData)
    {
        if (vmaMapMemory(allocator, stagingAllocation, reinterpret_cast<void**>(&mappedData)) != VK_SUCCESS)
        {
            LOG_ERROR(Graphics, "VK: Failed to map staging buffer memory");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return -1;
        }
    }

    std::vector<VkBufferImageCopy> bufferCopyRegions;
    VkDeviceSize currentOffset = 0;

    PacFormat sharedFmt = _mipmaps[levelMin].DstFormat();

    for (int i = levelMin; i < _src->GetMipmapCount(); i++)
    {
        PacLevelMem& mip = _mipmaps[i];
        int aLevel = i - levelMin;

        PacFormat dstFmt = sharedFmt;
        const auto layout = Poseidon::render::mipmap::ComputeLayout(dstFmt, mip._w, mip._h);
        int dataSize = layout.dataSize;

        int ret = _src->GetMipmapData(mappedData + currentOffset, mip, i);
        if (_interpolate)
        {
            PoseidonAssert(_interpolate->_nMipmaps == _nMipmaps);
            PacLevelMem& imip = _interpolate->_mipmaps[i];
            
            std::vector<short> imem(imip._w * imip._h);
            _interpolate->_src->GetMipmapData(imem.data(), imip, i);
            mip.Interpolate(mappedData + currentOffset, imem.data(), imip, _iFactor);
        }
        if (!ret)
        {
            std::memset(mappedData + currentOffset, 0, dataSize);
            LOG_WARN(Graphics, "VK: Cannot load mipmap {} level {}", static_cast<const char*>(GetName()), i);
        }

        VkBufferImageCopy region{};
        region.bufferOffset = currentOffset;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = aLevel;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            static_cast<uint32_t>(mip._w),
            static_cast<uint32_t>(mip._h),
            1
        };

        bufferCopyRegions.push_back(region);
        currentOffset += dataSize;
    }

    if (!stagingAllocInfo.pMappedData)
    {
        vmaUnmapMemory(allocator, stagingAllocation);
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = pool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    if (vkAllocateCommandBuffers(device, &cmdAllocInfo, &cb) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK: Failed to allocate command buffer for texture copy");
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return -1;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = surface._nMipmaps;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkCmdCopyBufferToImage(cb, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(bufferCopyRegions.size()), bufferCopyRegions.data());

    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    vkEndCommandBuffer(cb);

    LOG_INFO(Graphics, "[TextureVK::UploadToGPU] Submitting copy to queue for {} | Total Size: {} | Regions: {}", 
            GetName() ? static_cast<const char*>(GetName()) : "Unknown", totalSize, bufferCopyRegions.size());

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;

    if (vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK: Failed to submit texture copy command buffer");
        vkFreeCommandBuffers(device, pool, 1, &cb);
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        return -1;
    }

    vkQueueWaitIdle(queue);
    vkFreeCommandBuffers(device, pool, 1, &cb);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

    return totalSize;
}

void TextureVK::ReleaseMemory(bool store)
{
    if (_surface.GetImage())
    {
        _surface.Free(true);
    }
    _levelLoaded = -1;
    MemoryReleased();
}

void TextureVK::ReuseMemory(SurfaceInfoVK& surf)
{
    ReleaseMemory();
    _surface = surf;
    surf.SetHandles(VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE);
}

void TextureVK::MemoryReleased()
{
    _initialized = false;
}

Poseidon::AlphaStats::Kind TextureVK::ScanTopMipAlphaClass()
{
    return Poseidon::AlphaStats::Opaque;
}

int TextureVK::TotalSize(int levelMin) const
{
    int size = 0;
    if (_src)
    {
        for (int i = levelMin; i < _src->GetMipmapCount(); i++)
            size += _mipmaps[i].Size();
    }
    return size;
}

void TextureVK::ReleaseSmall(bool store)
{
    if (_smallSurface.GetImage())
    {
        _smallSurface.Free(true);
    }
    _smallLoaded = -1;
}

int TextureVK::LoadSmall()
{
    if (!_src)
        return -1;
    return LoadLevels(_src->GetMipmapCount() - 1);
}

void TextureVK::SetMaxSize(int size)
{
    _maxSize = size;
}

void TextureVK::SetMultitexturing(int type)
{
}

bool TextureVK::VerifyChecksum(const Poseidon::MipInfo& mip) const
{
    return true;
}

int TextureVK::AWidth(int level) const { return _surface._w > 0 ? _surface._w : (_src ? _mipmaps[0]._w : 0); }
int TextureVK::AHeight(int level) const { return _surface._h > 0 ? _surface._h : (_src ? _mipmaps[0]._h : 0); }
int TextureVK::Size() const { return _surface._usedSize; }

bool TextureVK::InitFromRGBA(int w, int h, const void* rgba, uint32_t size, bool mipmap)
{
    if (!rgba)
        return false;

    _initialized = true;
    _dynamicMipmapped = mipmap;

    int maxLevel = 0;
    if (mipmap)
    {
        int dim = w > h ? w : h;
        while (dim > 1)
        {
            dim >>= 1;
            maxLevel++;
        }
    }
    _nMipmaps = maxLevel + 1;
    _mipmaps[0]._w = static_cast<short>(w);
    _mipmaps[0]._h = static_cast<short>(h);
    _mipmaps[0]._pitch = static_cast<short>(w * 4);
    _maxSize = w > h ? w : h;

    TextureDescVK desc;
    desc.w = w;
    desc.h = h;
    desc.nMipmaps = _nMipmaps;
    desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    desc.compressed = false;

    _surface.CreateSurface(desc, PacFormat::PacARGB8888);
    EngineVK* engine = static_cast<EngineVK*>(Poseidon::GEngine);
    if (engine)
    {
        engine->_textureRegistry[_surface.GetCreationID()] = this;
    }

    UpdateRGBA(rgba, size);

    _levelLoaded = 0;
    _smallLoaded = 0;
    _largestUsed = 0;

    return true;
}

void TextureVK::UpdateRGBA(const void* rgba, uint32_t size)
{
    VkImage image = _surface.GetImage();
    if (image == VK_NULL_HANDLE || !rgba)
        return;

    EngineVK* engine = static_cast<EngineVK*>(Poseidon::GEngine);
    VkDevice device = engine->_device;
    VkQueue queue = engine->_graphicsQueue;
    VkCommandPool pool = engine->_commandPool;
    VmaAllocator allocator = engine->_vmaAllocator;

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
    allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;

    VmaAllocationInfo stagingAllocInfo{};
    if (vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &stagingBuffer, &stagingAllocation, &stagingAllocInfo) != VK_SUCCESS)
    {
        LOG_ERROR(Graphics, "VK: Failed to create staging buffer for dynamic texture update");
        return;
    }

    if (stagingAllocInfo.pMappedData)
    {
        std::memcpy(stagingAllocInfo.pMappedData, rgba, size);
    }
    else
    {
        void* mapped = nullptr;
        if (vmaMapMemory(allocator, stagingAllocation, &mapped) == VK_SUCCESS)
        {
            std::memcpy(mapped, rgba, size);
            vmaUnmapMemory(allocator, stagingAllocation);
        }
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandPool = pool;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cb = VK_NULL_HANDLE;
    vkAllocateCommandBuffers(device, &cmdAllocInfo, &cb);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.image = image;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {
        static_cast<uint32_t>(_mipmaps[0]._w),
        static_cast<uint32_t>(_mipmaps[0]._h),
        1
    };

    vkCmdCopyBufferToImage(cb, stagingBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

    if (_dynamicMipmapped && _nMipmaps > 1)
    {
        int32_t mipWidth = _mipmaps[0]._w;
        int32_t mipHeight = _mipmaps[0]._h;

        for (int i = 1; i < _nMipmaps; i++)
        {
            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            barrier.subresourceRange.baseMipLevel = i;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            VkImageBlit blit{};
            blit.srcOffsets[0] = {0, 0, 0};
            blit.srcOffsets[1] = {mipWidth, mipHeight, 1};
            blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.srcSubresource.mipLevel = i - 1;
            blit.srcSubresource.baseArrayLayer = 0;
            blit.srcSubresource.layerCount = 1;
            blit.dstOffsets[0] = {0, 0, 0};
            blit.dstOffsets[1] = {
                mipWidth > 1 ? mipWidth / 2 : 1,
                mipHeight > 1 ? mipHeight / 2 : 1,
                1
            };
            blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            blit.dstSubresource.mipLevel = i;
            blit.dstSubresource.baseArrayLayer = 0;
            blit.dstSubresource.layerCount = 1;

            vkCmdBlitImage(cb, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            barrier.subresourceRange.baseMipLevel = i - 1;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

            if (mipWidth > 1) mipWidth /= 2;
            if (mipHeight > 1) mipHeight /= 2;
        }

        barrier.subresourceRange.baseMipLevel = _nMipmaps - 1;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }
    else
    {
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    vkEndCommandBuffer(cb);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cb;

    vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(queue);

    vkFreeCommandBuffers(device, pool, 1, &cb);
    vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
}

Poseidon::AbstractMipmapLevel& TextureVK::AMipmap(int level)
{
    return _mipmaps[level];
}

const Poseidon::AbstractMipmapLevel& TextureVK::AMipmap(int level) const
{
    return _mipmaps[level];
}

Poseidon::AlphaStats::Kind TextureVK::GetAlphaClass() { return Poseidon::AlphaStats::Opaque; }

} // namespace Poseidon
