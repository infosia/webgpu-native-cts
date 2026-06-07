// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/queries/occlusionQuery.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Minimal port: occlusion_query,basic + occlusion_query,empty; OcclusionQueryTest matrix + timestampQuery deferred.

#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,queries,occlusionQuery",
    "Occlusion query operation tests.");

constexpr uint32_t kSize = 4;

constexpr std::string_view kVertexShader = R"(
@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
  var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
      vec2<f32>(-1.0, -3.0), vec2<f32>(3.0, 1.0), vec2<f32>(-1.0, 1.0));
  return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
}
)";

constexpr std::string_view kFragmentShader = R"(
@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(1.0, 0.0, 0.0, 1.0); }
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Creates a 4x4 rgba8unorm render target texture.
WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kSize, kSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

// Creates an 8-byte (one u64) resolve buffer for occlusion query results.
WGPUBuffer createResolveBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = sizeof(uint64_t);
    desc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
    return t.createBufferTracked(desc);
}

// Creates the fullscreen-triangle render pipeline (rgba8unorm target).
WGPURenderPipeline createPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = vertModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

// Submits the command buffer and reads back the u64 occlusion result.
// `expectNonZero` selects basic (true) vs empty (false) validation.
void runOcclusionQueryTest(AllFeaturesMaxLimitsGpuTest& t, bool draw) {
    WGPUTexture target = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(target, viewDesc);

    // Create the query set (occlusion, 1 query).
    WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    qsDesc.type = WGPUQueryType_Occlusion;
    qsDesc.count = 1;
    WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

    WGPUBuffer resolveBuffer = createResolveBuffer(t);

    // Build color attachment.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    // Attach the query set to the render pass.
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;
    passDesc.occlusionQuerySet = querySet;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
    if (draw) {
        WGPURenderPipeline pipeline = createPipeline(t);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    }
    wgpuRenderPassEncoderEndOcclusionQuery(pass);
    wgpuRenderPassEncoderEnd(pass);

    // Resolve the query into the resolve buffer.
    wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 1, resolveBuffer, 0);

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    wgpuQuerySetRelease(querySet);

    // Verify: read back the 8-byte u64 result.
    const bool expectNonZero = draw;
    t.expectGPUBufferValuesPassCheck(
        resolveBuffer,
        [expectNonZero](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < sizeof(uint64_t)) {
                std::ostringstream msg;
                msg << "resolve buffer too small: " << len << " < 8";
                return msg.str();
            }
            uint64_t result = 0;
            std::memcpy(&result, actual, sizeof(uint64_t));
            if (expectNonZero && result == 0) {
                return std::string("occlusion query result is 0, expected > 0 (covering draw)");
            }
            if (!expectNonZero && result != 0) {
                std::ostringstream msg;
                msg << "occlusion query result is " << result << ", expected 0 (no draw)";
                return msg.str();
            }
            return std::nullopt;
        },
        0,
        sizeof(uint64_t));
}

CTS_TEST(g, "occlusion_query,basic")
    .desc("A fullscreen-triangle draw bracketed by beginOcclusionQuery/endOcclusionQuery produces a non-zero result.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runOcclusionQueryTest(t, /*draw=*/true);
    });

CTS_TEST(g, "occlusion_query,empty")
    .desc("An empty occlusion query (begin/end with no draw) produces a zero result.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runOcclusionQueryTest(t, /*draw=*/false);
    });

} // namespace
