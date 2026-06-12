// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/copyTextureToTexture.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <algorithm>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,copyTextureToTexture",
    "copyTextureToTexture tests.");

enum class CopyExpectation {
    Success,
    FinishError,
    SubmitError,
};

struct BoxOffset {
    int32_t x;
    int32_t y;
    int32_t z;
    int32_t width;
    int32_t height;
    int32_t depthOrArrayLayers;
};

std::vector<Value> textureDimensions() {
    return {std::string("1d"), std::string("2d"), std::string("3d")};
}

WGPUTextureDimension parseDimension(const std::string& dimension) {
    return parseTextureDimension(dimension);
}

std::vector<Value> textureUsages() {
    std::vector<Value> values;
    for (WGPUTextureUsage usage : kTextureUsages) {
        if (usage != WGPUTextureUsage_TransientAttachment) {
            values.emplace_back(static_cast<int64_t>(usage));
        }
    }
    return values;
}

template <typename Formats>
std::vector<Value> formatValues(const Formats& formats) {
    return formatIdentifierValues(std::span<const WGPUTextureFormat>(formats.data(), formats.size()));
}

std::vector<Value> allFormatValues() {
    return formatValues(kAllTextureFormats);
}

std::vector<Value> depthStencilFormatValues() {
    return formatValues(kDepthStencilFormats);
}

std::vector<Value> compressedFormatValues() {
    return formatValues(kCompressedTextureFormats);
}

BoxOffset boxOffsetByIndex(int index) {
    static constexpr BoxOffset offsets[] = {
        {0, 0, 0, 0, 0, -2},
        {1, 0, 0, 0, 0, -2},
        {1, 0, 0, -1, 0, -2},
        {0, 1, 0, 0, 0, -2},
        {0, 1, 0, 0, -1, -2},
        {0, 0, 1, 0, 1, -2},
        {0, 0, 2, 0, 1, 0},
        {0, 0, 0, 1, 0, -2},
        {0, 0, 0, 0, 1, -2},
        {0, 0, 0, 0, 0, 1},
        {0, 0, 0, 0, 0, 0},
        {0, 0, 1, 0, 0, -1},
        {0, 0, 2, 0, 0, -1},
    };
    return offsets[index];
}

BoxOffset compressedBoxOffsetByIndex(int index) {
    static constexpr BoxOffset offsets[] = {
        {0, 0, 0, 0, 0, -2},
        {1, 0, 0, 0, 0, -2},
        {4, 0, 0, 0, 0, -2},
        {0, 0, 0, -1, 0, -2},
        {0, 0, 0, -4, 0, -2},
        {0, 1, 0, 0, 0, -2},
        {0, 4, 0, 0, 0, -2},
        {0, 0, 0, 0, -1, -2},
        {0, 0, 0, 0, -4, -2},
        {0, 0, 0, 0, 0, 0},
        {0, 0, 1, 0, 0, -1},
    };
    return offsets[index];
}

WGPUExtent3D physicalSubresourceSize(
    WGPUTextureDimension dimension,
    WGPUExtent3D textureSize,
    WGPUTextureFormat format,
    uint32_t mipLevel) {
    const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
    const uint32_t virtualWidth = std::max(textureSize.width >> mipLevel, 1u);
    const uint32_t virtualHeight = std::max(textureSize.height >> mipLevel, 1u);
    const uint32_t physicalWidth = ((virtualWidth + block.blockWidth - 1) / block.blockWidth) * block.blockWidth;
    const uint32_t physicalHeight = ((virtualHeight + block.blockHeight - 1) / block.blockHeight) * block.blockHeight;
    if (dimension == WGPUTextureDimension_1D) {
        return WGPUExtent3D{physicalWidth, 1, 1};
    }
    if (dimension == WGPUTextureDimension_3D) {
        return WGPUExtent3D{physicalWidth, physicalHeight, std::max(textureSize.depthOrArrayLayers >> mipLevel, 1u)};
    }
    return WGPUExtent3D{physicalWidth, physicalHeight, textureSize.depthOrArrayLayers};
}

WGPUTexture makeTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUExtent3D size,
    WGPUTextureFormat format,
    WGPUTextureUsage usage,
    WGPUTextureDimension dimension = WGPUTextureDimension_2D,
    uint32_t mipLevelCount = 1,
    uint32_t sampleCount = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = size;
    desc.mipLevelCount = mipLevelCount;
    desc.sampleCount = sampleCount;
    desc.dimension = dimension;
    desc.format = format;
    desc.usage = usage;
    return t.createTextureTracked(desc);
}

void testCopyTextureToTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    const WGPUTexelCopyTextureInfo& source,
    const WGPUTexelCopyTextureInfo& destination,
    WGPUExtent3D copySize,
    CopyExpectation expectation) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &copySize);
    if (expectation == CopyExpectation::FinishError) {
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, true);
        return;
    }
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    }, expectation == CopyExpectation::SubmitError);
}

WGPUTexelCopyTextureInfo copyInfo(
    WGPUTexture texture,
    uint32_t mipLevel = 0,
    WGPUOrigin3D origin = WGPUOrigin3D{0, 0, 0},
    WGPUTextureAspect aspect = WGPUTextureAspect_All) {
    WGPUTexelCopyTextureInfo info = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    info.texture = texture;
    info.mipLevel = mipLevel;
    info.origin = origin;
    info.aspect = aspect;
    return info;
}

bool validAspectForFormat(WGPUTextureFormat format, WGPUTextureAspect aspect) {
    switch (format) {
        case WGPUTextureFormat_RGBA8Unorm:
            return aspect == WGPUTextureAspect_All;
        case WGPUTextureFormat_Depth24Plus:
        case WGPUTextureFormat_Depth32Float:
        case WGPUTextureFormat_Depth16Unorm:
            return aspect == WGPUTextureAspect_All || aspect == WGPUTextureAspect_DepthOnly;
        case WGPUTextureFormat_Stencil8:
            return aspect == WGPUTextureAspect_All || aspect == WGPUTextureAspect_StencilOnly;
        case WGPUTextureFormat_Depth24PlusStencil8:
        case WGPUTextureFormat_Depth32FloatStencil8:
            return aspect == WGPUTextureAspect_All;
        default:
            return aspect == WGPUTextureAspect_All;
    }
}

WGPUTextureAspect parseAspectString(const std::string& aspect) {
    return parseTextureAspect(aspect);
}

CTS_TEST(testGroup, "copy_with_invalid_or_destroyed_texture")
    .desc("Test copyTextureToTexture is an error when one of the textures is invalid or destroyed.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("srcState", resourceStateValues()).combine("dstState", resourceStateValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        ResourceState srcState = parseResourceState(t.param<std::string>("srcState"));
        ResourceState dstState = parseResourceState(t.param<std::string>("dstState"));
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = WGPUExtent3D{4, 4, 1};
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture src = t.createTextureWithState(srcState, desc);
        WGPUTexture dst = t.createTextureWithState(dstState, desc);
        CopyExpectation expectation = CopyExpectation::Success;
        if (srcState == ResourceState::Invalid || dstState == ResourceState::Invalid) {
            expectation = CopyExpectation::FinishError;
        } else if (srcState == ResourceState::Destroyed || dstState == ResourceState::Destroyed) {
            expectation = CopyExpectation::SubmitError;
        }
        testCopyTextureToTexture(t, copyInfo(src), copyInfo(dst), WGPUExtent3D{1, 1, 1}, expectation);
    });

CTS_TEST(testGroup, "texture,device_mismatch")
    .desc("Tests copyTextureToTexture cannot be called with src texture or dst texture created from another device.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"srcMismatched", false}, {"dstMismatched", false}},
            ParamRecord{{"srcMismatched", true}, {"dstMismatched", false}},
            ParamRecord{{"srcMismatched", false}, {"dstMismatched", true}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool srcMismatched = t.param<bool>("srcMismatched");
        const bool dstMismatched = t.param<bool>("dstMismatched");
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = WGPUExtent3D{4, 4, 1};
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture src = srcMismatched ? wgpuDeviceCreateTexture(t.mismatchedDevice(), &desc) : t.createTextureTracked(desc);
        WGPUTexture dst = dstMismatched ? wgpuDeviceCreateTexture(t.mismatchedDevice(), &desc) : t.createTextureTracked(desc);
        testCopyTextureToTexture(
            t,
            copyInfo(src),
            copyInfo(dst),
            WGPUExtent3D{1, 1, 1},
            (srcMismatched || dstMismatched) ? CopyExpectation::FinishError : CopyExpectation::Success);
        if (srcMismatched && src != nullptr) {
            wgpuTextureRelease(src);
        }
        if (dstMismatched && dst != nullptr) {
            wgpuTextureRelease(dst);
        }
    });

CTS_TEST(testGroup, "mipmap_level")
    .desc("Test copyTextureToTexture must specify mipLevels that are in range.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", textureDimensions())
                .beginSubcases()
                .combineWithParams({
                    ParamRecord{{"srcLevelCount", 1}, {"dstLevelCount", 1}, {"srcCopyLevel", 0}, {"dstCopyLevel", 0}},
                    ParamRecord{{"srcLevelCount", 1}, {"dstLevelCount", 1}, {"srcCopyLevel", 1}, {"dstCopyLevel", 0}},
                    ParamRecord{{"srcLevelCount", 1}, {"dstLevelCount", 1}, {"srcCopyLevel", 0}, {"dstCopyLevel", 1}},
                    ParamRecord{{"srcLevelCount", 3}, {"dstLevelCount", 3}, {"srcCopyLevel", 0}, {"dstCopyLevel", 0}},
                    ParamRecord{{"srcLevelCount", 3}, {"dstLevelCount", 3}, {"srcCopyLevel", 2}, {"dstCopyLevel", 0}},
                    ParamRecord{{"srcLevelCount", 3}, {"dstLevelCount", 3}, {"srcCopyLevel", 3}, {"dstCopyLevel", 0}},
                    ParamRecord{{"srcLevelCount", 3}, {"dstLevelCount", 3}, {"srcCopyLevel", 0}, {"dstCopyLevel", 2}},
                    ParamRecord{{"srcLevelCount", 3}, {"dstLevelCount", 3}, {"srcCopyLevel", 0}, {"dstCopyLevel", 3}},
                })
                .filter([](const ParamRecord& p) {
                    const std::string dim = valueAs<std::string>(*findParam(p, "dimension"));
                    if (dim != "1d") {
                        return true;
                    }
                    return valueAs<int>(*findParam(p, "srcLevelCount")) == 1 &&
                           valueAs<int>(*findParam(p, "dstLevelCount")) == 1;
                });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        const uint32_t srcLevelCount = static_cast<uint32_t>(t.param<int>("srcLevelCount"));
        const uint32_t dstLevelCount = static_cast<uint32_t>(t.param<int>("dstLevelCount"));
        const uint32_t srcCopyLevel = static_cast<uint32_t>(t.param<int>("srcCopyLevel"));
        const uint32_t dstCopyLevel = static_cast<uint32_t>(t.param<int>("dstCopyLevel"));
        WGPUExtent3D size = dimension == WGPUTextureDimension_1D ? WGPUExtent3D{32, 1, 1} : WGPUExtent3D{32, 4, 1};
        WGPUTexture src = makeTexture(t, size, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopySrc, dimension, srcLevelCount);
        WGPUTexture dst = makeTexture(t, size, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopyDst, dimension, dstLevelCount);
        const bool success = srcCopyLevel < srcLevelCount && dstCopyLevel < dstLevelCount;
        testCopyTextureToTexture(
            t,
            copyInfo(src, srcCopyLevel),
            copyInfo(dst, dstCopyLevel),
            WGPUExtent3D{1, 1, 1},
            success ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "texture_usage")
    .desc("Test that copyTextureToTexture source/destination need COPY_SRC/COPY_DST usages.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("srcUsage", textureUsages()).combine("dstUsage", textureUsages());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureUsage srcUsage = t.param<WGPUTextureUsage>("srcUsage");
        const WGPUTextureUsage dstUsage = t.param<WGPUTextureUsage>("dstUsage");
        WGPUTexture src = makeTexture(t, WGPUExtent3D{4, 4, 1}, WGPUTextureFormat_RGBA8Unorm, srcUsage);
        WGPUTexture dst = makeTexture(t, WGPUExtent3D{4, 4, 1}, WGPUTextureFormat_RGBA8Unorm, dstUsage);
        const bool success = srcUsage == WGPUTextureUsage_CopySrc && dstUsage == WGPUTextureUsage_CopyDst;
        testCopyTextureToTexture(t, copyInfo(src), copyInfo(dst), WGPUExtent3D{1, 1, 1}, success ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "sample_count")
    .desc("Test that textures in copyTextureToTexture must have the same sample count.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("srcSampleCount", {1, 4}).combine("dstSampleCount", {1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t srcSampleCount = static_cast<uint32_t>(t.param<int>("srcSampleCount"));
        const uint32_t dstSampleCount = static_cast<uint32_t>(t.param<int>("dstSampleCount"));
        WGPUTexture src = makeTexture(t, WGPUExtent3D{4, 4, 1}, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment, WGPUTextureDimension_2D, 1, srcSampleCount);
        WGPUTexture dst = makeTexture(t, WGPUExtent3D{4, 4, 1}, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment, WGPUTextureDimension_2D, 1, dstSampleCount);
        const bool success = srcSampleCount == dstSampleCount;
        testCopyTextureToTexture(t, copyInfo(src), copyInfo(dst), WGPUExtent3D{4, 4, 1}, success ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "multisampled_copy_restrictions")
    .desc("Test that copyTextureToTexture of multisampled texture must copy a whole subresource to a whole subresource.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
                .combine("srcCopyOrigin", {0, 1, 2, 3})
                .combine("dstCopyOrigin", {0, 1, 2, 3})
                .combine("copyWidth", {16, 32})
                .combine("copyHeight", {8, 16});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t srcOriginIndex = static_cast<uint32_t>(t.param<int>("srcCopyOrigin"));
        const uint32_t dstOriginIndex = static_cast<uint32_t>(t.param<int>("dstCopyOrigin"));
        const WGPUOrigin3D srcOrigin{srcOriginIndex & 1u, (srcOriginIndex >> 1u) & 1u, 0};
        const WGPUOrigin3D dstOrigin{dstOriginIndex & 1u, (dstOriginIndex >> 1u) & 1u, 0};
        const uint32_t copyWidth = static_cast<uint32_t>(t.param<int>("copyWidth"));
        const uint32_t copyHeight = static_cast<uint32_t>(t.param<int>("copyHeight"));
        constexpr uint32_t kWidth = 32;
        constexpr uint32_t kHeight = 16;
        WGPUTexture src = makeTexture(t, WGPUExtent3D{kWidth, kHeight, 1}, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment, WGPUTextureDimension_2D, 1, 4);
        WGPUTexture dst = makeTexture(t, WGPUExtent3D{kWidth, kHeight, 1}, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment, WGPUTextureDimension_2D, 1, 4);
        const bool success = srcOrigin.x == 0 && srcOrigin.y == 0 && dstOrigin.x == 0 && dstOrigin.y == 0 && copyWidth == kWidth && copyHeight == kHeight;
        testCopyTextureToTexture(t, copyInfo(src, 0, srcOrigin), copyInfo(dst, 0, dstOrigin), WGPUExtent3D{copyWidth, copyHeight, 1}, success ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "texture_format_compatibility")
    .desc("Test the formats of textures in copyTextureToTexture must be copy-compatible.")
    .params([](ParamsBuilder u) {
        return u.combine("srcFormat", allFormatValues())
                .filter([](const ParamRecord& p) {
                    return !isDepthOrStencilTextureFormat(parseTextureFormat(valueAs<std::string>(*findParam(p, "srcFormat"))));
                })
                .combine("dstFormat", allFormatValues())
                .filter([](const ParamRecord& p) {
                    const WGPUTextureFormat src = parseTextureFormat(valueAs<std::string>(*findParam(p, "srcFormat")));
                    const WGPUTextureFormat dst = parseTextureFormat(valueAs<std::string>(*findParam(p, "dstFormat")));
                    const TextureBlockInfo srcInfo = getBlockInfoForTextureFormat(src);
                    const TextureBlockInfo dstInfo = getBlockInfoForTextureFormat(dst);
                    return !isDepthOrStencilTextureFormat(dst) &&
                           srcInfo.blockWidth == dstInfo.blockWidth &&
                           srcInfo.blockHeight == dstInfo.blockHeight;
                });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat srcFormat = parseTextureFormat(t.param<std::string>("srcFormat"));
        const WGPUTextureFormat dstFormat = parseTextureFormat(t.param<std::string>("dstFormat"));
        t.skipIfTextureFormatNotSupported(srcFormat);
        t.skipIfTextureFormatNotSupported(dstFormat);
        const TextureBlockInfo srcInfo = getBlockInfoForTextureFormat(srcFormat);
        const TextureBlockInfo dstInfo = getBlockInfoForTextureFormat(dstFormat);
        const uint32_t width = std::max(srcInfo.blockWidth, dstInfo.blockWidth);
        const uint32_t height = std::max(srcInfo.blockHeight, dstInfo.blockHeight);
        WGPUTexture src = makeTexture(t, WGPUExtent3D{width, height, 1}, srcFormat, WGPUTextureUsage_CopySrc);
        WGPUTexture dst = makeTexture(t, WGPUExtent3D{width, height, 1}, dstFormat, WGPUTextureUsage_CopyDst);
        const bool success = baseFormat(srcFormat) == baseFormat(dstFormat);
        testCopyTextureToTexture(t, copyInfo(src), copyInfo(dst), WGPUExtent3D{width, height, 1}, success ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "depth_stencil_copy_restrictions")
    .desc("Test that depth textures subresources must be entirely copied in copyTextureToTexture.")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormatValues())
                .beginSubcases()
                .combine("copyBoxOffsets", {0, 1, 2, 3, 4})
                .combine("srcTextureSize", {0, 1, 2})
                .combine("dstTextureSize", {0, 1, 2})
                .combine("srcCopyLevel", {1, 2})
                .combine("dstCopyLevel", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        static constexpr BoxOffset offsets[] = {{0, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0}, {0, 1, 0, 0, 0, 0}, {0, 0, 0, -1, 0, 0}, {0, 0, 0, 0, -1, 0}};
        static constexpr WGPUExtent3D sizes[] = {{64, 64, 1}, {64, 32, 1}, {32, 32, 1}};
        const BoxOffset offset = offsets[t.param<int>("copyBoxOffsets")];
        const WGPUExtent3D srcSize = sizes[t.param<int>("srcTextureSize")];
        const WGPUExtent3D dstSize = sizes[t.param<int>("dstTextureSize")];
        const uint32_t srcLevel = static_cast<uint32_t>(t.param<int>("srcCopyLevel"));
        const uint32_t dstLevel = static_cast<uint32_t>(t.param<int>("dstCopyLevel"));
        WGPUTexture src = makeTexture(t, srcSize, format, WGPUTextureUsage_CopySrc, WGPUTextureDimension_2D, 3);
        WGPUTexture dst = makeTexture(t, dstSize, format, WGPUTextureUsage_CopyDst, WGPUTextureDimension_2D, 3);
        const WGPUExtent3D srcAtLevel = physicalSubresourceSize(WGPUTextureDimension_2D, srcSize, format, srcLevel);
        const WGPUExtent3D dstAtLevel = physicalSubresourceSize(WGPUTextureDimension_2D, dstSize, format, dstLevel);
        const WGPUOrigin3D origin{static_cast<uint32_t>(offset.x), static_cast<uint32_t>(offset.y), 0};
        const uint32_t copyWidth = static_cast<uint32_t>(std::max<int32_t>(0, static_cast<int32_t>(std::min(srcAtLevel.width, dstAtLevel.width)) + offset.width - offset.x));
        const uint32_t copyHeight = static_cast<uint32_t>(std::max<int32_t>(0, static_cast<int32_t>(std::min(srcAtLevel.height, dstAtLevel.height)) + offset.height - offset.y));
        const bool success = origin.x == 0 && origin.y == 0 && copyWidth == srcAtLevel.width && copyHeight == srcAtLevel.height && copyWidth == dstAtLevel.width && copyHeight == dstAtLevel.height;
        testCopyTextureToTexture(t, copyInfo(src, srcLevel), copyInfo(dst, dstLevel, origin), WGPUExtent3D{copyWidth, copyHeight, 1}, success ? CopyExpectation::Success : CopyExpectation::FinishError);
        testCopyTextureToTexture(t, copyInfo(src, srcLevel, origin), copyInfo(dst, dstLevel), WGPUExtent3D{copyWidth, copyHeight, 1}, success ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "copy_ranges")
    .desc("Test that copyTextureToTexture copy boxes must be in range of the subresource.")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", textureDimensions())
                .combine("copyBoxOffsets", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12})
                .filter([](const ParamRecord& p) {
                    const std::string dimension = valueAs<std::string>(*findParam(p, "dimension"));
                    const BoxOffset offset = boxOffsetByIndex(valueAs<int>(*findParam(p, "copyBoxOffsets")));
                    return dimension != "1d" || (offset.y == 0 && offset.z == 0 && offset.height == 0 && offset.depthOrArrayLayers == 0);
                })
                .combine("srcCopyLevel", {0, 1, 3})
                .combine("dstCopyLevel", {0, 1, 3})
                .filter([](const ParamRecord& p) {
                    const std::string dimension = valueAs<std::string>(*findParam(p, "dimension"));
                    return dimension != "1d" || (valueAs<int>(*findParam(p, "srcCopyLevel")) == 0 && valueAs<int>(*findParam(p, "dstCopyLevel")) == 0);
                });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        const BoxOffset offset = boxOffsetByIndex(t.param<int>("copyBoxOffsets"));
        const uint32_t srcLevel = static_cast<uint32_t>(t.param<int>("srcCopyLevel"));
        const uint32_t dstLevel = static_cast<uint32_t>(t.param<int>("dstCopyLevel"));
        WGPUExtent3D textureSize{16, 8, 3};
        uint32_t mipLevelCount = 4;
        if (dimension == WGPUTextureDimension_1D) {
            textureSize = WGPUExtent3D{16, 1, 1};
            mipLevelCount = 1;
        }
        WGPUTexture src = makeTexture(t, textureSize, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopySrc, dimension, mipLevelCount);
        WGPUTexture dst = makeTexture(t, textureSize, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopyDst, dimension, mipLevelCount);
        const WGPUExtent3D srcAtLevel = physicalSubresourceSize(dimension, textureSize, WGPUTextureFormat_RGBA8Unorm, srcLevel);
        const WGPUExtent3D dstAtLevel = physicalSubresourceSize(dimension, textureSize, WGPUTextureFormat_RGBA8Unorm, dstLevel);
        const WGPUOrigin3D origin{static_cast<uint32_t>(offset.x), static_cast<uint32_t>(offset.y), static_cast<uint32_t>(offset.z)};
        const uint32_t copyWidth = static_cast<uint32_t>(std::max<int32_t>(std::min(srcAtLevel.width, dstAtLevel.width) + offset.width - offset.x, 0));
        const uint32_t copyHeight = static_cast<uint32_t>(std::max<int32_t>(std::min(srcAtLevel.height, dstAtLevel.height) + offset.height - offset.y, 0));
        const uint32_t copyDepth = static_cast<uint32_t>(std::max<int32_t>(static_cast<int32_t>(textureSize.depthOrArrayLayers) + offset.depthOrArrayLayers - offset.z, 0));
        bool successA = copyWidth <= srcAtLevel.width && copyHeight <= srcAtLevel.height && origin.x + copyWidth <= dstAtLevel.width && origin.y + copyHeight <= dstAtLevel.height;
        bool successB = origin.x + copyWidth <= srcAtLevel.width && origin.y + copyHeight <= srcAtLevel.height && copyWidth <= dstAtLevel.width && copyHeight <= dstAtLevel.height;
        if (dimension == WGPUTextureDimension_3D) {
            successA = successA && copyDepth <= srcAtLevel.depthOrArrayLayers && origin.z + copyDepth <= dstAtLevel.depthOrArrayLayers;
            successB = successB && copyDepth <= dstAtLevel.depthOrArrayLayers && origin.z + copyDepth <= srcAtLevel.depthOrArrayLayers;
        } else {
            successA = successA && copyDepth <= textureSize.depthOrArrayLayers && origin.z + copyDepth <= textureSize.depthOrArrayLayers;
            successB = successB && copyDepth <= textureSize.depthOrArrayLayers && origin.z + copyDepth <= textureSize.depthOrArrayLayers;
        }
        testCopyTextureToTexture(t, copyInfo(src, srcLevel), copyInfo(dst, dstLevel, origin), WGPUExtent3D{copyWidth, copyHeight, copyDepth}, successA ? CopyExpectation::Success : CopyExpectation::FinishError);
        testCopyTextureToTexture(t, copyInfo(src, srcLevel, origin), copyInfo(dst, dstLevel), WGPUExtent3D{copyWidth, copyHeight, copyDepth}, successB ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "copy_within_same_texture")
    .desc("Test that it is an error to use copyTextureToTexture from one subresource to itself.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
                .combine("srcCopyOriginZ", {0, 2, 4})
                .combine("dstCopyOriginZ", {0, 2, 4})
                .combine("copyExtentDepth", {1, 2, 3});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t srcZ = static_cast<uint32_t>(t.param<int>("srcCopyOriginZ"));
        const uint32_t dstZ = static_cast<uint32_t>(t.param<int>("dstCopyOriginZ"));
        const uint32_t depth = static_cast<uint32_t>(t.param<int>("copyExtentDepth"));
        WGPUTexture texture = makeTexture(t, WGPUExtent3D{16, 16, 7}, WGPUTextureFormat_RGBA8Unorm, WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
        const bool success = std::min(srcZ, dstZ) + depth <= std::max(srcZ, dstZ);
        testCopyTextureToTexture(t, copyInfo(texture, 0, WGPUOrigin3D{0, 0, srcZ}), copyInfo(texture, 0, WGPUOrigin3D{0, 0, dstZ}), WGPUExtent3D{16, 16, depth}, success ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "copy_aspects")
    .desc("Test the validations on the member 'aspect' of GPUTexelCopyTextureInfo in CopyTextureToTexture().")
    .params([](ParamsBuilder u) {
        std::vector<Value> formats = {std::string("rgba8unorm")};
        for (Value value : depthStencilFormatValues()) {
            formats.push_back(value);
        }
        return u.combine("format", formats)
                .beginSubcases()
                .combine("sourceAspect", {std::string("all"), std::string("depth-only"), std::string("stencil-only")})
                .combine("destinationAspect", {std::string("all"), std::string("depth-only"), std::string("stencil-only")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const WGPUTextureAspect sourceAspect = parseAspectString(t.param<std::string>("sourceAspect"));
        const WGPUTextureAspect destinationAspect = parseAspectString(t.param<std::string>("destinationAspect"));
        t.skipIfTextureFormatNotSupported(format);
        WGPUTexture src = makeTexture(t, WGPUExtent3D{16, 8, 1}, format, WGPUTextureUsage_CopySrc);
        WGPUTexture dst = makeTexture(t, WGPUExtent3D{16, 8, 1}, format, WGPUTextureUsage_CopyDst);
        const bool success = validAspectForFormat(format, sourceAspect) && validAspectForFormat(format, destinationAspect);
        testCopyTextureToTexture(t, copyInfo(src, 0, WGPUOrigin3D{0, 0, 0}, sourceAspect), copyInfo(dst, 0, WGPUOrigin3D{0, 0, 0}, destinationAspect), WGPUExtent3D{16, 8, 1}, success ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

CTS_TEST(testGroup, "copy_ranges_with_compressed_texture_formats")
    .desc("Test that copyTextureToTexture copy boxes must be in range of the subresource and aligned to the block size.")
    .params([](ParamsBuilder u) {
        return u.combine("format", compressedFormatValues())
                .combine("dimension", textureDimensions())
                .filter([](const ParamRecord& p) {
                    const WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                    const WGPUTextureDimension dimension = parseTextureDimension(valueAs<std::string>(*findParam(p, "dimension")));
                    return textureFormatAndDimensionPossiblyCompatible(dimension, format);
                })
                .beginSubcases()
                .combine("copyBoxOffsets", {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10})
                .combine("srcCopyLevel", {0, 1, 2})
                .combine("dstCopyLevel", {0, 1, 2});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
        const BoxOffset offset = compressedBoxOffsetByIndex(t.param<int>("copyBoxOffsets"));
        const uint32_t srcLevel = static_cast<uint32_t>(t.param<int>("srcCopyLevel"));
        const uint32_t dstLevel = static_cast<uint32_t>(t.param<int>("dstCopyLevel"));
        WGPUExtent3D textureSize{15 * block.blockWidth, 12 * block.blockHeight, 3};
        if (dimension == WGPUTextureDimension_1D) {
            textureSize.height = 1;
            textureSize.depthOrArrayLayers = 1;
        }
        WGPUTexture src = makeTexture(t, textureSize, format, WGPUTextureUsage_CopySrc, dimension, 4);
        WGPUTexture dst = makeTexture(t, textureSize, format, WGPUTextureUsage_CopyDst, dimension, 4);
        const WGPUExtent3D srcAtLevel = physicalSubresourceSize(dimension, textureSize, format, srcLevel);
        const WGPUExtent3D dstAtLevel = physicalSubresourceSize(dimension, textureSize, format, dstLevel);
        const WGPUOrigin3D origin{static_cast<uint32_t>(offset.x), static_cast<uint32_t>(offset.y), static_cast<uint32_t>(offset.z)};
        const uint32_t copyWidth = static_cast<uint32_t>(std::max<int32_t>(std::min(srcAtLevel.width, dstAtLevel.width) + offset.width - offset.x, 0));
        const uint32_t copyHeight = static_cast<uint32_t>(std::max<int32_t>(std::min(srcAtLevel.height, dstAtLevel.height) + offset.height - offset.y, 0));
        const uint32_t copyDepth = static_cast<uint32_t>(std::max<int32_t>(std::min(srcAtLevel.depthOrArrayLayers, dstAtLevel.depthOrArrayLayers) + offset.depthOrArrayLayers - offset.z, 0));
        const bool aligned = origin.x % block.blockWidth == 0 && origin.y % block.blockHeight == 0 && copyWidth % block.blockWidth == 0 && copyHeight % block.blockHeight == 0;
        const bool successA = aligned && copyWidth <= srcAtLevel.width && copyHeight <= srcAtLevel.height && origin.x + copyWidth <= dstAtLevel.width && origin.y + copyHeight <= dstAtLevel.height && origin.z + copyDepth <= textureSize.depthOrArrayLayers;
        const bool successB = aligned && origin.x + copyWidth <= srcAtLevel.width && origin.y + copyHeight <= srcAtLevel.height && copyWidth <= dstAtLevel.width && copyHeight <= dstAtLevel.height && origin.z + copyDepth <= textureSize.depthOrArrayLayers;
        testCopyTextureToTexture(t, copyInfo(src, srcLevel), copyInfo(dst, dstLevel, origin), WGPUExtent3D{copyWidth, copyHeight, copyDepth}, successA ? CopyExpectation::Success : CopyExpectation::FinishError);
        testCopyTextureToTexture(t, copyInfo(src, srcLevel, origin), copyInfo(dst, dstLevel), WGPUExtent3D{copyWidth, copyHeight, copyDepth}, successB ? CopyExpectation::Success : CopyExpectation::FinishError);
    });

} // namespace
