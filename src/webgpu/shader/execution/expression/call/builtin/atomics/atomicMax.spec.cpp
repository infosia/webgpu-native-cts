// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicMax.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicMax",
    "Atomically read, max and store value.");

CTS_TEST(testGroup, "max_storage")
    .params([](ParamsBuilder u) { return basicParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        std::vector<uint32_t> expected(2, 0);
        expected[0] = static_cast<uint32_t>(workgroupSize * dispatchSize - 1);
        runStorageVariableTest(t, workgroupSize, dispatchSize, 2, 0, "atomicMax(&output[0], id)", expected, scalarType);
    });

CTS_TEST(testGroup, "max_workgroup")
    .params([](ParamsBuilder u) { return basicParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        std::vector<uint32_t> expected(static_cast<size_t>(2 * dispatchSize), 0);
        for (int d = 0; d < dispatchSize; ++d) {
            expected[static_cast<size_t>(d) * 2] = static_cast<uint32_t>(workgroupSize - 1);
        }
        runWorkgroupVariableTest(t, workgroupSize, dispatchSize, 2, 0, "atomicMax(&wg[0], id)", expected, scalarType);
    });

} // namespace
