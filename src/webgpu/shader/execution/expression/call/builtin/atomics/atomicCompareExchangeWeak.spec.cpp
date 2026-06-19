// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicCompareExchangeWeak.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicCompareExchangeWeak",
    "Atomically compares and conditionally exchanges an atomic object.");

CTS_TEST(testGroup, "compare_exchange_weak_storage_basic")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = static_cast<int>(t.param<int64_t>("workgroupSize"));
        const int dispatchSize = static_cast<int>(t.param<int64_t>("dispatchSize"));
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
        const uint32_t n = static_cast<uint32_t>(workgroupSize * dispatchSize);
        const std::string type = scalarTypeName(scalarType);
        const std::string wgsl =
            "@group(0) @binding(0) var<storage, read_write> input : array<atomic<" + type + ">>;\n"
            "@group(0) @binding(1) var<storage, read_write> output : array<" + type + ">;\n"
            "@group(0) @binding(2) var<storage, read_write> exchanged : array<" + type + ">;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(global_invocation_id) global_invocation_id : vec3<u32>) {\n"
            "  let id = " + type + "(global_invocation_id[0]);\n"
            "  var comp = id + 1;\n"
            "  if (id % 3 == 0) { comp = id; }\n"
            "  let r = atomicCompareExchangeWeak(&input[id], comp, map_id(id * 2));\n"
            "  output[id] = r.old_value;\n"
            "  if (r.exchanged) { exchanged[id] = 1; } else { exchanged[id] = 0; }\n"
            "}\n" +
            mapId.wgsl(n, scalarType) + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        std::vector<uint32_t> initial(n);
        for (uint32_t i = 0; i < n; ++i) {
            initial[i] = i;
        }
        WGPUBuffer input = makeFilledStorageBuffer(t, initial);
        WGPUBuffer output = makeStorageBuffer(t, n);
        WGPUBuffer exchanged = makeStorageBuffer(t, n);
        WGPUBindGroup bindGroup =
            makeAutoBindGroup(t, pipeline, {{input, n * 4ull}, {output, n * 4ull}, {exchanged, n * 4ull}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        t.expectGPUBufferValuesEqual(output, initial.data(), initial.size() * sizeof(uint32_t));
        const std::vector<uint32_t> exchangedValues = readBackU32(t, exchanged, n);
        std::vector<uint32_t> inputExpected(n);
        for (uint32_t i = 0; i < n; ++i) {
            inputExpected[i] = (i % 3u == 0u && exchangedValues[i] != 0u) ? mapId.f(i * 2u, n) : i;
        }
        t.expectGPUBufferValuesEqual(input, inputExpected.data(), inputExpected.size() * sizeof(uint32_t));
    });

CTS_TEST(testGroup, "compare_exchange_weak_workgroup_basic")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = static_cast<int>(t.param<int64_t>("workgroupSize"));
        const int dispatchSize = static_cast<int>(t.param<int64_t>("dispatchSize"));
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
        const uint32_t n = static_cast<uint32_t>(workgroupSize);
        const uint32_t total = n * static_cast<uint32_t>(dispatchSize);
        const std::string type = scalarTypeName(scalarType);
        const std::string wgsl =
            "var<workgroup> wg: array<atomic<" + type + ">, " + std::to_string(n) + ">;\n"
            "@group(0) @binding(0) var<storage, read_write> output: array<" + type + ", " + std::to_string(total) + ">;\n"
            "@group(0) @binding(1) var<storage, read_write> exchanged: array<" + type + ", " + std::to_string(total) + ">;\n"
            "@group(0) @binding(2) var<storage, read_write> wg_copy: array<" + type + ", " + std::to_string(total) + ">;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(local_invocation_index) local_invocation_index: u32,\n"
            "        @builtin(workgroup_id) workgroup_id : vec3<u32>) {\n"
            "  let id = " + type + "(local_invocation_index);\n"
            "  let global_id = " + type + "(workgroup_id.x * " + std::to_string(n) + " + local_invocation_index);\n"
            "  atomicStore(&wg[id], global_id);\n"
            "  var comp = global_id + 1;\n"
            "  if (global_id % 3 == 0) { comp = global_id; }\n"
            "  let r = atomicCompareExchangeWeak(&wg[id], comp, map_id(global_id * 2));\n"
            "  output[global_id] = r.old_value;\n"
            "  if (r.exchanged) { exchanged[global_id] = 1; } else { exchanged[global_id] = 0; }\n"
            "  wg_copy[global_id] = atomicLoad(&wg[id]);\n"
            "}\n" +
            mapId.wgsl(n, scalarType) + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer output = makeStorageBuffer(t, total);
        WGPUBuffer exchanged = makeStorageBuffer(t, total);
        WGPUBuffer wgCopy = makeStorageBuffer(t, total);
        WGPUBindGroup bindGroup =
            makeAutoBindGroup(t, pipeline, {{output, total * 4ull}, {exchanged, total * 4ull}, {wgCopy, total * 4ull}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        std::vector<uint32_t> outputExpected(total);
        for (uint32_t i = 0; i < total; ++i) {
            outputExpected[i] = i;
        }
        t.expectGPUBufferValuesEqual(output, outputExpected.data(), outputExpected.size() * sizeof(uint32_t));
        const std::vector<uint32_t> exchangedValues = readBackU32(t, exchanged, total);
        std::vector<uint32_t> wgCopyExpected(total);
        for (uint32_t i = 0; i < total; ++i) {
            wgCopyExpected[i] = (i % 3u == 0u && exchangedValues[i] != 0u) ? mapId.f(i * 2u, n) : i;
        }
        t.expectGPUBufferValuesEqual(wgCopy, wgCopyExpected.data(), wgCopyExpected.size() * sizeof(uint32_t));
    });

CTS_TEST(testGroup, "compare_exchange_weak_storage_advanced")
    .params([](ParamsBuilder u) { return onlyWorkgroupParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = static_cast<int>(t.param<int64_t>("workgroupSize"));
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const WGPULimits limits = t.getLimits();
        if (static_cast<uint32_t>(workgroupSize) > limits.maxComputeWorkgroupSizeX) {
            t.skip("workgroupSize exceeds maxComputeWorkgroupSizeX");
        }
        const uint32_t numWrites = 4;
        const uint32_t n = static_cast<uint32_t>(workgroupSize);
        const uint32_t total = n * numWrites;
        const std::string type = scalarTypeName(scalarType);
        const std::string wgsl =
            "@group(0) @binding(0) var<storage, read_write> data : atomic<" + type + ">;\n"
            "@group(0) @binding(1) var<storage, read_write> old_values : array<" + type + ">;\n"
            "@group(0) @binding(2) var<storage, read_write> exchanged : array<" + type + ">;\n"
            "fn ping_pong_value(i: u32) -> " + type + " { if (i % 2 == 0) { return 24; } else { return 68; } }\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(global_invocation_id) global_invocation_id : vec3<u32>) {\n"
            "  let id = " + type + "(global_invocation_id[0]);\n"
            "  for (var i = 0u; i < 4u; i++) {\n"
            "    let compare = ping_pong_value(i);\n"
            "    let next = ping_pong_value(i + 1);\n"
            "    let r = atomicCompareExchangeWeak(&data, compare, next);\n"
            "    let slot = i * " + std::to_string(n) + "u + u32(id);\n"
            "    old_values[slot] = r.old_value;\n"
            "    if (r.exchanged) { exchanged[slot] = 1; } else { exchanged[slot] = 0; }\n"
            "    workgroupBarrier();\n"
            "  }\n"
            "}\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer data = makeFilledStorageBuffer(t, std::vector<uint32_t>{24u});
        WGPUBuffer oldValues = makeFilledStorageBuffer(t, total, 99999999u);
        WGPUBuffer exchanged = makeFilledStorageBuffer(t, total, 99999999u);
        WGPUBindGroup bindGroup = makeAutoBindGroup(t, pipeline, {{data, 4}, {oldValues, total * 4ull}, {exchanged, total * 4ull}});
        runComputePassX(t, pipeline, bindGroup, 1);
        validateCompareExchangeWeakAdvanced(t, readBackU32(t, oldValues, total), readBackU32(t, exchanged, total), n, numWrites);
    });

CTS_TEST(testGroup, "compare_exchange_weak_workgroup_advanced")
    .params([](ParamsBuilder u) { return onlyWorkgroupParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = static_cast<int>(t.param<int64_t>("workgroupSize"));
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const WGPULimits limits = t.getLimits();
        if (static_cast<uint32_t>(workgroupSize) > limits.maxComputeWorkgroupSizeX) {
            t.skip("workgroupSize exceeds maxComputeWorkgroupSizeX");
        }
        const uint32_t numWrites = 4;
        const uint32_t n = static_cast<uint32_t>(workgroupSize);
        const uint32_t total = n * numWrites;
        const std::string type = scalarTypeName(scalarType);
        const std::string wgsl =
            "var<workgroup> wg: atomic<" + type + ">;\n"
            "@group(0) @binding(0) var<storage, read_write> old_values : array<" + type + ">;\n"
            "@group(0) @binding(1) var<storage, read_write> exchanged : array<" + type + ">;\n"
            "fn ping_pong_value(i: u32) -> " + type + " { if (i % 2 == 0) { return 24; } else { return 68; } }\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(local_invocation_index) local_invocation_index: u32,\n"
            "        @builtin(workgroup_id) workgroup_id : vec3<u32>) {\n"
            "  let id = " + type + "(local_invocation_index);\n"
            "  if (local_invocation_index == 0) { atomicStore(&wg, 24); }\n"
            "  workgroupBarrier();\n"
            "  for (var i = 0u; i < 4u; i++) {\n"
            "    let compare = ping_pong_value(i);\n"
            "    let next = ping_pong_value(i + 1);\n"
            "    let r = atomicCompareExchangeWeak(&wg, compare, next);\n"
            "    let slot = i * " + std::to_string(n) + "u + u32(id);\n"
            "    old_values[slot] = r.old_value;\n"
            "    if (r.exchanged) { exchanged[slot] = 1; } else { exchanged[slot] = 0; }\n"
            "    workgroupBarrier();\n"
            "  }\n"
            "}\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer oldValues = makeFilledStorageBuffer(t, total, 99999999u);
        WGPUBuffer exchanged = makeFilledStorageBuffer(t, total, 99999999u);
        WGPUBindGroup bindGroup = makeAutoBindGroup(t, pipeline, {{oldValues, total * 4ull}, {exchanged, total * 4ull}});
        runComputePassX(t, pipeline, bindGroup, 1);
        validateCompareExchangeWeakAdvanced(t, readBackU32(t, oldValues, total), readBackU32(t, exchanged, total), n, numWrites);
    });

} // namespace
