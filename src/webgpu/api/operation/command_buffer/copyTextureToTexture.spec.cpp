// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/copyTextureToTexture.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

struct SignedOrigin {
    int32_t x = 0;
    int32_t y = 0;
    int32_t z = 0;
};

struct SignedExtent {
    int32_t width = 0;
    int32_t height = 0;
    int32_t depthOrArrayLayers = 0;
};

struct CopyBoxOffset {
    SignedOrigin srcOffset;
    SignedOrigin dstOffset;
    SignedExtent copyExtent;
};

struct SizePair {
    WGPUExtent3D srcTextureSize;
    WGPUExtent3D dstTextureSize;
};

struct ZeroSizedConfig {
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    WGPUExtent3D textureSize = WGPUExtent3D{1, 1, 1};
};

struct DepthStencilParams {
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUExtent3D srcTextureSize = WGPUExtent3D{1, 1, 1};
    uint32_t srcCopyLevel = 0;
    uint32_t dstCopyLevel = 0;
    uint32_t srcCopyBaseArrayLayer = 0;
    uint32_t dstCopyBaseArrayLayer = 0;
};

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,copyTextureToTexture",
    "Texture to texture copy operation tests.");

constexpr std::array<CopyBoxOffset, 7> kCopyBoxOffsetsForWholeDepth = {{
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {1, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}},
    {{1, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 1, 0}, {0, 0, 0}, {0, 0, 0}},
    {{1, 0, 0}, {0, 0, 0}, {-1, 0, 0}},
    {{0, 1, 0}, {0, 0, 0}, {0, -1, 0}},
}};

constexpr std::array<CopyBoxOffset, 13> kCopyBoxOffsetsFor2DArrayTextures = {{
    {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {1, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 1, 0}, {0, 0, 0}},
    {{1, 0, 0}, {0, 0, 0}, {0, 0, 0}},
    {{0, 1, 0}, {0, 0, 0}, {0, 0, 0}},
    {{1, 0, 0}, {0, 0, 0}, {-1, 0, 0}},
    {{0, 1, 0}, {0, 0, 0}, {0, -1, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, -2}},
    {{0, 0, 1}, {0, 0, 1}, {0, 0, -3}},
    {{0, 0, 0}, {0, 0, 1}, {0, 0, -1}},
    {{0, 0, 1}, {0, 0, 0}, {0, 0, -1}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, -3}},
    {{0, 0, 1}, {0, 0, 1}, {0, 0, -1}},
}};

constexpr std::array<CopyBoxOffset, 9> kZeroSizedCopyBoxOffsets = {{
    {{0, 0, 0}, {0, 0, 0}, {-64, 0, 0}},
    {{64, 0, 0}, {0, 0, 0}, {-64, 0, 0}},
    {{0, 0, 0}, {64, 0, 0}, {-64, 0, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, -32, 0}},
    {{0, 32, 0}, {0, 0, 0}, {0, -32, 0}},
    {{0, 0, 0}, {0, 32, 0}, {0, -32, 0}},
    {{0, 0, 0}, {0, 0, 0}, {0, 0, -5}},
    {{0, 0, 5}, {0, 0, 0}, {0, 0, 0}},
    {{0, 0, 0}, {0, 0, 5}, {0, 0, 0}},
}};

constexpr std::array<SizePair, 4> kNonArraySizePairs = {{
    {{32, 32, 1}, {32, 32, 1}},
    {{31, 33, 1}, {31, 33, 1}},
    {{32, 32, 1}, {64, 64, 1}},
    {{32, 32, 1}, {63, 61, 1}},
}};

constexpr std::array<SizePair, 4> kArraySizePairs = {{
    {{64, 32, 5}, {64, 32, 5}},
    {{31, 33, 5}, {31, 33, 5}},
    {{31, 32, 33}, {31, 32, 33}},
    {{32, 32, 6}, {32, 32, 6}},
}};

constexpr std::array<ZeroSizedConfig, 3> kZeroSizedConfigs = {{
    {WGPUTextureDimension_1D, {32, 1, 1}},
    {WGPUTextureDimension_2D, {32, 32, 5}},
    {WGPUTextureDimension_3D, {32, 32, 5}},
}};

constexpr std::array<WGPUExtent3D, 3> kDepthStencilTextureSizes = {{
    {32, 16, 1},
    {32, 16, 4},
    {24, 48, 5},
}};

constexpr uint32_t kDynamicUniformStride = 256;

constexpr std::string_view kDepthVertexShader = R"(
struct Params { copyLayer: f32 };
@group(0) @binding(0) var<uniform> param: Params;
@vertex
fn main(@builtin(vertex_index) VertexIndex : u32)-> @builtin(position) vec4<f32> {
  var depthValue = 0.5 + 0.2 * sin(param.copyLayer);
  var pos : array<vec3<f32>, 6> = array<vec3<f32>, 6>(
      vec3<f32>(-1.0,  1.0, depthValue),
      vec3<f32>(-1.0, -1.0, 0.0),
      vec3<f32>( 1.0,  1.0, 1.0),
      vec3<f32>(-1.0, -1.0, 0.0),
      vec3<f32>( 1.0,  1.0, 1.0),
      vec3<f32>( 1.0, -1.0, depthValue));
  return vec4<f32>(pos[VertexIndex], 1.0);
}
)";

constexpr std::string_view kGreenFragmentShader = R"(
@fragment
fn main() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }
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

std::vector<Value> regularTextureFormatValues() {
    std::vector<Value> values;
    values.reserve(kRegularTextureFormats.size());
    for (WGPUTextureFormat format : kRegularTextureFormats) {
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

std::vector<Value> textureDimensionValues(bool include1D) {
    std::vector<Value> values;
    if (include1D) {
        values.push_back(static_cast<int64_t>(WGPUTextureDimension_1D));
    }
    values.push_back(static_cast<int64_t>(WGPUTextureDimension_2D));
    values.push_back(static_cast<int64_t>(WGPUTextureDimension_3D));
    return values;
}

std::vector<Value> indexValues(uint32_t count) {
    std::vector<Value> values;
    values.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        values.push_back(static_cast<int64_t>(i));
    }
    return values;
}

WGPUExtent3D sizeForDimension(WGPUExtent3D size, WGPUTextureDimension dimension) {
    if (dimension == WGPUTextureDimension_1D) {
        size.height = 1;
        size.depthOrArrayLayers = 1;
    }
    if (dimension == WGPUTextureDimension_2D) {
        size.depthOrArrayLayers = std::max(1u, size.depthOrArrayLayers);
    }
    return size;
}

uint32_t blockCount(uint32_t texels, uint32_t blockSize) {
    return (texels + blockSize - 1) / blockSize;
}

WGPUTexelCopyBufferLayout toWgpuLayout(TexelCopyBufferLayout layout) {
    WGPUTexelCopyBufferLayout out = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    out.offset = layout.offset;
    out.bytesPerRow = layout.bytesPerRow;
    out.rowsPerImage = layout.rowsPerImage;
    return out;
}

WGPUTexture createTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    WGPUExtent3D size,
    uint32_t mipLevelCount) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = size;
    desc.mipLevelCount = mipLevelCount;
    desc.sampleCount = 1;
    desc.dimension = dimension;
    desc.format = format;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    return t.createTextureTracked(desc);
}

WGPUTexture createDepthStencilTexture(
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

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void copyTextureToTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture src,
    WGPUTexture dst,
    WGPUExtent3D copySize,
    uint32_t srcMipLevel,
    WGPUOrigin3D srcOrigin,
    uint32_t dstMipLevel,
    WGPUOrigin3D dstOrigin) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = src;
    source.mipLevel = srcMipLevel;
    source.origin = srcOrigin;
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = dst;
    destination.mipLevel = dstMipLevel;
    destination.origin = dstOrigin;
    destination.aspect = WGPUTextureAspect_All;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &copySize);
    submit(t, encoder);
}

void copyTextureToBuffer(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUBuffer buffer,
    TexelCopyBufferLayout layout,
    WGPUExtent3D copySize,
    uint32_t mipLevel) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.mipLevel = mipLevel;
    source.origin = WGPUOrigin3D{0, 0, 0};
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

void writeTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    WGPUTextureDimension dimension,
    WGPUExtent3D size,
    uint32_t mipLevel,
    const std::vector<uint8_t>& data) {
    const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
    const uint32_t blocksPerRow = blockCount(size.width, block.blockWidth);
    const uint32_t blockRowsPerImage = blockCount(size.height, block.blockHeight);
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = blocksPerRow * block.bytesPerBlock;
    layout.rowsPerImage = dimension == WGPUTextureDimension_1D ? WGPU_COPY_STRIDE_UNDEFINED : blockRowsPerImage;
    t.queueWriteTexture(texture, size, layout, data.data(), data.size(), mipLevel, WGPUOrigin3D{0, 0, 0});
}

void writeTextureAspect(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUExtent3D size,
    uint32_t mipLevel,
    WGPUOrigin3D origin,
    WGPUTextureAspect aspect,
    uint32_t bytesPerRow,
    uint32_t rowsPerImage,
    const std::vector<uint8_t>& data) {
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset = 0;
    layout.bytesPerRow = bytesPerRow;
    layout.rowsPerImage = rowsPerImage;

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = texture;
    destination.mipLevel = mipLevel;
    destination.origin = origin;
    destination.aspect = aspect;
    wgpuQueueWriteTexture(t.queue(), &destination, data.data(), data.size(), &layout, &size);
}

struct AppliedCopy {
    WGPUOrigin3D srcOrigin;
    WGPUOrigin3D dstOrigin;
    WGPUExtent3D size;
};

AppliedCopy computeAppliedCopy(
    WGPUTextureFormat format,
    WGPUExtent3D srcSize,
    WGPUExtent3D dstSize,
    const CopyBoxOffset& offsets) {
    const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
    const int32_t srcOffsetX = offsets.srcOffset.x * static_cast<int32_t>(block.blockWidth);
    const int32_t srcOffsetY = offsets.srcOffset.y * static_cast<int32_t>(block.blockHeight);
    const int32_t dstOffsetX = offsets.dstOffset.x * static_cast<int32_t>(block.blockWidth);
    const int32_t dstOffsetY = offsets.dstOffset.y * static_cast<int32_t>(block.blockHeight);
    const int32_t srcOffsetZ = offsets.srcOffset.z;
    const int32_t dstOffsetZ = offsets.dstOffset.z;

    const int32_t maxWidth = std::min(
        static_cast<int32_t>(srcSize.width) - srcOffsetX,
        static_cast<int32_t>(dstSize.width) - dstOffsetX);
    const int32_t maxHeight = std::min(
        static_cast<int32_t>(srcSize.height) - srcOffsetY,
        static_cast<int32_t>(dstSize.height) - dstOffsetY);
    const int32_t maxDepth = std::min(
        static_cast<int32_t>(srcSize.depthOrArrayLayers) - srcOffsetZ,
        static_cast<int32_t>(dstSize.depthOrArrayLayers) - dstOffsetZ);

    const int32_t copyWidth = std::max(0, maxWidth + offsets.copyExtent.width * static_cast<int32_t>(block.blockWidth));
    const int32_t copyHeight = std::max(0, maxHeight + offsets.copyExtent.height * static_cast<int32_t>(block.blockHeight));
    const int32_t copyDepth = std::max(0, maxDepth + offsets.copyExtent.depthOrArrayLayers);

    WGPUOrigin3D srcOrigin{
        static_cast<uint32_t>(std::max(0, srcOffsetX)),
        static_cast<uint32_t>(std::max(0, srcOffsetY)),
        static_cast<uint32_t>(std::max(0, srcOffsetZ)),
    };
    WGPUOrigin3D dstOrigin{
        static_cast<uint32_t>(std::max(0, dstOffsetX)),
        static_cast<uint32_t>(std::max(0, dstOffsetY)),
        static_cast<uint32_t>(std::max(0, dstOffsetZ)),
    };
    if (copyWidth == 0) {
        srcOrigin.x = std::min(srcOrigin.x, srcSize.width);
        dstOrigin.x = std::min(dstOrigin.x, dstSize.width);
    }
    if (copyHeight == 0) {
        srcOrigin.y = std::min(srcOrigin.y, srcSize.height);
        dstOrigin.y = std::min(dstOrigin.y, dstSize.height);
    }
    if (copyDepth == 0) {
        srcOrigin.z = std::min(srcOrigin.z, srcSize.depthOrArrayLayers);
        dstOrigin.z = std::min(dstOrigin.z, dstSize.depthOrArrayLayers);
    }

    return AppliedCopy{
        srcOrigin,
        dstOrigin,
        WGPUExtent3D{static_cast<uint32_t>(copyWidth), static_cast<uint32_t>(copyHeight), static_cast<uint32_t>(copyDepth)},
    };
}

uint64_t linearDataSize(WGPUTextureFormat format, WGPUExtent3D size, uint32_t bytesPerRow) {
    const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
    const uint32_t blockRows = blockCount(size.height, block.blockHeight);
    const uint32_t depth = size.depthOrArrayLayers;
    const uint32_t rowBytes = blockCount(size.width, block.blockWidth) * block.bytesPerBlock;
    if (depth == 0 || blockRows == 0 || rowBytes == 0) {
        return 0;
    }
    const uint64_t bytesPerSlice = static_cast<uint64_t>(bytesPerRow) * blockRows;
    return alignTo(bytesPerSlice * (depth - 1)
        + static_cast<uint64_t>(bytesPerRow) * (blockRows - 1)
        + rowBytes, kBufferCopyAlignment);
}

void overlayExpectedCopy(
    std::vector<uint8_t>& expected,
    const std::vector<uint8_t>& srcData,
    WGPUTextureFormat format,
    WGPUExtent3D srcSize,
    WGPUExtent3D dstSize,
    const AppliedCopy& copy,
    uint32_t srcBytesPerRow,
    uint32_t srcRowsPerImage,
    uint32_t dstBytesPerRow,
    uint32_t dstRowsPerImage) {
    const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
    const uint32_t widthBlocks = blockCount(copy.size.width, block.blockWidth);
    const uint32_t heightBlocks = blockCount(copy.size.height, block.blockHeight);
    const uint32_t depth = copy.size.depthOrArrayLayers;
    const uint32_t bytesInRow = widthBlocks * block.bytesPerBlock;
    const uint32_t srcXBlocks = copy.srcOrigin.x / block.blockWidth;
    const uint32_t srcYBlocks = copy.srcOrigin.y / block.blockHeight;
    const uint32_t dstXBlocks = copy.dstOrigin.x / block.blockWidth;
    const uint32_t dstYBlocks = copy.dstOrigin.y / block.blockHeight;
    (void)srcSize;
    (void)dstSize;

    for (uint32_t z = 0; z < depth; ++z) {
        for (uint32_t y = 0; y < heightBlocks; ++y) {
            const uint64_t srcOffset =
                static_cast<uint64_t>(copy.srcOrigin.z + z) * srcRowsPerImage * srcBytesPerRow
                + static_cast<uint64_t>(srcYBlocks + y) * srcBytesPerRow
                + static_cast<uint64_t>(srcXBlocks) * block.bytesPerBlock;
            const uint64_t dstOffset =
                static_cast<uint64_t>(copy.dstOrigin.z + z) * dstRowsPerImage * dstBytesPerRow
                + static_cast<uint64_t>(dstYBlocks + y) * dstBytesPerRow
                + static_cast<uint64_t>(dstXBlocks) * block.bytesPerBlock;
            if (bytesInRow > 0 && srcOffset + bytesInRow <= srcData.size() && dstOffset + bytesInRow <= expected.size()) {
                std::memcpy(expected.data() + dstOffset, srcData.data() + srcOffset, bytesInRow);
            }
        }
    }
}

std::vector<uint8_t> getInitialStencilData(WGPUExtent3D copySize) {
    return generateData(static_cast<size_t>(copySize.width) * copySize.height * copySize.depthOrArrayLayers);
}

void initializeStencilAspect(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    uint32_t mipLevel,
    uint32_t baseArrayLayer,
    WGPUExtent3D copySize,
    const std::vector<uint8_t>& stencilData) {
    writeTextureAspect(
        t,
        texture,
        copySize,
        mipLevel,
        WGPUOrigin3D{0, 0, baseArrayLayer},
        WGPUTextureAspect_StencilOnly,
        copySize.width,
        copySize.height,
        stencilData);
}

void verifyStencilAspect(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const std::vector<uint8_t>& stencilData,
    uint32_t mipLevel,
    uint32_t baseArrayLayer,
    WGPUExtent3D copySize) {
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(copySize.width, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * copySize.height * (copySize.depthOrArrayLayers - 1)
            + static_cast<uint64_t>(bytesPerRow) * (copySize.height - 1)
            + copySize.width,
        kBufferCopyAlignment);
    std::vector<uint8_t> expected(static_cast<size_t>(byteLength), 0);
    for (uint32_t z = 0; z < copySize.depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < copySize.height; ++y) {
            const uint64_t srcOffset = static_cast<uint64_t>(z) * copySize.height * copySize.width
                + static_cast<uint64_t>(y) * copySize.width;
            const uint64_t dstOffset = static_cast<uint64_t>(z) * copySize.height * bytesPerRow
                + static_cast<uint64_t>(y) * bytesPerRow;
            std::memcpy(expected.data() + dstOffset, stencilData.data() + srcOffset, copySize.width);
        }
    }

    WGPUBuffer buffer = t.makeBufferWithContents(expected.data(), expected.size(), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    copyTextureToBufferAspect(
        t,
        texture,
        buffer,
        TexelCopyBufferLayout{0, bytesPerRow, copySize.height},
        copySize,
        mipLevel,
        WGPUOrigin3D{0, 0, baseArrayLayer},
        WGPUTextureAspect_StencilOnly);
    t.expectGPUBufferValuesEqual(buffer, expected.data(), expected.size());
}

WGPUTextureView createLayerView(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mipLevel,
    uint32_t arrayLayer) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = format;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.baseMipLevel = mipLevel;
    desc.mipLevelCount = 1;
    desc.baseArrayLayer = arrayLayer;
    desc.arrayLayerCount = 1;
    desc.aspect = WGPUTextureAspect_All;
    return t.createViewTracked(texture, desc);
}

WGPUBindGroupLayout getDepthCopyBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Vertex;
    entry.buffer.type = WGPUBufferBindingType_Uniform;
    entry.buffer.hasDynamicOffset = WGPU_TRUE;
    entry.buffer.minBindingSize = 4;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBuffer createLayerUniformBuffer(AllFeaturesMaxLimitsGpuTest& t, uint32_t layerCount) {
    std::vector<uint8_t> data(static_cast<size_t>(layerCount) * kDynamicUniformStride, 0);
    for (uint32_t layer = 0; layer < layerCount; ++layer) {
        const float value = static_cast<float>(layer);
        std::memcpy(data.data() + static_cast<size_t>(layer) * kDynamicUniformStride, &value, sizeof(value));
    }
    return t.makeBufferWithContents(data.data(), data.size(), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
}

WGPUBindGroup createLayerUniformBindGroup(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout layout, WGPUBuffer buffer) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer;
    entry.offset = 0;
    entry.size = 4;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

WGPURenderPipeline createDepthCopyPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat depthStencilFormat,
    WGPUCompareFunction depthCompare,
    WGPUOptionalBool depthWriteEnabled,
    bool includeFragment,
    WGPUPipelineLayout layout) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kDepthVertexShader);
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = depthStencilFormat;
    depthStencil.depthWriteEnabled = depthWriteEnabled;
    depthStencil.depthCompare = depthCompare;

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUShaderModule fragmentModule = nullptr;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    if (includeFragment) {
        fragmentModule = t.createShaderModuleTracked(kGreenFragmentShader);
        fragment.module = fragmentModule;
        fragment.entryPoint = stringView("main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
    }

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.depthStencil = &depthStencil;
    desc.fragment = includeFragment ? &fragment : nullptr;
    return t.createRenderPipelineTracked(desc);
}

void encodeDepthDrawPass(
    WGPUCommandEncoder encoder,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bindGroup,
    WGPUTextureView depthStencilView,
    WGPUTextureFormat format,
    WGPURenderPassColorAttachment* colorAttachment,
    bool clearDepth,
    uint32_t layer) {
    WGPURenderPassDepthStencilAttachment depthStencilAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depthStencilAttachment.view = depthStencilView;
    depthStencilAttachment.depthLoadOp = clearDepth ? WGPULoadOp_Clear : WGPULoadOp_Load;
    depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthStencilAttachment.depthClearValue = 0.0f;
    if (isStencilTextureFormat(format)) {
        depthStencilAttachment.stencilLoadOp = WGPULoadOp_Load;
        depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Store;
    }

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = colorAttachment == nullptr ? 0 : 1;
    passDesc.colorAttachments = colorAttachment;
    passDesc.depthStencilAttachment = &depthStencilAttachment;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    const uint32_t dynamicOffset = layer * kDynamicUniformStride;
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 1, &dynamicOffset);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
}

void initializeDepthAspect(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mipLevel,
    uint32_t baseArrayLayer,
    WGPUExtent3D copySize) {
    WGPUBindGroupLayout bindGroupLayout = getDepthCopyBindGroupLayout(t);
    WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
    WGPURenderPipeline pipeline = createDepthCopyPipeline(
        t, format, WGPUCompareFunction_Always, WGPUOptionalBool_True, false, pipelineLayout);
    WGPUBuffer uniformBuffer = createLayerUniformBuffer(t, copySize.depthOrArrayLayers);
    WGPUBindGroup bindGroup = createLayerUniformBindGroup(t, bindGroupLayout, uniformBuffer);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    for (uint32_t layer = 0; layer < copySize.depthOrArrayLayers; ++layer) {
        WGPUTextureView view = createLayerView(t, texture, format, mipLevel, baseArrayLayer + layer);
        encodeDepthDrawPass(encoder, pipeline, bindGroup, view, format, nullptr, true, layer);
    }
    submit(t, encoder);
}

WGPUTexture createColorOutputTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUExtent3D size) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = size;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

void expectSingleColor(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, WGPUExtent3D size) {
    const uint32_t rowBytes = size.width * 4;
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(rowBytes, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * size.height * (size.depthOrArrayLayers - 1)
            + static_cast<uint64_t>(bytesPerRow) * (size.height - 1)
            + rowBytes,
        kBufferCopyAlignment);
    std::vector<uint8_t> expected(static_cast<size_t>(byteLength), 0);
    for (uint32_t z = 0; z < size.depthOrArrayLayers; ++z) {
        for (uint32_t y = 0; y < size.height; ++y) {
            const uint64_t rowOffset = static_cast<uint64_t>(z) * size.height * bytesPerRow
                + static_cast<uint64_t>(y) * bytesPerRow;
            for (uint32_t x = 0; x < size.width; ++x) {
                expected[rowOffset + x * 4 + 0] = 0;
                expected[rowOffset + x * 4 + 1] = 255;
                expected[rowOffset + x * 4 + 2] = 0;
                expected[rowOffset + x * 4 + 3] = 255;
            }
        }
    }

    WGPUBuffer buffer = t.makeBufferWithContents(expected.data(), expected.size(), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    copyTextureToBufferAspect(
        t,
        texture,
        buffer,
        TexelCopyBufferLayout{0, bytesPerRow, size.height},
        size,
        0,
        WGPUOrigin3D{0, 0, 0},
        WGPUTextureAspect_All);
    t.expectGPUBufferValuesEqual(buffer, expected.data(), expected.size());
}

WGPUTextureView createColorLayerView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, uint32_t layer) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.dimension = WGPUTextureViewDimension_2D;
    desc.baseMipLevel = 0;
    desc.mipLevelCount = 1;
    desc.baseArrayLayer = layer;
    desc.arrayLayerCount = 1;
    desc.aspect = WGPUTextureAspect_All;
    return t.createViewTracked(texture, desc);
}

void verifyDepthAspect(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    uint32_t mipLevel,
    uint32_t baseArrayLayer,
    WGPUExtent3D copySize) {
    WGPUBindGroupLayout bindGroupLayout = getDepthCopyBindGroupLayout(t);
    WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
    WGPURenderPipeline pipeline = createDepthCopyPipeline(
        t, format, WGPUCompareFunction_Equal, WGPUOptionalBool_False, true, pipelineLayout);
    WGPUBuffer uniformBuffer = createLayerUniformBuffer(t, copySize.depthOrArrayLayers);
    WGPUBindGroup bindGroup = createLayerUniformBindGroup(t, bindGroupLayout, uniformBuffer);
    WGPUTexture output = createColorOutputTexture(t, copySize);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    for (uint32_t layer = 0; layer < copySize.depthOrArrayLayers; ++layer) {
        WGPUTextureView colorView = createColorLayerView(t, output, layer);
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = colorView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};
        WGPUTextureView depthView = createLayerView(t, texture, format, mipLevel, baseArrayLayer + layer);
        encodeDepthDrawPass(encoder, pipeline, bindGroup, depthView, format, &colorAttachment, false, layer);
    }
    submit(t, encoder);
    expectSingleColor(t, output, copySize);
}

ParamsBuilder depthStencilParams(ParamsBuilder u) {
    return u.combine("format", depthStencilFormatValues())
        .beginSubcases()
        .combine("srcTextureSizeIndex", indexValues(static_cast<uint32_t>(kDepthStencilTextureSizes.size())))
        .combine("srcCopyLevel", {0, 2})
        .combine("dstCopyLevel", {0, 2})
        .combine("srcCopyBaseArrayLayer", {0, 1})
        .combine("dstCopyBaseArrayLayer", {0, 1})
        .filter([](const ParamRecord& params) {
            const uint32_t sizeIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "srcTextureSizeIndex")));
            const uint32_t srcBase = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "srcCopyBaseArrayLayer")));
            const uint32_t dstBase = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "dstCopyBaseArrayLayer")));
            const WGPUExtent3D size = kDepthStencilTextureSizes[sizeIndex];
            return size.depthOrArrayLayers > srcBase && size.depthOrArrayLayers > dstBase;
        });
}

void runDepthStencil(AllFeaturesMaxLimitsGpuTest& t) {
    const auto format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
    t.skipIfTextureFormatNotSupported(format);

    const uint32_t sizeIndex = static_cast<uint32_t>(t.param<int64_t>("srcTextureSizeIndex"));
    const uint32_t srcCopyLevel = static_cast<uint32_t>(t.param<int64_t>("srcCopyLevel"));
    const uint32_t dstCopyLevel = static_cast<uint32_t>(t.param<int64_t>("dstCopyLevel"));
    const uint32_t srcCopyBaseArrayLayer = static_cast<uint32_t>(t.param<int64_t>("srcCopyBaseArrayLayer"));
    const uint32_t dstCopyBaseArrayLayer = static_cast<uint32_t>(t.param<int64_t>("dstCopyBaseArrayLayer"));
    const WGPUExtent3D srcTextureSize = kDepthStencilTextureSizes[sizeIndex];
    WGPUExtent3D copySize{
        std::max(1u, srcTextureSize.width >> srcCopyLevel),
        std::max(1u, srcTextureSize.height >> srcCopyLevel),
        srcTextureSize.depthOrArrayLayers - std::max(srcCopyBaseArrayLayer, dstCopyBaseArrayLayer),
    };
    WGPUExtent3D dstTextureSize{
        copySize.width << dstCopyLevel,
        copySize.height << dstCopyLevel,
        srcTextureSize.depthOrArrayLayers,
    };

    WGPUTexture src = createDepthStencilTexture(t, format, srcTextureSize, srcCopyLevel + 1);
    WGPUTexture dst = createDepthStencilTexture(t, format, dstTextureSize, dstCopyLevel + 1);
    std::vector<uint8_t> stencilData;
    if (isStencilTextureFormat(format)) {
        stencilData = getInitialStencilData(copySize);
        initializeStencilAspect(t, src, srcCopyLevel, srcCopyBaseArrayLayer, copySize, stencilData);
    }
    if (isDepthTextureFormat(format)) {
        initializeDepthAspect(t, src, format, srcCopyLevel, srcCopyBaseArrayLayer, copySize);
    }

    copyTextureToTexture(
        t,
        src,
        dst,
        copySize,
        srcCopyLevel,
        WGPUOrigin3D{0, 0, srcCopyBaseArrayLayer},
        dstCopyLevel,
        WGPUOrigin3D{0, 0, dstCopyBaseArrayLayer});

    if (isStencilTextureFormat(format)) {
        verifyStencilAspect(t, dst, stencilData, dstCopyLevel, dstCopyBaseArrayLayer, copySize);
    }
    if (isDepthTextureFormat(format)) {
        verifyDepthAspect(t, dst, format, dstCopyLevel, dstCopyBaseArrayLayer, copySize);
    }
}

void doCopyTextureToTextureTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat srcFormat,
    WGPUTextureFormat dstFormat,
    WGPUTextureDimension dimension,
    WGPUExtent3D srcTextureSize,
    WGPUExtent3D dstTextureSize,
    const CopyBoxOffset& copyBoxOffsets,
    uint32_t srcCopyLevel,
    uint32_t dstCopyLevel) {
    t.skipIfTextureFormatNotSupported(srcFormat);
    t.skipIfTextureFormatNotSupported(dstFormat);
    t.skipIfTextureFormatAndDimensionNotCompatible(srcFormat, dimension);
    t.skipIfTextureFormatAndDimensionNotCompatible(dstFormat, dimension);
    if (!textureFormatsAreViewCompatible(srcFormat, dstFormat)) {
        t.skip("source and destination formats are not view-compatible");
    }

    const uint32_t mipLevelCount = dimension == WGPUTextureDimension_1D ? 1 : 4;
    WGPUTexture srcTexture = createTexture(t, srcFormat, dimension, srcTextureSize, mipLevelCount);
    WGPUTexture dstTexture = createTexture(t, dstFormat, dimension, dstTextureSize, mipLevelCount);

    const WGPUExtent3D srcSizeAtLevel = physicalMipSize(srcTextureSize, dimension, srcCopyLevel);
    const WGPUExtent3D dstSizeAtLevel = physicalMipSize(dstTextureSize, dimension, dstCopyLevel);
    const TextureBlockInfo srcBlock = getBlockInfoForTextureFormat(srcFormat);
    const uint32_t srcBlocksPerRow = blockCount(srcSizeAtLevel.width, srcBlock.blockWidth);
    const uint32_t srcBlockRowsPerImage = blockCount(srcSizeAtLevel.height, srcBlock.blockHeight);
    const uint32_t srcBytesPerRow = srcBlocksPerRow * srcBlock.bytesPerBlock;
    const uint64_t srcByteSize = linearDataSize(srcFormat, srcSizeAtLevel, srcBytesPerRow);
    const std::vector<uint8_t> initialSrcData = generateData(static_cast<size_t>(srcByteSize));
    writeTexture(t, srcTexture, srcFormat, dimension, srcSizeAtLevel, srcCopyLevel, initialSrcData);

    const AppliedCopy applied = computeAppliedCopy(srcFormat, srcSizeAtLevel, dstSizeAtLevel, copyBoxOffsets);
    copyTextureToTexture(t, srcTexture, dstTexture, applied.size, srcCopyLevel, applied.srcOrigin, dstCopyLevel, applied.dstOrigin);

    const TextureBlockInfo dstBlock = getBlockInfoForTextureFormat(dstFormat);
    const uint32_t dstBlocksPerRow = blockCount(dstSizeAtLevel.width, dstBlock.blockWidth);
    const uint32_t dstBlockRowsPerImage = blockCount(dstSizeAtLevel.height, dstBlock.blockHeight);
    const uint32_t bytesPerDstAlignedBlockRow = static_cast<uint32_t>(alignTo(
        static_cast<uint64_t>(dstBlocksPerRow) * dstBlock.bytesPerBlock, kBytesPerRowAlignment));
    const uint64_t dstBufferSize = linearDataSize(dstFormat, dstSizeAtLevel, bytesPerDstAlignedBlockRow);
    TexelCopyBufferLayout dstLayout{0, bytesPerDstAlignedBlockRow, dstBlockRowsPerImage};

    std::vector<uint8_t> expected(static_cast<size_t>(dstBufferSize), 0);
    overlayExpectedCopy(
        expected,
        initialSrcData,
        srcFormat,
        srcSizeAtLevel,
        dstSizeAtLevel,
        applied,
        srcBytesPerRow,
        srcBlockRowsPerImage,
        bytesPerDstAlignedBlockRow,
        dstBlockRowsPerImage);

    WGPUBuffer dstBuffer = t.makeBufferWithContents(expected.data(), expected.size(), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    copyTextureToBuffer(t, dstTexture, dstBuffer, dstLayout, dstSizeAtLevel, dstCopyLevel);
    t.expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
        expected.data(), expected.size(), dstBuffer, dstFormat, dstSizeAtLevel, dstLayout);
}

ParamsBuilder compatibleFormatParams(ParamsBuilder u) {
    return u.combine("srcFormat", regularTextureFormatValues())
        .combine("dstFormat", regularTextureFormatValues())
        .filter([](const ParamRecord& params) {
            const auto srcFormat = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "srcFormat")));
            const auto dstFormat = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "dstFormat")));
            return textureFormatsAreViewCompatible(srcFormat, dstFormat);
        });
}

ParamsBuilder nonArrayParams(ParamsBuilder u) {
    return compatibleFormatParams(u)
        .combine("dimension", textureDimensionValues(true))
        .filter([](const ParamRecord& params) {
            const auto srcFormat = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "srcFormat")));
            const auto dstFormat = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "dstFormat")));
            const auto dimension = static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "dimension")));
            return textureFormatAndDimensionPossiblyCompatible(dimension, srcFormat)
                && textureFormatAndDimensionPossiblyCompatible(dimension, dstFormat);
        })
        .beginSubcases()
        .combine("sizePairIndex", indexValues(static_cast<uint32_t>(kNonArraySizePairs.size())))
        .combine("copyBoxOffsetsIndex", indexValues(static_cast<uint32_t>(kCopyBoxOffsetsForWholeDepth.size())))
        .filter([](const ParamRecord& params) {
            const auto dimension = static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "dimension")));
            const uint32_t offsetIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "copyBoxOffsetsIndex")));
            const CopyBoxOffset& offsets = kCopyBoxOffsetsForWholeDepth[offsetIndex];
            return dimension != WGPUTextureDimension_1D
                || (offsets.copyExtent.height == 0 && offsets.srcOffset.y == 0 && offsets.dstOffset.y == 0);
        })
        .combine("srcCopyLevel", {0, 3})
        .combine("dstCopyLevel", {0, 3})
        .filter([](const ParamRecord& params) {
            const auto dimension = static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "dimension")));
            const uint32_t srcCopyLevel = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "srcCopyLevel")));
            const uint32_t dstCopyLevel = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "dstCopyLevel")));
            return dimension != WGPUTextureDimension_1D || (srcCopyLevel == 0 && dstCopyLevel == 0);
        });
}

ParamsBuilder arrayParams(ParamsBuilder u) {
    return compatibleFormatParams(u)
        .combine("dimension", textureDimensionValues(false))
        .filter([](const ParamRecord& params) {
            const auto srcFormat = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "srcFormat")));
            const auto dstFormat = static_cast<WGPUTextureFormat>(valueAs<int64_t>(*findParam(params, "dstFormat")));
            const auto dimension = static_cast<WGPUTextureDimension>(valueAs<int64_t>(*findParam(params, "dimension")));
            return textureFormatAndDimensionPossiblyCompatible(dimension, srcFormat)
                && textureFormatAndDimensionPossiblyCompatible(dimension, dstFormat);
        })
        .beginSubcases()
        .combine("sizePairIndex", indexValues(static_cast<uint32_t>(kArraySizePairs.size())))
        .combine("copyBoxOffsetsIndex", indexValues(static_cast<uint32_t>(kCopyBoxOffsetsFor2DArrayTextures.size())))
        .combine("srcCopyLevel", {0, 3})
        .combine("dstCopyLevel", {0, 3});
}

ParamsBuilder zeroSizedParams(ParamsBuilder u) {
    return u.combine("srcFormat", {static_cast<int64_t>(WGPUTextureFormat_RGBA8Unorm)})
        .combine("dstFormat", {static_cast<int64_t>(WGPUTextureFormat_RGBA8Unorm)})
        .beginSubcases()
        .combine("zeroSizedConfigIndex", indexValues(static_cast<uint32_t>(kZeroSizedConfigs.size())))
        .combine("copyBoxOffsetsIndex", indexValues(static_cast<uint32_t>(kZeroSizedCopyBoxOffsets.size())))
        .filter([](const ParamRecord& params) {
            const uint32_t configIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "zeroSizedConfigIndex")));
            const uint32_t offsetIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "copyBoxOffsetsIndex")));
            const WGPUTextureDimension dimension = kZeroSizedConfigs[configIndex].dimension;
            const CopyBoxOffset& offsets = kZeroSizedCopyBoxOffsets[offsetIndex];
            return dimension != WGPUTextureDimension_1D
                || (offsets.copyExtent.height == 0 && offsets.srcOffset.y == 0 && offsets.dstOffset.y == 0);
        })
        .combine("srcCopyLevel", {0, 3})
        .combine("dstCopyLevel", {0, 3})
        .filter([](const ParamRecord& params) {
            const uint32_t configIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "zeroSizedConfigIndex")));
            const WGPUTextureDimension dimension = kZeroSizedConfigs[configIndex].dimension;
            const uint32_t srcCopyLevel = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "srcCopyLevel")));
            const uint32_t dstCopyLevel = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "dstCopyLevel")));
            return dimension != WGPUTextureDimension_1D || (srcCopyLevel == 0 && dstCopyLevel == 0);
        });
}

void runNonArray(AllFeaturesMaxLimitsGpuTest& t) {
    const auto srcFormat = static_cast<WGPUTextureFormat>(t.param<int64_t>("srcFormat"));
    const auto dstFormat = static_cast<WGPUTextureFormat>(t.param<int64_t>("dstFormat"));
    const auto dimension = static_cast<WGPUTextureDimension>(t.param<int64_t>("dimension"));
    const uint32_t sizePairIndex = static_cast<uint32_t>(t.param<int64_t>("sizePairIndex"));
    const uint32_t offsetIndex = static_cast<uint32_t>(t.param<int64_t>("copyBoxOffsetsIndex"));
    const uint32_t srcCopyLevel = static_cast<uint32_t>(t.param<int64_t>("srcCopyLevel"));
    const uint32_t dstCopyLevel = static_cast<uint32_t>(t.param<int64_t>("dstCopyLevel"));
    const SizePair& pair = kNonArraySizePairs[sizePairIndex];
    doCopyTextureToTextureTest(
        t,
        srcFormat,
        dstFormat,
        dimension,
        sizeForDimension(pair.srcTextureSize, dimension),
        sizeForDimension(pair.dstTextureSize, dimension),
        kCopyBoxOffsetsForWholeDepth[offsetIndex],
        srcCopyLevel,
        dstCopyLevel);
}

void runArray(AllFeaturesMaxLimitsGpuTest& t) {
    const auto srcFormat = static_cast<WGPUTextureFormat>(t.param<int64_t>("srcFormat"));
    const auto dstFormat = static_cast<WGPUTextureFormat>(t.param<int64_t>("dstFormat"));
    const auto dimension = static_cast<WGPUTextureDimension>(t.param<int64_t>("dimension"));
    const uint32_t sizePairIndex = static_cast<uint32_t>(t.param<int64_t>("sizePairIndex"));
    const uint32_t offsetIndex = static_cast<uint32_t>(t.param<int64_t>("copyBoxOffsetsIndex"));
    const uint32_t srcCopyLevel = static_cast<uint32_t>(t.param<int64_t>("srcCopyLevel"));
    const uint32_t dstCopyLevel = static_cast<uint32_t>(t.param<int64_t>("dstCopyLevel"));
    const SizePair& pair = kArraySizePairs[sizePairIndex];
    doCopyTextureToTextureTest(
        t,
        srcFormat,
        dstFormat,
        dimension,
        pair.srcTextureSize,
        pair.dstTextureSize,
        kCopyBoxOffsetsFor2DArrayTextures[offsetIndex],
        srcCopyLevel,
        dstCopyLevel);
}

void runZeroSized(AllFeaturesMaxLimitsGpuTest& t) {
    const uint32_t configIndex = static_cast<uint32_t>(t.param<int64_t>("zeroSizedConfigIndex"));
    const uint32_t offsetIndex = static_cast<uint32_t>(t.param<int64_t>("copyBoxOffsetsIndex"));
    const uint32_t srcCopyLevel = static_cast<uint32_t>(t.param<int64_t>("srcCopyLevel"));
    const uint32_t dstCopyLevel = static_cast<uint32_t>(t.param<int64_t>("dstCopyLevel"));
    const ZeroSizedConfig& config = kZeroSizedConfigs[configIndex];
    doCopyTextureToTextureTest(
        t,
        WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureFormat_RGBA8Unorm,
        config.dimension,
        config.textureSize,
        config.textureSize,
        kZeroSizedCopyBoxOffsets[offsetIndex],
        srcCopyLevel,
        dstCopyLevel);
}

} // namespace

CTS_TEST(g, "color_textures,non_compressed,non_array")
    .params(nonArrayParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runNonArray(t); });

CTS_TEST(g, "color_textures,non_compressed,array")
    .params(arrayParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runArray(t); });

CTS_TEST(g, "zero_sized")
    .params(zeroSizedParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runZeroSized(t); });

CTS_TEST(g, "color_textures,compressed,non_array")
    .unimplemented("compressed texture copyTextureToTexture tests are deferred");

CTS_TEST(g, "color_textures,compressed,array")
    .unimplemented("compressed texture copyTextureToTexture tests are deferred");

CTS_TEST(g, "copy_depth_stencil")
    .params(depthStencilParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runDepthStencil(t); });

CTS_TEST(g, "copy_multisampled_color")
    .unimplemented("multisampled color copyTextureToTexture tests are deferred");

CTS_TEST(g, "copy_multisampled_depth")
    .unimplemented("multisampled depth copyTextureToTexture tests are deferred");

CTS_TEST(g, "copy_multisampled_stencil")
    .unimplemented("multisampled stencil copyTextureToTexture tests are deferred");
