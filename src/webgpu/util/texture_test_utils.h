// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/texture_test_utils.ts, src/webgpu/util/texture/texture_ok.ts and
// the GPUTest.expectSingleColor helper of src/webgpu/gpu_test.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Header-only texel expectation helpers shared by the api/operation render-target tests:
// - compareTexelToColor: texture_ok.ts makeTexelViewComparer's per-texel predicate against an
//   expected color fed through TexelView.fromTexelsAsColors.
// - expectSingleColor: GPUTest.expectSingleColor (exact bytes of pack(encode(exp))).
// - expectSingleColorWithTolerance: texture_test_utils.ts expectSingleColorWithTolerance.
// - expectSinglePixelComparisonsAreOkInTexture: texture_test_utils.ts, colour or byte expectations.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/webgpu.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texture_layout.h"

namespace cts {

/// texture_ok.ts TexelCompareOptions. Upstream's default (used when a test passes no options) is
/// { maxIntDiff: 0, maxDiffULPsForNormFormat: 1, maxDiffULPsForFloatFormat: 1 }.
struct TexelCompareOptions {
    double maxIntDiff = 0.0;
    std::optional<double> maxFractionalDiff;
    std::optional<int64_t> maxDiffULPsForNormFormat;
    std::optional<int64_t> maxDiffULPsForFloatFormat;
};

inline TexelCompareOptions defaultTexelCompareOptions() {
    TexelCompareOptions options;
    options.maxDiffULPsForNormFormat = 1;
    options.maxDiffULPsForFloatFormat = 1;
    return options;
}

/// Port of texture_ok.ts makeTexelViewComparer's predicate plus comparePerComponent, for one
/// texel: `actual` holds the texel's bytes; `expected` is a colour (R, G, B, A) fed through
/// TexelView.fromTexelsAsColors (so its `color` is the unquantized value and its ulpFromZero is
/// bitsToULPFromZero(numberToBits(color))). Only the format's own components are compared.
inline std::optional<std::string> compareTexelToColor(
    WGPUTextureFormat format,
    const uint8_t* actual,
    size_t len,
    const std::array<double, 4>& expected,
    const TexelCompareOptions& options,
    WGPUOrigin3D coord = WGPUOrigin3D{0, 0, 0}) {
    const std::string_view identifier = textureFormatIdentifier(format);
    const bool fmtIsInt = identifier.find("int") != std::string_view::npos;
    const bool fmtIsNorm = identifier.find("norm") != std::string_view::npos;
    const bool fmtIsFloat = identifier.find("float") != std::string_view::npos;
    const TexelRepresentation& repr = texelRepresentation(format);
    if (len < repr.bytesPerBlock) {
        return std::string("readback too small for one texel");
    }
    const TexelComponents act = repr.bitsToNumber(repr.unpackBits(actual, repr.bytesPerBlock));

    const bool useNormUlp = !fmtIsInt && fmtIsNorm && options.maxDiffULPsForNormFormat.has_value();
    const bool useFloatUlp =
        !fmtIsInt && !useNormUlp && fmtIsFloat && options.maxDiffULPsForFloatFormat.has_value();
    if (!fmtIsInt && !useNormUlp && !useFloatUlp && !options.maxFractionalDiff.has_value()) {
        return std::string("need maxFractionalDiff or maxDiffULPs to compare this format");
    }

    for (TexelComponent component : repr.componentOrder) {
        const uint32_t index = static_cast<uint32_t>(component);
        const double a = act.values[index];
        const double e = expected[index];
        bool ok = false;
        std::ostringstream detail;
        if (fmtIsInt) {
            ok = texelComponentEqual(a, e, options.maxIntDiff);
            detail << "expected " << e << ", got " << a << " (tolerance +-" << options.maxIntDiff << ")";
        } else if (useNormUlp || useFloatUlp) {
            const int64_t tolerance =
                useNormUlp ? *options.maxDiffULPsForNormFormat : *options.maxDiffULPsForFloatFormat;
            // The actual texel's bits decode exactly and re-encode to the same bits, so the
            // round-trip through ulpFromZero equals upstream's bitsToULPFromZero(unpackBits(...)).
            const int64_t actUlp = repr.ulpFromZero(index, a);
            const int64_t expUlp = repr.ulpFromZero(index, e);
            const int64_t diff = actUlp - expUlp;
            ok = (diff < 0 ? -diff : diff) <= tolerance;
            detail << "expected " << e << " (" << expUlp << " normal-ULPs from zero), got " << a << " ("
                   << actUlp << " normal-ULPs from zero), tolerance +-" << tolerance << " normal-ULPs";
        } else {
            ok = texelComponentEqual(a, e, *options.maxFractionalDiff);
            detail << "expected " << e << ", got " << a << " (tolerance +-" << *options.maxFractionalDiff << ")";
        }
        if (!ok) {
            std::ostringstream message;
            message << identifier << " texel mismatch at (" << coord.x << ", " << coord.y << ", " << coord.z
                    << ") component " << "RGBA"[index] << ": " << detail.str();
            return message.str();
        }
    }
    return std::nullopt;
}

/// Decodes the bytes of one texel of `format` to its colour (upstream TexelView.fromTexelsAsBytes
/// `color`), indexed R, G, B, A.
inline std::array<double, 4> texelBytesToColor(WGPUTextureFormat format, const std::vector<uint8_t>& bytes) {
    const TexelRepresentation& repr = texelRepresentation(format);
    return repr.bitsToNumber(repr.unpackBits(bytes.data(), bytes.size())).values;
}

/// Per-texel components for expectSingleColor (upstream PerTexelComponent<number>).
struct TexelValue {
    double R = 0;
    double G = 0;
    double B = 0;
    double A = 0;
    double Depth = 0;
    double Stencil = 0;
};

inline TexelValue colorTexelValue(const std::array<double, 4>& rgba) {
    TexelValue value;
    value.R = rgba[0];
    value.G = rgba[1];
    value.B = rgba[2];
    value.A = rgba[3];
    return value;
}

/// Upstream `resolvePerAspectFormat`.
inline WGPUTextureFormat resolveFormatForAspect(WGPUTextureFormat format, WGPUTextureAspect aspect) {
    if (aspect == WGPUTextureAspect_StencilOnly && isStencilTextureFormat(format)) {
        return WGPUTextureFormat_Stencil8;
    }
    if (aspect == WGPUTextureAspect_DepthOnly) {
        if (format == WGPUTextureFormat_Depth24PlusStencil8) {
            return WGPUTextureFormat_Depth24Plus;
        }
        if (format == WGPUTextureFormat_Depth32FloatStencil8) {
            return WGPUTextureFormat_Depth32Float;
        }
    }
    return format;
}

/// Upstream `kTexelRepresentationInfo[format].pack(rep.encode(components))`: colour formats use the
/// shared texel representation; the per-aspect depth/stencil formats are encoded here.
inline std::vector<uint8_t> encodeTexelValue(WGPUTextureFormat format, const TexelValue& value) {
    switch (format) {
        case WGPUTextureFormat_Depth16Unorm: {
            const uint16_t v = static_cast<uint16_t>(std::llround(value.Depth * 65535.0));
            return {static_cast<uint8_t>(v & 0xffu), static_cast<uint8_t>(v >> 8)};
        }
        case WGPUTextureFormat_Depth32Float: {
            const float f = static_cast<float>(value.Depth);
            std::vector<uint8_t> out(4);
            std::memcpy(out.data(), &f, 4);
            return out;
        }
        case WGPUTextureFormat_Stencil8:
            return {static_cast<uint8_t>(value.Stencil)};
        default:
            break;
    }
    const TexelRepresentation& repr = texelRepresentation(format);
    TexelComponents numbers;
    numbers.values = {value.R, value.G, value.B, value.A};
    return repr.packBits(repr.numberToBits(numbers));
}

/// Fills the members of a WGPUDepthStencilState that the JS API defaults when omitted
/// (GPUStencilFaceState {compare: 'always', failOp/depthFailOp/passOp: 'keep'}); the C *_INIT
/// macros leave them Undefined. depthCompare / depthWriteEnabled are left to the caller.
inline void applyJsStencilFaceDefaults(WGPUDepthStencilState& state) {
    for (WGPUStencilFaceState* face : {&state.stencilFront, &state.stencilBack}) {
        face->compare = WGPUCompareFunction_Always;
        face->failOp = WGPUStencilOperation_Keep;
        face->depthFailOp = WGPUStencilOperation_Keep;
        face->passOp = WGPUStencilOperation_Keep;
    }
    state.stencilReadMask = 0xFFFFFFFFu;
    state.stencilWriteMask = 0xFFFFFFFFu;
    state.depthBias = 0;
    state.depthBiasSlopeScale = 0.0f;
    state.depthBiasClamp = 0.0f;
}

namespace texture_test_utils_detail {

inline uint64_t alignUp(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

inline void submitEncoder(GpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

inline WGPUBuffer createReadbackBuffer(GpuTest& t, uint64_t size) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    return t.createBufferTracked(desc);
}

inline void encodeCopy(GpuTest& t,
                       WGPUTexture src,
                       uint32_t mipLevel,
                       WGPUOrigin3D origin,
                       WGPUTextureAspect aspect,
                       WGPUBuffer buffer,
                       uint32_t bytesPerRow,
                       uint32_t rowsPerImage,
                       WGPUExtent3D copySize) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = src;
    source.mipLevel = mipLevel;
    source.origin = origin;
    source.aspect = aspect;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = bytesPerRow;
    destination.layout.rowsPerImage = rowsPerImage;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    submitEncoder(t, encoder);
}

inline WGPUExtent3D singleColorCopySize(const TextureCopyLayout& layout, WGPUTextureDimension dimension) {
    return WGPUExtent3D{
        layout.mipSize.width,
        dimension != WGPUTextureDimension_1D ? layout.mipSize.height : 1u,
        dimension == WGPUTextureDimension_3D ? layout.mipSize.depthOrArrayLayers : 1u,
    };
}

} // namespace texture_test_utils_detail

/// Upstream GPUTest.expectSingleColor: copies one subresource (2d: one `slice`, 3d: the whole mip)
/// of `src` (aspect `aspect`, mip `mipLevel`) into a fresh zero-initialized buffer and checks that
/// every texel equals `pack(encode(expected))` and the row padding is zero
/// (expectGPUBufferRepeatsSingleValue).
inline void expectSingleColor(GpuTest& t,
                              WGPUTexture src,
                              WGPUTextureFormat format,
                              std::array<uint32_t, 3> size,
                              const TexelValue& expected,
                              WGPUTextureDimension dimension = WGPUTextureDimension_2D,
                              uint32_t slice = 0,
                              uint32_t mipLevel = 0,
                              WGPUTextureAspect aspect = WGPUTextureAspect_All) {
    namespace detail = texture_test_utils_detail;
    if (slice != 0 && dimension != WGPUTextureDimension_2D) {
        t.fail("texture slices are only implemented for 2d textures");
    }
    format = resolveFormatForAspect(format, aspect);
    const TextureCopyLayout layout =
        getTextureCopyLayout(format, dimension, WGPUExtent3D{size[0], size[1], size[2]}, mipLevel);
    const WGPUExtent3D copySize = detail::singleColorCopySize(layout, dimension);
    const std::vector<uint8_t> expectedTexel = encodeTexelValue(format, expected);

    WGPUBuffer buffer = detail::createReadbackBuffer(t, layout.byteLength);
    detail::encodeCopy(t, src, mipLevel, WGPUOrigin3D{0, 0, slice}, aspect, buffer, layout.bytesPerRow,
                       layout.rowsPerImage, copySize);

    const uint32_t numRows = layout.rowsPerImage * copySize.depthOrArrayLayers;
    const uint32_t minBytesPerRow = layout.minBytesPerRow;
    const uint32_t bytesPerRow = layout.bytesPerRow;
    const uint32_t rowsPerImage = layout.rowsPerImage;
    const size_t bufferSize = static_cast<size_t>(bytesPerRow) * (numRows - 1) + minBytesPerRow;
    const size_t readSize = (bufferSize + 3u) & ~size_t{3};
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [expectedTexel, bufferSize, bytesPerRow, minBytesPerRow, rowsPerImage, format](
            const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < bufferSize) {
                return std::string("readback too short");
            }
            const size_t texelSize = expectedTexel.size();
            for (size_t i = 0; i < bufferSize; ++i) {
                const size_t row = i / bytesPerRow;
                const size_t inRow = i % bytesPerRow;
                const uint8_t want =
                    inRow < minBytesPerRow ? expectedTexel[inRow % texelSize] : static_cast<uint8_t>(0);
                if (actual[i] != want) {
                    std::ostringstream msg;
                    msg << "expectSingleColor: texel (x=" << (inRow / texelSize) << ", y=" << (row % rowsPerImage)
                        << ", z=" << (row / rowsPerImage) << ") byte " << (inRow % texelSize) << " of "
                        << textureFormatIdentifier(format) << ": expected " << static_cast<int>(want)
                        << ", got " << static_cast<int>(actual[i]);
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        0,
        readSize);
}

/// Upstream texture_test_utils.ts expectSingleColorWithTolerance: every texel of one subresource
/// (2d: one `slice`) compares to the colour `expected` with { maxFractionalDiff } (integer formats
/// compare exactly, upstream's default maxIntDiff 0).
inline void expectSingleColorWithTolerance(GpuTest& t,
                                           WGPUTexture src,
                                           WGPUTextureFormat format,
                                           std::array<uint32_t, 3> size,
                                           const std::array<double, 4>& expected,
                                           uint32_t slice,
                                           uint32_t mipLevel,
                                           double maxFractionalDiff) {
    namespace detail = texture_test_utils_detail;
    const WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    const TextureCopyLayout layout =
        getTextureCopyLayout(format, dimension, WGPUExtent3D{size[0], size[1], size[2]}, mipLevel);
    const WGPUExtent3D copySize = detail::singleColorCopySize(layout, dimension);

    WGPUBuffer buffer = detail::createReadbackBuffer(t, layout.byteLength);
    detail::encodeCopy(t, src, mipLevel, WGPUOrigin3D{0, 0, slice}, WGPUTextureAspect_All, buffer,
                       layout.bytesPerRow, layout.rowsPerImage, copySize);

    TexelCompareOptions options;
    options.maxFractionalDiff = maxFractionalDiff;
    const uint32_t bytesPerRow = layout.bytesPerRow;
    const uint32_t rowsPerImage = layout.rowsPerImage;
    const uint32_t bytesPerBlock = layout.bytesPerBlock;
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [=](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t z = 0; z < copySize.depthOrArrayLayers; ++z) {
                for (uint32_t y = 0; y < copySize.height; ++y) {
                    for (uint32_t x = 0; x < copySize.width; ++x) {
                        const size_t offset = (static_cast<size_t>(z) * rowsPerImage + y) * bytesPerRow
                            + static_cast<size_t>(x) * bytesPerBlock;
                        if (offset + bytesPerBlock > len) {
                            return std::string("readback too short");
                        }
                        if (auto error = compareTexelToColor(format, actual + offset, bytesPerBlock, expected, options,
                                                             WGPUOrigin3D{x, y, slice + z})) {
                            return error;
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(layout.byteLength));
}

/// One expectation of expectSinglePixelComparisonsAreOkInTexture: an expected colour (R, G, B, A),
/// e.g. built from bytes with texelBytesToColor.
struct PixelComparison {
    WGPUOrigin3D coord = WGPUOrigin3D{0, 0, 0};
    std::array<double, 4> exp = {0, 0, 0, 0};
};

/// Upstream texture_test_utils.ts expectSinglePixelComparisonsAreOkInTexture: copies the minimal
/// sub-rectangle covering every coordinate out of mip `mipLevel` of `texture` (`z` is the array
/// layer of a 2d texture) and compares each listed texel with `options`.
inline void expectSinglePixelComparisonsAreOkInTexture(
    GpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mipLevel,
    const std::vector<PixelComparison>& comparisons,
    const TexelCompareOptions& options = defaultTexelCompareOptions()) {
    namespace detail = texture_test_utils_detail;
    if (comparisons.empty()) {
        t.fail("must specify at least one pixel comparison");
    }
    WGPUOrigin3D lower = comparisons[0].coord;
    WGPUOrigin3D upper = comparisons[0].coord;
    for (const PixelComparison& c : comparisons) {
        lower.x = std::min(lower.x, c.coord.x);
        lower.y = std::min(lower.y, c.coord.y);
        lower.z = std::min(lower.z, c.coord.z);
        upper.x = std::max(upper.x, c.coord.x);
        upper.y = std::max(upper.y, c.coord.y);
        upper.z = std::max(upper.z, c.coord.z);
    }
    const WGPUExtent3D size{upper.x - lower.x + 1, upper.y - lower.y + 1, upper.z - lower.z + 1};
    const uint32_t bytesPerBlock = texelRepresentation(format).bytesPerBlock;
    const uint32_t bytesPerRow =
        static_cast<uint32_t>(detail::alignUp(static_cast<uint64_t>(size.width) * bytesPerBlock, kBytesPerRowAlignment));
    const uint32_t rowsPerImage = size.height;
    const uint64_t byteLength = detail::alignUp(
        static_cast<uint64_t>(bytesPerRow) * rowsPerImage * (size.depthOrArrayLayers - 1)
            + static_cast<uint64_t>(bytesPerRow) * (size.height - 1)
            + static_cast<uint64_t>(size.width) * bytesPerBlock,
        kBufferCopyAlignment);

    WGPUBuffer buffer = detail::createReadbackBuffer(t, byteLength);
    detail::encodeCopy(t, texture, mipLevel, lower, WGPUTextureAspect_All, buffer, bytesPerRow, rowsPerImage, size);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [=](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (const PixelComparison& c : comparisons) {
                const size_t offset =
                    (static_cast<size_t>(c.coord.z - lower.z) * rowsPerImage + (c.coord.y - lower.y)) * bytesPerRow
                    + static_cast<size_t>(c.coord.x - lower.x) * bytesPerBlock;
                if (offset + bytesPerBlock > len) {
                    return std::string("readback too short");
                }
                if (auto error = compareTexelToColor(format, actual + offset, bytesPerBlock, c.exp, options, c.coord)) {
                    return error;
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

} // namespace cts
