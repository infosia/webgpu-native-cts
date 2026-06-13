// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxBufferSize.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxBufferSizeTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxBufferSize"; }
};

TestGroup<MaxBufferSizeTest> testGroup = MakeTestGroup<MaxBufferSizeTest>(
    "api,validation,capability_checks,limits,maxBufferSize",
    "API Validation Tests for maxBufferSize.");

CTS_TEST(testGroup, "createBuffer,at_over")
    .desc("Test using at and over maxBufferSize limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxBufferSizeTest& t) {
        t.testDeviceWithRequestedMaximumLimits(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                t.testForValidationErrorWithPossibleOutOfMemoryError([&] {
                    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
                    desc.usage = WGPUBufferUsage_Vertex;
                    desc.size = inputs.testValue;
                    t.createBufferTracked(desc);
                }, inputs.shouldError);
            });
    });

} // namespace
