// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/image_copy/layout_related.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85.
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,image_copy,layout_related",
    "Validation tests for the linear data layout of linear data <-> texture copies\n\n"
    "TODO check if the tests need to be updated to support aspects of depth-stencil textures");

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

TextureBlockInfo blockInfoForCopyAspect(WGPUTextureFormat format, WGPUTextureAspect aspect) {
    TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    if (isDepthOrStencilTextureFormat(format) && aspect != WGPUTextureAspect_All) {
        info.bytesPerBlock = depthStencilFormatAspectSize(format, aspect);
    }
    return info;
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

std::vector<WGPUTextureFormat> sizedFormats() {
    std::vector<WGPUTextureFormat> formats;
    for (WGPUTextureFormat format : kAllTextureFormats) {
        if (getBlockInfoForTextureFormat(format).bytesPerBlock > 0) {
            formats.push_back(format);
        }
    }
    return formats;
}

std::vector<Value> sizedFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : sizedFormats()) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
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

bool formatAndDimensionPossiblyCompatible(WGPUTextureDimension dimension, WGPUTextureFormat format) {
    if (dimension == WGPUTextureDimension_1D && isCompressedTextureFormat(format)) return false;
    if (dimension == WGPUTextureDimension_3D &&
        (isCompressedTextureFormat(format) || isDepthOrStencilTextureFormat(format))) return false;
    return true;
}

bool formatCopyableWithMethodRecord(const ParamRecord& p) {
    WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
    ImageCopyType method = parseMethod(valueAs<std::string>(*findParam(p, "method")));
    if (isDepthOrStencilTextureFormat(format)) {
        return !depthStencilFormatCopyableAspects(method, format).empty();
    }
    return getBlockInfoForTextureFormat(format).bytesPerBlock > 0;
}

std::vector<Value> dimensionValues() {
    std::vector<Value> values;
    for (WGPUTextureDimension dimension : kTextureDimensions) {
        values.emplace_back(dimensionName(dimension));
    }
    return values;
}

std::vector<Value> offsetExpander(const ParamRecord& p) {
    WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
    uint32_t align = isDepthOrStencilTextureFormat(format) ? 4 : getBlockInfoForTextureFormat(format).bytesPerBlock;
    return {Value(int64_t(0)), Value(int64_t(1)), Value(int64_t(align - 1)), Value(int64_t(align)), Value(int64_t(align + 1)), Value(int64_t(2 * align))};
}

std::vector<Value> rowsPerImageExpander(const ParamRecord& p) {
    WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
    uint32_t blockHeight = getBlockInfoForTextureFormat(format).blockHeight;
    return {Value(int64_t(0)), Value(int64_t(1)), Value(int64_t(blockHeight - 1)), Value(int64_t(blockHeight)), Value(int64_t(blockHeight + 1)), Value(int64_t(2 * blockHeight))};
}

uint64_t requiredBytesInCopy(
    uint32_t bytesPerRow,
    uint32_t rowsPerImage,
    WGPUExtent3D copySize,
    WGPUTextureFormat format,
    WGPUTextureAspect aspect) {
    if (copySize.depthOrArrayLayers == 0) {
        return 0;
    }
    const TextureBlockInfo info = blockInfoForCopyAspect(format, aspect);
    const uint32_t widthBlocks = copySize.width / info.blockWidth;
    const uint32_t heightBlocks = copySize.height / info.blockHeight;
    const uint32_t bytesInLastRow = widthBlocks * info.bytesPerBlock;

    uint64_t required = 0;
    if (copySize.depthOrArrayLayers > 1) {
        required += static_cast<uint64_t>(bytesPerRow) * rowsPerImage * (copySize.depthOrArrayLayers - 1);
    }
    if (heightBlocks > 0) {
        required += static_cast<uint64_t>(bytesPerRow) * (heightBlocks - 1);
        required += bytesInLastRow;
    }
    return required;
}

uint64_t dataSizeForCopyOrOverestimate(
    TexelCopyBufferLayout layout,
    WGPUTextureFormat format,
    WGPUTextureAspect aspect,
    WGPUExtent3D copySize,
    const std::string& method,
    bool* copyValid) {
    const TextureBlockInfo info = blockInfoForCopyAspect(format, aspect);
    bool valid = true;
    if (copySize.width % info.blockWidth != 0 || copySize.height % info.blockHeight != 0) {
        valid = false;
    }
    const uint32_t widthBlocks = (copySize.width + info.blockWidth - 1) / info.blockWidth;
    const uint32_t heightBlocks = (copySize.height + info.blockHeight - 1) / info.blockHeight;
    const uint32_t depth = copySize.depthOrArrayLayers;
    const uint32_t bytesInLastRow = widthBlocks * info.bytesPerBlock;

    if (method != "WriteTexture") {
        const uint32_t offsetAlignment = isDepthOrStencilTextureFormat(format) ? 4u : info.bytesPerBlock;
        if (layout.offset % offsetAlignment != 0) {
            valid = false;
        }
        if (layout.bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED && layout.bytesPerRow % kBytesPerRowAlignment != 0) {
            valid = false;
        }
    }

    uint32_t bytesPerRow = layout.bytesPerRow;
    if (bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED && bytesPerRow < bytesInLastRow) {
        bytesPerRow = WGPU_COPY_STRIDE_UNDEFINED;
        valid = false;
    }
    if (bytesPerRow == WGPU_COPY_STRIDE_UNDEFINED && (heightBlocks > 1 || depth > 1)) {
        valid = false;
    }

    uint32_t rowsPerImage = layout.rowsPerImage;
    if (rowsPerImage != WGPU_COPY_STRIDE_UNDEFINED && rowsPerImage < heightBlocks) {
        rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;
        valid = false;
    }
    if (rowsPerImage == WGPU_COPY_STRIDE_UNDEFINED && depth > 1) {
        valid = false;
    }

    if (bytesPerRow == WGPU_COPY_STRIDE_UNDEFINED) {
        bytesPerRow = static_cast<uint32_t>(alignTo(bytesInLastRow, kBytesPerRowAlignment));
    }
    if (rowsPerImage == WGPU_COPY_STRIDE_UNDEFINED) {
        rowsPerImage = heightBlocks;
    }

    *copyValid = valid;
    return layout.offset + requiredBytesInCopy(bytesPerRow, rowsPerImage, copySize, format, aspect);
}

WGPUTextureAspect copyableAspect(WGPUTextureFormat format, const std::string& method) {
    if (!isDepthOrStencilTextureFormat(format)) return WGPUTextureAspect_All;
    auto aspects = depthStencilFormatCopyableAspects(parseMethod(method), format);
    return aspects.empty() ? WGPUTextureAspect_All : aspects.front();
}

WGPUTexture createAlignedTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUExtent3D copySize,
    WGPUTextureDimension dimension) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    WGPUExtent3D size = {
        std::max(info.blockWidth, static_cast<uint32_t>(alignTo(copySize.width == 0 ? info.blockWidth : copySize.width, info.blockWidth))),
        std::max(info.blockHeight, static_cast<uint32_t>(alignTo(copySize.height == 0 ? info.blockHeight : copySize.height, info.blockHeight))),
        std::max(1u, copySize.depthOrArrayLayers),
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
    TexelCopyBufferLayout simpleLayout,
    WGPUExtent3D copySize,
    uint64_t dataSize,
    const std::string& method,
    bool success) {
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = simpleLayout.offset;
    layout.bytesPerRow = simpleLayout.bytesPerRow;
    layout.rowsPerImage = simpleLayout.rowsPerImage;

    if (method == "WriteTexture") {
        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture;
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
    tex.aspect = aspect;
    WGPUTexelCopyBufferInfo buf = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    buf.buffer = buffer;
    buf.layout = layout;
    if (method == "CopyB2T") {
        wgpuCommandEncoderCopyBufferToTexture(encoder, &buf, &tex, &copySize);
    } else {
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &tex, &buf, &copySize);
    }
    t.expectValidationError([&] {
        WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
        if (cmd) wgpuCommandBufferRelease(cmd);
    }, !success);
    wgpuCommandEncoderRelease(encoder);
}

CTS_TEST(g, "bound_on_rows_per_image")
    .desc("\nTest that rowsPerImage must be at least the copy height (if defined).\n- for various copy methods\n- for all texture dimensions\n- for various values of rowsPerImage including undefined\n- for various copy heights\n- for various copy depths\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combineWithParams({
                ParamRecord{{"dimension", "1d"}, {"size", "4x1x1"}},
                ParamRecord{{"dimension", "2d"}, {"size", "4x4x1"}},
                ParamRecord{{"dimension", "2d"}, {"size", "4x4x3"}},
                ParamRecord{{"dimension", "3d"}, {"size", "4x4x3"}},
            })
            .beginSubcases()
            .combine("rowsPerImage", {Value::undef(), 0, 1, 2, 1024})
            .combine("copyHeightInBlocks", {0, 1, 2})
            .combine("copyDepth", {1, 3})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    valueAs<int64_t>(*findParam(p, "copyHeightInBlocks")) != 1);
            })
            .filter([](const ParamRecord& p) {
                std::string size = valueAs<std::string>(*findParam(p, "size"));
                uint32_t depth = size == "4x4x3" ? 3u : 1u;
                return static_cast<uint32_t>(valueAs<int64_t>(*findParam(p, "copyDepth"))) <= depth;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        WGPUExtent3D size = t.param<std::string>("size") == "4x1x1" ? WGPUExtent3D{4, 1, 1}
            : (t.param<std::string>("size") == "4x4x1" ? WGPUExtent3D{4, 4, 1} : WGPUExtent3D{4, 4, 3});
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = size;
        desc.dimension = dimension;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(desc);
        uint32_t copyHeight = static_cast<uint32_t>(t.param<int64_t>("copyHeightInBlocks"));
        uint32_t copyDepth = static_cast<uint32_t>(t.param<int64_t>("copyDepth"));
        TexelCopyBufferLayout layout{0, 1024, WGPU_COPY_STRIDE_UNDEFINED};
        if (!t.paramIsUndefined("rowsPerImage")) layout.rowsPerImage = static_cast<uint32_t>(t.param<int64_t>("rowsPerImage"));
        WGPUExtent3D copySize = {0, copyHeight, copyDepth};
        bool valid = false;
        uint64_t bytes = dataSizeForCopyOrOverestimate(
            layout, WGPUTextureFormat_RGBA8Unorm, WGPUTextureAspect_All, copySize, method, &valid);
        testRun(t, texture, WGPUTextureAspect_All, layout, copySize, bytes, method, valid);
    });

CTS_TEST(g, "copy_end_overflows_u64")
    .desc("\nTest an error is produced when offset+requiredBytesInCopy overflows GPUSize64.\n- for various copy methods\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"bytesPerRow", Value(uint64_t(1) << 31)}, {"rowsPerImage", Value(uint64_t(1) << 31)}, {"depthOrArrayLayers", 1}, {"_success", true}},
                ParamRecord{{"bytesPerRow", Value(uint64_t(1) << 31)}, {"rowsPerImage", Value(uint64_t(1) << 31)}, {"depthOrArrayLayers", 16}, {"_success", false}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        uint32_t depth = static_cast<uint32_t>(t.param<int64_t>("depthOrArrayLayers"));
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {1, 1, depth};
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(desc);
        TexelCopyBufferLayout layout{0, static_cast<uint32_t>(t.param<uint64_t>("bytesPerRow")), static_cast<uint32_t>(t.param<uint64_t>("rowsPerImage"))};
        testRun(t, texture, WGPUTextureAspect_All, layout, {1, 1, depth}, 10000, t.param<std::string>("method"), t.param<bool>("_success"));
    });

CTS_TEST(g, "required_bytes_in_copy")
    .desc("\nTest the computation of requiredBytesInCopy by computing the minimum data size for the copy and checking success/error at the boundary.\n- for various copy methods\n- for all formats\n- for all dimensions\n- for various extra bytesPerRow/rowsPerImage\n- for various copy sizes\n- for various offsets in the linear data\n")
    .params([](ParamsBuilder u) {
        std::vector<ParamRecord> records;
        for (const Value& methodValue : imageCopyTypeValues()) {
            std::string method = valueAs<std::string>(methodValue);
            for (WGPUTextureFormat format : sizedFormats()) {
                ParamRecord filterRecord{{"method", method}, {"format", std::string(textureFormatIdentifier(format))}};
                if (!formatCopyableWithMethodRecord(filterRecord)) continue;
                for (WGPUTextureDimension dimension : kTextureDimensions) {
                    if (!formatAndDimensionPossiblyCompatible(dimension, format)) continue;
                    records.push_back({{"method", method}, {"format", std::string(textureFormatIdentifier(format))}, {"dimension", dimensionName(dimension)}});
                }
            }
        }
        return u.combineWithParams(records)
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"bytesPerRowPadding", 0}, {"rowsPerImagePaddingInBlocks", 0}},
                ParamRecord{{"bytesPerRowPadding", 0}, {"rowsPerImagePaddingInBlocks", 6}},
                ParamRecord{{"bytesPerRowPadding", 6}, {"rowsPerImagePaddingInBlocks", 0}},
                ParamRecord{{"bytesPerRowPadding", 15}, {"rowsPerImagePaddingInBlocks", 17}},
            })
            .combineWithParams({
                ParamRecord{{"copyWidthInBlocks", 3}, {"copyHeightInBlocks", 4}, {"copyDepth", 5}, {"_offsetMultiplier", 0}},
                ParamRecord{{"copyWidthInBlocks", 5}, {"copyHeightInBlocks", 4}, {"copyDepth", 3}, {"_offsetMultiplier", 11}},
                ParamRecord{{"copyWidthInBlocks", 256}, {"copyHeightInBlocks", 3}, {"copyDepth", 2}, {"_offsetMultiplier", 0}},
                ParamRecord{{"copyWidthInBlocks", 0}, {"copyHeightInBlocks", 4}, {"copyDepth", 5}, {"_offsetMultiplier", 0}},
                ParamRecord{{"copyWidthInBlocks", 3}, {"copyHeightInBlocks", 0}, {"copyDepth", 5}, {"_offsetMultiplier", 0}},
                ParamRecord{{"copyWidthInBlocks", 3}, {"copyHeightInBlocks", 4}, {"copyDepth", 0}, {"_offsetMultiplier", 13}},
                ParamRecord{{"copyWidthInBlocks", 1}, {"copyHeightInBlocks", 4}, {"copyDepth", 5}, {"_offsetMultiplier", 0}},
                ParamRecord{{"copyWidthInBlocks", 3}, {"copyHeightInBlocks", 1}, {"copyDepth", 5}, {"_offsetMultiplier", 15}},
                ParamRecord{{"copyWidthInBlocks", 5}, {"copyHeightInBlocks", 4}, {"copyDepth", 1}, {"_offsetMultiplier", 0}},
                ParamRecord{{"copyWidthInBlocks", 7}, {"copyHeightInBlocks", 1}, {"copyDepth", 1}, {"_offsetMultiplier", 0}},
            })
            .filter([](const ParamRecord& p) {
                WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                return !isDepthOrStencilTextureFormat(format) ||
                    (valueAs<int64_t>(*findParam(p, "copyWidthInBlocks")) > 0 &&
                     valueAs<int64_t>(*findParam(p, "copyHeightInBlocks")) > 0 &&
                     valueAs<int64_t>(*findParam(p, "copyDepth")) > 0);
            })
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    (valueAs<int64_t>(*findParam(p, "copyHeightInBlocks")) > 1 ||
                     valueAs<int64_t>(*findParam(p, "copyDepth")) > 1));
            })
            .expand("offset", [](const ParamRecord& p) {
                WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                int64_t mult = valueAs<int64_t>(*findParam(p, "_offsetMultiplier"));
                uint32_t scale = isDepthOrStencilTextureFormat(format) ? 4 : getBlockInfoForTextureFormat(format).bytesPerBlock;
                return std::vector<Value>{Value(mult * scale)};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPULimits limits = t.getLimits();
        auto maxSize = getMaxValidTextureSizeForFormatAndDimension(limits, format, dimension);
        uint32_t bprAlignment = method == "WriteTexture" ? 1u : 256u;
        uint32_t copyWidth = std::min(static_cast<uint32_t>(t.param<int64_t>("copyWidthInBlocks")) * info.blockWidth, maxSize[0]);
        uint32_t copyHeight = std::min(static_cast<uint32_t>(t.param<int64_t>("copyHeightInBlocks")) * info.blockHeight, maxSize[1]);
        uint32_t copyDepth = static_cast<uint32_t>(t.param<int64_t>("copyDepth"));
        uint32_t rowsPerImage = copyHeight + static_cast<uint32_t>(t.param<int64_t>("rowsPerImagePaddingInBlocks")) * info.blockHeight;
        uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(bytesInACompleteRow(copyWidth, format), bprAlignment) +
            static_cast<uint32_t>(t.param<int64_t>("bytesPerRowPadding")) * bprAlignment);
        WGPUExtent3D copySize = {copyWidth, copyHeight, copyDepth};
        TexelCopyBufferLayout layout{static_cast<uint64_t>(t.param<int64_t>("offset")), bytesPerRow, rowsPerImage};
        WGPUTexture texture = createAlignedTexture(t, format, copySize, dimension);
        WGPUTextureAspect aspect = copyableAspect(format, method);
        bool copyValid = false;
        uint64_t minSize = dataSizeForCopyOrOverestimate(layout, format, aspect, copySize, method, &copyValid);
        t.expect(copyValid, "required_bytes_in_copy generated an invalid copy layout");
        testRun(t, texture, aspect, layout, copySize, minSize, method, true);
        if (minSize > 0) {
            testRun(t, texture, aspect, layout, copySize, minSize - 1, method, false);
        }
    });

CTS_TEST(g, "rows_per_image_alignment")
    .desc("\nTest that rowsPerImage has no alignment constraints.\n- for various copy methods\n- for all sized format\n- for all dimensions\n- for various rowsPerImage\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combine("format", sizedFormatValues())
            .filter(formatCopyableWithMethodRecord)
            .combine("dimension", dimensionValues())
            .filter([](const ParamRecord& p) {
                return formatAndDimensionPossiblyCompatible(
                    parseDimension(valueAs<std::string>(*findParam(p, "dimension"))),
                    parseTextureFormat(valueAs<std::string>(*findParam(p, "format"))));
            })
            .beginSubcases()
            .expand("rowsPerImage", rowsPerImageExpander)
            .filter([](const ParamRecord& p) {
                WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                return static_cast<uint32_t>(valueAs<int64_t>(*findParam(p, "rowsPerImage"))) >= getBlockInfoForTextureFormat(format).blockHeight;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPUExtent3D size = {info.blockWidth, info.blockHeight, 1};
        WGPUTexture texture = createAlignedTexture(t, format, size, dimension);
        TexelCopyBufferLayout layout{0, 256, static_cast<uint32_t>(t.param<int64_t>("rowsPerImage"))};
        testRun(t, texture, copyableAspect(format, method), layout, size, info.bytesPerBlock, method, true);
    });

CTS_TEST(g, "offset_alignment")
    .desc("\nTest the alignment requirement on the linear data offset (block size, or 4 for depth-stencil).\n- for various copy methods\n- for all sized formats\n- for all dimensions\n- for various linear data offsets\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combine("format", sizedFormatValues())
            .filter(formatCopyableWithMethodRecord)
            .combine("dimension", dimensionValues())
            .filter([](const ParamRecord& p) {
                return formatAndDimensionPossiblyCompatible(
                    parseDimension(valueAs<std::string>(*findParam(p, "dimension"))),
                    parseTextureFormat(valueAs<std::string>(*findParam(p, "format"))));
            })
            .beginSubcases()
            .expand("offset", offsetExpander);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPUExtent3D size = {info.blockWidth, info.blockHeight, 1};
        WGPUTexture texture = createAlignedTexture(t, format, size, dimension);
        uint64_t offset = static_cast<uint64_t>(t.param<int64_t>("offset"));
        bool success = method == "WriteTexture";
        if (isDepthOrStencilTextureFormat(format)) success = success || offset % 4 == 0;
        else success = success || offset % info.bytesPerBlock == 0;
        TexelCopyBufferLayout layout{offset, 256, WGPU_COPY_STRIDE_UNDEFINED};
        testRun(t, texture, copyableAspect(format, method), layout, size, offset + info.bytesPerBlock, method, success);
    });

CTS_TEST(g, "bound_on_bytes_per_row")
    .desc("\nTest that bytesPerRow, if specified must be big enough for a full copy row.\n- for various copy methods\n- for all sized formats\n- for all dimension\n- for various copy heights\n- for various copy depths\n- for various combinations of bytesPerRow and copy width.\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .combine("format", sizedFormatValues())
            .filter(formatCopyableWithMethodRecord)
            .combine("dimension", dimensionValues())
            .filter([](const ParamRecord& p) {
                return formatAndDimensionPossiblyCompatible(
                    parseDimension(valueAs<std::string>(*findParam(p, "dimension"))),
                    parseTextureFormat(valueAs<std::string>(*findParam(p, "format"))));
            })
            .beginSubcases()
            .combine("copyHeightInBlocks", {1, 2})
            .combine("copyDepth", {1, 2})
            .filter([](const ParamRecord& p) {
                return !(valueAs<std::string>(*findParam(p, "dimension")) == "1d" &&
                    (valueAs<int64_t>(*findParam(p, "copyHeightInBlocks")) > 1 ||
                     valueAs<int64_t>(*findParam(p, "copyDepth")) > 1));
            })
            .combine("bprCase", {0, 1, 2, 3, 4, 5});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string method = t.param<std::string>("method");
        WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDimension dimension = parseDimension(t.param<std::string>("dimension"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPUTextureAspect aspect = copyableAspect(format, method);
        const TextureBlockInfo aspectInfo = blockInfoForCopyAspect(format, aspect);
        uint32_t copyHeightBlocks = static_cast<uint32_t>(t.param<int64_t>("copyHeightInBlocks"));
        uint32_t copyDepth = static_cast<uint32_t>(t.param<int64_t>("copyDepth"));
        int64_t which = t.param<int64_t>("bprCase");
        uint32_t bytesPerRow = 256;
        uint32_t widthBlocks = 256 / aspectInfo.bytesPerBlock;
        uint32_t copyWidthBlocks = widthBlocks;
        bool success = true;
        bool bprUndefined = false;
        if (which == 1) { copyWidthBlocks = widthBlocks - 1; success = !isDepthOrStencilTextureFormat(format); }
        if (which == 2) { bytesPerRow = 128; widthBlocks = 128 / aspectInfo.bytesPerBlock; copyWidthBlocks = widthBlocks; success = method == "WriteTexture"; }
        if (which == 3) { bytesPerRow = 384; widthBlocks = 384 / aspectInfo.bytesPerBlock; copyWidthBlocks = widthBlocks; success = method == "WriteTexture"; }
        if (which == 4) { widthBlocks = (2 * 256) / aspectInfo.bytesPerBlock; copyWidthBlocks = widthBlocks; success = false; }
        if (which == 5) { bprUndefined = true; success = !(copyHeightBlocks > 1 || copyDepth > 1); }
        WGPUExtent3D texSize = {widthBlocks * info.blockWidth, copyHeightBlocks * info.blockHeight, copyDepth};
        WGPUTexture texture = createAlignedTexture(t, format, texSize, dimension);
        WGPUExtent3D copySize = {copyWidthBlocks * info.blockWidth, copyHeightBlocks * info.blockHeight, copyDepth};
        TexelCopyBufferLayout layout{0, bprUndefined ? WGPU_COPY_STRIDE_UNDEFINED : bytesPerRow, copyHeightBlocks};
        bool valid = false;
        uint64_t bytes = dataSizeForCopyOrOverestimate(layout, format, aspect, copySize, method, &valid);
        testRun(t, texture, aspect, layout, copySize, bytes, method, success);
    });

CTS_TEST(g, "bound_on_offset")
    .desc("\nTest that the offset cannot be larger than the linear data size (even for an empty copy).\n- for various offsets and data sizes\n")
    .params([](ParamsBuilder u) {
        return u.combine("method", imageCopyTypeValues())
            .beginSubcases()
            .combine("offsetInBlocks", {0, 1, 2})
            .combine("dataSizeInBlocks", {0, 1, 2});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const TextureBlockInfo info = getBlockInfoForColorTextureFormat(WGPUTextureFormat_RGBA8Unorm);
        uint64_t offset = static_cast<uint64_t>(t.param<int64_t>("offsetInBlocks")) * info.bytesPerBlock;
        uint64_t dataSize = static_cast<uint64_t>(t.param<int64_t>("dataSizeInBlocks")) * info.bytesPerBlock;
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {4, 4, 1};
        desc.dimension = WGPUTextureDimension_2D;
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(desc);
        TexelCopyBufferLayout layout{offset, 0, WGPU_COPY_STRIDE_UNDEFINED};
        testRun(t, texture, WGPUTextureAspect_All, layout, {0, 0, 0}, dataSize, t.param<std::string>("method"), offset <= dataSize);
    });

} // namespace
