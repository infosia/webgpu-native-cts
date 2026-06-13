// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/features/texture_formats_tier2.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause

#include <string>

#include "cts/test.h"
#include "feature_test_helpers.h"

using namespace cts;
using namespace cts::capability_features;

namespace {

TestGroup<FeatureGpuTest> testGroup = MakeTestGroup<FeatureGpuTest>(
    "api,validation,capability_checks,features,texture_formats_tier2",
    "Tests for capability checking for the 'texture-formats-tier2' feature.");

void selectTier2IfEnabled(FeatureGpuTest& t, bool enable) {
    if (enable) {
        t.selectDeviceOrSkipTestCase({WGPUFeatureName_TextureFormatsTier2});
    } else {
        t.selectDeviceOrSkipTestCase({});
    }
}

WGPUShaderModule createReadWriteStorageTextureModule(FeatureGpuTest& t, WGPUTextureFormat format) {
    const std::string formatId(textureFormatIdentifier(format));
    const std::string code =
        "@group(0) @binding(0) var tex1d: texture_storage_1d<" + formatId + ", read_write>;\n"
        "@group(0) @binding(1) var tex2d: texture_storage_1d<" + formatId + ", read_write>;\n"
        "@group(0) @binding(2) var tex3d: texture_storage_1d<" + formatId + ", read_write>;\n"
        "fn useTextures() { _ = tex1d; _ = tex2d; _ = tex3d; }\n"
        "@compute @workgroup_size(1) fn cs() { useTextures(); }\n"
        "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0); }\n"
        "@fragment fn fs() -> @location(0) vec4f { useTextures(); return vec4f(0); }\n";
    return t.createShaderModuleTracked(code);
}

CTS_TEST(testGroup, "enables_rg11b10ufloat_renderable_and_texture_formats_tier1")
    .desc("Test that texture-formats-tier2 enables rg11b10ufloat-renderable and texture-formats-tier1.")
    .fn([](FeatureGpuTest& t) {
        t.selectDeviceOrSkipTestCase({WGPUFeatureName_TextureFormatsTier2});
        t.expect(hasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable));
        t.expect(hasFeature(t.device(), WGPUFeatureName_TextureFormatsTier1));
    });

CTS_TEST(testGroup, "bind_group_layout,storage_binding_read_write_access")
    .desc("Read-write storage texture BGL access for tier2 formats.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kTextureFormatsTier2EnablesStorageReadWrite))
            .combine("enable_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_feature");
        selectTier2IfEnabled(t, enable);
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
        entry.storageTexture.format = format;
        entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;
        expectValidationError(t, [&] { t.createBindGroupLayoutTracked(desc); }, !enable);
    });

CTS_TEST(testGroup, "pipeline_auto_layout,storage_texture")
    .desc("Auto-layout pipelines with tier2 read-write storage texture formats.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kTextureFormatsTier2EnablesStorageReadWrite))
            .combine("enable_feature", {true, false})
            .beginSubcases()
            .combine("isAsync", {false, true})
            .combine("type", {std::string("compute"), std::string("render")});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_feature");
        const bool isAsync = t.param<bool>("isAsync");
        const std::string type = t.param<std::string>("type");
        selectTier2IfEnabled(t, enable);
        WGPUShaderModule module = createReadWriteStorageTextureModule(t, format);
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
