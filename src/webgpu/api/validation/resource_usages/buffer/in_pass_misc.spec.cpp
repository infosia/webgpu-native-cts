// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/resource_usages/buffer/in_pass_misc.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

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
constexpr uint64_t kBufferSize = 256;

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,resource_usages,buffer,in_pass_misc",
    R"(
Test other buffer usage validation rules that are not tests in ./in_pass_encoder.spec.js.
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

std::vector<Value> allCopyAndPassUsages() {
    std::vector<Value> values = {std::string("copy-src"), std::string("copy-dst")};
    std::vector<Value> passValues = allBufferUsages();
    values.insert(values.end(), passValues.begin(), passValues.end());
    return values;
}

bool isCopyUsage(const std::string& usage) {
    return usage == "copy-src" || usage == "copy-dst";
}

bool isValidComputeUsage(const std::string& usage) {
    return usage != "vertex" && usage != "index" && usage != "indexedIndirect";
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
    if (usage == "storage" || usage == "read-only-storage") {
        (void)t;
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

WGPURenderPipeline createNoOpRenderPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    return createRenderPipelineForTest(t, nullptr, 0);
}

CTS_TEST(testGroup, "subresources,reset_buffer_usage_before_dispatch")
    .desc(R"(
Test that the buffer usages which are reset by another state-setting commands before a dispatch call
do not contribute directly to any usage scope in a compute pass.)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", {std::string("uniform"), std::string("storage"), std::string("read-only-storage")})
            .combine("usage1", {std::string("uniform"), std::string("storage"), std::string("read-only-storage"), std::string("indirect")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string usage1 = t.param<std::string>("usage1");
        const WGPUBufferUsage usages = WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect;
        WGPUBuffer buffer = createBuffer(t, kBufferSize, usages);
        WGPUBuffer anotherBuffer = createBuffer(t, kBufferSize, usages);

        t.expectValidationError([&] {
            std::vector<WGPUBindGroupLayout> bindGroupLayouts;
            bindGroupLayouts.push_back(createBindGroupLayoutForTest(t, usage0, "compute"));
            if (usage1 != "indirect") {
                bindGroupLayouts.push_back(createBindGroupLayoutForTest(t, usage1, "compute"));
            }
            WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayouts);
            WGPUComputePipeline computePipeline = createNoOpComputePipeline(t, pipelineLayout);
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, computePipeline);

            WGPUBindGroup bindGroup0 = createBindGroupForTest(t, buffer, 0, usage0, "compute");
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup0, 0, nullptr);
            WGPUBindGroup anotherBindGroup = createBindGroupForTest(t, anotherBuffer, 0, usage0, "compute");
            wgpuComputePassEncoderSetBindGroup(pass, 0, anotherBindGroup, 0, nullptr);

            if (usage1 == "indirect") {
                wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, buffer, 0);
            } else {
                WGPUBindGroup bindGroup1 = createBindGroupForTest(t, buffer, 0, usage1, "compute");
                wgpuComputePassEncoderSetBindGroup(pass, 1, bindGroup1, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            }
            wgpuComputePassEncoderEnd(pass);
            t.finishTracked(encoder);
        }, false);
    });

CTS_TEST(testGroup, "subresources,reset_buffer_usage_before_draw")
    .desc(R"(
Test that the buffer usages which are reset by another state-setting commands before a draw call
still contribute directly to the usage scope of the draw call.)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", {std::string("uniform"), std::string("storage"), std::string("read-only-storage"), std::string("vertex"), std::string("index")})
            .combine("usage1", allBufferUsages())
            .filter([](const ParamRecord& params) {
                return !(paramValue<std::string>(params, "usage0") == "index" &&
                         paramValue<std::string>(params, "usage1") == "indirect");
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string usage1 = t.param<std::string>("usage1");
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage0, "fragment", 1);
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage1, "fragment", 1);

        const WGPUBufferUsage usages = WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage |
            WGPUBufferUsage_Indirect | WGPUBufferUsage_Vertex | WGPUBufferUsage_Index;
        WGPUBuffer buffer = createBuffer(t, kBufferSize, usages);
        WGPUBuffer anotherBuffer = createBuffer(t, kBufferSize, usages);
        const bool fail = (usage0 == "storage") != (usage1 == "storage");

        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
            std::vector<WGPUBindGroupLayout> bindGroupLayouts;
            uint32_t vertexBufferCount = 0;

            if (usage0 == "uniform" || usage0 == "storage" || usage0 == "read-only-storage") {
                WGPUBindGroup bindGroup0 = createBindGroupForTest(t, buffer, 0, usage0, "fragment");
                wgpuRenderPassEncoderSetBindGroup(pass, static_cast<uint32_t>(bindGroupLayouts.size()), bindGroup0, 0, nullptr);
                WGPUBindGroup anotherBindGroup = createBindGroupForTest(t, anotherBuffer, 0, usage0, "fragment");
                wgpuRenderPassEncoderSetBindGroup(pass, static_cast<uint32_t>(bindGroupLayouts.size()), anotherBindGroup, 0, nullptr);
                bindGroupLayouts.push_back(createBindGroupLayoutForTest(t, usage0, "fragment"));
            } else if (usage0 == "vertex") {
                wgpuRenderPassEncoderSetVertexBuffer(pass, vertexBufferCount, buffer, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderSetVertexBuffer(pass, vertexBufferCount, anotherBuffer, 0, WGPU_WHOLE_SIZE);
                ++vertexBufferCount;
            } else if (usage0 == "index") {
                wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderSetIndexBuffer(pass, anotherBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
            }

            if (usage1 == "uniform" || usage1 == "storage" || usage1 == "read-only-storage") {
                WGPUBindGroup bindGroup1 = createBindGroupForTest(t, buffer, 0, usage1, "fragment");
                wgpuRenderPassEncoderSetBindGroup(pass, static_cast<uint32_t>(bindGroupLayouts.size()), bindGroup1, 0, nullptr);
                bindGroupLayouts.push_back(createBindGroupLayoutForTest(t, usage1, "fragment"));
            } else if (usage1 == "vertex") {
                wgpuRenderPassEncoderSetVertexBuffer(pass, vertexBufferCount, buffer, 0, WGPU_WHOLE_SIZE);
                ++vertexBufferCount;
            } else if (usage1 == "index") {
                wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
            }

            WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayouts);
            WGPURenderPipeline renderPipeline = createRenderPipelineForTest(t, pipelineLayout, vertexBufferCount);
            wgpuRenderPassEncoderSetPipeline(pass, renderPipeline);
            if (usage1 == "indexedIndirect") {
                if (usage0 != "index") {
                    WGPUBuffer indexBuffer = createBuffer(t, 4, WGPUBufferUsage_Index);
                    wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
                }
                wgpuRenderPassEncoderDrawIndexedIndirect(pass, buffer, 0);
            } else if (usage1 == "indirect") {
                wgpuRenderPassEncoderDrawIndirect(pass, buffer, 0);
            } else if (usage1 == "index") {
                wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);
            } else if (usage0 == "index") {
                wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);
            } else {
                wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
            }

            wgpuRenderPassEncoderEnd(pass);
            t.finishTracked(encoder);
        }, fail);
    });

CTS_TEST(testGroup, "subresources,buffer_usages_in_copy_and_pass")
    .desc(R"(
  Test that using one buffer in a copy command, a render or compute pass encoder is always allowed
  as WebGPU SPEC (chapter 3.4.5) defines that out of any pass encoder, each command belongs to one
  separated usage scope.)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", allCopyAndPassUsages())
            .combine("usage1", allCopyAndPassUsages())
            .combine("pass", {std::string("render"), std::string("compute")})
            .filter([](const ParamRecord& params) {
                const std::string usage0 = paramValue<std::string>(params, "usage0");
                const std::string usage1 = paramValue<std::string>(params, "usage1");
                const std::string pass = paramValue<std::string>(params, "pass");
                if (!isCopyUsage(usage0) && !isCopyUsage(usage1)) return false;
                if (isCopyUsage(usage0) && isCopyUsage(usage1)) return pass != "compute";
                if (pass == "compute") return isValidComputeUsage(usage0) && isValidComputeUsage(usage1);
                return true;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string usage1 = t.param<std::string>("usage1");
        const std::string passType = t.param<std::string>("pass");
        const std::string visibility = passType == "render" ? "fragment" : "compute";
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage0, visibility, 1);
        skipIfStorageBuffersUsedAndNotAvailableInStages(t, usage1, visibility, 1);

        const WGPUBufferUsage usages = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst |
            WGPUBufferUsage_Uniform | WGPUBufferUsage_Storage | WGPUBufferUsage_Indirect |
            WGPUBufferUsage_Vertex | WGPUBufferUsage_Index;
        WGPUBuffer buffer = createBuffer(t, kBufferSize, usages);

        auto useBufferOnCommandEncoder = [&](const std::string& usage, WGPUCommandEncoder encoder) {
            if (usage == "copy-src") {
                WGPUBuffer destinationBuffer = createBuffer(t, 4, WGPUBufferUsage_CopyDst);
                wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, 0, destinationBuffer, 0, 4);
            } else if (usage == "copy-dst") {
                WGPUBuffer sourceBuffer = createBuffer(t, 4, WGPUBufferUsage_CopySrc);
                wgpuCommandEncoderCopyBufferToBuffer(encoder, sourceBuffer, 0, buffer, 0, 4);
            } else if (usage == "uniform" || usage == "storage" || usage == "read-only-storage") {
                if (passType == "render") {
                    WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
                    WGPUBindGroup bindGroup = createBindGroupForTest(t, buffer, 0, usage, "fragment");
                    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
                    wgpuRenderPassEncoderEnd(pass);
                } else {
                    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
                    WGPUBindGroup bindGroup = createBindGroupForTest(t, buffer, 0, usage, "compute");
                    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
                    wgpuComputePassEncoderEnd(pass);
                }
            } else if (usage == "vertex") {
                WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, buffer, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderEnd(pass);
            } else if (usage == "index") {
                WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
                wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderEnd(pass);
            } else if (usage == "indirect") {
                if (passType == "render") {
                    WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
                    WGPURenderPipeline renderPipeline = createNoOpRenderPipeline(t);
                    wgpuRenderPassEncoderSetPipeline(pass, renderPipeline);
                    wgpuRenderPassEncoderDrawIndirect(pass, buffer, 0);
                    wgpuRenderPassEncoderEnd(pass);
                } else {
                    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
                    WGPUComputePipeline computePipeline = createNoOpComputePipeline(t);
                    wgpuComputePassEncoderSetPipeline(pass, computePipeline);
                    wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, buffer, 0);
                    wgpuComputePassEncoderEnd(pass);
                }
            } else if (usage == "indexedIndirect") {
                WGPURenderPassEncoder pass = beginSimpleRenderPass(t, encoder);
                WGPURenderPipeline renderPipeline = createNoOpRenderPipeline(t);
                wgpuRenderPassEncoderSetPipeline(pass, renderPipeline);
                WGPUBuffer indexBuffer = createBuffer(t, 4, WGPUBufferUsage_Index);
                wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
                wgpuRenderPassEncoderDrawIndexedIndirect(pass, buffer, 0);
                wgpuRenderPassEncoderEnd(pass);
            }
        };

        t.expectValidationError([&] {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            useBufferOnCommandEncoder(usage0, encoder);
            useBufferOnCommandEncoder(usage1, encoder);
            t.finishTracked(encoder);
        }, false);
    });

} // namespace
