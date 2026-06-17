// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/atomics/atomicSub.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"

using namespace cts;
using namespace cts::atomics;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,atomics,atomicSub",
    "Atomically read, subtract and store value.");

CTS_TEST(testGroup, "sub_storage")
    .params([](ParamsBuilder u) { return basicParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        const uint32_t numInvocations = static_cast<uint32_t>(workgroupSize * dispatchSize);
        std::vector<uint32_t> expected(2, 0);
        expected[0] = 0u - numInvocations;
        runStorageVariableTest(t, workgroupSize, dispatchSize, 2, 0, "atomicSub(&output[0], 1)", expected, scalarType);
    });

CTS_TEST(testGroup, "sub_workgroup")
    .params([](ParamsBuilder u) { return basicParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int workgroupSize = t.param<int64_t>("workgroupSize");
        const int dispatchSize = t.param<int64_t>("dispatchSize");
        const ScalarType scalarType = scalarTypeFromParam(t.param<std::string>("scalarType"));
        std::vector<uint32_t> expected(static_cast<size_t>(2 * dispatchSize), 0);
        for (int d = 0; d < dispatchSize; ++d) {
            expected[static_cast<size_t>(d) * 2] = 0u - static_cast<uint32_t>(workgroupSize);
        }
        runWorkgroupVariableTest(t, workgroupSize, dispatchSize, 2, 0, "atomicSub(&wg[0], 1)", expected, scalarType);
    });

CTS_TEST(testGroup, "sub_i32_min")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<uint32_t> expected(2, 0xffffu);
        expected[0] = bits(static_cast<int32_t>(-0x7fff0001));
        runStorageVariableTest(t, 1, 1, 2, 0xffffu, "atomicSub(&output[0], -2147483648)", expected, ScalarType::I32);
    });

} // namespace
