// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/image_copy.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

enum class InitMethod {
    WriteTexture = 0,
    CopyB2T = 1,
};

enum class CheckMethod {
    FullCopyT2B = 0,
    PartialCopyT2B = 1,
};

struct CopyScenario {
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    InitMethod initMethod = InitMethod::WriteTexture;
    CheckMethod checkMethod = CheckMethod::FullCopyT2B;
    WGPUExtent3D textureSize = WGPUExtent3D{1, 1, 1};
    WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};
    WGPUOrigin3D origin = WGPUOrigin3D{0, 0, 0};
    uint32_t mipLevel = 0;
    TexelCopyBufferLayout uploadLayout;
};

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,image_copy",
    "Image copy operation tests.");

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::vector<uint8_t> generateData(size_t size, uint32_t start = 0) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>((i * 73 + start * 131 + 1) & 0xff);
    }
    return data;
}

std::vector<Value> colorFormatValues() {
    std::vector<Value> values;
    values.reserve(kColorTextureFormats.size());
    for (WGPUTextureFormat format : kColorTextureFormats) {
        values.push_back(static_cast<int64_t>(format));
    }
    return values;
}

std::vector<Value> textureDimensionValues() {
    return {
        static_cast<int64_t>(WGPUTextureDimension_1D),
        static_cast<int64_t>(WGPUTextureDimension_2D),
        static_cast<int64_t>(WGPUTextureDimension_3D),
    };
}

ParamsBuilder baseParams(ParamsBuilder u) {
    return u.combine("format", colorFormatValues())
        .combine("dimension", textureDimensionValues())
        .combine("initMethod", {
            static_cast<int64_t>(InitMethod::WriteTexture),
            static_cast<int64_t>(InitMethod::CopyB2T),
        })
        .combine("checkMethod", {
            static_cast<int64_t>(CheckMethod::FullCopyT2B),
            static_cast<int64_t>(CheckMethod::PartialCopyT2B),
        })
        .filter([](const ParamRecord& params) {
            const auto format = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "format")));
            const auto dimension = static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "dimension")));
            return textureFormatAndDimensionPossiblyCompatible(dimension, format);
        });
}

WGPUExtent3D baseTextureSize(WGPUTextureDimension dimension) {
    switch (dimension) {
        case WGPUTextureDimension_1D:
            return WGPUExtent3D{256, 1, 1};
        case WGPUTextureDimension_2D:
            return WGPUExtent3D{256, 16, 4};
        case WGPUTextureDimension_3D:
            return WGPUExtent3D{256, 16, 8};
        default:
            std::abort();
    }
}

WGPUExtent3D mipSize(WGPUExtent3D base, WGPUTextureDimension dimension, uint32_t mipLevel) {
    WGPUExtent3D size = base;
    size.width = std::max(1u, base.width >> mipLevel);
    if (dimension != WGPUTextureDimension_1D) {
        size.height = std::max(1u, base.height >> mipLevel);
    }
    if (dimension == WGPUTextureDimension_3D) {
        size.depthOrArrayLayers = std::max(1u, base.depthOrArrayLayers >> mipLevel);
    }
    return size;
}

uint32_t copyDepthForDimension(WGPUTextureDimension dimension, uint32_t depth) {
    return dimension == WGPUTextureDimension_3D ? depth : 1;
}

uint32_t copyHeightForDimension(WGPUTextureDimension dimension, uint32_t height) {
    return dimension == WGPUTextureDimension_1D ? 1 : height;
}

TexelCopyBufferLayout concreteLayout(TexelCopyBufferLayout layout, WGPUTextureFormat format, WGPUExtent3D copySize) {
    if (layout.bytesPerRow == WGPU_COPY_STRIDE_UNDEFINED) {
        layout.bytesPerRow = bytesInACompleteRow(copySize.width, format);
    }
    if (layout.rowsPerImage == WGPU_COPY_STRIDE_UNDEFINED) {
        layout.rowsPerImage = copySize.height;
    }
    return layout;
}

uint64_t requiredBytes(TexelCopyBufferLayout layout, WGPUTextureFormat format, WGPUExtent3D copySize) {
    return dataBytesForCopyOrFail(concreteLayout(layout, format, copySize), format, copySize, false);
}

WGPUTexelCopyBufferLayout toWgpuLayout(TexelCopyBufferLayout layout) {
    WGPUTexelCopyBufferLayout out = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    out.offset = layout.offset;
    out.bytesPerRow = layout.bytesPerRow;
    out.rowsPerImage = layout.rowsPerImage;
    return out;
}

TexelCopyBufferLayout uploadLayoutFor(
    WGPUTextureFormat format,
    WGPUExtent3D copySize,
    uint32_t rowPadding,
    uint32_t imagePadding,
    uint64_t offset,
    bool undefinedStrides) {
    TexelCopyBufferLayout layout;
    layout.offset = offset;
    if (undefinedStrides) {
        layout.bytesPerRow = WGPU_COPY_STRIDE_UNDEFINED;
        layout.rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;
    } else {
        layout.bytesPerRow = static_cast<uint32_t>(
            alignTo(bytesInACompleteRow(copySize.width, format) + rowPadding, kBytesPerRowAlignment));
        layout.rowsPerImage = copySize.height + imagePadding;
    }
    return layout;
}

WGPUTexture createCopyTexture(AllFeaturesMaxLimitsGpuTest& t, const CopyScenario& scenario, uint32_t mipLevelCount) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = scenario.textureSize;
    desc.mipLevelCount = mipLevelCount;
    desc.sampleCount = 1;
    desc.dimension = scenario.dimension;
    desc.format = scenario.format;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    return t.createTextureTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void copyBufferToTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUBuffer buffer,
    TexelCopyBufferLayout layout,
    WGPUExtent3D copySize,
    uint32_t mipLevel,
    WGPUOrigin3D origin) {
    WGPUTexelCopyBufferInfo source = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    source.buffer = buffer;
    source.layout = toWgpuLayout(layout);

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture;
    destination.mipLevel = mipLevel;
    destination.origin = origin;
    destination.aspect = WGPUTextureAspect_All;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyBufferToTexture(encoder, &source, &destination, &copySize);
    submit(t, encoder);
}

void copyTextureToBuffer(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUBuffer buffer,
    TexelCopyBufferLayout layout,
    WGPUExtent3D copySize,
    uint32_t mipLevel,
    WGPUOrigin3D origin) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.mipLevel = mipLevel;
    source.origin = origin;
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout = toWgpuLayout(layout);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    submit(t, encoder);
}

void uploadLinearTextureDataToTextureSubBox(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const CopyScenario& scenario,
    const std::vector<uint8_t>& data) {
    if (scenario.initMethod == InitMethod::WriteTexture) {
        t.queueWriteTexture(
            texture,
            scenario.copySize,
            toWgpuLayout(scenario.uploadLayout),
            data.data(),
            data.size(),
            scenario.mipLevel,
            scenario.origin);
        return;
    }

    WGPUBuffer src = t.makeBufferWithContents(data.data(), data.size(), WGPUBufferUsage_CopySrc);
    copyBufferToTexture(t, texture, src, scenario.uploadLayout, scenario.copySize, scenario.mipLevel, scenario.origin);
}

void copyPartialTextureToBufferAndCheckContents(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const CopyScenario& scenario,
    const std::vector<uint8_t>& uploadData) {
    const TextureCopyLayout readInfo = getTextureCopyLayout(scenario.format, scenario.dimension, scenario.copySize);
    TexelCopyBufferLayout readLayout{0, readInfo.bytesPerRow, readInfo.rowsPerImage};
    std::vector<uint8_t> expected = generateData(static_cast<size_t>(readInfo.byteLength), 17);
    std::vector<uint8_t> initial = expected;
    WGPUBuffer dst = t.makeBufferWithContents(initial.data(), initial.size(), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

    LinearTextureSubBox copy;
    copy.src = uploadData.data();
    copy.srcLen = uploadData.size();
    copy.srcLayout = concreteLayout(scenario.uploadLayout, scenario.format, scenario.copySize);
    copy.dst = expected.data();
    copy.dstLen = expected.size();
    copy.dstLayout = readLayout;
    updateLinearTextureDataSubBox(scenario.format, scenario.copySize, copy);

    copyTextureToBuffer(t, texture, dst, readLayout, scenario.copySize, scenario.mipLevel, scenario.origin);
    t.expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
        expected.data(), expected.size(), dst, scenario.format, scenario.copySize, readLayout);
}

std::vector<uint8_t> copyTextureToVector(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    TexelCopyBufferLayout layout,
    WGPUExtent3D copySize,
    uint32_t mipLevel,
    WGPUOrigin3D origin,
    size_t byteLength) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = byteLength;
    desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = t.createBufferTracked(desc);
    copyTextureToBuffer(t, texture, buffer, layout, copySize, mipLevel, origin);

    t.expectMapAsync(buffer, WGPUMapMode_Read, true, 0, byteLength);
    const void* mapped = wgpuBufferGetConstMappedRange(buffer, 0, byteLength);
    if (byteLength > 0 && mapped == nullptr) {
        wgpuBufferUnmap(buffer);
        t.fail("failed to get mapped range for texture snapshot");
    }

    std::vector<uint8_t> data(byteLength);
    if (byteLength > 0) {
        std::memcpy(data.data(), mapped, byteLength);
    }
    wgpuBufferUnmap(buffer);
    return data;
}

void copyWholeTextureToBufferAndCheckContentsWithUpdatedData(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const CopyScenario& scenario,
    const std::vector<uint8_t>& uploadData) {
    const WGPUExtent3D subresourceSize = mipSize(scenario.textureSize, scenario.dimension, scenario.mipLevel);
    const TextureCopyLayout readInfo = getTextureCopyLayout(scenario.format, scenario.dimension, subresourceSize);
    TexelCopyBufferLayout readLayout{0, readInfo.bytesPerRow, readInfo.rowsPerImage};

    uploadLinearTextureDataToTextureSubBox(t, texture, scenario, uploadData);

    // Faithful upstream FullCopyT2B: snapshot the whole subresource after the upload, overlay the known
    // uploaded sub-box onto the snapshot to form the expected contents, then re-read the whole subresource
    // from the texture and compare. The copied region must equal the uploaded data; the rest must equal its
    // (snapshotted) pre-existing contents.
    std::vector<uint8_t> snapshot = copyTextureToVector(
        t,
        texture,
        readLayout,
        subresourceSize,
        scenario.mipLevel,
        WGPUOrigin3D{0, 0, 0},
        static_cast<size_t>(readInfo.byteLength));

    std::vector<uint8_t> expected = snapshot;
    LinearTextureSubBox copy;
    copy.src = uploadData.data();
    copy.srcLen = uploadData.size();
    copy.srcLayout = concreteLayout(scenario.uploadLayout, scenario.format, scenario.copySize);
    copy.dst = expected.data();
    copy.dstLen = expected.size();
    copy.dstLayout = readLayout;
    copy.dstOrigin = scenario.origin;
    updateLinearTextureDataSubBox(scenario.format, scenario.copySize, copy);

    WGPUBufferDescriptor cmpDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    cmpDesc.size = static_cast<uint64_t>(readInfo.byteLength);
    cmpDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer cmp = t.createBufferTracked(cmpDesc);
    copyTextureToBuffer(t, texture, cmp, readLayout, subresourceSize, scenario.mipLevel, WGPUOrigin3D{0, 0, 0});
    t.expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
        expected.data(), expected.size(), cmp, scenario.format, subresourceSize, readLayout);
}

void uploadTextureAndVerifyCopy(AllFeaturesMaxLimitsGpuTest& t, const CopyScenario& scenario, uint32_t mipLevelCount = 1) {
    t.skipIfTextureFormatNotSupported(scenario.format);
    t.skipIfTextureFormatAndDimensionNotCompatible(scenario.format, scenario.dimension);
    if (!isColorTextureFormat(scenario.format)) {
        t.skip("image_copy color slice only covers color formats");
    }

    const uint64_t exactUploadBytes = requiredBytes(scenario.uploadLayout, scenario.format, scenario.copySize);
    std::vector<uint8_t> uploadData = generateData(static_cast<size_t>(alignTo(exactUploadBytes, kBufferCopyAlignment)));
    WGPUTexture texture = createCopyTexture(t, scenario, mipLevelCount);

    if (scenario.checkMethod == CheckMethod::FullCopyT2B) {
        copyWholeTextureToBufferAndCheckContentsWithUpdatedData(t, texture, scenario, uploadData);
    } else {
        uploadLinearTextureDataToTextureSubBox(t, texture, scenario, uploadData);
        copyPartialTextureToBufferAndCheckContents(t, texture, scenario, uploadData);
    }
}

CopyScenario scenarioFromParams(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario;
    scenario.format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
    scenario.dimension = static_cast<WGPUTextureDimension>(t.param<int64_t>("dimension"));
    scenario.initMethod = static_cast<InitMethod>(t.param<int64_t>("initMethod"));
    scenario.checkMethod = static_cast<CheckMethod>(t.param<int64_t>("checkMethod"));
    scenario.textureSize = baseTextureSize(scenario.dimension);
    scenario.copySize = scenario.textureSize;
    scenario.uploadLayout = uploadLayoutFor(scenario.format, scenario.copySize, 0, 0, 0, false);
    return scenario;
}

std::vector<Value> indexValues(uint32_t count) {
    std::vector<Value> values;
    for (uint32_t i = 0; i < count; ++i) {
        values.push_back(static_cast<int64_t>(i));
    }
    return values;
}

ParamsBuilder rowsParams(ParamsBuilder u) {
    return baseParams(u)
        .beginSubcases()
        .combine("paddingIndex", indexValues(4))
        .combine("copySizeIndex", indexValues(14))
        .filter([](const ParamRecord& params) {
            const auto dimension = static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "dimension")));
            const uint32_t copySizeIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "copySizeIndex")));
            return dimension != WGPUTextureDimension_1D || copySizeIndex < 5;
        });
}

ParamsBuilder offsetsParams(ParamsBuilder u) {
    return baseParams(u)
        .beginSubcases()
        .combine("offsetsAndPaddingsIndex", indexValues(11))
        .combine("copyDepthIndex", indexValues(3))
        .combine("copyWidthIndex", indexValues(3))
        .combine("rowsPerImageEqualsCopyHeight", {true, false})
        .filter([](const ParamRecord& params) {
            const auto dimension = static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "dimension")));
            const uint32_t copyDepthIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "copyDepthIndex")));
            return dimension == WGPUTextureDimension_3D || copyDepthIndex == 0;
        });
}

ParamsBuilder originsParams(ParamsBuilder u) {
    return baseParams(u)
        .beginSubcases()
        .combine("originValueInBlocks", indexValues(4))
        .combine("copySizeValueInBlocks", indexValues(4))
        .combine("textureSizePaddingValueInBlocks", indexValues(3))
        .combine("coordinateToTest", indexValues(3))
        .filter([](const ParamRecord& params) {
            const auto dimension = static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "dimension")));
            const uint32_t coordinate = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "coordinateToTest")));
            return !(dimension == WGPUTextureDimension_1D && coordinate != 0)
                && !(dimension == WGPUTextureDimension_2D && coordinate == 2);
        });
}

ParamsBuilder mipParams(ParamsBuilder u) {
    return baseParams(u)
        .beginSubcases()
        .combine("textureSizeIndex", indexValues(4))
        .combine("mipLevelOffset", indexValues(3));
}

ParamsBuilder undefinedParams(ParamsBuilder u) {
    return baseParams(u)
        .beginSubcases()
        .combine("undefinedBytesPerRow", {true, false})
        .combine("undefinedRowsPerImage", {true, false})
        .combine("undefinedOrigin", {true, false})
        .filter([](const ParamRecord& params) {
            const bool undefinedBytesPerRow = valueAs<bool>(*findParam(params, "undefinedBytesPerRow"));
            const bool undefinedRowsPerImage = valueAs<bool>(*findParam(params, "undefinedRowsPerImage"));
            return undefinedBytesPerRow == undefinedRowsPerImage;
        });
}

void runRowsPerImageAndBytesPerRow(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const uint32_t paddingIndex = static_cast<uint32_t>(t.param<int64_t>("paddingIndex"));
    const uint32_t copySizeIndex = static_cast<uint32_t>(t.param<int64_t>("copySizeIndex"));
    static constexpr std::array<uint32_t, 14> widths = {1, 2, 3, 4, 5, 7, 8, 15, 16, 31, 32, 63, 64, 128};
    const uint32_t width = std::min(widths[copySizeIndex], scenario.textureSize.width);
    const uint32_t height = copyHeightForDimension(scenario.dimension, 1 + copySizeIndex % 4);
    const uint32_t depth = copyDepthForDimension(scenario.dimension, 1 + copySizeIndex % 3);
    scenario.copySize = WGPUExtent3D{width, height, depth};
    scenario.uploadLayout = uploadLayoutFor(
        scenario.format, scenario.copySize, paddingIndex * 64, paddingIndex, 0, false);
    uploadTextureAndVerifyCopy(t, scenario);
}

void runOffsetsAndSizes(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const uint32_t offsetIndex = static_cast<uint32_t>(t.param<int64_t>("offsetsAndPaddingsIndex"));
    const uint32_t copyDepthIndex = static_cast<uint32_t>(t.param<int64_t>("copyDepthIndex"));
    const uint32_t copyWidthIndex = static_cast<uint32_t>(t.param<int64_t>("copyWidthIndex"));
    const bool rowsPerImageEqualsCopyHeight = t.param<bool>("rowsPerImageEqualsCopyHeight");
    static constexpr std::array<uint32_t, 3> widths = {1, 17, 256};
    scenario.copySize = WGPUExtent3D{
        std::min(widths[copyWidthIndex], scenario.textureSize.width),
        copyHeightForDimension(scenario.dimension, 1 + offsetIndex % 4),
        copyDepthForDimension(scenario.dimension, 1 + copyDepthIndex),
    };
    const uint32_t imagePadding = rowsPerImageEqualsCopyHeight ? 0 : 1 + offsetIndex % 3;
    const uint32_t offsetStride = static_cast<uint32_t>(
        alignTo(getBlockInfoForTextureFormat(scenario.format).bytesPerBlock, kBufferCopyAlignment));
    scenario.uploadLayout = uploadLayoutFor(
        scenario.format, scenario.copySize, (offsetIndex % 4) * 64, imagePadding, offsetIndex * offsetStride, false);
    uploadTextureAndVerifyCopy(t, scenario);
}

void runOriginsAndExtents(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const uint32_t originValue = static_cast<uint32_t>(t.param<int64_t>("originValueInBlocks"));
    const uint32_t copySizeValue = static_cast<uint32_t>(t.param<int64_t>("copySizeValueInBlocks"));
    const uint32_t texturePadding = static_cast<uint32_t>(t.param<int64_t>("textureSizePaddingValueInBlocks"));
    const uint32_t coordinate = static_cast<uint32_t>(t.param<int64_t>("coordinateToTest"));
    const uint32_t origin = originValue;
    const uint32_t copyExtent = 1 + copySizeValue;
    const uint32_t textureExtent = origin + copyExtent + texturePadding + 1;

    scenario.textureSize = WGPUExtent3D{8, copyHeightForDimension(scenario.dimension, 8), copyDepthForDimension(scenario.dimension, 8)};
    if (coordinate == 0) {
        scenario.textureSize.width = std::max(textureExtent, 1u);
        scenario.origin.x = origin;
        scenario.copySize.width = copyExtent;
        scenario.copySize.height = copyHeightForDimension(scenario.dimension, 2);
        scenario.copySize.depthOrArrayLayers = copyDepthForDimension(scenario.dimension, 2);
    } else if (coordinate == 1) {
        scenario.textureSize.height = textureExtent;
        scenario.origin.y = origin;
        scenario.copySize = WGPUExtent3D{4, copyExtent, copyDepthForDimension(scenario.dimension, 2)};
    } else {
        scenario.textureSize.depthOrArrayLayers = textureExtent;
        scenario.origin.z = origin;
        scenario.copySize = WGPUExtent3D{4, copyHeightForDimension(scenario.dimension, 2), copyExtent};
    }
    scenario.uploadLayout = uploadLayoutFor(scenario.format, scenario.copySize, 256, 1, 0, false);
    uploadTextureAndVerifyCopy(t, scenario);
}

void runMipLevels(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const uint32_t textureSizeIndex = static_cast<uint32_t>(t.param<int64_t>("textureSizeIndex"));
    const uint32_t mipLevelOffset = static_cast<uint32_t>(t.param<int64_t>("mipLevelOffset"));
    static constexpr std::array<uint32_t, 4> baseWidths = {8, 16, 32, 64};
    const uint32_t width = baseWidths[textureSizeIndex];
    scenario.textureSize = WGPUExtent3D{
        width,
        copyHeightForDimension(scenario.dimension, std::max(1u, width / 2)),
        copyDepthForDimension(scenario.dimension, std::max(1u, width / 4)),
    };
    const uint32_t mipLevelCount = maxMipLevelCount(scenario.textureSize, scenario.dimension);
    scenario.mipLevel = std::min(mipLevelOffset, mipLevelCount - 1);
    const WGPUExtent3D subresourceSize = mipSize(scenario.textureSize, scenario.dimension, scenario.mipLevel);
    scenario.copySize = subresourceSize;
    scenario.uploadLayout = uploadLayoutFor(scenario.format, scenario.copySize, 256, 1, 0, false);
    uploadTextureAndVerifyCopy(t, scenario, mipLevelCount);
}

void runUndefinedParams(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const bool undefinedBytesPerRow = t.param<bool>("undefinedBytesPerRow");
    const bool undefinedRowsPerImage = t.param<bool>("undefinedRowsPerImage");
    const bool undefinedOrigin = t.param<bool>("undefinedOrigin");
    scenario.textureSize = WGPUExtent3D{8, 1, 1};
    scenario.copySize = WGPUExtent3D{7, 1, 1};
    scenario.origin = undefinedOrigin ? WGPUOrigin3D{0, 0, 0} : WGPUOrigin3D{1, 0, 0};
    scenario.uploadLayout = uploadLayoutFor(
        scenario.format, scenario.copySize, 0, 0, 0, undefinedBytesPerRow && undefinedRowsPerImage);
    uploadTextureAndVerifyCopy(t, scenario);
}

} // namespace

CTS_TEST(g, "rowsPerImage_and_bytesPerRow")
    .params(rowsParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runRowsPerImageAndBytesPerRow(t); });

CTS_TEST(g, "offsets_and_sizes")
    .params(offsetsParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runOffsetsAndSizes(t); });

CTS_TEST(g, "origins_and_extents")
    .params(originsParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runOriginsAndExtents(t); });

CTS_TEST(g, "mip_levels")
    .params(mipParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runMipLevels(t); });

CTS_TEST(g, "undefined_params")
    .params(undefinedParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runUndefinedParams(t); });
