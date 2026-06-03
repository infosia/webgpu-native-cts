// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/image_copy.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
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

enum class DepthStencilAspect {
    DepthOnly = 0,
    StencilOnly = 1,
};

struct PaddingConfig {
    uint32_t bytesPerRowPadding = 0;
    uint32_t rowsPerImagePadding = 0;
};

struct DepthStencilCopySize {
    uint32_t copyWidthInBlocks = 0;
    uint32_t copyHeightInBlocks = 0;
    uint32_t copyDepth = 0;
};

struct OffsetAndPaddingConfig {
    uint32_t offsetInBlocks = 0;
    uint32_t dataPaddingInBytes = 0;
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

constexpr std::array<PaddingConfig, 4> kDepthStencilPaddings = {{
    {0, 0},
    {0, 6},
    {6, 0},
    {15, 17},
}};

constexpr std::array<DepthStencilCopySize, 12> kDepthStencilCopySizes = {{
    {3, 4, 5},
    {5, 4, 2},
    {0, 4, 5},
    {3, 0, 5},
    {3, 4, 0},
    {256, 3, 2},
    {1, 3, 5},
    {32, 1, 8},
    {5, 4, 1},
    {7, 1, 1},
    {3, 3, 1},
    {4, 4, 6},
}};

constexpr std::array<OffsetAndPaddingConfig, 11> kDepthStencilOffsetsAndPaddings = {{
    {0, 0},
    {1, 0},
    {2, 0},
    {15, 0},
    {16, 0},
    {242, 0},
    {243, 0},
    {768, 0},
    {769, 0},
    {0, 1},
    {1, 8},
}};

constexpr std::array<uint32_t, 3> kDepthStencilCopyDepths = {1, 2, 6};

constexpr std::string_view kDepthStencilVertexShader = R"(
@vertex
fn main(@builtin(vertex_index) VertexIndex : u32)-> @builtin(position) vec4<f32> {
  var pos : array<vec2<f32>, 6> = array<vec2<f32>, 6>(
      vec2<f32>(-1.0,  1.0), vec2<f32>(-1.0, -1.0), vec2<f32>( 1.0,  1.0),
      vec2<f32>(-1.0, -1.0), vec2<f32>( 1.0,  1.0), vec2<f32>( 1.0, -1.0));
  return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
}
)";

constexpr std::string_view kDepthStencilFragmentShader = R"(
@group(0) @binding(0) var inputTexture: texture_2d<f32>;
@fragment fn main(@builtin(position) fragcoord : vec4<f32>) -> @builtin(frag_depth) f32 {
  var depthValue : vec4<f32> = textureLoad(inputTexture, vec2<i32>(fragcoord.xy), 0);
  return depthValue.x;
}
)";

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
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

std::vector<Value> depthStencilFormatValues() {
    std::vector<Value> values;
    values.reserve(kDepthStencilFormats.size());
    for (WGPUTextureFormat format : kDepthStencilFormats) {
        values.push_back(static_cast<int64_t>(format));
    }
    return values;
}

std::vector<Value> depthStencilAspectValues() {
    return {
        static_cast<int64_t>(DepthStencilAspect::DepthOnly),
        static_cast<int64_t>(DepthStencilAspect::StencilOnly),
    };
}

std::vector<Value> depthStencilCopyMethodValues() {
    return {
        static_cast<int64_t>(ImageCopyType::WriteTexture),
        static_cast<int64_t>(ImageCopyType::CopyB2T),
        static_cast<int64_t>(ImageCopyType::CopyT2B),
    };
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

void copyBufferToTextureAspect(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUBuffer buffer,
    TexelCopyBufferLayout layout,
    WGPUExtent3D copySize,
    uint32_t mipLevel,
    WGPUOrigin3D origin,
    WGPUTextureAspect aspect) {
    WGPUTexelCopyBufferInfo source = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    source.buffer = buffer;
    source.layout = toWgpuLayout(layout);

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture;
    destination.mipLevel = mipLevel;
    destination.origin = origin;
    destination.aspect = aspect;

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

void copyTextureToBufferAspect(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUBuffer buffer,
    TexelCopyBufferLayout layout,
    WGPUExtent3D copySize,
    uint32_t mipLevel,
    WGPUOrigin3D origin,
    WGPUTextureAspect aspect) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.mipLevel = mipLevel;
    source.origin = origin;
    source.aspect = aspect;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout = toWgpuLayout(layout);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    submit(t, encoder);
}

void writeTextureAspect(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUExtent3D size,
    uint32_t mipLevel,
    WGPUOrigin3D origin,
    WGPUTextureAspect aspect,
    TexelCopyBufferLayout layout,
    const std::vector<uint8_t>& data) {
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture;
    destination.mipLevel = mipLevel;
    destination.origin = origin;
    destination.aspect = aspect;

    WGPUTexelCopyBufferLayout wgpuLayout = toWgpuLayout(layout);
    wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &wgpuLayout, &size);
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

WGPUTextureAspect textureAspect(DepthStencilAspect aspect) {
    return aspect == DepthStencilAspect::DepthOnly ? WGPUTextureAspect_DepthOnly : WGPUTextureAspect_StencilOnly;
}

bool copyMethodSupportedWithDepthStencilFormat(
    DepthStencilAspect aspect,
    WGPUTextureFormat format,
    ImageCopyType copyMethod) {
    const WGPUTextureAspect wgpuAspect = textureAspect(aspect);
    return (aspect == DepthStencilAspect::StencilOnly && isStencilTextureFormat(format))
        || (aspect == DepthStencilAspect::DepthOnly
            && isDepthTextureFormat(format)
            && copyMethod == ImageCopyType::CopyT2B
            && depthStencilBufferTextureCopySupported(copyMethod, format, wgpuAspect));
}

WGPUTexture createDepthStencilCopyTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUExtent3D size,
    uint32_t mipLevelCount) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = size;
    desc.mipLevelCount = mipLevelCount;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

WGPUTextureView createDepthStencilLayerView(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mipLevel,
    uint32_t layer) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = format;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.baseMipLevel = mipLevel;
    desc.mipLevelCount = 1;
    desc.baseArrayLayer = layer;
    desc.arrayLayerCount = 1;
    desc.aspect = WGPUTextureAspect_All;
    return t.createViewTracked(texture, desc);
}

WGPUTextureView createR32FloatLayerView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, uint32_t layer) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = WGPUTextureFormat_R32Float;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.baseMipLevel = 0;
    desc.mipLevelCount = 1;
    desc.baseArrayLayer = layer;
    desc.arrayLayerCount = 1;
    desc.aspect = WGPUTextureAspect_All;
    return t.createViewTracked(texture, desc);
}

WGPUBindGroupLayout createDepthInputBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Fragment;
    entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    entry.texture.viewDimension = WGPUTextureViewDimension_2D;
    entry.texture.multisampled = WGPU_FALSE;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createSingleBindGroupPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

WGPUBindGroup createDepthInputBindGroup(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout layout, WGPUTextureView view) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.textureView = view;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPURenderPipeline createDepthStagingPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat depthStencilFormat,
    WGPUPipelineLayout layout) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kDepthStencilVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kDepthStencilFragmentShader);

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = depthStencilFormat;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencil.depthCompare = WGPUCompareFunction_Always;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 0;
    fragment.targets = nullptr;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.depthStencil = &depthStencil;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

void initializeDepthAspectWithRendering(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture depthTexture,
    WGPUTextureFormat format,
    WGPUExtent3D copySize,
    uint32_t copyMipLevel,
    const std::vector<float>& initialData) {
    WGPUTextureDescriptor inputDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    inputDesc.size = copySize;
    inputDesc.mipLevelCount = 1;
    inputDesc.sampleCount = 1;
    inputDesc.dimension = WGPUTextureDimension_2D;
    inputDesc.format = WGPUTextureFormat_R32Float;
    inputDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
    WGPUTexture inputTexture = t.createTextureTracked(inputDesc);

    std::vector<uint8_t> inputBytes(initialData.size() * sizeof(float));
    if (!inputBytes.empty()) {
        std::memcpy(inputBytes.data(), initialData.data(), inputBytes.size());
    }
    WGPUTexelCopyBufferLayout inputLayout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    inputLayout.offset = 0;
    inputLayout.bytesPerRow = copySize.width * sizeof(float);
    inputLayout.rowsPerImage = copySize.height;
    t.queueWriteTexture(inputTexture, copySize, inputLayout, inputBytes.data(), inputBytes.size(), 0, WGPUOrigin3D{0, 0, 0});

    WGPUBindGroupLayout bindGroupLayout = createDepthInputBindGroupLayout(t);
    WGPUPipelineLayout pipelineLayout = createSingleBindGroupPipelineLayout(t, bindGroupLayout);
    WGPURenderPipeline pipeline = createDepthStagingPipeline(t, format, pipelineLayout);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    for (uint32_t layer = 0; layer < copySize.depthOrArrayLayers; ++layer) {
        WGPUTextureView inputView = createR32FloatLayerView(t, inputTexture, layer);
        WGPUBindGroup bindGroup = createDepthInputBindGroup(t, bindGroupLayout, inputView);
        WGPUTextureView depthView = createDepthStencilLayerView(t, depthTexture, format, copyMipLevel, layer);

        WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        attachment.view = depthView;
        attachment.depthLoadOp = WGPULoadOp_Clear;
        attachment.depthStoreOp = WGPUStoreOp_Store;
        attachment.depthClearValue = 0.0f;
        if (isStencilTextureFormat(format)) {
            attachment.stencilLoadOp = WGPULoadOp_Load;
            attachment.stencilStoreOp = WGPUStoreOp_Store;
        }

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 0;
        passDesc.colorAttachments = nullptr;
        passDesc.depthStencilAttachment = &attachment;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
    }
    submit(t, encoder);
}

uint64_t aspectDataBytesForCopy(
    TexelCopyBufferLayout layout,
    uint32_t aspectSize,
    WGPUExtent3D copySize,
    uint32_t dataPaddingInBytes = 0) {
    const uint64_t lastLayerOffset = static_cast<uint64_t>(copySize.depthOrArrayLayers - 1) * layout.rowsPerImage * layout.bytesPerRow;
    const uint64_t lastRowOffset = static_cast<uint64_t>(copySize.height - 1) * layout.bytesPerRow;
    const uint64_t rowBytes = static_cast<uint64_t>(copySize.width) * aspectSize;
    return layout.offset + lastLayerOffset + lastRowOffset + rowBytes + dataPaddingInBytes;
}

void copyAspectRows(
    std::vector<uint8_t>& expected,
    const std::vector<uint8_t>& source,
    TexelCopyBufferLayout sourceLayout,
    TexelCopyBufferLayout destinationLayout,
    WGPUExtent3D copySize,
    uint32_t bytesPerBlock) {
    const uint32_t bytesPerRow = copySize.width * bytesPerBlock;
    for (uint32_t z = 0; z < copySize.depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < copySize.height; ++y) {
            const uint64_t srcOffset = sourceLayout.offset
                + static_cast<uint64_t>(z) * sourceLayout.rowsPerImage * sourceLayout.bytesPerRow
                + static_cast<uint64_t>(y) * sourceLayout.bytesPerRow;
            const uint64_t dstOffset = destinationLayout.offset
                + static_cast<uint64_t>(z) * destinationLayout.rowsPerImage * destinationLayout.bytesPerRow
                + static_cast<uint64_t>(y) * destinationLayout.bytesPerRow;
            std::memcpy(expected.data() + dstOffset, source.data() + srcOffset, bytesPerRow);
        }
    }
}

void doUploadToStencilTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUExtent3D textureSize,
    WGPUExtent3D copySize,
    ImageCopyType copyMethod,
    uint32_t bytesPerRow,
    uint32_t rowsPerImage,
    uint64_t initialDataSize,
    uint64_t initialDataOffset,
    uint32_t mipLevel) {
    WGPUTexture texture = createDepthStencilCopyTexture(t, format, textureSize, mipLevel + 1);
    const size_t dataSize = static_cast<size_t>(alignTo(initialDataSize, kBufferSizeAlignment));
    const std::vector<uint8_t> initialData = generateData(dataSize, static_cast<uint32_t>(initialDataOffset));
    const TexelCopyBufferLayout uploadLayout{initialDataOffset, bytesPerRow, rowsPerImage};
    if (copyMethod == ImageCopyType::WriteTexture) {
        writeTextureAspect(t, texture, copySize, mipLevel, WGPUOrigin3D{0, 0, 0}, WGPUTextureAspect_StencilOnly, uploadLayout, initialData);
    } else {
        WGPUBuffer uploadBuffer = t.makeBufferWithContents(initialData.data(), initialData.size(), WGPUBufferUsage_CopySrc);
        copyBufferToTextureAspect(t, texture, uploadBuffer, uploadLayout, copySize, mipLevel, WGPUOrigin3D{0, 0, 0}, WGPUTextureAspect_StencilOnly);
    }

    const uint32_t outputBytesPerRow = static_cast<uint32_t>(alignTo(bytesPerRow, kBytesPerRowAlignment));
    const TexelCopyBufferLayout outputLayout{0, outputBytesPerRow, rowsPerImage};
    const uint64_t outputSize = alignTo(aspectDataBytesForCopy(outputLayout, 1, copySize), kBufferCopyAlignment);
    std::vector<uint8_t> expected(static_cast<size_t>(outputSize), 0);
    copyAspectRows(expected, initialData, uploadLayout, outputLayout, copySize, 1);
    std::vector<uint8_t> outputInitial(expected.size(), 0);
    WGPUBuffer output = t.makeBufferWithContents(outputInitial.data(), outputInitial.size(), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    copyTextureToBufferAspect(t, texture, output, outputLayout, copySize, mipLevel, WGPUOrigin3D{0, 0, 0}, WGPUTextureAspect_StencilOnly);
    t.expectGPUBufferValuesEqual(output, expected.data(), expected.size());
}

void doCopyFromStencilTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUExtent3D textureSize,
    WGPUExtent3D copySize,
    uint32_t bytesPerRow,
    uint32_t rowsPerImage,
    uint64_t offset,
    uint32_t mipLevel) {
    WGPUTexture texture = createDepthStencilCopyTexture(t, format, textureSize, mipLevel + 1);
    const TexelCopyBufferLayout sourceLayout{0, copySize.width, copySize.height};
    const uint64_t sourceSize = aspectDataBytesForCopy(sourceLayout, 1, copySize);
    const std::vector<uint8_t> initialData = generateData(static_cast<size_t>(sourceSize));
    writeTextureAspect(t, texture, copySize, mipLevel, WGPUOrigin3D{0, 0, 0}, WGPUTextureAspect_StencilOnly, sourceLayout, initialData);

    const TexelCopyBufferLayout outputLayout{offset, bytesPerRow, rowsPerImage};
    const uint64_t outputSize = alignTo(aspectDataBytesForCopy(outputLayout, 1, copySize), kBufferCopyAlignment);
    std::vector<uint8_t> expected(static_cast<size_t>(outputSize), 0);
    copyAspectRows(expected, initialData, sourceLayout, outputLayout, copySize, 1);
    std::vector<uint8_t> outputInitial(expected.size(), 0);
    WGPUBuffer output = t.makeBufferWithContents(outputInitial.data(), outputInitial.size(), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    copyTextureToBufferAspect(t, texture, output, outputLayout, copySize, mipLevel, WGPUOrigin3D{0, 0, 0}, WGPUTextureAspect_StencilOnly);
    t.expectGPUBufferValuesEqual(output, expected.data(), expected.size());
}

std::vector<float> generateDepthValues(WGPUExtent3D copySize, WGPUTextureFormat format, std::vector<uint8_t>* expectedTightlyPacked) {
    const uint32_t aspectSize = depthStencilFormatAspectSize(format, WGPUTextureAspect_DepthOnly);
    const size_t elementCount = static_cast<size_t>(copySize.width) * copySize.height * copySize.depthOrArrayLayers;
    std::vector<float> depthValues(elementCount);
    expectedTightlyPacked->assign(elementCount * aspectSize, 0);
    for (size_t i = 0; i < elementCount; ++i) {
        float value = i % 40 == 0 ? 1.0f : static_cast<float>(std::fmod(0.05 * static_cast<double>(i), 1.0));
        if (format == WGPUTextureFormat_Depth16Unorm) {
            const uint16_t quantized = static_cast<uint16_t>(value * 65535.0f);
            value = static_cast<float>(quantized) / 65535.0f;
            std::memcpy(expectedTightlyPacked->data() + i * aspectSize, &quantized, sizeof(quantized));
        } else {
            std::memcpy(expectedTightlyPacked->data() + i * aspectSize, &value, sizeof(value));
        }
        depthValues[i] = value;
    }
    return depthValues;
}

void doCopyTextureToBufferWithDepthAspectTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUExtent3D copySize,
    uint32_t bytesPerRowPadding,
    uint32_t rowsPerImagePadding,
    uint64_t offset,
    uint32_t dataPaddingInBytes,
    uint32_t mipLevel) {
    const uint32_t aspectSize = depthStencilFormatAspectSize(format, WGPUTextureAspect_DepthOnly);
    const uint32_t bytesPerRow = static_cast<uint32_t>(
        alignTo(static_cast<uint64_t>(aspectSize) * copySize.width, kBytesPerRowAlignment)
        + static_cast<uint64_t>(bytesPerRowPadding) * kBytesPerRowAlignment);
    const uint32_t rowsPerImage = copySize.height + rowsPerImagePadding;
    const WGPUExtent3D textureSize{copySize.width << mipLevel, copySize.height << mipLevel, copySize.depthOrArrayLayers};
    WGPUTexture texture = createDepthStencilCopyTexture(t, format, textureSize, mipLevel + 1);

    std::vector<uint8_t> tightExpected;
    const std::vector<float> depthValues = generateDepthValues(copySize, format, &tightExpected);
    initializeDepthAspectWithRendering(t, texture, format, copySize, mipLevel, depthValues);

    const TexelCopyBufferLayout tightLayout{0, copySize.width * aspectSize, copySize.height};
    const TexelCopyBufferLayout outputLayout{offset, bytesPerRow, rowsPerImage};
    const uint64_t outputSize = alignTo(aspectDataBytesForCopy(outputLayout, aspectSize, copySize, dataPaddingInBytes), kBufferCopyAlignment);
    std::vector<uint8_t> expected(static_cast<size_t>(outputSize), 0);
    copyAspectRows(expected, tightExpected, tightLayout, outputLayout, copySize, aspectSize);
    std::vector<uint8_t> outputInitial(expected.size(), 0);
    WGPUBuffer output = t.makeBufferWithContents(outputInitial.data(), outputInitial.size(), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    copyTextureToBufferAspect(t, texture, output, outputLayout, copySize, mipLevel, WGPUOrigin3D{0, 0, 0}, WGPUTextureAspect_DepthOnly);
    t.expectGPUBufferValuesEqual(output, expected.data(), expected.size());
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

ParamsBuilder depthStencilBaseParams(ParamsBuilder u) {
    return u.combine("format", depthStencilFormatValues())
        .combine("copyMethod", depthStencilCopyMethodValues())
        .combine("aspect", depthStencilAspectValues())
        .filter([](const ParamRecord& params) {
            const auto format = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "format")));
            const auto copyMethod = static_cast<ImageCopyType>(valueAs<int64_t>(*findParam(params, "copyMethod")));
            const auto aspect = static_cast<DepthStencilAspect>(valueAs<int64_t>(*findParam(params, "aspect")));
            return copyMethodSupportedWithDepthStencilFormat(aspect, format, copyMethod);
        });
}

ParamsBuilder rowsDepthStencilParams(ParamsBuilder u) {
    return depthStencilBaseParams(u)
        .beginSubcases()
        .combine("paddingIndex", indexValues(static_cast<uint32_t>(kDepthStencilPaddings.size())))
        .combine("copySizeIndex", indexValues(static_cast<uint32_t>(kDepthStencilCopySizes.size())))
        .filter([](const ParamRecord& params) {
            const uint32_t copySizeIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "copySizeIndex")));
            const DepthStencilCopySize copySize = kDepthStencilCopySizes[copySizeIndex];
            return copySize.copyWidthInBlocks * copySize.copyHeightInBlocks * copySize.copyDepth > 0;
        })
        .combine("mipLevel", {0, 2});
}

ParamsBuilder offsetsDepthStencilParams(ParamsBuilder u) {
    return depthStencilBaseParams(u)
        .beginSubcases()
        .combine("offsetsAndPaddingsIndex", indexValues(static_cast<uint32_t>(kDepthStencilOffsetsAndPaddings.size())))
        .filter([](const ParamRecord& params) {
            const uint32_t index = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "offsetsAndPaddingsIndex")));
            return kDepthStencilOffsetsAndPaddings[index].offsetInBlocks % 4 == 0;
        })
        .combine("copyDepthIndex", indexValues(static_cast<uint32_t>(kDepthStencilCopyDepths.size())))
        .combine("mipLevel", {0, 2});
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

void runRowsPerImageAndBytesPerRowDepthStencil(AllFeaturesMaxLimitsGpuTest& t) {
    const auto format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
    const auto copyMethod = static_cast<ImageCopyType>(t.param<int64_t>("copyMethod"));
    const auto aspect = static_cast<DepthStencilAspect>(t.param<int64_t>("aspect"));
    t.skipIfTextureFormatNotSupported(format);

    const uint32_t paddingIndex = static_cast<uint32_t>(t.param<int64_t>("paddingIndex"));
    const uint32_t copySizeIndex = static_cast<uint32_t>(t.param<int64_t>("copySizeIndex"));
    const uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
    const PaddingConfig padding = kDepthStencilPaddings[paddingIndex];
    const DepthStencilCopySize copySizeInBlocks = kDepthStencilCopySizes[copySizeIndex];
    const uint32_t bytesPerBlock = depthStencilFormatAspectSize(format, textureAspect(aspect));
    const uint32_t bytesPerRowAlignment = copyMethod == ImageCopyType::WriteTexture ? 1 : kBytesPerRowAlignment;
    const uint32_t bytesPerRow = static_cast<uint32_t>(
        alignTo(static_cast<uint64_t>(bytesPerBlock) * copySizeInBlocks.copyWidthInBlocks, bytesPerRowAlignment)
        + static_cast<uint64_t>(padding.bytesPerRowPadding) * bytesPerRowAlignment);
    const uint32_t rowsPerImage = copySizeInBlocks.copyHeightInBlocks + padding.rowsPerImagePadding;
    const WGPUExtent3D copySize{
        copySizeInBlocks.copyWidthInBlocks,
        copySizeInBlocks.copyHeightInBlocks,
        copySizeInBlocks.copyDepth,
    };
    const WGPUExtent3D textureSize{
        copySizeInBlocks.copyWidthInBlocks << mipLevel,
        copySizeInBlocks.copyHeightInBlocks << mipLevel,
        copySizeInBlocks.copyDepth,
    };

    if (copyMethod == ImageCopyType::CopyT2B) {
        if (aspect == DepthStencilAspect::DepthOnly) {
            doCopyTextureToBufferWithDepthAspectTest(
                t,
                format,
                copySize,
                padding.bytesPerRowPadding,
                padding.rowsPerImagePadding,
                0,
                0,
                mipLevel);
        } else {
            doCopyFromStencilTest(t, format, textureSize, copySize, bytesPerRow, rowsPerImage, 0, mipLevel);
        }
        return;
    }

    t.expect(aspect == DepthStencilAspect::StencilOnly, "only stencil upload is supported in this test");
    const TexelCopyBufferLayout uploadLayout{0, bytesPerRow, rowsPerImage};
    const uint64_t initialDataSize = dataBytesForCopyOrFail(
        uploadLayout,
        WGPUTextureFormat_Stencil8,
        copySize,
        copyMethod != ImageCopyType::WriteTexture);
    doUploadToStencilTest(t, format, textureSize, copySize, copyMethod, bytesPerRow, rowsPerImage, initialDataSize, 0, mipLevel);
}

void runOffsetsAndSizesDepthStencil(AllFeaturesMaxLimitsGpuTest& t) {
    const auto format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
    const auto copyMethod = static_cast<ImageCopyType>(t.param<int64_t>("copyMethod"));
    const auto aspect = static_cast<DepthStencilAspect>(t.param<int64_t>("aspect"));
    t.skipIfTextureFormatNotSupported(format);

    const uint32_t offsetIndex = static_cast<uint32_t>(t.param<int64_t>("offsetsAndPaddingsIndex"));
    const uint32_t copyDepthIndex = static_cast<uint32_t>(t.param<int64_t>("copyDepthIndex"));
    const uint32_t mipLevel = static_cast<uint32_t>(t.param<int64_t>("mipLevel"));
    const OffsetAndPaddingConfig offsetAndPadding = kDepthStencilOffsetsAndPaddings[offsetIndex];
    const uint32_t bytesPerBlock = depthStencilFormatAspectSize(format, textureAspect(aspect));
    const uint64_t initialDataOffset = static_cast<uint64_t>(offsetAndPadding.offsetInBlocks) * bytesPerBlock;
    const WGPUExtent3D copySize{3, 3, kDepthStencilCopyDepths[copyDepthIndex]};
    const WGPUExtent3D textureSize{3u << mipLevel, 3u << mipLevel, copySize.depthOrArrayLayers};
    const uint32_t rowsPerImage = 3;
    const uint32_t bytesPerRow = kBytesPerRowAlignment;

    if (copyMethod == ImageCopyType::CopyT2B) {
        if (aspect == DepthStencilAspect::DepthOnly) {
            doCopyTextureToBufferWithDepthAspectTest(
                t,
                format,
                copySize,
                0,
                0,
                0,
                0,
                mipLevel);
        } else {
            doCopyFromStencilTest(t, format, textureSize, copySize, bytesPerRow, rowsPerImage, initialDataOffset, mipLevel);
        }
        return;
    }

    t.expect(aspect == DepthStencilAspect::StencilOnly, "only stencil upload is supported in this test");
    const TexelCopyBufferLayout uploadLayout{initialDataOffset, bytesPerRow, rowsPerImage};
    const uint64_t initialDataSize = dataBytesForCopyOrFail(
        uploadLayout,
        WGPUTextureFormat_Stencil8,
        copySize,
        copyMethod != ImageCopyType::WriteTexture)
        + offsetAndPadding.dataPaddingInBytes;
    doUploadToStencilTest(
        t,
        format,
        textureSize,
        copySize,
        copyMethod,
        bytesPerRow,
        rowsPerImage,
        initialDataSize,
        initialDataOffset,
        mipLevel);
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

CTS_TEST(g, "rowsPerImage_and_bytesPerRow_depth_stencil")
    .params(rowsDepthStencilParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runRowsPerImageAndBytesPerRowDepthStencil(t); });

CTS_TEST(g, "offsets_and_sizes_copy_depth_stencil")
    .params(offsetsDepthStencilParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runOffsetsAndSizesDepthStencil(t); });
