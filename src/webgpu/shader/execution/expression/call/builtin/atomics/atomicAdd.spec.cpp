// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicAdd.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicAdd",
    "Atomically read, add and store value.");

CTS_TEST(testGroup, "add_storage")
    .params([](ParamsBuilder u) { return basicParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = static_cast<int>(t.param<int64_t>("workgroupSize"));
        const int dispatchSize = static_cast<int>(t.param<int64_t>("dispatchSize"));
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const uint32_t numInvocations = static_cast<uint32_t>(workgroupSize * dispatchSize);
        std::vector<uint32_t> expected(2, 0);
        expected[0] = numInvocations;
        runStorageVariableTest(t, workgroupSize, dispatchSize, 2, 0, "atomicAdd(&output[0], 1)", expected, scalarType);
    });

CTS_TEST(testGroup, "add_workgroup")
    .params([](ParamsBuilder u) { return basicParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = static_cast<int>(t.param<int64_t>("workgroupSize"));
        const int dispatchSize = static_cast<int>(t.param<int64_t>("dispatchSize"));
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        std::vector<uint32_t> expected(static_cast<size_t>(2 * dispatchSize), 0);
        for (int d = 0; d < dispatchSize; ++d) {
            expected[static_cast<size_t>(d) * 2] = static_cast<uint32_t>(workgroupSize);
        }
        runWorkgroupVariableTest(t, workgroupSize, dispatchSize, 2, 0, "atomicAdd(&wg[0], 1)", expected, scalarType);
    });

} // namespace
