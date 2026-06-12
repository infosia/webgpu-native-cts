// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/encoding/programmable/pipeline_bind_group_compat.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "api,validation,encoding,programmable,pipeline_bind_group_compat",
    "TODO:\n- test compatibility between bind groups and pipelines\nTODO: subsume existing test, rewrite fixture as needed.\nTODO: Add externalTexture to kResourceTypes [1]");

struct Context {
    std::string encoderType;
    WGPUCommandEncoder commandEncoder = nullptr;
    WGPUComputePassEncoder computePass = nullptr;
    WGPURenderPassEncoder renderPass = nullptr;
    WGPURenderBundleEncoder bundle = nullptr;
};

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

std::vector<Value> programmableEncoderTypes() {
    return {std::string("compute pass"), std::string("render pass"), std::string("render bundle")};
}

WGPUBuffer makeBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
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

WGPURenderPassEncoder beginRenderPass(WGPUCommandEncoder encoder, WGPUTextureView view) {
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &color;
    return wgpuCommandEncoderBeginRenderPass(encoder, &desc);
}

Context makeContext(AllFeaturesMaxLimitsGpuTest& t, const std::string& encoderType) {
    Context ctx;
    ctx.encoderType = encoderType;
    ctx.commandEncoder = t.createCommandEncoderTracked();
    if (encoderType == "compute pass") {
        ctx.computePass = wgpuCommandEncoderBeginComputePass(ctx.commandEncoder, nullptr);
    } else {
        WGPUTextureView view = makeRenderView(t);
        ctx.renderPass = beginRenderPass(ctx.commandEncoder, view);
        if (encoderType == "render bundle") {
            WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
            WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            desc.colorFormatCount = 1;
            desc.colorFormats = &format;
            ctx.bundle = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
        }
    }
    return ctx;
}

WGPUCommandBuffer finishContext(AllFeaturesMaxLimitsGpuTest& t, Context& ctx) {
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderEnd(ctx.computePass);
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.renderPass);
    } else {
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(ctx.bundle, nullptr);
        wgpuRenderPassEncoderExecuteBundles(ctx.renderPass, 1, &bundle);
        if (bundle != nullptr) {
            wgpuRenderBundleRelease(bundle);
        }
        wgpuRenderPassEncoderEnd(ctx.renderPass);
    }
    return t.finishTracked(ctx.commandEncoder);
}

void validateFinish(AllFeaturesMaxLimitsGpuTest& t, Context& ctx, bool success) {
    if (!success) {
        t.expectValidationError([&] { finishContext(t, ctx); }, true);
        return;
    }
    WGPUCommandBuffer cb = finishContext(t, ctx);
    t.expectValidationError([&] { wgpuQueueSubmit(t.queue(), 1, &cb); }, false);
}

WGPUShaderStage stageForEncoder(const std::string& encoderType) {
    return encoderType == "compute pass" ? WGPUShaderStage_Compute : WGPUShaderStage_Fragment;
}

WGPUBindGroupLayoutEntry bufferEntry(uint32_t binding, WGPUShaderStage visibility, bool dynamic = false, WGPUBufferBindingType type = WGPUBufferBindingType_Uniform) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.buffer.type = type;
    entry.buffer.hasDynamicOffset = dynamic ? WGPU_TRUE : WGPU_FALSE;
    return entry;
}

WGPUBindGroupLayoutEntry samplerEntry(uint32_t binding, WGPUShaderStage visibility, WGPUSamplerBindingType type) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.sampler.type = type;
    return entry;
}

WGPUBindGroupLayoutEntry textureEntry(uint32_t binding, WGPUShaderStage visibility) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.texture.sampleType = WGPUTextureSampleType_Float;
    entry.texture.viewDimension = WGPUTextureViewDimension_2D;
    return entry;
}

WGPUBindGroupLayoutEntry storageTextureEntry(uint32_t binding, WGPUShaderStage visibility, WGPUStorageTextureAccess access) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.storageTexture.access = access;
    entry.storageTexture.format = WGPUTextureFormat_R32Float;
    entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
    return entry;
}

WGPUBindGroupLayout createLayout(AllFeaturesMaxLimitsGpuTest& t, const std::vector<WGPUBindGroupLayoutEntry>& entries) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.empty() ? nullptr : entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, const std::vector<WGPUBindGroupLayout>& layouts) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.empty() ? nullptr : layouts.data();
    return t.createPipelineLayoutTracked(desc);
}

WGPUBindGroup createBindGroup(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout layout, const std::vector<WGPUBindGroupEntry>& entries) {
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.empty() ? nullptr : entries.data();
    return t.createBindGroupTracked(desc);
}

WGPUBindGroupEntry bufferResource(AllFeaturesMaxLimitsGpuTest& t, uint32_t binding) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.buffer = makeBuffer(t, 16, WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage);
    entry.offset = 0;
    entry.size = 16;
    return entry;
}

WGPUBindGroupEntry samplerResource(AllFeaturesMaxLimitsGpuTest& t, uint32_t binding, bool comparison = false) {
    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    if (comparison) {
        desc.compare = WGPUCompareFunction_Always;
    }
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.sampler = t.createSamplerTracked(desc);
    return entry;
}

WGPUBindGroupEntry textureResource(AllFeaturesMaxLimitsGpuTest& t, uint32_t binding, WGPUTextureUsage usage = WGPUTextureUsage_TextureBinding) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_R32Float;
    desc.usage = usage;
    WGPUTexture texture = t.createTextureTracked(desc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.textureView = t.createViewTracked(texture, viewDesc);
    return entry;
}

WGPUBindGroupLayoutEntry resourceLayoutEntry(const std::string& encoderType, const std::string& resourceType, bool dynamic) {
    const WGPUShaderStage visibility = stageForEncoder(encoderType);
    if (resourceType == "uniformBuf") {
        return bufferEntry(0, visibility, dynamic, WGPUBufferBindingType_Uniform);
    }
    if (resourceType == "filtSamp") {
        return samplerEntry(0, visibility, WGPUSamplerBindingType_Filtering);
    }
    if (resourceType == "sampledTex") {
        return textureEntry(0, visibility);
    }
    if (resourceType == "readonlyStorageTex") {
        return storageTextureEntry(0, visibility, WGPUStorageTextureAccess_ReadOnly);
    }
    if (resourceType == "readwriteStorageTex") {
        return storageTextureEntry(0, visibility, WGPUStorageTextureAccess_ReadWrite);
    }
    return storageTextureEntry(0, visibility, WGPUStorageTextureAccess_WriteOnly);
}

WGPUBindGroupEntry resourceEntry(AllFeaturesMaxLimitsGpuTest& t, const std::string& resourceType) {
    if (resourceType == "uniformBuf") {
        return bufferResource(t, 0);
    }
    if (resourceType == "filtSamp") {
        return samplerResource(t, 0);
    }
    if (resourceType == "sampledTex") {
        return textureResource(t, 0, WGPUTextureUsage_TextureBinding);
    }
    return textureResource(t, 0, WGPUTextureUsage_StorageBinding);
}

WGPURenderPipeline createRenderPipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout) {
    constexpr std::string_view code =
        "@vertex fn vs_main() -> @builtin(position) vec4<f32> { return vec4<f32>(); }\n"
        "@fragment fn fs_main() {}";
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_None;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs_main");
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

WGPUComputePipeline createComputePipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout) {
    constexpr std::string_view code = "@compute @workgroup_size(1) fn main() {}";
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

WGPURenderPipeline createAutoRenderPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    constexpr std::string_view code =
        "@group(2) @binding(0) var<uniform> u2: vec4<f32>;\n"
        "@group(3) @binding(0) var<uniform> u3: vec4<f32>;\n"
        "@vertex fn vs_main() -> @builtin(position) vec4<f32> { return u2 + u3; }\n"
        "@fragment fn fs_main() {}";
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_None;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs_main");
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

WGPUComputePipeline createAutoComputePipeline(AllFeaturesMaxLimitsGpuTest& t) {
    constexpr std::string_view code =
        "@group(2) @binding(0) var<uniform> u2: vec4<f32>;\n"
        "@group(3) @binding(0) var<uniform> u3: vec4<f32>;\n"
        "@compute @workgroup_size(1) fn main() { _ = u2 + u3; }";
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

void setPipeline(Context& ctx, WGPUComputePipeline compute, WGPURenderPipeline render) {
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderSetPipeline(ctx.computePass, compute);
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetPipeline(ctx.renderPass, render);
    } else {
        wgpuRenderBundleEncoderSetPipeline(ctx.bundle, render);
    }
}

void setBindGroup(Context& ctx, uint32_t index, WGPUBindGroup bindGroup, const std::vector<uint32_t>& offsets) {
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderSetBindGroup(ctx.computePass, index, bindGroup, offsets.size(), offsets.empty() ? nullptr : offsets.data());
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetBindGroup(ctx.renderPass, index, bindGroup, offsets.size(), offsets.empty() ? nullptr : offsets.data());
    } else {
        wgpuRenderBundleEncoderSetBindGroup(ctx.bundle, index, bindGroup, offsets.size(), offsets.empty() ? nullptr : offsets.data());
    }
}

void doCall(AllFeaturesMaxLimitsGpuTest& t, Context& ctx, const std::string& call, bool zero) {
    const uint32_t count = zero ? 0u : 1u;
    if (call == "dispatch") {
        wgpuComputePassEncoderDispatchWorkgroups(ctx.computePass, count, 1, 1);
    } else if (call == "dispatchIndirect") {
        WGPUBuffer buffer = t.makeBufferWithContents(std::vector<uint32_t>{count, 1, 1}.data(), 3 * sizeof(uint32_t), WGPUBufferUsage_Indirect);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(ctx.computePass, buffer, 0);
    } else if (call == "draw") {
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderDraw(ctx.renderPass, count == 0 ? 0 : 3, 1, 0, 0);
        } else {
            wgpuRenderBundleEncoderDraw(ctx.bundle, count == 0 ? 0 : 3, 1, 0, 0);
        }
    } else if (call == "drawIndexed") {
        WGPUBuffer index = makeBuffer(t, 16, WGPUBufferUsage_Index);
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderSetIndexBuffer(ctx.renderPass, index, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexed(ctx.renderPass, count == 0 ? 0 : 3, 1, 0, 0, 0);
        } else {
            wgpuRenderBundleEncoderSetIndexBuffer(ctx.bundle, index, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            wgpuRenderBundleEncoderDrawIndexed(ctx.bundle, count == 0 ? 0 : 3, 1, 0, 0, 0);
        }
    } else if (call == "drawIndirect") {
        WGPUBuffer buffer = t.makeBufferWithContents(std::vector<uint32_t>{count == 0 ? 0u : 3u, 1, 0, 0}.data(), 4 * sizeof(uint32_t), WGPUBufferUsage_Indirect);
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderDrawIndirect(ctx.renderPass, buffer, 0);
        } else {
            wgpuRenderBundleEncoderDrawIndirect(ctx.bundle, buffer, 0);
        }
    } else if (call == "drawIndexedIndirect") {
        WGPUBuffer index = makeBuffer(t, 16, WGPUBufferUsage_Index);
        WGPUBuffer buffer = t.makeBufferWithContents(std::vector<uint32_t>{count == 0 ? 0u : 3u, 1, 0, 0, 0}.data(), 5 * sizeof(uint32_t), WGPUBufferUsage_Indirect);
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderSetIndexBuffer(ctx.renderPass, index, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderDrawIndexedIndirect(ctx.renderPass, buffer, 0);
        } else {
            wgpuRenderBundleEncoderSetIndexBuffer(ctx.bundle, index, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
            wgpuRenderBundleEncoderDrawIndexedIndirect(ctx.bundle, buffer, 0);
        }
    }
}

void runCompatibility(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType,
    WGPUComputePipeline computePipeline,
    WGPURenderPipeline renderPipeline,
    const std::vector<WGPUBindGroup>& groups,
    const std::vector<uint32_t>& offsets,
    const std::string& call,
    bool callWithZero,
    bool success) {
    Context noCall = makeContext(t, encoderType);
    setPipeline(noCall, computePipeline, renderPipeline);
    for (uint32_t i = 0; i < groups.size(); ++i) {
        if (groups[i] != nullptr) {
            setBindGroup(noCall, i, groups[i], offsets);
        }
    }
    validateFinish(t, noCall, true);

    Context withCall = makeContext(t, encoderType);
    setPipeline(withCall, computePipeline, renderPipeline);
    for (uint32_t i = 0; i < groups.size(); ++i) {
        if (groups[i] != nullptr) {
            setBindGroup(withCall, i, groups[i], offsets);
        }
    }
    doCall(t, withCall, call, callWithZero);
    validateFinish(t, withCall, success);
}

std::vector<Value> filteredCompatCallsForParams(const std::string& encoderType) {
    if (encoderType == "compute pass") {
        return {std::string("dispatch"), std::string("dispatchIndirect")};
    }
    return {std::string("draw"), std::string("drawIndexed"), std::string("drawIndirect"), std::string("drawIndexedIndirect")};
}

CTS_TEST(testGroup, "bind_groups_and_pipeline_layout_mismatch")
    .desc("Tests the bind groups must match the requirements of the pipeline layout.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
                .expand("call", [](const ParamRecord& p) {
                    return filteredCompatCallsForParams(valueAs<std::string>(*findParam(p, "encoderType")));
                })
                .combine("callWithZero", {true, false})
                .beginSubcases()
                .combineWithParams({
                    ParamRecord{{"setBindGroup0", true}, {"setBindGroup1", true}, {"setUnusedBindGroup2", true}, {"_success", true}},
                    ParamRecord{{"setBindGroup0", true}, {"setBindGroup1", true}, {"setUnusedBindGroup2", false}, {"_success", true}},
                    ParamRecord{{"setBindGroup0", true}, {"setBindGroup1", false}, {"setUnusedBindGroup2", true}, {"_success", false}},
                    ParamRecord{{"setBindGroup0", false}, {"setBindGroup1", true}, {"setUnusedBindGroup2", true}, {"_success", false}},
                    ParamRecord{{"setBindGroup0", false}, {"setBindGroup1", false}, {"setUnusedBindGroup2", false}, {"_success", false}},
                })
                .combine("useU32Array", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const bool dynamic = t.param<bool>("useU32Array");
        const WGPUShaderStage stage = stageForEncoder(encoderType);
        WGPUBindGroupLayout layout0 = createLayout(t, {bufferEntry(0, stage, dynamic)});
        WGPUBindGroupLayout layout1 = createLayout(t, {bufferEntry(0, stage, dynamic)});
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, {layout0, layout1});
        WGPUComputePipeline compute = createComputePipeline(t, pipelineLayout);
        WGPURenderPipeline render = createRenderPipeline(t, pipelineLayout);
        WGPUBindGroup group0 = t.param<bool>("setBindGroup0") ? createBindGroup(t, layout0, {bufferResource(t, 0)}) : nullptr;
        WGPUBindGroup group1 = t.param<bool>("setBindGroup1") ? createBindGroup(t, layout1, {bufferResource(t, 0)}) : nullptr;
        WGPUBindGroup group2 = t.param<bool>("setUnusedBindGroup2") ? createBindGroup(t, layout1, {bufferResource(t, 0)}) : nullptr;
        runCompatibility(t, encoderType, compute, render, {group0, group1, group2}, dynamic ? std::vector<uint32_t>{0} : std::vector<uint32_t>{}, t.param<std::string>("call"), t.param<bool>("callWithZero"), t.param<bool>("_success"));
    });

CTS_TEST(testGroup, "buffer_binding,render_pipeline")
    .desc("The GPUBufferBindingLayout bindings configure should be exactly same in PipelineLayout and bindgroup.")
    .params([](ParamsBuilder u) {
        return u.combine("type", {std::string("uniform"), std::string("storage"), std::string("read-only-storage")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string type = t.param<std::string>("type");
        WGPUBindGroupLayout bgLayout = createLayout(t, {bufferEntry(0, WGPUShaderStage_Fragment, false, WGPUBufferBindingType_Uniform)});
        WGPUBindGroup bindGroup = createBindGroup(t, bgLayout, {bufferResource(t, 0)});
        WGPUBufferBindingType plType = WGPUBufferBindingType_Uniform;
        if (type == "storage") {
            plType = WGPUBufferBindingType_Storage;
        } else if (type == "read-only-storage") {
            plType = WGPUBufferBindingType_ReadOnlyStorage;
        }
        WGPUBindGroupLayout plLayout = createLayout(t, {bufferEntry(0, WGPUShaderStage_Fragment, false, plType)});
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, {plLayout});
        WGPURenderPipeline render = createRenderPipeline(t, pipelineLayout);
        runCompatibility(t, "render pass", nullptr, render, {bindGroup}, {}, "draw", false, type == "uniform");
    });

CTS_TEST(testGroup, "sampler_binding,render_pipeline")
    .desc("The GPUSamplerBindingLayout bindings configure should be exactly same in PipelineLayout and bindgroup.")
    .params([](ParamsBuilder u) {
        return u.combine("bglType", {std::string("filtering"), std::string("non-filtering"), std::string("comparison")})
                .combine("bgType", {std::string("filtering"), std::string("non-filtering"), std::string("comparison")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        auto parse = [](const std::string& s) {
            if (s == "comparison") return WGPUSamplerBindingType_Comparison;
            if (s == "non-filtering") return WGPUSamplerBindingType_NonFiltering;
            return WGPUSamplerBindingType_Filtering;
        };
        const std::string bglType = t.param<std::string>("bglType");
        const std::string bgType = t.param<std::string>("bgType");
        WGPUBindGroupLayout bgLayout = createLayout(t, {samplerEntry(0, WGPUShaderStage_Fragment, parse(bgType))});
        WGPUBindGroup bindGroup = createBindGroup(t, bgLayout, {samplerResource(t, 0, bgType == "comparison")});
        WGPUBindGroupLayout plLayout = createLayout(t, {samplerEntry(0, WGPUShaderStage_Fragment, parse(bglType))});
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, {plLayout});
        WGPURenderPipeline render = createRenderPipeline(t, pipelineLayout);
        runCompatibility(t, "render pass", nullptr, render, {bindGroup}, {}, "draw", false, bglType == bgType);
    });

CTS_TEST(testGroup, "bgl_binding_mismatch")
    .desc("Tests the binding number must exist or not exist in both bindGroups[i].layout and pipelineLayout.bgls[i]")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
                .expand("call", [](const ParamRecord& p) { return filteredCompatCallsForParams(valueAs<std::string>(*findParam(p, "encoderType"))); })
                .combine("callWithZero", {true, false})
                .beginSubcases()
                .combine("caseIndex", {0, 1, 2, 3, 4, 5})
                .combine("useU32Array", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        static const std::vector<std::vector<uint32_t>> bgCases = {{0, 1, 2}, {0, 1, 2}, {0, 2}, {0, 2}, {0, 1, 2}, {0, 1}};
        static const std::vector<std::vector<uint32_t>> plCases = {{0, 1, 2}, {0, 1, 3}, {0, 2}, {2, 0}, {0, 1}, {0, 1, 2}};
        static const bool successCases[] = {true, false, true, true, false, false};
        const int caseIndex = t.param<int>("caseIndex");
        const std::string encoderType = t.param<std::string>("encoderType");
        const WGPUShaderStage stage = stageForEncoder(encoderType);
        const bool dynamic = t.param<bool>("useU32Array");
        std::vector<WGPUBindGroupLayoutEntry> bgEntries;
        std::vector<WGPUBindGroupEntry> resources;
        for (uint32_t binding : bgCases[caseIndex]) {
            bgEntries.push_back(bufferEntry(binding, stage, dynamic));
            resources.push_back(bufferResource(t, binding));
        }
        std::vector<WGPUBindGroupLayoutEntry> plEntries;
        for (uint32_t binding : plCases[caseIndex]) {
            plEntries.push_back(bufferEntry(binding, stage, dynamic));
        }
        WGPUBindGroupLayout bgLayout = createLayout(t, bgEntries);
        WGPUBindGroup bindGroup = createBindGroup(t, bgLayout, resources);
        WGPUBindGroupLayout plLayout = createLayout(t, plEntries);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, {plLayout});
        WGPUComputePipeline compute = createComputePipeline(t, pipelineLayout);
        WGPURenderPipeline render = createRenderPipeline(t, pipelineLayout);
        runCompatibility(t, encoderType, compute, render, {bindGroup}, dynamic ? std::vector<uint32_t>(bgCases[caseIndex].size(), 0u) : std::vector<uint32_t>{}, t.param<std::string>("call"), t.param<bool>("callWithZero"), successCases[caseIndex]);
    });

CTS_TEST(testGroup, "bgl_visibility_mismatch")
    .desc("Tests the visibility in bindGroups[i].layout and pipelineLayout.bgls[i] must be matched")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
                .expand("call", [](const ParamRecord& p) { return filteredCompatCallsForParams(valueAs<std::string>(*findParam(p, "encoderType"))); })
                .combine("callWithZero", {true, false})
                .beginSubcases()
                .combine("bgVisibility", {1, 2, 4, 3, 5, 6, 7})
                .combine("plVisibility", {1, 2, 4, 6})
                .combine("useU32Array", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const WGPUShaderStage bgVisibility = static_cast<WGPUShaderStage>(t.param<int>("bgVisibility"));
        const WGPUShaderStage plVisibility = static_cast<WGPUShaderStage>(t.param<int>("plVisibility"));
        const bool dynamic = t.param<bool>("useU32Array");
        WGPUBindGroupLayout bgLayout = createLayout(t, {bufferEntry(0, bgVisibility, dynamic)});
        WGPUBindGroup bindGroup = createBindGroup(t, bgLayout, {bufferResource(t, 0)});
        WGPUBindGroupLayout plLayout = createLayout(t, {bufferEntry(0, plVisibility, dynamic)});
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, {plLayout});
        WGPUComputePipeline compute = createComputePipeline(t, pipelineLayout);
        WGPURenderPipeline render = createRenderPipeline(t, pipelineLayout);
        runCompatibility(t, encoderType, compute, render, {bindGroup}, dynamic ? std::vector<uint32_t>{0} : std::vector<uint32_t>{}, t.param<std::string>("call"), t.param<bool>("callWithZero"), bgVisibility == plVisibility);
    });

CTS_TEST(testGroup, "bgl_resource_type_mismatch")
    .desc("Tests the binding resource type in bindGroups[i].layout and pipelineLayout.bgls[i] must be matched")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
                .expand("call", [](const ParamRecord& p) { return filteredCompatCallsForParams(valueAs<std::string>(*findParam(p, "encoderType"))); })
                .combine("callWithZero", {true, false})
                .beginSubcases()
                .combine("bgResourceType", {std::string("uniformBuf"), std::string("filtSamp"), std::string("sampledTex"), std::string("readonlyStorageTex"), std::string("writeonlyStorageTex"), std::string("readwriteStorageTex")})
                .combine("plResourceType", {std::string("uniformBuf"), std::string("filtSamp"), std::string("sampledTex"), std::string("readonlyStorageTex"), std::string("writeonlyStorageTex"), std::string("readwriteStorageTex")})
                .expand("useU32Array", [](const ParamRecord& p) {
                    const std::string bgType = valueAs<std::string>(*findParam(p, "bgResourceType"));
                    return bgType == "uniformBuf" ? std::vector<Value>{true, false} : std::vector<Value>{false};
                });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string bgType = t.param<std::string>("bgResourceType");
        const std::string plType = t.param<std::string>("plResourceType");
        WGPUBindGroupLayout bgLayout = createLayout(t, {resourceLayoutEntry(encoderType, bgType, t.param<bool>("useU32Array"))});
        WGPUBindGroup bindGroup = createBindGroup(t, bgLayout, {resourceEntry(t, bgType)});
        WGPUBindGroupLayout plLayout = createLayout(t, {resourceLayoutEntry(encoderType, plType, t.param<bool>("useU32Array"))});
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, {plLayout});
        WGPUComputePipeline compute = createComputePipeline(t, pipelineLayout);
        WGPURenderPipeline render = createRenderPipeline(t, pipelineLayout);
        runCompatibility(t, encoderType, compute, render, {bindGroup}, t.param<bool>("useU32Array") ? std::vector<uint32_t>{0} : std::vector<uint32_t>{}, t.param<std::string>("call"), t.param<bool>("callWithZero"), bgType == plType);
    });

CTS_TEST(testGroup, "empty_bind_group_layouts_never_requires_empty_bind_groups,compute_pass")
    .desc("Test that a compute pipeline with empty bind group layouts doesn't require empty bind groups to be set.")
    .params([](ParamsBuilder u) {
        return u.combine("emptyBindGroupLayoutType", {std::string("Null"), std::string("Undefined"), std::string("Empty")})
                .combine("bindGroupLayoutEntryCount", {3, 4})
                .combine("computeCommand", {std::string("dispatchIndirect"), std::string("dispatch")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBindGroupLayout emptyLayout = createLayout(t, {});
        std::vector<WGPUBindGroupLayout> layouts(4, emptyLayout);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, layouts);
        WGPUComputePipeline compute = createComputePipeline(t, pipelineLayout);
        WGPUBindGroup emptyGroup = createBindGroup(t, emptyLayout, {});
        std::vector<WGPUBindGroup> groups(static_cast<size_t>(t.param<int>("bindGroupLayoutEntryCount")), emptyGroup);
        runCompatibility(t, "compute pass", compute, nullptr, groups, {}, t.param<std::string>("computeCommand"), true, true);
    });

CTS_TEST(testGroup, "empty_bind_group_layouts_never_requires_empty_bind_groups,render_pass")
    .desc("Test that a render pipeline with empty bind groups layouts doesn't require empty bind groups to be set.")
    .params([](ParamsBuilder u) {
        return u.combine("emptyBindGroupLayoutType", {std::string("Null"), std::string("Undefined"), std::string("Empty")})
                .combine("bindGroupLayoutEntryCount", {3, 4})
                .combine("renderCommand", {std::string("draw"), std::string("drawIndexed"), std::string("drawIndirect"), std::string("drawIndexedIndirect")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBindGroupLayout emptyLayout = createLayout(t, {});
        std::vector<WGPUBindGroupLayout> layouts(4, emptyLayout);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, layouts);
        WGPURenderPipeline render = createRenderPipeline(t, pipelineLayout);
        WGPUBindGroup emptyGroup = createBindGroup(t, emptyLayout, {});
        std::vector<WGPUBindGroup> groups(static_cast<size_t>(t.param<int>("bindGroupLayoutEntryCount")), emptyGroup);
        runCompatibility(t, "render pass", nullptr, render, groups, {}, t.param<std::string>("renderCommand"), true, true);
    });

void runDefaultLayoutBindingTest(AllFeaturesMaxLimitsGpuTest& t, bool computeTest) {
    const bool empty = t.param<bool>("empty");
    const std::string pipelineType = t.param<std::string>("pipelineType");
    const std::string bindingType = t.param<std::string>("bindingType");
    const bool swap = t.param<bool>("swap");
    WGPUBindGroupLayout emptyExplicit = createLayout(t, {});
    WGPUBindGroupLayout nonEmptyExplicit = createLayout(t, {bufferEntry(0, computeTest ? WGPUShaderStage_Compute : WGPUShaderStage_Vertex)});
    WGPUPipelineLayout explicitLayout = createPipelineLayout(t, {emptyExplicit, emptyExplicit, nonEmptyExplicit, nonEmptyExplicit});
    WGPUComputePipeline autoCompute0 = computeTest ? createAutoComputePipeline(t) : nullptr;
    WGPUComputePipeline autoCompute1 = computeTest ? createAutoComputePipeline(t) : nullptr;
    WGPUComputePipeline explicitCompute = computeTest ? createComputePipeline(t, explicitLayout) : nullptr;
    WGPURenderPipeline autoRender0 = computeTest ? nullptr : createAutoRenderPipeline(t);
    WGPURenderPipeline autoRender1 = computeTest ? nullptr : createAutoRenderPipeline(t);
    WGPURenderPipeline explicitRender = computeTest ? nullptr : createRenderPipeline(t, explicitLayout);
    auto getLayout = [&](const std::string& source, uint32_t index) {
        if (source == "explicit") {
            return index < 2 ? emptyExplicit : nonEmptyExplicit;
        }
        if (computeTest) {
            return wgpuComputePipelineGetBindGroupLayout(source == "auto0" ? autoCompute0 : autoCompute1, index);
        }
        return wgpuRenderPipelineGetBindGroupLayout(source == "auto0" ? autoRender0 : autoRender1, index);
    };
    const std::string chosenPipeline = pipelineType == "auto0" ? "auto0" : "explicit";
    WGPUComputePipeline compute = pipelineType == "auto0" ? autoCompute0 : explicitCompute;
    WGPURenderPipeline render = pipelineType == "auto0" ? autoRender0 : explicitRender;
    std::vector<WGPUBindGroupLayout> emptyLayouts = {getLayout(empty ? bindingType : chosenPipeline, 0), getLayout(empty ? bindingType : chosenPipeline, 1)};
    std::vector<WGPUBindGroupLayout> nonEmptyLayouts = {getLayout(empty ? chosenPipeline : bindingType, 2), getLayout(empty ? chosenPipeline : bindingType, 3)};
    if (swap) {
        std::swap(emptyLayouts[0], emptyLayouts[1]);
        std::swap(nonEmptyLayouts[0], nonEmptyLayouts[1]);
    }
    std::vector<WGPUBindGroup> groups = {
        createBindGroup(t, emptyLayouts[0], {}),
        createBindGroup(t, emptyLayouts[1], {}),
        createBindGroup(t, nonEmptyLayouts[0], {bufferResource(t, 0)}),
        createBindGroup(t, nonEmptyLayouts[1], {bufferResource(t, 0)}),
    };
    const bool success = empty || t.param<bool>("_success");
    runCompatibility(t, computeTest ? "compute pass" : "render pass", compute, render, groups, {}, computeTest ? t.param<std::string>("computeCommand") : t.param<std::string>("renderCommand"), true, success);
}

CTS_TEST(testGroup, "pipeline_layouts,compute_pass")
    .desc("Test auto and explicit compute pipeline layouts with empty and non-empty bind groups.")
    .params([](ParamsBuilder u) {
        return u.combine("empty", {false, true})
                .combineWithParams({
                    ParamRecord{{"pipelineType", std::string("auto0")}, {"bindingType", std::string("auto0")}, {"swap", false}, {"_success", true}},
                    ParamRecord{{"pipelineType", std::string("explicit")}, {"bindingType", std::string("explicit")}, {"swap", false}, {"_success", true}},
                    ParamRecord{{"pipelineType", std::string("explicit")}, {"bindingType", std::string("auto0")}, {"swap", false}, {"_success", false}},
                    ParamRecord{{"pipelineType", std::string("auto0")}, {"bindingType", std::string("explicit")}, {"swap", false}, {"_success", false}},
                    ParamRecord{{"pipelineType", std::string("auto0")}, {"bindingType", std::string("auto1")}, {"swap", false}, {"_success", false}},
                    ParamRecord{{"pipelineType", std::string("auto0")}, {"bindingType", std::string("auto0")}, {"swap", true}, {"_success", true}},
                })
                .combine("computeCommand", {std::string("dispatchIndirect"), std::string("dispatch")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runDefaultLayoutBindingTest(t, true); });

CTS_TEST(testGroup, "pipeline_layouts,render_pass")
    .desc("Test auto and explicit render pipeline layouts with empty and non-empty bind groups.")
    .params([](ParamsBuilder u) {
        return u.combine("empty", {false, true})
                .combineWithParams({
                    ParamRecord{{"pipelineType", std::string("auto0")}, {"bindingType", std::string("auto0")}, {"swap", false}, {"_success", true}},
                    ParamRecord{{"pipelineType", std::string("explicit")}, {"bindingType", std::string("explicit")}, {"swap", false}, {"_success", true}},
                    ParamRecord{{"pipelineType", std::string("explicit")}, {"bindingType", std::string("auto0")}, {"swap", false}, {"_success", false}},
                    ParamRecord{{"pipelineType", std::string("auto0")}, {"bindingType", std::string("explicit")}, {"swap", false}, {"_success", false}},
                    ParamRecord{{"pipelineType", std::string("auto0")}, {"bindingType", std::string("auto1")}, {"swap", false}, {"_success", false}},
                    ParamRecord{{"pipelineType", std::string("auto0")}, {"bindingType", std::string("auto0")}, {"swap", true}, {"_success", true}},
                })
                .combine("renderCommand", {std::string("draw"), std::string("drawIndexed"), std::string("drawIndirect"), std::string("drawIndexedIndirect")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runDefaultLayoutBindingTest(t, false); });

} // namespace
