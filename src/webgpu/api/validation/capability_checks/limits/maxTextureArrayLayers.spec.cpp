// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxTextureArrayLayers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxTextureArrayLayersTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxTextureArrayLayers"; }
};

TestGroup<MaxTextureArrayLayersTest> testGroup = MakeTestGroup<MaxTextureArrayLayersTest>(
    "api,validation,capability_checks,limits,maxTextureArrayLayers",
    "API Validation Tests for maxTextureArrayLayers.");

CTS_TEST(testGroup, "createTexture,at_over")
    .desc("Test using at and over maxTextureArrayLayers limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxTextureArrayLayersTest& t) {
        t.testDeviceWithRequestedMaximumLimits(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                t.testForValidationErrorWithPossibleOutOfMemoryError([&] {
                    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                    desc.size = {1, 1, static_cast<uint32_t>(inputs.testValue)};
                    desc.format = WGPUTextureFormat_RGBA8Unorm;
                    desc.usage = WGPUTextureUsage_TextureBinding;
                    WGPUTexture texture = t.createTextureTracked(desc);
                    if (!inputs.shouldError && texture != nullptr) {
                        wgpuTextureDestroy(texture);
                    }
                }, inputs.shouldError);
            });
    });

} // namespace
