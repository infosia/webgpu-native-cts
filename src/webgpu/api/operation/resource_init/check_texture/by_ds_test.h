// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/resource_init/check_texture/by_ds_test.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
#pragma once

#include <cstdint>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

#include "webgpu/api/operation/resource_init/check_texture/by_copy.h"
#include "webgpu/api/operation/resource_init/check_texture/texture_zero_init_test.h"

namespace cts::texture_zero {

// Per-device cache of the depth/stencil-test pipelines (they depend only on the format, sample
// count and, for the depth test, the expected depth baked into the shader).
struct TextureZeroDsPipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, std::map<std::string, WGPURenderPipeline>> byDevice;

    ~TextureZeroDsPipelineCache() override {
        for (auto& [device, pipelines] : byDevice) {
            (void)device;
            for (auto& [key, pipeline] : pipelines) {
                (void)key;
                wgpuRenderPipelineRelease(pipeline);
            }
        }
    }
};

inline constexpr const char* kFullscreenVertexShader = R"(
    @vertex
    fn main(@builtin(vertex_index) VertexIndex : u32)
         -> @builtin(position) vec4<f32> {
      var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
        vec2<f32>(-1.0, -3.0),
        vec2<f32>( 3.0,  1.0),
        vec2<f32>(-1.0,  1.0));
      return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
    }
    )";

/// JS `${number}` for the two depth values used here (0 and 0.8).
inline std::string jsNumberString(double value) {
    std::ostringstream s;
    s << value;
    return s.str();
}

inline WGPURenderPipeline createDsTestPipeline(TextureZeroInitTest& t,
                                               const std::string& key,
                                               const std::string& fragmentCode,
                                               const WGPUDepthStencilState& depthStencil,
                                               uint32_t sampleCount) {
    auto& pipelines = deviceScoped<TextureZeroDsPipelineCache>().byDevice[t.device()];
    auto it = pipelines.find(key);
    if (it != pipelines.end()) {
        return it->second;
    }

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_R8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = t.createShaderModuleTracked(fragmentCode);
    fragment.entryPoint = tzSv("main");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.vertex.module = t.createShaderModuleTracked(kFullscreenVertexShader);
    desc.vertex.entryPoint = tzSv("main");
    desc.fragment = &fragment;
    desc.depthStencil = &depthStencil;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = sampleCount;

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(t.device(), &desc);
    if (pipeline == nullptr) {
        t.fail("failed to create the depth/stencil test pipeline");
    }
    pipelines.emplace(key, pipeline);
    return pipeline;
}

inline WGPURenderPipeline getDepthTestEqualPipeline(TextureZeroInitTest& t,
                                                    WGPUTextureFormat format,
                                                    uint32_t sampleCount,
                                                    double expected) {
    const std::string expectedText = jsNumberString(expected);
    const std::string fragmentCode = R"(
        struct Outputs {
          @builtin(frag_depth) FragDepth : f32,
          @location(0) outSuccess : f32,
        };

        @fragment
        fn main() -> Outputs {
          var output : Outputs;
          output.FragDepth = f32()" + expectedText + R"();
          output.outSuccess = 1.0;
          return output;
        }
        )";

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = format;
    depthStencil.depthCompare = WGPUCompareFunction_Equal;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_False;

    const std::string key = "depth:" + std::string(textureFormatIdentifier(format)) + ":"
                            + std::to_string(sampleCount) + ":" + expectedText;
    return createDsTestPipeline(t, key, fragmentCode, depthStencil, sampleCount);
}

inline WGPURenderPipeline getStencilTestEqualPipeline(TextureZeroInitTest& t,
                                                      WGPUTextureFormat format,
                                                      uint32_t sampleCount) {
    const std::string fragmentCode = R"(
        @fragment
        fn main() -> @location(0) f32 {
          return 1.0;
        }
        )";

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
    depthStencil.depthCompare = WGPUCompareFunction_Always;
    depthStencil.format = format;
    depthStencil.stencilFront.compare = WGPUCompareFunction_Equal;
    depthStencil.stencilBack.compare = WGPUCompareFunction_Equal;

    const std::string key =
        "stencil:" + std::string(textureFormatIdentifier(format)) + ":" + std::to_string(sampleCount);
    return createDsTestPipeline(t, key, fragmentCode, depthStencil, sampleCount);
}

enum class DsCheckType {
    Depth,
    Stencil,
};

inline void checkContentsByDsTest(DsCheckType type,
                                  TextureZeroInitTest& t,
                                  WGPUTexture texture,
                                  InitializedState state,
                                  const SubresourceRange& subresourceRange) {
    const TextureZeroParams& params = t.p;
    if (params.dimension != WGPUTextureDimension_2D) {
        t.fail("depth/stencil test checks require a 2d texture");
    }
    for (const WGPUTextureViewDescriptor& viewDescriptor :
         t.generateTextureViewDescriptorsForRendering(WGPUTextureAspect_All, subresourceRange)) {
        const std::array<uint32_t, 3> mip =
            virtualMipSize(params.dimension, {t.textureWidth(), t.textureHeight(), 1}, viewDescriptor.baseMipLevel);
        const uint32_t width = mip[0];
        const uint32_t height = mip[1];

        WGPUTextureDescriptor renderDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        renderDesc.size = WGPUExtent3D{width, height, 1};
        renderDesc.format = WGPUTextureFormat_R8Unorm;
        renderDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        renderDesc.sampleCount = params.sampleCount;
        WGPUTexture renderTexture = t.createTextureTracked(renderDesc);

        WGPUTexture resolveTexture = nullptr;
        WGPUTextureView resolveTarget = nullptr;
        if (params.sampleCount > 1) {
            WGPUTextureDescriptor resolveDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            resolveDesc.size = WGPUExtent3D{width, height, 1};
            resolveDesc.format = WGPUTextureFormat_R8Unorm;
            resolveDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
            resolveTexture = t.createTextureTracked(resolveDesc);
            WGPUTextureViewDescriptor resolveViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            resolveTarget = t.createViewTracked(resolveTexture, resolveViewDesc);
        }

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        wgpuCommandEncoderPushDebugGroup(encoder, tzSv("checkContentsWithDepthStencil"));

        WGPUTextureViewDescriptor renderViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = t.createViewTracked(renderTexture, renderViewDesc);
        color.resolveTarget = resolveTarget;
        color.clearValue = WGPUColor{0, 0, 0, 0};
        color.loadOp = WGPULoadOp_Load;
        color.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        ds.view = t.createViewTracked(texture, viewDescriptor);
        if (isDepthTextureFormat(params.format)) {
            ds.depthLoadOp = WGPULoadOp_Load;
            ds.depthStoreOp = WGPUStoreOp_Store;
        }
        if (isStencilTextureFormat(params.format)) {
            ds.stencilLoadOp = WGPULoadOp_Load;
            ds.stencilStoreOp = WGPUStoreOp_Store;
        }

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &color;
        passDesc.depthStencilAttachment = &ds;
        WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);

        const PerTexelComponents expectedState = t.stateToTexelComponents(state);
        switch (type) {
            case DsCheckType::Depth:
                wgpuRenderPassEncoderSetPipeline(
                    pass, getDepthTestEqualPipeline(t, params.format, params.sampleCount, expectedState.Depth));
                break;
            case DsCheckType::Stencil:
                wgpuRenderPassEncoderSetPipeline(pass, getStencilTestEqualPipeline(t, params.format, params.sampleCount));
                wgpuRenderPassEncoderSetStencilReference(pass, static_cast<uint32_t>(expectedState.Stencil));
                break;
        }

        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        wgpuCommandEncoderPopDebugGroup(encoder);
        t.submit(encoder);

        PerTexelComponents exp;
        exp.R = 1;
        expectSingleColor(t, resolveTexture != nullptr ? resolveTexture : renderTexture, WGPUTextureFormat_R8Unorm,
                          {width, height, 1}, exp, WGPUTextureDimension_2D, 0, 0, WGPUTextureAspect_All,
                          subresourceContext(type == DsCheckType::Depth ? "checkContentsByDepthTest"
                                                                        : "checkContentsByStencilTest",
                                             state, viewDescriptor.baseMipLevel, viewDescriptor.baseArrayLayer));
    }
}

inline void checkContentsByDepthTest(TextureZeroInitTest& t,
                                     WGPUTexture texture,
                                     InitializedState state,
                                     const SubresourceRange& subresourceRange) {
    checkContentsByDsTest(DsCheckType::Depth, t, texture, state, subresourceRange);
}

inline void checkContentsByStencilTest(TextureZeroInitTest& t,
                                       WGPUTexture texture,
                                       InitializedState state,
                                       const SubresourceRange& subresourceRange) {
    checkContentsByDsTest(DsCheckType::Stencil, t, texture, state, subresourceRange);
}

}  // namespace cts::texture_zero
