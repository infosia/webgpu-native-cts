// Ported from gpuweb/cts src/webgpu/api/operation/rendering/depth_bias.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,depth_bias",
    "Depth bias rendering operation tests.");

constexpr uint32_t kWidth = 1;
constexpr uint32_t kHeight = 1;
constexpr uint32_t kDepthBytesPerPixel = 4;
constexpr int32_t kPointTwoFiveBiasForPointTwoFiveZOnFloat = 8388608;
constexpr double kDepthTolerance = 1.0 / 256.0;

constexpr std::string_view kFlatVertexShader = R"(
@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec2<f32>, 6>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>(1.0, -1.0),
    vec2<f32>(-1.0, 1.0),
    vec2<f32>(-1.0, 1.0),
    vec2<f32>(1.0, -1.0),
    vec2<f32>(1.0, 1.0)
  );
  return vec4<f32>(pos[VertexIndex], 0.25, 1.0);
}
)";

constexpr std::string_view kTiltedXVertexShader = R"(
@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
  var pos = array<vec3<f32>, 6>(
    vec3<f32>(-1.0, -1.0, 0.0),
    vec3<f32>(1.0, -1.0, 0.0),
    vec3<f32>(-1.0, 1.0, 0.5),
    vec3<f32>(-1.0, 1.0, 0.5),
    vec3<f32>(1.0, -1.0, 0.0),
    vec3<f32>(1.0, 1.0, 0.5)
  );
  return vec4<f32>(pos[VertexIndex], 1.0);
}
)";

constexpr std::string_view kRedFragmentShader = R"(
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(1.0, 0.0, 0.0, 1.0);
}
)";

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
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
    desc.usage = WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

WGPUTexture createDepthTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kWidth, kHeight, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_Depth32Float;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

WGPURenderPipeline createRenderPipelineForTest(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view vertexShader,
    const WGPUDepthStencilState& depthStencilState) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(vertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kRedFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    desc.depthStencil = &depthStencilState;
    return t.createRenderPipelineTracked(desc);
}

WGPUDepthStencilState depthStencilState(int32_t bias, float biasSlopeScale, float biasClamp) {
    WGPUDepthStencilState state = WGPU_DEPTH_STENCIL_STATE_INIT;
    state.format = WGPUTextureFormat_Depth32Float;
    state.depthCompare = WGPUCompareFunction_Always;
    state.depthWriteEnabled = WGPUOptionalBool_True;
    state.depthBias = bias;
    state.depthBiasSlopeScale = biasSlopeScale;
    state.depthBiasClamp = biasClamp;
    return state;
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void expectDepthValueInTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, float expectedDepth) {
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(kDepthBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(kDepthBytesPerPixel, kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_DepthOnly;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = bytesPerRow;
    destination.layout.rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUExtent3D copySize{kWidth, kHeight, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < sizeof(float)) {
                return std::string("depth readback buffer is too small");
            }
            float actualDepth = 0.0f;
            std::memcpy(&actualDepth, actual, sizeof(actualDepth));
            if (std::abs(static_cast<double>(actualDepth) - static_cast<double>(expectedDepth)) <= kDepthTolerance) {
                return std::nullopt;
            }
            std::ostringstream message;
            message << "depth mismatch: expected " << expectedDepth << ", got " << actualDepth;
            return message.str();
        },
        0,
        static_cast<size_t>(byteLength));
}

void runDepthBiasTest(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string quadAngle = t.param<std::string>("quadAngle");
    const int32_t bias = static_cast<int32_t>(t.param<int64_t>("bias"));
    const float biasSlopeScale = static_cast<float>(t.param<double>("biasSlopeScale"));
    const float biasClamp = static_cast<float>(t.param<double>("biasClamp"));
    const float expectedDepth = static_cast<float>(t.param<double>("expectedDepth"));

    WGPUTexture color = createColorTarget(t);
    WGPUTexture depth = createDepthTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(color, viewDesc);
    WGPUTextureView depthView = t.createViewTracked(depth, viewDesc);

    WGPUDepthStencilState depthStencil = depthStencilState(bias, biasSlopeScale, biasClamp);
    WGPURenderPipeline pipeline = createRenderPipelineForTest(
        t,
        quadAngle == "flat" ? kFlatVertexShader : kTiltedXVertexShader,
        depthStencil);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDepthStencilAttachment depthAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    depthAttachment.view = depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 0.0f;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    expectDepthValueInTexture(t, depth, expectedDepth);
}

CTS_TEST(g, "depth_bias")
    .params([](ParamsBuilder u) {
        return u.combineWithParams({
            ParamRecord{{"quadAngle", "flat"}, {"bias", kPointTwoFiveBiasForPointTwoFiveZOnFloat}, {"biasSlopeScale", 0.0}, {"biasClamp", 0.0}, {"expectedDepth", 0.5}},
            ParamRecord{{"quadAngle", "flat"}, {"bias", kPointTwoFiveBiasForPointTwoFiveZOnFloat}, {"biasSlopeScale", 0.0}, {"biasClamp", 0.125}, {"expectedDepth", 0.375}},
            ParamRecord{{"quadAngle", "flat"}, {"bias", -kPointTwoFiveBiasForPointTwoFiveZOnFloat}, {"biasSlopeScale", 0.0}, {"biasClamp", 0.125}, {"expectedDepth", 0.0}},
            ParamRecord{{"quadAngle", "flat"}, {"bias", -kPointTwoFiveBiasForPointTwoFiveZOnFloat}, {"biasSlopeScale", 0.0}, {"biasClamp", -0.125}, {"expectedDepth", 0.125}},
            ParamRecord{{"quadAngle", "tilted"}, {"bias", 0}, {"biasSlopeScale", 0.0}, {"biasClamp", 0.0}, {"expectedDepth", 0.25}},
            ParamRecord{{"quadAngle", "tilted"}, {"bias", 0}, {"biasSlopeScale", 1.0}, {"biasClamp", 0.0}, {"expectedDepth", 0.75}},
            ParamRecord{{"quadAngle", "tilted"}, {"bias", 0}, {"biasSlopeScale", -0.5}, {"biasClamp", 0.0}, {"expectedDepth", 0.0}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runDepthBiasTest(t);
    });

} // namespace
