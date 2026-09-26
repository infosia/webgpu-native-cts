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
    .fn([](FeatureGpuTest& t) {
        // WGPUFeatureName_SubgroupSizeControl is in the Dawn and yawgpu headers but not
        // in wgpu-native's; there the feature cannot be requested, so the case skips.
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
        t.selectDeviceOrSkipTestCase({WGPUFeatureName_SubgroupSizeControl});
        t.expect(hasFeature(t.device(), WGPUFeatureName_Subgroups),
                 "device with subgroup-size-control must also have subgroups");
#else
        t.skip("subgroup-size-control is not in this backend's webgpu.h");
#endif
    });

} // namespace
