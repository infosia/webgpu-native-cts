// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/render_pass/resolve.spec.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Native spelling: the array-valued subcase param `slotsToResolve` is a comma-separated string
// ("0,2"), as before.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
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
    "api,operation,render_pass,resolve",
    "API Operation Tests for multisample resolve in render passes.");

constexpr uint32_t kSize = 4;
constexpr WGPUTextureFormat kDepthStencilFormat = WGPUTextureFormat_Depth24PlusStencil8;

// kColorTextureFormats filtered by isTextureFormatPossiblyResolvable, in upstream order.
constexpr std::array<WGPUTextureFormat, 14> kPossiblyResolvableColorFormats = {
    WGPUTextureFormat_R8Unorm,
    WGPUTextureFormat_R8Snorm,
    WGPUTextureFormat_RG8Unorm,
    WGPUTextureFormat_RG8Snorm,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RGBA8UnormSrgb,
    WGPUTextureFormat_RGBA8Snorm,
    WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb,
    WGPUTextureFormat_R16Float,
    WGPUTextureFormat_RG16Float,
    WGPUTextureFormat_RGBA16Float,
    WGPUTextureFormat_RGB10A2Unorm,
    WGPUTextureFormat_RG11B10Ufloat,
};

constexpr std::string_view kVertexShader = R"(
            @vertex fn main(
              @builtin(vertex_index) VertexIndex : u32
              ) -> @builtin(position) vec4<f32> {
              var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
                  vec2<f32>(-1.0, -1.0),
                  vec2<f32>(-1.0,  1.0),
                  vec2<f32>( 1.0,  1.0));
              return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
            })";

constexpr std::string_view kFragmentShader = R"(
            struct Output {
              @location(0) fragColor0 : vec4<f32>,
              @location(1) fragColor1 : vec4<f32>,
              @location(2) fragColor2 : vec4<f32>,
              @location(3) fragColor3 : vec4<f32>,
            };

            @fragment fn main() -> Output {
              return Output(
                vec4<f32>(1.0, 1.0, 1.0, 1.0),
                vec4<f32>(1.0, 1.0, 1.0, 1.0),
                vec4<f32>(1.0, 1.0, 1.0, 1.0),
                vec4<f32>(1.0, 1.0, 1.0, 1.0)
              );
            })";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

std::vector<Value> colorFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kPossiblyResolvableColorFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

std::vector<uint32_t> parseSlots(std::string_view text) {
    std::vector<uint32_t> slots;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            slots.push_back(static_cast<uint32_t>(c - '0'));
        }
    }
    return slots;
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

bool paramBool(const ParamRecord& p, std::string_view key) {
    return valueAs<bool>(*findParam(p, key));
}

// format_info.ts isTextureFormatResolvable.
bool isTextureFormatResolvable(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format) {
    if (format == WGPUTextureFormat_RG11B10Ufloat) {
        return wgpuDeviceHasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable);
    }
    if (textureFormatInList(format, kTextureFormatTier1AllowsResolve)) {
        return wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TextureFormatsTier1);
    }
    // You can't resolve a non-multisampled format. Every other possibly-resolvable format has
    // colorRender.resolve.
    return t.isTextureFormatMultisampled(format);
}

CTS_TEST(g, "render_pass_resolve")
    .desc(R"(
Test basic render pass resolve behavior for combinations of:
  - number of color attachments, some with and some without a resolveTarget
  - {a single draw+resolve pass, one draw-store pass and one empty load-resolve pass}
    (attempts to test known driver bugs with empty resolve passes)
  - in the resolve pass, storeOp set to {'store', 'discard'}
  - mip levels {0, 1} and array layers {0, 1}
    TODO: cases where color attachment and resolve target don't have the same mip level
  - resolveTarget {2d array layer, TODO: 3d slice} {0, >0} with {2d, TODO: 3d} resolveTarget
    TODO: cases where color attachment and resolve target don't have the same z (slice or layer)
  - TODO: test that any not-resolved attachments are rendered to correctly.
  - TODO: test different loadOps
  - TODO?: resolveTarget mip level {0, >0} (TODO?: different mip level from colorAttachment)
  - TODO?: resolveTarget {2d array layer, TODO: 3d slice} {0, >0} with {2d, TODO: 3d} resolveTarget
    (different z from colorAttachment)
)")
    .params([](ParamsBuilder u) {
        return u.combine("colorFormat", colorFormatValues())
            .combine("separateResolvePass", {false, true})
            .combine("storeOperation", {"discard", "store"})
            .beginSubcases()
            .combine("transientColorAttachment", {false, true})
            // Exclude if transient AND (separate pass OR storing)
            .filter([](const ParamRecord& p) {
                return !(paramBool(p, "transientColorAttachment")
                         && (paramBool(p, "separateResolvePass")
                             || valueAs<std::string>(*findParam(p, "storeOperation")) == "store"));
            })
            .combine("numColorAttachments", {2, 4})
            .combine("slotsToResolve", {"0,2", "1,3", "0,1,2,3"})
            .combine("resolveTargetBaseMipLevel", {0, 1})
            .combine("resolveTargetBaseArrayLayer", {0, 1})
            .combine("transientDepthStencilAttachment", {false, true})
            .combine("depthStencilAttachment", {false, true})
            .filter([](const ParamRecord& p) {
                return !(!paramBool(p, "depthStencilAttachment") && paramBool(p, "transientDepthStencilAttachment"));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat colorFormat = parseTextureFormat(t.param<std::string>("colorFormat"));
        const bool separateResolvePass = t.param<bool>("separateResolvePass");
        const WGPUStoreOp storeOperation = parseStoreOp(t.param<std::string>("storeOperation"));
        const bool transientColorAttachment = t.param<bool>("transientColorAttachment");
        const uint32_t numColorAttachments = static_cast<uint32_t>(t.param<int64_t>("numColorAttachments"));
        const std::vector<uint32_t> slotsToResolve = parseSlots(t.param<std::string>("slotsToResolve"));
        const uint32_t resolveTargetBaseMipLevel = static_cast<uint32_t>(t.param<int64_t>("resolveTargetBaseMipLevel"));
        const uint32_t resolveTargetBaseArrayLayer =
            static_cast<uint32_t>(t.param<int64_t>("resolveTargetBaseArrayLayer"));
        const bool transientDepthStencilAttachment = t.param<bool>("transientDepthStencilAttachment");
        const bool hasDepthStencilAttachment = t.param<bool>("depthStencilAttachment");

        t.skipIfTextureFormatNotSupported(colorFormat);
        if (!isTextureFormatResolvable(t, colorFormat)) {
            t.skip("texture format is not resolvable");
        }
        if (transientColorAttachment || transientDepthStencilAttachment) {
            t.skipIfTransientAttachmentNotSupported();
        }

        std::vector<WGPUColorTargetState> targets;
        for (uint32_t i = 0; i < numColorAttachments; ++i) {
            WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
            target.format = colorFormat;
            targets.push_back(target);
        }

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        applyJsStencilFaceDefaults(depthStencil);
        depthStencil.format = kDepthStencilFormat;
        depthStencil.depthWriteEnabled = WGPUOptionalBool_False;

        // These shaders will draw a white triangle into a texture. After draw, the top left half of
        // the texture will be white, and the bottom right half will be unchanged. When this texture
        // is resolved, there will be two distinct colors in each portion of the texture, as well as
        // a line between the portions that contain the midpoint color due to the multisample resolve.
        WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShader);
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fragmentModule;
        fragment.entryPoint = stringView("main");
        fragment.targetCount = targets.size();
        fragment.targets = targets.data();

        WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = nullptr; // 'auto'
        pipelineDesc.vertex.module = vertexModule;
        pipelineDesc.vertex.entryPoint = stringView("main");
        pipelineDesc.fragment = &fragment;
        pipelineDesc.depthStencil = hasDepthStencilAttachment ? &depthStencil : nullptr;
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count = 4;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipelineDesc);

        std::vector<WGPUTexture> resolveTargets;
        std::vector<WGPURenderPassColorAttachment> drawPassAttachments;
        std::vector<WGPURenderPassColorAttachment> resolvePassAttachments;

        // The resolve target must be the same size as the color attachment. If we're resolving to
        // mip level 1, the resolve target base mip level should be 2x the color attachment size.
        const uint32_t kResolveTargetSize = kSize << resolveTargetBaseMipLevel;

        for (uint32_t i = 0; i < numColorAttachments; ++i) {
            WGPUTextureDescriptor colorDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            colorDesc.format = colorFormat;
            colorDesc.size = WGPUExtent3D{kSize, kSize, 1};
            colorDesc.sampleCount = 4;
            colorDesc.mipLevelCount = 1;
            colorDesc.usage = transientColorAttachment
                ? (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment)
                : WGPUTextureUsage_RenderAttachment;
            WGPUTextureViewDescriptor defaultViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPUTextureView colorAttachment = t.createViewTracked(t.createTextureTracked(colorDesc), defaultViewDesc);

            WGPUTextureView resolveTarget = nullptr;
            if (std::find(slotsToResolve.begin(), slotsToResolve.end(), i) != slotsToResolve.end()) {
                WGPUTextureDescriptor resolveDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                resolveDesc.format = colorFormat;
                resolveDesc.size =
                    WGPUExtent3D{kResolveTargetSize, kResolveTargetSize, resolveTargetBaseArrayLayer + 1};
                resolveDesc.sampleCount = 1;
                resolveDesc.mipLevelCount = resolveTargetBaseMipLevel + 1;
                resolveDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
                WGPUTexture resolveTargetTexture = t.createTextureTracked(resolveDesc);
                resolveTargets.push_back(resolveTargetTexture);

                WGPUTextureViewDescriptor resolveViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                resolveViewDesc.baseMipLevel = resolveTargetBaseMipLevel;
                resolveViewDesc.baseArrayLayer = resolveTargetBaseArrayLayer;
                resolveTarget = t.createViewTracked(resolveTargetTexture, resolveViewDesc);
            }

            // Clear to black for the load operation. After the draw, the top left half of the
            // attachment will be white and the bottom right half will be black.
            WGPURenderPassColorAttachment draw = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            draw.view = colorAttachment;
            draw.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
            draw.loadOp = WGPULoadOp_Clear;
            if (separateResolvePass) {
                draw.storeOp = WGPUStoreOp_Store;
                drawPassAttachments.push_back(draw);

                WGPURenderPassColorAttachment resolve = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                resolve.view = colorAttachment;
                resolve.resolveTarget = resolveTarget;
                resolve.loadOp = WGPULoadOp_Load;
                resolve.storeOp = storeOperation;
                resolvePassAttachments.push_back(resolve);
            } else {
                draw.resolveTarget = resolveTarget;
                draw.storeOp = storeOperation;
                drawPassAttachments.push_back(draw);
            }
        }

        WGPURenderPassDepthStencilAttachment depthStencilAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        if (hasDepthStencilAttachment) {
            WGPUTextureDescriptor dsDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            dsDesc.format = kDepthStencilFormat;
            dsDesc.size = WGPUExtent3D{kSize, kSize, 1};
            dsDesc.sampleCount = 4;
            dsDesc.usage = transientDepthStencilAttachment
                ? (WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TransientAttachment)
                : WGPUTextureUsage_RenderAttachment;
            WGPUTextureViewDescriptor dsViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            depthStencilAttachment.view = t.createViewTracked(t.createTextureTracked(dsDesc), dsViewDesc);
            depthStencilAttachment.depthClearValue = 1.0f;
            depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.depthStoreOp = WGPUStoreOp_Discard;
            depthStencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Discard;
        }
        const WGPURenderPassDepthStencilAttachment* depthStencilAttachmentPtr =
            hasDepthStencilAttachment ? &depthStencilAttachment : nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        {
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = drawPassAttachments.size();
            passDesc.colorAttachments = drawPassAttachments.data();
            passDesc.depthStencilAttachment = depthStencilAttachmentPtr;
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        }
        if (separateResolvePass) {
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = resolvePassAttachments.size();
            passDesc.colorAttachments = resolvePassAttachments.data();
            passDesc.depthStencilAttachment = depthStencilAttachmentPtr;
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
        }
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // Verify the resolve targets contain the correct values. Note that we use z to specify the
        // array layer from which to pull the pixels for testing.
        const uint32_t z = resolveTargetBaseArrayLayer;
        for (WGPUTexture resolveTarget : resolveTargets) {
            expectSinglePixelComparisonsAreOkInTexture(
                t, resolveTarget, colorFormat, resolveTargetBaseMipLevel,
                {
                    // Top left pixel should be {1.0, 1.0, 1.0, 1.0}.
                    PixelComparison{WGPUOrigin3D{0, 0, z}, {1.0, 1.0, 1.0, 1.0}},
                    // Bottom right pixel should be {0, 0, 0, 0}.
                    PixelComparison{WGPUOrigin3D{kSize - 1, kSize - 1, z}, {0, 0, 0, 0}},
                    // Top right pixel should be {0.5, 0.5, 0.5, 0.5} due to the multisampled resolve.
                    PixelComparison{WGPUOrigin3D{kSize - 1, 0, z}, {0.5, 0.5, 0.5, 0.5}},
                });
        }
    });

} // namespace
