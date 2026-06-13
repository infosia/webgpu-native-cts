// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/features/texture_formats_tier1.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause

#include <string>
#include <vector>

#include "cts/test.h"
#include "feature_test_helpers.h"

using namespace cts;
using namespace cts::capability_features;

namespace {

TestGroup<FeatureGpuTest> testGroup = MakeTestGroup<FeatureGpuTest>(
    "api,validation,capability_checks,features,texture_formats_tier1",
    "Tests for capability checking for the 'texture-formats-tier1' feature.");

std::vector<Value> rgba8PlusTier1ColorFormats() {
    std::vector<Value> values;
    values.emplace_back(std::string(textureFormatIdentifier(WGPUTextureFormat_RGBA8Unorm)));
    const std::vector<Value> tier1 = formatValues(kTextureFormatTier1AllowsRenderAttachmentBlendableMultisample);
    values.insert(values.end(), tier1.begin(), tier1.end());
    return values;
}

void selectTier1IfEnabled(FeatureGpuTest& t, bool enable) {
    if (enable) {
        t.selectDeviceOrSkipTestCase({WGPUFeatureName_TextureFormatsTier1});
    } else {
        t.selectDeviceOrSkipTestCase({});
    }
}

WGPUShaderModule createStorageTextureModule(
    FeatureGpuTest& t,
    WGPUTextureFormat format,
    const std::string& access) {
    const std::string formatId(textureFormatIdentifier(format));
    const std::string code =
        "@group(0) @binding(0) var tex1d: texture_storage_1d<" + formatId + ", " + access + ">;\n"
        "@group(0) @binding(1) var tex2d: texture_storage_1d<" + formatId + ", " + access + ">;\n"
        "@group(0) @binding(2) var tex3d: texture_storage_1d<" + formatId + ", " + access + ">;\n"
        "fn useTextures() { _ = tex1d; _ = tex2d; _ = tex3d; }\n"
        "@compute @workgroup_size(1) fn cs() { useTextures(); }\n"
        "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0); }\n"
        "@fragment fn fs() -> @location(0) vec4f { useTextures(); return vec4f(0); }\n";
    return t.createShaderModuleTracked(code);
}

CTS_TEST(testGroup, "enables_rg11b10ufloat_renderable")
    .desc("Test that enabling texture-formats-tier1 also enables rg11b10ufloat-renderable.")
    .fn([](FeatureGpuTest& t) {
        t.selectDeviceOrSkipTestCase({WGPUFeatureName_TextureFormatsTier1});
        t.expect(hasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable));
    });

CTS_TEST(testGroup, "texture_usage,render_attachment")
    .desc("RENDER_ATTACHMENT usage for tier1 formats is gated by texture-formats-tier1.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kTextureFormatTier1AllowsRenderAttachmentBlendableMultisample))
            .combine("enable_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_feature");
        selectTier1IfEnabled(t, enable);
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {1, 1, 1};
        desc.format = format;
        desc.usage = WGPUTextureUsage_RenderAttachment;
        // Native has no exception path; upstream exception-or-validation collapses to validation error.
        expectValidationError(t, [&] { t.createTextureTracked(desc); }, !enable);
    });

CTS_TEST(testGroup, "texture_usage,multisample")
    .desc("Multisampled tier1 render attachment formats are gated by texture-formats-tier1.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kTextureFormatTier1AllowsRenderAttachmentBlendableMultisample))
            .combine("enable_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_feature");
        selectTier1IfEnabled(t, enable);
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {1, 1, 1};
        desc.format = format;
        desc.usage = WGPUTextureUsage_RenderAttachment;
        desc.sampleCount = 4;
        expectValidationError(t, [&] { t.createTextureTracked(desc); }, !enable);
    });

CTS_TEST(testGroup, "texture_usage,storage_binding")
    .desc("STORAGE_BINDING usage for tier1 formats is gated by texture-formats-tier1.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly))
            .combine("enable_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_feature");
        selectTier1IfEnabled(t, enable);
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {1, 1, 1};
        desc.format = format;
        desc.usage = WGPUTextureUsage_StorageBinding;
        expectValidationError(t, [&] { t.createTextureTracked(desc); }, !enable);
    });

CTS_TEST(testGroup, "render_pipeline,color_target")
    .desc("Render pipeline color target capabilities enabled by texture-formats-tier1.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combine("format", rgba8PlusTier1ColorFormats())
            .combine("enable_feature", {true, false})
            .combine("check", {std::string("RENDER_ATTACHMENT"), std::string("blendable"), std::string("multisample")});
    })
    .fn([](FeatureGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_feature");
        const std::string check = t.param<std::string>("check");
        selectTier1IfEnabled(t, enable);

        WGPUShaderModule vertexModule = t.createShaderModuleTracked(
            "@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }\n");
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(
            "@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }\n");
        WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
        blend.color.operation = WGPUBlendOperation_Add;
        blend.color.srcFactor = WGPUBlendFactor_One;
        blend.color.dstFactor = WGPUBlendFactor_Zero;
        blend.alpha.operation = WGPUBlendOperation_Add;
        blend.alpha.srcFactor = WGPUBlendFactor_One;
        blend.alpha.dstFactor = WGPUBlendFactor_Zero;
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format;
        target.writeMask = WGPUColorWriteMask_All;
        if (check == "blendable") {
            target.blend = &blend;
        }
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fragmentModule;
        fragment.entryPoint = sv("main");
        fragment.targetCount = 1;
        fragment.targets = &target;
        WGPUMultisampleState multisample = WGPU_MULTISAMPLE_STATE_INIT;
        if (check == "multisample") {
            multisample.count = 4;
        }
        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = nullptr;
        desc.vertex.module = vertexModule;
        desc.vertex.entryPoint = sv("main");
        desc.fragment = &fragment;
        desc.multisample = multisample;
        const bool success = enable || format == WGPUTextureFormat_RGBA8Unorm;
        doCreateRenderPipelineTest(t, isAsync, success, desc);
    });

CTS_TEST(testGroup, "render_pass,resolvable")
    .desc("Resolve attachments for tier1 formats succeed when texture-formats-tier1 is enabled.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kTextureFormatTier1AllowsResolve))
            .combine("enable_feature", {true});
    })
    .fn([](FeatureGpuTest& t) {
        (void)t.param<bool>("enable_feature");
        t.selectDeviceOrSkipTestCase({WGPUFeatureName_TextureFormatsTier1});
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        WGPUTextureDescriptor msaaDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        msaaDesc.size = {1, 1, 1};
        msaaDesc.format = format;
        msaaDesc.sampleCount = 4;
        msaaDesc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture msaa = t.createTextureTracked(msaaDesc);
        WGPUTextureDescriptor resolveDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        resolveDesc.size = {1, 1, 1};
        resolveDesc.format = format;
        resolveDesc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture resolve = t.createTextureTracked(resolveDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView msaaView = t.createViewTracked(msaa, viewDesc);
        WGPUTextureView resolveView = t.createViewTracked(resolve, viewDesc);
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view = msaaView;
        color.resolveTarget = resolveView;
        color.loadOp = WGPULoadOp_Clear;
        color.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &color;
        expectValidationError(t, [&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            t.finishTracked(encoder);
        }, false);
    });

CTS_TEST(testGroup, "bind_group_layout,storage_texture")
    .desc("Storage texture BGL read-only/write-only access for tier1 formats.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly))
            .combine("access", {std::string("read-only"), std::string("write-only")})
            .combine("enable_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string access = t.param<std::string>("access");
        const bool enable = t.param<bool>("enable_feature");
        selectTier1IfEnabled(t, enable);
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.storageTexture.access = access == "read-only"
            ? WGPUStorageTextureAccess_ReadOnly
            : WGPUStorageTextureAccess_WriteOnly;
        entry.storageTexture.format = format;
        entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;
        expectValidationError(t, [&] { t.createBindGroupLayoutTracked(desc); }, !enable);
    });

CTS_TEST(testGroup, "pipeline_auto_layout,storage_texture")
    .desc("Auto-layout pipelines with tier1 storage texture formats.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly))
            .combine("access", {std::string("read"), std::string("write")})
            .combine("enable_feature", {true, false})
            .beginSubcases()
            .combine("isAsync", {false, true})
            .combine("type", {std::string("compute"), std::string("render")});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string access = t.param<std::string>("access");
        const bool enable = t.param<bool>("enable_feature");
        const bool isAsync = t.param<bool>("isAsync");
        const std::string type = t.param<std::string>("type");
        selectTier1IfEnabled(t, enable);
        WGPUShaderModule module = createStorageTextureModule(t, format, access);
        if (type == "compute") {
            WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            desc.layout = nullptr;
            desc.compute.module = module;
            desc.compute.entryPoint = sv("cs");
            doCreateComputePipelineTest(t, isAsync, enable, desc);
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
            doCreateRenderPipelineTest(t, isAsync, enable, desc);
        }
    });

} // namespace
