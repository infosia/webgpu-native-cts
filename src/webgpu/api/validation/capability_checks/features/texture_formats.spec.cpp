// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/features/texture_formats.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause

#include <array>
#include <string>
#include <vector>

#include "cts/test.h"
#include "feature_test_helpers.h"

using namespace cts;
using namespace cts::capability_features;

namespace {

TestGroup<FeatureGpuTest> testGroup = MakeTestGroup<FeatureGpuTest>(
    "api,validation,capability_checks,features,texture_formats",
    "Tests for capability checking for features enabling optional texture formats.");

std::vector<Value> optionalFormatValues() {
    return formatValues(kOptionalTextureFormats);
}

std::vector<Value> optionalStorageFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kOptionalTextureFormats) {
        if (isTextureFormatPossiblyStorageReadable(format)) {
            values.emplace_back(std::string(textureFormatIdentifier(format)));
        }
    }
    return values;
}

std::vector<Value> optionalColorRenderableFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kOptionalTextureFormats) {
        if (isTextureFormatPossiblyUsableAsColorRenderAttachment(format)) {
            values.emplace_back(std::string(textureFormatIdentifier(format)));
        }
    }
    return values;
}

std::vector<Value> optionalDepthStencilFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kOptionalTextureFormats) {
        if (isDepthOrStencilTextureFormat(format)) {
            values.emplace_back(std::string(textureFormatIdentifier(format)));
        }
    }
    return values;
}

void selectForFormatIfEnabled(FeatureGpuTest& t, WGPUTextureFormat format, bool enable) {
    if (enable) {
        t.selectDeviceForTextureFormatOrSkipTestCase(format);
    } else {
        t.selectDeviceOrSkipTestCase({});
    }
}

void createTextureWithFormat(FeatureGpuTest& t, WGPUTextureFormat format, WGPUTextureUsage usage) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = {info.blockWidth, info.blockHeight, 1};
    desc.format = format;
    desc.usage = usage;
    t.createTextureTracked(desc);
}

void makeSimpleRenderPipeline(
    FeatureGpuTest& t,
    bool isAsync,
    bool success,
    WGPUTextureFormat colorFormat,
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(
        "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
        "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
        "}\n");
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(
        "@fragment fn main() -> @location(0) vec4<f32> {\n"
        "  return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
        "}\n");
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = colorFormat;
    target.writeMask = WGPUColorWriteMask_All;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = sv("main");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    depthStencil.format = depthStencilFormat;
    depthStencil.depthCompare = WGPUCompareFunction_Always;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_False;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = sv("main");
    desc.fragment = &fragment;
    if (depthStencilFormat != WGPUTextureFormat_Undefined) {
        desc.depthStencil = &depthStencil;
    }
    doCreateRenderPipelineTest(t, isAsync, success, desc);
}

CTS_TEST(testGroup, "texture_descriptor")
    .desc("Optional texture formats require their enabling feature in texture descriptors.")
    .params([](ParamsBuilder u) {
        return u.combine("format", optionalFormatValues()).combine("enable_required_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_required_feature");
        selectForFormatIfEnabled(t, format, enable);
        expectValidationError(t, [&] {
            createTextureWithFormat(t, format, WGPUTextureUsage_TextureBinding);
        }, !enable);
    });

CTS_TEST(testGroup, "texture_descriptor_view_formats")
    .desc("Optional texture formats require their enabling feature in texture viewFormats.")
    .params([](ParamsBuilder u) {
        return u.combine("format", optionalFormatValues()).combine("enable_required_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_required_feature");
        selectForFormatIfEnabled(t, format, enable);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        expectValidationError(t, [&] {
            WGPUTextureFormat viewFormats[] = {format};
            WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            desc.size = {info.blockWidth, info.blockHeight, 1};
            desc.format = format;
            desc.usage = WGPUTextureUsage_TextureBinding;
            desc.viewFormatCount = 1;
            desc.viewFormats = viewFormats;
            t.createTextureTracked(desc);
        }, !enable);
    });

CTS_TEST(testGroup, "texture_view_descriptor")
    .desc("Optional texture formats require their enabling feature in texture view descriptors.")
    .params([](ParamsBuilder u) {
        return u.combine("format", optionalFormatValues()).combine("enable_required_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_required_feature");
        selectForFormatIfEnabled(t, format, enable);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.size = {info.blockWidth, info.blockHeight, 1};
        textureDesc.format = enable ? format : WGPUTextureFormat_RGBA8Unorm;
        textureDesc.usage = WGPUTextureUsage_TextureBinding;
        WGPUTexture texture = t.createTextureTracked(textureDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.format = format;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.aspect = WGPUTextureAspect_All;
        viewDesc.arrayLayerCount = 1;
        viewDesc.mipLevelCount = 1;
        expectValidationError(t, [&] {
            t.createViewTracked(texture, viewDesc);
        }, !enable);
    });

CTS_TEST(testGroup, "texture_compression_bc_sliced_3d")
    .desc("3D BC textures require both texture-compression-bc and texture-compression-bc-sliced-3d.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kBCCompressedTextureFormats))
            .combine("supportsBC", {false, true})
            .combine("supportsBCSliced3D", {false, true});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool supportsBC = t.param<bool>("supportsBC");
        const bool supportsBCSliced3D = t.param<bool>("supportsBCSliced3D");
        std::vector<WGPUFeatureName> features;
        if (supportsBC) {
            features.push_back(WGPUFeatureName_TextureCompressionBC);
        }
        if (supportsBCSliced3D) {
            features.push_back(WGPUFeatureName_TextureCompressionBCSliced3D);
        }
        t.selectDeviceOrSkipTestCase(features);
        t.skipIfTextureFormatNotSupported(format);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {info.blockWidth, info.blockHeight, 1};
        desc.dimension = WGPUTextureDimension_3D;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;
        expectValidationError(t, [&] { t.createTextureTracked(desc); }, !supportsBC || !supportsBCSliced3D);
    });

CTS_TEST(testGroup, "texture_compression_astc_sliced_3d")
    .desc("3D ASTC textures require both texture-compression-astc and texture-compression-astc-sliced-3d.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kASTCCompressedTextureFormats))
            .combine("supportsASTC", {false, true})
            .combine("supportsASTCSliced3D", {false, true});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool supportsASTC = t.param<bool>("supportsASTC");
        const bool supportsASTCSliced3D = t.param<bool>("supportsASTCSliced3D");
        std::vector<WGPUFeatureName> features;
        if (supportsASTC) {
            features.push_back(WGPUFeatureName_TextureCompressionASTC);
        }
        if (supportsASTCSliced3D) {
            features.push_back(WGPUFeatureName_TextureCompressionASTCSliced3D);
        }
        t.selectDeviceOrSkipTestCase(features);
        t.skipIfTextureFormatNotSupported(format);
        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = {info.blockWidth, info.blockHeight, 1};
        desc.dimension = WGPUTextureDimension_3D;
        desc.format = format;
        desc.usage = WGPUTextureUsage_TextureBinding;
        expectValidationError(t, [&] { t.createTextureTracked(desc); }, !supportsASTC || !supportsASTCSliced3D);
    });

CTS_TEST(testGroup, "canvas_configuration")
    .desc("Canvas configuration has no native C API analog.")
    .unimplemented("canvas - no native C-API");

CTS_TEST(testGroup, "canvas_configuration_view_formats")
    .desc("Canvas viewFormats configuration has no native C API analog.")
    .unimplemented("canvas - no native C-API");

CTS_TEST(testGroup, "storage_texture_binding_layout")
    .desc("Optional storage texture binding layout formats require their enabling feature.")
    .params([](ParamsBuilder u) {
        return u.combine("format", optionalStorageFormatValues())
            .combine("enable_required_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_required_feature");
        selectForFormatIfEnabled(t, format, enable);
        if (enable && !t.isTextureFormatUsableWithStorageAccessMode(format, WGPUStorageTextureAccess_WriteOnly)) {
            t.skip("texture format is not usable with write-only storage access");
        }
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entry.storageTexture.format = format;
        entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;
        expectValidationError(t, [&] { t.createBindGroupLayoutTracked(desc); }, !enable);
    });

CTS_TEST(testGroup, "color_target_state")
    .desc("Optional color target formats require their enabling feature.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combine("format", optionalColorRenderableFormatValues())
            .combine("enable_required_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_required_feature");
        selectForFormatIfEnabled(t, format, enable);
        if (enable && !t.isTextureFormatUsableAsRenderAttachment(format)) {
            t.skip("texture format is not usable as render attachment");
        }
        makeSimpleRenderPipeline(t, isAsync, enable, format);
    });

CTS_TEST(testGroup, "depth_stencil_state")
    .desc("Optional depth/stencil formats require their enabling feature.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false, true})
            .combine("format", optionalDepthStencilFormatValues())
            .combine("enable_required_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_required_feature");
        selectForFormatIfEnabled(t, format, enable);
        makeSimpleRenderPipeline(t, isAsync, enable, WGPUTextureFormat_RGBA8Unorm, format);
    });

CTS_TEST(testGroup, "render_bundle_encoder_descriptor_color_format")
    .desc("Optional render bundle color formats require their enabling feature.")
    .params([](ParamsBuilder u) {
        return u.combine("format", optionalColorRenderableFormatValues())
            .combine("enable_required_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_required_feature");
        selectForFormatIfEnabled(t, format, enable);
        if (enable && !t.isTextureFormatUsableAsRenderAttachment(format)) {
            t.skip("texture format is not usable as render attachment");
        }
        WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        desc.colorFormatCount = 1;
        desc.colorFormats = &format;
        expectValidationError(t, [&] {
            WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
            if (encoder != nullptr) {
                wgpuRenderBundleEncoderRelease(encoder);
            }
        }, !enable);
    });

CTS_TEST(testGroup, "render_bundle_encoder_descriptor_depth_stencil_format")
    .desc("Optional render bundle depth/stencil formats require their enabling feature.")
    .params([](ParamsBuilder u) {
        return u.combine("format", optionalDepthStencilFormatValues())
            .combine("enable_required_feature", {true, false});
    })
    .fn([](FeatureGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const bool enable = t.param<bool>("enable_required_feature");
        selectForFormatIfEnabled(t, format, enable);
        WGPUTextureFormat colorFormat = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        desc.colorFormatCount = 1;
        desc.colorFormats = &colorFormat;
        desc.depthStencilFormat = format;
        expectValidationError(t, [&] {
            WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
            if (encoder != nullptr) {
                wgpuRenderBundleEncoderRelease(encoder);
            }
        }, !enable);
    });

CTS_TEST(testGroup, "check_capability_guarantees")
    .desc("Check adapter feature guarantees for optional texture compression features.")
    .fn([](FeatureGpuTest& t) {
        WGPUAdapter adapter = t.adapter();
        const bool supportsBC = hasFeature(adapter, WGPUFeatureName_TextureCompressionBC);
        const bool supportsBCSliced3D = hasFeature(adapter, WGPUFeatureName_TextureCompressionBCSliced3D);
        const bool supportsASTC = hasFeature(adapter, WGPUFeatureName_TextureCompressionASTC);
        const bool supportsASTCSliced3D = hasFeature(adapter, WGPUFeatureName_TextureCompressionASTCSliced3D);
        const bool supportsETC2 = hasFeature(adapter, WGPUFeatureName_TextureCompressionETC2);
        t.expect(supportsBC || (supportsETC2 && supportsASTC),
                 "Adapter must support BC or both ETC2 and ASTC");
        if (supportsBCSliced3D) {
            t.expect(supportsBC, "If BC Sliced 3D is supported, BC must be supported");
        }
        if (supportsASTCSliced3D) {
            t.expect(supportsASTC, "If ASTC Sliced 3D is supported, ASTC must be supported");
        }
    });

} // namespace
