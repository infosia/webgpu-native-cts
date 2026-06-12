// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/image_copy/buffer_texture_copies.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85.
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,image_copy,buffer_texture_copies",
    "\ncopyTextureToBuffer and copyBufferToTexture validation tests not covered by\n"
    "the general image_copy tests, or by destroyed,*.\n");

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

ImageCopyType parseCopyType(const std::string& method) {
    if (method == "WriteTexture") return ImageCopyType::WriteTexture;
    if (method == "CopyB2T") return ImageCopyType::CopyB2T;
    if (method == "CopyT2B") return ImageCopyType::CopyT2B;
    std::abort();
}

WGPUTextureAspect parseAspect(const std::string& aspect) {
    if (aspect == "all") return WGPUTextureAspect_All;
    if (aspect == "depth-only") return WGPUTextureAspect_DepthOnly;
    if (aspect == "stencil-only") return WGPUTextureAspect_StencilOnly;
    std::abort();
}

bool depthStencilBufferTextureCopySupportedLocal(
    ImageCopyType type,
    WGPUTextureFormat format,
    WGPUTextureAspect aspect);

std::vector<Value> formatValues(const std::array<WGPUTextureFormat, 6>& formats) {
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

std::vector<Value> colorFormatValues() {
    std::vector<Value> values;
    values.reserve(kColorTextureFormats.size());
    for (WGPUTextureFormat format : kColorTextureFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

std::vector<Value> dimensionValues() {
    return {Value("1d"), Value("2d"), Value("3d")};
}

WGPUTextureDimension parseDimension(const std::string& dimension) {
    if (dimension == "1d") return WGPUTextureDimension_1D;
    if (dimension == "2d") return WGPUTextureDimension_2D;
    if (dimension == "3d") return WGPUTextureDimension_3D;
    std::abort();
}

bool formatAndDimensionPossiblyCompatible(WGPUTextureDimension dimension, WGPUTextureFormat format) {
    if (dimension == WGPUTextureDimension_1D && isCompressedTextureFormat(format)) {
        return false;
    }
    if (dimension == WGPUTextureDimension_3D &&
        (isCompressedTextureFormat(format) || isDepthOrStencilTextureFormat(format))) {
        return false;
    }
    return true;
}

bool depthStencilSupported(const ParamRecord& p) {
    std::string formatString;
    std::string aspectString;
    std::string copyTypeString;
    for (const auto& [key, value] : p) {
        if (key == "format") formatString = std::get<std::string>(value.data());
        if (key == "aspect") aspectString = std::get<std::string>(value.data());
        if (key == "copyType") copyTypeString = std::get<std::string>(value.data());
    }
    return depthStencilBufferTextureCopySupportedLocal(
        parseCopyType(copyTypeString), parseTextureFormat(formatString), parseAspect(aspectString));
}

bool depthStencilBufferTextureCopySupportedLocal(
    ImageCopyType type,
    WGPUTextureFormat format,
    WGPUTextureAspect aspect) {
    const ImageCopyType appliedType = type == ImageCopyType::WriteTexture ? ImageCopyType::CopyB2T : type;
    switch (format) {
        case WGPUTextureFormat_Stencil8:
            return aspect == WGPUTextureAspect_All || aspect == WGPUTextureAspect_StencilOnly;
        case WGPUTextureFormat_Depth16Unorm:
            return aspect == WGPUTextureAspect_All || aspect == WGPUTextureAspect_DepthOnly;
        case WGPUTextureFormat_Depth32Float:
            return appliedType == ImageCopyType::CopyT2B &&
                (aspect == WGPUTextureAspect_All || aspect == WGPUTextureAspect_DepthOnly);
        case WGPUTextureFormat_Depth24Plus:
            return false;
        case WGPUTextureFormat_Depth24PlusStencil8:
            return aspect == WGPUTextureAspect_StencilOnly;
        case WGPUTextureFormat_Depth32FloatStencil8:
            return appliedType == ImageCopyType::CopyT2B
                ? (aspect == WGPUTextureAspect_DepthOnly || aspect == WGPUTextureAspect_StencilOnly)
                : aspect == WGPUTextureAspect_StencilOnly;
        default:
            return false;
    }
}

void testCopyBufferToTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBuffer buffer,
    const WGPUTexelCopyBufferLayout& layout,
    WGPUTexture texture,
    WGPUTextureAspect aspect,
    WGPUOrigin3D origin,
    WGPUExtent3D copySize,
    bool success) {
    WGPUTexelCopyBufferInfo source = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    source.buffer = buffer;
    source.layout = layout;
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture;
    destination.origin = origin;
    destination.aspect = aspect;
    WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);
    wgpuCommandEncoderCopyBufferToTexture(encoder, &source, &destination, &copySize);
    t.expectValidationError([&] {
        WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
        if (cmd) wgpuCommandBufferRelease(cmd);
    }, !success);
    wgpuCommandEncoderRelease(encoder);
}

void testCopyTextureToBuffer(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureAspect aspect,
    WGPUOrigin3D origin,
    WGPUBuffer buffer,
    const WGPUTexelCopyBufferLayout& layout,
    WGPUExtent3D copySize,
    bool success) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.origin = origin;
    source.aspect = aspect;
    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout = layout;
    WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    t.expectValidationError([&] {
        WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
        if (cmd) wgpuCommandBufferRelease(cmd);
    }, !success);
    wgpuCommandEncoderRelease(encoder);
}

void testWriteTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureAspect aspect,
    const WGPUTexelCopyBufferLayout& layout,
    WGPUExtent3D copySize,
    uint64_t dataSize,
    bool success) {
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture;
    destination.aspect = aspect;
    std::vector<uint8_t> data(static_cast<size_t>(dataSize), 0);
    t.expectValidationError([&] {
        wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &layout, &copySize);
    }, !success);
}

CTS_TEST(g, "depth_stencil_format,copy_usage_and_aspect")
    .desc(
        "\n  Validate the combination of usage and aspect of each depth stencil format in copyBufferToTexture,\n"
        "  copyTextureToBuffer and writeTexture. See https://gpuweb.github.io/gpuweb/#depth-formats for more\n"
        "  details.\n  ")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kDepthStencilFormats))
            .beginSubcases()
            .combine("aspect", {Value("all"), Value("depth-only"), Value("stencil-only")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureAspect aspect = parseAspect(t.param<std::string>("aspect"));
        t.skipIfTextureFormatNotSupported(format);

        WGPUExtent3D size = {1, 1, 1};
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = size;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = format;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = 32;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;

        testCopyBufferToTexture(t, buffer, layout, texture, aspect, {0, 0, 0}, size,
            depthStencilBufferTextureCopySupportedLocal(ImageCopyType::CopyB2T, format, aspect));
        testCopyTextureToBuffer(t, texture, aspect, {0, 0, 0}, buffer, layout, size,
            depthStencilBufferTextureCopySupportedLocal(ImageCopyType::CopyT2B, format, aspect));
        testWriteTexture(t, texture, aspect, layout, size, 32,
            depthStencilBufferTextureCopySupportedLocal(ImageCopyType::WriteTexture, format, aspect));
    });

CTS_TEST(g, "depth_stencil_format,copy_buffer_size")
    .desc(
        "\n  Validate the minimum buffer size for each depth stencil format in copyBufferToTexture,\n"
        "  copyTextureToBuffer and writeTexture.\n\n"
        "  Given a depth stencil format, a copy aspect ('depth-only' or 'stencil-only'), the copy method\n"
        "  (buffer-to-texture or texture-to-buffer) and the copy size, validate\n"
        "  - if the copy can be successfully executed with the minimum required buffer size.\n"
        "  - if the copy fails with a validation error when the buffer size is less than the minimum\n"
        "  required buffer size.\n  ")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kDepthStencilFormats))
            .combine("aspect", {Value("depth-only"), Value("stencil-only")})
            .combine("copyType", {Value("CopyB2T"), Value("CopyT2B"), Value("WriteTexture")})
            .filter(depthStencilSupported)
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"copySize", Value("8x1x1")}},
                ParamRecord{{"copySize", Value("4x4x1")}},
                ParamRecord{{"copySize", Value("4x4x3")}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureAspect aspect = parseAspect(t.param<std::string>("aspect"));
        std::string copyType = t.param<std::string>("copyType");
        std::string sizeKey = t.param<std::string>("copySize");
        t.skipIfTextureFormatNotSupported(format);
        WGPUExtent3D size = sizeKey == "8x1x1" ? WGPUExtent3D{8, 1, 1}
            : (sizeKey == "4x4x1" ? WGPUExtent3D{4, 4, 1} : WGPUExtent3D{4, 4, 3});

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = size;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = format;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        uint32_t texelSize = depthStencilFormatAspectSize(format, aspect);
        uint64_t bprAlignment = copyType == "WriteTexture" ? 1 : kBytesPerRowAlignment;
        uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(texelSize * size.width, bprAlignment));
        uint32_t rowsPerImage = size.height;
        uint64_t minSize = static_cast<uint64_t>(bytesPerRow) * (rowsPerImage * size.depthOrArrayLayers - 1) +
            alignTo(texelSize * size.width, kBufferCopyAlignment);

        WGPUBufferDescriptor bigDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bigDesc.size = minSize;
        bigDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer big = t.createBufferTracked(bigDesc);
        WGPUBufferDescriptor smallDesc = bigDesc;
        smallDesc.size = minSize - kBufferCopyAlignment;
        WGPUBuffer small = t.createBufferTracked(smallDesc);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.bytesPerRow = bytesPerRow;
        layout.rowsPerImage = rowsPerImage;

        if (copyType == "CopyB2T") {
            testCopyBufferToTexture(t, big, layout, texture, aspect, {0, 0, 0}, size, true);
            testCopyBufferToTexture(t, small, layout, texture, aspect, {0, 0, 0}, size, false);
        } else if (copyType == "CopyT2B") {
            testCopyTextureToBuffer(t, texture, aspect, {0, 0, 0}, big, layout, size, true);
            testCopyTextureToBuffer(t, texture, aspect, {0, 0, 0}, small, layout, size, false);
        } else {
            testWriteTexture(t, texture, aspect, layout, size, minSize, true);
            testWriteTexture(t, texture, aspect, layout, size, minSize - kBufferCopyAlignment, false);
        }
    });

CTS_TEST(g, "depth_stencil_format,copy_buffer_offset")
    .desc(
        "\n    Validate for every depth stencil formats the buffer offset must be a multiple of 4 in\n"
        "    copyBufferToTexture() and copyTextureToBuffer(), but the offset in writeTexture() doesn't always\n"
        "    need to be a multiple of 4.\n    ")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kDepthStencilFormats))
            .combine("aspect", {Value("depth-only"), Value("stencil-only")})
            .combine("copyType", {Value("CopyB2T"), Value("CopyT2B"), Value("WriteTexture")})
            .filter(depthStencilSupported)
            .beginSubcases()
            .combine("offset", {1, 2, 4, 6, 8});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureAspect aspect = parseAspect(t.param<std::string>("aspect"));
        std::string copyType = t.param<std::string>("copyType");
        uint64_t offset = t.param<int64_t>("offset");
        t.skipIfTextureFormatNotSupported(format);

        WGPUExtent3D size = {4, 4, 1};
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = size;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = format;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        uint32_t texelSize = depthStencilFormatAspectSize(format, aspect);
        uint64_t bprAlignment = copyType == "WriteTexture" ? 1 : kBytesPerRowAlignment;
        uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(texelSize * size.width, bprAlignment));
        uint64_t minSize = static_cast<uint64_t>(bytesPerRow) * (size.height * size.depthOrArrayLayers - 1) +
            alignTo(texelSize * size.width, kBufferCopyAlignment);
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = alignTo(minSize + offset, kBufferCopyAlignment);
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = offset;
        layout.bytesPerRow = bytesPerRow;
        layout.rowsPerImage = size.height;
        bool success = copyType == "WriteTexture" || offset % 4 == 0;
        if (copyType == "CopyB2T") {
            testCopyBufferToTexture(t, buffer, layout, texture, aspect, {0, 0, 0}, size, success);
        } else if (copyType == "CopyT2B") {
            testCopyTextureToBuffer(t, texture, aspect, {0, 0, 0}, buffer, layout, size, success);
        } else {
            testWriteTexture(t, texture, aspect, layout, size, minSize + offset, success);
        }
    });

CTS_TEST(g, "sample_count")
    .desc("\n  Test that the texture sample count. Check that a validation error is generated if sample count is\n  not 1.\n  ")
    .params([](ParamsBuilder u) {
        return u.combine("copyType", {Value("CopyB2T"), Value("CopyT2B")})
            .beginSubcases()
            .combine("sampleCount", {1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string copyType = t.param<std::string>("copyType");
        uint32_t sampleCount = static_cast<uint32_t>(t.param<int64_t>("sampleCount"));
        WGPUTextureUsage usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        if (sampleCount > 1) usage |= WGPUTextureUsage_RenderAttachment;
        WGPUExtent3D size = {16, 1, 1};
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = size;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.sampleCount = sampleCount;
        texDesc.format = WGPUTextureFormat_BGRA8Unorm;
        texDesc.usage = usage;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = 64;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        bool success = sampleCount == 1;
        if (copyType == "CopyB2T") {
            testCopyBufferToTexture(t, buffer, layout, texture, WGPUTextureAspect_All, {0, 0, 0}, size, success);
        } else {
            testCopyTextureToBuffer(t, texture, WGPUTextureAspect_All, {0, 0, 0}, buffer, layout, size, success);
        }
    });

CTS_TEST(g, "texture_buffer_usages")
    .desc(
        "\n  Tests calling copyTextureToBuffer or copyBufferToTexture with the texture and the buffer missed\n"
        "  COPY_SRC, COPY_DST usage respectively.\n    - texture and buffer {with, without} COPY_SRC and COPY_DST usage.\n  ")
    .params([](ParamsBuilder u) {
        std::vector<Value> texUsages;
        for (WGPUTextureUsage usage : kTextureUsages) texUsages.emplace_back(static_cast<int64_t>(usage));
        std::vector<Value> bufUsages;
        for (WGPUBufferUsage usage : kBufferUsages) bufUsages.emplace_back(static_cast<int64_t>(usage));
        return u.combine("copyType", {Value("CopyB2T"), Value("CopyT2B")})
            .beginSubcases()
            .combine("textureUsage", texUsages)
            .filter([](const ParamRecord& p) {
                return valueAs<int64_t>(*findParam(p, "textureUsage")) != WGPUTextureUsage_TransientAttachment;
            })
            .expand("_textureUsageValid", [](const ParamRecord& p) {
                std::string copyType = valueAs<std::string>(*findParam(p, "copyType"));
                WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(valueAs<int64_t>(*findParam(p, "textureUsage")));
                WGPUTextureUsage required = copyType == "CopyT2B" ? WGPUTextureUsage_CopySrc : WGPUTextureUsage_CopyDst;
                return std::vector<Value>{usage == required};
            })
            .combine("bufferUsage", bufUsages)
            .expand("_bufferUsageValid", [](const ParamRecord& p) {
                std::string copyType = valueAs<std::string>(*findParam(p, "copyType"));
                WGPUBufferUsage usage = static_cast<WGPUBufferUsage>(valueAs<int64_t>(*findParam(p, "bufferUsage")));
                WGPUBufferUsage required = copyType == "CopyB2T" ? WGPUBufferUsage_CopySrc : WGPUBufferUsage_CopyDst;
                return std::vector<Value>{usage == required};
            })
            .filter([](const ParamRecord& p) {
                return valueAs<bool>(*findParam(p, "_textureUsageValid")) ||
                    valueAs<bool>(*findParam(p, "_bufferUsageValid"));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string copyType = t.param<std::string>("copyType");
        WGPUTextureUsage textureUsage = static_cast<WGPUTextureUsage>(t.param<int64_t>("textureUsage"));
        WGPUBufferUsage bufferUsage = static_cast<WGPUBufferUsage>(t.param<int64_t>("bufferUsage"));
        bool texValid = t.param<bool>("_textureUsageValid");
        bool bufValid = t.param<bool>("_bufferUsageValid");
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = {16, 16, 1};
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = textureUsage;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = 32;
        bufDesc.usage = bufferUsage;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        WGPUExtent3D size = {1, 1, 1};
        bool success = texValid && bufValid;
        if (copyType == "CopyB2T") {
            testCopyBufferToTexture(t, buffer, layout, texture, WGPUTextureAspect_All, {0, 0, 0}, size, success);
        } else {
            testCopyTextureToBuffer(t, texture, WGPUTextureAspect_All, {0, 0, 0}, buffer, layout, size, success);
        }
    });

CTS_TEST(g, "device_mismatch")
    .desc(
        "\n    Tests copyBufferToTexture and copyTextureToBuffer cannot be called with a buffer or a texture\n"
        "    created from another device.\n  ")
    .params([](ParamsBuilder u) {
        return u.combine("copyType", {Value("CopyB2T"), Value("CopyT2B")})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"bufMismatched", false}, {"texMismatched", false}},
                ParamRecord{{"bufMismatched", true}, {"texMismatched", false}},
                ParamRecord{{"bufMismatched", false}, {"texMismatched", true}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string copyType = t.param<std::string>("copyType");
        bool bufMismatched = t.param<bool>("bufMismatched");
        bool texMismatched = t.param<bool>("texMismatched");
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = 32;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = bufMismatched ? t.createBufferOnMismatchedDevice(bufDesc) : t.createBufferTracked(bufDesc);
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = {1, 1, 1};
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = wgpuDeviceCreateTexture(texMismatched ? t.mismatchedDevice() : t.device(), &texDesc);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        bool success = !bufMismatched && !texMismatched;
        if (copyType == "CopyB2T") {
            testCopyBufferToTexture(t, buffer, layout, texture, WGPUTextureAspect_All, {0, 0, 0}, texDesc.size, success);
        } else {
            testCopyTextureToBuffer(t, texture, WGPUTextureAspect_All, {0, 0, 0}, buffer, layout, texDesc.size, success);
        }
        if (texture) wgpuTextureRelease(texture);
    });

CTS_TEST(g, "offset_and_bytesPerRow")
    .desc(
        "Test that for copyBufferToTexture, and copyTextureToBuffer\n"
        "     * bytesPerRow must be a multiple of 256\n"
        "     * offset must be a multiple of bytesPerBlock\n"
        "     * the last row does not need to be a multiple of 256\n"
        "       In other words, If the copy size is 4x2 of a r8unorm texture that's 4 bytes per row.\n"
        "       To get from row 0 to row 1 in the buffer, bytesPerRow must be a multiple of 256.\n"
        "       But, the size requirement for the buffer is only 256 + 4, not 256 * 2\n"
        "     * origin.x must be a multiple of blockWidth\n"
        "     * origin.y must be a multiple of blockHeight\n"
        "     * copySize.width must be a multiple of blockWidth\n"
        "     * copySize.height must be a multiple of blockHeight\n")
    .params([](ParamsBuilder u) {
        return u.combine("format", colorFormatValues())
            .combine("copyType", {Value("CopyB2T"), Value("CopyT2B")})
            .combine("dimension", dimensionValues())
            .filter([](const ParamRecord& p) {
                WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                WGPUTextureDimension dim = parseDimension(valueAs<std::string>(*findParam(p, "dimension")));
                return formatAndDimensionPossiblyCompatible(dim, format);
            })
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"xInBlocks", 1.0}, {"yInBlocks", 1.0}, {"copyWidthInBlocks", 64.0}, {"copyHeightInBlocks", 2.0}, {"offsetInBlocks", 1.0}, {"bytesPerRowAlign", 256}},
                ParamRecord{{"xInBlocks", 0.0}, {"yInBlocks", 0.0}, {"copyWidthInBlocks", 64.0}, {"copyHeightInBlocks", 2.0}, {"offsetInBlocks", 1.5}, {"bytesPerRowAlign", 256}},
                ParamRecord{{"xInBlocks", 0.0}, {"yInBlocks", 0.0}, {"copyWidthInBlocks", 64.0}, {"copyHeightInBlocks", 2.0}, {"offsetInBlocks", 0.0}, {"bytesPerRowAlign", 128}},
                ParamRecord{{"xInBlocks", 0.0}, {"yInBlocks", 0.0}, {"copyWidthInBlocks", 64.0}, {"copyHeightInBlocks", 2.0}, {"offsetInBlocks", 0.0}, {"bytesPerRowAlign", 384}},
                ParamRecord{{"xInBlocks", 1.5}, {"yInBlocks", 0.0}, {"copyWidthInBlocks", 64.0}, {"copyHeightInBlocks", 2.0}, {"offsetInBlocks", 0.0}, {"bytesPerRowAlign", 256}},
                ParamRecord{{"xInBlocks", 0.0}, {"yInBlocks", 1.5}, {"copyWidthInBlocks", 64.0}, {"copyHeightInBlocks", 2.0}, {"offsetInBlocks", 0.0}, {"bytesPerRowAlign", 256}},
                ParamRecord{{"xInBlocks", 0.0}, {"yInBlocks", 0.0}, {"copyWidthInBlocks", 64.5}, {"copyHeightInBlocks", 2.0}, {"offsetInBlocks", 0.0}, {"bytesPerRowAlign", 256}},
                ParamRecord{{"xInBlocks", 0.0}, {"yInBlocks", 0.0}, {"copyWidthInBlocks", 64.0}, {"copyHeightInBlocks", 2.5}, {"offsetInBlocks", 0.0}, {"bytesPerRowAlign", 256}},
            })
            .filter([](const ParamRecord& p) {
                WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                const TextureBlockInfo info = getBlockInfoForColorTextureFormat(format);
                double offsetInBlocks = valueAs<double>(*findParam(p, "offsetInBlocks"));
                double copyWidthInBlocks = valueAs<double>(*findParam(p, "copyWidthInBlocks"));
                double copyHeightInBlocks = valueAs<double>(*findParam(p, "copyHeightInBlocks"));
                bool fractional = std::fmod(offsetInBlocks, 1.0) != 0.0 ||
                    std::fmod(copyWidthInBlocks, 1.0) != 0.0 ||
                    std::fmod(copyHeightInBlocks, 1.0) != 0.0;
                return !(fractional && info.bytesPerBlock > 1);
            })
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    valueAs<double>(*findParam(p, "yInBlocks")) > 0.0);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string copyType = t.param<std::string>("copyType");
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForColorTextureFormat(format);
        double xInBlocks = t.param<double>("xInBlocks");
        double yInBlocks = t.param<double>("yInBlocks");
        double offsetInBlocks = t.param<double>("offsetInBlocks");
        double copyWidthInBlocks = t.param<double>("copyWidthInBlocks");
        double copyHeightInBlocks = t.param<double>("copyHeightInBlocks");
        uint32_t bytesPerRowAlign = static_cast<uint32_t>(t.param<int64_t>("bytesPerRowAlign"));

        uint32_t widthBlocks = static_cast<uint32_t>(std::ceil(xInBlocks)) + static_cast<uint32_t>(std::ceil(copyWidthInBlocks));
        uint32_t heightBlocks = static_cast<uint32_t>(std::ceil(yInBlocks)) + static_cast<uint32_t>(std::ceil(copyHeightInBlocks));
        double copySizeBlockY = copyHeightInBlocks;
        if (dimension == WGPUTextureDimension_1D) {
            copySizeBlockY = 1.0;
            heightBlocks = 1;
        }
        WGPUOrigin3D origin = {
            static_cast<uint32_t>(std::ceil(xInBlocks * info.blockWidth)),
            static_cast<uint32_t>(std::ceil(yInBlocks * info.blockHeight)),
            0,
        };
        WGPUExtent3D copySize = {
            static_cast<uint32_t>(std::ceil(copyWidthInBlocks * info.blockWidth)),
            static_cast<uint32_t>(std::ceil(copySizeBlockY * info.blockHeight)),
            1,
        };
        WGPUExtent3D textureSize = {widthBlocks * info.blockWidth, heightBlocks * info.blockHeight, 1};
        uint32_t textureBytesPerRow = info.bytesPerBlock * widthBlocks;
        uint32_t rowsPerImage = static_cast<uint32_t>(std::ceil(copySizeBlockY));
        uint64_t offset = static_cast<uint64_t>(std::ceil(offsetInBlocks * info.bytesPerBlock));
        uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(textureBytesPerRow, bytesPerRowAlign));
        uint64_t bufferSize = offset + static_cast<uint64_t>(rowsPerImage - 1) * bytesPerRow + textureBytesPerRow;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = bufferSize;
        bufDesc.usage = copyType == "CopyB2T" ? WGPUBufferUsage_CopySrc : WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = textureSize;
        texDesc.dimension = dimension;
        texDesc.format = format;
        texDesc.usage = copyType == "CopyB2T" ? WGPUTextureUsage_CopyDst : WGPUTextureUsage_CopySrc;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        bool success = offset % info.bytesPerBlock == 0 &&
            bytesPerRow % 256 == 0 &&
            origin.x % info.blockWidth == 0 &&
            origin.y % info.blockHeight == 0 &&
            copySize.width % info.blockWidth == 0 &&
            copySize.height % info.blockHeight == 0;
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = offset;
        layout.bytesPerRow = bytesPerRow;
        if (copyType == "CopyB2T") {
            testCopyBufferToTexture(t, buffer, layout, texture, WGPUTextureAspect_All, origin, copySize, success);
        } else {
            testCopyTextureToBuffer(t, texture, WGPUTextureAspect_All, origin, buffer, layout, copySize, success);
        }
    });

} // namespace
