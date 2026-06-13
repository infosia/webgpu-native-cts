// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxComputeWorkgroupSizeX.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxComputeWorkgroupSizeXTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxComputeWorkgroupSizeX"; }
};

TestGroup<MaxComputeWorkgroupSizeXTest> testGroup = MakeTestGroup<MaxComputeWorkgroupSizeXTest>(
    "api,validation,capability_checks,limits,maxComputeWorkgroupSizeX",
    "API Validation Tests for maxComputeWorkgroupSizeX.");

CTS_TEST(testGroup, "createComputePipeline,at_over")
    .desc("Test using createComputePipeline(Async) at and over maxComputeWorkgroupSizeX limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("async", {false, true}); })
    .fn([](MaxComputeWorkgroupSizeXTest& t) {
        t.testMaxComputeWorkgroupSize(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            t.param<bool>("async"),
            'X');
    });

CTS_TEST(testGroup, "validate,maxComputeInvocationsPerWorkgroup")
    .desc("Test that maxComputeWorkgroupSizeX <= maxComputeInvocationsPerWorkgroup")
    .fn([](MaxComputeWorkgroupSizeXTest& t) {
        t.expect(t.defaultLimit <= t.getDefaultLimit("maxComputeInvocationsPerWorkgroup"));
        t.expect(t.adapterLimit <= t.getAdapterLimit("maxComputeInvocationsPerWorkgroup"));
    });

} // namespace
