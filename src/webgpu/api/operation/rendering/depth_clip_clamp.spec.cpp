// Ported from gpuweb/cts src/webgpu/api/operation/rendering/depth_clip_clamp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// This file ports the depth_test_input_clamped subset for depth32float (non-multisampled).

#include <cstdint>
#include <optional>
#include <sstream>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,depth_clip_clamp",
    "Depth clip/clamp rendering operation tests.");

// kNumDepthValues points drawn as a 1-row texture (one point per texel).
constexpr uint32_t kNumDepthValues = 8;
// Viewport depth range used in the test pass.
constexpr float kVpMin = 0.25f;
constexpr float kVpMax = 0.75f;

// Shared WGSL: vertex shader + init fragment (clamped frag_depth) + test fragment (raw frag_depth).
// The vertex shader emits one point per vertex index along a 1×8 row.
// finit writes clamp(kDepths[idx], vpMin, vpMax) — used to pre-fill the depth buffer.
// ftest writes the raw kDepths[idx] value — the viewport must clamp it to [0.25,0.75].
constexpr std::string_view kShader = R"(
var<private> kDepths: array<f32, 8> = array<f32, 8>(-1.0, -0.5, 0.0, 0.25, 0.75, 1.0, 1.5, 2.0);
const vpMin: f32 = 0.25;
const vpMax: f32 = 0.75;

fn vertexX(idx: u32) -> f32 { return (f32(idx) + 0.5) * 2.0 / 8.0 - 1.0; }

struct VF {
  @builtin(position) pos: vec4<f32>,
  @location(0) @interpolate(flat, either) vertexIndex: u32
};

@vertex fn vmain(@builtin(vertex_index) idx: u32) -> VF {
  var vf: VF;
  vf.pos = vec4<f32>(vertexX(idx), 0.0, 0.5, 1.0);
  vf.vertexIndex = idx;
  return vf;
}

@fragment fn finit(vf: VF) -> @builtin(frag_depth) f32 {
  return clamp(kDepths[vf.vertexIndex], vpMin, vpMax);
}

struct FTest {
  @builtin(frag_depth) depth: f32,
  @location(0) color: f32
};

@fragment fn ftest(vf: VF) -> FTest {
  var f: FTest;
  f.depth = kDepths[vf.vertexIndex];
  f.color = 1.0;
  return f;
}
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

// depth_test_input_clamped:
//   Pass 1 (init): write expected (clamped) depth values into a depth32float texture via finit,
//                  depthCompare:Always, depthWriteEnabled:True, default viewport.
//   Pass 2 (test): draw with unclamped frag_depth = kDepths[idx] via ftest,
//                  depthCompare:NotEqual, depthWriteEnabled:False,
//                  viewport depth = [0.25, 0.75], writing color=1 to r8unorm target.
//   Verify: r8unorm must be all zero (not-equal fails because viewport clamps the test depth
//           to match the stored init depth, so no fragment passes the depth test).
CTS_TEST(g, "depth_test_input_clamped")
    .params([](ParamsBuilder u) {
        return u.combine("unclippedDepth", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool unclippedDepth = t.param<bool>("unclippedDepth");

        // Feature gate: depth-clip-control is required when unclippedDepth=true.
        if (unclippedDepth && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DepthClipControl)) {
            t.skip("depth-clip-control feature is not supported");
        }

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(kShader);

        // --- depth-stencil texture (depth32float, 8×1) ---
        WGPUTextureDescriptor dsDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dsDesc.size = WGPUExtent3D{kNumDepthValues, 1, 1};
        dsDesc.mipLevelCount = 1;
        dsDesc.sampleCount = 1;
        dsDesc.dimension = WGPUTextureDimension_2D;
        dsDesc.format = WGPUTextureFormat_Depth32Float;
        dsDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture dsTexture = t.createTextureTracked(dsDesc);
        WGPUTextureViewDescriptor dsViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView dsView = t.createViewTracked(dsTexture, dsViewDesc);

        // --- r8unorm color texture (8×1) ---
        WGPUTextureDescriptor testDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        testDesc.size = WGPUExtent3D{kNumDepthValues, 1, 1};
        testDesc.mipLevelCount = 1;
        testDesc.sampleCount = 1;
        testDesc.dimension = WGPUTextureDimension_2D;
        testDesc.format = WGPUTextureFormat_R8Unorm;
        testDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture testTexture = t.createTextureTracked(testDesc);
        WGPUTextureViewDescriptor testViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView testView = t.createViewTracked(testTexture, testViewDesc);

        // --- result readback buffer (≥256 bytes; bytesPerRow = align(8,256) = 256) ---
        constexpr uint32_t kBytesPerRow = 256; // align(kNumDepthValues * 1, 256)
        constexpr uint64_t kResultBufSize = kBytesPerRow; // 1 row
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = kResultBufSize;
        bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer resultBuffer = t.createBufferTracked(bufDesc);

        // --- init pipeline: layout:auto, vmain/finit, point-list, no color targets,
        //     depthCompare:Always, depthWriteEnabled:True ---
        {
            WGPUDepthStencilState initDS = WGPU_DEPTH_STENCIL_STATE_INIT;
            initDS.format = WGPUTextureFormat_Depth32Float;
            initDS.depthWriteEnabled = WGPUOptionalBool_True;
            initDS.depthCompare = WGPUCompareFunction_Always;

            WGPURenderPipelineDescriptor initPipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            initPipeDesc.vertex.module = shaderModule;
            initPipeDesc.vertex.entryPoint = stringView("vmain");
            initPipeDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
            initPipeDesc.multisample.count = 1;
            // Depth-only pass: no fragment color targets.
            WGPUFragmentState initFragment = WGPU_FRAGMENT_STATE_INIT;
            initFragment.module = shaderModule;
            initFragment.entryPoint = stringView("finit");
            initFragment.targetCount = 0;
            initFragment.targets = nullptr;
            initPipeDesc.fragment = &initFragment;
            initPipeDesc.depthStencil = &initDS;
            WGPURenderPipeline initPipeline = t.createRenderPipelineTracked(initPipeDesc);

            // Pass 1 (init): depth-only render pass.
            WGPURenderPassDepthStencilAttachment initDsAttach = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            initDsAttach.view = dsView;
            initDsAttach.depthLoadOp = WGPULoadOp_Clear;
            initDsAttach.depthStoreOp = WGPUStoreOp_Store;
            initDsAttach.depthClearValue = 1.0f;

            WGPURenderPassDescriptor initPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            initPassDesc.colorAttachmentCount = 0;
            initPassDesc.colorAttachments = nullptr;
            initPassDesc.depthStencilAttachment = &initDsAttach;

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &initPassDesc);
            wgpuRenderPassEncoderSetPipeline(pass, initPipeline);
            wgpuRenderPassEncoderDraw(pass, kNumDepthValues, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
            submit(t, encoder);
        }

        // --- test pipeline: layout:auto, vmain/ftest, point-list, unclippedDepth,
        //     depthCompare:NotEqual, depthWriteEnabled:False, r8unorm color target ---
        {
            WGPUDepthStencilState testDS = WGPU_DEPTH_STENCIL_STATE_INIT;
            testDS.format = WGPUTextureFormat_Depth32Float;
            testDS.depthWriteEnabled = WGPUOptionalBool_False;
            testDS.depthCompare = WGPUCompareFunction_NotEqual;

            WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
            colorTarget.format = WGPUTextureFormat_R8Unorm;

            WGPUFragmentState testFragment = WGPU_FRAGMENT_STATE_INIT;
            testFragment.module = shaderModule;
            testFragment.entryPoint = stringView("ftest");
            testFragment.targetCount = 1;
            testFragment.targets = &colorTarget;

            WGPURenderPipelineDescriptor testPipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            testPipeDesc.vertex.module = shaderModule;
            testPipeDesc.vertex.entryPoint = stringView("vmain");
            testPipeDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
            // Set unclippedDepth directly on the primitive state (all backends: webgpu-headers 1.0+).
            testPipeDesc.primitive.unclippedDepth = unclippedDepth ? WGPU_TRUE : WGPU_FALSE;
            testPipeDesc.multisample.count = 1;
            testPipeDesc.fragment = &testFragment;
            testPipeDesc.depthStencil = &testDS;
            WGPURenderPipeline testPipeline = t.createRenderPipelineTracked(testPipeDesc);

            // Pass 2 (test): color + depth-stencil pass with viewport depth [0.25, 0.75].
            WGPURenderPassColorAttachment testColorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            testColorAttach.view = testView;
            testColorAttach.loadOp = WGPULoadOp_Clear;
            testColorAttach.storeOp = WGPUStoreOp_Store;
            testColorAttach.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

            WGPURenderPassDepthStencilAttachment testDsAttach = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            testDsAttach.view = dsView;
            testDsAttach.depthLoadOp = WGPULoadOp_Load;
            testDsAttach.depthStoreOp = WGPUStoreOp_Store;

            WGPURenderPassDescriptor testPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            testPassDesc.colorAttachmentCount = 1;
            testPassDesc.colorAttachments = &testColorAttach;
            testPassDesc.depthStencilAttachment = &testDsAttach;

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &testPassDesc);
            wgpuRenderPassEncoderSetPipeline(pass, testPipeline);
            // Viewport depth range [0.25, 0.75]: frag_depth gets clamped to [vpMin, vpMax].
            wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f,
                static_cast<float>(kNumDepthValues), 1.0f, kVpMin, kVpMax);
            wgpuRenderPassEncoderDraw(pass, kNumDepthValues, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);

            // Copy r8unorm testTexture → resultBuffer.
            WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            source.texture = testTexture;
            source.mipLevel = 0;
            source.origin = WGPUOrigin3D{0, 0, 0};
            source.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            destination.buffer = resultBuffer;
            destination.layout.offset = 0;
            destination.layout.bytesPerRow = kBytesPerRow;
            destination.layout.rowsPerImage = 1;

            WGPUExtent3D copyExtent = WGPUExtent3D{kNumDepthValues, 1, 1};
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copyExtent);
            submit(t, encoder);
        }

        // --- Verify: all 8 r8unorm bytes must be 0 (no fragment drew color). ---
        t.expectGPUBufferValuesPassCheck(
            resultBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                for (uint32_t i = 0; i < kNumDepthValues; ++i) {
                    if (i >= len) {
                        std::ostringstream msg;
                        msg << "r8unorm readback buffer too small at index " << i;
                        return msg.str();
                    }
                    if (actual[i] != 0) {
                        std::ostringstream msg;
                        msg << "r8unorm pixel " << i << " expected 0 (depth test blocked draw)"
                            << ", got " << static_cast<int>(actual[i]);
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kResultBufSize));
    });

} // namespace
