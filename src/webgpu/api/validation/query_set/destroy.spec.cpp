// Ported from gpuweb/cts src/webgpu/api/validation/query_set/destroy.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,query_set,destroy",
    "Destroying a query set more than once is allowed.");

// g.test('twice')
// Destroying a query set more than once is allowed.
CTS_TEST(g, "twice")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type = WGPUQueryType_Occlusion;
        desc.count = 1;
        WGPUQuerySet qset = wgpuDeviceCreateQuerySet(t.device(), &desc);

        wgpuQuerySetDestroy(qset);
        wgpuQuerySetDestroy(qset);

        wgpuQuerySetRelease(qset);
    });

// g.test('invalid_queryset')
// Test that invalid querysets may be destroyed without generating validation errors.
CTS_TEST(g, "invalid_queryset")
    .desc("Test that invalid querysets may be destroyed without generating validation errors.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Create a query set with count=4097, which exceeds the default limit of 4096.
        // This should generate a validation error.
        WGPUQuerySet invalidQuerySet = nullptr;
        t.expectValidationError([&] {
            WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
            desc.type = WGPUQueryType_Occlusion;
            desc.count = 4097; // 4096 is the limit
            invalidQuerySet = wgpuDeviceCreateQuerySet(t.device(), &desc);
        }, true);

        // This line should not generate an error even though the query set is invalid.
        if (invalidQuerySet != nullptr) {
            wgpuQuerySetDestroy(invalidQuerySet);
            wgpuQuerySetRelease(invalidQuerySet);
        }
    });

} // namespace
