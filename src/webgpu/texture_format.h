#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "cts/webgpu.h"

namespace cts {

enum class TextureFormatClass {
    Uncompressed,
    BC,
    ETC2,
    ASTC,
};

struct TextureFormatInfo {
    WGPUTextureFormat format;
    uint32_t blockWidth;
    uint32_t blockHeight;
    uint32_t bytesPerBlock;
    bool hasDepth;
    bool hasStencil;
    bool hasRequiredFeature;
    WGPUFeatureName requiredFeature;
    bool multisample;
    TextureFormatClass formatClass;
};

struct TextureBlockInfo {
    uint32_t blockWidth;
    uint32_t blockHeight;
    uint32_t bytesPerBlock;
};

struct SizeVariantComponent {
    int mult;
    int addLiteral;
    int addBlockW;
    int addBlockH;
};

using SizeVariant = std::array<SizeVariantComponent, 3>;

constexpr std::array<WGPUTextureDimension, 3> kTextureDimensions = {
    WGPUTextureDimension_1D,
    WGPUTextureDimension_2D,
    WGPUTextureDimension_3D,
};

inline constexpr std::array<WGPUTextureAspect, 3> kTextureAspects = {
    WGPUTextureAspect_All,
    WGPUTextureAspect_DepthOnly,
    WGPUTextureAspect_StencilOnly,
};

inline constexpr std::array<WGPUTextureViewDimension, 6> kTextureViewDimensions = {
    WGPUTextureViewDimension_1D,
    WGPUTextureViewDimension_2D,
    WGPUTextureViewDimension_2DArray,
    WGPUTextureViewDimension_Cube,
    WGPUTextureViewDimension_CubeArray,
    WGPUTextureViewDimension_3D,
};

inline constexpr uint32_t kLevels = 6;

inline constexpr std::array<TextureFormatInfo, 49> kUncompressedTextureFormatInfos = {{
    {WGPUTextureFormat_R8Unorm, 1, 1, 1, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R8Snorm, 1, 1, 1, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R8Uint, 1, 1, 1, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R8Sint, 1, 1, 1, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG8Unorm, 1, 1, 2, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG8Snorm, 1, 1, 2, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG8Uint, 1, 1, 2, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG8Sint, 1, 1, 2, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA8Unorm, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA8UnormSrgb, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA8Snorm, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA8Uint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA8Sint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_BGRA8Unorm, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_BGRA8UnormSrgb, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R16Unorm, 1, 1, 2, false, false, true, WGPUFeatureName_TextureFormatsTier1, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R16Snorm, 1, 1, 2, false, false, true, WGPUFeatureName_TextureFormatsTier1, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R16Uint, 1, 1, 2, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R16Sint, 1, 1, 2, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R16Float, 1, 1, 2, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG16Unorm, 1, 1, 4, false, false, true, WGPUFeatureName_TextureFormatsTier1, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG16Snorm, 1, 1, 4, false, false, true, WGPUFeatureName_TextureFormatsTier1, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG16Uint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG16Sint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG16Float, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA16Unorm, 1, 1, 8, false, false, true, WGPUFeatureName_TextureFormatsTier1, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA16Snorm, 1, 1, 8, false, false, true, WGPUFeatureName_TextureFormatsTier1, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA16Uint, 1, 1, 8, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA16Sint, 1, 1, 8, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA16Float, 1, 1, 8, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R32Uint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R32Sint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_R32Float, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG32Uint, 1, 1, 8, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG32Sint, 1, 1, 8, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG32Float, 1, 1, 8, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA32Uint, 1, 1, 16, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA32Sint, 1, 1, 16, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGBA32Float, 1, 1, 16, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGB10A2Uint, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGB10A2Unorm, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RG11B10Ufloat, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_RGB9E5Ufloat, 1, 1, 4, false, false, false, WGPUFeatureName_Force32, false, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_Stencil8, 1, 1, 1, false, true, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_Depth16Unorm, 1, 1, 2, true, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_Depth32Float, 1, 1, 4, true, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_Depth24Plus, 1, 1, 0, true, false, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_Depth24PlusStencil8, 1, 1, 1, true, true, false, WGPUFeatureName_Force32, true, TextureFormatClass::Uncompressed},
    {WGPUTextureFormat_Depth32FloatStencil8, 1, 1, 4, true, true, true, WGPUFeatureName_Depth32FloatStencil8, true, TextureFormatClass::Uncompressed},
}};

inline constexpr std::array<TextureFormatInfo, 52> kCompressedTextureFormatInfos = {{
    {WGPUTextureFormat_BC1RGBAUnorm, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC1RGBAUnormSrgb, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC2RGBAUnorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC2RGBAUnormSrgb, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC3RGBAUnorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC3RGBAUnormSrgb, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC4RUnorm, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC4RSnorm, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC5RGUnorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC5RGSnorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC6HRGBUfloat, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC6HRGBFloat, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC7RGBAUnorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_BC7RGBAUnormSrgb, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionBC, false, TextureFormatClass::BC},
    {WGPUTextureFormat_ETC2RGB8Unorm, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_ETC2RGB8UnormSrgb, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_ETC2RGB8A1Unorm, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_ETC2RGB8A1UnormSrgb, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_ETC2RGBA8Unorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_ETC2RGBA8UnormSrgb, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_EACR11Unorm, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_EACR11Snorm, 4, 4, 8, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_EACRG11Unorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_EACRG11Snorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionETC2, false, TextureFormatClass::ETC2},
    {WGPUTextureFormat_ASTC4x4Unorm, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC4x4UnormSrgb, 4, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC5x4Unorm, 5, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC5x4UnormSrgb, 5, 4, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC5x5Unorm, 5, 5, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC5x5UnormSrgb, 5, 5, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC6x5Unorm, 6, 5, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC6x5UnormSrgb, 6, 5, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC6x6Unorm, 6, 6, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC6x6UnormSrgb, 6, 6, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC8x5Unorm, 8, 5, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC8x5UnormSrgb, 8, 5, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC8x6Unorm, 8, 6, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC8x6UnormSrgb, 8, 6, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC8x8Unorm, 8, 8, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC8x8UnormSrgb, 8, 8, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC10x5Unorm, 10, 5, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC10x5UnormSrgb, 10, 5, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC10x6Unorm, 10, 6, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC10x6UnormSrgb, 10, 6, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC10x8Unorm, 10, 8, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC10x8UnormSrgb, 10, 8, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC10x10Unorm, 10, 10, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC10x10UnormSrgb, 10, 10, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC12x10Unorm, 12, 10, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC12x10UnormSrgb, 12, 10, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC12x12Unorm, 12, 12, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
    {WGPUTextureFormat_ASTC12x12UnormSrgb, 12, 12, 16, false, false, true, WGPUFeatureName_TextureCompressionASTC, false, TextureFormatClass::ASTC},
}};

inline constexpr std::array<WGPUTextureFormat, kUncompressedTextureFormatInfos.size()> kUncompressedTextureFormats = {
    WGPUTextureFormat_R8Unorm, WGPUTextureFormat_R8Snorm, WGPUTextureFormat_R8Uint, WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RG8Unorm, WGPUTextureFormat_RG8Snorm, WGPUTextureFormat_RG8Uint, WGPUTextureFormat_RG8Sint,
    WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_RGBA8Snorm,
    WGPUTextureFormat_RGBA8Uint, WGPUTextureFormat_RGBA8Sint, WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb, WGPUTextureFormat_R16Unorm, WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_R16Uint, WGPUTextureFormat_R16Sint, WGPUTextureFormat_R16Float, WGPUTextureFormat_RG16Unorm,
    WGPUTextureFormat_RG16Snorm, WGPUTextureFormat_RG16Uint, WGPUTextureFormat_RG16Sint, WGPUTextureFormat_RG16Float,
    WGPUTextureFormat_RGBA16Unorm, WGPUTextureFormat_RGBA16Snorm, WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RGBA16Sint, WGPUTextureFormat_RGBA16Float, WGPUTextureFormat_R32Uint, WGPUTextureFormat_R32Sint,
    WGPUTextureFormat_R32Float, WGPUTextureFormat_RG32Uint, WGPUTextureFormat_RG32Sint, WGPUTextureFormat_RG32Float,
    WGPUTextureFormat_RGBA32Uint, WGPUTextureFormat_RGBA32Sint, WGPUTextureFormat_RGBA32Float, WGPUTextureFormat_RGB10A2Uint,
    WGPUTextureFormat_RGB10A2Unorm, WGPUTextureFormat_RG11B10Ufloat, WGPUTextureFormat_RGB9E5Ufloat,
    WGPUTextureFormat_Stencil8, WGPUTextureFormat_Depth16Unorm, WGPUTextureFormat_Depth32Float,
    WGPUTextureFormat_Depth24Plus, WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureFormat_Depth32FloatStencil8,
};

inline constexpr std::array<WGPUTextureFormat, 43> kRegularTextureFormats = {
    WGPUTextureFormat_R8Unorm, WGPUTextureFormat_R8Snorm, WGPUTextureFormat_R8Uint, WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RG8Unorm, WGPUTextureFormat_RG8Snorm, WGPUTextureFormat_RG8Uint, WGPUTextureFormat_RG8Sint,
    WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_RGBA8Snorm,
    WGPUTextureFormat_RGBA8Uint, WGPUTextureFormat_RGBA8Sint, WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb, WGPUTextureFormat_R16Unorm, WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_R16Uint, WGPUTextureFormat_R16Sint, WGPUTextureFormat_R16Float, WGPUTextureFormat_RG16Unorm,
    WGPUTextureFormat_RG16Snorm, WGPUTextureFormat_RG16Uint, WGPUTextureFormat_RG16Sint, WGPUTextureFormat_RG16Float,
    WGPUTextureFormat_RGBA16Unorm, WGPUTextureFormat_RGBA16Snorm, WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RGBA16Sint, WGPUTextureFormat_RGBA16Float, WGPUTextureFormat_R32Uint, WGPUTextureFormat_R32Sint,
    WGPUTextureFormat_R32Float, WGPUTextureFormat_RG32Uint, WGPUTextureFormat_RG32Sint, WGPUTextureFormat_RG32Float,
    WGPUTextureFormat_RGBA32Uint, WGPUTextureFormat_RGBA32Sint, WGPUTextureFormat_RGBA32Float, WGPUTextureFormat_RGB10A2Uint,
    WGPUTextureFormat_RGB10A2Unorm, WGPUTextureFormat_RG11B10Ufloat, WGPUTextureFormat_RGB9E5Ufloat,
};

inline constexpr std::array<WGPUTextureFormat, kRegularTextureFormats.size()> kColorTextureFormats = kRegularTextureFormats;

inline constexpr std::array<WGPUTextureFormat, 6> kDepthStencilFormats = {
    WGPUTextureFormat_Stencil8,
    WGPUTextureFormat_Depth16Unorm,
    WGPUTextureFormat_Depth32Float,
    WGPUTextureFormat_Depth24Plus,
    WGPUTextureFormat_Depth24PlusStencil8,
    WGPUTextureFormat_Depth32FloatStencil8,
};

// Representative formats for the --sample-formats fast-iteration mode (see specs/format-sampling-mode.md).
inline constexpr std::array<WGPUTextureFormat, 12> kRepresentativeTextureFormats = {
    WGPUTextureFormat_RGBA8Unorm,     WGPUTextureFormat_RGBA8Snorm,    WGPUTextureFormat_RGBA8Uint,
    WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_BGRA8Unorm,    WGPUTextureFormat_RGBA16Float,
    WGPUTextureFormat_RGBA32Float,    WGPUTextureFormat_RGB10A2Unorm,
    WGPUTextureFormat_Depth32Float,   WGPUTextureFormat_Depth24PlusStencil8,
    WGPUTextureFormat_BC1RGBAUnorm,   WGPUTextureFormat_ASTC4x4Unorm,
};

inline bool isRepresentativeTextureFormat(WGPUTextureFormat format) {
    for (WGPUTextureFormat listedFormat : kRepresentativeTextureFormats) {
        if (listedFormat == format) {
            return true;
        }
    }
    return false;
}

inline constexpr std::array<WGPUTextureFormat, kCompressedTextureFormatInfos.size()> kCompressedTextureFormats = {
    WGPUTextureFormat_BC1RGBAUnorm, WGPUTextureFormat_BC1RGBAUnormSrgb, WGPUTextureFormat_BC2RGBAUnorm,
    WGPUTextureFormat_BC2RGBAUnormSrgb, WGPUTextureFormat_BC3RGBAUnorm, WGPUTextureFormat_BC3RGBAUnormSrgb,
    WGPUTextureFormat_BC4RUnorm, WGPUTextureFormat_BC4RSnorm, WGPUTextureFormat_BC5RGUnorm, WGPUTextureFormat_BC5RGSnorm,
    WGPUTextureFormat_BC6HRGBUfloat, WGPUTextureFormat_BC6HRGBFloat, WGPUTextureFormat_BC7RGBAUnorm,
    WGPUTextureFormat_BC7RGBAUnormSrgb, WGPUTextureFormat_ETC2RGB8Unorm, WGPUTextureFormat_ETC2RGB8UnormSrgb,
    WGPUTextureFormat_ETC2RGB8A1Unorm, WGPUTextureFormat_ETC2RGB8A1UnormSrgb, WGPUTextureFormat_ETC2RGBA8Unorm,
    WGPUTextureFormat_ETC2RGBA8UnormSrgb, WGPUTextureFormat_EACR11Unorm, WGPUTextureFormat_EACR11Snorm,
    WGPUTextureFormat_EACRG11Unorm, WGPUTextureFormat_EACRG11Snorm, WGPUTextureFormat_ASTC4x4Unorm,
    WGPUTextureFormat_ASTC4x4UnormSrgb, WGPUTextureFormat_ASTC5x4Unorm, WGPUTextureFormat_ASTC5x4UnormSrgb,
    WGPUTextureFormat_ASTC5x5Unorm, WGPUTextureFormat_ASTC5x5UnormSrgb, WGPUTextureFormat_ASTC6x5Unorm,
    WGPUTextureFormat_ASTC6x5UnormSrgb, WGPUTextureFormat_ASTC6x6Unorm, WGPUTextureFormat_ASTC6x6UnormSrgb,
    WGPUTextureFormat_ASTC8x5Unorm, WGPUTextureFormat_ASTC8x5UnormSrgb, WGPUTextureFormat_ASTC8x6Unorm,
    WGPUTextureFormat_ASTC8x6UnormSrgb, WGPUTextureFormat_ASTC8x8Unorm, WGPUTextureFormat_ASTC8x8UnormSrgb,
    WGPUTextureFormat_ASTC10x5Unorm, WGPUTextureFormat_ASTC10x5UnormSrgb, WGPUTextureFormat_ASTC10x6Unorm,
    WGPUTextureFormat_ASTC10x6UnormSrgb, WGPUTextureFormat_ASTC10x8Unorm, WGPUTextureFormat_ASTC10x8UnormSrgb,
    WGPUTextureFormat_ASTC10x10Unorm, WGPUTextureFormat_ASTC10x10UnormSrgb, WGPUTextureFormat_ASTC12x10Unorm,
    WGPUTextureFormat_ASTC12x10UnormSrgb, WGPUTextureFormat_ASTC12x12Unorm, WGPUTextureFormat_ASTC12x12UnormSrgb,
};

inline constexpr std::array<WGPUTextureFormat, kUncompressedTextureFormats.size() + kCompressedTextureFormats.size()> kAllTextureFormats = {
    WGPUTextureFormat_R8Unorm, WGPUTextureFormat_R8Snorm, WGPUTextureFormat_R8Uint, WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RG8Unorm, WGPUTextureFormat_RG8Snorm, WGPUTextureFormat_RG8Uint, WGPUTextureFormat_RG8Sint,
    WGPUTextureFormat_RGBA8Unorm, WGPUTextureFormat_RGBA8UnormSrgb, WGPUTextureFormat_RGBA8Snorm,
    WGPUTextureFormat_RGBA8Uint, WGPUTextureFormat_RGBA8Sint, WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb, WGPUTextureFormat_R16Unorm, WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_R16Uint, WGPUTextureFormat_R16Sint, WGPUTextureFormat_R16Float, WGPUTextureFormat_RG16Unorm,
    WGPUTextureFormat_RG16Snorm, WGPUTextureFormat_RG16Uint, WGPUTextureFormat_RG16Sint, WGPUTextureFormat_RG16Float,
    WGPUTextureFormat_RGBA16Unorm, WGPUTextureFormat_RGBA16Snorm, WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RGBA16Sint, WGPUTextureFormat_RGBA16Float, WGPUTextureFormat_R32Uint, WGPUTextureFormat_R32Sint,
    WGPUTextureFormat_R32Float, WGPUTextureFormat_RG32Uint, WGPUTextureFormat_RG32Sint, WGPUTextureFormat_RG32Float,
    WGPUTextureFormat_RGBA32Uint, WGPUTextureFormat_RGBA32Sint, WGPUTextureFormat_RGBA32Float, WGPUTextureFormat_RGB10A2Uint,
    WGPUTextureFormat_RGB10A2Unorm, WGPUTextureFormat_RG11B10Ufloat, WGPUTextureFormat_RGB9E5Ufloat,
    WGPUTextureFormat_Stencil8, WGPUTextureFormat_Depth16Unorm, WGPUTextureFormat_Depth32Float,
    WGPUTextureFormat_Depth24Plus, WGPUTextureFormat_Depth24PlusStencil8, WGPUTextureFormat_Depth32FloatStencil8,
    WGPUTextureFormat_BC1RGBAUnorm, WGPUTextureFormat_BC1RGBAUnormSrgb, WGPUTextureFormat_BC2RGBAUnorm,
    WGPUTextureFormat_BC2RGBAUnormSrgb, WGPUTextureFormat_BC3RGBAUnorm, WGPUTextureFormat_BC3RGBAUnormSrgb,
    WGPUTextureFormat_BC4RUnorm, WGPUTextureFormat_BC4RSnorm, WGPUTextureFormat_BC5RGUnorm, WGPUTextureFormat_BC5RGSnorm,
    WGPUTextureFormat_BC6HRGBUfloat, WGPUTextureFormat_BC6HRGBFloat, WGPUTextureFormat_BC7RGBAUnorm,
    WGPUTextureFormat_BC7RGBAUnormSrgb, WGPUTextureFormat_ETC2RGB8Unorm, WGPUTextureFormat_ETC2RGB8UnormSrgb,
    WGPUTextureFormat_ETC2RGB8A1Unorm, WGPUTextureFormat_ETC2RGB8A1UnormSrgb, WGPUTextureFormat_ETC2RGBA8Unorm,
    WGPUTextureFormat_ETC2RGBA8UnormSrgb, WGPUTextureFormat_EACR11Unorm, WGPUTextureFormat_EACR11Snorm,
    WGPUTextureFormat_EACRG11Unorm, WGPUTextureFormat_EACRG11Snorm, WGPUTextureFormat_ASTC4x4Unorm,
    WGPUTextureFormat_ASTC4x4UnormSrgb, WGPUTextureFormat_ASTC5x4Unorm, WGPUTextureFormat_ASTC5x4UnormSrgb,
    WGPUTextureFormat_ASTC5x5Unorm, WGPUTextureFormat_ASTC5x5UnormSrgb, WGPUTextureFormat_ASTC6x5Unorm,
    WGPUTextureFormat_ASTC6x5UnormSrgb, WGPUTextureFormat_ASTC6x6Unorm, WGPUTextureFormat_ASTC6x6UnormSrgb,
    WGPUTextureFormat_ASTC8x5Unorm, WGPUTextureFormat_ASTC8x5UnormSrgb, WGPUTextureFormat_ASTC8x6Unorm,
    WGPUTextureFormat_ASTC8x6UnormSrgb, WGPUTextureFormat_ASTC8x8Unorm, WGPUTextureFormat_ASTC8x8UnormSrgb,
    WGPUTextureFormat_ASTC10x5Unorm, WGPUTextureFormat_ASTC10x5UnormSrgb, WGPUTextureFormat_ASTC10x6Unorm,
    WGPUTextureFormat_ASTC10x6UnormSrgb, WGPUTextureFormat_ASTC10x8Unorm, WGPUTextureFormat_ASTC10x8UnormSrgb,
    WGPUTextureFormat_ASTC10x10Unorm, WGPUTextureFormat_ASTC10x10UnormSrgb, WGPUTextureFormat_ASTC12x10Unorm,
    WGPUTextureFormat_ASTC12x10UnormSrgb, WGPUTextureFormat_ASTC12x12Unorm, WGPUTextureFormat_ASTC12x12UnormSrgb,
};

inline constexpr std::array<WGPUTextureFormat, 10> kTextureFormatTier1AllowsRenderAttachmentBlendableMultisample = {
    WGPUTextureFormat_R16Unorm,
    WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_RG16Unorm,
    WGPUTextureFormat_RG16Snorm,
    WGPUTextureFormat_RGBA16Unorm,
    WGPUTextureFormat_RGBA16Snorm,
    WGPUTextureFormat_R8Snorm,
    WGPUTextureFormat_RG8Snorm,
    WGPUTextureFormat_RGBA8Snorm,
    WGPUTextureFormat_RG11B10Ufloat,
};

inline constexpr std::array<WGPUTextureFormat, 39> kColorRenderableTextureFormats = {
    WGPUTextureFormat_R8Unorm,
    WGPUTextureFormat_R8Uint,
    WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RG8Unorm,
    WGPUTextureFormat_RG8Uint,
    WGPUTextureFormat_RG8Sint,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RGBA8UnormSrgb,
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
};

inline constexpr std::array<WGPUTextureFormat, 22> kStorageTextureFormats = {
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RGBA8Snorm,
    WGPUTextureFormat_RGBA8Uint,
    WGPUTextureFormat_RGBA8Sint,
    WGPUTextureFormat_R16Unorm,
    WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_RG16Unorm,
    WGPUTextureFormat_RG16Snorm,
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
};

inline constexpr std::array<WGPUTextureFormat, 17> kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly = {
    WGPUTextureFormat_R8Unorm,
    WGPUTextureFormat_R8Snorm,
    WGPUTextureFormat_R8Uint,
    WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RG8Unorm,
    WGPUTextureFormat_RG8Snorm,
    WGPUTextureFormat_RG8Uint,
    WGPUTextureFormat_RG8Sint,
    WGPUTextureFormat_R16Uint,
    WGPUTextureFormat_R16Sint,
    WGPUTextureFormat_R16Float,
    WGPUTextureFormat_RG16Uint,
    WGPUTextureFormat_RG16Sint,
    WGPUTextureFormat_RG16Float,
    WGPUTextureFormat_RGB10A2Uint,
    WGPUTextureFormat_RGB10A2Unorm,
    WGPUTextureFormat_RG11B10Ufloat,
};

inline constexpr std::array<WGPUTextureFormat, 3> kReadWriteStorageTextureFormats = {
    WGPUTextureFormat_R32Uint,
    WGPUTextureFormat_R32Sint,
    WGPUTextureFormat_R32Float,
};

inline constexpr std::array<WGPUTextureFormat, 15> kTextureFormatsTier2EnablesStorageReadWrite = {
    WGPUTextureFormat_R8Unorm,
    WGPUTextureFormat_R8Uint,
    WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RGBA8Uint,
    WGPUTextureFormat_RGBA8Sint,
    WGPUTextureFormat_R16Uint,
    WGPUTextureFormat_R16Sint,
    WGPUTextureFormat_R16Float,
    WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RGBA16Sint,
    WGPUTextureFormat_RGBA16Float,
    WGPUTextureFormat_RGBA32Uint,
    WGPUTextureFormat_RGBA32Sint,
    WGPUTextureFormat_RGBA32Float,
};

inline constexpr std::array<WGPUFeatureName, 6> kFeaturesForFormats = {
    WGPUFeatureName_Force32,
    WGPUFeatureName_TextureFormatsTier1,
    WGPUFeatureName_Depth32FloatStencil8,
    WGPUFeatureName_TextureCompressionBC,
    WGPUFeatureName_TextureCompressionETC2,
    WGPUFeatureName_TextureCompressionASTC,
};

inline constexpr std::array<SizeVariant, 28> kCompressedTextureSizeVariants = {{
    {{{1, -1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    {{{1, 0, -1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    {{{1, 0, -1, 0}, {0, 0, 0, 1}, {0, 1, 0, 0}}},
    {{{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    {{{1, 0, 0, 0}, {0, 0, 0, 1}, {0, 1, 0, 0}}},
    {{{1, 1, 0, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    {{{1, 0, 1, 0}, {0, 1, 0, 0}, {0, 1, 0, 0}}},
    {{{1, 0, 1, 0}, {0, 0, 0, 1}, {0, 1, 0, 0}}},
    {{{0, 1, 0, 0}, {1, -1, 0, 0}, {0, 1, 0, 0}}},
    {{{0, 1, 0, 0}, {1, 0, 0, -1}, {0, 1, 0, 0}}},
    {{{0, 0, 1, 0}, {1, 0, 0, -1}, {0, 1, 0, 0}}},
    {{{0, 1, 0, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}}},
    {{{0, 0, 1, 0}, {1, 0, 0, 0}, {0, 1, 0, 0}}},
    {{{0, 1, 0, 0}, {1, 1, 0, 0}, {0, 1, 0, 0}}},
    {{{0, 1, 0, 0}, {1, 0, 1, 0}, {0, 1, 0, 0}}},
    {{{0, 0, 1, 0}, {1, 0, 0, 1}, {0, 1, 0, 0}}},
    {{{0, 1, 0, 0}, {0, 1, 0, 0}, {1, -1, 0, 0}}},
    {{{0, 0, 1, 0}, {0, 1, 0, 0}, {1, -1, 0, 0}}},
    {{{0, 1, 0, 0}, {0, 0, 0, 1}, {1, -1, 0, 0}}},
    {{{0, 0, 1, 0}, {0, 0, 0, 1}, {1, -1, 0, 0}}},
    {{{0, 1, 0, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}}},
    {{{0, 0, 1, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}}},
    {{{0, 1, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 0}}},
    {{{0, 0, 1, 0}, {0, 0, 0, 1}, {1, 0, 0, 0}}},
    {{{0, 1, 0, 0}, {0, 1, 0, 0}, {1, 1, 0, 0}}},
    {{{0, 0, 1, 0}, {0, 1, 0, 0}, {1, 1, 0, 0}}},
    {{{0, 1, 0, 0}, {0, 0, 0, 1}, {1, 1, 0, 0}}},
    {{{0, 0, 1, 0}, {0, 0, 0, 1}, {1, 1, 0, 0}}},
}};

inline const TextureFormatInfo& textureFormatInfo(WGPUTextureFormat format) {
    for (const TextureFormatInfo& info : kUncompressedTextureFormatInfos) {
        if (info.format == format) {
            return info;
        }
    }
    for (const TextureFormatInfo& info : kCompressedTextureFormatInfos) {
        if (info.format == format) {
            return info;
        }
    }
    std::abort();
}

inline TextureBlockInfo getBlockInfoForTextureFormat(WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    return TextureBlockInfo{info.blockWidth, info.blockHeight, info.bytesPerBlock};
}

inline TextureBlockInfo getBlockInfoForColorTextureFormat(WGPUTextureFormat format) {
    for (WGPUTextureFormat colorFormat : kColorTextureFormats) {
        if (colorFormat == format) {
            return getBlockInfoForTextureFormat(format);
        }
    }
    std::abort();
}

inline uint32_t roundDown(uint32_t value, uint32_t multiple) {
    return (value / multiple) * multiple;
}

inline std::array<uint32_t, 3> getMaxValidTextureSizeForFormatAndDimension(const WGPULimits& limits,
                                                                            WGPUTextureFormat format,
                                                                            WGPUTextureDimension dimension) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    if (dimension == WGPUTextureDimension_1D) {
        return {limits.maxTextureDimension1D, 1, 1};
    }
    if (dimension == WGPUTextureDimension_2D || dimension == WGPUTextureDimension_Undefined) {
        return {
            roundDown(limits.maxTextureDimension2D, info.blockWidth),
            roundDown(limits.maxTextureDimension2D, info.blockHeight),
            limits.maxTextureArrayLayers,
        };
    }
    if (dimension == WGPUTextureDimension_3D) {
        return {
            roundDown(limits.maxTextureDimension3D, info.blockWidth),
            roundDown(limits.maxTextureDimension3D, info.blockHeight),
            limits.maxTextureDimension3D,
        };
    }
    std::abort();
}

inline uint32_t maxMipLevelCount(const WGPUExtent3D& size, WGPUTextureDimension dimension) {
    uint32_t maxMippedDimension = std::max(size.width, size.height);
    if (dimension == WGPUTextureDimension_1D) {
        maxMippedDimension = 1;
    } else if (dimension == WGPUTextureDimension_3D) {
        maxMippedDimension = std::max(maxMippedDimension, size.depthOrArrayLayers);
    }

    uint32_t mipLevels = 1;
    while (maxMippedDimension > 1) {
        maxMippedDimension >>= 1;
        ++mipLevels;
    }
    return mipLevels;
}

inline bool isBCTextureFormat(WGPUTextureFormat format) {
    return textureFormatInfo(format).formatClass == TextureFormatClass::BC;
}

inline bool isASTCTextureFormat(WGPUTextureFormat format) {
    return textureFormatInfo(format).formatClass == TextureFormatClass::ASTC;
}

inline bool isETC2TextureFormat(WGPUTextureFormat format) {
    return textureFormatInfo(format).formatClass == TextureFormatClass::ETC2;
}

inline bool isCompressedTextureFormat(WGPUTextureFormat format) {
    return textureFormatInfo(format).formatClass != TextureFormatClass::Uncompressed;
}

inline bool isColorTextureFormat(WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    return !info.hasDepth && !info.hasStencil;
}

inline bool isDepthTextureFormat(WGPUTextureFormat format) {
    return textureFormatInfo(format).hasDepth;
}

inline bool isStencilTextureFormat(WGPUTextureFormat format) {
    return textureFormatInfo(format).hasStencil;
}

inline uint32_t depthStencilFormatAspectSize(WGPUTextureFormat format, WGPUTextureAspect aspect) {
    if (aspect == WGPUTextureAspect_StencilOnly) {
        return 1;
    }
    switch (format) {
        case WGPUTextureFormat_Depth16Unorm:
            return 2;
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth24Plus:
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32FloatStencil8:
            return 4;
        default:
            return textureFormatInfo(format).bytesPerBlock;
    }
}

inline bool isDepthOrStencilTextureFormat(WGPUTextureFormat format) {
    return isDepthTextureFormat(format) || isStencilTextureFormat(format);
}

inline WGPUTextureDimension getTextureDimensionFromView(WGPUTextureViewDimension dimension) {
    switch (dimension) {
        case WGPUTextureViewDimension_1D:
            return WGPUTextureDimension_1D;
        case WGPUTextureViewDimension_2D:
        case WGPUTextureViewDimension_2DArray:
        case WGPUTextureViewDimension_Cube:
        case WGPUTextureViewDimension_CubeArray:
            return WGPUTextureDimension_2D;
        case WGPUTextureViewDimension_3D:
            return WGPUTextureDimension_3D;
        default:
            std::abort();
    }
}

inline std::vector<WGPUTextureViewDimension> viewDimensionsForTextureDimension(WGPUTextureDimension dimension) {
    switch (dimension) {
        case WGPUTextureDimension_1D:
            return {WGPUTextureViewDimension_1D};
        case WGPUTextureDimension_2D:
            return {
                WGPUTextureViewDimension_2D,
                WGPUTextureViewDimension_2DArray,
                WGPUTextureViewDimension_Cube,
                WGPUTextureViewDimension_CubeArray,
            };
        case WGPUTextureDimension_3D:
            return {WGPUTextureViewDimension_3D};
        default:
            std::abort();
    }
}

inline WGPUTextureViewDimension effectiveViewDimensionForDimension(
    WGPUTextureViewDimension dimension,
    WGPUTextureDimension textureDimension,
    uint32_t depthOrArrayLayers) {
    if (dimension != WGPUTextureViewDimension_Undefined) {
        return dimension;
    }

    switch (textureDimension) {
        case WGPUTextureDimension_1D:
            return WGPUTextureViewDimension_1D;
        case WGPUTextureDimension_2D:
            return depthOrArrayLayers > 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
        case WGPUTextureDimension_3D:
            return WGPUTextureViewDimension_3D;
        default:
            std::abort();
    }
}

struct ReifiedTextureView {
    WGPUTextureViewDimension dimension;
    uint32_t baseMipLevel;
    uint32_t mipLevelCount;
    uint32_t baseArrayLayer;
    uint32_t arrayLayerCount;
};

inline uint32_t textureMipLevelCountOrDefault(const WGPUTextureDescriptor& texture) {
    return texture.mipLevelCount == 0 ? 1 : texture.mipLevelCount;
}

inline ReifiedTextureView reifyTextureViewDescriptor(
    const WGPUTextureDescriptor& texture,
    const WGPUTextureViewDescriptor& view,
    bool dimensionGiven,
    bool baseMipLevelGiven,
    bool mipLevelCountGiven,
    bool baseArrayLayerGiven,
    bool arrayLayerCountGiven) {
    const uint32_t textureLevels = textureMipLevelCountOrDefault(texture);
    const uint32_t baseMipLevel = baseMipLevelGiven ? view.baseMipLevel : 0;
    const uint32_t baseArrayLayer = baseArrayLayerGiven ? view.baseArrayLayer : 0;
    const WGPUTextureViewDimension dimension = effectiveViewDimensionForDimension(
        dimensionGiven ? view.dimension : WGPUTextureViewDimension_Undefined,
        texture.dimension,
        texture.size.depthOrArrayLayers);

    uint32_t arrayLayerCount = 1;
    if (arrayLayerCountGiven) {
        arrayLayerCount = view.arrayLayerCount;
    } else if (dimension == WGPUTextureViewDimension_2DArray || dimension == WGPUTextureViewDimension_CubeArray) {
        arrayLayerCount = texture.size.depthOrArrayLayers - baseArrayLayer;
    } else if (dimension == WGPUTextureViewDimension_Cube) {
        arrayLayerCount = 6;
    }

    return ReifiedTextureView{
        dimension,
        baseMipLevel,
        mipLevelCountGiven ? view.mipLevelCount : textureLevels - baseMipLevel,
        baseArrayLayer,
        arrayLayerCount,
    };
}

inline bool validateCreateViewLayersLevels(
    const WGPUTextureDescriptor& texture,
    const WGPUTextureViewDescriptor& view,
    bool dimensionGiven,
    bool baseMipLevelGiven,
    bool mipLevelCountGiven,
    bool baseArrayLayerGiven,
    bool arrayLayerCountGiven) {
    const uint32_t textureLevels = textureMipLevelCountOrDefault(texture);
    const uint32_t textureLayers =
        texture.dimension == WGPUTextureDimension_2D ? texture.size.depthOrArrayLayers : 1;
    const ReifiedTextureView reified = reifyTextureViewDescriptor(
        texture,
        view,
        dimensionGiven,
        baseMipLevelGiven,
        mipLevelCountGiven,
        baseArrayLayerGiven,
        arrayLayerCountGiven);

    bool success = reified.mipLevelCount > 0
        && reified.baseMipLevel < textureLevels
        && static_cast<uint64_t>(reified.baseMipLevel) + reified.mipLevelCount <= textureLevels
        && reified.arrayLayerCount > 0
        && reified.baseArrayLayer < textureLayers
        && static_cast<uint64_t>(reified.baseArrayLayer) + reified.arrayLayerCount <= textureLayers;

    switch (reified.dimension) {
        case WGPUTextureViewDimension_1D:
        case WGPUTextureViewDimension_2D:
        case WGPUTextureViewDimension_3D:
            success = success && reified.arrayLayerCount == 1;
            break;
        case WGPUTextureViewDimension_Cube:
            success = success && reified.arrayLayerCount == 6;
            break;
        case WGPUTextureViewDimension_CubeArray:
            success = success && reified.arrayLayerCount % 6 == 0;
            break;
        case WGPUTextureViewDimension_2DArray:
            break;
        default:
            std::abort();
    }

    return success;
}

inline WGPUTextureFormat baseFormat(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_RGBA8UnormSrgb:
            return WGPUTextureFormat_RGBA8Unorm;
        case WGPUTextureFormat_BGRA8UnormSrgb:
            return WGPUTextureFormat_BGRA8Unorm;
        case WGPUTextureFormat_BC1RGBAUnormSrgb:
            return WGPUTextureFormat_BC1RGBAUnorm;
        case WGPUTextureFormat_BC2RGBAUnormSrgb:
            return WGPUTextureFormat_BC2RGBAUnorm;
        case WGPUTextureFormat_BC3RGBAUnormSrgb:
            return WGPUTextureFormat_BC3RGBAUnorm;
        case WGPUTextureFormat_BC7RGBAUnormSrgb:
            return WGPUTextureFormat_BC7RGBAUnorm;
        case WGPUTextureFormat_ETC2RGB8UnormSrgb:
            return WGPUTextureFormat_ETC2RGB8Unorm;
        case WGPUTextureFormat_ETC2RGB8A1UnormSrgb:
            return WGPUTextureFormat_ETC2RGB8A1Unorm;
        case WGPUTextureFormat_ETC2RGBA8UnormSrgb:
            return WGPUTextureFormat_ETC2RGBA8Unorm;
        case WGPUTextureFormat_ASTC4x4UnormSrgb:
            return WGPUTextureFormat_ASTC4x4Unorm;
        case WGPUTextureFormat_ASTC5x4UnormSrgb:
            return WGPUTextureFormat_ASTC5x4Unorm;
        case WGPUTextureFormat_ASTC5x5UnormSrgb:
            return WGPUTextureFormat_ASTC5x5Unorm;
        case WGPUTextureFormat_ASTC6x5UnormSrgb:
            return WGPUTextureFormat_ASTC6x5Unorm;
        case WGPUTextureFormat_ASTC6x6UnormSrgb:
            return WGPUTextureFormat_ASTC6x6Unorm;
        case WGPUTextureFormat_ASTC8x5UnormSrgb:
            return WGPUTextureFormat_ASTC8x5Unorm;
        case WGPUTextureFormat_ASTC8x6UnormSrgb:
            return WGPUTextureFormat_ASTC8x6Unorm;
        case WGPUTextureFormat_ASTC8x8UnormSrgb:
            return WGPUTextureFormat_ASTC8x8Unorm;
        case WGPUTextureFormat_ASTC10x5UnormSrgb:
            return WGPUTextureFormat_ASTC10x5Unorm;
        case WGPUTextureFormat_ASTC10x6UnormSrgb:
            return WGPUTextureFormat_ASTC10x6Unorm;
        case WGPUTextureFormat_ASTC10x8UnormSrgb:
            return WGPUTextureFormat_ASTC10x8Unorm;
        case WGPUTextureFormat_ASTC10x10UnormSrgb:
            return WGPUTextureFormat_ASTC10x10Unorm;
        case WGPUTextureFormat_ASTC12x10UnormSrgb:
            return WGPUTextureFormat_ASTC12x10Unorm;
        case WGPUTextureFormat_ASTC12x12UnormSrgb:
            return WGPUTextureFormat_ASTC12x12Unorm;
        default:
            return format;
    }
}

inline bool textureFormatsAreViewCompatible(WGPUTextureFormat a, WGPUTextureFormat b) {
    return baseFormat(a) == baseFormat(b);
}

inline WGPUFeatureName textureFormatFeature(WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    return info.hasRequiredFeature ? info.requiredFeature : WGPUFeatureName_Force32;
}

inline std::vector<WGPUTextureFormat> filterFormatsByFeature(WGPUFeatureName feature) {
    std::vector<WGPUTextureFormat> formats;
    for (WGPUTextureFormat format : kAllTextureFormats) {
        if (textureFormatFeature(format) == feature) {
            formats.push_back(format);
        }
    }
    return formats;
}

template <std::size_t N>
inline bool textureFormatInList(WGPUTextureFormat format, const std::array<WGPUTextureFormat, N>& formats) {
    for (WGPUTextureFormat listedFormat : formats) {
        if (listedFormat == format) {
            return true;
        }
    }
    return false;
}

inline bool textureFormatAndDimensionPossiblyCompatible(WGPUTextureDimension dimension, WGPUTextureFormat format) {
    if (dimension == WGPUTextureDimension_3D && (isBCTextureFormat(format) || isASTCTextureFormat(format))) {
        return true;
    }
    const TextureFormatInfo& info = textureFormatInfo(format);
    const bool restrictedDimension = dimension == WGPUTextureDimension_1D || dimension == WGPUTextureDimension_3D;
    return !(restrictedDimension && (info.blockWidth > 1 || info.hasDepth || info.hasStencil));
}

inline bool isTier1BlendableMultisampleTextureFormat(WGPUTextureFormat format) {
    return textureFormatInList(format, kTextureFormatTier1AllowsRenderAttachmentBlendableMultisample);
}

inline bool isTextureFormatPossiblyUsableAsRenderAttachment(WGPUTextureFormat format) {
    return isDepthOrStencilTextureFormat(format)
        || textureFormatInList(format, kColorRenderableTextureFormats)
        || isTier1BlendableMultisampleTextureFormat(format);
}

inline bool isTextureFormatPossiblyUsableAsColorRenderAttachment(WGPUTextureFormat format) {
    return textureFormatInList(format, kColorRenderableTextureFormats)
        || isTier1BlendableMultisampleTextureFormat(format);
}

inline bool isTextureFormatPossiblyStorageReadable(WGPUTextureFormat format) {
    return textureFormatInList(format, kStorageTextureFormats)
        || textureFormatInList(format, kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly);
}

} // namespace cts
