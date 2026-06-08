// Ported from gpuweb/cts src/webgpu/api/operation/rendering/depth_clip_clamp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// This file ports the depth_test_input_clamped subset for depth32float (non-multisampled),
// and the depth_clamp_and_clip subset for depth32float, unclippedDepth=false, non-multisampled.

#include <bit>
#include <cstdint>
#include <cstring>
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

// ============================================================
// depth_clamp_and_clip
// ============================================================
//
// Ports the upstream depth_clamp_and_clip test (b507bd1) with:
//   format = depth32float, unclippedDepth = false, multisampled = false,
//   writeDepth ∈ {false, true} → 2 cases.
//
// Shader is copied verbatim from the upstream shaderSource with:
//   kNumDepthValues=8, kNumTestPoints=64, vpMin=0.25, vpMax=0.75,
//   ${!unclippedDepth} → literal true,
//   depth texture binding → texture_2d<f32> (non-multisampled).
//
// Two-pass structure:
//   Pass 1 (test):  draw 64 points with viewport [0.25, 0.75]; write
//                   position.z error to fragInputZFailedBuffer (storage).
//   Pass 2 (check): re-draw at z=0.5 with viewport [0.0, 1.0]; sample
//                   dsTexture depth-only; output 1.0 on mismatch → r8unorm.
// Verify both buffers: fragInputZFailedBuffer ≈ 0, checkBuffer == 0.

constexpr std::string_view kShaderClampAndClip = R"(
      // Test depths, with viewport range corresponding to [0,1].
      var<private> kDepths: array<f32, 8> = array<f32, 8>(
          -1.0, -0.5, 0.0, 0.25, 0.75, 1.0, 1.5, 2.0);

      const vpMin: f32 = 0.25;
      const vpMax: f32 = 0.75;

      // Draw the points in a straight horizontal row, one per pixel.
      fn vertexX(idx: u32) -> f32 {
        return (f32(idx) + 0.5) * 2.0 / 64.0 - 1.0;
      }

      // Test vertex shader's position.z output.
      // Here, the viewport range corresponds to position.z in [0,1].
      fn vertexZ(idx: u32) -> f32 {
        return kDepths[idx / 8u];
      }

      // Test fragment shader's expected position.z input.
      // Here, the viewport range corresponds to position.z in [vpMin,vpMax], but
      // unclipped values extend beyond that range.
      fn expectedFragPosZ(idx: u32) -> f32 {
        return vpMin + vertexZ(idx) * (vpMax - vpMin);
      }

      //////// "Test" entry points

      struct VFTest {
        @builtin(position) pos: vec4<f32>,
        @location(0) @interpolate(flat, either) vertexIndex: u32,
      };

      @vertex
      fn vtest(@builtin(vertex_index) idx: u32) -> VFTest {
        var vf: VFTest;
        vf.pos = vec4<f32>(vertexX(idx), 0.0, vertexZ(idx), 1.0);
        vf.vertexIndex = idx;
        return vf;
      }

      struct Output {
        // Each fragment (that didn't get clipped) writes into one element of this output.
        // (Anything that doesn't get written is already zero.)
        fragInputZDiff: array<f32, 64>
      };
      @group(0) @binding(0) var <storage, read_write> output: Output;

      fn checkZ(vf: VFTest) {
        output.fragInputZDiff[vf.vertexIndex] = vf.pos.z - expectedFragPosZ(vf.vertexIndex);
      }

      @fragment
      fn ftest_WriteDepth(vf: VFTest) -> @builtin(frag_depth) f32 {
        checkZ(vf);
        return kDepths[vf.vertexIndex % 8u];
      }

      @fragment
      fn ftest_NoWriteDepth(vf: VFTest) {
        checkZ(vf);
      }

      //////// "Check" entry points

      struct VFCheck {
        @builtin(position) pos: vec4<f32>,
        @location(0) @interpolate(flat, either) vertexIndex: u32,
      };

      @vertex
      fn vcheck(@builtin(vertex_index) idx: u32) -> VFCheck {
        var vf: VFCheck;
        // Depth=0.5 because we want to render every point, not get clipped.
        vf.pos = vec4<f32>(vertexX(idx), 0.0, 0.5, 1.0);
        vf.vertexIndex = idx;
        return vf;
      }

      struct FCheck {
        @location(0) color: f32,
      };

      @group(0) @binding(0) var depthTex: texture_2d<f32>;

      @fragment
      fn fcheck(vf: VFCheck) -> FCheck {
        let vertZ = vertexZ(vf.vertexIndex);
        let outOfRange = vertZ < 0.0 || vertZ > 1.0;
        let expFragPosZ = expectedFragPosZ(vf.vertexIndex);

        let writtenDepth = kDepths[vf.vertexIndex % 8u];

        let expectedDepthWriteInput = expFragPosZ;
        var expectedDepthBufferValue = clamp(expectedDepthWriteInput, vpMin, vpMax);
        if (true && outOfRange) {
          // Test fragment should have been clipped; expect the depth attachment to
          // have its clear value (0.5).
          expectedDepthBufferValue = 0.5;
        }

        let actualDepthBufferValue = textureLoad(depthTex, vec2u(vf.vertexIndex, 0), 0).r;
        let actualVsExpectedDiff = abs(expectedDepthBufferValue - actualDepthBufferValue);
        var f: FCheck;
        f.color = 1.0; // Color written if the resulting depth is unexpected.
        if (actualVsExpectedDiff < 1e-5) {
          f.color = 0.0;
        }
        return f;
      }
)";

// writeDepth=true variant: fcheck uses writtenDepth for expectedDepthWriteInput.
constexpr std::string_view kShaderClampAndClipWriteDepth = R"(
      // Test depths, with viewport range corresponding to [0,1].
      var<private> kDepths: array<f32, 8> = array<f32, 8>(
          -1.0, -0.5, 0.0, 0.25, 0.75, 1.0, 1.5, 2.0);

      const vpMin: f32 = 0.25;
      const vpMax: f32 = 0.75;

      // Draw the points in a straight horizontal row, one per pixel.
      fn vertexX(idx: u32) -> f32 {
        return (f32(idx) + 0.5) * 2.0 / 64.0 - 1.0;
      }

      // Test vertex shader's position.z output.
      // Here, the viewport range corresponds to position.z in [0,1].
      fn vertexZ(idx: u32) -> f32 {
        return kDepths[idx / 8u];
      }

      // Test fragment shader's expected position.z input.
      // Here, the viewport range corresponds to position.z in [vpMin,vpMax], but
      // unclipped values extend beyond that range.
      fn expectedFragPosZ(idx: u32) -> f32 {
        return vpMin + vertexZ(idx) * (vpMax - vpMin);
      }

      //////// "Test" entry points

      struct VFTest {
        @builtin(position) pos: vec4<f32>,
        @location(0) @interpolate(flat, either) vertexIndex: u32,
      };

      @vertex
      fn vtest(@builtin(vertex_index) idx: u32) -> VFTest {
        var vf: VFTest;
        vf.pos = vec4<f32>(vertexX(idx), 0.0, vertexZ(idx), 1.0);
        vf.vertexIndex = idx;
        return vf;
      }

      struct Output {
        // Each fragment (that didn't get clipped) writes into one element of this output.
        // (Anything that doesn't get written is already zero.)
        fragInputZDiff: array<f32, 64>
      };
      @group(0) @binding(0) var <storage, read_write> output: Output;

      fn checkZ(vf: VFTest) {
        output.fragInputZDiff[vf.vertexIndex] = vf.pos.z - expectedFragPosZ(vf.vertexIndex);
      }

      @fragment
      fn ftest_WriteDepth(vf: VFTest) -> @builtin(frag_depth) f32 {
        checkZ(vf);
        return kDepths[vf.vertexIndex % 8u];
      }

      @fragment
      fn ftest_NoWriteDepth(vf: VFTest) {
        checkZ(vf);
      }

      //////// "Check" entry points

      struct VFCheck {
        @builtin(position) pos: vec4<f32>,
        @location(0) @interpolate(flat, either) vertexIndex: u32,
      };

      @vertex
      fn vcheck(@builtin(vertex_index) idx: u32) -> VFCheck {
        var vf: VFCheck;
        // Depth=0.5 because we want to render every point, not get clipped.
        vf.pos = vec4<f32>(vertexX(idx), 0.0, 0.5, 1.0);
        vf.vertexIndex = idx;
        return vf;
      }

      struct FCheck {
        @location(0) color: f32,
      };

      @group(0) @binding(0) var depthTex: texture_2d<f32>;

      @fragment
      fn fcheck(vf: VFCheck) -> FCheck {
        let vertZ = vertexZ(vf.vertexIndex);
        let outOfRange = vertZ < 0.0 || vertZ > 1.0;
        let expFragPosZ = expectedFragPosZ(vf.vertexIndex);

        let writtenDepth = kDepths[vf.vertexIndex % 8u];

        let expectedDepthWriteInput = writtenDepth;
        var expectedDepthBufferValue = clamp(expectedDepthWriteInput, vpMin, vpMax);
        if (true && outOfRange) {
          // Test fragment should have been clipped; expect the depth attachment to
          // have its clear value (0.5).
          expectedDepthBufferValue = 0.5;
        }

        let actualDepthBufferValue = textureLoad(depthTex, vec2u(vf.vertexIndex, 0), 0).r;
        let actualVsExpectedDiff = abs(expectedDepthBufferValue - actualDepthBufferValue);
        var f: FCheck;
        f.color = 1.0; // Color written if the resulting depth is unexpected.
        if (actualVsExpectedDiff < 1e-5) {
          f.color = 0.0;
        }
        return f;
      }
)";

CTS_TEST(g, "depth_clamp_and_clip")
    .desc(R"(
Depth written to the depth attachment should always be in the range of the viewport depth,
even if it was written by the fragment shader (using frag_depth). If depth clipping is enabled,
primitives should be clipped to the viewport depth before rasterization.

Minimal port: format=depth32float, unclippedDepth=false, multisampled=false,
writeDepth in {false, true} = 2 cases.
)")
    .params([](ParamsBuilder u) {
        return u.combine("writeDepth", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool writeDepth = t.param<bool>("writeDepth");

        // kNumTestPoints = kNumDepthValues * kNumDepthValues = 8 * 8 = 64
        constexpr uint32_t kNumTestPoints = 64;
        // bytesPerRow must be >= 256 (WebGPU alignment)
        constexpr uint32_t kCheckBytesPerRow = 256;
        // fragInputZFailedBuffer: 4 bytes per f32 * 64 entries = 256 bytes
        constexpr uint64_t kFragInputZBufSize = 4 * kNumTestPoints; // 256

        // Select the correct shader (fcheck uses different expectedDepthWriteInput).
        std::string_view shaderSrc = writeDepth ? kShaderClampAndClipWriteDepth : kShaderClampAndClip;
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(shaderSrc);

        // --- dsTexture: depth32float, 64×1, RENDER_ATTACHMENT|TEXTURE_BINDING|COPY_SRC ---
        WGPUTextureDescriptor dsDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dsDesc.size = WGPUExtent3D{kNumTestPoints, 1, 1};
        dsDesc.mipLevelCount = 1;
        dsDesc.sampleCount = 1;
        dsDesc.dimension = WGPUTextureDimension_2D;
        dsDesc.format = WGPUTextureFormat_Depth32Float;
        dsDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc;
        WGPUTexture dsTexture = t.createTextureTracked(dsDesc);

        WGPUTextureViewDescriptor dsViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView dsView = t.createViewTracked(dsTexture, dsViewDesc);

        // Depth-only view for the check bind group.
        WGPUTextureViewDescriptor dsDepthOnlyViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        dsDepthOnlyViewDesc.aspect = WGPUTextureAspect_DepthOnly;
        WGPUTextureView dsDepthOnlyView = t.createViewTracked(dsTexture, dsDepthOnlyViewDesc);

        // --- checkTexture: r8unorm, 64×1, RENDER_ATTACHMENT|COPY_SRC ---
        WGPUTextureDescriptor checkTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        checkTexDesc.size = WGPUExtent3D{kNumTestPoints, 1, 1};
        checkTexDesc.mipLevelCount = 1;
        checkTexDesc.sampleCount = 1;
        checkTexDesc.dimension = WGPUTextureDimension_2D;
        checkTexDesc.format = WGPUTextureFormat_R8Unorm;
        checkTexDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture checkTexture = t.createTextureTracked(checkTexDesc);

        WGPUTextureViewDescriptor checkViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView checkView = t.createViewTracked(checkTexture, checkViewDesc);

        // --- fragInputZFailedBuffer: STORAGE|COPY_SRC, 256 bytes ---
        WGPUBufferDescriptor fragBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        fragBufDesc.size = kFragInputZBufSize;
        fragBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer fragInputZFailedBuffer = t.createBufferTracked(fragBufDesc);

        // --- checkBuffer: readback for checkTexture ---
        WGPUBufferDescriptor checkBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        checkBufDesc.size = kCheckBytesPerRow; // 256 bytes, covers 64 r8unorm texels
        checkBufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer checkBuffer = t.createBufferTracked(checkBufDesc);

        // --- testPipeline: layout auto, vtest, point-list, unclippedDepth=false,
        //     depthStencil{depth32float, depthWriteEnabled true, depthCompare always},
        //     fragment ftest_WriteDepth or ftest_NoWriteDepth, no color targets ---
        WGPUDepthStencilState testDS = WGPU_DEPTH_STENCIL_STATE_INIT;
        testDS.format = WGPUTextureFormat_Depth32Float;
        testDS.depthWriteEnabled = WGPUOptionalBool_True;
        testDS.depthCompare = WGPUCompareFunction_Always;

        WGPUFragmentState testFragment = WGPU_FRAGMENT_STATE_INIT;
        testFragment.module = shaderModule;
        testFragment.entryPoint = stringView(writeDepth ? "ftest_WriteDepth" : "ftest_NoWriteDepth");
        testFragment.targetCount = 0;
        testFragment.targets = nullptr;

        WGPURenderPipelineDescriptor testPipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        testPipeDesc.vertex.module = shaderModule;
        testPipeDesc.vertex.entryPoint = stringView("vtest");
        testPipeDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        testPipeDesc.primitive.unclippedDepth = WGPU_FALSE;
        testPipeDesc.multisample.count = 1;
        testPipeDesc.fragment = &testFragment;
        testPipeDesc.depthStencil = &testDS;
        WGPURenderPipeline testPipeline = t.createRenderPipelineTracked(testPipeDesc);

        // testBindGroup: bind fragInputZFailedBuffer at group(0) binding(0)
        // (layout from testPipeline.getBindGroupLayout(0)).
        WGPUBindGroupEntry testBGEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        testBGEntry.binding = 0;
        testBGEntry.buffer = fragInputZFailedBuffer;
        testBGEntry.offset = 0;
        testBGEntry.size = kFragInputZBufSize;

        WGPUBindGroupDescriptor testBGDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        testBGDesc.layout = wgpuRenderPipelineGetBindGroupLayout(testPipeline, 0);
        testBGDesc.entryCount = 1;
        testBGDesc.entries = &testBGEntry;
        WGPUBindGroup testBindGroup = t.createBindGroupTracked(testBGDesc);
        // Release the layout handle returned by GetBindGroupLayout.
        wgpuBindGroupLayoutRelease(testBGDesc.layout);

        // --- checkPipeline: explicit layout with binding0 = texture_2d<f32> depth,
        //     vcheck, point-list, fcheck → r8unorm, no depthStencil ---
        WGPUBindGroupLayoutEntry checkBGLEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        checkBGLEntry.binding = 0;
        checkBGLEntry.visibility = WGPUShaderStage_Fragment;
        checkBGLEntry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        checkBGLEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
        checkBGLEntry.texture.multisampled = WGPU_FALSE;

        WGPUBindGroupLayoutDescriptor checkBGLDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        checkBGLDesc.entryCount = 1;
        checkBGLDesc.entries = &checkBGLEntry;
        WGPUBindGroupLayout checkBindGroupLayout = t.createBindGroupLayoutTracked(checkBGLDesc);

        WGPUPipelineLayoutDescriptor checkPLDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        checkPLDesc.bindGroupLayoutCount = 1;
        checkPLDesc.bindGroupLayouts = &checkBindGroupLayout;
        WGPUPipelineLayout checkPipelineLayout = t.createPipelineLayoutTracked(checkPLDesc);

        WGPUColorTargetState checkColorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        checkColorTarget.format = WGPUTextureFormat_R8Unorm;

        WGPUFragmentState checkFragment = WGPU_FRAGMENT_STATE_INIT;
        checkFragment.module = shaderModule;
        checkFragment.entryPoint = stringView("fcheck");
        checkFragment.targetCount = 1;
        checkFragment.targets = &checkColorTarget;

        WGPURenderPipelineDescriptor checkPipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        checkPipeDesc.layout = checkPipelineLayout;
        checkPipeDesc.vertex.module = shaderModule;
        checkPipeDesc.vertex.entryPoint = stringView("vcheck");
        checkPipeDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        checkPipeDesc.multisample.count = 1;
        checkPipeDesc.fragment = &checkFragment;
        WGPURenderPipeline checkPipeline = t.createRenderPipelineTracked(checkPipeDesc);

        // checkBindGroup: bind dsTexture depth-only view at group(0) binding(0).
        WGPUBindGroupEntry checkBGEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        checkBGEntry.binding = 0;
        checkBGEntry.textureView = dsDepthOnlyView;

        WGPUBindGroupDescriptor checkBGDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        checkBGDesc.layout = checkBindGroupLayout;
        checkBGDesc.entryCount = 1;
        checkBGDesc.entries = &checkBGEntry;
        WGPUBindGroup checkBindGroup = t.createBindGroupTracked(checkBGDesc);

        // --- Encode and submit both passes + copy in a single command encoder ---
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        // Pass 1 (test): depth-only, viewport [0.25, 0.75].
        {
            WGPURenderPassDepthStencilAttachment testDsAttach = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            testDsAttach.view = dsView;
            testDsAttach.depthClearValue = 0.5f; // Clear value; visible when clipped.
            testDsAttach.depthLoadOp = WGPULoadOp_Clear;
            testDsAttach.depthStoreOp = WGPUStoreOp_Store;

            WGPURenderPassDescriptor testPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            testPassDesc.colorAttachmentCount = 0;
            testPassDesc.colorAttachments = nullptr;
            testPassDesc.depthStencilAttachment = &testDsAttach;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &testPassDesc);
            wgpuRenderPassEncoderSetPipeline(pass, testPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, testBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f,
                static_cast<float>(kNumTestPoints), 1.0f, 0.25f, 0.75f);
            wgpuRenderPassEncoderDraw(pass, kNumTestPoints, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        }

        // Pass 2 (check): color r8unorm, viewport [0.0, 1.0]; sample dsTexture depth-only.
        {
            WGPURenderPassColorAttachment checkColorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            checkColorAttach.view = checkView;
            checkColorAttach.loadOp = WGPULoadOp_Clear;
            checkColorAttach.storeOp = WGPUStoreOp_Store;
            checkColorAttach.clearValue = WGPUColor{0.5, 0.5, 0.5, 0.5};

            WGPURenderPassDescriptor checkPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            checkPassDesc.colorAttachmentCount = 1;
            checkPassDesc.colorAttachments = &checkColorAttach;
            checkPassDesc.depthStencilAttachment = nullptr;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &checkPassDesc);
            wgpuRenderPassEncoderSetPipeline(pass, checkPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, checkBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f,
                static_cast<float>(kNumTestPoints), 1.0f, 0.0f, 1.0f);
            wgpuRenderPassEncoderDraw(pass, kNumTestPoints, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        }

        // Copy checkTexture → checkBuffer.
        {
            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = checkTexture;
            src.mipLevel = 0;
            src.origin = WGPUOrigin3D{0, 0, 0};
            src.aspect = WGPUTextureAspect_All;

            WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            dst.buffer = checkBuffer;
            dst.layout.offset = 0;
            dst.layout.bytesPerRow = kCheckBytesPerRow;
            dst.layout.rowsPerImage = 1;

            WGPUExtent3D copyExtent = WGPUExtent3D{kNumTestPoints, 1, 1};
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copyExtent);
        }

        submit(t, encoder);

        // --- Verify: fragInputZFailedBuffer (64 f32 little-endian) all ≈ 0 ---
        t.expectGPUBufferValuesPassCheck(
            fragInputZFailedBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                for (uint32_t i = 0; i < kNumTestPoints; ++i) {
                    if (i * 4 + 4 > len) {
                        std::ostringstream msg;
                        msg << "fragInputZFailedBuffer too small at f32 index " << i;
                        return msg.str();
                    }
                    float v = 0.0f;
                    std::memcpy(&v, actual + i * 4, 4);
                    if (v < -1e-5f || v > 1e-5f) {
                        std::ostringstream msg;
                        msg << "fragInputZFailedBuffer[" << i << "] = " << v
                            << " is outside [-1e-5, 1e-5]: fragment position.z mismatch";
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            kFragInputZBufSize);

        // --- Verify: checkBuffer first 64 bytes all == 0 ---
        t.expectGPUBufferValuesPassCheck(
            checkBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                for (uint32_t i = 0; i < kNumTestPoints; ++i) {
                    if (i >= len) {
                        std::ostringstream msg;
                        msg << "checkBuffer too small at index " << i;
                        return msg.str();
                    }
                    if (actual[i] != 0) {
                        std::ostringstream msg;
                        msg << "checkBuffer[" << i << "] = " << static_cast<int>(actual[i])
                            << ": depth value in attachment did not match expected (check pass drew color)";
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kCheckBytesPerRow));
    });

} // namespace
