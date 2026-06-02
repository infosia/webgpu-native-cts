// Ported from gpuweb/cts src/webgpu/util/texture/layout.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "cts/webgpu.h"

namespace cts {

constexpr uint32_t kBytesPerRowAlignment = 256;
constexpr uint32_t kBufferCopyAlignment = 4;

struct TextureCopyLayout {
    uint32_t bytesPerBlock = 0;
    uint64_t byteLength = 0;
    uint32_t minBytesPerRow = 0;
    uint32_t bytesPerRow = 0;
    uint32_t rowsPerImage = 0;
    WGPUExtent3D mipSize = WGPUExtent3D{0, 0, 0};
};

struct TexelCopyBufferLayout {
    uint64_t offset = 0;
    uint32_t bytesPerRow = WGPU_COPY_STRIDE_UNDEFINED;
    uint32_t rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;
};

struct LinearTextureSubBox {
    const uint8_t* src = nullptr;
    size_t srcLen = 0;
    TexelCopyBufferLayout srcLayout;
    WGPUOrigin3D srcOrigin = WGPUOrigin3D{0, 0, 0};
    uint8_t* dst = nullptr;
    size_t dstLen = 0;
    TexelCopyBufferLayout dstLayout;
    WGPUOrigin3D dstOrigin = WGPUOrigin3D{0, 0, 0};
};

uint32_t bytesInACompleteRow(uint32_t width, WGPUTextureFormat format);
TextureCopyLayout getTextureCopyLayout(
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    WGPUExtent3D baseSize,
    uint32_t mipLevel = 0);
uint64_t dataBytesForCopyOrFail(
    TexelCopyBufferLayout layout,
    WGPUTextureFormat format,
    WGPUExtent3D copySize,
    bool requireStrictAlignment);
void updateLinearTextureDataSubBox(WGPUTextureFormat format, WGPUExtent3D copySize, const LinearTextureSubBox& copy);

} // namespace cts
