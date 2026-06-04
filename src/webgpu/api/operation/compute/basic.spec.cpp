// Ported from gpuweb/cts src/webgpu/api/operation/compute/basic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,compute,basic",
    "Basic compute operation tests.");

constexpr std::string_view kMemcpyShader = R"(
struct Data { value : u32 };
@group(0) @binding(0) var<storage, read> src : Data;
@group(0) @binding(1) var<storage, read_write> dst : Data;
@compute @workgroup_size(1) fn main() {
  dst.value = src.value;
  return;
}
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

WGPUBuffer createStorageDestinationBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = 4;
    desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
    return t.createBufferTracked(desc);
}

WGPUBindGroupLayout createMemcpyBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries;

    entries[0] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    entries[1] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[1].buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

WGPUComputePipeline createMemcpyPipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(kMemcpyShader);

    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = stringView("main");
    return t.createComputePipelineTracked(desc);
}

WGPUBindGroup createMemcpyBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUBuffer src,
    WGPUBuffer dst) {
    std::array<WGPUBindGroupEntry, 2> entries;

    entries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].buffer = src;
    entries[0].offset = 0;
    entries[0].size = 4;

    entries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[1].binding = 1;
    entries[1].buffer = dst;
    entries[1].offset = 0;
    entries[1].size = 4;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

CTS_TEST(g, "memcpy")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t data = 0x01020304;
        WGPUBuffer src = t.makeBufferWithContents(&data, sizeof(data), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage);
        WGPUBuffer dst = createStorageDestinationBuffer(t);

        WGPUBindGroupLayout bindGroupLayout = createMemcpyBindGroupLayout(t);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
        WGPUComputePipeline pipeline = createMemcpyPipeline(t, pipelineLayout);
        WGPUBindGroup bindGroup = createMemcpyBindGroup(t, bindGroupLayout, src, dst);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        submit(t, encoder);

        t.expectGPUBufferValuesEqual(dst, &data, sizeof(data));
    });

CTS_TEST(g, "large_dispatch")
    .unimplemented("large dispatch compute tests are deferred");

} // namespace
