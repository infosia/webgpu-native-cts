// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/bufferLength.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/language_features.h"
#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"
#include "webgpu/shader/execution/expression/call/builtin/buffer_view_utils.h"

using namespace cts;
using namespace cts::atomics;
using cts::buffer_view::skipIfBufferViewNotSupported;
using cts::buffer_view::skipIfUnrestrictedPointerParametersNotSupported;

namespace {

TestGroup<GpuTest> testGroup = MakeTestGroup<GpuTest>(
    "shader,execution,expression,call,builtin,bufferLength",
    "\nExecution tests for bufferLength\n\n"
    "Tests sized and unsized buffers across all address spaces (where applicable).\n"
    "Tested against buffers with static and dynamic offsets.\n");

WGPUBuffer createBuffer(GpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUBindGroupLayoutEntry bufferLayoutEntry(
    uint32_t binding,
    WGPUBufferBindingType type,
    bool hasDynamicOffset,
    uint64_t minBindingSize) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = WGPUShaderStage_Compute;
    entry.buffer.type = type;
    entry.buffer.hasDynamicOffset = hasDynamicOffset ? WGPU_TRUE : WGPU_FALSE;
    entry.buffer.minBindingSize = minBindingSize;
    return entry;
}

WGPUBindGroupEntry bufferEntry(uint32_t binding, WGPUBuffer buffer, uint64_t offset, uint64_t size) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.buffer = buffer;
    entry.offset = offset;
    entry.size = size;
    return entry;
}

/** Creates a pipeline with an explicit layout, binds the entries, and dispatches a workgroup. */
void runWithLayout(
    GpuTest& t,
    const std::string& wgsl,
    const std::vector<WGPUBindGroupLayoutEntry>& layoutEntries,
    const std::vector<WGPUBindGroupEntry>& entries,
    const std::vector<uint32_t>& dynamicOffsets) {
    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.entryCount = layoutEntries.size();
    layoutDesc.entries = layoutEntries.data();
    WGPUBindGroupLayout bgLayout = t.createBindGroupLayoutTracked(layoutDesc);

    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &bgLayout;
    WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(pipelineLayoutDesc);

    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = t.createShaderModuleTracked(wgsl);
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    WGPUBindGroupLayout pipelineBgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = pipelineBgl;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(pipelineBgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bg, dynamicOffsets.size(), dynamicOffsets.data());
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);
}

CTS_TEST(testGroup, "sized_buffer")
    .desc("Tests bufferLength directly on sized buffer variables")
    .params([](ParamsBuilder u) {
        return u.combine("size", {256, 512, 600, 1024, 2048})
            .beginSubcases()
            .combine("param", {"none", "unsized", "sized"})
            .combine("padding", {false, true})
            .combine("dynamic_offset", {false, true})
            .combine("offset", {0, 256});
    })
    .fn([](GpuTest& t) {
        const uint32_t size = static_cast<uint32_t>(t.param<int64_t>("size"));
        const std::string param = t.param<std::string>("param");
        const bool dynamicOffset = t.param<bool>("dynamic_offset");
        skipIfBufferViewNotSupported(t);
        if (param != "none") {
            skipIfUnrestrictedPointerParametersNotSupported(t);
        }

        const std::string sizeStr = std::to_string(size);
        const std::string halfSize = std::to_string(size / 2);
        std::string fnDecls;
        std::string assigns[4];
        if (param == "none") {
            assigns[0] = "bufferLength(&uniform_buffer)";
            assigns[1] = "bufferLength(&ro_storage_buffer)";
            assigns[2] = "bufferLength(&storage_buffer)";
            assigns[3] = "bufferLength(&workgroup_buffer)";
        } else if (param == "unsized") {
            fnDecls = R"(
fn uniformUnsized(p : ptr<uniform, buffer>) -> u32 {
  return bufferLength(p);
}
fn roStorageUnsized(p : ptr<storage, buffer>) -> u32 {
  return bufferLength(p);
}
fn storageUnsized(p : ptr<storage, buffer, read_write>) -> u32 {
  return bufferLength(p);
}
fn workgroupUnsized(p : ptr<workgroup, buffer>) -> u32 {
  return bufferLength(p);
})";
            assigns[0] = "uniformUnsized(&uniform_buffer)";
            assigns[1] = "roStorageUnsized(&ro_storage_buffer)";
            assigns[2] = "storageUnsized(&storage_buffer)";
            assigns[3] = "workgroupUnsized(&workgroup_buffer)";
        } else {
            fnDecls = R"(
fn uniformSized(p : ptr<uniform, buffer<)" + halfSize + R"(>>) -> u32 {
  return bufferLength(p);
}
fn roStorageSized(p : ptr<storage, buffer<)" + halfSize + R"(>>) -> u32 {
  return bufferLength(p);
}
fn storageSized(p : ptr<storage, buffer<)" + halfSize + R"(>, read_write>) -> u32 {
  return bufferLength(p);
}
fn workgroupSized(p : ptr<workgroup, buffer<)" + halfSize + R"(>>) -> u32 {
  return bufferLength(p);
})";
            assigns[0] = "uniformSized(&uniform_buffer)";
            assigns[1] = "roStorageSized(&ro_storage_buffer)";
            assigns[2] = "storageSized(&storage_buffer)";
            assigns[3] = "workgroupSized(&workgroup_buffer)";
        }

        const std::string wgsl = R"(
@group(0) @binding(0) var<uniform> uniform_buffer : buffer<)" + sizeStr + R"(>;
@group(0) @binding(1) var<storage> ro_storage_buffer : buffer<)" + sizeStr + R"(>;
@group(0) @binding(2) var<storage, read_write> storage_buffer : buffer<)" + sizeStr + R"(>;
var<workgroup> workgroup_buffer : buffer<)" + sizeStr + R"(>;

@group(0) @binding(3) var<storage, read_write> out : array<u32, 4>;

)" + fnDecls + R"(

@compute @workgroup_size(4)
fn main(@builtin(local_invocation_index) lid : u32) {
  out[0] = )" + assigns[0] + R"(;
  out[1] = )" + assigns[1] + R"(;
  out[2] = )" + assigns[2] + R"(;
  out[3] = )" + assigns[3] + R"(;
})";

        const uint64_t padding = t.param<bool>("padding") ? 256 : 0;
        const uint64_t offset = static_cast<uint64_t>(t.param<int64_t>("offset"));
        const uint32_t dynOffset = dynamicOffset ? 256 : 0;
        const uint64_t bufferSize = size + padding + offset + dynOffset;
        const uint64_t noOffsetSize = size + padding;

        WGPUBuffer uniformBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_Uniform);
        WGPUBuffer roStorageBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_Storage);
        WGPUBuffer storageBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_Storage);
        WGPUBuffer outputBuffer = createBuffer(t, 4 * 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);

        runWithLayout(
            t,
            wgsl,
            {bufferLayoutEntry(0, WGPUBufferBindingType_Uniform, dynamicOffset, size),
             bufferLayoutEntry(1, WGPUBufferBindingType_ReadOnlyStorage, dynamicOffset, size),
             bufferLayoutEntry(2, WGPUBufferBindingType_Storage, dynamicOffset, size),
             bufferLayoutEntry(3, WGPUBufferBindingType_Storage, false, 4 * 4)},
            {bufferEntry(0, uniformBuffer, offset, noOffsetSize),
             bufferEntry(1, roStorageBuffer, offset, noOffsetSize),
             bufferEntry(2, storageBuffer, offset, noOffsetSize),
             bufferEntry(3, outputBuffer, 0, 4 * 4)},
            dynamicOffset ? std::vector<uint32_t>{dynOffset, dynOffset, dynOffset} : std::vector<uint32_t>{});

        const uint32_t expectedSize = param == "sized" ? size / 2 : size;
        const uint32_t expected[4] = {expectedSize, expectedSize, expectedSize, expectedSize};
        t.expectGPUBufferValuesEqual(outputBuffer, expected, sizeof(expected));
    });

CTS_TEST(testGroup, "unsized_buffer")
    .desc("Tests bufferLength directly on unsized buffer variables")
    .params([](ParamsBuilder u) {
        return u.combine("size", {256, 512, 600, 1024, 2048})
            .beginSubcases()
            .combine("param", {"none", "unsized"})
            .combine("padding", {false, true})
            .combine("dynamic_offset", {false, true})
            .combine("offset", {0, 256});
    })
    .fn([](GpuTest& t) {
        const uint32_t size = static_cast<uint32_t>(t.param<int64_t>("size"));
        const std::string param = t.param<std::string>("param");
        const bool dynamicOffset = t.param<bool>("dynamic_offset");
        skipIfBufferViewNotSupported(t);
        if (param != "none") {
            skipIfUnrestrictedPointerParametersNotSupported(t);
        }

        std::string fnDecls;
        std::string assigns[2];
        if (param == "none") {
            assigns[0] = "bufferLength(&ro_storage_buffer)";
            assigns[1] = "bufferLength(&storage_buffer)";
        } else {
            fnDecls = R"(
fn roStorageUnsized(p : ptr<storage, buffer>) -> u32 {
  return bufferLength(p);
}
fn storageUnsized(p : ptr<storage, buffer, read_write>) -> u32 {
  return bufferLength(p);
})";
            assigns[0] = "roStorageUnsized(&ro_storage_buffer)";
            assigns[1] = "storageUnsized(&storage_buffer)";
        }

        const std::string wgsl = R"(
@group(0) @binding(0) var<storage> ro_storage_buffer : buffer;
@group(0) @binding(1) var<storage, read_write> storage_buffer : buffer;

@group(0) @binding(2) var<storage, read_write> out : array<u32, 4>;

)" + fnDecls + R"(

@compute @workgroup_size(4)
fn main(@builtin(local_invocation_index) lid : u32) {
  out[0] = )" + assigns[0] + R"(;
  out[1] = )" + assigns[1] + R"(;
})";

        const uint64_t padding = t.param<bool>("padding") ? 256 : 0;
        const uint64_t offset = static_cast<uint64_t>(t.param<int64_t>("offset"));
        const uint32_t dynOffset = dynamicOffset ? 256 : 0;
        const uint64_t bufferSize = size + padding + offset + dynOffset;
        const uint64_t noOffsetSize = size + padding;

        WGPUBuffer roStorageBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_Storage);
        WGPUBuffer storageBuffer = createBuffer(t, bufferSize, WGPUBufferUsage_Storage);
        WGPUBuffer outputBuffer = createBuffer(t, 4 * 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);

        runWithLayout(
            t,
            wgsl,
            {bufferLayoutEntry(0, WGPUBufferBindingType_ReadOnlyStorage, dynamicOffset, size),
             bufferLayoutEntry(1, WGPUBufferBindingType_Storage, dynamicOffset, size),
             bufferLayoutEntry(2, WGPUBufferBindingType_Storage, false, 4 * 4)},
            {bufferEntry(0, roStorageBuffer, offset, noOffsetSize),
             bufferEntry(1, storageBuffer, offset, noOffsetSize),
             bufferEntry(2, outputBuffer, 0, 4 * 4)},
            dynamicOffset ? std::vector<uint32_t>{dynOffset, dynOffset} : std::vector<uint32_t>{});

        const uint32_t expected[2] = {static_cast<uint32_t>(noOffsetSize), static_cast<uint32_t>(noOffsetSize)};
        t.expectGPUBufferValuesEqual(outputBuffer, expected, sizeof(expected));
    });

CTS_TEST(testGroup, "max_size_buffer")
    .desc("Test with a maximum sized buffer")
    .params([](ParamsBuilder u) {
        return u.combine("aspace", {"workgroup", "uniform", "storage", "ro_storage"})
            .combine("sized", {false, true})
            .filter([](const ParamRecord& p) {
                const std::string aspace = valueAs<std::string>(*findParam(p, "aspace"));
                return valueAs<bool>(*findParam(p, "sized")) || aspace == "storage" || aspace == "ro_storage";
            });
    })
    .fn([](GpuTest& t) {
        const std::string aspace = t.param<std::string>("aspace");
        skipIfBufferViewNotSupported(t);

        const WGPULimits limits = t.getLimits();

        uint64_t size = 16;
        if (aspace == "workgroup") {
            size = limits.maxComputeWorkgroupStorageSize;
        } else if (aspace == "uniform") {
            size = limits.maxUniformBufferBindingSize;
        } else {
            size = limits.maxStorageBufferBindingSize;
        }

        const std::string type = t.param<bool>("sized") ? "buffer<" + std::to_string(size) + "u>" : "buffer";
        std::string decl;
        if (aspace == "workgroup") {
            decl = "var<workgroup> v : " + type + ";\n@group(0) @binding(1) var<storage> dummy : u32;";
        } else if (aspace == "uniform") {
            decl = "@group(0) @binding(1) var<uniform> v: " + type + ";";
        } else if (aspace == "storage") {
            decl = "@group(0) @binding(1) var<storage, read_write> v : " + type + ";";
        } else {
            decl = "@group(0) @binding(1) var<storage> v : " + type + ";";
        }

        const std::string wgsl = R"(
@group(0) @binding(0) var<storage, read_write> out : u32;
)" + decl + R"(

@compute @workgroup_size(1)
fn main() {
  out = bufferLength(&v);
  )" + (aspace == "workgroup" ? "_ = dummy;" : "") + R"(
})";

        WGPUBuffer outputBuffer = createBuffer(t, 4, WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        // For the workgroup case we create a dummy storage buffer to simplify bindings.
        WGPUBuffer inputBuffer =
            createBuffer(t, size, aspace == "uniform" ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage);

        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBindGroup bg = makeAutoBindGroup(t, pipeline, {{outputBuffer, 4}, {inputBuffer, size}});
        runComputePassX(t, pipeline, bg, 1);

        const uint32_t expected = static_cast<uint32_t>(size);
        t.expectGPUBufferValuesEqual(outputBuffer, &expected, sizeof(expected));
    });

} // namespace
