// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/encoding/programmable/pipeline_immediate.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,programmable,pipeline_immediate",
    "Validate immediate data usage in RenderPassEncoder, ComputePassEncoder, and RenderBundleEncoder.");

std::vector<Value> programmableEncoderTypes() {
    return {std::string("compute pass"), std::string("render pass"), std::string("render bundle")};
}

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

std::vector<ParamRecord> requiredSlotsRows() {
    std::vector<ParamRecord> rows;
    const std::vector<std::string> encoderTypes = {"compute pass", "render pass", "render bundle"};
    const std::vector<std::string> scenarios = {
        "scalar",
        "vector",
        "struct_padding",
        "dynamic_indexing",
        "mixed_types",
        "multiple_variables",
    };
    const std::vector<std::string> usages = {"full", "partial", "split", "overprovision"};
    for (const std::string& encoderType : encoderTypes) {
        const std::vector<std::string> stages =
            encoderType == "compute pass"
                ? std::vector<std::string>{"compute"}
                : std::vector<std::string>{"vertex", "fragment", "both"};
        for (const std::string& scenario : scenarios) {
            for (const std::string& usage : usages) {
                if (scenario == "scalar" && usage == "split") {
                    continue;
                }
                for (const std::string& stage : stages) {
                    rows.push_back(ParamRecord{
                        {"encoderType", encoderType},
                        {"scenario", scenario},
                        {"usage", usage},
                        {"stage", stage},
                    });
                }
            }
        }
    }
    return rows;
}

void skipImmediateWritePath(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPULimits limits = t.getLimits();
    const uint32_t maxImmediateSize = limits.maxImmediateSize;
    if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
        t.skip("Immediate data not supported (maxImmediateSize is 0 or undefined)");
    }
    t.skip("encoder.setImmediates not exported by any backend at this revision");
}

void skipIfImmediateUnsupported(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPULimits limits = t.getLimits();
    const uint32_t maxImmediateSize = limits.maxImmediateSize;
    if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
        t.skip("Immediate data not supported (maxImmediateSize is 0 or undefined)");
    }
}

WGPUTextureView makeRenderView(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{4, 4, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(desc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, viewDesc);
}

WGPUPipelineLayout makeImmediateLayout(AllFeaturesMaxLimitsGpuTest& t, uint32_t immediateSize) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 0;
    desc.bindGroupLayouts = nullptr;
    desc.immediateSize = immediateSize;
    return t.createPipelineLayoutTracked(desc);
}

WGPUComputePipeline makeComputePipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout, std::string_view code) {
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main_compute");
    return t.createComputePipelineTracked(desc);
}

WGPURenderPipeline makeRenderPipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout, std::string_view code) {
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("main_fragment");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("main_vertex");
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

void runUnusedImmediateNoWrites(AllFeaturesMaxLimitsGpuTest& t, const std::string& encoderType, const std::string& scenario) {
    skipIfImmediateUnsupported(t);
    const char* notReferenced =
        "var<immediate> data: vec4<u32>;\n"
        "@compute @workgroup_size(1) fn main_compute() {}\n"
        "@vertex fn main_vertex() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }\n"
        "@fragment fn main_fragment() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }";
    const char* referencedInUnusedFunction =
        "var<immediate> data: vec4<u32>;\n"
        "fn unused_helper() { _ = data; }\n"
        "@compute @workgroup_size(1) fn main_compute() {}\n"
        "@vertex fn main_vertex() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }\n"
        "@fragment fn main_fragment() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 1.0, 0.0, 1.0); }";
    const char* code = scenario == "not_referenced" ? notReferenced : referencedInUnusedFunction;
    WGPUPipelineLayout layout = makeImmediateLayout(t, 16);
    WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();
    if (encoderType == "compute pass") {
        WGPUComputePipeline pipeline = makeComputePipeline(t, layout, code);
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(commandEncoder, nullptr);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer cb = t.finishTracked(commandEncoder);
        t.expectValidationError([&] { wgpuQueueSubmit(t.queue(), 1, &cb); }, false);
        return;
    }
    WGPURenderPipeline pipeline = makeRenderPipeline(t, layout, code);
    WGPUTextureView view = makeRenderView(t);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(commandEncoder, &passDesc);
    if (encoderType == "render bundle") {
        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor bundleDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc.colorFormatCount = 1;
        bundleDesc.colorFormats = &format;
        WGPURenderBundleEncoder bundleEncoder = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc);
        wgpuRenderBundleEncoderSetPipeline(bundleEncoder, pipeline);
        wgpuRenderBundleEncoderDraw(bundleEncoder, 3, 1, 0, 0);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEncoder, nullptr);
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
        wgpuRenderBundleRelease(bundle);
        wgpuRenderBundleEncoderRelease(bundleEncoder);
    } else {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
    WGPUCommandBuffer cb = t.finishTracked(commandEncoder);
    t.expectValidationError([&] { wgpuQueueSubmit(t.queue(), 1, &cb); }, false);
}

CTS_TEST(testGroup, "required_slots_set")
    .desc("Validate that all immediate data slots required by the pipeline are set on the encoder.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(requiredSlotsRows());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage = t.param<std::string>("usage");
        if (usage != "none") {
            skipImmediateWritePath(t);
        }
        runUnusedImmediateNoWrites(t, t.param<std::string>("encoderType"), t.param<std::string>("scenario"));
    });

CTS_TEST(testGroup, "unused_variable")
    .desc("Validate that if an immediate data variable is declared but not statically used, it does not require slots to be set.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
                .combine("usage", {std::string("none"), std::string("partial_start")})
                .combine("scenario", {std::string("not_referenced"), std::string("referenced_in_unused_function")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateWritePath(t);
    });

CTS_TEST(testGroup, "overprovisioned_immediate_data")
    .desc("Validate that setting more immediate data than used by the shader is valid.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
                .combine("scenario", {std::string("larger_than_shader"), std::string("larger_than_layout")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateWritePath(t);
    });

CTS_TEST(testGroup, "render_bundle_execution_state_invalidation")
    .desc("Validate that executeBundles invalidates the current immediate data state in the RenderPassEncoder.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("resetImmediates", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateWritePath(t);
    });

} // namespace
