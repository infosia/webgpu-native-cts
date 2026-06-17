// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicXor.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicXor",
    "Atomically read, xor and store value.");

void runXor(AllFeaturesMaxLimitsGpuTest& t, bool workgroup) {
    const int workgroupSize = t.param<int64_t>("workgroupSize");
    const int dispatchSize = t.param<int64_t>("dispatchSize");
    const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
    const MapId& mapId = mapIdFromParam(t.param<std::string>("mapId"));
    const uint32_t numInvocations = static_cast<uint32_t>(workgroup ? workgroupSize : workgroupSize * dispatchSize);
    const uint32_t numElements = std::max(1u, numInvocations / 32u) + 1u;
    const uint32_t initValue = 0b11000011010110100000111100111100u;
    const std::string type = scalarTypeName(scalarType);
    const std::string extra = mapId.wgsl(numInvocations, ScalarType::U32);
    const std::string target = workgroup ? "wg" : "output";
    const std::string op =
        "let i = map_id(u32(id));\n"
        "  atomicXor(&" + target + "[i / 32], " + type + "(1) << i)";

    std::vector<uint32_t> expected(static_cast<size_t>(numElements) * (workgroup ? dispatchSize : 1), initValue);
    for (int d = 0; d < (workgroup ? dispatchSize : 1); ++d) {
        for (uint32_t id = 0; id < numInvocations; ++id) {
            const uint32_t i = mapId.f(id, numInvocations);
            expected[static_cast<size_t>(d) * numElements + i / 32u] ^= 1u << (i & 31u);
        }
    }
    if (workgroup) {
        runWorkgroupVariableTest(t, workgroupSize, dispatchSize, numElements, initValue, op, expected, scalarType, extra);
    } else {
        runStorageVariableTest(t, workgroupSize, dispatchSize, numElements, initValue, op, expected, scalarType, extra);
    }
}

CTS_TEST(testGroup, "xor_storage")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runXor(t, false); });

CTS_TEST(testGroup, "xor_workgroup")
    .params([](ParamsBuilder u) { return mapIdParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { runXor(t, true); });

} // namespace
