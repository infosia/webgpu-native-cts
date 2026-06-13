// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/features/texture_component_swizzle.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause

#include <array>
#include <cstdlib>
#include <string>
#include <string_view>

#include "cts/test.h"
#include "feature_test_helpers.h"

using namespace cts;
using namespace cts::capability_features;

namespace {

TestGroup<FeatureGpuTest> testGroup = MakeTestGroup<FeatureGpuTest>(
    "api,validation,capability_checks,features,texture_component_swizzle",
    "Validation tests for the 'texture-component-swizzle' feature.");

constexpr std::array<std::string_view, 19> kSwizzleTests = {{
    "rgba", "0000", "1111", "rrrr", "gggg", "bbbb", "aaaa", "abgr", "gbar", "barg",
    "argb", "0gba", "r0ba", "rg0a", "rgb0", "1gba", "r1ba", "rg1a", "rgb1",
}};

std::vector<Value> swizzleValues() {
    std::vector<Value> values;
    values.reserve(kSwizzleTests.size());
    for (std::string_view swizzle : kSwizzleTests) {
        values.emplace_back(std::string(swizzle));
    }
    return values;
}

bool isIdentitySwizzle(std::string_view swizzle) {
    return swizzle == "rgba";
}

WGPUComponentSwizzle parseComponentSwizzle(char c) {
    switch (c) {
        case '0':
            return WGPUComponentSwizzle_Zero;
        case '1':
            return WGPUComponentSwizzle_One;
        case 'r':
            return WGPUComponentSwizzle_R;
        case 'g':
            return WGPUComponentSwizzle_G;
        case 'b':
            return WGPUComponentSwizzle_B;
        case 'a':
            return WGPUComponentSwizzle_A;
        default:
            std::abort();
    }
}

WGPUTextureComponentSwizzle makeSwizzle(const std::string& swizzle) {
    WGPUTextureComponentSwizzle out = WGPU_TEXTURE_COMPONENT_SWIZZLE_INIT;
    out.r = parseComponentSwizzle(swizzle[0]);
    out.g = parseComponentSwizzle(swizzle[1]);
    out.b = parseComponentSwizzle(swizzle[2]);
    out.a = parseComponentSwizzle(swizzle[3]);
    return out;
}

WGPUTextureView createSwizzledView(FeatureGpuTest& t, WGPUTexture texture, const std::string& swizzle) {
    if (isIdentitySwizzle(swizzle)) {
        // Native identity swizzle is the default all-Undefined struct, equivalently no chained descriptor.
        // Explicit R/G/B/A still counts as using the texture-component-swizzle feature.
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        return t.createViewTracked(texture, viewDesc);
    }
    WGPUTextureComponentSwizzleDescriptor swizzleDesc = WGPU_TEXTURE_COMPONENT_SWIZZLE_DESCRIPTOR_INIT;
    swizzleDesc.swizzle = makeSwizzle(swizzle);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.nextInChain = &swizzleDesc.chain;
    return t.createViewTracked(texture, viewDesc);
}

WGPUTexture createTextureForUseCase(FeatureGpuTest& t, const std::string& useCase) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = {1, 1, 1};
    if (useCase == "depth-attachment") {
        desc.format = WGPUTextureFormat_Depth16Unorm;
    } else if (useCase == "stencil-attachment") {
        desc.format = WGPUTextureFormat_Stencil8;
    } else {
        desc.format = WGPUTextureFormat_RGBA8Unorm;
    }
    desc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
    if (useCase == "storage-binding") {
        desc.usage |= WGPUTextureUsage_StorageBinding;
    }
    return t.createTextureTracked(desc);
}

CTS_TEST(testGroup, "invalid_swizzle")
    .desc("Test that setting an invalid swizzle value on a texture view throws an exception.")
    .unimplemented("JS-string swizzle parsing - no native analog");

CTS_TEST(testGroup, "only_identity_swizzle")
    .desc("Test that non-identity swizzle is invalid without texture-component-swizzle.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("swizzle", swizzleValues());
    })
    .fn([](FeatureGpuTest& t) {
        t.selectDeviceOrSkipTestCase({});
        const std::string swizzle = t.param<std::string>("swizzle");
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {1, 1, 1};
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureTracked(desc);
        expectValidationError(t, [&] {
            createSwizzledView(t, texture, swizzle);
        }, !isIdentitySwizzle(swizzle));
    });

CTS_TEST(testGroup, "no_render_no_resolve_no_storage")
    .desc("Test that non-identity swizzles cannot be used as render, resolve, or storage bindings.")
    .params([](ParamsBuilder u) {
        return u.combine("useCase",
                         {std::string("texture-binding"), std::string("color-attachment"),
                          std::string("depth-attachment"), std::string("stencil-attachment"),
                          std::string("resolve-target"), std::string("storage-binding")})
            .beginSubcases()
            .combine("swizzle", swizzleValues());
    })
    .fn([](FeatureGpuTest& t) {
        t.selectDeviceOrSkipTestCase({WGPUFeatureName_TextureComponentSwizzle});
        const std::string useCase = t.param<std::string>("useCase");
        const std::string swizzle = t.param<std::string>("swizzle");
        WGPUTexture texture = createTextureForUseCase(t, useCase);
        WGPUTextureView view = createSwizzledView(t, texture, swizzle);
        const bool shouldError = useCase != "texture-binding" && !isIdentitySwizzle(swizzle);

        if (useCase == "texture-binding") {
            WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entry.binding = 0;
            entry.visibility = WGPUShaderStage_Fragment;
            entry.texture.sampleType = WGPUTextureSampleType_Float;
            entry.texture.viewDimension = WGPUTextureViewDimension_2D;
            WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            layoutDesc.entryCount = 1;
            layoutDesc.entries = &entry;
            WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(layoutDesc);
            WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            bgEntry.binding = 0;
            bgEntry.textureView = view;
            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout = layout;
            bgDesc.entryCount = 1;
            bgDesc.entries = &bgEntry;
            expectValidationError(t, [&] { t.createBindGroupTracked(bgDesc); }, shouldError);
            return;
        }

        if (useCase == "storage-binding") {
            WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entry.binding = 0;
            entry.visibility = WGPUShaderStage_Compute;
            entry.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
            entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
            entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
            WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            layoutDesc.entryCount = 1;
            layoutDesc.entries = &entry;
            WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(layoutDesc);
            WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            bgEntry.binding = 0;
            bgEntry.textureView = view;
            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout = layout;
            bgDesc.entryCount = 1;
            bgDesc.entries = &bgEntry;
            expectValidationError(t, [&] { t.createBindGroupTracked(bgDesc); }, shouldError);
            return;
        }

        expectValidationError(t, [&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            if (useCase == "depth-attachment" || useCase == "stencil-attachment") {
                WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
                ds.view = view;
                ds.depthLoadOp = useCase == "depth-attachment" ? WGPULoadOp_Clear : WGPULoadOp_Undefined;
                ds.depthStoreOp = useCase == "depth-attachment" ? WGPUStoreOp_Store : WGPUStoreOp_Undefined;
                ds.depthClearValue = 1.0f;
                ds.stencilLoadOp = useCase == "stencil-attachment" ? WGPULoadOp_Clear : WGPULoadOp_Undefined;
                ds.stencilStoreOp = useCase == "stencil-attachment" ? WGPUStoreOp_Store : WGPUStoreOp_Undefined;
                ds.stencilClearValue = 0;
                WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.depthStencilAttachment = &ds;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                if (pass != nullptr) {
                    wgpuRenderPassEncoderEnd(pass);
                    wgpuRenderPassEncoderRelease(pass);
                }
            } else {
                WGPUTextureDescriptor msaaDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                msaaDesc.size = {1, 1, 1};
                msaaDesc.format = WGPUTextureFormat_RGBA8Unorm;
                msaaDesc.usage = WGPUTextureUsage_RenderAttachment;
                msaaDesc.sampleCount = 4;
                WGPUTexture msaaTexture = t.createTextureTracked(msaaDesc);
                WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                WGPUTextureView colorView = useCase == "resolve-target"
                    ? t.createViewTracked(msaaTexture, viewDesc)
                    : view;
                WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                color.view = colorView;
                color.resolveTarget = useCase == "resolve-target" ? view : nullptr;
                color.loadOp = WGPULoadOp_Clear;
                color.storeOp = WGPUStoreOp_Store;
                WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &color;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                if (pass != nullptr) {
                    wgpuRenderPassEncoderEnd(pass);
                    wgpuRenderPassEncoderRelease(pass);
                }
            }
            t.finishTracked(encoder);
        }, shouldError);
    });

CTS_TEST(testGroup, "compatibility_mode")
    .desc("Test that in compatibility mode, swizzles must be equivalent.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("swizzle", swizzleValues())
            .combine("otherSwizzle", swizzleValues())
            .combine("pipelineType", {std::string("render"), std::string("compute")});
    })
    .fn([](FeatureGpuTest& t) {
        t.selectDeviceOrSkipTestCase({WGPUFeatureName_TextureComponentSwizzle});
        const std::string swizzle = t.param<std::string>("swizzle");
        const std::string otherSwizzle = t.param<std::string>("otherSwizzle");
        const std::string pipelineType = t.param<std::string>("pipelineType");

        const char* code =
            "@group(0) @binding(0) var tex0: texture_2d<f32>;\n"
            "@group(1) @binding(0) var tex1: texture_2d<f32>;\n"
            "@compute @workgroup_size(1) fn cs() { _ = tex0; _ = tex1; }\n"
            "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0); }\n"
            "@fragment fn fs() -> @location(0) vec4f { _ = tex0; _ = tex1; return vec4f(0); }\n";
        WGPUShaderModule module = t.createShaderModuleTracked(code);

        WGPUComputePipeline computePipeline = nullptr;
        WGPURenderPipeline renderPipeline = nullptr;
        if (pipelineType == "compute") {
            WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            desc.layout = nullptr;
            desc.compute.module = module;
            desc.compute.entryPoint = sv("cs");
            computePipeline = t.createComputePipelineTracked(desc);
        } else {
            WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
            target.format = WGPUTextureFormat_RGBA8Unorm;
            target.writeMask = WGPUColorWriteMask_All;
            WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
            fragment.module = module;
            fragment.entryPoint = sv("fs");
            fragment.targetCount = 1;
            fragment.targets = &target;
            WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            desc.layout = nullptr;
            desc.vertex.module = module;
            desc.vertex.entryPoint = sv("vs");
            desc.fragment = &fragment;
            renderPipeline = t.createRenderPipelineTracked(desc);
        }

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = {1, 1, 1};
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUTextureView view0 = createSwizzledView(t, texture, swizzle);
        WGPUTextureView view1 = createSwizzledView(t, texture, otherSwizzle);

        WGPUBindGroupLayout layout0 = pipelineType == "compute"
            ? wgpuComputePipelineGetBindGroupLayout(computePipeline, 0)
            : wgpuRenderPipelineGetBindGroupLayout(renderPipeline, 0);
        WGPUBindGroupLayout layout1 = pipelineType == "compute"
            ? wgpuComputePipelineGetBindGroupLayout(computePipeline, 1)
            : wgpuRenderPipelineGetBindGroupLayout(renderPipeline, 1);
        WGPUBindGroupEntry entry0 = WGPU_BIND_GROUP_ENTRY_INIT;
        entry0.binding = 0;
        entry0.textureView = view0;
        WGPUBindGroupDescriptor bgDesc0 = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc0.layout = layout0;
        bgDesc0.entryCount = 1;
        bgDesc0.entries = &entry0;
        WGPUBindGroup bindGroup0 = t.createBindGroupTracked(bgDesc0);
        WGPUBindGroupEntry entry1 = WGPU_BIND_GROUP_ENTRY_INIT;
        entry1.binding = 0;
        entry1.textureView = view1;
        WGPUBindGroupDescriptor bgDesc1 = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc1.layout = layout1;
        bgDesc1.entryCount = 1;
        bgDesc1.entries = &entry1;
        WGPUBindGroup bindGroup1 = t.createBindGroupTracked(bgDesc1);
        wgpuBindGroupLayoutRelease(layout0);
        wgpuBindGroupLayoutRelease(layout1);

        expectValidationError(t, [&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            if (pipelineType == "compute") {
                WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
                WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
                wgpuComputePassEncoderSetPipeline(pass, computePipeline);
                wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup0, 0, nullptr);
                wgpuComputePassEncoderSetBindGroup(pass, 1, bindGroup1, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            } else {
                WGPUTextureDescriptor colorDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                colorDesc.size = {1, 1, 1};
                colorDesc.format = WGPUTextureFormat_RGBA8Unorm;
                colorDesc.usage = WGPUTextureUsage_RenderAttachment;
                WGPUTexture colorTexture = t.createTextureTracked(colorDesc);
                WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                WGPUTextureView colorView = t.createViewTracked(colorTexture, viewDesc);
                WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                color.view = colorView;
                color.loadOp = WGPULoadOp_Clear;
                color.storeOp = WGPUStoreOp_Store;
                WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments = &color;
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                wgpuRenderPassEncoderSetPipeline(pass, renderPipeline);
                wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup0, 0, nullptr);
                wgpuRenderPassEncoderSetBindGroup(pass, 1, bindGroup1, 0, nullptr);
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }
            t.finishTracked(encoder);
        }, false);
    });

} // namespace
