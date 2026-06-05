// Ported from gpuweb/cts src/webgpu/api/operation/rendering/stencil.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

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
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (isSingleEncoderMultiplePass) {
        wgpuRenderPassEncoderEnd(pass);
    }

    for (const TestState& state : testStates) {
        if (isSingleEncoderMultiplePass) {
            pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
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

CTS_TEST(g, "stencil_accumulation")
    .unimplemented("stencil bit-accumulation + stencil-aspect copy readback deferred");

} // namespace
