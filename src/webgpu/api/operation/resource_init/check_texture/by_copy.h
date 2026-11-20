// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/resource_init/check_texture/by_copy.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md), plus the GPUTest
// `expectSingleColor` / `expectGPUBufferRepeatsSingleValue` helpers it relies on.
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
#pragma once

#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "webgpu/api/operation/resource_init/check_texture/texture_zero_init_test.h"

namespace cts::texture_zero {

/// Upstream GPUTest.expectSingleColor: copies one subresource (2d: one `slice`, 3d: the whole mip)
/// of `src` into a fresh (zero-initialized) buffer and checks that every texel equals the encoding
/// of `exp`. Uses upstream's CPU path (all of this test's readbacks are far below 256 KiB):
/// the expected contents are the repeated texel in each row and zero in the row padding.
inline void expectSingleColor(TextureZeroInitTest& t,
                              WGPUTexture src,
                              WGPUTextureFormat format,
                              std::array<uint32_t, 3> size,
                              const PerTexelComponents& exp,
                              WGPUTextureDimension dimension,
                              uint32_t slice,
                              uint32_t mipLevel,
                              WGPUTextureAspect aspect,
                              const std::string& context) {
    if (slice != 0 && dimension != WGPUTextureDimension_2D) {
        t.fail("texture slices are only implemented for 2d textures");
    }

    format = resolvePerAspectFormat(format, aspect);
    const TextureCopyLayout layout =
        getTextureCopyLayout(format, dimension, WGPUExtent3D{size[0], size[1], size[2]}, mipLevel);
    const WGPUExtent3D copySize{
        layout.mipSize.width,
        dimension != WGPUTextureDimension_1D ? layout.mipSize.height : 1u,
        dimension == WGPUTextureDimension_3D ? layout.mipSize.depthOrArrayLayers : 1u,
    };

    const std::vector<uint8_t> expectedTexel = packTexel(format, exp);

    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.label = tzSv("expectSingleColor");
    bufferDesc.size = layout.byteLength;
    bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = src;
    source.mipLevel = mipLevel;
    source.origin = WGPUOrigin3D{0, 0, slice};
    source.aspect = aspect;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = layout.bytesPerRow;
    destination.layout.rowsPerImage = layout.rowsPerImage;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    t.submit(encoder);

    // expectGPUBufferRepeatsSingleValue (CPU path).
    const uint32_t numRows = layout.rowsPerImage * copySize.depthOrArrayLayers;
    const uint32_t minBytesPerRow = layout.minBytesPerRow;
    const uint32_t bytesPerRow = layout.bytesPerRow;
    const size_t bufferSize = static_cast<size_t>(bytesPerRow) * (numRows - 1) + minBytesPerRow;
    const size_t readSize = (bufferSize + 3u) & ~size_t{3};
    const size_t texelSize = expectedTexel.size();

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < bufferSize) {
                return std::string("readback too short");
            }
            for (size_t i = 0; i < bufferSize; ++i) {
                const size_t row = i / bytesPerRow;
                const size_t inRow = i % bytesPerRow;
                const uint8_t expected =
                    inRow < minBytesPerRow ? expectedTexel[inRow % texelSize] : static_cast<uint8_t>(0);
                if (actual[i] != expected) {
                    const size_t rowsPerImage = layout.rowsPerImage;
                    std::ostringstream msg;
                    msg << context << ": texel (x=" << (inRow / texelSize) << ", y=" << (row % rowsPerImage)
                        << ", z=" << (row / rowsPerImage) << ") byte " << (inRow % texelSize) << " of "
                        << textureFormatIdentifier(format) << ": expected " << static_cast<int>(expected)
                        << ", got " << static_cast<int>(actual[i]);
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        readSize);
}

inline std::string subresourceContext(const char* what, InitializedState state, uint32_t level, uint32_t layer) {
    std::ostringstream msg;
    msg << what << " (expected " << initializedStateName(state) << ") at mipLevel " << level << ", layer " << layer;
    return msg.str();
}

inline void checkContentsByBufferCopy(TextureZeroInitTest& t,
                                      WGPUTexture texture,
                                      InitializedState state,
                                      const SubresourceRange& subresourceRange) {
    const TextureZeroParams& params = t.p;
    for (const LevelLayer& ll : subresourceRange.each()) {
        expectSingleColor(t, texture, params.format, t.textureSize(), t.stateToTexelComponents(state),
                          params.dimension, params.dimension == WGPUTextureDimension_2D ? ll.layer : 0, ll.level,
                          params.aspect, subresourceContext("checkContentsByBufferCopy", state, ll.level, ll.layer));
    }
}

inline void checkContentsByTextureCopy(TextureZeroInitTest& t,
                                       WGPUTexture texture,
                                       InitializedState state,
                                       const SubresourceRange& subresourceRange) {
    const TextureZeroParams& params = t.p;
    for (const LevelLayer& ll : subresourceRange.each()) {
        const std::array<uint32_t, 3> mip = virtualMipSize(params.dimension, t.textureSize(), ll.level);

        WGPUTextureDescriptor dstDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dstDesc.dimension = params.dimension;
        dstDesc.size = WGPUExtent3D{mip[0], mip[1], mip[2]};
        dstDesc.format = params.format;
        dstDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc;
        WGPUTexture dst = t.createTextureTracked(dstDesc);

        WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        source.texture = texture;
        source.mipLevel = ll.level;
        source.origin = WGPUOrigin3D{0, 0, ll.layer};

        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = dst;
        destination.mipLevel = 0;

        const WGPUExtent3D copySize{mip[0], mip[1], mip[2]};
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &copySize);
        t.submit(encoder);

        // Upstream passes no `dimension`, so expectSingleColor uses its '2d' default: for a 3d
        // source only the first depth slice of the copy is read back.
        expectSingleColor(t, dst, params.format, mip, t.stateToTexelComponents(state), WGPUTextureDimension_2D, 0, 0,
                          params.aspect, subresourceContext("checkContentsByTextureCopy", state, ll.level, ll.layer));
    }
}

}  // namespace cts::texture_zero
