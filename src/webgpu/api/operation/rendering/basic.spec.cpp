// Ported from gpuweb/cts src/webgpu/api/operation/rendering/basic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,basic",
    "Basic rendering operation tests.");

constexpr std::string_view kFullscreenVertexShader = R"(
@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
  var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
      vec2<f32>(-1.0, -3.0), vec2<f32>(3.0, 1.0), vec2<f32>(-1.0, 1.0));
  return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
}
)";

constexpr std::string_view kGreenFragmentShader = R"(
@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

WGPUBuffer createCopyBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = 4;
    desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    return t.createBufferTracked(desc);
}

WGPUTexture createColorAttachment(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

WGPURenderPassEncoder beginColorRenderPass(
    WGPUCommandEncoder encoder,
    WGPUTextureView view,
    WGPUColor clearValue) {
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = clearValue;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;
    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
}

void copyColorToBuffer(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder, WGPUTexture color, WGPUBuffer dst) {
    t.copyTextureToBuffer(encoder, color, dst, 256, WGPUExtent3D{1, 1, 1});
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void expectGreen(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer dst) {
    constexpr std::array<uint8_t, 4> expected = {0x00, 0xff, 0x00, 0xff};
    t.expectGPUBufferValuesEqual(dst, expected.data(), expected.size());
}

WGPURenderPipeline createFullscreenQuadPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kFullscreenVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kGreenFragmentShader);

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
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

CTS_TEST(g, "clear")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBuffer dst = createCopyBuffer(t);
        WGPUTexture color = createColorAttachment(t);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(color, viewDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginColorRenderPass(encoder, view, WGPUColor{0.0, 1.0, 0.0, 1.0});
        wgpuRenderPassEncoderEnd(pass);
        copyColorToBuffer(t, encoder, color, dst);
        submit(t, encoder);

        expectGreen(t, dst);
    });

CTS_TEST(g, "fullscreen_quad")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBuffer dst = createCopyBuffer(t);
        WGPUTexture color = createColorAttachment(t);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(color, viewDesc);
        WGPURenderPipeline pipeline = createFullscreenQuadPipeline(t);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginColorRenderPass(encoder, view, WGPUColor{1.0, 0.0, 0.0, 1.0});
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        copyColorToBuffer(t, encoder, color, dst);
        submit(t, encoder);

        expectGreen(t, dst);
    });

CTS_TEST(g, "large_draw")
    .unimplemented("large draw rendering tests are deferred");

} // namespace
