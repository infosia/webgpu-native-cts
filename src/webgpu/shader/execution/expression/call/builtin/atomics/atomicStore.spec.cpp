// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicStore.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicStore",
    "Atomically stores the value v in the atomic object pointed to by atomic_ptr.");

CTS_TEST(testGroup, "store_storage_basic")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
        const uint32_t numInvocations = static_cast<uint32_t>(workgroupSize * dispatchSize);
        std::vector<uint32_t> expected(numInvocations);
        for (uint32_t i = 0; i < numInvocations; ++i) {
            expected[i] = mapId.f(i, numInvocations);
        }
        runStorageVariableTest(
            t,
            workgroupSize,
            dispatchSize,
            numInvocations,
            0,
            "atomicStore(&output[id], map_id(id))",
            expected,
            scalarType,
            mapId.wgsl(numInvocations, scalarType));
    });

CTS_TEST(testGroup, "store_workgroup_basic")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
        const uint32_t numInvocations = static_cast<uint32_t>(workgroupSize);
        const uint32_t outputElements = numInvocations * static_cast<uint32_t>(dispatchSize);
        std::vector<uint32_t> expected(outputElements);
        for (uint32_t i = 0; i < outputElements; ++i) {
            expected[i] = mapId.f(i, numInvocations);
        }
        runWorkgroupVariableTest(
            t,
            workgroupSize,
            dispatchSize,
            numInvocations,
            0,
            "atomicStore(&wg[id], map_id(global_id))",
            expected,
            scalarType,
            mapId.wgsl(numInvocations, scalarType));
    });

CTS_TEST(testGroup, "store_storage_advanced")
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
            "var<storage, read_write> output : array<atomic<" + type + ">>;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(global_invocation_id) global_invocation_id : vec3<u32>) {\n"
            "  let id = " + type + "(global_invocation_id[0]);\n"
            "  atomicStore(&output[0], map_id(id));\n"
            "}\n" +
            mapId.wgsl(numInvocations, scalarType) + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer output = makeStorageBuffer(t, 1);
        WGPUBindGroup bindGroup = makeAutoBindGroup(t, pipeline, {{output, sizeof(uint32_t)}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        const std::vector<uint32_t> result = readBackU32(t, output, 1);
        std::vector<uint32_t> expectedOneOf(numInvocations);
        for (uint32_t i = 0; i < numInvocations; ++i) {
            expectedOneOf[i] = mapId.f(i, numInvocations);
        }
        expectOneOf(t, result[0], expectedOneOf, "output[0]");
    });

CTS_TEST(testGroup, "store_workgroup_advanced")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
        const uint32_t numInvocations = static_cast<uint32_t>(workgroupSize);
        const std::string type = scalarTypeName(scalarType);
        const std::string wgsl =
            "var<workgroup> wg: atomic<" + type + ">;\n"
            "@group(0) @binding(0)\n"
            "var<storage, read_write> output: array<" + type + ", " + std::to_string(dispatchSize) + ">;\n"
            "@compute @workgroup_size(" + std::to_string(workgroupSize) + ")\n"
            "fn main(@builtin(local_invocation_index) local_invocation_index: u32,\n"
            "        @builtin(workgroup_id) workgroup_id : vec3<u32>) {\n"
            "  let id = " + type + "(local_invocation_index);\n"
            "  atomicStore(&wg, map_id(id));\n"
            "  workgroupBarrier();\n"
            "  if (local_invocation_index == 0u) {\n"
            "    output[workgroup_id.x] = atomicLoad(&wg);\n"
            "  }\n"
            "}\n" +
            mapId.wgsl(numInvocations, scalarType) + "\n";
        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBuffer output = makeStorageBuffer(t, static_cast<uint32_t>(dispatchSize));
        WGPUBindGroup bindGroup =
            makeAutoBindGroup(t, pipeline, {{output, static_cast<uint64_t>(dispatchSize) * sizeof(uint32_t)}});
        runComputePassX(t, pipeline, bindGroup, static_cast<uint32_t>(dispatchSize));
        const std::vector<uint32_t> result = readBackU32(t, output, static_cast<size_t>(dispatchSize));
        std::vector<uint32_t> expectedOneOf(numInvocations);
        for (uint32_t i = 0; i < numInvocations; ++i) {
            expectedOneOf[i] = mapId.f(i, numInvocations);
        }
        for (int d = 0; d < dispatchSize; ++d) {
            expectOneOf(t, result[static_cast<size_t>(d)], expectedOneOf, "output[" + std::to_string(d) + "]");
        }
    });

} // namespace
