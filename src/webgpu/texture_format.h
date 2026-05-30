#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "cts/webgpu.h"

namespace cts {

struct TextureFormatInfo {
    WGPUTextureFormat format;
    uint32_t blockWidth;
    uint32_t blockHeight;
    uint32_t bytesPerBlock;
    bool hasDepth;
    bool hasStencil;
    bool hasRequiredFeature;
    WGPUFeatureName requiredFeature;
};

struct TextureBlockInfo {
    uint32_t blockWidth;
    uint32_t blockHeight;
    uint32_t bytesPerBlock;
};

constexpr std::array<WGPUTextureDimension, 3> kTextureDimensions = {
    WGPUTextureDimension_1D,
    WGPUTextureDimension_2D,
    WGPUTextureDimension_3D,
};

inline constexpr std::array<TextureFormatInfo, 49> kUncompressedTextureFormatInfos = {{
    {WGPUTextureFormat_R8Unorm, 1, 1, 1, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R8Snorm, 1, 1, 1, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R8Uint, 1, 1, 1, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R8Sint, 1, 1, 1, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG8Unorm, 1, 1, 2, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG8Snorm, 1, 1, 2, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG8Uint, 1, 1, 2, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG8Sint, 1, 1, 2, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA8Unorm, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA8UnormSrgb, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA8Snorm, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA8Uint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA8Sint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_BGRA8Unorm, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_BGRA8UnormSrgb, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R16Unorm, 1, 1, 2, false, false, true, WGPUFeatureName_TextureFormatsTier1},
    {WGPUTextureFormat_R16Snorm, 1, 1, 2, false, false, true, WGPUFeatureName_TextureFormatsTier1},
    {WGPUTextureFormat_R16Uint, 1, 1, 2, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R16Sint, 1, 1, 2, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R16Float, 1, 1, 2, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG16Unorm, 1, 1, 4, false, false, true, WGPUFeatureName_TextureFormatsTier1},
    {WGPUTextureFormat_RG16Snorm, 1, 1, 4, false, false, true, WGPUFeatureName_TextureFormatsTier1},
    {WGPUTextureFormat_RG16Uint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG16Sint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG16Float, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA16Unorm, 1, 1, 8, false, false, true, WGPUFeatureName_TextureFormatsTier1},
    {WGPUTextureFormat_RGBA16Snorm, 1, 1, 8, false, false, true, WGPUFeatureName_TextureFormatsTier1},
    {WGPUTextureFormat_RGBA16Uint, 1, 1, 8, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA16Sint, 1, 1, 8, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA16Float, 1, 1, 8, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R32Uint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R32Sint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_R32Float, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG32Uint, 1, 1, 8, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG32Sint, 1, 1, 8, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG32Float, 1, 1, 8, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA32Uint, 1, 1, 16, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA32Sint, 1, 1, 16, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGBA32Float, 1, 1, 16, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGB10A2Uint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGB10A2Unorm, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RG11B10Ufloat, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_RGB9E5Ufloat, 1, 1, 4, false, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_Stencil8, 1, 1, 1, false, true, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_Depth16Unorm, 1, 1, 2, true, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_Depth32Float, 1, 1, 4, true, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_Depth24Plus, 1, 1, 0, true, false, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_Depth24PlusStencil8, 1, 1, 1, true, true, false, WGPUFeatureName_Force32},
    {WGPUTextureFormat_Depth32FloatStencil8, 1, 1, 4, true, true, true, WGPUFeatureName_Depth32FloatStencil8},
}};

inline constexpr std::array<WGPUTextureFormat, kUncompressedTextureFormatInfos.size()> kUncompressedTextureFormats = {
    WGPUTextureFormat_R8Unorm,
    WGPUTextureFormat_R8Snorm,
    WGPUTextureFormat_R8Uint,
    WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RG8Unorm,
    WGPUTextureFormat_RG8Snorm,
    WGPUTextureFormat_RG8Uint,
    WGPUTextureFormat_RG8Sint,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RGBA8UnormSrgb,
    WGPUTextureFormat_RGBA8Snorm,
    WGPUTextureFormat_RGBA8Uint,
    WGPUTextureFormat_RGBA8Sint,
    WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb,
    WGPUTextureFormat_R16Unorm,
    WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_R16Uint,
    WGPUTextureFormat_R16Sint,
    WGPUTextureFormat_R16Float,
    WGPUTextureFormat_RG16Unorm,
    WGPUTextureFormat_RG16Snorm,
    WGPUTextureFormat_RG16Uint,
    WGPUTextureFormat_RG16Sint,
    WGPUTextureFormat_RG16Float,
    WGPUTextureFormat_RGBA16Unorm,
    WGPUTextureFormat_RGBA16Snorm,
    WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RGBA16Sint,
    WGPUTextureFormat_RGBA16Float,
    WGPUTextureFormat_R32Uint,
    WGPUTextureFormat_R32Sint,
    WGPUTextureFormat_R32Float,
    WGPUTextureFormat_RG32Uint,
    WGPUTextureFormat_RG32Sint,
    WGPUTextureFormat_RG32Float,
    WGPUTextureFormat_RGBA32Uint,
    WGPUTextureFormat_RGBA32Sint,
    WGPUTextureFormat_RGBA32Float,
    WGPUTextureFormat_RGB10A2Uint,
    WGPUTextureFormat_RGB10A2Unorm,
    WGPUTextureFormat_RG11B10Ufloat,
    WGPUTextureFormat_RGB9E5Ufloat,
    WGPUTextureFormat_Stencil8,
    WGPUTextureFormat_Depth16Unorm,
    WGPUTextureFormat_Depth32Float,
    WGPUTextureFormat_Depth24Plus,
    WGPUTextureFormat_Depth24PlusStencil8,
    WGPUTextureFormat_Depth32FloatStencil8,
};

inline const TextureFormatInfo& textureFormatInfo(WGPUTextureFormat format) {
    for (const TextureFormatInfo& info : kUncompressedTextureFormatInfos) {
        if (info.format == format) {
            return info;
        }
    }
    return kUncompressedTextureFormatInfos[0];
}

inline TextureBlockInfo getBlockInfoForTextureFormat(WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    return TextureBlockInfo{info.blockWidth, info.blockHeight, info.bytesPerBlock};
}

} // namespace cts
