// Ported from gpuweb/cts src/webgpu/api/operation/rendering/color_target_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

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

WGPURenderPassEncoder beginRenderPass(
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
    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
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
    WGPURenderPassEncoder pass = beginRenderPass(encoder, view, loadOp);
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

CTS_TEST(g, "blending,GPUBlendComponent")
    .unimplemented("blend component matrix tests are deferred");

CTS_TEST(g, "blending,formats")
    .unimplemented("blend format matrix tests are deferred");

CTS_TEST(g, "blending,clamping")
    .unimplemented("blend clamping tests are deferred");

} // namespace
