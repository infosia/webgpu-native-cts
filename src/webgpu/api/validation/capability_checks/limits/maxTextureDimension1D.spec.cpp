// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxTextureDimension1D.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxTextureDimension1DTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxTextureDimension1D"; }
};

TestGroup<MaxTextureDimension1DTest> testGroup = MakeTestGroup<MaxTextureDimension1DTest>(
    "api,validation,capability_checks,limits,maxTextureDimension1D",
    "API Validation Tests for maxTextureDimension1D.");

CTS_TEST(testGroup, "createTexture,at_over")
    .desc("Test using at and over maxTextureDimension1D limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxTextureDimension1DTest& t) {
        t.testDeviceWithRequestedMaximumLimits(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                t.testForValidationErrorWithPossibleOutOfMemoryError([&] {
                    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                    desc.size = {static_cast<uint32_t>(inputs.testValue), 1, 1};
                    desc.format = WGPUTextureFormat_RGBA8Unorm;
                    desc.dimension = WGPUTextureDimension_1D;
                    desc.usage = WGPUTextureUsage_TextureBinding;
                    WGPUTexture texture = t.createTextureTracked(desc);
                    if (!inputs.shouldError && texture != nullptr) {
                        wgpuTextureDestroy(texture);
                    }
                }, inputs.shouldError);
            });
    });

} // namespace
