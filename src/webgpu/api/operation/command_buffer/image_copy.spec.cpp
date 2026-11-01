// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/image_copy.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"
#include "webgpu/util/texel_view.h"
#include "webgpu/util/texture_layout.h"
#include "webgpu/util/texture_ok.h"

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

/// How the arguments are passed to WriteTexture/CopyB2T/CopyT2B. Upstream's 'arrays' passes GPUOrigin3D
/// and GPUExtent3D as arrays, which has no C analog: it behaves like None. Undefined passes the default
/// values as undefined: in C, WGPU_COPY_STRIDE_UNDEFINED for the strides (a zero origin, mip level and
/// offset already are the defaults).
enum class ChangeBeforePass {
    None,
    Undefined,
};

struct CopyScenario {
    TexelCopyBufferLayout textureDataLayout;
    WGPUExtent3D copySize = WGPUExtent3D{1, 1, 1};
    uint64_t dataSize = 0;
    uint32_t mipLevel = 0;
    WGPUOrigin3D origin = WGPUOrigin3D{0, 0, 0};
    WGPUExtent3D textureSize = WGPUExtent3D{1, 1, 1};
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    WGPUTextureDimension dimension = WGPUTextureDimension_2D;
    InitMethod initMethod = InitMethod::WriteTexture;
    CheckMethod checkMethod = CheckMethod::FullCopyT2B;
    ChangeBeforePass changeBeforePass = ChangeBeforePass::None;
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

std::string_view copyMethodIdentifier(ImageCopyType method) {
    switch (method) {
        case ImageCopyType::WriteTexture:
            return "WriteTexture";
        case ImageCopyType::CopyB2T:
            return "CopyB2T";
        case ImageCopyType::CopyT2B:
            return "CopyT2B";
        default:
            std::abort();
    }
}

ImageCopyType parseCopyMethod(std::string_view identifier) {
    if (identifier == "WriteTexture") {
        return ImageCopyType::WriteTexture;
    }
    if (identifier == "CopyB2T") {
        return ImageCopyType::CopyB2T;
    }
    if (identifier == "CopyT2B") {
        return ImageCopyType::CopyT2B;
    }
    std::abort();
}

std::string_view initMethodIdentifier(InitMethod method) {
    switch (method) {
        case InitMethod::WriteTexture:
            return "WriteTexture";
        case InitMethod::CopyB2T:
            return "CopyB2T";
        default:
            std::abort();
    }
}

InitMethod parseInitMethod(std::string_view identifier) {
    if (identifier == "WriteTexture") {
        return InitMethod::WriteTexture;
    }
    if (identifier == "CopyB2T") {
        return InitMethod::CopyB2T;
    }
    std::abort();
}

std::string_view checkMethodIdentifier(CheckMethod method) {
    switch (method) {
        case CheckMethod::FullCopyT2B:
            return "FullCopyT2B";
        case CheckMethod::PartialCopyT2B:
            return "PartialCopyT2B";
        default:
            std::abort();
    }
}

CheckMethod parseCheckMethod(std::string_view identifier) {
    if (identifier == "FullCopyT2B") {
        return CheckMethod::FullCopyT2B;
    }
    if (identifier == "PartialCopyT2B") {
        return CheckMethod::PartialCopyT2B;
    }
    std::abort();
}

std::string_view depthStencilAspectIdentifier(DepthStencilAspect aspect) {
    switch (aspect) {
        case DepthStencilAspect::DepthOnly:
            return textureAspectIdentifier(WGPUTextureAspect_DepthOnly);
        case DepthStencilAspect::StencilOnly:
            return textureAspectIdentifier(WGPUTextureAspect_StencilOnly);
        default:
            std::abort();
    }
}

DepthStencilAspect parseDepthStencilAspect(std::string_view identifier) {
    const WGPUTextureAspect aspect = parseTextureAspect(identifier);
    if (aspect == WGPUTextureAspect_DepthOnly) {
        return DepthStencilAspect::DepthOnly;
    }
    if (aspect == WGPUTextureAspect_StencilOnly) {
        return DepthStencilAspect::StencilOnly;
    }
    std::abort();
}

std::vector<uint8_t> generateData(size_t size, uint32_t start = 0) {
    std::vector<uint8_t> data(size);
    for (size_t i = 0; i < data.size(); ++i) {
        data[i] = static_cast<uint8_t>((i * 73 + start * 131 + 1) & 0xff);
    }
    return data;
}

// Upstream's kColorTextureFormats: the regular and the compressed formats.
std::vector<Value> colorFormatValues() {
    std::vector<Value> values = formatIdentifierValues(kRegularTextureFormats);
    const std::vector<Value> compressed = formatIdentifierValues(kCompressedTextureFormats);
    values.insert(values.end(), compressed.begin(), compressed.end());
    return values;
}

std::vector<Value> depthStencilFormatValues() {
    return formatIdentifierValues(kDepthStencilFormats);
}

std::vector<Value> depthStencilAspectValues() {
    return {
        std::string(depthStencilAspectIdentifier(DepthStencilAspect::DepthOnly)),
        std::string(depthStencilAspectIdentifier(DepthStencilAspect::StencilOnly)),
    };
}

std::vector<Value> depthStencilCopyMethodValues() {
    return {
        std::string(copyMethodIdentifier(ImageCopyType::WriteTexture)),
        std::string(copyMethodIdentifier(ImageCopyType::CopyB2T)),
        std::string(copyMethodIdentifier(ImageCopyType::CopyT2B)),
    };
}

std::vector<Value> textureDimensionValues() {
    return {
        std::string(textureDimensionIdentifier(WGPUTextureDimension_1D)),
        std::string(textureDimensionIdentifier(WGPUTextureDimension_2D)),
        std::string(textureDimensionIdentifier(WGPUTextureDimension_3D)),
    };
}

/** Each combination of methods assume that the ones before it were tested and work correctly. */
std::vector<ParamRecord> methodsToTest() {
    return {
        // Then we make sure that WriteTexture works for all formats:
        ParamRecord{{"initMethod", std::string(initMethodIdentifier(InitMethod::WriteTexture))},
                    {"checkMethod", std::string(checkMethodIdentifier(CheckMethod::FullCopyT2B))}},
        // Then we make sure that CopyB2T works for all formats:
        ParamRecord{{"initMethod", std::string(initMethodIdentifier(InitMethod::CopyB2T))},
                    {"checkMethod", std::string(checkMethodIdentifier(CheckMethod::FullCopyT2B))}},
        // Then we make sure that CopyT2B works for all formats:
        ParamRecord{{"initMethod", std::string(initMethodIdentifier(InitMethod::WriteTexture))},
                    {"checkMethod", std::string(checkMethodIdentifier(CheckMethod::PartialCopyT2B))}},
    };
}

WGPUTextureFormat formatFromParams(const ParamRecord& params) {
    return parseTextureFormat(valueAs<std::string>(*findParam(params, "format")));
}

WGPUTextureDimension dimensionFromParams(const ParamRecord& params) {
    return parseTextureDimension(valueAs<std::string>(*findParam(params, "dimension")));
}

bool formatAndDimensionPossiblyCompatible(const ParamRecord& params) {
    return textureFormatAndDimensionPossiblyCompatible(dimensionFromParams(params), formatFromParams(params));
}

ParamsBuilder baseParams(ParamsBuilder u) {
    return u.combineWithParams(methodsToTest())
        .combine("format", colorFormatValues())
        .combine("dimension", textureDimensionValues())
        .filter(formatAndDimensionPossiblyCompatible);
}

WGPUTexelCopyBufferLayout toWgpuLayout(TexelCopyBufferLayout layout) {
    WGPUTexelCopyBufferLayout out = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    out.offset = layout.offset;
    out.bytesPerRow = layout.bytesPerRow;
    out.rowsPerImage = layout.rowsPerImage;
    return out;
}

// undefDataLayoutIfNeeded(): zero strides are passed as undefined with ChangeBeforePass::Undefined.
WGPUTexelCopyBufferLayout appliedLayout(TexelCopyBufferLayout layout, ChangeBeforePass changeBeforePass) {
    if (changeBeforePass == ChangeBeforePass::Undefined) {
        if (layout.bytesPerRow == 0) {
            layout.bytesPerRow = WGPU_COPY_STRIDE_UNDEFINED;
        }
        if (layout.rowsPerImage == 0) {
            layout.rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;
        }
    }
    return toWgpuLayout(layout);
}

WGPUTexture createCopyTexture(AllFeaturesMaxLimitsGpuTest& t, const CopyScenario& scenario) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = scenario.textureSize;
    desc.mipLevelCount = scenario.mipLevel + 1;
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
    WGPUTexelCopyBufferLayout layout,
    WGPUExtent3D copySize,
    uint32_t mipLevel,
    WGPUOrigin3D origin) {
    WGPUTexelCopyBufferInfo source = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    source.buffer = buffer;
    source.layout = layout;

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
    WGPUTexelCopyBufferLayout layout,
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
    destination.layout = layout;

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

/** Put data into a part of the texture with an appropriate method. */
void uploadLinearTextureDataToTextureSubBox(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const CopyScenario& scenario,
    const std::vector<uint8_t>& data) {
    const WGPUTexelCopyBufferLayout layout = appliedLayout(scenario.textureDataLayout, scenario.changeBeforePass);
    if (scenario.initMethod == InitMethod::WriteTexture) {
        // An empty vector has no data pointer, an empty write still needs one.
        const uint8_t emptyData = 0;
        t.queueWriteTexture(
            texture,
            scenario.copySize,
            layout,
            data.empty() ? &emptyData : data.data(),
            data.size(),
            scenario.mipLevel,
            scenario.origin);
        return;
    }

    WGPUBuffer src = t.makeBufferWithContents(data.data(), data.size(), WGPUBufferUsage_CopySrc);
    copyBufferToTexture(t, texture, src, layout, scenario.copySize, scenario.mipLevel, scenario.origin);
}

/**
 * Compares data in `expected` to data in `buffer`.
 * Areas defined by size and dataLayout are compared by interpreting the data as appropriate
 * for the texture format. As an example, with 'rgb9e5ufloat' multiple values can
 * represent the same number. For example, double the exponent and halving the
 * mantissa. Areas outside the area defined by size and dataLayout are expected to match
 * by binary comparison.
 *
 * Upstream's texel views ignore dataLayout.offset; here they start at the offset, so that the texel
 * areas that get compared are the ones the copy wrote.
 */
void expectBufferValuesEqualWhenInterpretedAsTextureFormat(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::vector<uint8_t>& expected,
    WGPUBuffer buffer,
    WGPUTextureFormat format,
    WGPUExtent3D size,
    TexelCopyBufferLayout dataLayout) {
    if (isCompressedTextureFormat(format)) {
        t.expectGPUBufferValuesEqual(buffer, expected.data(), expected.size());
        return;
    }

    const std::function<std::optional<std::string>(const uint8_t*, size_t)> check =
        [&](const uint8_t* actual, size_t) -> std::optional<std::string> {
        const bool emptyCopy = size.width == 0 || size.height == 0 || size.depthOrArrayLayers == 0;
        // compare texel areas
        if (!emptyCopy) {
            TexelViewConfig config;
            config.bytesPerRow = dataLayout.bytesPerRow;
            config.rowsPerImage = dataLayout.rowsPerImage;
            config.subrectOrigin = WGPUOrigin3D{0, 0, 0};
            config.subrectSize = size;
            const size_t viewLength = expected.size() - static_cast<size_t>(dataLayout.offset);
            const TexelView actualView = TexelView::fromTextureDataByReference(
                format, actual + dataLayout.offset, viewLength, config);
            const TexelView expectedView = TexelView::fromTextureDataByReference(
                format, expected.data() + dataLayout.offset, viewLength, config);
            std::optional<std::string> failedPixels =
                findFailedPixels(format, WGPUOrigin3D{0, 0, 0}, size, actualView, expectedView, 0.0);
            if (failedPixels) {
                return "Texture level had unexpected contents:\n" + *failedPixels;
            }
        }

        // compare non texel areas
        const auto compareBytes = [&](size_t begin, size_t end) -> std::optional<std::string> {
            for (size_t i = begin; i < end; ++i) {
                if (actual[i] != expected[i]) {
                    std::ostringstream message;
                    message << "mismatch outside of the texel area at byte " << i << ": expected "
                            << static_cast<int>(expected[i]) << ", got " << static_cast<int>(actual[i]);
                    return message.str();
                }
            }
            return std::nullopt;
        };
        const uint32_t rowLength = bytesInACompleteRow(size.width, format);
        size_t lastOffset = 0;
        if (!emptyCopy) {
            for (uint32_t z = 0; z < size.depthOrArrayLayers; ++z) {
                for (uint32_t y = 0; y < size.height; ++y) {
                    const size_t offset = static_cast<size_t>(
                        dataLayout.offset
                        + (static_cast<uint64_t>(z) * dataLayout.rowsPerImage + y) * dataLayout.bytesPerRow);
                    if (std::optional<std::string> error = compareBytes(lastOffset, offset)) {
                        return error;
                    }
                    lastOffset = offset + rowLength;
                }
            }
        }
        // compare end of buffers
        return compareBytes(lastOffset, expected.size());
    };
    t.expectGPUBufferValuesPassCheck(buffer, check, 0, expected.size());
}

/**
 * We check an appropriate part of the texture against the given data.
 * Used directly with PartialCopyT2B check method (for a subpart of the texture)
 * and by `copyWholeTextureToBufferAndCheckContentsWithUpdatedData` with FullCopyT2B check method
 * (for the whole texture).
 */
void copyPartialTextureToBufferAndCheckContents(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    uint32_t mipLevel,
    WGPUOrigin3D origin,
    WGPUExtent3D checkSize,
    WGPUTextureFormat format,
    const std::vector<uint8_t>& expected,
    TexelCopyBufferLayout expectedDataLayout,
    ChangeBeforePass changeBeforePass = ChangeBeforePass::None) {
    // The alignment is necessary because we need to copy and map data from this buffer.
    const size_t bufferSize = static_cast<size_t>(alignTo(expected.size(), kBufferCopyAlignment));
    // The start value ensures generated data here doesn't match the expected data.
    std::vector<uint8_t> bufferData = generateData(bufferSize, 17);

    WGPUBuffer buffer = t.makeBufferWithContents(
        bufferData.data(), bufferData.size(), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
    // At this point both buffer and bufferData have the same random data in them. We'll use
    // copyTextureToBuffer to update buffer with data from the texture and
    // updateLinearTextureDataSubBox to update bufferData with the data we originally uploaded to the
    // texture.
    copyTextureToBuffer(
        t, texture, buffer, appliedLayout(expectedDataLayout, changeBeforePass), checkSize, mipLevel, origin);

    LinearTextureSubBox copy;
    copy.src = expected.data();
    copy.srcLen = expected.size();
    copy.srcLayout = expectedDataLayout;
    copy.dst = bufferData.data();
    copy.dstLen = bufferData.size();
    copy.dstLayout = expectedDataLayout;
    updateLinearTextureDataSubBox(format, checkSize, copy);

    expectBufferValuesEqualWhenInterpretedAsTextureFormat(t, bufferData, buffer, format, checkSize, expectedDataLayout);
}

std::vector<uint8_t> copyTextureToVector(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTexelCopyBufferLayout layout,
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

/**
 * Tests copy between linear data and texture by creating a texture, putting some data into it
 * with WriteTexture/CopyB2T, then getting data for the whole texture/for a part of it back and
 * comparing it with the expectation.
 */
void uploadTextureAndVerifyCopy(AllFeaturesMaxLimitsGpuTest& t, const CopyScenario& scenario) {
    WGPUTexture texture = createCopyTexture(t, scenario);
    const std::vector<uint8_t> data = generateData(static_cast<size_t>(scenario.dataSize));

    if (scenario.checkMethod == CheckMethod::PartialCopyT2B) {
        uploadLinearTextureDataToTextureSubBox(t, texture, scenario, data);
        copyPartialTextureToBufferAndCheckContents(
            t,
            texture,
            scenario.mipLevel,
            scenario.origin,
            scenario.copySize,
            scenario.format,
            data,
            scenario.textureDataLayout,
            scenario.changeBeforePass);
        return;
    }

    // Upload, snapshot the whole subresource (the untouched texels keep their contents), overlay the
    // uploaded sub-box onto the snapshot, then check the whole subresource against that.
    uploadLinearTextureDataToTextureSubBox(t, texture, scenario, data);

    const TextureCopyLayout fullTextureCopyLayout =
        getTextureCopyLayout(scenario.format, scenario.dimension, scenario.textureSize, scenario.mipLevel);
    const TexelCopyBufferLayout fullLayout{0, fullTextureCopyLayout.bytesPerRow, fullTextureCopyLayout.rowsPerImage};
    std::vector<uint8_t> fullData = copyTextureToVector(
        t,
        texture,
        toWgpuLayout(fullLayout),
        fullTextureCopyLayout.mipSize,
        scenario.mipLevel,
        WGPUOrigin3D{0, 0, 0},
        static_cast<size_t>(fullTextureCopyLayout.byteLength));

    LinearTextureSubBox copy;
    copy.src = data.data();
    copy.srcLen = data.size();
    copy.srcLayout = scenario.textureDataLayout;
    copy.dst = fullData.data();
    copy.dstLen = fullData.size();
    copy.dstLayout = fullLayout;
    copy.dstOrigin = scenario.origin;
    updateLinearTextureDataSubBox(scenario.format, scenario.copySize, copy);

    copyPartialTextureToBufferAndCheckContents(
        t,
        texture,
        scenario.mipLevel,
        WGPUOrigin3D{0, 0, 0},
        fullTextureCopyLayout.mipSize,
        scenario.format,
        fullData,
        fullLayout);
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
        WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
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
    scenario.dimension = parseTextureDimension(t.param<std::string>("dimension"));
    scenario.initMethod = parseInitMethod(t.param<std::string>("initMethod"));
    scenario.checkMethod = parseCheckMethod(t.param<std::string>("checkMethod"));
    if (t.hasParam("format")) {
        scenario.format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(scenario.format);
        t.skipIfTextureFormatAndDimensionNotCompatible(scenario.format, scenario.dimension);
    }
    return scenario;
}

uint64_t dataBytesForScenario(const CopyScenario& scenario, TexelCopyBufferLayout layout) {
    return dataBytesForCopyOrFail(
        layout, scenario.format, scenario.copySize, scenario.initMethod != InitMethod::WriteTexture);
}

std::vector<Value> indexValues(uint32_t count) {
    std::vector<Value> values;
    for (uint32_t i = 0; i < count; ++i) {
        values.push_back(static_cast<int64_t>(i));
    }
    return values;
}

struct RowsPerImagePadding {
    uint32_t bytesPerRowPadding;
    uint32_t rowsPerImagePadding;
};

constexpr std::array<RowsPerImagePadding, 4> kRowsPerImagePaddings = {{
    {0, 0}, // no padding
    {0, 6}, // rowsPerImage padding
    {6, 0}, // bytesPerRow padding
    {15, 17}, // both paddings
}};

struct CopySizeInBlocks {
    uint32_t copyWidthInBlocks;
    uint32_t copyHeightInBlocks;
    uint32_t copyDepth;
};

constexpr std::array<CopySizeInBlocks, 12> kRowsPerImageCopySizes = {{
    // In the two cases below, for (WriteTexture, PartialCopyB2T) and (CopyB2T, FullCopyT2B)
    // sets of methods we will have bytesPerRow = 256 and copyDepth % 2 == { 0, 1 }
    // respectively. This covers a special code path for D3D12.
    {3, 4, 5}, // standard copy
    {5, 4, 2}, // standard copy

    {0, 4, 5}, // empty copy because of width
    {3, 0, 5}, // empty copy because of height
    {3, 4, 0}, // empty copy because of depthOrArrayLayers
    {256, 3, 2}, // copyWidth is 256-aligned
    {1, 3, 5}, // copyWidth = 1

    // The two cases below cover another special code path for D3D12.
    //   - For (WriteTexture, FullCopyT2B) with r8unorm:
    //         bytesPerRow = 15 = 3 * 5 = bytesInACompleteCopyImage.
    {32, 1, 8}, // copyHeight = 1
    //   - For (CopyB2T, FullCopyT2B) and (WriteTexture, PartialCopyT2B) with r8unorm:
    //         bytesPerRow = 256 = 8 * 32 = bytesInACompleteCopyImage.
    {5, 4, 1}, // copyDepth = 1

    {7, 1, 1}, // copyHeight = 1 and copyDepth = 1

    // These 2 cases are for textureBindingViewDimension: 'cube'
    // Note: this is indirect. The fact that the copyWidthInBlocks and copyHeightInBlocks
    // are equal makes it possible to use as a cube
    {3, 3, 1},
    {4, 4, 6},
}};

// Copy sizes that are suitable for 1D texture and check both some copy sizes and empty copies.
constexpr std::array<CopySizeInBlocks, 5> kRowsPerImageCopySizes1D = {{
    {3, 1, 1},
    {5, 1, 1},

    {3, 0, 1},
    {0, 1, 1},
    {5, 1, 0},
}};

ParamsBuilder rowsParams(ParamsBuilder u) {
    return baseParams(u)
        .beginSubcases()
        .combine("paddingIndex", indexValues(static_cast<uint32_t>(kRowsPerImagePaddings.size())))
        .expand("copySizeIndex", [](const ParamRecord& params) {
            return indexValues(static_cast<uint32_t>(dimensionFromParams(params) == WGPUTextureDimension_1D
                                                         ? kRowsPerImageCopySizes1D.size()
                                                         : kRowsPerImageCopySizes.size()));
        });
}

struct OffsetAndPadding {
    uint32_t offsetInBlocks;
    uint32_t dataPaddingInBytes;
};

constexpr std::array<OffsetAndPadding, 11> kOffsetsAndPaddings = {{
    {0, 0}, // no offset and no padding
    {1, 0}, // offset = 1
    {2, 0}, // offset = 2
    {15, 0}, // offset = 15
    {16, 0}, // offset = 16
    {242, 0}, // for rgba8unorm format: offset + bytesInCopyExtentPerRow = 242 + 12 = 256 = bytesPerRow
    {243, 0}, // for rgba8unorm format: offset + bytesInCopyExtentPerRow = 243 + 12 > 256 = bytesPerRow
    {768, 0}, // for copyDepth = 1, blockWidth = 1 and bytesPerBlock = 1: offset = 768 = 3 * 256 = bytesInACompleteCopyImage
    {769, 0}, // for copyDepth = 1, blockWidth = 1 and bytesPerBlock = 1: offset = 769 > 768 = bytesInACompleteCopyImage
    {0, 1}, // dataPaddingInBytes > 0
    {1, 8}, // offset > 0 and dataPaddingInBytes > 0
}};

ParamsBuilder offsetsParams(ParamsBuilder u) {
    return baseParams(u)
        .beginSubcases()
        .combine("offsetsAndPaddingsIndex", indexValues(static_cast<uint32_t>(kOffsetsAndPaddings.size())))
        .combine("copyDepth", {1, 2, 6}) // 2d and 2d-array textures
        // copyWidth === 3 is the default. Others covers special cases for r8snorm and rg8snorm on
        // compatiblity mode.
        .combine("copyWidth", {3, 1, 2, 127, 128, 255, 256})
        .filter([](const ParamRecord& params) {
            const WGPUTextureFormat format = formatFromParams(params);
            if (format == WGPUTextureFormat_R8Snorm || format == WGPUTextureFormat_RG8Snorm) {
                return true;
            }
            // Restrict test parameters to save run time.
            return valueAs<int>(*findParam(params, "copyWidth")) == 3;
        })
        .combine("rowsPerImageEqualsCopyHeight", {true, false})
        .filter([](const ParamRecord& params) {
            return !(dimensionFromParams(params) == WGPUTextureDimension_1D
                     && valueAs<int>(*findParam(params, "copyDepth")) != 1);
        });
}

ParamsBuilder originsParams(ParamsBuilder u) {
    return baseParams(u)
        .beginSubcases()
        .combine("originValueInBlocks", {0, 6, 7, 8}) // the 6 is needed for textureBindingViewDimension: 'cube'
        .combine("copySizeValueInBlocks", {0, 6, 7, 8}) // the 6 is needed for textureBindingViewDimension: 'cube'
        .combine("textureSizePaddingValueInBlocks", {0, 7, 8})
        .filter([](const ParamRecord& params) {
            // we can't create an empty texture
            return valueAs<int>(*findParam(params, "copySizeValueInBlocks"))
                       + valueAs<int>(*findParam(params, "originValueInBlocks"))
                       + valueAs<int>(*findParam(params, "textureSizePaddingValueInBlocks"))
                != 0;
        })
        .combine("coordinateToTest", {0, 1, 2})
        .filter([](const ParamRecord& params) {
            return !(dimensionFromParams(params) == WGPUTextureDimension_1D
                     && valueAs<int>(*findParam(params, "coordinateToTest")) != 0);
        });
}

struct MipLevelCase {
    WGPUExtent3D copySizeInBlocks;
    WGPUOrigin3D originInBlocks;
    WGPUExtent3D mipSizeInBlocks;
    uint32_t mipLevel;
};

constexpr std::array<MipLevelCase, 7> kMipLevelCases = {{
    // origin + copySize = texturePhysicalSizeAtMipLevel for all coordinates, 2d texture */
    {{5, 4, 1}, {3, 2, 0}, {8, 6, 1}, 1},
    // origin + copySize = texturePhysicalSizeAtMipLevel for all coordinates, 2d-array texture
    {{5, 4, 2}, {3, 2, 1}, {8, 6, 3}, 2},
    // origin.x + copySize.width = texturePhysicalSizeAtMipLevel.width
    {{5, 4, 2}, {3, 2, 1}, {8, 7, 4}, 3},
    // origin.y + copySize.height = texturePhysicalSizeAtMipLevel.height
    {{5, 4, 2}, {3, 2, 1}, {9, 6, 4}, 4},
    // origin.z + copySize.depthOrArrayLayers = texturePhysicalSizeAtMipLevel.depthOrArrayLayers
    {{5, 4, 2}, {3, 2, 1}, {9, 7, 3}, 4},
    // origin + copySize < texturePhysicalSizeAtMipLevel for all coordinates
    {{5, 4, 2}, {3, 2, 1}, {9, 7, 4}, 4},
    // width === height and depthOrArrayLayers = 6 for textureBindingViewDimension: 'cube'
    {{5, 4, 2}, {3, 2, 1}, {9, 9, 6}, 4},
}};

std::string textureSizeParam(uint32_t width, uint32_t height, uint32_t depth) {
    return std::to_string(width) + "," + std::to_string(height) + "," + std::to_string(depth);
}

WGPUExtent3D parseTextureSizeParam(const std::string& textureSize) {
    WGPUExtent3D size{};
    std::stringstream stream(textureSize);
    char comma = 0;
    stream >> size.width >> comma >> size.height >> comma >> size.depthOrArrayLayers;
    return size;
}

/**
 * Generates textureSizes which correspond to the same physicalSizeAtMipLevel including virtual
 * sizes at mip level different from the physical ones.
 */
std::vector<Value> generateTestTextureSizes(const ParamRecord& params) {
    const WGPUTextureDimension dimension = dimensionFromParams(params);
    const MipLevelCase& mipCase =
        kMipLevelCases[static_cast<size_t>(valueAs<int64_t>(*findParam(params, "mipLevelCaseIndex")))];
    const TextureBlockInfo info = getBlockInfoForTextureFormat(formatFromParams(params));
    const uint32_t mipLevel = mipCase.mipLevel;

    const uint32_t widthAtThisLevel = mipCase.mipSizeInBlocks.width * info.blockWidth;
    const uint32_t heightAtThisLevel = mipCase.mipSizeInBlocks.height * info.blockHeight;
    const uint32_t textureWidth = widthAtThisLevel << mipLevel;
    const uint32_t textureHeight = heightAtThisLevel << mipLevel;
    const uint32_t textureDepth = mipCase.mipSizeInBlocks.depthOrArrayLayers
        << (dimension == WGPUTextureDimension_3D ? mipLevel : 0);
    std::vector<Value> sizes;
    sizes.emplace_back(textureSizeParam(textureWidth, textureHeight, textureDepth));

    // We choose width and height of the texture so that the values are divisible by blockWidth and
    // blockHeight respectively and so that the virtual size at mip level corresponds to the same
    // physical size.
    // Virtual size at mip level with modified width has width = (physical size width) - (blockWidth / 2).
    // Virtual size at mip level with modified height has height = (physical size height) - (blockHeight / 2).
    const uint32_t widthAtPrevLevel = widthAtThisLevel << 1;
    const uint32_t heightAtPrevLevel = heightAtThisLevel << 1;
    const uint32_t modifiedWidth = (widthAtPrevLevel - info.blockWidth) << (mipLevel - 1);
    const uint32_t modifiedHeight = (heightAtPrevLevel - info.blockHeight) << (mipLevel - 1);

    const bool modifyWidth = info.blockWidth > 1 && modifiedWidth != textureWidth;
    const bool modifyHeight = info.blockHeight > 1 && modifiedHeight != textureHeight;

    if (modifyWidth) {
        sizes.emplace_back(textureSizeParam(modifiedWidth, textureHeight, textureDepth));
    }
    if (modifyHeight) {
        sizes.emplace_back(textureSizeParam(textureWidth, modifiedHeight, textureDepth));
    }
    if (modifyWidth && modifyHeight) {
        sizes.emplace_back(textureSizeParam(modifiedWidth, modifiedHeight, textureDepth));
    }

    if (dimension == WGPUTextureDimension_3D) {
        sizes.emplace_back(textureSizeParam(textureWidth, textureHeight, textureDepth + 1));
    }
    return sizes;
}

ParamsBuilder mipParams(ParamsBuilder u) {
    return u.combineWithParams(methodsToTest())
        .combine("format", colorFormatValues())
        .combine("dimension",
                 {std::string(textureDimensionIdentifier(WGPUTextureDimension_2D)),
                  std::string(textureDimensionIdentifier(WGPUTextureDimension_3D))})
        .filter(formatAndDimensionPossiblyCompatible)
        .beginSubcases()
        .combine("mipLevelCaseIndex", indexValues(static_cast<uint32_t>(kMipLevelCases.size())))
        .expand("textureSize", generateTestTextureSizes);
}

struct UnalignedCopyCase {
    uint32_t originXInBlocks;
    uint32_t originYInBlocks;
    uint32_t widthInBlocks;
    uint32_t heightInBlocks;
};

// Copy box in block units. The mip level 0 size is 3 full blocks + 1 partial-edge block (physical
// size 4 blocks) in each dimension, so x/y of 3 selects the partial edge block.
constexpr std::array<UnalignedCopyCase, 5> kUnalignedCopyCases = {{
    // Full copy of the physical extent (touches every partial edge block).
    {0, 0, 4, 4},
    // Just the partial edge column / row / corner.
    {3, 0, 1, 4},
    {0, 3, 4, 1},
    {3, 3, 1, 1},
    // Interior block that does not touch a partial edge block.
    {0, 0, 1, 1},
}};

ParamsBuilder unalignedMipLevel0Params(ParamsBuilder u) {
    return u.combineWithParams(methodsToTest())
        .combine("format", colorFormatValues())
        .filter([](const ParamRecord& params) { return isCompressedTextureFormat(formatFromParams(params)); })
        .beginSubcases()
        .combine("copyCaseIndex", indexValues(static_cast<uint32_t>(kUnalignedCopyCases.size())));
}

struct UndefinedParamsCase {
    uint32_t copyWidth;
    uint32_t copyHeight;
    uint32_t copyDepth;
    // Zero stands for an undefined origin component or stride.
    uint32_t originX;
    uint32_t originY;
    uint32_t originZ;
    uint32_t bytesPerRow;
    uint32_t rowsPerImage;
};

constexpr std::array<UndefinedParamsCase, 7> kUndefinedParamsCases = {{
    // copying one row: bytesPerRow and rowsPerImage can be undefined
    {3, 1, 1, 0, 0, 0, 0, 0},
    // copying one slice: rowsPerImage can be undefined
    {3, 1, 1, 0, 0, 0, 256, 0},
    {3, 3, 1, 0, 0, 0, 256, 0},
    // copying two slices
    {3, 3, 2, 0, 0, 0, 256, 3},
    // origin.x = undefined
    {1, 1, 1, 0, 1, 1, 0, 0},
    // origin.y = undefined
    {1, 1, 1, 1, 0, 1, 0, 0},
    // origin.z = undefined
    {1, 1, 1, 1, 1, 0, 0, 0},
}};

ParamsBuilder undefinedParams(ParamsBuilder u) {
    return u.combineWithParams(methodsToTest())
        .combine("dimension", textureDimensionValues())
        .beginSubcases()
        .combine("undefinedCaseIndex", indexValues(static_cast<uint32_t>(kUndefinedParamsCases.size())))
        .filter([](const ParamRecord& params) {
            const UndefinedParamsCase& undefinedCase =
                kUndefinedParamsCases[static_cast<size_t>(valueAs<int64_t>(*findParam(params, "undefinedCaseIndex")))];
            const uint32_t textureHeight = undefinedCase.copyHeight + undefinedCase.originY;
            const uint32_t textureDepth = undefinedCase.copyDepth + undefinedCase.originZ;
            return !(dimensionFromParams(params) == WGPUTextureDimension_1D && (textureHeight > 1 || textureDepth > 1));
        });
}

ParamsBuilder depthStencilBaseParams(ParamsBuilder u) {
    return u.combine("format", depthStencilFormatValues())
        .combine("copyMethod", depthStencilCopyMethodValues())
        .combine("aspect", depthStencilAspectValues())
        .filter([](const ParamRecord& params) {
            const auto format = parseTextureFormat(valueAs<std::string>(*findParam(params, "format")));
            const auto copyMethod = parseCopyMethod(valueAs<std::string>(*findParam(params, "copyMethod")));
            const auto aspect = parseDepthStencilAspect(valueAs<std::string>(*findParam(params, "aspect")));
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
    const RowsPerImagePadding& padding = kRowsPerImagePaddings[static_cast<size_t>(t.param<int64_t>("paddingIndex"))];
    const size_t copySizeIndex = static_cast<size_t>(t.param<int64_t>("copySizeIndex"));
    const CopySizeInBlocks& copySizeInBlocks = scenario.dimension == WGPUTextureDimension_1D
        ? kRowsPerImageCopySizes1D[copySizeIndex]
        : kRowsPerImageCopySizes[copySizeIndex];

    const TextureBlockInfo info = getBlockInfoForTextureFormat(scenario.format);
    const std::array<uint32_t, 3> maxSize =
        getMaxValidTextureSizeForFormatAndDimension(t.getLimits(), scenario.format, scenario.dimension);

    // For CopyB2T and CopyT2B we need to have bytesPerRow 256-aligned,
    // to make this happen we align the bytesInACompleteRow value and multiply
    // bytesPerRowPadding by 256.
    const uint32_t bytesPerRowAlignment =
        scenario.initMethod == InitMethod::WriteTexture && scenario.checkMethod == CheckMethod::FullCopyT2B ? 1 : 256;

    const uint32_t copyWidth = std::min(copySizeInBlocks.copyWidthInBlocks * info.blockWidth, maxSize[0]);
    const uint32_t copyHeight = std::min(copySizeInBlocks.copyHeightInBlocks * info.blockHeight, maxSize[1]);
    const uint32_t copyDepth = copySizeInBlocks.copyDepth;
    const uint32_t rowsPerImage = copySizeInBlocks.copyHeightInBlocks + padding.rowsPerImagePadding;
    const uint32_t bytesPerRow =
        static_cast<uint32_t>(alignTo(bytesInACompleteRow(copyWidth, scenario.format), bytesPerRowAlignment))
        + padding.bytesPerRowPadding * bytesPerRowAlignment;

    scenario.copySize = WGPUExtent3D{copyWidth, copyHeight, copyDepth};
    scenario.textureDataLayout = TexelCopyBufferLayout{0, bytesPerRow, rowsPerImage};
    scenario.dataSize = dataBytesForScenario(scenario, scenario.textureDataLayout);
    /* making sure the texture is non-empty */
    scenario.textureSize = WGPUExtent3D{
        std::max(copyWidth, info.blockWidth),
        std::max(copyHeight, info.blockHeight),
        std::max(copyDepth, scenario.dimension == WGPUTextureDimension_2D ? 6u : 1u),
    };
    uploadTextureAndVerifyCopy(t, scenario);
}

void runOffsetsAndSizes(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const OffsetAndPadding& offsetAndPadding =
        kOffsetsAndPaddings[static_cast<size_t>(t.param<int64_t>("offsetsAndPaddingsIndex"))];
    const uint32_t copyDepth = static_cast<uint32_t>(t.param<int>("copyDepth"));
    const uint32_t copyWidth = static_cast<uint32_t>(t.param<int>("copyWidth"));
    const bool rowsPerImageEqualsCopyHeight = t.param<bool>("rowsPerImageEqualsCopyHeight");

    // Skip test cases designed for special cases coverage on compatibility mode to save run time.
    // (This port never runs in compatibility mode.)
    if (!rowsPerImageEqualsCopyHeight) {
        t.skip("rowsPerImageEqualsCopyHeight === false is only for r8snorm and rg8snorm on compatibility mode");
    }
    if (copyWidth != 3) {
        t.skip("copyWidth !== 3 is only for r8snorm and rg8snorm on compatibility mode");
    }

    const TextureBlockInfo info = getBlockInfoForTextureFormat(scenario.format);

    const uint64_t offset = static_cast<uint64_t>(offsetAndPadding.offsetInBlocks) * info.bytesPerBlock;
    constexpr uint32_t copyHeight = 3;
    scenario.copySize = WGPUExtent3D{copyWidth * info.blockWidth, copyHeight * info.blockHeight, copyDepth};
    uint32_t textureHeight = 4 * info.blockHeight;
    uint32_t rowsPerImage = rowsPerImageEqualsCopyHeight ? copyHeight : copyHeight + 1;
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(copyWidth * info.bytesPerBlock, 256));

    if (scenario.dimension == WGPUTextureDimension_1D) {
        scenario.copySize.height = 1;
        textureHeight = info.blockHeight;
        rowsPerImage = 1;
    }
    // Add textureWidth by 1 to make sure we are doing a partial copy.
    scenario.textureSize = WGPUExtent3D{(copyWidth + 1) * info.blockWidth, textureHeight, copyDepth};

    scenario.textureDataLayout = TexelCopyBufferLayout{offset, bytesPerRow, rowsPerImage};
    scenario.dataSize = dataBytesForScenario(scenario, scenario.textureDataLayout) + offsetAndPadding.dataPaddingInBytes;

    // We're copying a (copyWidth x 3 x copyDepth) (in texel blocks) part of a ((copyWidth + 1) x 4 x copyDepth)
    // (in texel blocks) texture with no origin.
    uploadTextureAndVerifyCopy(t, scenario);
}

void runOriginsAndExtents(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const uint32_t originValueInBlocks = static_cast<uint32_t>(t.param<int>("originValueInBlocks"));
    const uint32_t copySizeValueInBlocks = static_cast<uint32_t>(t.param<int>("copySizeValueInBlocks"));
    const uint32_t textureSizePaddingValueInBlocks =
        static_cast<uint32_t>(t.param<int>("textureSizePaddingValueInBlocks"));
    const size_t coordinateToTest = static_cast<size_t>(t.param<int>("coordinateToTest"));
    const TextureBlockInfo info = getBlockInfoForTextureFormat(scenario.format);

    std::array<uint32_t, 3> originBlocks = {1, 1, 1};
    std::array<uint32_t, 3> copySizeBlocks = {2, 2, 2};
    std::array<uint32_t, 3> texSizeBlocks = {3, 3, 3};
    if (scenario.dimension == WGPUTextureDimension_1D) {
        originBlocks = {1, 0, 0};
        copySizeBlocks = {2, 1, 1};
        texSizeBlocks = {3, 1, 1};
    }

    originBlocks[coordinateToTest] = originValueInBlocks;
    copySizeBlocks[coordinateToTest] = copySizeValueInBlocks;
    texSizeBlocks[coordinateToTest] =
        originBlocks[coordinateToTest] + copySizeBlocks[coordinateToTest] + textureSizePaddingValueInBlocks;

    scenario.origin = WGPUOrigin3D{originBlocks[0] * info.blockWidth, originBlocks[1] * info.blockHeight, originBlocks[2]};
    scenario.copySize = WGPUExtent3D{copySizeBlocks[0] * info.blockWidth, copySizeBlocks[1] * info.blockHeight, copySizeBlocks[2]};
    scenario.textureSize = WGPUExtent3D{texSizeBlocks[0] * info.blockWidth, texSizeBlocks[1] * info.blockHeight, texSizeBlocks[2]};

    const uint32_t rowsPerImage = copySizeBlocks[1];
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(copySizeBlocks[0] * info.bytesPerBlock, 256));
    scenario.textureDataLayout = TexelCopyBufferLayout{0, bytesPerRow, rowsPerImage};
    scenario.dataSize = dataBytesForScenario(scenario, scenario.textureDataLayout);

    // For testing width: we copy a (_ x 2 x 2) (in texel blocks) part of a (_ x 3 x 3)
    // (in texel blocks) texture with origin (_, 1, 1) (in texel blocks).
    // Similarly for other coordinates.
    uploadTextureAndVerifyCopy(t, scenario);
}

void runMipLevels(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const MipLevelCase& mipCase = kMipLevelCases[static_cast<size_t>(t.param<int64_t>("mipLevelCaseIndex"))];
    const TextureBlockInfo info = getBlockInfoForTextureFormat(scenario.format);

    scenario.origin = WGPUOrigin3D{
        mipCase.originInBlocks.x * info.blockWidth,
        mipCase.originInBlocks.y * info.blockHeight,
        mipCase.originInBlocks.z,
    };
    scenario.copySize = WGPUExtent3D{
        mipCase.copySizeInBlocks.width * info.blockWidth,
        mipCase.copySizeInBlocks.height * info.blockHeight,
        mipCase.copySizeInBlocks.depthOrArrayLayers,
    };

    const uint32_t rowsPerImage = mipCase.copySizeInBlocks.height + 1;
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(scenario.copySize.width, 256));
    scenario.textureDataLayout = TexelCopyBufferLayout{0, bytesPerRow, rowsPerImage};
    scenario.dataSize = dataBytesForScenario(scenario, scenario.textureDataLayout);
    scenario.mipLevel = mipCase.mipLevel;
    scenario.textureSize = parseTextureSizeParam(t.param<std::string>("textureSize"));
    uploadTextureAndVerifyCopy(t, scenario);
}

void runUnalignedMipLevel0(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario;
    scenario.format = parseTextureFormat(t.param<std::string>("format"));
    scenario.initMethod = parseInitMethod(t.param<std::string>("initMethod"));
    scenario.checkMethod = parseCheckMethod(t.param<std::string>("checkMethod"));
    const UnalignedCopyCase& copyCase = kUnalignedCopyCases[static_cast<size_t>(t.param<int64_t>("copyCaseIndex"))];
    t.skipIfTextureFormatNotSupported(scenario.format);
    if (!deviceHasTextureCompressionUnaligned(t.device())) {
        t.skip("device does not have feature texture-compression-unaligned");
    }

    const TextureBlockInfo info = getBlockInfoForTextureFormat(scenario.format);

    // Unaligned mip level 0: three full blocks plus a one-texel partial edge block in each dimension
    // (physical size four blocks). This is only valid with 'texture-compression-unaligned'.
    scenario.textureSize = WGPUExtent3D{3 * info.blockWidth + 1, 3 * info.blockHeight + 1, 1};

    scenario.origin = WGPUOrigin3D{copyCase.originXInBlocks * info.blockWidth, copyCase.originYInBlocks * info.blockHeight, 0};
    scenario.copySize = WGPUExtent3D{copyCase.widthInBlocks * info.blockWidth, copyCase.heightInBlocks * info.blockHeight, 1};

    const uint32_t rowsPerImage = copyCase.heightInBlocks + 1;
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(scenario.copySize.width, 256));
    scenario.textureDataLayout = TexelCopyBufferLayout{0, bytesPerRow, rowsPerImage};
    scenario.dataSize = dataBytesForScenario(scenario, scenario.textureDataLayout);
    scenario.dimension = WGPUTextureDimension_2D;
    uploadTextureAndVerifyCopy(t, scenario);
}

void runUndefinedParams(AllFeaturesMaxLimitsGpuTest& t) {
    CopyScenario scenario = scenarioFromParams(t);
    const UndefinedParamsCase& undefinedCase =
        kUndefinedParamsCases[static_cast<size_t>(t.param<int64_t>("undefinedCaseIndex"))];

    // Zero will get turned back into undefined later.
    scenario.textureDataLayout = TexelCopyBufferLayout{0, undefinedCase.bytesPerRow, undefinedCase.rowsPerImage};
    scenario.copySize = WGPUExtent3D{undefinedCase.copyWidth, undefinedCase.copyHeight, undefinedCase.copyDepth};
    scenario.dataSize = 2000;
    scenario.textureSize = WGPUExtent3D{
        100,
        undefinedCase.copyHeight + undefinedCase.originY,
        undefinedCase.copyDepth + undefinedCase.originZ,
    };
    // Zeros will get turned back into undefined later.
    scenario.origin = WGPUOrigin3D{undefinedCase.originX, undefinedCase.originY, undefinedCase.originZ};
    scenario.format = WGPUTextureFormat_RGBA8Unorm;
    scenario.changeBeforePass = ChangeBeforePass::Undefined;
    uploadTextureAndVerifyCopy(t, scenario);
}

void runRowsPerImageAndBytesPerRowDepthStencil(AllFeaturesMaxLimitsGpuTest& t) {
    const auto format = parseTextureFormat(t.param<std::string>("format"));
    const auto copyMethod = parseCopyMethod(t.param<std::string>("copyMethod"));
    const auto aspect = parseDepthStencilAspect(t.param<std::string>("aspect"));
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
    const auto format = parseTextureFormat(t.param<std::string>("format"));
    const auto copyMethod = parseCopyMethod(t.param<std::string>("copyMethod"));
    const auto aspect = parseDepthStencilAspect(t.param<std::string>("aspect"));
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

CTS_TEST(g, "compressed_textures,unaligned_mip_level_0")
    .params(unalignedMipLevel0Params)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runUnalignedMipLevel0(t); });

CTS_TEST(g, "undefined_params")
    .params(undefinedParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runUndefinedParams(t); });

CTS_TEST(g, "rowsPerImage_and_bytesPerRow_depth_stencil")
    .params(rowsDepthStencilParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runRowsPerImageAndBytesPerRowDepthStencil(t); });

CTS_TEST(g, "offsets_and_sizes_copy_depth_stencil")
    .params(offsetsDepthStencilParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runOffsetsAndSizesDepthStencil(t); });
