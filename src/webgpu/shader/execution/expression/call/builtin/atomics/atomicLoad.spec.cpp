// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicLoad.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicLoad",
    "Returns the atomically loaded value pointed to by atomic_ptr.");

CTS_TEST(testGroup, "load_storage")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
        const uint32_t numInvocations = static_cast<uint32_t>(workgroupSize * dispatchSize);
        const std::string type = scalarTypeName(scalarType);
        const std::string wgsl =
            "@group(0) @binding(0)\n"
            "var<storage, read_write> input : array<atomic<" + type + ">>;\n"
            "@group(0) @binding(1)\n"
            "var<storage, read_write> output : array<" + type + ">;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(global_invocation_id) global_invocation_id : vec3<u32>) {\n"
            "  let id = " + type + "(global_invocation_id[0]);\n"
            "  output[id] = atomicLoad(&input[id]);\n"
            "}\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        std::vector<uint32_t> expected(numInvocations);
        for (uint32_t i = 0; i < numInvocations; ++i) {
            expected[i] = mapId.f(i, numInvocations);
        }
        WGPUBuffer input = makeFilledStorageBuffer(t, expected);
        WGPUBuffer output = makeStorageBuffer(t, numInvocations);
        WGPUBindGroup bindGroup = makeAutoBindGroup(
            t,
            pipeline,
            {{input, static_cast<uint64_t>(numInvocations) * sizeof(uint32_t)},
             {output, static_cast<uint64_t>(numInvocations) * sizeof(uint32_t)}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        t.expectGPUBufferValuesEqual(input, expected.data(), expected.size() * sizeof(uint32_t));
        t.expectGPUBufferValuesEqual(output, expected.data(), expected.size() * sizeof(uint32_t));
    });

CTS_TEST(testGroup, "load_workgroup")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
        const uint32_t numInvocations = static_cast<uint32_t>(workgroupSize);
        const uint32_t outputElements = numInvocations * static_cast<uint32_t>(dispatchSize);
        const std::string type = scalarTypeName(scalarType);
        const std::string extra = mapId.wgsl(numInvocations, scalarType);
        const std::string wgsl =
            "var<workgroup> wg: array<atomic<" + type + ">, " + std::to_string(numInvocations) + ">;\n"
            "@group(0) @binding(0)\n"
            "var<storage, read_write> output: array<" + type + ", " + std::to_string(outputElements) + ">;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(local_invocation_index) local_invocation_index: u32,\n"
            "        @builtin(workgroup_id) workgroup_id : vec3<u32>) {\n"
            "  let id = " + type + "(local_invocation_index);\n"
            "  let global_id = " + type + "(workgroup_id.x * " + std::to_string(numInvocations) +
            " + local_invocation_index);\n"
            "  atomicStore(&wg[id], map_id(global_id));\n"
            "  workgroupBarrier();\n"
            "  output[global_id] = atomicLoad(&wg[id]);\n"
            "}\n" +
            extra + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer output = makeStorageBuffer(t, outputElements);
        WGPUBindGroup bindGroup =
            makeAutoBindGroup(t, pipeline, {{output, static_cast<uint64_t>(outputElements) * sizeof(uint32_t)}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        std::vector<uint32_t> expected(outputElements);
        for (uint32_t i = 0; i < outputElements; ++i) {
            expected[i] = mapId.f(i, numInvocations);
        }
        t.expectGPUBufferValuesEqual(output, expected.data(), expected.size() * sizeof(uint32_t));
    });

} // namespace
