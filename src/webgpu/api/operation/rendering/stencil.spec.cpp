// Ported from gpuweb/cts src/webgpu/api/operation/rendering/stencil.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

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
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,stencil",
    "Stencil rendering operation tests.");

constexpr uint32_t kWidth = 1;
constexpr uint32_t kHeight = 1;
constexpr uint32_t kBytesPerPixel = 4;
constexpr uint32_t kReferenceStencil = 3;

constexpr std::array<double, 4> kBaseColor = {1.0, 1.0, 1.0, 1.0};
constexpr std::array<double, 4> kRedStencilColor = {1.0, 0.0, 0.0, 1.0};
constexpr std::array<double, 4> kGreenStencilColor = {0.0, 1.0, 0.0, 1.0};

constexpr std::array<WGPUTextureFormat, 3> kStencilTextureFormats = {{
    WGPUTextureFormat_Stencil8,
    WGPUTextureFormat_Depth24PlusStencil8,
    WGPUTextureFormat_Depth32FloatStencil8,
}};

constexpr std::array<WGPUTextureFormat, 2> kDepthAndStencilTextureFormats = {{
    WGPUTextureFormat_Depth24PlusStencil8,
    WGPUTextureFormat_Depth32FloatStencil8,
}};

struct PipelineForTest {
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
};

struct TestState {
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    std::array<float, 4> color = {};
    std::optional<uint32_t> stencil;
};

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

std::array<float, 4> colorAsFloat(std::array<double, 4> color) {
    return {
        static_cast<float>(color[0]),
        static_cast<float>(color[1]),
        static_cast<float>(color[2]),
        static_cast<float>(color[3]),
    };
}

std::vector<Value> stencilFormatValues() {
    return formatIdentifierValues(kStencilTextureFormats);
}

std::vector<Value> depthAndStencilFormatValues() {
    return formatIdentifierValues(kDepthAndStencilTextureFormats);
}

WGPUCompareFunction parseCompareFunction(std::string_view value) {
    if (value == "always") {
        return WGPUCompareFunction_Always;
    }
    if (value == "equal") {
        return WGPUCompareFunction_Equal;
    }
    if (value == "greater") {
        return WGPUCompareFunction_Greater;
    }
    if (value == "greater-equal") {
        return WGPUCompareFunction_GreaterEqual;
    }
    if (value == "less") {
        return WGPUCompareFunction_Less;
    }
    if (value == "less-equal") {
        return WGPUCompareFunction_LessEqual;
    }
    if (value == "never") {
        return WGPUCompareFunction_Never;
    }
    if (value == "not-equal") {
        return WGPUCompareFunction_NotEqual;
    }
    std::abort();
}

WGPUStencilOperation parseStencilOperation(std::string_view value) {
    if (value == "keep") {
        return WGPUStencilOperation_Keep;
    }
    if (value == "zero") {
        return WGPUStencilOperation_Zero;
    }
    if (value == "replace") {
        return WGPUStencilOperation_Replace;
    }
    if (value == "invert") {
        return WGPUStencilOperation_Invert;
    }
    if (value == "increment-clamp") {
        return WGPUStencilOperation_IncrementClamp;
    }
    if (value == "increment-wrap") {
        return WGPUStencilOperation_IncrementWrap;
    }
    if (value == "decrement-clamp") {
        return WGPUStencilOperation_DecrementClamp;
    }
    if (value == "decrement-wrap") {
        return WGPUStencilOperation_DecrementWrap;
    }
    std::abort();
}

std::array<double, 4> parseColor(std::string_view value) {
    if (value == "base") {
        return kBaseColor;
    }
    if (value == "red") {
        return kRedStencilColor;
    }
    if (value == "green") {
        return kGreenStencilColor;
    }
    std::abort();
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUTexture createColorTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kWidth, kHeight, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

WGPUTexture createDepthStencilTarget(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kWidth, kHeight, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopyDst;
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

constexpr std::string_view kFixedPointVertexShader = R"(
@vertex fn main() -> @builtin(position) vec4<f32> {
  return vec4<f32>(0.0, 0.0, 0.0, 1.0);
}
)";

constexpr std::string_view kUniformColorFragmentShader = R"(
struct Params { color : vec4<f32> };
@group(0) @binding(0) var<uniform> params : Params;

@fragment fn main() -> @location(0) vec4<f32> {
  return params.color;
}
)";

PipelineForTest createRenderPipelineForTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const WGPUDepthStencilState& depthStencilState) {
    WGPUBindGroupLayout bindGroupLayout = createUniformBindGroupLayout(t);
    WGPUPipelineLayout layout = createPipelineLayout(t, bindGroupLayout);
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kFixedPointVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kUniformColorFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    desc.depthStencil = &depthStencilState;
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

void expectColorOkInTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    std::array<double, 4> expected,
    WGPUExtent3D size,
    double maxDiff = 0.0) {
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

WGPUStencilFaceState stencilFace(
    WGPUCompareFunction compare = WGPUCompareFunction_Always,
    WGPUStencilOperation failOp = WGPUStencilOperation_Keep,
    WGPUStencilOperation depthFailOp = WGPUStencilOperation_Keep,
    WGPUStencilOperation passOp = WGPUStencilOperation_Keep) {
    WGPUStencilFaceState face = WGPU_STENCIL_FACE_STATE_INIT;
    face.compare = compare;
    face.failOp = failOp;
    face.depthFailOp = depthFailOp;
    face.passOp = passOp;
    return face;
}

WGPUDepthStencilState depthStencilState(
    WGPUTextureFormat format,
    WGPUStencilFaceState stencilState,
    WGPUCompareFunction depthCompare = WGPUCompareFunction_Always,
    WGPUOptionalBool depthWriteEnabled = WGPUOptionalBool_False,
    uint32_t stencilReadMask = 0xff,
    uint32_t stencilWriteMask = 0xff) {
    const bool hasDepth = isDepthTextureFormat(format);
    WGPUDepthStencilState state = WGPU_DEPTH_STENCIL_STATE_INIT;
    state.format = format;
    state.depthCompare = hasDepth ? depthCompare : WGPUCompareFunction_Always;
    state.depthWriteEnabled = hasDepth ? depthWriteEnabled : WGPUOptionalBool_False;
    state.stencilFront = stencilState;
    state.stencilBack = stencilState;
    state.stencilReadMask = stencilReadMask;
    state.stencilWriteMask = stencilWriteMask;
    return state;
}

WGPURenderPassColorAttachment colorAttachment(WGPUTextureView view) {
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Load;
    attachment.storeOp = WGPUStoreOp_Store;
    return attachment;
}

WGPURenderPassDepthStencilAttachment depthStencilAttachment(WGPUTextureView view, WGPUTextureFormat format) {
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = view;
    if (isDepthTextureFormat(format)) {
        attachment.depthLoadOp = WGPULoadOp_Load;
        attachment.depthStoreOp = WGPUStoreOp_Store;
    }
    attachment.stencilLoadOp = WGPULoadOp_Load;
    attachment.stencilStoreOp = WGPUStoreOp_Store;
    return attachment;
}

void drawState(WGPURenderPassEncoder pass, AllFeaturesMaxLimitsGpuTest& t, const TestState& state) {
    PipelineForTest pipeline = createRenderPipelineForTest(t, state.depthStencil);
    WGPUBindGroup bindGroup = createBindGroupForTest(t, pipeline.bindGroupLayout, state.color);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline.pipeline);
    if (state.stencil.has_value()) {
        wgpuRenderPassEncoderSetStencilReference(pass, *state.stencil);
    }
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
}

void runStencilStateTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    const std::vector<TestState>& testStates,
    std::array<double, 4> expectedColor,
    bool isSingleEncoderMultiplePass = false) {
    WGPUTexture colorTarget = createColorTarget(t);
    WGPUTexture depthStencil = createDepthStencilTarget(t, format);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(colorTarget, viewDesc);
    WGPUTextureView depthStencilView = t.createViewTracked(depthStencil, viewDesc);

    WGPURenderPassColorAttachment colorAttach = colorAttachment(colorView);
    WGPURenderPassDepthStencilAttachment dsAttachment = depthStencilAttachment(depthStencilView, format);

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttach;
    passDesc.depthStencilAttachment = &dsAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
    if (isSingleEncoderMultiplePass) {
        wgpuRenderPassEncoderEnd(pass);
    }

    for (const TestState& state : testStates) {
        if (isSingleEncoderMultiplePass) {
            pass = t.beginRenderPassTracked(encoder, &passDesc);
        }
        drawState(pass, t, state);
        if (isSingleEncoderMultiplePass) {
            wgpuRenderPassEncoderEnd(pass);
        }
    }

    if (!isSingleEncoderMultiplePass) {
        wgpuRenderPassEncoderEnd(pass);
    }
    submit(t, encoder);

    expectColorOkInTexture(t, colorTarget, expectedColor, WGPUExtent3D{kWidth, kHeight, 1});
}

TestState makeState(WGPUDepthStencilState state, std::array<double, 4> color, std::optional<uint32_t> stencil) {
    return TestState{state, colorAsFloat(color), stencil};
}

void checkStencilCompareFunction(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUCompareFunction compareFunction,
    uint32_t stencilRefValue,
    std::array<double, 4> expectedColor) {
    const WGPUStencilFaceState baseStencilState = stencilFace(
        WGPUCompareFunction_Always,
        WGPUStencilOperation_Keep,
        WGPUStencilOperation_Keep,
        WGPUStencilOperation_Replace);
    const WGPUStencilFaceState stencilState = stencilFace(compareFunction);

    const WGPUDepthStencilState baseState = depthStencilState(format, baseStencilState);
    const WGPUDepthStencilState testState = depthStencilState(format, stencilState);

    runStencilStateTest(t, format, {
        makeState(baseState, kBaseColor, 1),
        makeState(testState, kGreenStencilColor, stencilRefValue),
    }, expectedColor);
}

void checkStencilOperation(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTextureFormat format,
    WGPUStencilFaceState testStencilState,
    uint32_t initialStencil,
    uint32_t expectedStencil,
    WGPUCompareFunction depthCompare = WGPUCompareFunction_Always) {
    const WGPUStencilFaceState baseStencilState = stencilFace(
        WGPUCompareFunction_Always,
        WGPUStencilOperation_Keep,
        WGPUStencilOperation_Keep,
        WGPUStencilOperation_Replace);
    const WGPUStencilFaceState stencilState = stencilFace(WGPUCompareFunction_Equal);

    const WGPUDepthStencilState baseState = depthStencilState(format, baseStencilState);
    const WGPUDepthStencilState testState = depthStencilState(format, testStencilState, depthCompare);
    const WGPUDepthStencilState testState2 = depthStencilState(format, stencilState);

    runStencilStateTest(t, format, {
        makeState(baseState, kBaseColor, initialStencil),
        makeState(testState, kRedStencilColor, kReferenceStencil),
        makeState(testState2, kGreenStencilColor, expectedStencil),
    }, kGreenStencilColor);
}

CTS_TEST(g, "stencil_compare_func")
    .params([](ParamsBuilder u) {
        return u.combine("format", stencilFormatValues())
            .combineWithParams({
                ParamRecord{{"stencilCompare", "always"}, {"stencilRefValue", 0}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "always"}, {"stencilRefValue", 1}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "always"}, {"stencilRefValue", 2}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "equal"}, {"stencilRefValue", 0}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "equal"}, {"stencilRefValue", 1}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "equal"}, {"stencilRefValue", 2}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "greater"}, {"stencilRefValue", 0}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "greater"}, {"stencilRefValue", 1}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "greater"}, {"stencilRefValue", 2}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "greater-equal"}, {"stencilRefValue", 0}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "greater-equal"}, {"stencilRefValue", 1}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "greater-equal"}, {"stencilRefValue", 2}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "less"}, {"stencilRefValue", 0}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "less"}, {"stencilRefValue", 1}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "less"}, {"stencilRefValue", 2}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "less-equal"}, {"stencilRefValue", 0}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "less-equal"}, {"stencilRefValue", 1}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "less-equal"}, {"stencilRefValue", 2}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "never"}, {"stencilRefValue", 0}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "never"}, {"stencilRefValue", 1}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "never"}, {"stencilRefValue", 2}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "not-equal"}, {"stencilRefValue", 0}, {"expectedColor", "green"}},
                ParamRecord{{"stencilCompare", "not-equal"}, {"stencilRefValue", 1}, {"expectedColor", "base"}},
                ParamRecord{{"stencilCompare", "not-equal"}, {"stencilRefValue", 2}, {"expectedColor", "green"}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        checkStencilCompareFunction(
            t,
            format,
            parseCompareFunction(t.param<std::string>("stencilCompare")),
            static_cast<uint32_t>(t.param<int64_t>("stencilRefValue")),
            parseColor(t.param<std::string>("expectedColor")));
    });

CTS_TEST(g, "stencil_passOp_operation")
    .params([](ParamsBuilder u) {
        return u.combine("format", stencilFormatValues())
            .combineWithParams({
                ParamRecord{{"passOp", "keep"}, {"initialStencil", 1}, {"expectedStencil", 1}},
                ParamRecord{{"passOp", "zero"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"passOp", "replace"}, {"initialStencil", 1}, {"expectedStencil", 3}},
                ParamRecord{{"passOp", "invert"}, {"initialStencil", 0xf0}, {"expectedStencil", 0x0f}},
                ParamRecord{{"passOp", "increment-clamp"}, {"initialStencil", 1}, {"expectedStencil", 2}},
                ParamRecord{{"passOp", "increment-clamp"}, {"initialStencil", 0xff}, {"expectedStencil", 0xff}},
                ParamRecord{{"passOp", "increment-wrap"}, {"initialStencil", 1}, {"expectedStencil", 2}},
                ParamRecord{{"passOp", "increment-wrap"}, {"initialStencil", 0xff}, {"expectedStencil", 0}},
                ParamRecord{{"passOp", "decrement-clamp"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"passOp", "decrement-clamp"}, {"initialStencil", 0}, {"expectedStencil", 0}},
                ParamRecord{{"passOp", "decrement-wrap"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"passOp", "decrement-wrap"}, {"initialStencil", 0}, {"expectedStencil", 0xff}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        const WGPUStencilFaceState stencilState = stencilFace(
            WGPUCompareFunction_Always,
            WGPUStencilOperation_Keep,
            WGPUStencilOperation_Keep,
            parseStencilOperation(t.param<std::string>("passOp")));
        checkStencilOperation(
            t,
            format,
            stencilState,
            static_cast<uint32_t>(t.param<int64_t>("initialStencil")),
            static_cast<uint32_t>(t.param<int64_t>("expectedStencil")));
    });

CTS_TEST(g, "stencil_failOp_operation")
    .params([](ParamsBuilder u) {
        return u.combine("format", stencilFormatValues())
            .combineWithParams({
                ParamRecord{{"failOp", "keep"}, {"initialStencil", 1}, {"expectedStencil", 1}},
                ParamRecord{{"failOp", "zero"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"failOp", "replace"}, {"initialStencil", 1}, {"expectedStencil", 3}},
                ParamRecord{{"failOp", "invert"}, {"initialStencil", 0xf0}, {"expectedStencil", 0x0f}},
                ParamRecord{{"failOp", "increment-clamp"}, {"initialStencil", 1}, {"expectedStencil", 2}},
                ParamRecord{{"failOp", "increment-clamp"}, {"initialStencil", 0xff}, {"expectedStencil", 0xff}},
                ParamRecord{{"failOp", "increment-wrap"}, {"initialStencil", 1}, {"expectedStencil", 2}},
                ParamRecord{{"failOp", "increment-wrap"}, {"initialStencil", 0xff}, {"expectedStencil", 0}},
                ParamRecord{{"failOp", "decrement-clamp"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"failOp", "decrement-clamp"}, {"initialStencil", 0}, {"expectedStencil", 0}},
                ParamRecord{{"failOp", "decrement-wrap"}, {"initialStencil", 2}, {"expectedStencil", 1}},
                ParamRecord{{"failOp", "decrement-wrap"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"failOp", "decrement-wrap"}, {"initialStencil", 0}, {"expectedStencil", 0xff}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        const WGPUStencilFaceState stencilState = stencilFace(
            WGPUCompareFunction_Never,
            parseStencilOperation(t.param<std::string>("failOp")));
        checkStencilOperation(
            t,
            format,
            stencilState,
            static_cast<uint32_t>(t.param<int64_t>("initialStencil")),
            static_cast<uint32_t>(t.param<int64_t>("expectedStencil")));
    });

CTS_TEST(g, "stencil_depthFailOp_operation")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthAndStencilFormatValues())
            .combineWithParams({
                ParamRecord{{"depthFailOp", "keep"}, {"initialStencil", 1}, {"expectedStencil", 1}},
                ParamRecord{{"depthFailOp", "zero"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"depthFailOp", "replace"}, {"initialStencil", 1}, {"expectedStencil", 3}},
                ParamRecord{{"depthFailOp", "invert"}, {"initialStencil", 0xf0}, {"expectedStencil", 0x0f}},
                ParamRecord{{"depthFailOp", "increment-clamp"}, {"initialStencil", 1}, {"expectedStencil", 2}},
                ParamRecord{{"depthFailOp", "increment-clamp"}, {"initialStencil", 0xff}, {"expectedStencil", 0xff}},
                ParamRecord{{"depthFailOp", "increment-wrap"}, {"initialStencil", 1}, {"expectedStencil", 2}},
                ParamRecord{{"depthFailOp", "increment-wrap"}, {"initialStencil", 0xff}, {"expectedStencil", 0}},
                ParamRecord{{"depthFailOp", "decrement-clamp"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"depthFailOp", "decrement-clamp"}, {"initialStencil", 0}, {"expectedStencil", 0}},
                ParamRecord{{"depthFailOp", "decrement-wrap"}, {"initialStencil", 2}, {"expectedStencil", 1}},
                ParamRecord{{"depthFailOp", "decrement-wrap"}, {"initialStencil", 1}, {"expectedStencil", 0}},
                ParamRecord{{"depthFailOp", "decrement-wrap"}, {"initialStencil", 0}, {"expectedStencil", 0xff}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        const WGPUStencilFaceState stencilState = stencilFace(
            WGPUCompareFunction_Always,
            WGPUStencilOperation_Keep,
            parseStencilOperation(t.param<std::string>("depthFailOp")),
            WGPUStencilOperation_Keep);
        checkStencilOperation(
            t,
            format,
            stencilState,
            static_cast<uint32_t>(t.param<int64_t>("initialStencil")),
            static_cast<uint32_t>(t.param<int64_t>("expectedStencil")),
            WGPUCompareFunction_Never);
    });

CTS_TEST(g, "stencil_read_write_mask")
    .params([](ParamsBuilder u) {
        return u.combine("format", stencilFormatValues())
            .combineWithParams({
                ParamRecord{{"maskType", "write"}, {"stencilRefValue", 1}, {"expectedColor", "red"}},
                ParamRecord{{"maskType", "write"}, {"stencilRefValue", 2}, {"expectedColor", "base"}},
                ParamRecord{{"maskType", "read"}, {"stencilRefValue", 1}, {"expectedColor", "base"}},
                ParamRecord{{"maskType", "read"}, {"stencilRefValue", 2}, {"expectedColor", "red"}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        const std::string maskType = t.param<std::string>("maskType");
        const WGPUStencilFaceState baseStencilState = stencilFace(
            WGPUCompareFunction_Always,
            WGPUStencilOperation_Keep,
            WGPUStencilOperation_Keep,
            WGPUStencilOperation_Replace);
        const WGPUStencilFaceState stencilState = stencilFace(WGPUCompareFunction_Equal);

        const WGPUDepthStencilState baseState = depthStencilState(
            format,
            baseStencilState,
            WGPUCompareFunction_Always,
            WGPUOptionalBool_False,
            0xff,
            maskType == "write" ? 0x1 : 0xff);
        const WGPUDepthStencilState testState = depthStencilState(
            format,
            stencilState,
            WGPUCompareFunction_Always,
            WGPUOptionalBool_False,
            maskType == "read" ? 0x2 : 0xff,
            0xff);

        runStencilStateTest(t, format, {
            makeState(baseState, kBaseColor, 3),
            makeState(testState, kRedStencilColor, static_cast<uint32_t>(t.param<int64_t>("stencilRefValue"))),
        }, parseColor(t.param<std::string>("expectedColor")));
    });

CTS_TEST(g, "stencil_reference_initialized")
    .params([](ParamsBuilder u) {
        return u.combine("format", stencilFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        const WGPUOptionalBool depthWriteEnabled = isDepthTextureFormat(format) ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        const WGPUStencilFaceState baseStencilState = stencilFace(
            WGPUCompareFunction_Always,
            WGPUStencilOperation_Keep,
            WGPUStencilOperation_Keep,
            WGPUStencilOperation_Replace);
        const WGPUStencilFaceState testStencilState = stencilFace(WGPUCompareFunction_Equal);
        const WGPUDepthStencilState baseState = depthStencilState(
            format,
            baseStencilState,
            WGPUCompareFunction_Always,
            depthWriteEnabled);
        const WGPUDepthStencilState testState = depthStencilState(
            format,
            testStencilState,
            WGPUCompareFunction_Always,
            depthWriteEnabled);

        runStencilStateTest(t, format, {
            makeState(baseState, kBaseColor, 0x1),
            makeState(baseState, kRedStencilColor, std::nullopt),
            makeState(testState, kGreenStencilColor, 0x0),
        }, kGreenStencilColor, true);
    });

constexpr uint32_t kMinDynamicBufferOffsetAlignment = 256;
constexpr uint64_t kBufferSizeAlignment = 4;

// data_generation.ts DataArrayGenerator.generateView(byteSize, start, offset) on a fresh generator:
// `offset` leading zero bytes, then ((i ** 3 + i + start) % 251) + 1. Upstream shares one generator
// across cases, whose cached buffer may carry bytes from earlier calls; the test compares the
// rendered output against the very bytes it uploaded, so only the regeneration (not its history)
// matters.
std::vector<uint8_t> generateDataView(uint64_t byteSize, uint64_t start, uint64_t offset) {
    std::vector<uint8_t> data(static_cast<size_t>(byteSize), 0);
    for (uint64_t i = 0; i + offset < byteSize; ++i) {
        data[static_cast<size_t>(i + offset)] = static_cast<uint8_t>(((i * i * i + i + start) % 251) + 1);
    }
    return data;
}

constexpr std::string_view kStencilAccumulationVertexShader = R"(
            @vertex
            fn main(@builtin(vertex_index) VertexIndex : u32)-> @builtin(position) vec4<f32> {
              var pos : array<vec2<f32>, 6> = array<vec2<f32>, 6>(
                  vec2<f32>(-1.0,  1.0),
                  vec2<f32>(-1.0, -1.0),
                  vec2<f32>( 1.0,  1.0),
                  vec2<f32>(-1.0, -1.0),
                  vec2<f32>( 1.0,  1.0),
                  vec2<f32>( 1.0, -1.0));
              return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
            })";

constexpr std::string_view kStencilAccumulationFragmentShader = R"(
            struct Params {
              stencilBitIndex: u32
            };
            @group(0) @binding(0) var<uniform> param: Params;
            @fragment
            fn main() -> @location(0) vec4<f32> {
              return vec4<f32>(f32(1u << param.stencilBitIndex) / 255.0, 0.0, 0.0, 0.0);
            })";

CTS_TEST(g, "stencil_accumulation")
    .desc(R"(A variation of a technique previously used to verify stencil texture copy that was observed to
    fail on some Qualcomm chipsets. Attempts to read back from a stencil texture by doing multiple
    fullscreen renders, one per stencil bit. Each pass sets the stencil mask and stencil reference
    to 1 << passIndex, and renders 1 << passIndex into the red channel with additive blending. This
    should duplicate the stencil values into the red channel of the texture, and on most GPUs it
    works. Technique is replicated here to catch driver bugs, while the original stencil copy test
    has been updated to verify the stencil values using copyTextureToBuffer, which is more
    straightforward and allows those tests to focus on the behavior of the APIs in question.
)")
    .params([](ParamsBuilder u) {
        return u.combine("format", stencilFormatValues())
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"offsetInBlocks", 0}, {"dataPaddingInBytes", 0}},
                ParamRecord{{"offsetInBlocks", 16}, {"dataPaddingInBytes", 0}},
                ParamRecord{{"offsetInBlocks", 768}, {"dataPaddingInBytes", 0}},
                ParamRecord{{"offsetInBlocks", 0}, {"dataPaddingInBytes", 1}},
            })
            .combine("copyDepth", {1, 2})
            .combine("mipLevel", {0, 2});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint64_t offsetInBlocks = t.param<uint64_t>("offsetInBlocks");
        const uint64_t dataPaddingInBytes = t.param<uint64_t>("dataPaddingInBytes");
        const uint32_t copyDepth = static_cast<uint32_t>(t.param<uint64_t>("copyDepth"));
        const uint32_t mipLevel = static_cast<uint32_t>(t.param<uint64_t>("mipLevel"));
        t.skipIfTextureFormatNotSupported(format);
        // depthStencilFormatAspectSize(format, 'stencil-only') is 1 for every stencil format; the
        // stencil aspect copies/writes as 'stencil8'.
        constexpr WGPUTextureFormat kStencilAspectFormat = WGPUTextureFormat_Stencil8;
        const uint64_t bytesPerBlock = getBlockInfoForTextureFormat(kStencilAspectFormat).bytesPerBlock;
        const uint64_t initialDataOffset = offsetInBlocks * bytesPerBlock;
        const WGPUExtent3D copySize{3, 3, copyDepth};
        constexpr uint32_t rowsPerImage = 3;
        constexpr uint32_t bytesPerRow = 256;

        const WGPUExtent3D textureSize{copySize.width << mipLevel, copySize.height << mipLevel, copyDepth};

        TexelCopyBufferLayout dataLayout;
        dataLayout.offset = initialDataOffset;
        dataLayout.bytesPerRow = bytesPerRow;
        dataLayout.rowsPerImage = rowsPerImage;
        const uint64_t minDataSize = dataBytesForCopyOrFail(
            dataLayout, kStencilAspectFormat, copySize, /*requireStrictAlignment=*/false);
        const uint64_t initialDataSize = minDataSize + dataPaddingInBytes;

        WGPUTextureDescriptor srcDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        srcDesc.size = textureSize;
        srcDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        srcDesc.format = format;
        srcDesc.mipLevelCount = mipLevel + 1;
        srcDesc.dimension = WGPUTextureDimension_2D;
        srcDesc.sampleCount = 1;
        WGPUTexture srcTexture = t.createTextureTracked(srcDesc);

        const WGPUExtent3D writeSize{textureSize.width >> mipLevel, textureSize.height >> mipLevel, textureSize.depthOrArrayLayers};
        const std::vector<uint8_t> initialData =
            generateDataView(alignTo(initialDataSize, kBufferSizeAlignment), 0, initialDataOffset);

        {
            WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            destination.texture = srcTexture;
            destination.aspect = WGPUTextureAspect_StencilOnly;
            destination.mipLevel = mipLevel;
            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.offset = initialDataOffset;
            layout.bytesPerRow = bytesPerRow;
            layout.rowsPerImage = rowsPerImage;
            wgpuQueueWriteTexture(t.queue(), &destination, initialData.data(), initialData.size(), &layout, &writeSize);
        }

        // Everything below here was previously checkStencilTextureContent in image_copy.spec.ts
        constexpr uint32_t stencilBitCount = 8;

        // Prepare the uniform buffer that stores the bit indices (from 0 to 7) at stride 256
        // (required by Dynamic Buffer Offset).
        constexpr uint32_t uniformBufferSize = kMinDynamicBufferOffsetAlignment * (stencilBitCount - 1) + 4;
        std::vector<uint32_t> uniformBufferData(uniformBufferSize / 4, 0);
        for (uint32_t i = 1; i < stencilBitCount; ++i) {
            uniformBufferData[(kMinDynamicBufferOffsetAlignment / 4) * i] = i;
        }
        WGPUBuffer uniformBuffer = t.makeBufferWithContents(
            uniformBufferData.data(),
            uniformBufferData.size() * sizeof(uint32_t),
            WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);

        // Prepare the base render pipeline descriptor (all the settings expect stencilReadMask).
        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Fragment;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
        layoutEntry.buffer.minBindingSize = 4;
        layoutEntry.buffer.hasDynamicOffset = WGPU_TRUE;
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries = &layoutEntry;
        WGPUBindGroupLayout bindGroupLayout = t.createBindGroupLayoutTracked(bglDesc);

        WGPUShaderModule vertexModule = t.createShaderModuleTracked(kStencilAccumulationVertexShader);
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kStencilAccumulationFragmentShader);

        // As we implement "rendering one bit in each draw() call" with blending operation 'add',
        // the format of outputTexture must support blending.
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_One;
        blend.color.operation = WGPUBlendOperation_Add;
        // alpha: {} (the GPUBlendComponent defaults).
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_Zero;
        blend.alpha.operation = WGPUBlendOperation_Add;
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = WGPUTextureFormat_R8Unorm;
        target.blend = &blend;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fragmentModule;
        fragment.entryPoint = stringView("main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        depthStencil.depthCompare = WGPUCompareFunction_Always;
        depthStencil.format = format;
        depthStencil.stencilFront = stencilFace(WGPUCompareFunction_Equal);
        depthStencil.stencilBack = stencilFace(WGPUCompareFunction_Equal);

        WGPURenderPipelineDescriptor renderPipelineDescriptorBase = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        renderPipelineDescriptorBase.layout = createPipelineLayout(t, bindGroupLayout);
        renderPipelineDescriptorBase.vertex.module = vertexModule;
        renderPipelineDescriptorBase.vertex.entryPoint = stringView("main");
        renderPipelineDescriptorBase.fragment = &fragment;
        renderPipelineDescriptorBase.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        renderPipelineDescriptorBase.multisample.count = 1;
        renderPipelineDescriptorBase.depthStencil = &depthStencil;

        // Prepare the bindGroup that contains uniformBuffer and referenceTexture.
        WGPUBindGroupEntry bindGroupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bindGroupEntry.binding = 0;
        bindGroupEntry.buffer = uniformBuffer;
        bindGroupEntry.offset = 0;
        bindGroupEntry.size = 4;
        WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.layout = bindGroupLayout;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries = &bindGroupEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);

        // "Copy" the stencil value into the color attachment with 8 draws in one render pass. Each
        // draw will "Copy" one bit of the stencil value into the color attachment. The bit of the
        // stencil value is specified by setStencilReference().
        // getTextureCopyLayout(format, '2d', [w, h, 1], { mipLevel, aspect: 'stencil-only' }):
        // the stencil aspect resolves to 'stencil8'.
        const TextureCopyLayout copyFromOutputTextureLayout = getTextureCopyLayout(
            kStencilAspectFormat,
            WGPUTextureDimension_2D,
            WGPUExtent3D{textureSize.width, textureSize.height, 1},
            mipLevel);
        const WGPUExtent3D outputTextureSize{
            copyFromOutputTextureLayout.mipSize.width,
            copyFromOutputTextureLayout.mipSize.height,
            1,
        };
        WGPUTextureDescriptor outputDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        outputDesc.format = WGPUTextureFormat_R8Unorm;
        outputDesc.size = outputTextureSize;
        outputDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
        outputDesc.mipLevelCount = 1;
        outputDesc.sampleCount = 1;
        outputDesc.dimension = WGPUTextureDimension_2D;
        WGPUTexture outputTexture = t.createTextureTracked(outputDesc);

        for (uint32_t stencilTextureLayer = 0; stencilTextureLayer < textureSize.depthOrArrayLayers; ++stencilTextureLayer) {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

            WGPUTextureViewDescriptor srcViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            srcViewDesc.dimension = WGPUTextureViewDimension_2D;
            srcViewDesc.baseMipLevel = mipLevel;
            srcViewDesc.mipLevelCount = 1;
            srcViewDesc.baseArrayLayer = stencilTextureLayer;
            srcViewDesc.arrayLayerCount = 1;
            WGPURenderPassDepthStencilAttachment depthStencilAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            depthStencilAttachment.view = t.createViewTracked(srcTexture, srcViewDesc);
            if (isDepthTextureFormat(format)) {
                depthStencilAttachment.depthClearValue = 0.0f;
                depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
                depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
            }
            if (isStencilTextureFormat(format)) {
                depthStencilAttachment.stencilLoadOp = WGPULoadOp_Load;
                depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Store;
            }

            WGPUTextureViewDescriptor outputViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttach.view = t.createViewTracked(outputTexture, outputViewDesc);
            colorAttach.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
            colorAttach.loadOp = WGPULoadOp_Clear;
            colorAttach.storeOp = WGPUStoreOp_Store;

            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttach;
            passDesc.depthStencilAttachment = &depthStencilAttachment;
            WGPURenderPassEncoder renderPass = t.beginRenderPassTracked(encoder, &passDesc);

            for (uint32_t stencilBitIndex = 0; stencilBitIndex < stencilBitCount; ++stencilBitIndex) {
                depthStencil.stencilReadMask = 1u << stencilBitIndex;
                WGPURenderPipeline renderPipeline = t.createRenderPipelineTracked(renderPipelineDescriptorBase);

                wgpuRenderPassEncoderSetPipeline(renderPass, renderPipeline);
                wgpuRenderPassEncoderSetStencilReference(renderPass, 1u << stencilBitIndex);
                const uint32_t dynamicOffset = stencilBitIndex * kMinDynamicBufferOffsetAlignment;
                wgpuRenderPassEncoderSetBindGroup(renderPass, 0, bindGroup, 1, &dynamicOffset);
                wgpuRenderPassEncoderDraw(renderPass, 6, 1, 0, 0);
            }
            wgpuRenderPassEncoderEnd(renderPass);

            // Check outputTexture by copying the content of outputTexture into outputStagingBuffer
            // and checking all the data in outputStagingBuffer.
            WGPUBuffer outputStagingBuffer = createBuffer(
                t,
                copyFromOutputTextureLayout.byteLength,
                WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
            {
                WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                source.texture = outputTexture;
                WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
                destination.buffer = outputStagingBuffer;
                destination.layout.bytesPerRow = copyFromOutputTextureLayout.bytesPerRow;
                destination.layout.rowsPerImage = copyFromOutputTextureLayout.rowsPerImage;
                wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &outputTextureSize);
            }

            submit(t, encoder);

            // Check the valid data in outputStagingBuffer once per row.
            for (uint32_t y = 0; y < copyFromOutputTextureLayout.mipSize.height; ++y) {
                const uint64_t dataStart = initialDataOffset
                    + static_cast<uint64_t>(bytesPerRow) * rowsPerImage * stencilTextureLayer
                    + static_cast<uint64_t>(bytesPerRow) * y;
                // initialData.slice(dataStart, dataStart + width) (slice clamps to the array end).
                const size_t begin = static_cast<size_t>(std::min<uint64_t>(dataStart, initialData.size()));
                const size_t end = static_cast<size_t>(
                    std::min<uint64_t>(dataStart + copyFromOutputTextureLayout.mipSize.width, initialData.size()));
                t.expectGPUBufferValuesEqual(
                    outputStagingBuffer,
                    initialData.data() + begin,
                    end - begin,
                    static_cast<uint64_t>(copyFromOutputTextureLayout.bytesPerRow) * y);
            }
        }
    });

} // namespace
