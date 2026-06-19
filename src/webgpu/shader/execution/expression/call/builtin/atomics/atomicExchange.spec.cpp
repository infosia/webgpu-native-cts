// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicExchange.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicExchange",
    "Atomically stores the value v and returns the original atomic object value.");

CTS_TEST(testGroup, "exchange_storage_basic")
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
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(global_invocation_id) global_invocation_id : vec3<u32>) {\n"
            "  let id = " + type + "(global_invocation_id[0]);\n"
            "  output[id] = atomicExchange(&input[id], map_id(id * 2));\n"
            "}\n" +
            mapId.wgsl(n, scalarType) + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        std::vector<uint32_t> initial(n);
        for (uint32_t i = 0; i < n; ++i) {
            initial[i] = i;
        }
        WGPUBuffer input = makeFilledStorageBuffer(t, initial);
        WGPUBuffer output = makeStorageBuffer(t, n);
        WGPUBindGroup bindGroup =
            makeAutoBindGroup(t, pipeline, {{input, n * 4ull}, {output, n * 4ull}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        t.expectGPUBufferValuesEqual(output, initial.data(), initial.size() * sizeof(uint32_t));
        std::vector<uint32_t> inputExpected(n);
        for (uint32_t i = 0; i < n; ++i) {
            inputExpected[i] = mapId.f(i * 2u, n);
        }
        t.expectGPUBufferValuesEqual(input, inputExpected.data(), inputExpected.size() * sizeof(uint32_t));
    });

CTS_TEST(testGroup, "exchange_workgroup_basic")
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
            "@group(0) @binding(1) var<storage, read_write> wg_copy: array<" + type + ", " + std::to_string(total) + ">;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(local_invocation_index) local_invocation_index: u32,\n"
            "        @builtin(workgroup_id) workgroup_id : vec3<u32>) {\n"
            "  let id = " + type + "(local_invocation_index);\n"
            "  let global_id = " + type + "(workgroup_id.x * " + std::to_string(n) + " + local_invocation_index);\n"
            "  atomicStore(&wg[id], global_id);\n"
            "  workgroupBarrier();\n"
            "  output[global_id] = atomicExchange(&wg[id], map_id(global_id * 2));\n"
            "  wg_copy[global_id] = atomicLoad(&wg[id]);\n"
            "}\n" +
            mapId.wgsl(n, scalarType) + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer output = makeStorageBuffer(t, total);
        WGPUBuffer wgCopy = makeStorageBuffer(t, total);
        WGPUBindGroup bindGroup =
            makeAutoBindGroup(t, pipeline, {{output, total * 4ull}, {wgCopy, total * 4ull}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        std::vector<uint32_t> outputExpected(total);
        std::vector<uint32_t> wgCopyExpected(total);
        for (uint32_t i = 0; i < total; ++i) {
            outputExpected[i] = i;
            wgCopyExpected[i] = mapId.f(i * 2u, n);
        }
        t.expectGPUBufferValuesEqual(output, outputExpected.data(), outputExpected.size() * sizeof(uint32_t));
        t.expectGPUBufferValuesEqual(wgCopy, wgCopyExpected.data(), wgCopyExpected.size() * sizeof(uint32_t));
    });

CTS_TEST(testGroup, "exchange_storage_advanced")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = static_cast<int>(t.param<int64_t>("workgroupSize"));
        const int dispatchSize = static_cast<int>(t.param<int64_t>("dispatchSize"));
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
        const uint32_t n = static_cast<uint32_t>(workgroupSize * dispatchSize);
        const std::string type = scalarTypeName(scalarType);
        const std::string wgsl =
            "@group(0) @binding(0) var<storage, read_write> input : atomic<" + type + ">;\n"
            "@group(0) @binding(1) var<storage, read_write> output : array<" + type + ">;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(global_invocation_id) global_invocation_id : vec3<u32>) {\n"
            "  let id = " + type + "(global_invocation_id[0]);\n"
            "  output[id] = atomicExchange(&input, map_id(id));\n"
            "}\n" +
            mapId.wgsl(n, scalarType) + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer input = makeStorageBuffer(t, 1);
        WGPUBuffer output = makeStorageBuffer(t, n);
        WGPUBindGroup bindGroup = makeAutoBindGroup(t, pipeline, {{input, 4}, {output, n * 4ull}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        std::vector<uint32_t> values = readBackU32(t, input, 1);
        const std::vector<uint32_t> outputValues = readBackU32(t, output, n);
        values.insert(values.end(), outputValues.begin(), outputValues.end());
        std::vector<uint32_t> expected(values.size());
        expected[0] = 0;
        for (uint32_t i = 1; i < expected.size(); ++i) {
            expected[i] = mapId.f(i - 1u, n);
        }
        expectSortedEqual(t, values, expected);
    });

CTS_TEST(testGroup, "exchange_workgroup_advanced")
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
            "var<workgroup> wg: atomic<" + type + ">;\n"
            "@group(0) @binding(0) var<storage, read_write> output: array<" + type + ", " + std::to_string(total) + ">;\n"
            "@group(0) @binding(1) var<storage, read_write> wg_copy: array<" + type + ", " + std::to_string(dispatchSize) + ">;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(local_invocation_index) local_invocation_index: u32,\n"
            "        @builtin(workgroup_id) workgroup_id : vec3<u32>) {\n"
            "  let id = " + type + "(local_invocation_index);\n"
            "  let global_id = " + type + "(workgroup_id.x * " + std::to_string(n) + " + local_invocation_index);\n"
            "  output[global_id] = atomicExchange(&wg, map_id(id));\n"
            "  workgroupBarrier();\n"
            "  if (local_invocation_index == 0u) { wg_copy[workgroup_id.x] = atomicLoad(&wg); }\n"
            "}\n" +
            mapId.wgsl(n, scalarType) + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer output = makeStorageBuffer(t, total);
        WGPUBuffer wgCopy = makeStorageBuffer(t, static_cast<uint32_t>(dispatchSize));
        WGPUBindGroup bindGroup =
            makeAutoBindGroup(t, pipeline, {{output, total * 4ull}, {wgCopy, static_cast<uint64_t>(dispatchSize) * 4ull}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        const std::vector<uint32_t> outputValues = readBackU32(t, output, total);
        const std::vector<uint32_t> copyValues = readBackU32(t, wgCopy, static_cast<size_t>(dispatchSize));
        for (int d = 0; d < dispatchSize; ++d) {
            std::vector<uint32_t> values;
            values.reserve(static_cast<size_t>(n) + 1);
            values.push_back(copyValues[static_cast<size_t>(d)]);
            values.insert(values.end(), outputValues.begin() + static_cast<size_t>(d) * n, outputValues.begin() + (static_cast<size_t>(d) + 1) * n);
            std::vector<uint32_t> expected(values.size());
            expected[0] = 0;
            for (uint32_t i = 1; i < expected.size(); ++i) {
                expected[i] = mapId.f(i - 1u, n);
            }
            expectSortedEqual(t, values, expected);
        }
    });

} // namespace
