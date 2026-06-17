// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicMin.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicMin",
    "Atomically read, min and store value.");

CTS_TEST(testGroup, "min_storage")
    .params([](ParamsBuilder u) { return basicParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const uint32_t initValue = scalarType == ScalarType::U32 ? 0xffffffffu : 0x7fffffffu;
        std::vector<uint32_t> expected(2, initValue);
        expected[0] = 0;
        runStorageVariableTest(t, workgroupSize, dispatchSize, 2, initValue, "atomicMin(&output[0], id)", expected, scalarType);
    });

CTS_TEST(testGroup, "min_workgroup")
    .params([](ParamsBuilder u) { return basicParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const uint32_t initValue = scalarType == ScalarType::U32 ? 0xffffffffu : 0x7fffffffu;
        std::vector<uint32_t> expected(static_cast<size_t>(2 * dispatchSize), initValue);
        for (int d = 0; d < dispatchSize; ++d) {
            expected[static_cast<size_t>(d) * 2] = 0;
        }
        runWorkgroupVariableTest(t, workgroupSize, dispatchSize, 2, initValue, "atomicMin(&wg[0], id)", expected, scalarType);
    });

} // namespace
