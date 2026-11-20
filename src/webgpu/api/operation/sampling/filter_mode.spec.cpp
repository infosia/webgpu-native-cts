// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/sampling/filter_mode.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes:
//   - kPossiblyRenderablePossiblyFilterableColorTextureFormats (kPossibleColorRenderableTextureFormats
//     filtered to 'float'/'unfilterable-float') is written out in format_info.ts order.
//   - TexelView.fromTexelsAsColors is numberToBits+packBits per texel; texture uploads use
//     queue.writeTexture per mip level instead of upstream's staging-buffer copy.
//   - expectTexelViewComparisonIsOkInTexture uses upstream's default options (maxIntDiff 0, 1 ULP for
//     norm and float formats); every format here is a norm or float format.
//   - 'auto' layouts are written out as the equivalent explicit bind group layout.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texel_view.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,sampling,filter_mode",
    "\nTests the behavior of different filtering modes in minFilter/magFilter/mipmapFilter.\n\n"
    "Note: It's possible these tests duplicated tests under shader/execution/expression/call/builtin/textureXXX.\n"
    "Further, these tests only test encodable/filterable/renderable color formats. Depth, sint, uint,\n"
    "and compressed formats are not tested.\n");

// kPossiblyRenderablePossiblyFilterableColorTextureFormats.
constexpr std::array<WGPUTextureFormat, 20> kPossiblyRenderablePossiblyFilterableColorTextureFormats = {{
    WGPUTextureFormat_R8Unorm,
    WGPUTextureFormat_RG8Unorm,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RGBA8UnormSrgb,
    WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb,
    WGPUTextureFormat_R16Unorm,
    WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_R16Float,
    WGPUTextureFormat_RG16Unorm,
    WGPUTextureFormat_RG16Snorm,
    WGPUTextureFormat_RG16Float,
    WGPUTextureFormat_RGBA16Unorm,
    WGPUTextureFormat_RGBA16Snorm,
    WGPUTextureFormat_RGBA16Float,
    WGPUTextureFormat_R32Float,
    WGPUTextureFormat_RG32Float,
    WGPUTextureFormat_RGBA32Float,
    WGPUTextureFormat_RGB10A2Unorm,
    WGPUTextureFormat_RG11B10Ufloat,
}};

std::vector<Value> formatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kPossiblyRenderablePossiblyFilterableColorTextureFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

std::vector<Value> addressModeValues() {
    return {Value("clamp-to-edge"), Value("repeat"), Value("mirror-repeat")};
}

WGPUAddressMode parseAddressMode(std::string_view value) {
    if (value == "clamp-to-edge") {
        return WGPUAddressMode_ClampToEdge;
    }
    if (value == "repeat") {
        return WGPUAddressMode_Repeat;
    }
    if (value == "mirror-repeat") {
        return WGPUAddressMode_MirrorRepeat;
    }
    std::abort();
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

using Color = std::array<double, 4>;

// Simple checkerboard 2x2 texture used as a base for the sampling.
constexpr uint32_t kCheckerTextureSize = 2;
constexpr std::array<Color, 4> kCheckerTextureData = {{
    {{1.0, 1.0, 1.0, 1.0}},
    {{0.0, 0.0, 0.0, 1.0}},
    {{0.0, 0.0, 0.0, 1.0}},
    {{1.0, 1.0, 1.0, 1.0}},
}};

// --- Expected results (see the diagrams in the upstream file). ---

constexpr uint32_t kNearestRenderSize = 6;
using NearestTable = std::array<std::array<int, kNearestRenderSize>, kNearestRenderSize>;

constexpr NearestTable kNearestURepeatVRepeat = {{
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
}};
constexpr NearestTable kNearestURepeatVClamped = {{
    {{1, 0, 1, 0, 1, 0}},
    {{1, 0, 1, 0, 1, 0}},
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
    {{0, 1, 0, 1, 0, 1}},
    {{0, 1, 0, 1, 0, 1}},
}};
constexpr NearestTable kNearestURepeatVMirror = {{
    {{0, 1, 0, 1, 0, 1}},
    {{1, 0, 1, 0, 1, 0}},
    {{1, 0, 1, 0, 1, 0}},
    {{0, 1, 0, 1, 0, 1}},
    {{0, 1, 0, 1, 0, 1}},
    {{1, 0, 1, 0, 1, 0}},
}};
constexpr NearestTable kNearestUClampedVRepeat = {{
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
}};
constexpr NearestTable kNearestUClampedVClamped = {{
    {{1, 1, 1, 0, 0, 0}},
    {{1, 1, 1, 0, 0, 0}},
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
    {{0, 0, 0, 1, 1, 1}},
    {{0, 0, 0, 1, 1, 1}},
}};
constexpr NearestTable kNearestUClampedVMirror = {{
    {{0, 0, 0, 1, 1, 1}},
    {{1, 1, 1, 0, 0, 0}},
    {{1, 1, 1, 0, 0, 0}},
    {{0, 0, 0, 1, 1, 1}},
    {{0, 0, 0, 1, 1, 1}},
    {{1, 1, 1, 0, 0, 0}},
}};
constexpr NearestTable kNearestUMirrorVRepeat = {{
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
}};
constexpr NearestTable kNearestUMirrorVClamped = {{
    {{0, 1, 1, 0, 0, 1}},
    {{0, 1, 1, 0, 0, 1}},
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
    {{1, 0, 0, 1, 1, 0}},
    {{1, 0, 0, 1, 1, 0}},
}};
constexpr NearestTable kNearestUMirrorVMirror = {{
    {{1, 0, 0, 1, 1, 0}},
    {{0, 1, 1, 0, 0, 1}},
    {{0, 1, 1, 0, 0, 1}},
    {{1, 0, 0, 1, 1, 0}},
    {{1, 0, 0, 1, 1, 0}},
    {{0, 1, 1, 0, 0, 1}},
}};

constexpr uint32_t kLinearRenderSize = 4;
using LinearTable = std::array<std::array<int, kLinearRenderSize>, kLinearRenderSize>;

constexpr LinearTable kLinearURepeatVRepeat = {{
    {{10, 6, 10, 6}},
    {{10, 6, 10, 6}},
    {{6, 10, 6, 10}},
    {{6, 10, 6, 10}},
}};
constexpr LinearTable kLinearURepeatVClamped = {{
    {{12, 4, 12, 4}},
    {{12, 4, 12, 4}},
    {{4, 12, 4, 12}},
    {{4, 12, 4, 12}},
}};
constexpr LinearTable kLinearURepeatVMirror = {{
    {{4, 12, 4, 12}},
    {{12, 4, 12, 4}},
    {{4, 12, 4, 12}},
    {{12, 4, 12, 4}},
}};
constexpr LinearTable kLinearUClampedVRepeat = {{
    {{12, 12, 4, 4}},
    {{12, 12, 4, 4}},
    {{4, 4, 12, 12}},
    {{4, 4, 12, 12}},
}};
constexpr LinearTable kLinearUClampedVClamped = {{
    {{16, 16, 0, 0}},
    {{16, 16, 0, 0}},
    {{0, 0, 16, 16}},
    {{0, 0, 16, 16}},
}};
constexpr LinearTable kLinearUClampedVMirror = {{
    {{0, 0, 16, 16}},
    {{16, 16, 0, 0}},
    {{0, 0, 16, 16}},
    {{16, 16, 0, 0}},
}};
constexpr LinearTable kLinearUMirrorVRepeat = {{
    {{4, 12, 4, 12}},
    {{4, 12, 4, 12}},
    {{12, 4, 12, 4}},
    {{12, 4, 12, 4}},
}};
constexpr LinearTable kLinearUMirrorVClamped = {{
    {{0, 16, 0, 16}},
    {{0, 16, 0, 16}},
    {{16, 0, 16, 0}},
    {{16, 0, 16, 0}},
}};
constexpr LinearTable kLinearUMirrorVMirror = {{
    {{16, 0, 16, 0}},
    {{0, 16, 0, 16}},
    {{16, 0, 16, 0}},
    {{0, 16, 0, 16}},
}};

// Selects the table for (addressModeU, addressModeV) among the 3x3 tables, ordered
// [clamp-to-edge, repeat, mirror-repeat] on each axis.
template <typename Table>
const Table& selectTable(
    std::string_view addressModeU,
    std::string_view addressModeV,
    const std::array<std::array<const Table*, 3>, 3>& tables) {
    const auto index = [](std::string_view mode) -> size_t {
        if (mode == "clamp-to-edge") {
            return 0;
        }
        if (mode == "repeat") {
            return 1;
        }
        if (mode == "mirror-repeat") {
            return 2;
        }
        std::abort();
    };
    return *tables[index(addressModeU)][index(addressModeV)];
}

// A TexelView.fromTexelsAsColors expectation: per-texel colors in `format`.
struct ExpectedColors {
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<Color> colors;

    const Color& at(uint32_t x, uint32_t y) const { return colors[static_cast<size_t>(y) * width + x]; }
};

// expectedNearestColors / expectedLinearColors (expectedColors).
ExpectedColors expectedColors(
    WGPUTextureFormat format,
    std::string_view filterMode,
    std::string_view addressModeU,
    std::string_view addressModeV) {
    ExpectedColors expected;
    expected.format = format;
    if (filterMode == "nearest") {
        const std::array<std::array<const NearestTable*, 3>, 3> tables = {{
            {{&kNearestUClampedVClamped, &kNearestUClampedVRepeat, &kNearestUClampedVMirror}},
            {{&kNearestURepeatVClamped, &kNearestURepeatVRepeat, &kNearestURepeatVMirror}},
            {{&kNearestUMirrorVClamped, &kNearestUMirrorVRepeat, &kNearestUMirrorVMirror}},
        }};
        const NearestTable& table = selectTable(addressModeU, addressModeV, tables);
        expected.width = kNearestRenderSize;
        expected.height = kNearestRenderSize;
        for (uint32_t y = 0; y < kNearestRenderSize; ++y) {
            for (uint32_t x = 0; x < kNearestRenderSize; ++x) {
                const double c = table[y][x];
                expected.colors.push_back(Color{{c, c, c, 1.0}});
            }
        }
    } else {
        const std::array<std::array<const LinearTable*, 3>, 3> tables = {{
            {{&kLinearUClampedVClamped, &kLinearUClampedVRepeat, &kLinearUClampedVMirror}},
            {{&kLinearURepeatVClamped, &kLinearURepeatVRepeat, &kLinearURepeatVMirror}},
            {{&kLinearUMirrorVClamped, &kLinearUMirrorVRepeat, &kLinearUMirrorVMirror}},
        }};
        const LinearTable& table = selectTable(addressModeU, addressModeV, tables);
        expected.width = kLinearRenderSize;
        expected.height = kLinearRenderSize;
        for (uint32_t y = 0; y < kLinearRenderSize; ++y) {
            for (uint32_t x = 0; x < kLinearRenderSize; ++x) {
                const double c = table[y][x] / 16.0;
                expected.colors.push_back(Color{{c, c, c, 1.0}});
            }
        }
    }
    return expected;
}

// TexelView.fromTexelsAsColors(format, ...) bytes: tightly packed rows.
std::vector<uint8_t> encodeColors(WGPUTextureFormat format, const std::vector<Color>& colors) {
    const TexelRepresentation& repr = texelRepresentation(format);
    std::vector<uint8_t> bytes;
    bytes.reserve(colors.size() * repr.bytesPerBlock);
    for (const Color& color : colors) {
        TexelComponents components;
        components.values = color;
        const std::vector<uint8_t> texel = repr.packBits(repr.numberToBits(components));
        bytes.insert(bytes.end(), texel.begin(), texel.end());
    }
    return bytes;
}

// createTextureFromTexelViews: one mip level per entry of `levels` (each a full set of texels for
// that level's size).
WGPUTexture createTextureFromColors(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUExtent3D size,
    WGPUTextureUsage usage,
    const std::vector<std::vector<Color>>& levels) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = size;
    desc.format = format;
    desc.usage = usage | WGPUTextureUsage_CopyDst;
    desc.mipLevelCount = static_cast<uint32_t>(levels.size());
    WGPUTexture texture = t.createTextureTracked(desc);

    const uint32_t bytesPerBlock = texelRepresentation(format).bytesPerBlock;
    for (uint32_t mipLevel = 0; mipLevel < levels.size(); ++mipLevel) {
        const WGPUExtent3D mipSize = physicalMipSize(size, WGPUTextureDimension_2D, mipLevel);
        const std::vector<uint8_t> bytes = encodeColors(format, levels[mipLevel]);
        WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
        layout.offset = 0;
        layout.bytesPerRow = mipSize.width * bytesPerBlock;
        layout.rowsPerImage = mipSize.height;
        t.queueWriteTexture(texture, mipSize, layout, bytes.data(), bytes.size(), mipLevel);
    }
    return texture;
}

std::vector<Color> fillColors(uint32_t count, const Color& color) {
    return std::vector<Color>(count, color);
}

WGPUBindGroupLayout createFilterBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Fragment;
    entries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Fragment;
    entries[1].texture.sampleType = WGPUTextureSampleType_Float;
    entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    entries[1].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

struct FilterPipeline {
    WGPURenderPipeline pipeline = nullptr;
    WGPUBindGroupLayout bindGroupLayout = nullptr;
};

FilterPipeline createFilterPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view code,
    WGPUTextureFormat renderTargetFormat) {
    FilterPipeline result;
    result.bindGroupLayout = createFilterBindGroupLayout(t);
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &result.bindGroupLayout;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = renderTargetFormat;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = stringView("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = stringView("vs_main");
    desc.fragment = &fragment;
    result.pipeline = t.createRenderPipelineTracked(desc);
    return result;
}

WGPUBindGroup createFilterBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUSampler sampler,
    WGPUTexture sampleTexture) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    std::array<WGPUBindGroupEntry, 2> entries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].sampler = sampler;
    entries[1].binding = 1;
    entries[1].textureView = t.createViewTracked(sampleTexture, viewDesc);

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

void renderAndSubmit(
    AllFeaturesMaxLimitsGpuTest& t,
    const FilterPipeline& pipeline,
    WGPUBindGroup bindGroup,
    WGPUTexture renderTexture,
    uint32_t vertexCount,
    uint32_t instanceCount) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = t.createViewTracked(renderTexture, viewDesc);
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPUTexture createRenderTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format, WGPUExtent3D size) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.format = format;
    desc.size = size;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

struct RenderResult {
    WGPUTexture texture = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_Undefined;
};

// FilterModeTest.runFilterRenderPipeline.
RenderResult runFilterRenderPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUSampler sampler,
    std::string_view code,
    WGPUTextureFormat format,
    uint32_t renderSize,
    uint32_t vertexCount,
    uint32_t instanceCount) {
    WGPUTextureFormat renderTargetFormat = format;
    if (!t.isTextureFormatColorRenderable(format)) {
        // If the format is not renderable, we use rgba32float as the render target format
        // to verify the result, which is always renderable.
        renderTargetFormat = WGPUTextureFormat_RGBA32Float;
    }
    WGPUTexture sampleTexture = createTextureFromColors(
        t,
        format,
        WGPUExtent3D{kCheckerTextureSize, kCheckerTextureSize, 1},
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
        {std::vector<Color>(kCheckerTextureData.begin(), kCheckerTextureData.end())});
    WGPUTexture renderTexture = createRenderTexture(t, renderTargetFormat, WGPUExtent3D{renderSize, renderSize, 1});
    const FilterPipeline pipeline = createFilterPipeline(t, code, renderTargetFormat);
    WGPUBindGroup bindGroup = createFilterBindGroup(t, pipeline.bindGroupLayout, sampler, sampleTexture);
    renderAndSubmit(t, pipeline, bindGroup, renderTexture, vertexCount, instanceCount);
    return RenderResult{renderTexture, renderTargetFormat};
}

// makeTexelViewComparer + comparePerComponent with the default options of
// expectTexelViewComparisonIsOkInTexture (maxIntDiff 0, 1 ULP for norm and float formats), for one
// texel: `actual` holds the texel bytes, `expected` a color fed through fromTexelsAsColors.
std::optional<std::string> compareTexel(
    WGPUTextureFormat format,
    const std::vector<uint8_t>& actual,
    const Color& expected,
    uint32_t x,
    uint32_t y) {
    const std::string_view identifier = textureFormatIdentifier(format);
    const bool fmtIsInt = identifier.find("int") != std::string_view::npos;
    const TexelRepresentation& repr = texelRepresentation(format);
    const TexelComponents act = repr.bitsToNumber(repr.unpackBits(actual.data(), actual.size()));
    for (TexelComponent component : repr.componentOrder) {
        const uint32_t index = static_cast<uint32_t>(component);
        bool ok = false;
        std::ostringstream detail;
        if (fmtIsInt) {
            ok = texelComponentEqual(act.values[index], expected[index], 0.0);
            detail << "expected " << expected[index] << ", got " << act.values[index];
        } else {
            // The actual texel's bits decode exactly and re-encode to the same bits, so the
            // round-trip through ulpFromZero equals upstream's bitsToULPFromZero(unpackBits(...)).
            const int64_t actUlp = repr.ulpFromZero(index, act.values[index]);
            const int64_t expUlp = repr.ulpFromZero(index, expected[index]);
            const int64_t diff = actUlp - expUlp;
            ok = (diff < 0 ? -diff : diff) <= 1;
            detail << "expected " << expected[index] << " (" << expUlp << " normal-ULPs from zero), got "
                   << act.values[index] << " (" << actUlp << " normal-ULPs from zero), tolerance +-1 normal-ULPs";
        }
        if (!ok) {
            std::ostringstream message;
            message << identifier << " texel mismatch at (" << x << ", " << y << ", 0) component "
                    << "RGBA"[index] << ": " << detail.str();
            return message.str();
        }
    }
    return std::nullopt;
}

struct TextureReadback {
    WGPUBuffer buffer = nullptr;
    uint32_t bytesPerRow = 0;
    uint64_t byteLength = 0;
};

// copyWholeTextureToNewBufferSimple(t, texture, 0) for a 2d texture of `size`.
TextureReadback copyWholeTextureToNewBuffer(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    WGPUExtent3D size) {
    TextureReadback readback;
    const uint32_t bytesPerBlock = texelRepresentation(format).bytesPerBlock;
    readback.bytesPerRow = static_cast<uint32_t>(alignTo(static_cast<uint64_t>(size.width) * bytesPerBlock, kBytesPerRowAlignment));
    readback.byteLength = static_cast<uint64_t>(readback.bytesPerRow) * size.height;
    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = alignTo(readback.byteLength, kBufferCopyAlignment);
    bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    readback.buffer = t.createBufferTracked(bufferDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, readback.buffer, readback.bytesPerRow, size);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    return readback;
}

// ttu.expectTexelViewComparisonIsOkInTexture(t, { texture }, expected, [width, height]).
void expectTexelViewComparisonIsOkInTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const ExpectedColors& expected) {
    const WGPUExtent3D size = WGPUExtent3D{expected.width, expected.height, 1};
    const TextureReadback readback = copyWholeTextureToNewBuffer(t, texture, expected.format, size);
    t.expectGPUBufferValuesPassCheck(
        readback.buffer,
        [&expected, &readback, size](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            TexelViewConfig config;
            config.bytesPerRow = readback.bytesPerRow;
            config.rowsPerImage = size.height;
            config.subrectSize = size;
            const TexelView view = TexelView::fromTextureDataByReference(expected.format, actual, len, config);
            for (uint32_t y = 0; y < size.height; ++y) {
                for (uint32_t x = 0; x < size.width; ++x) {
                    if (std::optional<std::string> failure =
                            compareTexel(expected.format, view.bytes(x, y, 0), expected.at(x, y), x, y)) {
                        return failure;
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(readback.byteLength));
}

WGPUSampler createSampler(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view addressModeU,
    std::string_view addressModeV,
    bool setMagFilter,
    WGPUFilterMode filter) {
    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    desc.addressModeU = parseAddressMode(addressModeU);
    desc.addressModeV = parseAddressMode(addressModeV);
    if (setMagFilter) {
        desc.magFilter = filter;
    } else {
        desc.minFilter = filter;
    }
    return t.createSamplerTracked(desc);
}

constexpr std::string_view kMagFilterNearestShader = R"(
      @group(0) @binding(0) var s : sampler;
      @group(0) @binding(1) var t : texture_2d<f32>;

      struct VertexOut {
        @builtin(position) pos: vec4f,
        @location(0) uv: vec2f,
      };

      @vertex
      fn vs_main(@builtin(vertex_index) vi : u32,
                 @builtin(instance_index) ii: u32) -> VertexOut {
        const grid = vec2f(6, 6);
        const posBases = array(
          vec2f(1, 1), vec2f(1, -1), vec2f(-1, -1),
          vec2f(1, 1), vec2f(-1, -1), vec2f(-1, 1),
        );
        const uvBases = array(
          vec2f(1., 0.), vec2f(1., 1.), vec2f(0., 1.),
          vec2f(1., 0.), vec2f(0., 1.), vec2f(0., 0.),
        );

        // Compute the offset of instance plane.
        let cell = vec2f(f32(ii) % grid.x, floor(f32(ii) / grid.y));
        let cellOffset = cell / grid * 2;
        let pos = (posBases[vi] + 1) / grid - 1 + cellOffset;

        // Compute the offset of the UVs.
        let uvBase = uvBases[vi] * 0.25 + vec2f(-0.875, 1.625);
        const uvPerPixelOffset = vec2f(0.5, -0.5);
        return VertexOut(vec4f(pos, 0.0, 1.0), uvBase + uvPerPixelOffset * cell);
      }

      @fragment
      fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
        return textureSample(t, s, uv);
      }
      )";

constexpr std::string_view kMagFilterLinearShader = R"(
      @group(0) @binding(0) var s : sampler;
      @group(0) @binding(1) var t : texture_2d<f32>;

      struct VertexOut {
        @builtin(position) pos: vec4f,
        @location(0) uv: vec2f,
      };

      @vertex
      fn vs_main(@builtin(vertex_index) vi : u32,
                 @builtin(instance_index) ii: u32) -> VertexOut {
        const grid = vec2f(4, 4);
        const posBases = array(
          vec2f(1, 1), vec2f(1, -1), vec2f(-1, -1),
          vec2f(1, 1), vec2f(-1, -1), vec2f(-1, 1),
        );
        const uvBases = array(
          vec2f(1., 0.), vec2f(1., 1.), vec2f(0., 1.),
          vec2f(1., 0.), vec2f(0., 1.), vec2f(0., 0.),
        );

        // Compute the offset of instance plane.
        let cell = vec2f(f32(ii) % grid.x, floor(f32(ii) / grid.y));
        let cellOffset = cell / grid * 2;
        let pos = (posBases[vi] + 1) / grid - 1 + cellOffset;

        // Compute the offset of the UVs.
        const uOffsets = array(0., 0.75, 2., 2.75);
        const vOffsets = array(0., 1., 1.75, 2.75);
        let uvBase = uvBases[vi] * 0.25 + vec2f(-1., 1.75);
        let uvPixelOffset = vec2f(uOffsets[u32(cell.x)], -vOffsets[u32(cell.y)]);
        return VertexOut(vec4f(pos, 0.0, 1.0), uvBase + uvPixelOffset);
      }

      @fragment
      fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
        return textureSample(t, s, uv);
      }
      )";

constexpr std::string_view kMinFilterNearestShader = R"(
      @group(0) @binding(0) var s : sampler;
      @group(0) @binding(1) var t : texture_2d<f32>;

      struct VertexOut {
        @builtin(position) pos: vec4f,
        @location(0) uv: vec2f,
      };

      @vertex
      fn vs_main(@builtin(vertex_index) vi : u32,
                 @builtin(instance_index) ii: u32) -> VertexOut {
        const grid = vec2f(6, 6);
        const posBases = array(
          vec2f(.5, .5), vec2f(.5, -.5), vec2f(-.5, -.5),
          vec2f(.5, .5), vec2f(-.5, -.5), vec2f(-.5, .5),
        );
        // Choose UVs so that the quad ends up being the 6x6 texture.
        const uvBases = array(
          vec2f(2., -1.), vec2f(2., 2.), vec2f(-1., 2.),
          vec2f(2., -1.), vec2f(-1., 2.), vec2f(-1., -1.),
        );

        let cell = vec2f(f32(ii) % grid.x, floor(f32(ii) / grid.y));

        // Compute the offset of instance plane (pre-grid transformation).
        const constantPlaneOffset = vec2f(5. / 12., 5. / 12.);
        const perPixelOffset = vec2f(1. / 6., 1. / 6.);
        let posBase = posBases[vi] + constantPlaneOffset - perPixelOffset * cell;

        // Apply the grid transformation.
        let cellOffset = cell / grid * 2;
        let absPos = (posBase + 1) / grid - 1 + cellOffset;

        return VertexOut(vec4f(absPos, 0.0, 1.0), uvBases[vi]);
      }

      @fragment
      fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
        return textureSample(t, s, uv);
      }
      )";

constexpr std::string_view kMinFilterLinearShader = R"(
      @group(0) @binding(0) var s : sampler;
      @group(0) @binding(1) var t : texture_2d<f32>;

      struct VertexOut {
        @builtin(position) pos: vec4f,
        @location(0) uv: vec2f,
      };

      @vertex
      fn vs_main(@builtin(vertex_index) vi : u32,
                 @builtin(instance_index) ii: u32) -> VertexOut {
        const grid = vec2f(4, 4);
        const posBases = array(
          vec2f(.5, .5), vec2f(.5, -.5), vec2f(-.5, -.5),
          vec2f(.5, .5), vec2f(-.5, -.5), vec2f(-.5, .5),
        );
        // Choose UVs so that the quad ends up being the 8x8 texture.
        const uvBases = array(
          vec2f(2.5, -1.5), vec2f(2.5, 2.5), vec2f(-1.5, 2.5),
          vec2f(2.5, -1.5), vec2f(-1.5, 2.5), vec2f(-1.5, -1.5),
        );

        let cell = vec2f(f32(ii) % grid.x, floor(f32(ii) / grid.y));

        // Compute the offset of instance plane (pre-grid transformation).
        const constantPlaneOffset = vec2f(11. / 32., 11. / 32.);
        const xOffsets = array(0., 3. / 16., 1. / 2., 11. / 16.);
        const yOffsets = array(0., 1. / 4., 7. / 16., 11. / 16.);
        let pixelOffset = vec2f(xOffsets[u32(cell.x)], yOffsets[u32(cell.y)]);
        let posBase = posBases[vi] + constantPlaneOffset - pixelOffset;

        // Compute the offset of instance plane.
        let cellOffset = cell / grid * 2;
        let absPos = (posBase + 1) / grid - 1 + cellOffset;

        return VertexOut(vec4f(absPos, 0.0, 1.0), uvBases[vi]);
      }

      @fragment
      fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
        return textureSample(t, s, uv);
      }
      )";

constexpr std::string_view kMipmapFilterShader = R"(
      @group(0) @binding(0) var s : sampler;
      @group(0) @binding(1) var t : texture_2d<f32>;

      struct VertexOut {
        @builtin(position) pos: vec4f,
        @location(0) uv: vec2f,
      };

      @vertex
      fn vs_main(@builtin(vertex_index) vi : u32,
                 @builtin(instance_index) ii: u32) -> VertexOut {
        const grid = vec2f(8., 1.);
        const pos = array(
          vec2f( 1.0,  1.0), vec2f( 1.0, -1.0), vec2f(-1.0, -1.0),
          vec2f( 1.0,  1.0), vec2f(-1.0, -1.0), vec2f(-1.0,  1.0),
        );
        const uv = array(
          vec2f(1., 0.), vec2f(1., 1.), vec2f(0., 1.),
          vec2f(1., 0.), vec2f(0., 1.), vec2f(0., 0.),
        );

        // Compute the offset of the plane.
        let cell = vec2f(f32(ii) % grid.x, 0.);
        let cellOffset = cell / grid * 2;
        let absPos = (pos[vi] + 1) / grid - 1 + cellOffset;
        let uvFactor = (1. / 8.) * (1 + (f32(ii) / (grid.x - 1)));
        return VertexOut(vec4f(absPos, 0.0, 1.0), uv[vi] * uvFactor);
      }

      @fragment
      fn fs_main(@location(0) uv : vec2f) -> @location(0) vec4f {
        return textureSample(t, s, uv);
      }
      )";

void skipIfTextureFormatNotFilterable(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    // isTextureFormatFilterable: every format here is 'float' except the 32-bit floats, which are
    // 'unfilterable-float' and filterable only with float32-filterable.
    const bool is32Float = format == WGPUTextureFormat_R32Float || format == WGPUTextureFormat_RG32Float
        || format == WGPUTextureFormat_RGBA32Float;
    if (is32Float && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Float32Filterable)) {
        t.skip(std::string(textureFormatIdentifier(format)) + " is not filterable");
    }
}

// Shared body of the magFilter/minFilter tests.
void runAddressModeFilterTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool magFilter,
    std::string_view filterMode,
    std::string_view code) {
    const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
    const std::string addressModeU = t.param<std::string>("addressModeU");
    const std::string addressModeV = t.param<std::string>("addressModeV");
    t.skipIfTextureFormatNotSupported(format);
    skipIfTextureFormatNotFilterable(t, format);
    const bool linear = filterMode == "linear";
    WGPUSampler sampler = createSampler(
        t, addressModeU, addressModeV, magFilter, linear ? WGPUFilterMode_Linear : WGPUFilterMode_Nearest);
    const uint32_t renderSize = linear ? kLinearRenderSize : kNearestRenderSize;
    const uint32_t vertexCount = 6;
    const uint32_t instanceCount = renderSize * renderSize;
    const RenderResult render =
        runFilterRenderPipeline(t, sampler, code, format, renderSize, vertexCount, instanceCount);
    expectTexelViewComparisonIsOkInTexture(
        t, render.texture, expectedColors(render.format, filterMode, addressModeU, addressModeV));
}

ParamsBuilder addressModeParams(ParamsBuilder u) {
    return u.combine("format", formatValues())
        .beginSubcases()
        .combine("addressModeU", addressModeValues())
        .combine("addressModeV", addressModeValues());
}

CTS_TEST(g, "magFilter,nearest")
    .desc(
        "\n  Test that for filterable formats, magFilter 'nearest' mode correctly modifies the sampling.\n"
        "    - format= {<filterable formats>}\n"
        "    - addressModeU= {'clamp-to-edge', 'repeat', 'mirror-repeat'}\n"
        "    - addressModeV= {'clamp-to-edge', 'repeat', 'mirror-repeat'}\n  ")
    .params([](ParamsBuilder u) { return addressModeParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runAddressModeFilterTest(t, /*magFilter=*/true, "nearest", kMagFilterNearestShader);
    });

CTS_TEST(g, "magFilter,linear")
    .desc(
        "\n  Test that for filterable formats, magFilter 'linear' mode correctly modifies the sampling.\n"
        "    - format= {<filterable formats>}\n"
        "    - addressModeU= {'clamp-to-edge', 'repeat', 'mirror-repeat'}\n"
        "    - addressModeV= {'clamp-to-edge', 'repeat', 'mirror-repeat'}\n  ")
    .params([](ParamsBuilder u) { return addressModeParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runAddressModeFilterTest(t, /*magFilter=*/true, "linear", kMagFilterLinearShader);
    });

CTS_TEST(g, "minFilter,nearest")
    .desc(
        "\n  Test that for filterable formats, minFilter 'nearest' mode correctly modifies the sampling.\n"
        "    - format= {<filterable formats>}\n"
        "    - addressModeU= {'clamp-to-edge', 'repeat', 'mirror-repeat'}\n"
        "    - addressModeV= {'clamp-to-edge', 'repeat', 'mirror-repeat'}\n  ")
    .params([](ParamsBuilder u) { return addressModeParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runAddressModeFilterTest(t, /*magFilter=*/false, "nearest", kMinFilterNearestShader);
    });

CTS_TEST(g, "minFilter,linear")
    .desc(
        "\n  Test that for filterable formats, minFilter 'linear' mode correctly modifies the sampling.\n"
        "    - format= {<filterable formats>}\n"
        "    - addressModeU= {'clamp-to-edge', 'repeat', 'mirror-repeat'}\n"
        "    - addressModeV= {'clamp-to-edge', 'repeat', 'mirror-repeat'}\n  ")
    .params([](ParamsBuilder u) { return addressModeParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runAddressModeFilterTest(t, /*magFilter=*/false, "linear", kMinFilterLinearShader);
    });

CTS_TEST(g, "mipmapFilter")
    .desc(
        "\n  Test that for filterable formats, mipmapFilter modes correctly modifies the sampling.\n"
        "    - format= {<filterable formats>}\n"
        "    - filterMode= {'nearest', 'linear'}\n  ")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues())
            .beginSubcases()
            .combine("filterMode", {Value("nearest"), Value("linear")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string filterMode = t.param<std::string>("filterMode");
        t.skipIfTextureFormatNotSupported(format);
        skipIfTextureFormatNotFilterable(t, format);
        WGPUTextureFormat renderTargetFormat = format;
        if (!t.isTextureFormatColorRenderable(format)) {
            // If the format is not renderable, we use rgba32float as the render target format
            // to verify the result, which is always renderable.
            renderTargetFormat = WGPUTextureFormat_RGBA32Float;
        }
        // Takes a 8x8/4x4 mipmapped texture and renders it on multiple quads with different UVs such
        // that each instanced quad from left to right emulates moving the quad further and further from
        // the camera. Each quad is then rendered to a single pixel in a 1-dimensional texture. Since
        // the 8x8 is fully black and the 4x4 is fully white, we should see the pixels increase in
        // brightness from left to right when sampling linearly, and jump from black to white when
        // sampling for the nearest mip level.
        constexpr uint32_t kTextureSize = 8;
        constexpr uint32_t kRenderSize = 8;

        WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        samplerDesc.mipmapFilter =
            filterMode == "linear" ? WGPUMipmapFilterMode_Linear : WGPUMipmapFilterMode_Nearest;
        WGPUSampler sampler = t.createSamplerTracked(samplerDesc);

        const WGPUExtent3D textureSize = WGPUExtent3D{kTextureSize, 1, 1};
        WGPUTexture sampleTexture = createTextureFromColors(
            t,
            format,
            textureSize,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
            {
                fillColors(physicalMipSize(textureSize, WGPUTextureDimension_2D, 0).width, Color{{0.0, 0.0, 0.0, 1.0}}),
                fillColors(physicalMipSize(textureSize, WGPUTextureDimension_2D, 1).width, Color{{1.0, 1.0, 1.0, 1.0}}),
            });
        const WGPUExtent3D renderSize = WGPUExtent3D{kRenderSize, 1, 1};
        WGPUTexture renderTexture = createRenderTexture(t, renderTargetFormat, renderSize);
        const FilterPipeline pipeline = createFilterPipeline(t, kMipmapFilterShader, renderTargetFormat);
        WGPUBindGroup bindGroup = createFilterBindGroup(t, pipeline.bindGroupLayout, sampler, sampleTexture);
        renderAndSubmit(t, pipeline, bindGroup, renderTexture, 6, kRenderSize);

        // Since mipmap filtering varies across different backends, we verify that the result exhibits
        // filtered characteristics without strict value equalities via copies to a buffer.
        const TextureReadback readback = copyWholeTextureToNewBuffer(t, renderTexture, renderTargetFormat, renderSize);
        t.expectGPUBufferValuesPassCheck(
            readback.buffer,
            [renderTargetFormat, renderSize, &filterMode](
                const uint8_t* actual, size_t len) -> std::optional<std::string> {
                // Convert the buffer to texel view so we can do comparisons.
                const TextureCopyLayout layout =
                    getTextureCopyLayout(renderTargetFormat, WGPUTextureDimension_2D, renderSize);
                TexelViewConfig config;
                config.bytesPerRow = layout.bytesPerRow;
                config.rowsPerImage = layout.rowsPerImage;
                config.subrectOrigin = WGPUOrigin3D{0, 0, 0};
                config.subrectSize = renderSize;
                const TexelView view = TexelView::fromTextureDataByReference(renderTargetFormat, actual, len, config);
                const auto describe = [&view]() {
                    std::ostringstream out;
                    for (uint32_t x = 0; x < kRenderSize; ++x) {
                        out << (x == 0 ? "" : ", ") << view.color(x, 0, 0).values[0];
                    }
                    return out.str();
                };

                // We only check the R component for the conditions, since all components should be
                // equal if specified in the format.
                if (filterMode == "linear") {
                    // For 'linear' mode, we check that the resulting 1d image is monotonically increasing.
                    for (uint32_t x = 1; x < kRenderSize; ++x) {
                        const double ri = view.color(x - 1, 0, 0).values[0];
                        const double rj = view.color(x, 0, 0).values[0];
                        if (ri >= rj) {
                            return "Linear filtering on mipmaps should be a monotonically increasing sequence:\n"
                                + describe();
                        }
                    }
                } else {
                    // For 'nearest' mode, we check that the resulting 1d image changes from 0.0 to 1.0
                    // exactly once.
                    uint32_t changes = 0;
                    for (uint32_t x = 1; x < kRenderSize; ++x) {
                        const double ri = view.color(x - 1, 0, 0).values[0];
                        const double rj = view.color(x, 0, 0).values[0];
                        if (ri != rj) {
                            changes++;
                        }
                    }
                    if (changes != 1) {
                        std::ostringstream message;
                        message << "Nearest filtering on mipmaps should change exactly once but found (" << changes
                                << "):\n"
                                << describe();
                        return message.str();
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(readback.byteLength));
    });

} // namespace
