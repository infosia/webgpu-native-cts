// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/copyTextureToTexture.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa

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
#include "webgpu/util/enum_strings.h"
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

struct SizeInBlocksPair {
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
};

constexpr std::array<SizeInBlocksPair, 6> kCompressedNonArraySizesInBlocks = {{
    // The heights and widths in blocks are all power of 2
    {16, 8, 16, 8},
    // The virtual width of the source texture at mipmap level 2 (15) is not a multiple of 4 blocks
    {15, 8, 16, 8},
    // The virtual width of the destination texture at mipmap level 2 (15) is not a multiple
    // of 4 blocks
    {16, 8, 15, 8},
    // The virtual height of the source texture at mipmap level 2 (13) is not a multiple of 4 blocks
    {16, 13, 16, 8},
    // The virtual height of the destination texture at mipmap level 2 (13) is not a
    // multiple of 4 blocks
    {16, 8, 16, 13},
    // None of the widths or heights in blocks are power of 2
    {15, 13, 15, 13},
}};

constexpr std::array<SizeInBlocksPair, 2> kCompressedArraySizesInBlocks = {{
    // The heights and widths in blocks are all power of 2
    {2, 2, 2, 2},
    // None of the widths or heights in blocks are power of 2
    {15, 13, 15, 13},
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
    return formatIdentifierValues(kRegularTextureFormats);
}

std::vector<Value> depthStencilFormatValues() {
    return formatIdentifierValues(kDepthStencilFormats);
}

std::vector<Value> textureDimensionValues(bool include1D) {
    std::vector<Value> values;
    if (include1D) {
        values.push_back(std::string(textureDimensionIdentifier(WGPUTextureDimension_1D)));
    }
    values.push_back(std::string(textureDimensionIdentifier(WGPUTextureDimension_2D)));
    values.push_back(std::string(textureDimensionIdentifier(WGPUTextureDimension_3D)));
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
    const int32_t blockWidth = static_cast<int32_t>(block.blockWidth);
    const int32_t blockHeight = static_cast<int32_t>(block.blockHeight);
    const int32_t minWidth = static_cast<int32_t>(std::min(srcSize.width, dstSize.width));
    const int32_t minHeight = static_cast<int32_t>(std::min(srcSize.height, dstSize.height));
    const int32_t minDepth = static_cast<int32_t>(std::min(srcSize.depthOrArrayLayers, dstSize.depthOrArrayLayers));

    const int32_t srcX = std::min(offsets.srcOffset.x * blockWidth, minWidth);
    const int32_t srcY = std::min(offsets.srcOffset.y * blockHeight, minHeight);
    const int32_t srcZ = std::min(offsets.srcOffset.z, minDepth);
    const int32_t dstX = std::min(offsets.dstOffset.x * blockWidth, minWidth);
    const int32_t dstY = std::min(offsets.dstOffset.y * blockHeight, minHeight);
    const int32_t dstZ = std::min(offsets.dstOffset.z, minDepth);

    const int32_t copyWidth = std::max(minWidth + offsets.copyExtent.width * blockWidth - std::max(srcX, dstX), 0);
    const int32_t copyHeight = std::max(minHeight + offsets.copyExtent.height * blockHeight - std::max(srcY, dstY), 0);
    const int32_t copyDepth = std::max(minDepth + offsets.copyExtent.depthOrArrayLayers - std::max(srcZ, dstZ), 0);

    return AppliedCopy{
        WGPUOrigin3D{static_cast<uint32_t>(srcX), static_cast<uint32_t>(srcY), static_cast<uint32_t>(srcZ)},
        WGPUOrigin3D{static_cast<uint32_t>(dstX), static_cast<uint32_t>(dstY), static_cast<uint32_t>(dstZ)},
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

void encodeDepthDrawPass(GpuTest& t, 
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
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
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
        encodeDepthDrawPass(t, encoder, pipeline, bindGroup, view, format, nullptr, true, layer);
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

    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = expected.size();
    bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = t.createBufferTracked(bufferDesc);
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
        encodeDepthDrawPass(t, encoder, pipeline, bindGroup, depthView, format, &colorAttachment, false, layer);
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
    const auto format = parseTextureFormat(t.param<std::string>("format"));
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
    uint32_t dstCopyLevel,
    uint32_t requestedMipLevelCount = 0) {
    t.skipIfTextureFormatNotSupported(srcFormat);
    t.skipIfTextureFormatNotSupported(dstFormat);
    t.skipIfTextureFormatAndDimensionNotCompatible(srcFormat, dimension);
    t.skipIfTextureFormatAndDimensionNotCompatible(dstFormat, dimension);
    if (!textureFormatsAreViewCompatible(srcFormat, dstFormat)) {
        t.skip("source and destination formats are not view-compatible");
    }

    const uint32_t mipLevelCount = requestedMipLevelCount != 0
        ? requestedMipLevelCount
        : (dimension == WGPUTextureDimension_1D ? 1 : 4);
    WGPUTexture srcTexture = createTexture(t, srcFormat, dimension, srcTextureSize, mipLevelCount);
    WGPUTexture dstTexture = createTexture(t, dstFormat, dimension, dstTextureSize, mipLevelCount);

    const WGPUExtent3D srcSizeAtLevel = physicalMipSize(srcTextureSize, srcFormat, dimension, srcCopyLevel);
    const WGPUExtent3D dstSizeAtLevel = physicalMipSize(dstTextureSize, dstFormat, dimension, dstCopyLevel);
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

    WGPUBufferDescriptor dstBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    dstBufferDesc.size = expected.size();
    dstBufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer dstBuffer = t.createBufferTracked(dstBufferDesc);
    copyTextureToBuffer(t, dstTexture, dstBuffer, dstLayout, dstSizeAtLevel, dstCopyLevel);

    if (isCompressedTextureFormat(dstFormat)) {
        t.expectGPUBufferValuesEqual(dstBuffer, expected.data(), expected.size());
        return;
    }
    t.expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
        expected.data(), expected.size(), dstBuffer, dstFormat, dstSizeAtLevel, dstLayout);
}

ParamsBuilder compatibleFormatParams(ParamsBuilder u, const std::vector<Value>& formats) {
    return u.combine("srcFormat", formats)
        .combine("dstFormat", formats)
        .filter([](const ParamRecord& params) {
            const auto srcFormat = parseTextureFormat(valueAs<std::string>(*findParam(params, "srcFormat")));
            const auto dstFormat = parseTextureFormat(valueAs<std::string>(*findParam(params, "dstFormat")));
            return textureFormatsAreViewCompatible(srcFormat, dstFormat);
        });
}

ParamsBuilder compatibleFormatParams(ParamsBuilder u) {
    return compatibleFormatParams(u, regularTextureFormatValues());
}

ParamsBuilder compatibleCompressedFormatParams(ParamsBuilder u) {
    return compatibleFormatParams(u, formatIdentifierValues(kCompressedTextureFormats));
}

ParamsBuilder compressedNonArrayParams(ParamsBuilder u) {
    return compatibleCompressedFormatParams(u)
        .combine("dimension", textureDimensionValues(true))
        .beginSubcases()
        .combine("textureSizeInBlocksIndex", indexValues(static_cast<uint32_t>(kCompressedNonArraySizesInBlocks.size())))
        .combine("copyBoxOffsetsIndex", indexValues(static_cast<uint32_t>(kCopyBoxOffsetsForWholeDepth.size())))
        .combine("srcCopyLevel", {0, 2})
        .combine("dstCopyLevel", {0, 2});
}

ParamsBuilder compressedArrayParams(ParamsBuilder u) {
    return compatibleCompressedFormatParams(u)
        .combine("dimension", textureDimensionValues(false))
        .beginSubcases()
        .combine("textureSizeInBlocksIndex", indexValues(static_cast<uint32_t>(kCompressedArraySizesInBlocks.size())))
        .combine("copyBoxOffsetsIndex", indexValues(static_cast<uint32_t>(kCopyBoxOffsetsFor2DArrayTextures.size())))
        .combine("srcCopyLevel", {0, 2})
        .combine("dstCopyLevel", {0, 2});
}

ParamsBuilder compressedUnalignedParams(ParamsBuilder u) {
    return compatibleCompressedFormatParams(u)
        .beginSubcases()
        // Which dimension(s) of mip level 0 have a partial edge block.
        .combine("partialEdge", {std::string("width"), std::string("height"), std::string("both")})
        .combine("copyBoxOffsetsIndex", indexValues(static_cast<uint32_t>(kCopyBoxOffsetsForWholeDepth.size())));
}

ParamsBuilder nonArrayParams(ParamsBuilder u) {
    return compatibleFormatParams(u)
        .combine("dimension", textureDimensionValues(true))
        .filter([](const ParamRecord& params) {
            const auto srcFormat = parseTextureFormat(valueAs<std::string>(*findParam(params, "srcFormat")));
            const auto dstFormat = parseTextureFormat(valueAs<std::string>(*findParam(params, "dstFormat")));
            const auto dimension = parseTextureDimension(valueAs<std::string>(*findParam(params, "dimension")));
            return textureFormatAndDimensionPossiblyCompatible(dimension, srcFormat)
                && textureFormatAndDimensionPossiblyCompatible(dimension, dstFormat);
        })
        .beginSubcases()
        .combine("sizePairIndex", indexValues(static_cast<uint32_t>(kNonArraySizePairs.size())))
        .combine("copyBoxOffsetsIndex", indexValues(static_cast<uint32_t>(kCopyBoxOffsetsForWholeDepth.size())))
        .filter([](const ParamRecord& params) {
            const auto dimension = parseTextureDimension(valueAs<std::string>(*findParam(params, "dimension")));
            const uint32_t offsetIndex = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "copyBoxOffsetsIndex")));
            const CopyBoxOffset& offsets = kCopyBoxOffsetsForWholeDepth[offsetIndex];
            return dimension != WGPUTextureDimension_1D
                || (offsets.copyExtent.height == 0 && offsets.srcOffset.y == 0 && offsets.dstOffset.y == 0);
        })
        .combine("srcCopyLevel", {0, 3})
        .combine("dstCopyLevel", {0, 3})
        .filter([](const ParamRecord& params) {
            const auto dimension = parseTextureDimension(valueAs<std::string>(*findParam(params, "dimension")));
            const uint32_t srcCopyLevel = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "srcCopyLevel")));
            const uint32_t dstCopyLevel = static_cast<uint32_t>(valueAs<int64_t>(*findParam(params, "dstCopyLevel")));
            return dimension != WGPUTextureDimension_1D || (srcCopyLevel == 0 && dstCopyLevel == 0);
        });
}

ParamsBuilder arrayParams(ParamsBuilder u) {
    return compatibleFormatParams(u)
        .combine("dimension", textureDimensionValues(false))
        .filter([](const ParamRecord& params) {
            const auto srcFormat = parseTextureFormat(valueAs<std::string>(*findParam(params, "srcFormat")));
            const auto dstFormat = parseTextureFormat(valueAs<std::string>(*findParam(params, "dstFormat")));
            const auto dimension = parseTextureDimension(valueAs<std::string>(*findParam(params, "dimension")));
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
    return u.beginSubcases()
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
    const auto srcFormat = parseTextureFormat(t.param<std::string>("srcFormat"));
    const auto dstFormat = parseTextureFormat(t.param<std::string>("dstFormat"));
    const auto dimension = parseTextureDimension(t.param<std::string>("dimension"));
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
    const auto srcFormat = parseTextureFormat(t.param<std::string>("srcFormat"));
    const auto dstFormat = parseTextureFormat(t.param<std::string>("dstFormat"));
    const auto dimension = parseTextureDimension(t.param<std::string>("dimension"));
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

void runCompressed(
    AllFeaturesMaxLimitsGpuTest& t,
    const SizeInBlocksPair& sizeInBlocks,
    uint32_t depthOrArrayLayers,
    const CopyBoxOffset& copyBoxOffsets) {
    const auto srcFormat = parseTextureFormat(t.param<std::string>("srcFormat"));
    const auto dstFormat = parseTextureFormat(t.param<std::string>("dstFormat"));
    const auto dimension = parseTextureDimension(t.param<std::string>("dimension"));
    const uint32_t srcCopyLevel = static_cast<uint32_t>(t.param<int64_t>("srcCopyLevel"));
    const uint32_t dstCopyLevel = static_cast<uint32_t>(t.param<int64_t>("dstCopyLevel"));
    t.skipIfTextureFormatAndDimensionNotCompatible(srcFormat, dimension);
    t.skipIfTextureFormatAndDimensionNotCompatible(dstFormat, dimension);
    const TextureBlockInfo srcBlock = getBlockInfoForTextureFormat(srcFormat);
    const TextureBlockInfo dstBlock = getBlockInfoForTextureFormat(dstFormat);

    doCopyTextureToTextureTest(
        t,
        srcFormat,
        dstFormat,
        dimension,
        WGPUExtent3D{sizeInBlocks.srcWidth * srcBlock.blockWidth, sizeInBlocks.srcHeight * srcBlock.blockHeight, depthOrArrayLayers},
        WGPUExtent3D{sizeInBlocks.dstWidth * dstBlock.blockWidth, sizeInBlocks.dstHeight * dstBlock.blockHeight, depthOrArrayLayers},
        copyBoxOffsets,
        srcCopyLevel,
        dstCopyLevel);
}

void runCompressedNonArray(AllFeaturesMaxLimitsGpuTest& t) {
    runCompressed(
        t,
        kCompressedNonArraySizesInBlocks[static_cast<size_t>(t.param<int64_t>("textureSizeInBlocksIndex"))],
        1,
        kCopyBoxOffsetsForWholeDepth[static_cast<size_t>(t.param<int64_t>("copyBoxOffsetsIndex"))]);
}

void runCompressedArray(AllFeaturesMaxLimitsGpuTest& t) {
    runCompressed(
        t,
        kCompressedArraySizesInBlocks[static_cast<size_t>(t.param<int64_t>("textureSizeInBlocksIndex"))],
        5,
        kCopyBoxOffsetsFor2DArrayTextures[static_cast<size_t>(t.param<int64_t>("copyBoxOffsetsIndex"))]);
}

void runCompressedUnaligned(AllFeaturesMaxLimitsGpuTest& t) {
    const auto srcFormat = parseTextureFormat(t.param<std::string>("srcFormat"));
    const auto dstFormat = parseTextureFormat(t.param<std::string>("dstFormat"));
    const std::string partialEdge = t.param<std::string>("partialEdge");
    const CopyBoxOffset& copyBoxOffsets =
        kCopyBoxOffsetsForWholeDepth[static_cast<size_t>(t.param<int64_t>("copyBoxOffsetsIndex"))];

    if (!deviceHasTextureCompressionUnaligned(t.device())) {
        t.skip("device does not have feature texture-compression-unaligned");
    }

    // The source and destination formats share the same base format, so they have the same texel
    // block size.
    const TextureBlockInfo block = getBlockInfoForTextureFormat(srcFormat);

    // Mip level 0 size: a few full blocks plus a one-texel partial edge block in the selected
    // dimension(s). An unaligned mip level 0 is only valid with 'texture-compression-unaligned'.
    const uint32_t width = partialEdge == "height" ? 4 * block.blockWidth : 3 * block.blockWidth + 1;
    const uint32_t height = partialEdge == "width" ? 4 * block.blockHeight : 3 * block.blockHeight + 1;
    const WGPUExtent3D size{width, height, 1};

    doCopyTextureToTextureTest(
        t,
        srcFormat,
        dstFormat,
        WGPUTextureDimension_2D,
        size,
        size,
        copyBoxOffsets,
        0, // srcCopyLevel: the top (and only) mip level.
        0, // dstCopyLevel
        1); // mipLevelCount: a single, unaligned mip level.
}

WGPUTextureDescriptor multisampledTextureDesc(
    WGPUTextureFormat format,
    WGPUExtent3D size,
    WGPUTextureUsage usage,
    uint32_t sampleCount) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.format = format;
    desc.size = size;
    desc.usage = usage;
    desc.sampleCount = sampleCount;
    return desc;
}

void copyWholeMultisampledTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture source,
    WGPUTexture destination,
    WGPUExtent3D size) {
    copyTextureToTexture(t, source, destination, size, 0, WGPUOrigin3D{0, 0, 0}, 0, WGPUOrigin3D{0, 0, 0});
}

void runCopyMultisampledColor(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPUExtent3D textureSize{32, 16, 1};
    constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8Unorm;
    constexpr uint32_t kSampleCount = 4;

    WGPUTexture sourceTexture = t.createTextureTracked(multisampledTextureDesc(
        kColorFormat,
        textureSize,
        WGPUTextureUsage_CopySrc | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        kSampleCount));
    WGPUTexture destinationTexture = t.createTextureTracked(multisampledTextureDesc(
        kColorFormat,
        textureSize,
        WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
        kSampleCount));
    const WGPUTextureViewDescriptor defaultViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = kColorFormat;
    target.writeMask = WGPUColorWriteMask_All;

    // Initialize sourceTexture with a draw call.
    WGPUFragmentState initFragment = WGPU_FRAGMENT_STATE_INIT;
    initFragment.module = t.createShaderModuleTracked(R"(
            @fragment
            fn main() -> @location(0) vec4<f32> {
              return vec4<f32>(0.3, 0.5, 0.8, 1.0);
            })");
    initFragment.targetCount = 1;
    initFragment.targets = &target;

    WGPURenderPipelineDescriptor initPipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    initPipelineDesc.vertex.module = t.createShaderModuleTracked(R"(
            @vertex
            fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
              var pos = array<vec2<f32>, 3>(
                  vec2<f32>(-1.0,  1.0),
                  vec2<f32>( 1.0,  1.0),
                  vec2<f32>( 1.0, -1.0)
              );
              return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
            })");
    initPipelineDesc.fragment = &initFragment;
    initPipelineDesc.multisample.count = kSampleCount;
    WGPURenderPipeline renderPipelineForInit = t.createRenderPipelineTracked(initPipelineDesc);

    WGPURenderPassColorAttachment initAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    initAttachment.view = t.createViewTracked(sourceTexture, defaultViewDesc);
    initAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};
    initAttachment.loadOp = WGPULoadOp_Clear;
    initAttachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor initPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    initPassDesc.colorAttachmentCount = 1;
    initPassDesc.colorAttachments = &initAttachment;

    WGPUCommandEncoder initEncoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder renderPassForInit = t.beginRenderPassTracked(initEncoder, &initPassDesc);
    wgpuRenderPassEncoderSetPipeline(renderPassForInit, renderPipelineForInit);
    wgpuRenderPassEncoderDraw(renderPassForInit, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(renderPassForInit);
    submit(t, initEncoder);

    // Do the texture-to-texture copy
    copyWholeMultisampledTexture(t, sourceTexture, destinationTexture, textureSize);

    // Verify if all the sub-pixel values at the same location of sourceTexture and
    // destinationTexture are equal.
    WGPUFragmentState validationFragment = WGPU_FRAGMENT_STATE_INIT;
    validationFragment.module = t.createShaderModuleTracked(
        R"(
          @group(0) @binding(0) var sourceTexture : texture_multisampled_2d<f32>;
          @group(0) @binding(1) var destinationTexture : texture_multisampled_2d<f32>;
          @fragment
          fn main(@builtin(position) coord_in: vec4<f32>) -> @location(0) vec4<f32> {
            var coord_in_vec2 = vec2<i32>(i32(coord_in.x), i32(coord_in.y));
            for (var sampleIndex = 0; sampleIndex < )" + std::to_string(kSampleCount) + R"(;
              sampleIndex = sampleIndex + 1) {
              var sourceSubPixel : vec4<f32> =
                textureLoad(sourceTexture, coord_in_vec2, sampleIndex);
              var destinationSubPixel : vec4<f32> =
                textureLoad(destinationTexture, coord_in_vec2, sampleIndex);
              if (!all(sourceSubPixel == destinationSubPixel)) {
                return vec4<f32>(1.0, 0.0, 0.0, 1.0);
              }
            }
            return vec4<f32>(0.0, 1.0, 0.0, 1.0);
          })");
    validationFragment.targetCount = 1;
    validationFragment.targets = &target;

    WGPURenderPipelineDescriptor validationPipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    validationPipelineDesc.vertex.module = t.createShaderModuleTracked(R"(
          @vertex
          fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
            var pos = array<vec2<f32>, 6>(
              vec2<f32>(-1.0,  1.0),
              vec2<f32>(-1.0, -1.0),
              vec2<f32>( 1.0,  1.0),
              vec2<f32>(-1.0, -1.0),
              vec2<f32>( 1.0,  1.0),
              vec2<f32>( 1.0, -1.0));
            return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
          })");
    validationPipelineDesc.fragment = &validationFragment;
    WGPURenderPipeline renderPipelineForValidation = t.createRenderPipelineTracked(validationPipelineDesc);

    std::array<WGPUBindGroupEntry, 2> entries = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
    entries[0].binding = 0;
    entries[0].textureView = t.createViewTracked(sourceTexture, defaultViewDesc);
    entries[1].binding = 1;
    entries[1].textureView = t.createViewTracked(destinationTexture, defaultViewDesc);
    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = wgpuRenderPipelineGetBindGroupLayout(renderPipelineForValidation, 0);
    bindGroupDesc.entryCount = entries.size();
    bindGroupDesc.entries = entries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
    wgpuBindGroupLayoutRelease(bindGroupDesc.layout);

    WGPUTexture expectedOutputTexture = t.createTextureTracked(multisampledTextureDesc(
        kColorFormat, textureSize, WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment, 1));
    WGPURenderPassColorAttachment validationAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    validationAttachment.view = t.createViewTracked(expectedOutputTexture, defaultViewDesc);
    validationAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};
    validationAttachment.loadOp = WGPULoadOp_Clear;
    validationAttachment.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor validationPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    validationPassDesc.colorAttachmentCount = 1;
    validationPassDesc.colorAttachments = &validationAttachment;

    WGPUCommandEncoder validationEncoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder renderPassForValidation = t.beginRenderPassTracked(validationEncoder, &validationPassDesc);
    wgpuRenderPassEncoderSetPipeline(renderPassForValidation, renderPipelineForValidation);
    wgpuRenderPassEncoderSetBindGroup(renderPassForValidation, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(renderPassForValidation, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(renderPassForValidation);
    submit(t, validationEncoder);

    expectSingleColor(t, expectedOutputTexture, textureSize);
}

void runCopyMultisampledDepth(AllFeaturesMaxLimitsGpuTest& t) {
    const auto format = parseTextureFormat(t.param<std::string>("format"));
    t.skipIfTextureFormatNotSupported(format);

    const WGPUExtent3D textureSize{32, 16, 1};
    constexpr uint32_t kSampleCount = 4;
    constexpr WGPUTextureFormat kColorFormat = WGPUTextureFormat_RGBA8Unorm;
    const WGPUTextureViewDescriptor defaultViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;

    WGPUTexture sourceTexture = t.createTextureTracked(multisampledTextureDesc(
        format, textureSize, WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment, kSampleCount));
    WGPUTexture destinationTexture = t.createTextureTracked(multisampledTextureDesc(
        format, textureSize, WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment, kSampleCount));

    WGPUShaderModule vertexModule = t.createShaderModuleTracked(R"(
          @vertex
          fn main(@builtin(vertex_index) VertexIndex : u32)-> @builtin(position) vec4<f32> {
            var pos : array<vec3<f32>, 6> = array<vec3<f32>, 6>(
                vec3<f32>(-1.0,  1.0, 0.5),
                vec3<f32>(-1.0, -1.0, 0.0),
                vec3<f32>( 1.0,  1.0, 1.0),
                vec3<f32>(-1.0, -1.0, 0.0),
                vec3<f32>( 1.0,  1.0, 1.0),
                vec3<f32>( 1.0, -1.0, 0.5));
            return vec4<f32>(pos[VertexIndex], 1.0);
          })");

    // Initialize the depth aspect of source texture with a draw call
    WGPUDepthStencilState initDepthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    initDepthStencil.format = format;
    initDepthStencil.depthCompare = WGPUCompareFunction_Always;
    initDepthStencil.depthWriteEnabled = WGPUOptionalBool_True;

    WGPURenderPipelineDescriptor initPipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    initPipelineDesc.vertex.module = vertexModule;
    initPipelineDesc.depthStencil = &initDepthStencil;
    initPipelineDesc.multisample.count = kSampleCount;
    WGPURenderPipeline renderPipelineForInit = t.createRenderPipelineTracked(initPipelineDesc);

    WGPURenderPassDepthStencilAttachment initAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    initAttachment.view = t.createViewTracked(sourceTexture, defaultViewDesc);
    initAttachment.depthClearValue = 0.0f;
    initAttachment.depthLoadOp = WGPULoadOp_Clear;
    initAttachment.depthStoreOp = WGPUStoreOp_Store;
    if (isStencilTextureFormat(format)) {
        initAttachment.stencilLoadOp = WGPULoadOp_Clear;
        initAttachment.stencilStoreOp = WGPUStoreOp_Store;
    }
    WGPURenderPassDescriptor initPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    initPassDesc.depthStencilAttachment = &initAttachment;

    WGPUCommandEncoder encoderForInit = t.createCommandEncoderTracked();
    WGPURenderPassEncoder renderPassForInit = t.beginRenderPassTracked(encoderForInit, &initPassDesc);
    wgpuRenderPassEncoderSetPipeline(renderPassForInit, renderPipelineForInit);
    wgpuRenderPassEncoderDraw(renderPassForInit, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(renderPassForInit);
    submit(t, encoderForInit);

    // Do the texture-to-texture copy
    copyWholeMultisampledTexture(t, sourceTexture, destinationTexture, textureSize);

    // Verify the depth values in destinationTexture are what we expected with
    // depthCompareFunction == 'equal' and depthWriteEnabled == false in the render pipeline
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = kColorFormat;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState verifyFragment = WGPU_FRAGMENT_STATE_INIT;
    verifyFragment.module = t.createShaderModuleTracked(R"(
          @fragment
          fn main() -> @location(0) vec4<f32> {
            return vec4<f32>(0.0, 1.0, 0.0, 1.0);
          })");
    verifyFragment.targetCount = 1;
    verifyFragment.targets = &target;

    WGPUDepthStencilState verifyDepthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    verifyDepthStencil.format = format;
    verifyDepthStencil.depthCompare = WGPUCompareFunction_Equal;
    verifyDepthStencil.depthWriteEnabled = WGPUOptionalBool_False;

    WGPURenderPipelineDescriptor verifyPipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    verifyPipelineDesc.vertex.module = vertexModule;
    verifyPipelineDesc.fragment = &verifyFragment;
    verifyPipelineDesc.depthStencil = &verifyDepthStencil;
    verifyPipelineDesc.multisample.count = kSampleCount;
    WGPURenderPipeline renderPipelineForVerify = t.createRenderPipelineTracked(verifyPipelineDesc);

    WGPUTexture multisampledColorTexture = t.createTextureTracked(multisampledTextureDesc(
        kColorFormat, textureSize, WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment, kSampleCount));
    WGPUTexture colorTextureAsResolveTarget = t.createTextureTracked(multisampledTextureDesc(
        kColorFormat, textureSize, WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment, 1));

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = t.createViewTracked(multisampledColorTexture, defaultViewDesc);
    colorAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Discard;
    colorAttachment.resolveTarget = t.createViewTracked(colorTextureAsResolveTarget, defaultViewDesc);

    WGPURenderPassDepthStencilAttachment depthAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depthAttachment.view = t.createViewTracked(destinationTexture, defaultViewDesc);
    depthAttachment.depthLoadOp = WGPULoadOp_Load;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    if (isStencilTextureFormat(format)) {
        depthAttachment.stencilLoadOp = WGPULoadOp_Clear;
        depthAttachment.stencilStoreOp = WGPUStoreOp_Store;
    }
    WGPURenderPassDescriptor verifyPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    verifyPassDesc.colorAttachmentCount = 1;
    verifyPassDesc.colorAttachments = &colorAttachment;
    verifyPassDesc.depthStencilAttachment = &depthAttachment;

    WGPUCommandEncoder encoderForVerify = t.createCommandEncoderTracked();
    WGPURenderPassEncoder renderPassForVerify = t.beginRenderPassTracked(encoderForVerify, &verifyPassDesc);
    wgpuRenderPassEncoderSetPipeline(renderPassForVerify, renderPipelineForVerify);
    wgpuRenderPassEncoderDraw(renderPassForVerify, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(renderPassForVerify);
    submit(t, encoderForVerify);

    expectSingleColor(t, colorTextureAsResolveTarget, textureSize);
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
    .params(compressedNonArrayParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runCompressedNonArray(t); });

CTS_TEST(g, "color_textures,compressed,unaligned,non_array")
    .params(compressedUnalignedParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runCompressedUnaligned(t); });

CTS_TEST(g, "color_textures,compressed,array")
    .params(compressedArrayParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runCompressedArray(t); });

CTS_TEST(g, "copy_depth_stencil")
    .params(depthStencilParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runDepthStencil(t); });

CTS_TEST(g, "copy_multisampled_color")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runCopyMultisampledColor(t); });

CTS_TEST(g, "copy_multisampled_depth")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormatValues()).filter([](const ParamRecord& params) {
            return isDepthTextureFormat(parseTextureFormat(valueAs<std::string>(*findParam(params, "format"))));
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runCopyMultisampledDepth(t); });

CTS_TEST(g, "copy_multisampled_stencil")
    .unimplemented("unimplemented upstream");
