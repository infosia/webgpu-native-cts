// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/image_copy/texture_related.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85.
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <algorithm>
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
    "api,validation,image_copy,texture_related",
    "Texture related validation tests for B2T copy and T2B copy and writeTexture.");

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::vector<Value> imageCopyTypeValues() {
    return {Value("WriteTexture"), Value("CopyB2T"), Value("CopyT2B")};
}

ImageCopyType parseMethod(const std::string& method) {
    if (method == "WriteTexture") return ImageCopyType::WriteTexture;
    if (method == "CopyB2T") return ImageCopyType::CopyB2T;
    if (method == "CopyT2B") return ImageCopyType::CopyT2B;
    std::abort();
}

WGPUTextureDimension parseDimension(const std::string& dimension) {
    if (dimension == "1d") return WGPUTextureDimension_1D;
    if (dimension == "2d") return WGPUTextureDimension_2D;
    if (dimension == "3d") return WGPUTextureDimension_3D;
    std::abort();
}

std::string dimensionName(WGPUTextureDimension dimension) {
    if (dimension == WGPUTextureDimension_1D) return "1d";
    if (dimension == WGPUTextureDimension_2D) return "2d";
    if (dimension == WGPUTextureDimension_3D) return "3d";
    std::abort();
}

std::vector<Value> dimensionValues() {
    std::vector<Value> values;
    for (WGPUTextureDimension dimension : kTextureDimensions) values.emplace_back(dimensionName(dimension));
    return values;
}

std::vector<WGPUTextureFormat> sizedFormats() {
    std::vector<WGPUTextureFormat> formats;
    for (WGPUTextureFormat format : kAllTextureFormats) {
        if (getBlockInfoForTextureFormat(format).bytesPerBlock > 0) formats.push_back(format);
    }
    return formats;
}

std::vector<Value> sizedFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : sizedFormats()) values.emplace_back(std::string(textureFormatIdentifier(format)));
    return values;
}

std::vector<Value> colorFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kColorTextureFormats) values.emplace_back(std::string(textureFormatIdentifier(format)));
    return values;
}

bool formatAndDimensionPossiblyCompatible(WGPUTextureDimension dimension, WGPUTextureFormat format) {
    if (dimension == WGPUTextureDimension_1D && isCompressedTextureFormat(format)) return false;
    if (dimension == WGPUTextureDimension_3D &&
        (isCompressedTextureFormat(format) || isDepthOrStencilTextureFormat(format))) return false;
    return true;
}

bool formatCopyableWithMethodRecord(const ParamRecord& p) {
    WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
    ImageCopyType method = parseMethod(valueAs<std::string>(*findParam(p, "method")));
    if (isDepthOrStencilTextureFormat(format)) return !depthStencilFormatCopyableAspects(method, format).empty();
    return getBlockInfoForTextureFormat(format).bytesPerBlock > 0;
}

WGPUTextureAspect copyableAspect(WGPUTextureFormat format, const std::string& method) {
    if (!isDepthOrStencilTextureFormat(format)) return WGPUTextureAspect_All;
    auto aspects = depthStencilFormatCopyableAspects(parseMethod(method), format);
    return aspects.empty() ? WGPUTextureAspect_All : aspects.front();
}

WGPUExtent3D virtualMipSize(WGPUTextureDimension dimension, WGPUExtent3D size, uint32_t mipLevel) {
    WGPUExtent3D mip = size;
    mip.width = std::max(1u, size.width >> mipLevel);
    if (dimension != WGPUTextureDimension_1D) mip.height = std::max(1u, size.height >> mipLevel);
    if (dimension == WGPUTextureDimension_3D) mip.depthOrArrayLayers = std::max(1u, size.depthOrArrayLayers >> mipLevel);
    return mip;
}

std::vector<Value> valueToCoordinateExpander(const ParamRecord& p) {
    WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    std::string key = valueAs<std::string>(*findParam(p, "coordinateToTest"));
    uint32_t align = (key == "y" || key == "height") ? info.blockHeight : info.blockWidth;
    return {Value(int64_t(0)), Value(int64_t(1)), Value(int64_t(align - 1)), Value(int64_t(align)), Value(int64_t(align + 1)), Value(int64_t(2 * align))};
}

WGPUTexture createAlignedTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUExtent3D copySize,
    WGPUOrigin3D origin,
    WGPUTextureDimension dimension) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    WGPUExtent3D size = {
        std::max(info.blockWidth, static_cast<uint32_t>(alignTo(origin.x + copySize.width + info.blockWidth, info.blockWidth))),
        std::max(info.blockHeight, static_cast<uint32_t>(alignTo(origin.y + copySize.height + info.blockHeight, info.blockHeight))),
        std::max(1u, origin.z + copySize.depthOrArrayLayers + 1),
    };
    if (dimension == WGPUTextureDimension_1D) {
        size.height = 1;
        size.depthOrArrayLayers = 1;
    }
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = size;
    desc.dimension = dimension;
    desc.format = format;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    return t.createTextureTracked(desc);
}

void testRun(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureAspect aspect,
    WGPUOrigin3D origin,
    uint32_t mipLevel,
    TexelCopyBufferLayout simpleLayout,
    WGPUExtent3D copySize,
    uint64_t dataSize,
    const std::string& method,
    bool success,
    bool submit = false) {
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = simpleLayout.offset;
    layout.bytesPerRow = simpleLayout.bytesPerRow;
    layout.rowsPerImage = simpleLayout.rowsPerImage;

    if (method == "WriteTexture") {
        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture;
        destination.origin = origin;
        destination.mipLevel = mipLevel;
        destination.aspect = aspect;
        std::vector<uint8_t> data(static_cast<size_t>(dataSize), 0);
        t.expectValidationError([&] {
            wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &layout, &copySize);
        }, !success);
        return;
    }

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = dataSize;
    bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = t.createBufferTracked(bufDesc);
    WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);
    WGPUTexelCopyTextureInfo tex = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    tex.texture = texture;
    tex.origin = origin;
    tex.mipLevel = mipLevel;
    tex.aspect = aspect;
    WGPUTexelCopyBufferInfo buf = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    buf.buffer = buffer;
    buf.layout = layout;
    if (method == "CopyB2T") wgpuCommandEncoderCopyBufferToTexture(encoder, &buf, &tex, &copySize);
    else wgpuCommandEncoderCopyTextureToBuffer(encoder, &tex, &buf, &copySize);
    if (submit) {
        WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
        t.expectValidationError([&] {
            WGPUCommandBuffer cmds[1] = {cmd};
            wgpuQueueSubmit(t.queue(), 1, cmds);
        }, !success);
        if (cmd) wgpuCommandBufferRelease(cmd);
    } else {
        t.expectValidationError([&] {
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
            if (cmd) wgpuCommandBufferRelease(cmd);
        }, !success);
    }
    wgpuCommandEncoderRelease(encoder);
}

CTS_TEST(g, "valid")
    .desc("\nTest that the texture must be valid and not destroyed.\n- for all copy methods\n- for all texture states\n- for various dimensions\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combine("textureState", resourceStateValues())
            .combineWithParams({
                ParamRecord{{"dimension", "1d"}, {"size", "4x1x1"}},
                ParamRecord{{"dimension", "2d"}, {"size", "4x4x1"}},
                ParamRecord{{"dimension", "2d"}, {"size", "4x4x3"}},
                ParamRecord{{"dimension", "3d"}, {"size", "4x4x3"}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        ResourceState state = parseResourceState(t.param<std::string>("textureState"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        WGPUExtent3D size = t.param<std::string>("size") == "4x1x1" ? WGPUExtent3D{4, 1, 1}
            : (t.param<std::string>("size") == "4x4x1" ? WGPUExtent3D{4, 4, 1} : WGPUExtent3D{4, 4, 3});
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = dimension;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureWithState(state, desc);
        testRun(t, texture, WGPUTextureAspect_All, {0, 0, 0}, 0, {0, 0, WGPU_COPY_STRIDE_UNDEFINED}, {0, 0, 0},
            1, t.param<std::string>("method"), state == ResourceState::Valid, state != ResourceState::Invalid);
    });

CTS_TEST(g, "texture,device_mismatch")
    .desc("Tests the image copies cannot be called with a texture created from another device")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues()).combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        bool mismatched = t.param<bool>("mismatched");
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {4, 4, 1};
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = wgpuDeviceCreateTexture(mismatched ? t.mismatchedDevice() : t.device(), &desc);
        testRun(t, texture, WGPUTextureAspect_All, {0, 0, 0}, 0, {0, 0, WGPU_COPY_STRIDE_UNDEFINED}, {0, 0, 0},
            1, t.param<std::string>("method"), !mismatched);
        if (texture) wgpuTextureRelease(texture);
    });

CTS_TEST(g, "usage")
    .desc("\nThe texture must have the appropriate COPY_SRC/COPY_DST usage.\n- for various copy methods\n- for various dimensions\n- for various usages\n")
    .params([](ParamsBuilder u) {
        std::vector<Value> usages;
        for (WGPUTextureUsage usage : kTextureUsages) usages.emplace_back(static_cast<int64_t>(usage));
        return u.combine("method", imageCopyTypeValues())
            .combineWithParams({
                ParamRecord{{"dimension", "1d"}, {"size", "4x1x1"}},
                ParamRecord{{"dimension", "2d"}, {"size", "4x4x1"}},
                ParamRecord{{"dimension", "2d"}, {"size", "4x4x3"}},
                ParamRecord{{"dimension", "3d"}, {"size", "4x4x3"}},
            })
            .beginSubcases()
            .combine("usage0", usages)
            .combine("usage1", usages)
            .filter([](const ParamRecord& p) {
                WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(
                    valueAs<int64_t>(*findParam(p, "usage0")) | valueAs<int64_t>(*findParam(p, "usage1")));
                std::string dim = valueAs<std::string>(*findParam(p, "dimension"));
                return !(((usage & WGPUTextureUsage_RenderAttachment) != 0) && (dim == "1d" || dim == "3d"));
            })
            .filter([](const ParamRecord& p) {
                WGPUTextureUsage u0 = static_cast<WGPUTextureUsage>(valueAs<int64_t>(*findParam(p, "usage0")));
                WGPUTextureUsage u1 = static_cast<WGPUTextureUsage>(valueAs<int64_t>(*findParam(p, "usage1")));
                return u0 != WGPUTextureUsage_TransientAttachment && u1 != WGPUTextureUsage_TransientAttachment;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(t.param<int64_t>("usage0") | t.param<int64_t>("usage1"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        WGPUExtent3D size = t.param<std::string>("size") == "4x1x1" ? WGPUExtent3D{4, 1, 1}
            : (t.param<std::string>("size") == "4x4x1" ? WGPUExtent3D{4, 4, 1} : WGPUExtent3D{4, 4, 3});
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = dimension;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = usage;
        WGPUTexture texture = t.createTextureTracked(desc);
        bool success = method == "CopyT2B" ? ((usage & WGPUTextureUsage_CopySrc) != 0) : ((usage & WGPUTextureUsage_CopyDst) != 0);
        testRun(t, texture, WGPUTextureAspect_All, {0, 0, 0}, 0, {0, 0, WGPU_COPY_STRIDE_UNDEFINED}, {0, 0, 0}, 1, method, success);
    });

CTS_TEST(g, "sample_count")
    .desc("\nTest that multisampled textures cannot be copied.\n- for various copy methods\n- multisampled or not\n\nNote: we don't test 1D, 2D array and 3D textures because multisample is not supported them.\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues()).beginSubcases().combine("sampleCount", {1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        uint32_t sampleCount = static_cast<uint32_t>(t.param<int64_t>("sampleCount"));
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {4, 4, 1};
        desc.dimension = WGPUTextureDimension_2D;
        desc.sampleCount = sampleCount;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        WGPUTexture texture = t.createTextureTracked(desc);
        testRun(t, texture, WGPUTextureAspect_All, {0, 0, 0}, 0, {0, 0, WGPU_COPY_STRIDE_UNDEFINED}, {0, 0, 0}, 1,
            t.param<std::string>("method"), sampleCount == 1);
    });

CTS_TEST(g, "mip_level")
    .desc("\nTest that the mipLevel of the copy must be in range of the texture.\n- for various copy methods\n- for various dimensions\n- for several mipLevelCounts\n- for several target/source mipLevels")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combineWithParams({
                ParamRecord{{"dimension", "1d"}, {"size", "32x1x1"}},
                ParamRecord{{"dimension", "2d"}, {"size", "32x32x1"}},
                ParamRecord{{"dimension", "2d"}, {"size", "32x32x3"}},
                ParamRecord{{"dimension", "3d"}, {"size", "32x32x3"}},
            })
            .beginSubcases()
            .combine("mipLevelCount", {1, 3, 5})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    valueAs<int64_t>(*findParam(p, "mipLevelCount")) != 1);
            })
            .combine("mipLevel", {0, 1, 3, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        WGPUExtent3D size = t.param<std::string>("size") == "32x1x1" ? WGPUExtent3D{32, 1, 1}
            : (t.param<std::string>("size") == "32x32x1" ? WGPUExtent3D{32, 32, 1} : WGPUExtent3D{32, 32, 3});
        uint32_t mipLevelCount = static_cast<uint32_t>(t.param<int64_t>("mipLevelCount"));
        uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = dimension;
        desc.mipLevelCount = mipLevelCount;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(desc);
        testRun(t, texture, WGPUTextureAspect_All, {0, 0, 0}, mipLevel, {0, 0, WGPU_COPY_STRIDE_UNDEFINED}, {0, 0, 0}, 1,
            t.param<std::string>("method"), mipLevel < mipLevelCount);
    });

CTS_TEST(g, "format")
    .desc("\nTest the copy must be a full subresource if the texture's format is depth/stencil format.\n- for various copy methods\n- for various dimensions\n- for all sized formats\n- for a couple target/source mipLevels\n- for some modifier (or not) for the full copy size\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combineWithParams({
                ParamRecord{{"depthOrArrayLayers", 1}, {"dimension", "1d"}},
                ParamRecord{{"depthOrArrayLayers", 1}, {"dimension", "2d"}},
                ParamRecord{{"depthOrArrayLayers", 3}, {"dimension", "2d"}},
                ParamRecord{{"depthOrArrayLayers", 32}, {"dimension", "3d"}},
            })
            .combine("format", sizedFormatValues())
            .filter([](const ParamRecord& p) {
                return formatAndDimensionPossiblyCompatible(
                    parseDimension(valueAs<std::string>(*findParam(p, "dimension"))),
                    parseTextureFormat(valueAs<std::string>(*findParam(p, "format"))));
            })
            .filter(formatCopyableWithMethodRecord)
            .beginSubcases()
            .combine("mipLevel", {0, 2})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    valueAs<int64_t>(*findParam(p, "mipLevel")) != 0);
            })
            .combine("copyWidthModifier", {0, -1})
            .combine("copyHeightModifier", {0, -1})
            .expand("copyDepthModifier", [](const ParamRecord& p) {
                return valueAs<std::string>(*findParam(p, "dimension")) == "3d"
                    ? std::vector<Value>{0, -1}
                    : std::vector<Value>{0};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPUExtent3D size = {32 * info.blockWidth, 32 * info.blockHeight, static_cast<uint32_t>(t.param<int64_t>("depthOrArrayLayers"))};
        if (dimension == WGPUTextureDimension_1D) size.height = 1;
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = dimension;
        desc.format = format;
        desc.mipLevelCount = dimension == WGPUTextureDimension_1D ? 1 : 5;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(desc);
        int32_t wMod = static_cast<int32_t>(t.param<int64_t>("copyWidthModifier"));
        int32_t hMod = static_cast<int32_t>(t.param<int64_t>("copyHeightModifier"));
        int32_t dMod = static_cast<int32_t>(t.param<int64_t>("copyDepthModifier"));
        bool success = !(isDepthOrStencilTextureFormat(format) && (wMod != 0 || hMod != 0 || dMod != 0));
        uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
        WGPUExtent3D level = virtualMipSize(dimension, size, mipLevel);
        WGPUExtent3D copySize = {
            static_cast<uint32_t>(static_cast<int64_t>(level.width) + static_cast<int64_t>(wMod * info.blockWidth)),
            static_cast<uint32_t>(static_cast<int64_t>(level.height) + static_cast<int64_t>(hMod * info.blockHeight)),
            static_cast<uint32_t>(static_cast<int64_t>(level.depthOrArrayLayers) + dMod),
        };
        testRun(t, texture, copyableAspect(format, method), {0, 0, 0}, mipLevel, {0, 512, 32}, copySize, 512u * 32u * 32u, method, success);
    });

CTS_TEST(g, "origin_alignment")
    .desc("\nTest that the texture copy origin must be aligned to the format's block size.\n- for various copy methods\n- for all color formats (depth stencil formats require a full copy)\n- for X, Y and Z coordinates\n- for various values for that coordinate depending on the block size\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combine("format", colorFormatValues())
            .filter(formatCopyableWithMethodRecord)
            .combineWithParams({
                ParamRecord{{"depthOrArrayLayers", 1}, {"dimension", "1d"}},
                ParamRecord{{"depthOrArrayLayers", 1}, {"dimension", "2d"}},
                ParamRecord{{"depthOrArrayLayers", 3}, {"dimension", "2d"}},
                ParamRecord{{"depthOrArrayLayers", 3}, {"dimension", "3d"}},
            })
            .filter([](const ParamRecord& p) {
                return formatAndDimensionPossiblyCompatible(
                    parseDimension(valueAs<std::string>(*findParam(p, "dimension"))),
                    parseTextureFormat(valueAs<std::string>(*findParam(p, "format"))));
            })
            .beginSubcases()
            .combine("coordinateToTest", {Value("x"), Value("y"), Value("z")})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    valueAs<std::string>(*findParam(p, "coordinateToTest")) != "x");
            })
            .expand("valueToCoordinate", valueToCoordinateExpander);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPUOrigin3D origin = {0, 0, 0};
        std::string coord = t.param<std::string>("coordinateToTest");
        uint32_t value = static_cast<uint32_t>(t.param<int64_t>("valueToCoordinate"));
        if (coord == "x") origin.x = value;
        if (coord == "y") origin.y = value;
        if (coord == "z") origin.z = value;
        bool success = true;
        if (coord == "x") success = origin.x % info.blockWidth == 0;
        if (coord == "y") success = origin.y % info.blockHeight == 0;
        WGPUExtent3D size = {0, 0, static_cast<uint32_t>(t.param<int64_t>("depthOrArrayLayers"))};
        WGPUTexture texture = createAlignedTexture(t, format, size, origin, dimension);
        testRun(t, texture, WGPUTextureAspect_All, origin, 0, {0, 0, 0}, size, 1, method, success);
    });

CTS_TEST(g, "size_alignment")
    .desc("\nTest that the copy size must be aligned to the texture's format's block size.\n- for various copy methods\n- for all formats (depth-stencil formats require a full copy)\n- for all texture dimensions\n- for the size's parameters to test (width / height / depth)\n- for various values for that copy size parameters, depending on the block size\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combine("format", colorFormatValues())
            .filter(formatCopyableWithMethodRecord)
            .combine("dimension", dimensionValues())
            .filter([](const ParamRecord& p) {
                return formatAndDimensionPossiblyCompatible(
                    parseDimension(valueAs<std::string>(*findParam(p, "dimension"))),
                    parseTextureFormat(valueAs<std::string>(*findParam(p, "format"))));
            })
            .beginSubcases()
            .combine("coordinateToTest", {Value("width"), Value("height"), Value("depthOrArrayLayers")})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    valueAs<std::string>(*findParam(p, "coordinateToTest")) != "width");
            })
            .expand("valueToCoordinate", valueToCoordinateExpander);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForColorTextureFormat(format);
        WGPUExtent3D size = {0, 0, 0};
        std::string coord = t.param<std::string>("coordinateToTest");
        uint32_t value = static_cast<uint32_t>(t.param<int64_t>("valueToCoordinate"));
        if (coord == "width") size.width = value;
        if (coord == "height") size.height = value;
        if (coord == "depthOrArrayLayers") size.depthOrArrayLayers = value;
        bool success = true;
        if (coord == "width") success = size.width % info.blockWidth == 0;
        if (coord == "height") success = size.height % info.blockHeight == 0;
        WGPUTexture texture = createAlignedTexture(t, format, size, {0, 0, 0}, dimension);
        uint32_t rowBlocks = std::max(1u, (size.width + info.blockWidth - 1) / info.blockWidth);
        TexelCopyBufferLayout layout{0, static_cast<uint32_t>(alignTo(rowBlocks * info.bytesPerBlock, 256)), (size.height + info.blockHeight - 1) / info.blockHeight};
        testRun(t, texture, WGPUTextureAspect_All, {0, 0, 0}, 0, layout, size, 1, method, success);
    });

CTS_TEST(g, "copy_rectangle")
    .desc("\nTest that the max corner of the copy rectangle (origin+copySize) must be inside the texture.\n- for various copy methods\n- for all dimensions\n- for the X, Y and Z dimensions\n- for various origin and copy size values (and texture sizes)\n- for various mip levels\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combine("dimension", dimensionValues())
            .beginSubcases()
            .combine("originValue", {7, 8})
            .combine("copySizeValue", {7, 8})
            .combine("textureSizeValue", {14, 15})
            .combine("mipLevel", {0, 2})
            .combine("coordinateToTest", {0, 1, 2})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    (valueAs<int64_t>(*findParam(p, "coordinateToTest")) != 0 ||
                     valueAs<int64_t>(*findParam(p, "mipLevel")) != 0));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        uint32_t originValue = static_cast<uint32_t>(t.param<int64_t>("originValue"));
        uint32_t copySizeValue = static_cast<uint32_t>(t.param<int64_t>("copySizeValue"));
        uint32_t textureSizeValue = static_cast<uint32_t>(t.param<int64_t>("textureSizeValue"));
        uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
        int64_t coord = t.param<int64_t>("coordinateToTest");
        WGPUOrigin3D origin = {0, 0, 0};
        WGPUExtent3D copySize = {0, 0, 0};
        WGPUExtent3D textureSize = {16u << mipLevel, 16u << mipLevel, 16};
        if (dimension == WGPUTextureDimension_1D) {
            textureSize.height = 1;
            textureSize.depthOrArrayLayers = 1;
        }
        bool success = originValue + copySizeValue <= textureSizeValue;
        if (coord == 0) { origin.x = originValue; copySize.width = copySizeValue; textureSize.width = textureSizeValue << mipLevel; }
        if (coord == 1) { origin.y = originValue; copySize.height = copySizeValue; textureSize.height = textureSizeValue << mipLevel; }
        if (coord == 2) {
            origin.z = originValue;
            copySize.depthOrArrayLayers = copySizeValue;
            textureSize.depthOrArrayLayers = dimension == WGPUTextureDimension_3D ? textureSizeValue << mipLevel : textureSizeValue;
        }
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = textureSize;
        desc.dimension = dimension;
        desc.mipLevelCount = dimension == WGPUTextureDimension_1D ? 1 : 3;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(desc);
        TexelCopyBufferLayout layout{0, static_cast<uint32_t>(alignTo(copySize.width, 256)), copySize.height};
        testRun(t, texture, WGPUTextureAspect_All, origin, mipLevel, layout, copySize, 1, method, success);
    });

} // namespace
