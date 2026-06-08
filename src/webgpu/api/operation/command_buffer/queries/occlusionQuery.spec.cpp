// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/queries/occlusionQuery.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Minimal port: occlusion_query,basic + occlusion_query,empty + occlusion_query,depth + occlusion_query,stencil;
// OcclusionQueryTest matrix + timestampQuery deferred.

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

// Vertex shader with a pipeline-overridable z value.
// Default zval = 0.0; set per pipeline via WGPUConstantEntry.
constexpr std::string_view kVertexShaderWithZOverride = R"(
override zval : f32 = 0.0;
@vertex fn main(@builtin(vertex_index) VertexIndex : u32) -> @builtin(position) vec4<f32> {
  var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
      vec2<f32>(-1.0, -3.0), vec2<f32>(3.0, 1.0), vec2<f32>(-1.0, 1.0));
  return vec4<f32>(pos[VertexIndex], zval, 1.0);
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

// Creates a 4x4 depth24plus-stencil8 depth-stencil texture.
WGPUTexture createDepthStencilTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kSize, kSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_Depth24PlusStencil8;
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

// Creates a 16-byte (two u64) resolve buffer for two occlusion query results.
WGPUBuffer createResolveBuffer2(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = 2 * sizeof(uint64_t);
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

// Creates a render pipeline with depth-stencil state and a z-override constant.
// `zval` is set via a pipeline-overridable constant in the vertex shader.
WGPURenderPipeline createDepthPipeline(AllFeaturesMaxLimitsGpuTest& t, double zval) {
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShaderWithZOverride);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    // Stencil face: compare Always, all ops Keep (stencil is unused in the depth test).
    WGPUStencilFaceState stencilFace = WGPU_STENCIL_FACE_STATE_INIT;
    stencilFace.compare = WGPUCompareFunction_Always;
    stencilFace.failOp = WGPUStencilOperation_Keep;
    stencilFace.depthFailOp = WGPUStencilOperation_Keep;
    stencilFace.passOp = WGPUStencilOperation_Keep;

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
    depthStencil.depthCompare = WGPUCompareFunction_Less;
    depthStencil.stencilFront = stencilFace;
    depthStencil.stencilBack = stencilFace;
    depthStencil.stencilReadMask = 0xff;
    depthStencil.stencilWriteMask = 0xff;

    // Pipeline-overridable constant: zval.
    WGPUConstantEntry zConst = WGPU_CONSTANT_ENTRY_INIT;
    zConst.key = stringView("zval");
    zConst.value = zval;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = vertModule;
    desc.vertex.entryPoint = stringView("main");
    desc.vertex.constantCount = 1;
    desc.vertex.constants = &zConst;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    desc.depthStencil = &depthStencil;
    return t.createRenderPipelineTracked(desc);
}

// Creates a render pipeline for the stencil occlusion test.
// depthCompare=Always, stencilFront/Back={compare=Equal, all ops Keep}.
WGPURenderPipeline createStencilPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    // Stencil face: compare Equal, all ops Keep.
    WGPUStencilFaceState stencilFace = WGPU_STENCIL_FACE_STATE_INIT;
    stencilFace.compare = WGPUCompareFunction_Equal;
    stencilFace.failOp = WGPUStencilOperation_Keep;
    stencilFace.depthFailOp = WGPUStencilOperation_Keep;
    stencilFace.passOp = WGPUStencilOperation_Keep;

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
    depthStencil.depthCompare = WGPUCompareFunction_Always;
    depthStencil.stencilFront = stencilFace;
    depthStencil.stencilBack = stencilFace;
    depthStencil.stencilReadMask = 0xff;
    depthStencil.stencilWriteMask = 0xff;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = vertModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    desc.depthStencil = &depthStencil;
    return t.createRenderPipelineTracked(desc);
}

// Verifies a 16-byte resolve buffer containing two u64 occlusion query results.
// Requires result[0] == 0 (occluded) and result[1] != 0 (passing).
void verifyTwoQueryResults(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer resolveBuffer) {
    t.expectGPUBufferValuesPassCheck(
        resolveBuffer,
        [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 2 * sizeof(uint64_t)) {
                std::ostringstream msg;
                msg << "resolve buffer too small: " << len << " < 16";
                return msg.str();
            }
            uint64_t q0 = 0, q1 = 0;
            std::memcpy(&q0, actual,                    sizeof(uint64_t));
            std::memcpy(&q1, actual + sizeof(uint64_t), sizeof(uint64_t));
            if (q0 != 0) {
                std::ostringstream msg;
                msg << "query[0] (occluded draw) expected 0, got " << q0;
                return msg.str();
            }
            if (q1 == 0) {
                return std::string("query[1] (passing draw) expected > 0, got 0");
            }
            return std::nullopt;
        },
        0,
        2 * sizeof(uint64_t));
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

// Tests that a draw failing the depth test (z=0.9, depthClear=0.5, compare=Less)
// produces query result 0, while a draw passing (z=0.1) produces a non-zero result.
void runDepthOcclusionQueryTest(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTexture colorTarget = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(colorTarget, viewDesc);

    WGPUTexture dsTarget = createDepthStencilTarget(t);
    WGPUTextureView dsView = t.createViewTracked(dsTarget, viewDesc);

    // Create the query set (occlusion, 2 queries).
    WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    qsDesc.type = WGPUQueryType_Occlusion;
    qsDesc.count = 2;
    WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

    WGPUBuffer resolveBuffer = createResolveBuffer2(t);

    // Color attachment: clear {0,0,0,0}, store.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    // Depth-stencil attachment: depthClearValue=0.5, stencilClearValue=0.
    // depth24plus-stencil8 has both depth and stencil aspects — set both load/store ops.
    WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    dsAttachment.view = dsView;
    dsAttachment.depthLoadOp = WGPULoadOp_Clear;
    dsAttachment.depthStoreOp = WGPUStoreOp_Store;
    dsAttachment.depthClearValue = 0.5f;
    dsAttachment.stencilLoadOp = WGPULoadOp_Clear;
    dsAttachment.stencilStoreOp = WGPUStoreOp_Store;
    dsAttachment.stencilClearValue = 0;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &dsAttachment;
    passDesc.occlusionQuerySet = querySet;

    // Two pipelines: one for z=0.9 (fails Less < 0.5), one for z=0.1 (passes Less < 0.5).
    WGPURenderPipeline pipelineFail = createDepthPipeline(t, 0.9);
    WGPURenderPipeline pipelinePass = createDepthPipeline(t, 0.1);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    // Query 0: z=0.9, 0.9 < 0.5 is false → depth fails → 0 samples counted.
    wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
    wgpuRenderPassEncoderSetPipeline(pass, pipelineFail);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);

    // Query 1: z=0.1, 0.1 < 0.5 is true → depth passes → all samples counted.
    wgpuRenderPassEncoderBeginOcclusionQuery(pass, 1);
    wgpuRenderPassEncoderSetPipeline(pass, pipelinePass);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);

    wgpuRenderPassEncoderEnd(pass);

    // Resolve both queries into the 16-byte buffer.
    wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 2, resolveBuffer, 0);

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    wgpuQuerySetRelease(querySet);

    verifyTwoQueryResults(t, resolveBuffer);
}

// Tests that a draw failing the stencil test (ref=1, buffer cleared to 0, compare=Equal)
// produces query result 0, while a draw passing (ref=0) produces a non-zero result.
void runStencilOcclusionQueryTest(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTexture colorTarget = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(colorTarget, viewDesc);

    WGPUTexture dsTarget = createDepthStencilTarget(t);
    WGPUTextureView dsView = t.createViewTracked(dsTarget, viewDesc);

    // Create the query set (occlusion, 2 queries).
    WGPUQuerySetDescriptor qsDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    qsDesc.type = WGPUQueryType_Occlusion;
    qsDesc.count = 2;
    WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &qsDesc);

    WGPUBuffer resolveBuffer = createResolveBuffer2(t);

    // Color attachment: clear {0,0,0,0}, store.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = colorView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    // Depth-stencil attachment: stencilClearValue=0, depthClearValue=1.0.
    // depth24plus-stencil8 has both depth and stencil aspects — set both load/store ops.
    WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    dsAttachment.view = dsView;
    dsAttachment.depthLoadOp = WGPULoadOp_Clear;
    dsAttachment.depthStoreOp = WGPUStoreOp_Store;
    dsAttachment.depthClearValue = 1.0f;
    dsAttachment.stencilLoadOp = WGPULoadOp_Clear;
    dsAttachment.stencilStoreOp = WGPUStoreOp_Store;
    dsAttachment.stencilClearValue = 0;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &dsAttachment;
    passDesc.occlusionQuerySet = querySet;

    WGPURenderPipeline pipeline = createStencilPipeline(t);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, pipeline);

    // Query 0: stencilReference=1, buffer=0, compare Equal: 1==0 false → stencil fails → 0.
    wgpuRenderPassEncoderSetStencilReference(pass, 1);
    wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);

    // Query 1: stencilReference=0, buffer=0, compare Equal: 0==0 true → stencil passes → non-zero.
    wgpuRenderPassEncoderSetStencilReference(pass, 0);
    wgpuRenderPassEncoderBeginOcclusionQuery(pass, 1);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEndOcclusionQuery(pass);

    wgpuRenderPassEncoderEnd(pass);

    // Resolve both queries into the 16-byte buffer.
    wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 2, resolveBuffer, 0);

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    wgpuQuerySetRelease(querySet);

    verifyTwoQueryResults(t, resolveBuffer);
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

CTS_TEST(g, "occlusion_query,depth")
    .desc("A draw at z=0.9 fails the depth test (Less vs cleared 0.5) → query 0 == 0; z=0.1 passes → query 1 != 0.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runDepthOcclusionQueryTest(t);
    });

CTS_TEST(g, "occlusion_query,stencil")
    .desc("A draw with stencilRef=1 fails Equal vs cleared 0 → query 0 == 0; ref=0 passes → query 1 != 0.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runStencilOcclusionQueryTest(t);
    });

} // namespace
