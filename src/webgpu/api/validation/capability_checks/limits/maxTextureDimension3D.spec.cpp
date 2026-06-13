// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxTextureDimension3D.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxTextureDimension3DTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxTextureDimension3D"; }
};

TestGroup<MaxTextureDimension3DTest> testGroup = MakeTestGroup<MaxTextureDimension3DTest>(
    "api,validation,capability_checks,limits,maxTextureDimension3D",
    "API Validation Tests for maxTextureDimension3D.");

CTS_TEST(testGroup, "createTexture,at_over")
    .desc("Test using at and over maxTextureDimension3D limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxTextureDimension3DTest& t) {
        t.testDeviceWithRequestedMaximumLimits(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                for (uint32_t dimensionIndex = 0; dimensionIndex < 3; ++dimensionIndex) {
                    t.testForValidationErrorWithPossibleOutOfMemoryError([&] {
                        WGPUExtent3D size = {2, 2, 2};
                        if (dimensionIndex == 0) size.width = static_cast<uint32_t>(inputs.testValue);
                        if (dimensionIndex == 1) size.height = static_cast<uint32_t>(inputs.testValue);
                        if (dimensionIndex == 2) size.depthOrArrayLayers = static_cast<uint32_t>(inputs.testValue);
                        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                        desc.size = size;
                        desc.format = WGPUTextureFormat_RGBA8Unorm;
                        desc.dimension = WGPUTextureDimension_3D;
                        desc.usage = WGPUTextureUsage_TextureBinding;
                        WGPUTexture texture = t.createTextureTracked(desc);
                        if (!inputs.shouldError && texture != nullptr) {
                            wgpuTextureDestroy(texture);
                        }
                    }, inputs.shouldError);
                }
            });
    });

} // namespace
