// Ported from gpuweb/cts src/webgpu/api/operation/rendering/depth.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

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
    "api,operation,rendering,depth",
    "Depth rendering operation tests.");

constexpr uint32_t kWidth = 1;
constexpr uint32_t kHeight = 1;
constexpr uint32_t kBytesPerPixel = 4;
constexpr float kMiddleDepthValue = 0.4f;

constexpr std::array<double, 4> kWhite = {1.0, 1.0, 1.0, 1.0};
constexpr std::array<double, 4> kBlack = {0.0, 0.0, 0.0, 1.0};
constexpr std::array<double, 4> kRed = {1.0, 0.0, 0.0, 1.0};
constexpr std::array<double, 4> kGreen = {0.0, 1.0, 0.0, 1.0};

constexpr std::array<WGPUTextureFormat, 5> kDepthTextureFormats = {{
    WGPUTextureFormat_Depth16Unorm,
    WGPUTextureFormat_Depth32Float,
    WGPUTextureFormat_Depth24Plus,
    WGPUTextureFormat_Depth24PlusStencil8,
    WGPUTextureFormat_Depth32FloatStencil8,
}};

constexpr std::array<WGPUCompareFunction, 8> kCompareFunctions = {{
    WGPUCompareFunction_Never,
    WGPUCompareFunction_Less,
    WGPUCompareFunction_LessEqual,
    WGPUCompareFunction_Equal,
    WGPUCompareFunction_NotEqual,
    WGPUCompareFunction_GreaterEqual,
    WGPUCompareFunction_Greater,
    WGPUCompareFunction_Always,
}};

struct PipelineForTest {
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPURenderPipeline pipeline = nullptr;
};

struct TestState {
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    float depth = 0.0f;
    std::array<float, 4> color = {};
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

std::array<double, 4> colorAsDouble(std::array<float, 4> color) {
    return {color[0], color[1], color[2], color[3]};
}

std::vector<Value> depthFormatValues() {
    return formatIdentifierValues(kDepthTextureFormats);
}

std::vector<Value> compareFunctionValues() {
    std::vector<Value> values;
    values.reserve(kCompareFunctions.size());
    for (WGPUCompareFunction compare : kCompareFunctions) {
        values.emplace_back(static_cast<int64_t>(compare));
    }
    return values;
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

WGPUTexture createDepthTarget(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
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

std::string vertexShaderForDepth(float depth) {
    std::ostringstream shader;
    shader << "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
           << "  return vec4<f32>(0.0, 0.0, " << depth << ", 1.0);\n"
           << "}\n";
    return shader.str();
}

constexpr std::string_view kUniformColorFragmentShader = R"(
struct Params { color : vec4<f32> };
@group(0) @binding(0) var<uniform> params : Params;

@fragment fn main() -> @location(0) vec4<f32> {
  return params.color;
}
)";

PipelineForTest createRenderPipelineForTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const WGPUDepthStencilState& depthStencilState,
    float depth) {
    WGPUBindGroupLayout bindGroupLayout = createUniformBindGroupLayout(t);
    WGPUPipelineLayout layout = createPipelineLayout(t, bindGroupLayout);
    const std::string vertexShader = vertexShaderForDepth(depth);
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(vertexShader);
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

WGPUStencilFaceState keepAlwaysStencilFace() {
    WGPUStencilFaceState face = WGPU_STENCIL_FACE_STATE_INIT;
    face.compare = WGPUCompareFunction_Always;
    face.failOp = WGPUStencilOperation_Keep;
    face.depthFailOp = WGPUStencilOperation_Keep;
    face.passOp = WGPUStencilOperation_Keep;
    return face;
}

WGPUDepthStencilState depthState(
    WGPUTextureFormat format,
    WGPUCompareFunction compare,
    WGPUOptionalBool writeEnabled,
    bool includeStencil = false) {
    WGPUDepthStencilState state = WGPU_DEPTH_STENCIL_STATE_INIT;
    state.format = format;
    state.depthCompare = compare;
    state.depthWriteEnabled = writeEnabled;
    if (includeStencil) {
        state.stencilFront = keepAlwaysStencilFace();
        state.stencilBack = keepAlwaysStencilFace();
        state.stencilReadMask = 0xff;
        state.stencilWriteMask = 0xff;
    }
    return state;
}

WGPURenderPassDepthStencilAttachment depthAttachment(
    WGPUTextureView view,
    WGPUTextureFormat format,
    WGPULoadOp loadOp,
    float clearValue = 0.0f) {
    WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.depthLoadOp = loadOp;
    attachment.depthStoreOp = WGPUStoreOp_Store;
    if (loadOp == WGPULoadOp_Clear) {
        attachment.depthClearValue = clearValue;
    }
    if (isStencilTextureFormat(format)) {
        attachment.stencilLoadOp = loadOp;
        attachment.stencilStoreOp = WGPUStoreOp_Store;
        attachment.stencilClearValue = 0;
    }
    return attachment;
}

void drawState(WGPURenderPassEncoder pass, AllFeaturesMaxLimitsGpuTest& t, const TestState& state) {
    PipelineForTest pipeline = createRenderPipelineForTest(t, state.depthStencil, state.depth);
    WGPUBindGroup bindGroup = createBindGroupForTest(t, pipeline.bindGroupLayout, state.color);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline.pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
}

void runDepthStateTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::vector<TestState>& testStates,
    std::array<double, 4> expectedColor) {
    WGPUTexture color = createColorTarget(t);
    WGPUTexture depth = createDepthTarget(t, WGPUTextureFormat_Depth24PlusStencil8);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(color, viewDesc);
    WGPUTextureView depthView = t.createViewTracked(depth, viewDesc);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDepthStencilAttachment dsAttachment = depthAttachment(
        depthView,
        WGPUTextureFormat_Depth24PlusStencil8,
        WGPULoadOp_Load);

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &dsAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    for (const TestState& state : testStates) {
        drawState(pass, t, state);
    }
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    expectColorOkInTexture(t, color, expectedColor, WGPUExtent3D{kWidth, kHeight, 1});
}

bool comparePasses(WGPUCompareFunction compare, float source, float destination) {
    switch (compare) {
        case WGPUCompareFunction_Never:
            return false;
        case WGPUCompareFunction_Less:
            return source < destination;
        case WGPUCompareFunction_LessEqual:
            return source <= destination;
        case WGPUCompareFunction_Equal:
            return source == destination;
        case WGPUCompareFunction_NotEqual:
            return source != destination;
        case WGPUCompareFunction_GreaterEqual:
            return source >= destination;
        case WGPUCompareFunction_Greater:
            return source > destination;
        case WGPUCompareFunction_Always:
            return true;
        default:
            std::abort();
    }
}

TestState makeState(
    WGPUCompareFunction compare,
    WGPUOptionalBool writeEnabled,
    float depth,
    std::array<double, 4> color,
    bool includeStencil = true,
    WGPUTextureFormat format = WGPUTextureFormat_Depth24PlusStencil8) {
    return TestState{depthState(format, compare, writeEnabled, includeStencil), depth, colorAsFloat(color)};
}

CTS_TEST(g, "depth_disabled")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<TestState, 3> states = {{
            makeState(WGPUCompareFunction_Always, WGPUOptionalBool_False, 0.0f, kWhite),
            makeState(WGPUCompareFunction_Always, WGPUOptionalBool_False, 0.5f, kRed),
            makeState(WGPUCompareFunction_Always, WGPUOptionalBool_False, 1.0f, kGreen),
        }};
        for (size_t last = 0; last < states.size(); ++last) {
            std::vector<TestState> orderA;
            std::vector<TestState> orderB;
            for (size_t i = 0; i < states.size(); ++i) {
                if (i != last) {
                    orderA.push_back(states[i]);
                }
            }
            orderB = orderA;
            orderA.push_back(states[last]);
            std::reverse(orderB.begin(), orderB.end());
            orderB.push_back(states[last]);
            runDepthStateTest(t, orderA, colorAsDouble(states[last].color));
            runDepthStateTest(t, orderB, colorAsDouble(states[last].color));
        }
    });

CTS_TEST(g, "depth_write_disabled")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"depthWriteEnabled", false}, {"lastDepth", 1.0}, {"expected", "green"}},
            ParamRecord{{"depthWriteEnabled", false}, {"lastDepth", 0.0}, {"expected", "red"}},
            ParamRecord{{"depthWriteEnabled", true}, {"lastDepth", 1.0}, {"expected", "red"}},
            ParamRecord{{"depthWriteEnabled", true}, {"lastDepth", 0.0}, {"expected", "green"}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool writeEnabled = t.param<bool>("depthWriteEnabled");
        const float lastDepth = static_cast<float>(t.param<double>("lastDepth"));
        const std::array<double, 4> expected = t.param<std::string>("expected") == "green" ? kGreen : kRed;
        runDepthStateTest(t, {
            makeState(WGPUCompareFunction_Always, WGPUOptionalBool_True, 1.0f, kWhite),
            makeState(WGPUCompareFunction_Always, writeEnabled ? WGPUOptionalBool_True : WGPUOptionalBool_False, 0.0f, kRed),
            makeState(WGPUCompareFunction_Equal, WGPUOptionalBool_False, lastDepth, kGreen),
        }, expected);
    });

CTS_TEST(g, "depth_test_fail")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"secondDepth", 1.0}, {"lastDepth", 2.0}, {"expected", "white"}},
            ParamRecord{{"secondDepth", 0.0}, {"lastDepth", 2.0}, {"expected", "red"}},
            ParamRecord{{"secondDepth", 2.0}, {"lastDepth", 0.9}, {"expected", "green"}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const float secondDepth = static_cast<float>(t.param<double>("secondDepth"));
        const float lastDepth = static_cast<float>(t.param<double>("lastDepth"));
        const std::string expectedKey = t.param<std::string>("expected");
        const std::array<double, 4> expected = expectedKey == "white" ? kWhite : (expectedKey == "green" ? kGreen : kRed);
        runDepthStateTest(t, {
            makeState(WGPUCompareFunction_Always, WGPUOptionalBool_True, 1.0f, kWhite),
            makeState(WGPUCompareFunction_Less, WGPUOptionalBool_True, secondDepth, kRed),
            makeState(WGPUCompareFunction_Less, WGPUOptionalBool_True, lastDepth, kGreen),
        }, expected);
    });

CTS_TEST(g, "depth_compare_func")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthFormatValues())
            .combine("depthCompare", compareFunctionValues())
            .combine("depthClearValue", {1.0, 0.4, 0.0});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        t.skipIfTextureFormatNotSupported(format);
        const auto compare = static_cast<WGPUCompareFunction>(t.param<int64_t>("depthCompare"));
        const float depthClearValue = static_cast<float>(t.param<double>("depthClearValue"));

        WGPUTexture color = createColorTarget(t);
        WGPUTexture depth = createDepthTarget(t, format);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView colorView = t.createViewTracked(color, viewDesc);
        WGPUTextureView depthView = t.createViewTracked(depth, viewDesc);

        WGPUDepthStencilState state = depthState(format, compare, WGPUOptionalBool_True, false);
        PipelineForTest pipeline = createRenderPipelineForTest(t, state, kMiddleDepthValue);
        WGPUBindGroup bindGroup = createBindGroupForTest(t, pipeline.bindGroupLayout, colorAsFloat(kWhite));

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = colorView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDepthStencilAttachment dsAttachment = depthAttachment(depthView, format, WGPULoadOp_Clear, depthClearValue);

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.depthStencilAttachment = &dsAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline.pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        submit(t, encoder);

        expectColorOkInTexture(
            t,
            color,
            comparePasses(compare, kMiddleDepthValue, depthClearValue) ? kWhite : kBlack,
            WGPUExtent3D{kWidth, kHeight, 1});
    });

CTS_TEST(g, "reverse_depth")
    .params([](ParamsBuilder u) {
        return u.combine("reversed", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool reversed = t.param<bool>("reversed");
        constexpr std::string_view vertexShader = R"(
struct VertexOut {
  @builtin(position) position : vec4<f32>,
  @location(0) color : vec4<f32>,
};

@vertex fn main(@builtin(instance_index) instanceIndex : u32) -> VertexOut {
  var depths : array<f32, 4> = array<f32, 4>(0.2, 0.3, -0.1, 1.1);
  var colors : array<vec4<f32>, 4> = array<vec4<f32>, 4>(
      vec4<f32>(1.0, 0.0, 0.0, 1.0),
      vec4<f32>(0.0, 1.0, 0.0, 1.0),
      vec4<f32>(0.0, 0.0, 1.0, 1.0),
      vec4<f32>(1.0, 1.0, 1.0, 1.0));
  var output : VertexOut;
  output.position = vec4<f32>(0.5, 0.5, depths[instanceIndex], 1.0);
  output.color = colors[instanceIndex];
  return output;
}
)";
        constexpr std::string_view fragmentShader = R"(
@fragment fn main(@location(0) color : vec4<f32>) -> @location(0) vec4<f32> {
  return color;
}
)";
        WGPUTexture color = createColorTarget(t);
        WGPUTexture depth = createDepthTarget(t, WGPUTextureFormat_Depth32Float);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView colorView = t.createViewTracked(color, viewDesc);
        WGPUTextureView depthView = t.createViewTracked(depth, viewDesc);

        WGPUDepthStencilState depthStencil = depthState(
            WGPUTextureFormat_Depth32Float,
            reversed ? WGPUCompareFunction_Greater : WGPUCompareFunction_Less,
            WGPUOptionalBool_True,
            false);
        WGPUShaderModule vertexModule = t.createShaderModuleTracked(vertexShader);
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentShader);
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fragmentModule;
        fragment.entryPoint = stringView("main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.vertex.module = vertexModule;
        pipelineDesc.vertex.entryPoint = stringView("main");
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.fragment = &fragment;
        pipelineDesc.depthStencil = &depthStencil;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipelineDesc);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = colorView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};
        WGPURenderPassDepthStencilAttachment dsAttachment = depthAttachment(
            depthView,
            WGPUTextureFormat_Depth32Float,
            WGPULoadOp_Clear,
            reversed ? 0.0f : 1.0f);

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.depthStencilAttachment = &dsAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 1, 4, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        submit(t, encoder);

        expectColorOkInTexture(t, color, reversed ? kGreen : kRed, WGPUExtent3D{kWidth, kHeight, 1});
    });

} // namespace
