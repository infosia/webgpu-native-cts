// Ported from gpuweb/cts src/webgpu/api/operation/render_pass/storeop2.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
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
    "api,operation,render_pass,storeop2",
    "Render pass storeOp operation tests.");

constexpr uint32_t kBytesPerPixel = 1;

constexpr std::string_view kVertexShader = R"(
@vertex fn main(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {
  let pos = array(
    vec2f(1, -1),
    vec2f(1, 1),
    vec2f(-1, 1)
  );
  return vec4f(pos[vertexIndex], 0, 1);
}
)";

constexpr std::string_view kFragmentShader = R"(
@fragment fn main() -> @location(0) vec4f {
  return vec4f(1, 0, 0, 1);
}
)";

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

WGPUStoreOp parseStoreOp(std::string_view value) {
    if (value == "store") {
        return WGPUStoreOp_Store;
    }
    if (value == "discard") {
        return WGPUStoreOp_Discard;
    }
    std::abort();
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_R8Unorm;
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

WGPURenderPipeline createPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_R8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

void expectSingleR8UnormByte(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, uint8_t expected) {
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(kBytesPerPixel, kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, bytesPerRow, WGPUExtent3D{1, 1, 1});
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < kBytesPerPixel) {
                return std::string("r8unorm readback buffer is too small");
            }
            if (actual[0] != expected) {
                std::ostringstream message;
                message << "r8unorm mismatch: expected " << static_cast<int>(expected)
                        << ", got " << static_cast<int>(actual[0]);
                return message.str();
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

CTS_TEST(g, "storeOp_controls_whether_1x1_drawn_quad_is_stored")
    .params([](ParamsBuilder u) {
        return u.combine("storeOp", {Value("store"), Value("discard")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string storeOp = t.param<std::string>("storeOp");
        WGPUTexture target = createRenderTarget(t);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView targetView = t.createViewTracked(target, viewDesc);
        WGPURenderPipeline pipeline = createPipeline(t);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = targetView;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = parseStoreOp(storeOp);
        colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        submit(t, encoder);

        expectSingleR8UnormByte(t, target, storeOp == "store" ? 255 : 0);
    });

} // namespace
