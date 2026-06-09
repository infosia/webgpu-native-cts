// Ported from gpuweb/cts src/webgpu/api/validation/query_set/create.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// kMaxQueryCount mirrors upstream kMaxQueryCount = 4096.
// kQueryTypes mirrors upstream kQueryTypes = ['occlusion', 'timestamp'].
constexpr int kMaxQueryCount = 4096;

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,query_set,create",
    "Tests for validation in createQuerySet.");

CTS_TEST(g, "count")
    .desc(
        "Tests that create query set with the count for all query types:\n"
        "- count {<, =, >} kMaxQueryCount\n"
        "- x= {occlusion, timestamp} query")
    .params([](ParamsBuilder u) {
        return u
            .combine("type", {"occlusion", "timestamp"})
            .beginSubcases()
            .combine("count", {0, kMaxQueryCount, kMaxQueryCount + 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string type = t.param<std::string>("type");
        const int count = t.param<int>("count");

        // Skip timestamp query if the device does not support the timestamp-query feature.
        if (type == "timestamp") {
            if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_TimestampQuery)) {
                t.skip("timestamp-query feature not available");
            }
        }

        const WGPUQueryType queryType =
            (type == "occlusion") ? WGPUQueryType_Occlusion : WGPUQueryType_Timestamp;
        const bool shouldError = count > kMaxQueryCount;

        t.expectValidationError([&] {
            WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
            desc.type = queryType;
            desc.count = static_cast<uint32_t>(count);
            WGPUQuerySet qs = wgpuDeviceCreateQuerySet(t.device(), &desc);
            if (qs != nullptr) {
                wgpuQuerySetRelease(qs);
            }
        }, shouldError);
    });

} // namespace
