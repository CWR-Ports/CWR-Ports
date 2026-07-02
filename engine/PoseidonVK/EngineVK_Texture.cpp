#include <PoseidonVK/TextureVK.hpp>
#include <PoseidonVK/TextBankVK.hpp>
#include <PoseidonVK/EngineVK.hpp>
#include <Poseidon/Core/Application.hpp>

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
    
    PacFormat format = _src->GetFormat();
    desc.compressed = enableDXT && (format >= PacFormat::PacDXT1 && format <= PacFormat::PacDXT5);
    
    if (desc.compressed)
    {
        desc.format = (format == PacFormat::PacDXT1) ? VK_FORMAT_BC1_RGBA_UNORM_BLOCK : VK_FORMAT_BC3_UNORM_BLOCK;
    }
    else
    {
        desc.format = VK_FORMAT_R8G8B8A8_UNORM;
    }
}

int TextureVK::LoadLevels(int levelMin)
{
    if (_levelLoaded == levelMin)
        return 0;
        
    TextureDescVK desc;
    InitDesc(desc, levelMin, true);
    
    // in actual implementation, we would create a staging buffer, map it, 
    // copy the pixels from _src, and issue a vkCmdCopyBufferToImage.
    // for this skeleton, we just create the GPU surface.
    
    _surface.CreateSurface(desc, _src->GetFormat(), TotalSize(levelMin));
    
    _levelLoaded = levelMin;
    _initialized = true;
    
    return desc.w * desc.h * 4;
}

int TextureVK::UploadToGPU(SurfaceInfoVK& surface, int levelMin)
{
    // Staging and uploading code goes here.
    return 0;
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

void TextureVK::UpdateRGBA(const unsigned char* rgb, int w, int h)
{
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
