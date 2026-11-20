// Ported from gpuweb/cts src/webgpu/api/operation/rendering/color_target_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"
#include "webgpu/util/texel_data.h"
#include "webgpu/util/texture_layout.h"
#include "webgpu/util/texture_test_utils.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,color_target_state",
    "Color target state rendering tests.");

constexpr uint32_t kWidth = 1;
constexpr uint32_t kHeight = 1;
constexpr uint32_t kBytesPerPixel = 4;

constexpr std::string_view kFullscreenVertexShader = R"(
@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
  var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
      vec2<f32>(-1.0, -3.0), vec2<f32>(3.0, 1.0), vec2<f32>(-1.0, 1.0));
  return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
}
)";

constexpr std::string_view kUniformColorFragmentShader = R"(
struct Params { color : vec4<f32> };
@group(0) @binding(0) var<uniform> params : Params;

@fragment fn main() -> @location(0) vec4<f32> {
  return params.color;
}
)";

constexpr std::array<WGPUColorWriteMask, 16> kColorWriteCombinations = {{
    WGPUColorWriteMask_None,
    WGPUColorWriteMask_Red,
    WGPUColorWriteMask_Green,
    WGPUColorWriteMask_Blue,
    WGPUColorWriteMask_Alpha,
    WGPUColorWriteMask_Red | WGPUColorWriteMask_Green,
    WGPUColorWriteMask_Red | WGPUColorWriteMask_Blue,
    WGPUColorWriteMask_Red | WGPUColorWriteMask_Alpha,
    WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue,
    WGPUColorWriteMask_Green | WGPUColorWriteMask_Alpha,
    WGPUColorWriteMask_Blue | WGPUColorWriteMask_Alpha,
    WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue,
    WGPUColorWriteMask_Red | WGPUColorWriteMask_Green | WGPUColorWriteMask_Alpha,
    WGPUColorWriteMask_Red | WGPUColorWriteMask_Blue | WGPUColorWriteMask_Alpha,
    WGPUColorWriteMask_Green | WGPUColorWriteMask_Blue | WGPUColorWriteMask_Alpha,
    WGPUColorWriteMask_All,
}};

struct PipelineForTest {
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
};

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

std::vector<Value> colorWriteMaskValues() {
    std::vector<Value> values;
    values.reserve(kColorWriteCombinations.size());
    for (WGPUColorWriteMask mask : kColorWriteCombinations) {
        values.emplace_back(static_cast<uint64_t>(mask));
    }
    return values;
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t, WGPUExtent3D size = WGPUExtent3D{kWidth, kHeight, 1}) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = size;
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

WGPUBindGroupLayout createUniformBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Fragment;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

PipelineForTest createRenderPipelineForTest(AllFeaturesMaxLimitsGpuTest& t, const WGPUColorTargetState& colorTargetState) {
    WGPUBindGroupLayout bindGroupLayout = createUniformBindGroupLayout(t);
    WGPUPipelineLayout layout = createPipelineLayout(t, bindGroupLayout);
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kFullscreenVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kUniformColorFragmentShader);

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTargetState;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return PipelineForTest{bindGroupLayout, t.createRenderPipelineTracked(desc)};
}

WGPUBindGroup createBindGroupForTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    const std::array<float, 4>& data) {
    WGPUBuffer uniform = t.makeBufferWithContents(
        data.data(),
        data.size() * sizeof(float),
        WGPUBufferUsage_Uniform);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = uniform;
    entry.offset = 0;
    entry.size = data.size() * sizeof(float);

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPURenderPassEncoder beginRenderPass(GpuTest& t, 
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPULoadOp loadOp = WGPULoadOp_Clear,
    WGPUColor clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0}) {
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = loadOp;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = clearValue;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    return t.beginRenderPassTracked(encoder, &passDesc);
}

void drawFullscreen(
    WGPURenderPassEncoder pass,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bindGroup) {
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
}

void renderColor(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPURenderPipeline pipeline,
    WGPUBindGroup bindGroup,
    std::optional<WGPUColor> blendConstant = std::nullopt,
    WGPULoadOp loadOp = WGPULoadOp_Clear) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = beginRenderPass(t, encoder, view, loadOp);
    if (blendConstant) {
        wgpuRenderPassEncoderSetBlendConstant(pass, &*blendConstant);
    }
    drawFullscreen(pass, pipeline, bindGroup);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);
}

void expectColorOkInTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    std::array<double, 4> expected,
    WGPUExtent3D size,
    double maxDiff) {
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(size.width * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (size.height - 1) + static_cast<uint64_t>(size.width) * kBytesPerPixel,
        kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, bytesPerRow, size);
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t y = 0; y < size.height; ++y) {
                for (uint32_t x = 0; x < size.width; ++x) {
                    const uint64_t offset = static_cast<uint64_t>(y) * bytesPerRow
                        + static_cast<uint64_t>(x) * kBytesPerPixel;
                    if (offset + kBytesPerPixel > len) {
                        std::ostringstream message;
                        message << "rgba8unorm pixel offset out of range: " << offset;
                        return message.str();
                    }
                    for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel) {
                        const double decoded = static_cast<double>(actual[offset + channel]) / 255.0;
                        if (std::abs(decoded - expected[channel]) > maxDiff) {
                            std::ostringstream message;
                            message << "rgba8unorm mismatch at (" << x << ", " << y << ") channel " << channel
                                    << ": expected " << expected[channel] << ", got " << decoded;
                            return message.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

WGPUBlendState constantBlendState(WGPUBlendFactor dstFactor = WGPUBlendFactor_Zero) {
    WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
    blend.color.operation = WGPUBlendOperation_Add;
    blend.color.srcFactor = WGPUBlendFactor_Constant;
    blend.color.dstFactor = dstFactor;
    blend.alpha.operation = WGPUBlendOperation_Add;
    blend.alpha.srcFactor = WGPUBlendFactor_Constant;
    blend.alpha.dstFactor = dstFactor;
    return blend;
}

CTS_TEST(g, "color_write_mask,channel_work")
    .params([](ParamsBuilder u) {
        return u.combine("mask", colorWriteMaskValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const auto mask = static_cast<WGPUColorWriteMask>(t.param<uint64_t>("mask"));

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.writeMask = mask;
        PipelineForTest pipeline = createRenderPipelineForTest(t, colorTarget);
        WGPUBindGroup bindGroup = createBindGroupForTest(t, pipeline.bindGroupLayout, {32.0f, 64.0f, 128.0f, 192.0f});
        WGPUTexture texture = createRenderTarget(t);

        renderColor(t, texture, pipeline.pipeline, bindGroup);

        expectColorOkInTexture(
            t,
            texture,
            {
                (mask & WGPUColorWriteMask_Red) != 0 ? 1.0 : 0.0,
                (mask & WGPUColorWriteMask_Green) != 0 ? 1.0 : 0.0,
                (mask & WGPUColorWriteMask_Blue) != 0 ? 1.0 : 0.0,
                (mask & WGPUColorWriteMask_Alpha) != 0 ? 1.0 : 0.0,
            },
            WGPUExtent3D{kWidth, kHeight, 1},
            0.0);
    });

CTS_TEST(g, "color_write_mask,blending_disabled")
    .params([](ParamsBuilder u) {
        return u.combine("disabled", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool disabled = t.param<bool>("disabled");
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.blend = disabled ? nullptr : &blend;
        colorTarget.writeMask = WGPUColorWriteMask_Red;
        PipelineForTest pipeline = createRenderPipelineForTest(t, colorTarget);
        WGPUBindGroup bindGroup = createBindGroupForTest(t, pipeline.bindGroupLayout, {1.0f, 1.0f, 1.0f, 1.0f});
        WGPUTexture texture = createRenderTarget(t);

        renderColor(t, texture, pipeline.pipeline, bindGroup);
        expectColorOkInTexture(t, texture, {1.0, 0.0, 0.0, 0.0}, WGPUExtent3D{kWidth, kHeight, 1}, 0.0);
    });

CTS_TEST(g, "blend_constant,initial")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBlendState blend = constantBlendState();
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.blend = &blend;
        PipelineForTest pipeline = createRenderPipelineForTest(t, colorTarget);
        WGPUBindGroup bindGroup = createBindGroupForTest(t, pipeline.bindGroupLayout, {255.0f, 255.0f, 255.0f, 255.0f});
        WGPUTexture texture = createRenderTarget(t);

        renderColor(t, texture, pipeline.pipeline, bindGroup);
        expectColorOkInTexture(t, texture, {0.0, 0.0, 0.0, 0.0}, WGPUExtent3D{kWidth, kHeight, 1}, 0.0);
    });

CTS_TEST(g, "blend_constant,setting")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"r", 1}, {"g", 1}, {"b", 1}, {"a", 1}},
            ParamRecord{{"r", 0.5}, {"g", 1}, {"b", 0.5}, {"a", 0}},
            ParamRecord{{"r", 0}, {"g", 0}, {"b", 0}, {"a", 0}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUColor constant{t.param<double>("r"), t.param<double>("g"), t.param<double>("b"), t.param<double>("a")};
        WGPUBlendState blend = constantBlendState(WGPUBlendFactor_One);
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.blend = &blend;
        PipelineForTest pipeline = createRenderPipelineForTest(t, colorTarget);
        WGPUBindGroup bindGroup = createBindGroupForTest(t, pipeline.bindGroupLayout, {255.0f, 255.0f, 255.0f, 255.0f});
        WGPUTexture texture = createRenderTarget(t);

        renderColor(t, texture, pipeline.pipeline, bindGroup, constant);
        const double tolerance = (constant.r == 0.5 || constant.b == 0.5) ? 1.5 / 255.0 : 0.0;
        expectColorOkInTexture(t, texture, {constant.r, constant.g, constant.b, constant.a}, WGPUExtent3D{kWidth, kHeight, 1}, tolerance);
    });

CTS_TEST(g, "blend_constant,not_inherited")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBlendState blend = constantBlendState();
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.blend = &blend;
        PipelineForTest pipeline = createRenderPipelineForTest(t, colorTarget);
        WGPUBindGroup bindGroup = createBindGroupForTest(t, pipeline.bindGroupLayout, {255.0f, 255.0f, 255.0f, 255.0f});
        WGPUTexture texture = createRenderTarget(t);

        renderColor(t, texture, pipeline.pipeline, bindGroup, WGPUColor{1.0, 1.0, 1.0, 1.0});
        renderColor(t, texture, pipeline.pipeline, bindGroup);

        expectColorOkInTexture(t, texture, {0.0, 0.0, 0.0, 0.0}, WGPUExtent3D{kWidth, kHeight, 1}, 0.0);
    });

// ---------------------------------------------------------------------------------------------
// blending,* (ported at d116bcc)
// ---------------------------------------------------------------------------------------------

struct BlendFactorInfo {
    const char* name;
    WGPUBlendFactor value;
};

// capability_info.ts kBlendFactors (order matters for case identity).
constexpr std::array<BlendFactorInfo, 17> kBlendFactors = {{
    {"zero", WGPUBlendFactor_Zero},
    {"one", WGPUBlendFactor_One},
    {"src", WGPUBlendFactor_Src},
    {"one-minus-src", WGPUBlendFactor_OneMinusSrc},
    {"src-alpha", WGPUBlendFactor_SrcAlpha},
    {"one-minus-src-alpha", WGPUBlendFactor_OneMinusSrcAlpha},
    {"dst", WGPUBlendFactor_Dst},
    {"one-minus-dst", WGPUBlendFactor_OneMinusDst},
    {"dst-alpha", WGPUBlendFactor_DstAlpha},
    {"one-minus-dst-alpha", WGPUBlendFactor_OneMinusDstAlpha},
    {"src-alpha-saturated", WGPUBlendFactor_SrcAlphaSaturated},
    {"constant", WGPUBlendFactor_Constant},
    {"one-minus-constant", WGPUBlendFactor_OneMinusConstant},
    {"src1", WGPUBlendFactor_Src1},
    {"one-minus-src1", WGPUBlendFactor_OneMinusSrc1},
    {"src1-alpha", WGPUBlendFactor_Src1Alpha},
    {"one-minus-src1-alpha", WGPUBlendFactor_OneMinusSrc1Alpha},
}};

struct BlendOperationInfo {
    const char* name;
    WGPUBlendOperation value;
};

// capability_info.ts kBlendOperations.
constexpr std::array<BlendOperationInfo, 5> kBlendOperations = {{
    {"add", WGPUBlendOperation_Add},
    {"subtract", WGPUBlendOperation_Subtract},
    {"reverse-subtract", WGPUBlendOperation_ReverseSubtract},
    {"min", WGPUBlendOperation_Min},
    {"max", WGPUBlendOperation_Max},
}};

std::vector<Value> blendFactorValues() {
    std::vector<Value> values;
    values.reserve(kBlendFactors.size());
    for (const BlendFactorInfo& info : kBlendFactors) {
        values.emplace_back(std::string(info.name));
    }
    return values;
}

std::vector<Value> blendOperationValues() {
    std::vector<Value> values;
    values.reserve(kBlendOperations.size());
    for (const BlendOperationInfo& info : kBlendOperations) {
        values.emplace_back(std::string(info.name));
    }
    return values;
}

WGPUBlendFactor parseBlendFactor(std::string_view name) {
    for (const BlendFactorInfo& info : kBlendFactors) {
        if (name == info.name) {
            return info.value;
        }
    }
    std::abort();
}

WGPUBlendOperation parseBlendOperation(std::string_view name) {
    for (const BlendOperationInfo& info : kBlendOperations) {
        if (name == info.name) {
            return info.value;
        }
    }
    std::abort();
}

// capability_info.ts IsDualSourceBlendingFactor.
bool isDualSourceBlendingFactor(WGPUBlendFactor factor) {
    return factor == WGPUBlendFactor_Src1 || factor == WGPUBlendFactor_OneMinusSrc1
        || factor == WGPUBlendFactor_Src1Alpha || factor == WGPUBlendFactor_OneMinusSrc1Alpha;
}

// GPUColorDict as {r, g, b, a}.
using BlendColor = std::array<double, 4>;
constexpr size_t kR = 0;
constexpr size_t kG = 1;
constexpr size_t kB = 2;
constexpr size_t kA = 3;

// Upstream's object-valued subcase params, spelled as the JSON upstream's query stringifier
// produces (same convention as compute_builtins' groupSize/numGroups).
constexpr std::string_view kSrcColorJson = R"({"r":0.11,"g":0.61,"b":0.81,"a":0.44})";
constexpr std::string_view kSrcColor1Json = R"({"r":0.22,"g":0.41,"b":0.51,"a":0.33})";
constexpr std::string_view kDstColor0Json = R"({"r":0.51,"g":0.22,"b":0.71,"a":0.33})";
constexpr std::string_view kDstColor1Json = R"({"r":0.09,"g":0.73,"b":0.93,"a":0.81})";
constexpr std::string_view kBlendConstantJson = R"({"r":0.91,"g":0.82,"b":0.73,"a":0.64})";

BlendColor parseColorJson(std::string_view json) {
    if (json == kSrcColorJson) return {0.11, 0.61, 0.81, 0.44};
    if (json == kSrcColor1Json) return {0.22, 0.41, 0.51, 0.33};
    if (json == kDstColor0Json) return {0.51, 0.22, 0.71, 0.33};
    if (json == kDstColor1Json) return {0.09, 0.73, 0.93, 0.81};
    if (json == kBlendConstantJson) return {0.91, 0.82, 0.73, 0.64};
    std::abort();
}

template <class F>
BlendColor mapColor(const BlendColor& col, F f) {
    return {f(col[kR], kR), f(col[kG], kG), f(col[kB], kB), f(col[kA], kA)};
}

BlendColor computeBlendFactor(
    const BlendColor& src,
    const BlendColor& src1,
    const BlendColor& dst,
    const std::optional<BlendColor>& blendColor,
    WGPUBlendFactor factor) {
    switch (factor) {
        case WGPUBlendFactor_Zero:
            return {0.0, 0.0, 0.0, 0.0};
        case WGPUBlendFactor_One:
            return {1.0, 1.0, 1.0, 1.0};
        case WGPUBlendFactor_Src:
            return src;
        case WGPUBlendFactor_OneMinusSrc:
            return mapColor(src, [](double v, size_t) { return 1.0 - v; });
        case WGPUBlendFactor_SrcAlpha:
            return mapColor(src, [&src](double, size_t) { return src[kA]; });
        case WGPUBlendFactor_OneMinusSrcAlpha:
            return mapColor(src, [&src](double, size_t) { return 1.0 - src[kA]; });
        case WGPUBlendFactor_Dst:
            return dst;
        case WGPUBlendFactor_OneMinusDst:
            return mapColor(dst, [](double v, size_t) { return 1.0 - v; });
        case WGPUBlendFactor_DstAlpha:
            return mapColor(dst, [&dst](double, size_t) { return dst[kA]; });
        case WGPUBlendFactor_OneMinusDstAlpha:
            return mapColor(dst, [&dst](double, size_t) { return 1.0 - dst[kA]; });
        case WGPUBlendFactor_SrcAlphaSaturated: {
            const double f = std::min(src[kA], 1.0 - dst[kA]);
            return {f, f, f, 1.0};
        }
        case WGPUBlendFactor_Constant:
            return blendColor.value();
        case WGPUBlendFactor_OneMinusConstant:
            return mapColor(blendColor.value(), [](double v, size_t) { return 1.0 - v; });
        case WGPUBlendFactor_Src1:
            return src1;
        case WGPUBlendFactor_OneMinusSrc1:
            return mapColor(src1, [](double v, size_t) { return 1.0 - v; });
        case WGPUBlendFactor_Src1Alpha:
            return mapColor(src1, [&src1](double, size_t) { return src1[kA]; });
        case WGPUBlendFactor_OneMinusSrc1Alpha:
            return mapColor(src1, [&src1](double, size_t) { return 1.0 - src1[kA]; });
        default:
            std::abort();
    }
}

BlendColor computeBlendOperation(
    const BlendColor& src,
    const BlendColor& srcFactor,
    const BlendColor& dst,
    const BlendColor& dstFactor,
    WGPUBlendOperation operation) {
    switch (operation) {
        case WGPUBlendOperation_Add:
            return mapColor(src, [&](double, size_t k) { return srcFactor[k] * src[k] + dstFactor[k] * dst[k]; });
        case WGPUBlendOperation_Max:
            return mapColor(src, [&](double, size_t k) { return std::max(src[k], dst[k]); });
        case WGPUBlendOperation_Min:
            return mapColor(src, [&](double, size_t k) { return std::min(src[k], dst[k]); });
        case WGPUBlendOperation_ReverseSubtract:
            return mapColor(src, [&](double, size_t k) { return dstFactor[k] * dst[k] - srcFactor[k] * src[k]; });
        case WGPUBlendOperation_Subtract:
            return mapColor(src, [&](double, size_t k) { return srcFactor[k] * src[k] - dstFactor[k] * dst[k]; });
        default:
            std::abort();
    }
}

// Port of expectTexelViewComparisonIsOkInTexture / expectSinglePixelComparisonsAreOkInTexture for
// the single texel at (0, 0, 0) of a 1x1 texture.
void expectSingleTexelOkInTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    WGPUTextureFormat format,
    const std::array<double, 4>& expected,
    const TexelCompareOptions& options) {
    const uint32_t bytesPerBlock = texelRepresentation(format).bytesPerBlock;
    const uint64_t byteLength = alignTo(bytesPerBlock, kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, kBytesPerRowAlignment, WGPUExtent3D{1, 1, 1});
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            return compareTexelToColor(format, actual, len, expected, options);
        },
        0,
        static_cast<size_t>(byteLength));
}

WGPUTexture createSinglePixelRenderTarget(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

constexpr std::string_view kPointVertexShader = R"(
@vertex fn main() -> @builtin(position) vec4<f32> {
    return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}
)";

// Pipeline with layout 'auto', a single point at the center, and one color target.
WGPURenderPipeline createPointPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view fragmentCode,
    const WGPUColorTargetState& target) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kPointVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentCode);

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr; // 'auto'
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

// Clears the 1x1 target to `clearValue`, draws one point, and submits.
void drawOnePoint(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture renderTarget,
    WGPUColor clearValue,
    WGPURenderPipeline pipeline,
    const std::optional<WGPUColor>& blendConstant,
    WGPUBindGroup bindGroup) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(renderTarget, viewDesc);
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = beginRenderPass(t, encoder, view, WGPULoadOp_Clear, clearValue);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    if (blendConstant) {
        wgpuRenderPassEncoderSetBlendConstant(pass, &*blendConstant);
    }
    if (bindGroup != nullptr) {
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    }
    wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);
}

// JS Number#toString for the shader literals interpolated by upstream (e.g. 0.4 -> "0.4", 1 -> "1").
std::string jsNumberString(double value) {
    // Shortest round-tripping decimal, which is what JS prints for these short literals.
    std::string text;
    for (int precision = 1; precision <= 17; ++precision) {
        std::ostringstream candidate;
        candidate.precision(precision);
        candidate << value;
        text = candidate.str();
        if (std::stod(text) == value) {
            break;
        }
    }
    return text;
}

constexpr WGPUTextureFormat kBlendingGPUBlendComponentFormat = WGPUTextureFormat_RGBA16Float;

CTS_TEST(g, "blending,GPUBlendComponent")
    .desc(R"(Test all combinations of parameters for GPUBlendComponent.

  Tests that parameters are correctly passed to the backend API and blend computations
  are done correctly by blending a single pixel. The test uses rgba16float as the format
  to avoid checking clamping behavior (tested in api,operation,rendering,blending:clamp,*).

  Params:
    - component= {color, alpha} - whether to test blending the color or the alpha component.
    - srcFactor= {...all GPUBlendFactors}
    - dstFactor= {...all GPUBlendFactors}
    - operation= {...all GPUBlendOperations})")
    .params([](ParamsBuilder u) {
        return u.combine("component", {"color", "alpha"})
            .combine("srcFactor", blendFactorValues())
            .combine("dstFactor", blendFactorValues())
            .beginSubcases()
            .combine("operation", blendOperationValues())
            .filter([](const ParamRecord& p) {
                const std::string operation = valueAs<std::string>(*findParam(p, "operation"));
                if (operation == "min" || operation == "max") {
                    return valueAs<std::string>(*findParam(p, "srcFactor")) == "one"
                        && valueAs<std::string>(*findParam(p, "dstFactor")) == "one";
                }
                return true;
            })
            .combine("srcColor", {std::string(kSrcColorJson)})
            .combine("srcColor1", {std::string(kSrcColor1Json)})
            .combine("dstColor", {std::string(kDstColor0Json), std::string(kDstColor1Json)})
            .expand("blendConstant", [](const ParamRecord& p) {
                const std::string srcFactor = valueAs<std::string>(*findParam(p, "srcFactor"));
                const std::string dstFactor = valueAs<std::string>(*findParam(p, "dstFactor"));
                const bool needsBlendConstant = srcFactor == "one-minus-constant" || srcFactor == "constant"
                    || dstFactor == "one-minus-constant" || dstFactor == "constant";
                return needsBlendConstant ? std::vector<Value>{std::string(kBlendConstantJson)}
                                          : std::vector<Value>{Value::undef()};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string component = t.param<std::string>("component");
        const WGPUBlendFactor srcFactorParam = parseBlendFactor(t.param<std::string>("srcFactor"));
        const WGPUBlendFactor dstFactorParam = parseBlendFactor(t.param<std::string>("dstFactor"));
        const WGPUBlendOperation operation = parseBlendOperation(t.param<std::string>("operation"));
        if (isDualSourceBlendingFactor(srcFactorParam) || isDualSourceBlendingFactor(dstFactorParam)) {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DualSourceBlending)) {
                t.skip("device does not have feature dual-source-blending");
            }
        }
        const WGPUTextureFormat textureFormat = kBlendingGPUBlendComponentFormat;
        const BlendColor srcColor = parseColorJson(t.param<std::string>("srcColor"));
        const BlendColor srcColor1 = parseColorJson(t.param<std::string>("srcColor1"));
        const BlendColor dstColor = parseColorJson(t.param<std::string>("dstColor"));
        std::optional<BlendColor> blendConstant;
        if (!t.paramIsUndefined("blendConstant")) {
            blendConstant = parseColorJson(t.param<std::string>("blendConstant"));
        }

        const BlendColor srcFactor = computeBlendFactor(srcColor, srcColor1, dstColor, blendConstant, srcFactorParam);
        const BlendColor dstFactor = computeBlendFactor(srcColor, srcColor1, dstColor, blendConstant, dstFactorParam);
        BlendColor expectedColor = computeBlendOperation(srcColor, srcFactor, dstColor, dstFactor, operation);

        if (component == "color") {
            expectedColor[kA] = srcColor[kA];
        } else {
            expectedColor[kR] = srcColor[kR];
            expectedColor[kG] = srcColor[kG];
            expectedColor[kB] = srcColor[kB];
        }

        const bool useBlendSrc1 = isDualSourceBlendingFactor(srcFactorParam) || isDualSourceBlendingFactor(dstFactorParam);

        // Set both color/alpha to defaults (operation add, srcFactor one, dstFactor zero)...
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_Zero;
        blend.alpha = blend.color;
        // ... but then override the component we're testing.
        WGPUBlendComponent& tested = component == "color" ? blend.color : blend.alpha;
        tested.srcFactor = srcFactorParam;
        tested.dstFactor = dstFactorParam;
        tested.operation = operation;

        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = textureFormat;
        target.blend = &blend;

        std::string fragmentCode;
        fragmentCode += "\n";
        fragmentCode += useBlendSrc1 ? "enable dual_source_blending;" : "";
        fragmentCode += R"(

struct Uniform {
  color: vec4f,
  blend: vec4f,
};
@group(0) @binding(0) var<uniform> u : Uniform;

struct FragOutput {
  @location(0) )";
        fragmentCode += useBlendSrc1 ? "@blend_src(0)" : "";
        fragmentCode += R"( color : vec4f,
  )";
        fragmentCode += useBlendSrc1 ? "@location(0) @blend_src(1) blend : vec4f," : "";
        fragmentCode += R"(
}

@fragment fn main() ->FragOutput {
  var fragOutput : FragOutput;
  fragOutput.color = u.color;
  )";
        fragmentCode += useBlendSrc1 ? "fragOutput.blend = u.blend;" : "";
        fragmentCode += R"(
  return fragOutput;
}
)";

        WGPURenderPipeline pipeline = createPointPipeline(t, fragmentCode, target);
        WGPUTexture renderTarget = createSinglePixelRenderTarget(t, textureFormat);

        const std::array<float, 8> uniformData = {
            static_cast<float>(srcColor[kR]), static_cast<float>(srcColor[kG]),
            static_cast<float>(srcColor[kB]), static_cast<float>(srcColor[kA]),
            static_cast<float>(srcColor1[kR]), static_cast<float>(srcColor1[kG]),
            static_cast<float>(srcColor1[kB]), static_cast<float>(srcColor1[kA]),
        };
        WGPUBuffer uniform = t.makeBufferWithContents(uniformData.data(), sizeof(uniformData), WGPUBufferUsage_Uniform);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = uniform;
        entry.offset = 0;
        entry.size = sizeof(uniformData);
        WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.layout = layout;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
        if (layout != nullptr) {
            wgpuBindGroupLayoutRelease(layout);
        }

        std::optional<WGPUColor> blendConstantColor;
        if (blendConstant) {
            blendConstantColor = WGPUColor{(*blendConstant)[kR], (*blendConstant)[kG], (*blendConstant)[kB], (*blendConstant)[kA]};
        }
        drawOnePoint(
            t,
            renderTarget,
            WGPUColor{dstColor[kR], dstColor[kG], dstColor[kB], dstColor[kA]},
            pipeline,
            blendConstantColor,
            bindGroup);

        TexelCompareOptions options;
        options.maxFractionalDiff = 0.003;
        expectSingleTexelOkInTexture(t, renderTarget, textureFormat, expectedColor, options);
    });

CTS_TEST(g, "blending,formats")
    .desc(R"(Test blending results works for all formats that support it, and that blending is not applied
  for formats that do not. Blending should be done in linear space for srgb formats.)")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatIdentifierValues(kColorRenderableTextureFormats));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        t.skipIfTextureFormatNotUsableAsRenderAttachment(format);
        if (!t.isTextureFormatBlendable(format)) {
            t.skip("texture format is not blendable");
        }

        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_One;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha = blend.color;
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format;
        target.blend = &blend;

        WGPURenderPipeline pipeline = createPointPipeline(t, R"(
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(0.4, 0.4, 0.4, 0.4);
}
)", target);
        WGPUTexture renderTarget = createSinglePixelRenderTarget(t, format);
        drawOnePoint(t, renderTarget, WGPUColor{0.2, 0.2, 0.2, 0.2}, pipeline, std::nullopt, nullptr);

        expectSingleTexelOkInTexture(t, renderTarget, format, {0.6, 0.6, 0.6, 0.6}, defaultTexelCompareOptions());
    });

CTS_TEST(g, "blending,clamping")
    .desc(R"(
  Test that clamping occurs at the correct points in the blend process: src value, src factor, dst
  factor, and output.
    - TODO: Need to test snorm formats.
    - TODO: Need to test src value, srcFactor and dstFactor.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("format", {"rgba8unorm", "rg16float"})
            .combine("srcValue", {0.4, 0.6, 0.8, 1.0})
            .combine("dstValue", {0.2, 0.4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const double srcValue = t.param<double>("srcValue");
        const double dstValue = t.param<double>("dstValue");

        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_One;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.alpha = blend.color;
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format;
        target.blend = &blend;

        const std::string src = jsNumberString(srcValue);
        const std::string fragmentCode = "\n@fragment fn main() -> @location(0) vec4<f32> {\n  return vec4<f32>("
            + src + ", " + src + ", " + src + ", " + src + ");\n}\n";
        WGPURenderPipeline pipeline = createPointPipeline(t, fragmentCode, target);
        WGPUTexture renderTarget = createSinglePixelRenderTarget(t, format);
        drawOnePoint(t, renderTarget, WGPUColor{dstValue, dstValue, dstValue, dstValue}, pipeline, std::nullopt, nullptr);

        double expValue = 0.0;
        if (format == WGPUTextureFormat_RGBA8Unorm) {
            // unorm types should clamp if the sum of srcValue and dstValue exceeds 1.
            expValue = std::clamp(srcValue + dstValue, 0.0, 1.0);
        } else {
            // float format types doesn't clamp.
            expValue = srcValue + dstValue;
        }

        expectSingleTexelOkInTexture(
            t, renderTarget, format, {expValue, expValue, expValue, expValue}, defaultTexelCompareOptions());
    });

} // namespace
