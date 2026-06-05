// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/buffer/single_buffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,memory_sync,buffer,single_buffer",
    "Single-buffer memory synchronization operation tests.");

constexpr uint32_t kBufferSize = 4;
constexpr uint32_t kSrcValue = 0;
constexpr uint32_t kOpValue = 1;

constexpr std::string_view kStorageReadShader = R"(
struct Data {
  a : u32,
};

@group(0) @binding(0) var<storage, read> src : Data;
@group(0) @binding(1) var<storage, read_write> dst : Data;

@compute @workgroup_size(1)
fn main() {
  dst.a = src.a;
}
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

std::vector<Value> boundaryValues() {
    return {Value("command-buffer"), Value("queue-op")};
}

std::vector<Value> writeOpValues() {
    return {Value("storage"), Value("b2b-copy")};
}

std::vector<Value> readOpValues() {
    return {Value("storage-read"), Value("b2b-copy")};
}

std::string storageWriteShader(uint32_t value) {
    std::ostringstream shader;
    shader << R"(
struct Data {
  a : u32,
};

@group(0) @binding(0) var<storage, read_write> data : Data;

@compute @workgroup_size(1)
fn main() {
  data.a = )" << value << R"(u;
}
)";
    return shader.str();
}

WGPUBuffer createBufferWithValue(AllFeaturesMaxLimitsGpuTest& t, uint32_t value) {
    return t.makeBufferWithContents(
        &value,
        sizeof(value),
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage);
}

WGPUBuffer createDstBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = kBufferSize;
    desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
    return t.createBufferTracked(desc);
}

WGPUBindGroupLayout createStorageWriteBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Compute;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBindGroupLayout createStorageReadBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

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

WGPUComputePipeline createStorageWriteComputePipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUPipelineLayout layout,
    uint32_t value) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(storageWriteShader(value));

    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = stringView("main");
    return t.createComputePipelineTracked(desc);
}

WGPUComputePipeline createStorageReadComputePipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(kStorageReadShader);

    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = stringView("main");
    return t.createComputePipelineTracked(desc);
}

WGPUBindGroup createStorageWriteBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUBuffer buffer) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer;
    entry.offset = 0;
    entry.size = kBufferSize;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPUBindGroup createStorageReadBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUBuffer src,
    WGPUBuffer dst) {
    std::array<WGPUBindGroupEntry, 2> entries = {{
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    }};
    entries[0].binding = 0;
    entries[0].buffer = src;
    entries[0].offset = 0;
    entries[0].size = kBufferSize;
    entries[1].binding = 1;
    entries[1].buffer = dst;
    entries[1].offset = 0;
    entries[1].size = kBufferSize;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

void encodeWriteOp(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder, std::string_view op, WGPUBuffer buffer, uint32_t value) {
    if (op == "storage") {
        WGPUBindGroupLayout bindGroupLayout = createStorageWriteBindGroupLayout(t);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
        WGPUComputePipeline pipeline = createStorageWriteComputePipeline(t, pipelineLayout, value);
        WGPUBindGroup bindGroup = createStorageWriteBindGroup(t, bindGroupLayout, buffer);

        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        return;
    }

    if (op == "b2b-copy") {
        WGPUBuffer tmp = createBufferWithValue(t, value);
        wgpuCommandEncoderCopyBufferToBuffer(encoder, tmp, 0, buffer, 0, kBufferSize);
        return;
    }

    std::abort();
}

void encodeReadOp(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder, std::string_view op, WGPUBuffer src, WGPUBuffer dst) {
    if (op == "storage-read") {
        WGPUBindGroupLayout bindGroupLayout = createStorageReadBindGroupLayout(t);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
        WGPUComputePipeline pipeline = createStorageReadComputePipeline(t, pipelineLayout);
        WGPUBindGroup bindGroup = createStorageReadBindGroup(t, bindGroupLayout, src, dst);

        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        return;
    }

    if (op == "b2b-copy") {
        wgpuCommandEncoderCopyBufferToBuffer(encoder, src, 0, dst, 0, kBufferSize);
        return;
    }

    std::abort();
}

void submitWithBoundary(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view boundary,
    WGPUCommandBuffer firstCommandBuffer,
    WGPUCommandBuffer secondCommandBuffer) {
    if (boundary == "command-buffer") {
        std::array<WGPUCommandBuffer, 2> commandBuffers = {{firstCommandBuffer, secondCommandBuffer}};
        wgpuQueueSubmit(t.queue(), commandBuffers.size(), commandBuffers.data());
        return;
    }

    if (boundary == "queue-op") {
        wgpuQueueSubmit(t.queue(), 1, &firstCommandBuffer);
        wgpuQueueSubmit(t.queue(), 1, &secondCommandBuffer);
        return;
    }

    std::abort();
}

void verifyData(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer buffer, uint32_t expected) {
    t.expectGPUBufferValuesEqual(buffer, &expected, sizeof(expected));
}

void runReadWriteTest(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string boundary = t.param<std::string>("boundary");
    const std::string readOp = t.param<std::string>("readOp");
    const std::string writeOp = t.param<std::string>("writeOp");
    WGPUBuffer src = createBufferWithValue(t, kSrcValue);
    WGPUBuffer dst = createDstBuffer(t);

    WGPUCommandEncoder firstEncoder = t.createCommandEncoderTracked();
    encodeReadOp(t, firstEncoder, readOp, src, dst);
    WGPUCommandBuffer firstCommandBuffer = t.finishTracked(firstEncoder);

    WGPUCommandEncoder secondEncoder = t.createCommandEncoderTracked();
    encodeWriteOp(t, secondEncoder, writeOp, src, kOpValue);
    WGPUCommandBuffer secondCommandBuffer = t.finishTracked(secondEncoder);

    submitWithBoundary(t, boundary, firstCommandBuffer, secondCommandBuffer);
    verifyData(t, dst, kSrcValue);
}

void runWriteReadTest(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string boundary = t.param<std::string>("boundary");
    const std::string writeOp = t.param<std::string>("writeOp");
    const std::string readOp = t.param<std::string>("readOp");
    WGPUBuffer src = createBufferWithValue(t, kSrcValue);
    WGPUBuffer dst = createDstBuffer(t);

    WGPUCommandEncoder firstEncoder = t.createCommandEncoderTracked();
    encodeWriteOp(t, firstEncoder, writeOp, src, kOpValue);
    WGPUCommandBuffer firstCommandBuffer = t.finishTracked(firstEncoder);

    WGPUCommandEncoder secondEncoder = t.createCommandEncoderTracked();
    encodeReadOp(t, secondEncoder, readOp, src, dst);
    WGPUCommandBuffer secondCommandBuffer = t.finishTracked(secondEncoder);

    submitWithBoundary(t, boundary, firstCommandBuffer, secondCommandBuffer);
    verifyData(t, dst, kOpValue);
}

void runWriteWriteTest(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string boundary = t.param<std::string>("boundary");
    const std::string firstWriteOp = t.param<std::string>("firstWriteOp");
    const std::string secondWriteOp = t.param<std::string>("secondWriteOp");
    WGPUBuffer src = createBufferWithValue(t, kSrcValue);

    WGPUCommandEncoder firstEncoder = t.createCommandEncoderTracked();
    encodeWriteOp(t, firstEncoder, firstWriteOp, src, kOpValue);
    WGPUCommandBuffer firstCommandBuffer = t.finishTracked(firstEncoder);

    WGPUCommandEncoder secondEncoder = t.createCommandEncoderTracked();
    encodeWriteOp(t, secondEncoder, secondWriteOp, src, kOpValue + 1);
    WGPUCommandBuffer secondCommandBuffer = t.finishTracked(secondEncoder);

    submitWithBoundary(t, boundary, firstCommandBuffer, secondCommandBuffer);
    verifyData(t, src, kOpValue + 1);
}

CTS_TEST(g, "rw")
    .params([](ParamsBuilder u) {
        return u.combine("boundary", boundaryValues())
            .combine("readOp", readOpValues())
            .combine("writeOp", writeOpValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runReadWriteTest(t);
    });

CTS_TEST(g, "wr")
    .params([](ParamsBuilder u) {
        return u.combine("boundary", boundaryValues())
            .combine("writeOp", writeOpValues())
            .combine("readOp", readOpValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runWriteReadTest(t);
    });

CTS_TEST(g, "ww")
    .params([](ParamsBuilder u) {
        return u.combine("boundary", boundaryValues())
            .combine("firstWriteOp", writeOpValues())
            .combine("secondWriteOp", writeOpValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runWriteWriteTest(t);
    });

CTS_TEST(g, "two_dispatches_in_the_same_compute_pass")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBuffer buffer = createBufferWithValue(t, kSrcValue);
        WGPUBindGroupLayout bindGroupLayout = createStorageWriteBindGroupLayout(t);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
        WGPUComputePipeline firstPipeline = createStorageWriteComputePipeline(t, pipelineLayout, kOpValue);
        WGPUComputePipeline secondPipeline = createStorageWriteComputePipeline(t, pipelineLayout, kOpValue + 1);
        WGPUBindGroup bindGroup = createStorageWriteBindGroup(t, bindGroupLayout, buffer);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, firstPipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderSetPipeline(pass, secondPipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        verifyData(t, buffer, kOpValue + 1);
    });

CTS_TEST(g, "two_draws_in_the_same_render_pass")
    .unimplemented("storage-write render pipeline + render bundles deferred to V7b");

CTS_TEST(g, "two_draws_in_the_same_render_bundle")
    .unimplemented("storage-write render pipeline + render bundles deferred to V7b");

} // namespace
