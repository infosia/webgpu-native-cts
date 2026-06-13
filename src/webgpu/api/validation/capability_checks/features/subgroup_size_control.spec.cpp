// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/features/subgroup_size_control.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause

#include "cts/test.h"
#include "feature_test_helpers.h"

using namespace cts;
using namespace cts::capability_features;

namespace {

TestGroup<FeatureGpuTest> testGroup = MakeTestGroup<FeatureGpuTest>(
    "api,validation,capability_checks,features,subgroup_size_control",
    "Tests for capability checking for the 'subgroup-size-control' feature.");

CTS_TEST(testGroup, "enables_subgroups")
    .desc("Test that enabling subgroup-size-control also enables subgroups.")
    .unimplemented("no native WGPUFeatureName for subgroup-size-control in the pinned header");

} // namespace
