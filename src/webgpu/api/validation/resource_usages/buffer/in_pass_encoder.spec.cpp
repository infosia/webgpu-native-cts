// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/resource_usages/buffer/in_pass_encoder.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

constexpr uint64_t kBoundBufferSize = 256;

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,resource_usages,buffer,in_pass_encoder",
    R"(
Buffer Usages Validation Tests in Render Pass and Compute Pass.
)");

WGPUStringView sv(const char* text) {
    return WGPUStringView{text, WGPU_STRLEN};
}

template <typename T>
T paramValue(const ParamRecord& params, std::string_view key) {
    return valueAs<T>(*findParam(params, key));
}

std::vector<Value> allBufferUsages() {
    return {
        std::string("uniform"),
        std::string("storage"),
        std::string("read-only-storage"),
        std::string("vertex"),
        std::string("index"),
        std::string("indirect"),
        std::string("indexedIndirect"),
    };
}

bool isBufferUsageInBindGroup(const std::string& usage) {
    if (usage == "uniform" || usage == "storage" || usage == "read-only-storage") return true;
    if (usage == "vertex" || usage == "index" || usage == "indirect" || usage == "indexedIndirect") {
        return false;
    }
    std::abort();
}

WGPUShaderStage visibilityFor(const std::string& visibility) {
    if (visibility == "compute") return WGPUShaderStage_Compute;
    if (visibility == "fragment") return WGPUShaderStage_Fragment;
    std::abort();
}

WGPUBufferBindingType bufferBindingTypeFor(const std::string& usage) {
    if (usage == "uniform") return WGPUBufferBindingType_Uniform;
    if (usage == "storage") return WGPUBufferBindingType_Storage;
    if (usage == "read-only-storage") return WGPUBufferBindingType_ReadOnlyStorage;
    std::abort();
}

void skipIfStorageBuffersUsedAndNotAvailableInStages(AllFeaturesMaxLimitsGpuTest& t,
                                                     const std::string& usage,
                                                     const std::string&,
                                                     uint32_t) {
    (void)t;
    if (usage == "storage" || usage == "read-only-storage") {
        // Upstream only applies the maxStorageBuffersIn*Stage guard in compatibility mode.
        // This harness does not expose t.isCompatibility, so no native feature gate is needed here.
    }
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUBindGroupLayout createBindGroupLayoutForTest(AllFeaturesMaxLimitsGpuTest& t,
                                                 const std::string& type,
                                                 const std::string& resourceVisibility) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = visibilityFor(resourceVisibility);
    entry.buffer.type = bufferBindingTypeFor(type);

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBindGroup createBindGroupForTest(AllFeaturesMaxLimitsGpuTest& t,
                                     WGPUBuffer buffer,
                                     uint64_t offset,
                                     const std::string& type,
                                     const std::string& resourceVisibility) {
    WGPUBindGroupLayout layout = createBindGroupLayoutForTest(t, type, resourceVisibility);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer;
    entry.offset = offset;
    entry.size = kBoundBufferSize;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t,
                                        const std::vector<WGPUBindGroupLayout>& layouts) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.empty() ? nullptr : layouts.data();
    return t.createPipelineLayoutTracked(desc);
}

WGPUComputePipeline createNoOpComputePipeline(AllFeaturesMaxLimitsGpuTest& t,
                                              WGPUPipelineLayout layout = nullptr) {
    WGPUShaderModule module = t.createShaderModuleTracked(
        "@compute @workgroup_size(1) fn main() {}");
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

WGPURenderPassEncoder beginSimpleRenderPass(AllFeaturesMaxLimitsGpuTest& t,
                                            WGPUCommandEncoder encoder) {
    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.size = WGPUExtent3D{16, 16, 1};
    textureDesc.mipLevelCount = 1;
    textureDesc.sampleCount = 1;
    textureDesc.dimension = WGPUTextureDimension_2D;
    textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
    textureDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(textureDesc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Load;
    color.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;
    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
}

std::string vertexShaderFor(uint32_t vertexBufferCount) {
    std::string code;
    for (uint32_t i = 0; i < vertexBufferCount; ++i) {
        code += "@location(" + std::to_string(i) + ") input" + std::to_string(i) + ": f32, ";
    }
    return "@vertex fn main(" + code + ") -> @builtin(position) vec4<f32> { return vec4<f32>(); }";
}

WGPURenderPipeline createRenderPipelineForTest(AllFeaturesMaxLimitsGpuTest& t,
                                               WGPUPipelineLayout pipelineLayout,
                                               uint32_t vertexBufferCount) {
    std::vector<WGPUVertexAttribute> attributes(vertexBufferCount);
    std::vector<WGPUVertexBufferLayout> vertexBuffers(vertexBufferCount);
    for (uint32_t i = 0; i < vertexBufferCount; ++i) {
        attributes[i] = WGPU_VERTEX_ATTRIBUTE_INIT;
        attributes[i].format = WGPUVertexFormat_Float32;
        attributes[i].shaderLocation = i;
        attributes[i].offset = 0;

        vertexBuffers[i] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vertexBuffers[i].arrayStride = 4;
        vertexBuffers[i].attributeCount = 1;
        vertexBuffers[i].attributes = &attributes[i];
    }

    const std::string vertexCode = vertexShaderFor(vertexBufferCount);
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(std::string_view(vertexCode));
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(
        "@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(0.0, 0.0, 0.0, 1.0); }");

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = sv("main");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = pipelineLayout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = sv("main");
    desc.vertex.bufferCount = vertexBuffers.size();
    desc.vertex.buffers = vertexBuffers.empty() ? nullptr : vertexBuffers.data();
    desc.fragment = &fragment;
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    return t.createRenderPipelineTracked(desc);
}

void useBufferOnComputePassEncoder(AllFeaturesMaxLimitsGpuTest& t,
                                   WGPUComputePassEncoder pass,
                                   WGPUBuffer buffer,
                                   const std::string& usage,
                                   uint64_t offset) {
    if (usage == "uniform" || usage == "storage" || usage == "read-only-storage") {
        WGPUBindGroup bindGroup = createBindGroupForTest(t, buffer, offset, usage, "compute");
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        WGPUBindGroupLayout bindGroupLayout = createBindGroupLayoutForTest(t, usage, "compute");
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, {bindGroupLayout});
        WGPUComputePipeline pipeline = createNoOpComputePipeline(t, pipelineLayout);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    } else if (usage == "indirect") {
        WGPUComputePipeline pipeline = createNoOpComputePipeline(t);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, buffer, offset);
    } else {
        std::abort();
    }
}

void useBufferOnRenderPassEncoderNoDraw(AllFeaturesMaxLimitsGpuTest& t,
                                        WGPURenderPassEncoder pass,
                                        WGPUBuffer buffer,
                                        uint64_t offset,
                                        const std::string& usage,
                                        const std::string& bindGroupVisibility) {
    if (usage == "uniform" || usage == "storage" || usage == "read-only-storage") {
        WGPUBindGroup bindGroup = createBindGroupForTest(t, buffer, offset, usage, bindGroupVisibility);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    } else if (usage == "vertex") {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, buffer, offset, kBoundBufferSize);
    } else if (usage == "index") {
        wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, WGPUIndexFormat_Uint16, offset, kBoundBufferSize);
    } else {
        std::abort();
    }
}

void makeDrawCallWithOneUsage(AllFeaturesMaxLimitsGpuTest& t,
                              WGPURenderPassEncoder pass,
                              WGPUBuffer buffer,
                              const std::string& usage,
                              uint64_t offset) {
    if (usage == "uniform" || usage == "read-only-storage" || usage == "storage" || usage == "vertex") {
        wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
    } else if (usage == "index") {
        wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);
    } else if (usage == "indirect") {
        wgpuRenderPassEncoderDrawIndirect(pass, buffer, offset);
    } else if (usage == "indexedIndirect") {
        WGPUBuffer indexBuffer = createBuffer(t, 4, WGPUBufferUsage_Index);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexedIndirect(pass, buffer, offset);
    } else {
        std::abort();
    }
}

void useBufferOnRenderPassEncoderInDrawCall(AllFeaturesMaxLimitsGpuTest& t,
                                            WGPURenderPassEncoder pass,
                                            WGPUBuffer buffer,
                                            uint64_t offset,
                                            const std::string& usage) {
    if (usage == "uniform" || usage == "storage" || usage == "read-only-storage") {
        WGPUBindGroupLayout bindGroupLayout = createBindGroupLayoutForTest(t, usage, "fragment");
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, {bindGroupLayout});
        WGPURenderPipeline pipeline = createRenderPipelineForTest(t, pipelineLayout, 0);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        WGPUBindGroup bindGroup = createBindGroupForTest(t, buffer, offset, usage, "fragment");
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
    } else if (usage == "vertex") {
        WGPURenderPipeline pipeline = createRenderPipelineForTest(t, nullptr, 1);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, buffer, offset, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
    } else if (usage == "index") {
        WGPURenderPipeline pipeline = createRenderPipelineForTest(t, nullptr, 0);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, WGPUIndexFormat_Uint16, offset, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);
    } else if (usage == "indirect") {
        WGPURenderPipeline pipeline = createRenderPipelineForTest(t, nullptr, 0);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDrawIndirect(pass, buffer, offset);
    } else if (usage == "indexedIndirect") {
        WGPURenderPipeline pipeline = createRenderPipelineForTest(t, nullptr, 0);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        WGPUBuffer indexBuffer = createBuffer(t, 4, WGPUBufferUsage_Index);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexedIndirect(pass, buffer, offset);
    } else {
        std::abort();
    }
}

CTS_TEST(testGroup, "subresources,buffer_usage_in_one_compute_pass_with_no_dispatch")
    .desc(R"(
Test that it is always allowed to set multiple bind groups with same buffer in a compute pass
encoder without any dispatch calls as state-setting compute pass commands, like setBindGroup(index,
bindGroup, dynamicOffsets), do not contribute directly to a usage scope.)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", {std::string("uniform"), std::string("storage"), std::string("read-only-storage")})
            .combine("usage1", {std::string("uniform"), std::string("storage"), std::string("read-only-storage")})
            .beginSubcases()
            .combine("visibility0", {std::string("compute"), std::string("fragment")})
            .combine("visibility1", {std::string("compute"), std::string("fragment")})
            .combine("hasOverlap", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string usage1 = t.param<std::string>("usage1");
        const std::string visibility0 = t.param<std::string>("visibility0");
        const std::string visibility1 = t.param<std::string>("visibility1");
        const bool hasOverlap = t.param<bool>("hasOverlap");
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage0, visibility0, 1);
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage1, visibility1, 1);

        WGPUBuffer buffer = createBuffer(t, kBoundBufferSize * 2, WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage);

        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            WGPUBindGroup bindGroup0 = createBindGroupForTest(t, buffer, 0, usage0, visibility0);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup0, 0, nullptr);
            const uint64_t offset1 = hasOverlap ? 0 : kBoundBufferSize;
            WGPUBindGroup bindGroup1 = createBindGroupForTest(t, buffer, offset1, usage1, visibility1);
            wgpuComputePassEncoderSetBindGroup(pass, 1, bindGroup1, 0, nullptr);
            wgpuComputePassEncoderEnd(pass);
            t.finishTracked(encoder);
        }, false);
    });

CTS_TEST(testGroup, "subresources,buffer_usage_in_one_compute_pass_with_one_dispatch")
    .desc(R"(
Test that when one buffer is used in one compute pass encoder, its list of internal usages within
one usage scope can only be a compatible usage list. According to WebGPU SPEC, within one dispatch,
for each bind group slot that is used by the current GPUComputePipeline's layout, every subresource
referenced by that bind group is "used" in the usage scope.

For both usage === storage, there is writable buffer binding aliasing so we skip this case and will
have tests covered (https://github.com/gpuweb/cts/issues/2232)
)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0AccessibleInDispatch", {true, false})
            .combine("usage1AccessibleInDispatch", {true, false})
            .combine("dispatchBeforeUsage1", {true, false})
            .beginSubcases()
            .combine("usage0", {std::string("uniform"), std::string("storage"), std::string("read-only-storage"), std::string("indirect")})
            .combine("visibility0", {std::string("compute"), std::string("fragment")})
            .filter([](const ParamRecord& params) {
                const std::string usage0 = paramValue<std::string>(params, "usage0");
                const std::string visibility0 = paramValue<std::string>(params, "visibility0");
                const bool usage0Accessible = paramValue<bool>(params, "usage0AccessibleInDispatch");
                const bool dispatchBefore = paramValue<bool>(params, "dispatchBeforeUsage1");
                if (usage0 == "indirect" && (!usage0Accessible || visibility0 != "compute" || !dispatchBefore)) return false;
                if (usage0Accessible && visibility0 != "compute") return false;
                if (dispatchBefore && paramValue<bool>(params, "usage1AccessibleInDispatch")) return false;
                return true;
            })
            .combine("usage1", {std::string("uniform"), std::string("storage"), std::string("read-only-storage"), std::string("indirect")})
            .combine("visibility1", {std::string("compute"), std::string("fragment")})
            .filter([](const ParamRecord& params) {
                const std::string usage0 = paramValue<std::string>(params, "usage0");
                const std::string usage1 = paramValue<std::string>(params, "usage1");
                const std::string visibility1 = paramValue<std::string>(params, "visibility1");
                const bool usage1Accessible = paramValue<bool>(params, "usage1AccessibleInDispatch");
                const bool dispatchBefore = paramValue<bool>(params, "dispatchBeforeUsage1");
                if (usage1 == "indirect" && (!usage1Accessible || visibility1 != "compute" || dispatchBefore)) return false;
                if (usage1Accessible && (visibility1 != "compute" || usage0 == "indirect")) return false;
                if (usage0 == "storage" && usage1 == "storage") return false;
                return true;
            })
            .combine("hasOverlap", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool usage0Accessible = t.param<bool>("usage0AccessibleInDispatch");
        const bool usage1Accessible = t.param<bool>("usage1AccessibleInDispatch");
        const bool dispatchBefore = t.param<bool>("dispatchBeforeUsage1");
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string visibility0 = t.param<std::string>("visibility0");
        const std::string usage1 = t.param<std::string>("usage1");
        const std::string visibility1 = t.param<std::string>("visibility1");
        const bool hasOverlap = t.param<bool>("hasOverlap");
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage0, visibility0, 1);
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage1, visibility1, 1);

        WGPUBuffer buffer = createBuffer(t, kBoundBufferSize * 2,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect);
        const bool usageHasConflict = (usage0 == "storage" && usage1 != "storage") ||
                                      (usage0 != "storage" && usage1 == "storage");
        const bool fail = usageHasConflict && visibility0 == "compute" && visibility1 == "compute" &&
                          usage0Accessible && usage1Accessible;

        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            if (usage0 == "indirect") {
                WGPUComputePipeline pipeline = createNoOpComputePipeline(t);
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, buffer, 0);
            } else {
                WGPUBindGroup bindGroup0 = createBindGroupForTest(t, buffer, 0, usage0, visibility0);
                wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup0, 0, nullptr);
                if (dispatchBefore) {
                    WGPUPipelineLayout layout = nullptr;
                    if (usage0Accessible) {
                        WGPUBindGroupLayout bindGroupLayout0 = createBindGroupLayoutForTest(t, usage0, visibility0);
                        layout = createPipelineLayout(t, {bindGroupLayout0});
                    }
                    WGPUComputePipeline pipeline = createNoOpComputePipeline(t, layout);
                    wgpuComputePassEncoderSetPipeline(pass, pipeline);
                    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
                }
            }

            const uint64_t offset1 = hasOverlap ? 0 : kBoundBufferSize;
            if (usage1 == "indirect") {
                WGPUPipelineLayout layout = nullptr;
                if (usage0Accessible) {
                    WGPUBindGroupLayout bindGroupLayout0 = createBindGroupLayoutForTest(t, usage0, visibility0);
                    layout = createPipelineLayout(t, {bindGroupLayout0});
                }
                WGPUComputePipeline pipeline = createNoOpComputePipeline(t, layout);
                wgpuComputePassEncoderSetPipeline(pass, pipeline);
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, buffer, offset1);
            } else {
                WGPUBindGroup bindGroup1 = createBindGroupForTest(t, buffer, offset1, usage1, visibility1);
                const uint32_t bindGroupIndex = usage0Accessible ? 1u : 0u;
                wgpuComputePassEncoderSetBindGroup(pass, bindGroupIndex, bindGroup1, 0, nullptr);
                if (!dispatchBefore) {
                    std::vector<WGPUBindGroupLayout> layouts;
                    if (usage0Accessible && usage0 != "indirect") {
                        layouts.push_back(createBindGroupLayoutForTest(t, usage0, visibility0));
                    }
                    if (usage1Accessible) {
                        layouts.push_back(createBindGroupLayoutForTest(t, usage1, visibility1));
                    }
                    WGPUPipelineLayout layout = createPipelineLayout(t, layouts);
                    WGPUComputePipeline pipeline = createNoOpComputePipeline(t, layout);
                    wgpuComputePassEncoderSetPipeline(pass, pipeline);
                    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
                }
            }
            wgpuComputePassEncoderEnd(pass);
            t.finishTracked(encoder);
        }, fail);
    });

CTS_TEST(testGroup, "subresources,buffer_usage_in_compute_pass_with_two_dispatches")
    .desc(R"(
Test that it is always allowed to use one buffer in different dispatch calls as in WebGPU SPEC,
within one dispatch, for each bind group slot that is used by the current GPUComputePipeline's
layout, every subresource referenced by that bind group is "used" in the usage scope, and different
dispatch calls refer to different usage scopes.)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", {std::string("uniform"), std::string("storage"), std::string("read-only-storage"), std::string("indirect")})
            .combine("usage1", {std::string("uniform"), std::string("storage"), std::string("read-only-storage"), std::string("indirect")})
            .beginSubcases()
            .combine("inSamePass", {true, false})
            .combine("hasOverlap", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string usage1 = t.param<std::string>("usage1");
        const bool inSamePass = t.param<bool>("inSamePass");
        const bool hasOverlap = t.param<bool>("hasOverlap");
        WGPUBuffer buffer = createBuffer(t, kBoundBufferSize * 2,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect);

        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            useBufferOnComputePassEncoder(t, pass, buffer, usage0, 0);
            const uint64_t offset1 = hasOverlap ? 0 : kBoundBufferSize;
            if (inSamePass) {
                useBufferOnComputePassEncoder(t, pass, buffer, usage1, offset1);
                wgpuComputePassEncoderEnd(pass);
            } else {
                wgpuComputePassEncoderEnd(pass);
                WGPUComputePassEncoder anotherPass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
                useBufferOnComputePassEncoder(t, anotherPass, buffer, usage1, offset1);
                wgpuComputePassEncoderEnd(anotherPass);
            }
            t.finishTracked(encoder);
        }, false);
    });

CTS_TEST(testGroup, "subresources,buffer_usage_in_one_render_pass_with_no_draw")
    .desc(R"(
Test that when one buffer is used in one render pass encoder, its list of internal usages within one
usage scope (all the commands in the whole render pass) can only be a compatible usage list even if
there is no draw call in the render pass.
    )")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", {std::string("uniform"), std::string("storage"), std::string("read-only-storage"), std::string("vertex"), std::string("index")})
            .combine("usage1", {std::string("uniform"), std::string("storage"), std::string("read-only-storage"), std::string("vertex"), std::string("index")})
            .beginSubcases()
            .combine("hasOverlap", {true, false})
            .combine("visibility0", {std::string("compute"), std::string("fragment")})
            .filter([](const ParamRecord& params) {
                return !(paramValue<std::string>(params, "visibility0") == "compute" &&
                         !isBufferUsageInBindGroup(paramValue<std::string>(params, "usage0")));
            })
            .combine("visibility1", {std::string("compute"), std::string("fragment")})
            .filter([](const ParamRecord& params) {
                return !(paramValue<std::string>(params, "visibility1") == "compute" &&
                         !isBufferUsageInBindGroup(paramValue<std::string>(params, "usage1")));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string usage1 = t.param<std::string>("usage1");
        const bool hasOverlap = t.param<bool>("hasOverlap");
        const std::string visibility0 = t.param<std::string>("visibility0");
        const std::string visibility1 = t.param<std::string>("visibility1");
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage0, visibility0, 1);
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage1, visibility1, 1);

        WGPUBuffer buffer = createBuffer(t, kBoundBufferSize * 2,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex | WGPUBufferUsage_Index);
        const bool fail = (usage0 == "storage") != (usage1 == "storage");

        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
            useBufferOnRenderPassEncoderNoDraw(t, pass, buffer, 0, usage0, visibility0);
            const uint64_t offset1 = hasOverlap ? 0 : kBoundBufferSize;
            useBufferOnRenderPassEncoderNoDraw(t, pass, buffer, offset1, usage1, visibility1);
            wgpuRenderPassEncoderEnd(pass);
            t.finishTracked(encoder);
        }, fail);
    });

CTS_TEST(testGroup, "subresources,buffer_usage_in_one_render_pass_with_one_draw")
    .desc(R"(
Test that when one buffer is used in one render pass encoder where there is one draw call, its list
of internal usages within one usage scope (all the commands in the whole render pass) can only be a
compatible usage list. The usage scope rules are not related to the buffer offset or the bind group
layout visibilities.

For both usage === storage, there is writable buffer binding aliasing so we skip this case and will
have tests covered (https://github.com/gpuweb/cts/issues/2232)
)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", allBufferUsages())
            .combine("usage1", allBufferUsages())
            .beginSubcases()
            .combine("usage0AccessibleInDraw", {true, false})
            .combine("usage1AccessibleInDraw", {true, false})
            .combine("drawBeforeUsage1", {true, false})
            .combine("visibility0", {std::string("compute"), std::string("fragment")})
            .filter([](const ParamRecord& params) {
                const std::string usage0 = paramValue<std::string>(params, "usage0");
                const std::string usage1 = paramValue<std::string>(params, "usage1");
                const std::string visibility0 = paramValue<std::string>(params, "visibility0");
                const bool usage0Accessible = paramValue<bool>(params, "usage0AccessibleInDraw");
                const bool usage1Accessible = paramValue<bool>(params, "usage1AccessibleInDraw");
                const bool drawBefore = paramValue<bool>(params, "drawBeforeUsage1");
                if ((usage0 == "indirect" || usage0 == "indexedIndirect") &&
                    (!usage0Accessible || visibility0 != "fragment" || !drawBefore)) {
                    return false;
                }
                if ((usage0 == "vertex" || usage0 == "index") && visibility0 != "fragment") return false;
                if (usage0Accessible && visibility0 != "fragment") return false;
                if (drawBefore && usage1Accessible) return false;
                if (usage0 == "storage" && usage1 == "storage") return false;
                return true;
            })
            .combine("visibility1", {std::string("compute"), std::string("fragment")})
            .filter([](const ParamRecord& params) {
                const std::string usage0 = paramValue<std::string>(params, "usage0");
                const std::string usage1 = paramValue<std::string>(params, "usage1");
                const std::string visibility1 = paramValue<std::string>(params, "visibility1");
                const bool usage0Accessible = paramValue<bool>(params, "usage0AccessibleInDraw");
                const bool usage1Accessible = paramValue<bool>(params, "usage1AccessibleInDraw");
                const bool drawBefore = paramValue<bool>(params, "drawBeforeUsage1");
                if ((usage1 == "indirect" || usage1 == "indexedIndirect") &&
                    (!usage1Accessible || visibility1 != "fragment" || drawBefore)) {
                    return false;
                }
                if ((usage1 == "vertex" || usage1 == "index") && visibility1 != "fragment") return false;
                if (usage1Accessible &&
                    (visibility1 != "fragment" || usage0 == "indirect" || usage0 == "indexedIndirect")) {
                    return false;
                }
                if (usage0 == "index" && usage0Accessible && usage1 == "indirect") return false;
                return true;
            })
            .combine("hasOverlap", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool usage0Accessible = t.param<bool>("usage0AccessibleInDraw");
        const bool usage1Accessible = t.param<bool>("usage1AccessibleInDraw");
        const bool drawBefore = t.param<bool>("drawBeforeUsage1");
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string visibility0 = t.param<std::string>("visibility0");
        const std::string usage1 = t.param<std::string>("usage1");
        const std::string visibility1 = t.param<std::string>("visibility1");
        const bool hasOverlap = t.param<bool>("hasOverlap");
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage0, visibility0, 1);
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage1, visibility1, 1);

        WGPUBuffer buffer = createBuffer(t, kBoundBufferSize * 2,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex |
            WGPUBufferUsage_Index | WGPUBufferUsage_Indirect);
        const bool fail = (usage0 == "storage") != (usage1 == "storage");

        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
            const uint64_t offset0 = 0;
            const uint32_t bufferIndex0 = visibility0 == "fragment" ? 0u : 1u;
            std::vector<WGPUBindGroupLayout> usedBindGroupLayouts;

            auto useBufferOnRenderPassEncoder = [&](bool bufferAccessibleInDraw,
                                                    uint32_t bufferIndex,
                                                    uint64_t offset,
                                                    const std::string& usage,
                                                    const std::string& bindGroupVisibility) {
                if (usage == "uniform" || usage == "storage" || usage == "read-only-storage") {
                    WGPUBindGroup bindGroup = createBindGroupForTest(t, buffer, offset, usage, bindGroupVisibility);
                    wgpuRenderPassEncoderSetBindGroup(pass, bufferIndex, bindGroup, 0, nullptr);
                    if (bufferAccessibleInDraw && bindGroupVisibility == "fragment") {
                        usedBindGroupLayouts.push_back(createBindGroupLayoutForTest(t, usage, bindGroupVisibility));
                    }
                } else if (usage == "vertex") {
                    wgpuRenderPassEncoderSetVertexBuffer(pass, bufferIndex, buffer, offset, WGPU_WHOLE_SIZE);
                } else if (usage == "index") {
                    wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, WGPUIndexFormat_Uint16, offset, WGPU_WHOLE_SIZE);
                }
            };

            useBufferOnRenderPassEncoder(usage0Accessible, bufferIndex0, offset0, usage0, visibility0);
            uint32_t vertexBufferCount = 0;

            if (drawBefore) {
                WGPUPipelineLayout layout = createPipelineLayout(t, usedBindGroupLayouts);
                if (usage0 == "vertex" && usage0Accessible) ++vertexBufferCount;
                WGPURenderPipeline pipeline = createRenderPipelineForTest(t, layout, vertexBufferCount);
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                if (!usage0Accessible) {
                    wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
                } else {
                    makeDrawCallWithOneUsage(t, pass, buffer, usage0, offset0);
                }
            }

            const uint64_t offset1 = hasOverlap ? offset0 : kBoundBufferSize;
            uint32_t bufferIndex1 = 0;
            if (visibility1 != "fragment") {
                bufferIndex1 = 1;
            } else if (visibility0 == "fragment" && usage0Accessible) {
                if (isBufferUsageInBindGroup(usage0) && isBufferUsageInBindGroup(usage1)) {
                    bufferIndex1 = 1;
                } else if (usage0 == "vertex" && usage1 == "vertex") {
                    bufferIndex1 = 1;
                }
            }

            useBufferOnRenderPassEncoder(usage1Accessible, bufferIndex1, offset1, usage1, visibility1);

            if (!drawBefore) {
                WGPUPipelineLayout layout = createPipelineLayout(t, usedBindGroupLayouts);
                if (usage1 == "vertex" && usage1Accessible) ++vertexBufferCount;
                WGPURenderPipeline pipeline = createRenderPipelineForTest(t, layout, vertexBufferCount);
                wgpuRenderPassEncoderSetPipeline(pass, pipeline);
                if (!usage0Accessible && !usage1Accessible) {
                    wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
                } else if (usage0Accessible && !usage1Accessible) {
                    makeDrawCallWithOneUsage(t, pass, buffer, usage0, offset0);
                } else if (!usage0Accessible && usage1Accessible) {
                    makeDrawCallWithOneUsage(t, pass, buffer, usage1, offset1);
                } else if (usage1 == "indexedIndirect") {
                    if (usage0 != "index") {
                        WGPUBuffer indexBuffer = createBuffer(t, 4, WGPUBufferUsage_Index);
                        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
                    }
                    wgpuRenderPassEncoderDrawIndexedIndirect(pass, buffer, offset1);
                } else if (usage1 == "indirect") {
                    wgpuRenderPassEncoderDrawIndirect(pass, buffer, offset1);
                } else if (usage0 == "index" || usage1 == "index") {
                    wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);
                } else {
                    wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
                }
            }
            wgpuRenderPassEncoderEnd(pass);
            t.finishTracked(encoder);
        }, fail);
    });

CTS_TEST(testGroup, "subresources,buffer_usage_in_one_render_pass_with_two_draws")
    .desc(R"(
Test that when one buffer is used in different draw calls in one render pass, its list of internal
usages within one usage scope (all the commands in the whole render pass) can only be a compatible
usage list, and the usage scope rules are not related to the buffer offset, while the draw calls in
different render pass encoders belong to different usage scopes.)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", allBufferUsages())
            .combine("usage1", allBufferUsages())
            .beginSubcases()
            .combine("inSamePass", {true, false})
            .combine("hasOverlap", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string usage1 = t.param<std::string>("usage1");
        const bool inSamePass = t.param<bool>("inSamePass");
        const bool hasOverlap = t.param<bool>("hasOverlap");
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage0, "fragment", 1);
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage1, "fragment", 1);

        WGPUBuffer buffer = createBuffer(t, kBoundBufferSize * 2,
            WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_Vertex |
            WGPUBufferUsage_Index | WGPUBufferUsage_Indirect);
        const bool fail = inSamePass && ((usage0 == "storage") != (usage1 == "storage"));

        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
            useBufferOnRenderPassEncoderInDrawCall(t, pass, buffer, 0, usage0);
            const uint64_t offset1 = hasOverlap ? 0 : kBoundBufferSize;
            if (inSamePass) {
                useBufferOnRenderPassEncoderInDrawCall(t, pass, buffer, offset1, usage1);
                wgpuRenderPassEncoderEnd(pass);
            } else {
                wgpuRenderPassEncoderEnd(pass);
                WGPURenderPassEncoder anotherPass = beginSimpleRenderPass(t, encoder);
                useBufferOnRenderPassEncoderInDrawCall(t, anotherPass, buffer, offset1, usage1);
                wgpuRenderPassEncoderEnd(anotherPass);
            }
            t.finishTracked(encoder);
        }, fail);
    });

} // namespace
