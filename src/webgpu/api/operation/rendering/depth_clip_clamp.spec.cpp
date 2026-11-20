// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/rendering/depth_clip_clamp.spec.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// The `dsActual`/`dsExpected` depth copies are encoded as upstream; upstream only prints them in
// the failure table, so they are not read back here.

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_test_utils.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,rendering,depth_clip_clamp",
    R"(
Tests for depth clipping, depth clamping (at various points in the pipeline), and maybe extended
depth ranges as well.
)");

// Number of depth values to test for both vertex output and frag_depth output.
constexpr uint32_t kNumDepthValues = 8;
// depth_clamp_and_clip tests every combination of vertex output and frag_depth output.
constexpr uint32_t kNumTestPoints = kNumDepthValues * kNumDepthValues;
constexpr float kViewportMinDepth = 0.25f;
constexpr float kViewportMaxDepth = 0.75f;

// kDepthTextureFormats, in upstream order.
std::vector<Value> depthTextureFormatValues() {
    return {Value("depth16unorm"), Value("depth32float"), Value("depth24plus"), Value("depth24plus-stencil8"),
            Value("depth32float-stencil8")};
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

WGPUTextureView createDefaultView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, desc);
}

WGPUTexture create1DRowTexture(AllFeaturesMaxLimitsGpuTest& t,
                               WGPUTextureFormat format,
                               uint32_t width,
                               WGPUTextureUsage usage,
                               uint32_t sampleCount) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.format = format;
    desc.size = WGPUExtent3D{width, 1, 1};
    desc.usage = usage;
    desc.sampleCount = sampleCount;
    return t.createTextureTracked(desc);
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

void encodeCopyToBuffer(WGPUCommandEncoder encoder,
                        WGPUTexture texture,
                        WGPUTextureAspect aspect,
                        WGPUBuffer buffer,
                        uint32_t width) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = texture;
    source.aspect = aspect;
    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = buffer;
    const WGPUExtent3D copySize{width, 1, 1};
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &copySize);
}

// expectGPUBufferValuesPassCheck(..., { method: 'map' }) on a MAP_READ buffer.
void expectMappedBufferPassCheck(AllFeaturesMaxLimitsGpuTest& t,
                                 WGPUBuffer buffer,
                                 size_t size,
                                 const std::function<std::optional<std::string>(const uint8_t*)>& check) {
    t.expectMapAsync(buffer, WGPUMapMode_Read, true, 0, size);
    const auto* data = static_cast<const uint8_t*>(wgpuBufferGetConstMappedRange(buffer, 0, size));
    if (data == nullptr) {
        wgpuBufferUnmap(buffer);
        t.fail("failed to get mapped range");
    }
    const std::optional<std::string> message = check(data);
    wgpuBufferUnmap(buffer);
    if (message) {
        t.fail(*message);
    }
}

std::optional<std::string> checkAllZero(const uint8_t* data, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (data[i] != 0) {
            std::ostringstream msg;
            msg << "element " << i << ": expected == 0, got " << static_cast<int>(data[i]);
            return msg.str();
        }
    }
    return std::nullopt;
}

void applyStencilClearDiscard(WGPUTextureFormat format, WGPURenderPassDepthStencilAttachment& attachment) {
    if (isStencilTextureFormat(format)) {
        attachment.stencilClearValue = 0;
        attachment.stencilLoadOp = WGPULoadOp_Clear;
        attachment.stencilStoreOp = WGPUStoreOp_Discard;
    }
}

CTS_TEST(g, "depth_clamp_and_clip")
    .desc(R"(
Depth written to the depth attachment should always be in the range of the viewport depth,
even if it was written by the fragment shader (using frag_depth). If depth clipping is enabled,
primitives should be clipped to the viewport depth before rasterization; if not, these fragments
should be rasterized, and the fragment shader should receive out-of-viewport position.z values.

To test this, render NxN points, with N vertex depth values, by (if writeDepth=true) N
frag_depth values with the viewport depth set to [0.25,0.75].

While rendering, check the fragment input position.z has the expected value (for all fragments that
were produced by the rasterizer) by writing the diff to a storage buffer, which is later checked to
be all (near) 0.

Then, run another pass (which outputs every point at z=0.5 to avoid clipping) to verify the depth
buffer contents by outputting the expected depth with depthCompare:'not-equal': any fragments that
have unexpected values then get drawn to the color buffer, which is later checked to be empty.)")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthTextureFormatValues())
            .combine("unclippedDepth", {Value::undef(), Value(false), Value(true)})
            .combine("writeDepth", {false, true})
            .combine("multisampled", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool unclippedDepth = !t.paramIsUndefined("unclippedDepth") && t.param<bool>("unclippedDepth");
        const bool writeDepth = t.param<bool>("writeDepth");
        const bool multisampled = t.param<bool>("multisampled");
        t.skipIfTextureFormatNotSupported(format);
        if (unclippedDepth && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DepthClipControl)) {
            t.skip("device does not have feature 'depth-clip-control'");
        }

        const uint32_t sampleCount = multisampled ? 4 : 1;

        std::ostringstream shader;
        shader << R"(
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

      )" << (multisampled ? "@group(0) @binding(0) var depthTex: texture_multisampled_2d<f32>;"
                          : "@group(0) @binding(0) var depthTex: texture_2d<f32>;")
               << R"(

      @fragment
      fn fcheck(vf: VFCheck) -> FCheck {
        let vertZ = vertexZ(vf.vertexIndex);
        let outOfRange = vertZ < 0.0 || vertZ > 1.0;
        let expFragPosZ = expectedFragPosZ(vf.vertexIndex);

        let writtenDepth = kDepths[vf.vertexIndex % 8u];

        let expectedDepthWriteInput = )"
               << (writeDepth ? "writtenDepth" : "expFragPosZ") << R"(;
        var expectedDepthBufferValue = clamp(expectedDepthWriteInput, vpMin, vpMax);
        if ()" << (!unclippedDepth ? "true" : "false")
               << R"( && outOfRange) {
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
        WGPUShaderModule module = t.createShaderModuleTracked(shader.str());

        // Draw points at different vertex depths and fragment depths into the depth attachment,
        // with a viewport of [0.25,0.75].
        WGPUDepthStencilState testDepthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        applyJsStencilFaceDefaults(testDepthStencil);
        testDepthStencil.format = format;
        testDepthStencil.depthWriteEnabled = WGPUOptionalBool_True;
        testDepthStencil.depthCompare = WGPUCompareFunction_Always;
        WGPUFragmentState testFragment = WGPU_FRAGMENT_STATE_INIT;
        testFragment.module = module;
        testFragment.entryPoint = stringView(writeDepth ? "ftest_WriteDepth" : "ftest_NoWriteDepth");
        testFragment.targetCount = 0;
        WGPURenderPipelineDescriptor testDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        testDesc.label = stringView("testPipeline");
        testDesc.layout = nullptr; // 'auto'
        testDesc.vertex.module = module;
        testDesc.vertex.entryPoint = stringView("vtest");
        testDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        testDesc.primitive.unclippedDepth = unclippedDepth ? 1u : 0u;
        testDesc.depthStencil = &testDepthStencil;
        testDesc.multisample.count = sampleCount;
        testDesc.fragment = &testFragment;
        WGPURenderPipeline testPipeline = t.createRenderPipelineTracked(testDesc);

        WGPUBindGroupLayoutEntry checkEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        checkEntry.binding = 0;
        checkEntry.visibility = WGPUShaderStage_Fragment;
        checkEntry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        checkEntry.texture.viewDimension = WGPUTextureViewDimension_2D;
        checkEntry.texture.multisampled = multisampled ? 1u : 0u;
        WGPUBindGroupLayoutDescriptor checkBglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        checkBglDesc.entryCount = 1;
        checkBglDesc.entries = &checkEntry;
        WGPUBindGroupLayout checkBindGroupLayout = t.createBindGroupLayoutTracked(checkBglDesc);
        WGPUPipelineLayoutDescriptor checkLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        checkLayoutDesc.bindGroupLayoutCount = 1;
        checkLayoutDesc.bindGroupLayouts = &checkBindGroupLayout;
        WGPUPipelineLayout checkPipelineLayout = t.createPipelineLayoutTracked(checkLayoutDesc);

        // Read the depth values and output 0 if they match expected, 1 if they don't
        WGPUColorTargetState checkTarget = WGPU_COLOR_TARGET_STATE_INIT;
        checkTarget.format = WGPUTextureFormat_R8Unorm;
        WGPUFragmentState checkFragment = WGPU_FRAGMENT_STATE_INIT;
        checkFragment.module = module;
        checkFragment.entryPoint = stringView("fcheck");
        checkFragment.targetCount = 1;
        checkFragment.targets = &checkTarget;
        WGPURenderPipelineDescriptor checkDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        checkDesc.label = stringView("checkPipeline");
        checkDesc.layout = checkPipelineLayout;
        checkDesc.vertex.module = module;
        checkDesc.vertex.entryPoint = stringView("vcheck");
        checkDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        checkDesc.multisample.count = sampleCount;
        checkDesc.fragment = &checkFragment;
        WGPURenderPipeline checkPipeline = t.createRenderPipelineTracked(checkDesc);

        WGPUTexture dsTexture = create1DRowTexture(
            t, format, kNumTestPoints,
            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc,
            sampleCount);
        WGPUTextureView dsTextureView = createDefaultView(t, dsTexture);

        const WGPUTextureUsage checkUsage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture checkTexture = create1DRowTexture(t, WGPUTextureFormat_R8Unorm, kNumTestPoints, checkUsage, 1);
        WGPUTextureView checkTextureView = createDefaultView(t, checkTexture);
        WGPUTextureView checkTextureMSView =
            multisampled
                ? createDefaultView(t, create1DRowTexture(t, WGPUTextureFormat_R8Unorm, kNumTestPoints, checkUsage, 4))
                : nullptr;

        // canCopyFromAspectOfTextureFormat(format, 'depth-only') && !multisampled && bytesPerBlock.
        const bool copyDepth =
            depthStencilBufferTextureCopySupported(ImageCopyType::CopyT2B, format, WGPUTextureAspect_DepthOnly)
            && !multisampled;
        const uint64_t depthCopySize =
            static_cast<uint64_t>(kNumTestPoints) * depthStencilFormatAspectSize(format, WGPUTextureAspect_DepthOnly);
        WGPUBuffer dsActual =
            copyDepth ? createBuffer(t, depthCopySize, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead) : nullptr;
        WGPUBuffer dsExpected =
            copyDepth ? createBuffer(t, depthCopySize, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead) : nullptr;
        WGPUBuffer checkBuffer = createBuffer(t, kNumTestPoints, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);

        WGPUBuffer fragInputZFailedBuffer =
            createBuffer(t, 4 * kNumTestPoints, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        WGPUBindGroupLayout testBindGroupLayout = wgpuRenderPipelineGetBindGroupLayout(testPipeline, 0);
        WGPUBindGroupEntry testEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        testEntry.binding = 0;
        testEntry.buffer = fragInputZFailedBuffer;
        testEntry.size = WGPU_WHOLE_SIZE;
        WGPUBindGroupDescriptor testBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        testBindGroupDesc.layout = testBindGroupLayout;
        testBindGroupDesc.entryCount = 1;
        testBindGroupDesc.entries = &testEntry;
        WGPUBindGroup testBindGroup = t.createBindGroupTracked(testBindGroupDesc);
        wgpuBindGroupLayoutRelease(testBindGroupLayout);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        {
            WGPURenderPassDepthStencilAttachment depthStencilAttachment =
                WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            depthStencilAttachment.view = dsTextureView;
            depthStencilAttachment.depthClearValue = 0.5f; // Will see this depth value if the fragment was clipped.
            depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
            applyStencilClearDiscard(format, depthStencilAttachment);
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 0;
            passDesc.depthStencilAttachment = &depthStencilAttachment;
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, testPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, testBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetViewport(pass, 0, 0, static_cast<float>(kNumTestPoints), 1, kViewportMinDepth,
                                             kViewportMaxDepth);
            wgpuRenderPassEncoderDraw(pass, kNumTestPoints, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        }
        if (dsActual != nullptr) {
            encodeCopyToBuffer(encoder, dsTexture, WGPUTextureAspect_DepthOnly, dsActual, kNumTestPoints);
        }
        {
            WGPUTextureViewDescriptor depthOnlyViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            depthOnlyViewDesc.aspect = WGPUTextureAspect_DepthOnly;
            WGPUBindGroupEntry checkBgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            checkBgEntry.binding = 0;
            checkBgEntry.textureView = t.createViewTracked(dsTexture, depthOnlyViewDesc);
            WGPUBindGroupDescriptor checkBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            checkBindGroupDesc.layout = checkBindGroupLayout;
            checkBindGroupDesc.entryCount = 1;
            checkBindGroupDesc.entries = &checkBgEntry;
            WGPUBindGroup checkBindGroup = t.createBindGroupTracked(checkBindGroupDesc);

            const WGPUColor clearValue{0.5, 0.5, 0.5, 0.5}; // We should only see 0.0 or 1.0
            WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachment.clearValue = clearValue;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            if (checkTextureMSView != nullptr) {
                colorAttachment.view = checkTextureMSView;
                colorAttachment.resolveTarget = checkTextureView;
                colorAttachment.storeOp = WGPUStoreOp_Discard;
            } else {
                colorAttachment.view = checkTextureView;
                colorAttachment.storeOp = WGPUStoreOp_Store;
            }
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, checkPipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, checkBindGroup, 0, nullptr);
            wgpuRenderPassEncoderSetViewport(pass, 0, 0, static_cast<float>(kNumTestPoints), 1, 0.0f, 1.0f);
            wgpuRenderPassEncoderDraw(pass, kNumTestPoints, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        }
        encodeCopyToBuffer(encoder, checkTexture, WGPUTextureAspect_All, checkBuffer, kNumTestPoints);
        if (dsExpected != nullptr) {
            encodeCopyToBuffer(encoder, dsTexture, WGPUTextureAspect_DepthOnly, dsExpected, kNumTestPoints);
        }
        submit(t, encoder);

        t.expectGPUBufferValuesPassCheck(
            fragInputZFailedBuffer,
            [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                for (size_t i = 0; i < kNumTestPoints && (i + 1) * 4 <= len; ++i) {
                    float value = 0.0f;
                    std::memcpy(&value, actual + i * 4, sizeof(value));
                    if (!(value >= -1e-5f && value <= 1e-5f)) {
                        std::ostringstream msg;
                        msg << "fragInputZDiff[" << i << "]: expected between -1e-5 and 1e-5, got " << value;
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0, 4 * kNumTestPoints);

        expectMappedBufferPassCheck(t, checkBuffer, kNumTestPoints,
                                    [](const uint8_t* data) { return checkAllZero(data, kNumTestPoints); });
    });

CTS_TEST(g, "depth_test_input_clamped")
    .desc(R"(
Input to the depth test should always be in the range of viewport depth, even if it was written by
the fragment shader (using frag_depth).

To test this, first initialize the depth buffer with N expected values (by writing frag_depth, with
the default viewport). These expected values are clamped by the shader to [0.25, 0.75].

Then, run another pass with the viewport depth set to [0.25,0.75], and output various (unclamped)
frag_depth values from its fragment shader with depthCompare:'not-equal'. These should get clamped;
any fragments that have unexpected values then get drawn to the color buffer, which is later checked
to be empty.)")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthTextureFormatValues())
            .combine("unclippedDepth", {false, true})
            .combine("multisampled", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool unclippedDepth = t.param<bool>("unclippedDepth");
        const bool multisampled = t.param<bool>("multisampled");
        t.skipIfTextureFormatNotSupported(format);
        if (unclippedDepth && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_DepthClipControl)) {
            t.skip("device does not have feature 'depth-clip-control'");
        }

        const uint32_t sampleCount = multisampled ? 4 : 1;

        constexpr std::string_view kShaderSource = R"(
      // Test depths, with viewport range corresponding to [0,1].
      var<private> kDepths: array<f32, 8> = array<f32, 8>(
          -1.0, -0.5, 0.0, 0.25, 0.75, 1.0, 1.5, 2.0);

      const vpMin: f32 = 0.25;
      const vpMax: f32 = 0.75;

      // Draw the points in a straight horizontal row, one per pixel.
      fn vertexX(idx: u32) -> f32 {
        return (f32(idx) + 0.5) * 2.0 / 8.0 - 1.0;
      }

      struct VF {
        @builtin(position) pos: vec4<f32>,
        @location(0) @interpolate(flat, either) vertexIndex: u32,
      };

      @vertex
      fn vmain(@builtin(vertex_index) idx: u32) -> VF {
        var vf: VF;
        // Depth=0.5 because we want to render every point, not get clipped.
        vf.pos = vec4<f32>(vertexX(idx), 0.0, 0.5, 1.0);
        vf.vertexIndex = idx;
        return vf;
      }

      @fragment
      fn finit(vf: VF) -> @builtin(frag_depth) f32 {
        // Expected values of the ftest pipeline.
        return clamp(kDepths[vf.vertexIndex], vpMin, vpMax);
      }

      struct FTest {
        @builtin(frag_depth) depth: f32,
        @location(0) color: f32,
      };

      @fragment
      fn ftest(vf: VF) -> FTest {
        var f: FTest;
        f.depth = kDepths[vf.vertexIndex]; // Should get clamped to the viewport.
        f.color = 1.0; // Color written if the resulting depth is unexpected.
        return f;
      }
    )";
        WGPUShaderModule module = t.createShaderModuleTracked(kShaderSource);

        // Initialize depth attachment with expected values, in [0.25,0.75].
        WGPUDepthStencilState initDepthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        applyJsStencilFaceDefaults(initDepthStencil);
        initDepthStencil.format = format;
        initDepthStencil.depthWriteEnabled = WGPUOptionalBool_True;
        initDepthStencil.depthCompare = WGPUCompareFunction_Always;
        WGPUFragmentState initFragment = WGPU_FRAGMENT_STATE_INIT;
        initFragment.module = module;
        initFragment.entryPoint = stringView("finit");
        initFragment.targetCount = 0;
        WGPURenderPipelineDescriptor initDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        initDesc.layout = nullptr; // 'auto'
        initDesc.vertex.module = module;
        initDesc.vertex.entryPoint = stringView("vmain");
        initDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        initDesc.depthStencil = &initDepthStencil;
        initDesc.multisample.count = sampleCount;
        initDesc.fragment = &initFragment;
        WGPURenderPipeline initPipeline = t.createRenderPipelineTracked(initDesc);

        // With a viewport set to [0.25,0.75], output values in [0.0,1.0] and check they're clamped
        // before the depth test, regardless of whether unclippedDepth is enabled.
        WGPUDepthStencilState testDepthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        applyJsStencilFaceDefaults(testDepthStencil);
        testDepthStencil.format = format;
        testDepthStencil.depthCompare = WGPUCompareFunction_NotEqual;
        testDepthStencil.depthWriteEnabled = WGPUOptionalBool_False;
        WGPUColorTargetState testTarget = WGPU_COLOR_TARGET_STATE_INIT;
        testTarget.format = WGPUTextureFormat_R8Unorm;
        WGPUFragmentState testFragment = WGPU_FRAGMENT_STATE_INIT;
        testFragment.module = module;
        testFragment.entryPoint = stringView("ftest");
        testFragment.targetCount = 1;
        testFragment.targets = &testTarget;
        WGPURenderPipelineDescriptor testDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        testDesc.layout = nullptr; // 'auto'
        testDesc.vertex.module = module;
        testDesc.vertex.entryPoint = stringView("vmain");
        testDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        testDesc.primitive.unclippedDepth = unclippedDepth ? 1u : 0u;
        testDesc.depthStencil = &testDepthStencil;
        testDesc.multisample.count = sampleCount;
        testDesc.fragment = &testFragment;
        WGPURenderPipeline testPipeline = t.createRenderPipelineTracked(testDesc);

        WGPUTexture dsTexture = create1DRowTexture(t, format, kNumDepthValues,
                                                   WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc,
                                                   sampleCount);
        WGPUTextureView dsTextureView = createDefaultView(t, dsTexture);

        const WGPUTextureUsage testUsage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture testTexture = create1DRowTexture(t, WGPUTextureFormat_R8Unorm, kNumDepthValues, testUsage, 1);
        WGPUTextureView testTextureView = createDefaultView(t, testTexture);
        WGPUTextureView testTextureMSView =
            multisampled
                ? createDefaultView(t, create1DRowTexture(t, WGPUTextureFormat_R8Unorm, kNumDepthValues, testUsage, 4))
                : nullptr;

        WGPUBuffer resultBuffer = createBuffer(t, kNumDepthValues, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        {
            WGPURenderPassDepthStencilAttachment depthStencilAttachment =
                WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            depthStencilAttachment.view = dsTextureView;
            depthStencilAttachment.depthClearValue = 1.0f;
            depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
            applyStencilClearDiscard(format, depthStencilAttachment);
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 0;
            passDesc.depthStencilAttachment = &depthStencilAttachment;
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, initPipeline);
            wgpuRenderPassEncoderDraw(pass, kNumDepthValues, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        }
        {
            const WGPUColor clearValue{0, 0, 0, 0}; // Will see this color if the test passed.
            WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachment.clearValue = clearValue;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            if (testTextureMSView != nullptr) {
                colorAttachment.view = testTextureMSView;
                colorAttachment.resolveTarget = testTextureView;
                colorAttachment.storeOp = WGPUStoreOp_Discard;
            } else {
                colorAttachment.view = testTextureView;
                colorAttachment.storeOp = WGPUStoreOp_Store;
            }
            WGPURenderPassDepthStencilAttachment depthStencilAttachment =
                WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            depthStencilAttachment.view = dsTextureView;
            depthStencilAttachment.depthLoadOp = WGPULoadOp_Load;
            depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
            applyStencilClearDiscard(format, depthStencilAttachment);
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            passDesc.depthStencilAttachment = &depthStencilAttachment;
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, testPipeline);
            wgpuRenderPassEncoderSetViewport(pass, 0, 0, static_cast<float>(kNumDepthValues), 1, kViewportMinDepth,
                                             kViewportMaxDepth);
            wgpuRenderPassEncoderDraw(pass, kNumDepthValues, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        }
        encodeCopyToBuffer(encoder, testTexture, WGPUTextureAspect_All, resultBuffer, kNumDepthValues);
        submit(t, encoder);

        expectMappedBufferPassCheck(t, resultBuffer, kNumDepthValues,
                                    [](const uint8_t* data) { return checkAllZero(data, kNumDepthValues); });
    });

} // namespace
