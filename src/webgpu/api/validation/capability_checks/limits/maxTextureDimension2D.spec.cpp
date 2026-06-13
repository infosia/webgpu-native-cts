// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxTextureDimension2D.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxTextureDimension2DTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxTextureDimension2D"; }
};

TestGroup<MaxTextureDimension2DTest> testGroup = MakeTestGroup<MaxTextureDimension2DTest>(
    "api,validation,capability_checks,limits,maxTextureDimension2D",
    "API Validation Tests for maxTextureDimension2D.");

CTS_TEST(testGroup, "createTexture,at_over")
    .desc("Test using at and over maxTextureDimension2D limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxTextureDimension2DTest& t) {
        t.testDeviceWithRequestedMaximumLimits(
            t.param<std::string>("limitTest"),
            t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                for (uint32_t dimensionIndex = 0; dimensionIndex < 2; ++dimensionIndex) {
                    t.testForValidationErrorWithPossibleOutOfMemoryError([&] {
                        WGPUExtent3D size = {1, 1, 1};
                        if (dimensionIndex == 0) {
                            size.width = static_cast<uint32_t>(inputs.testValue);
                        } else {
                            size.height = static_cast<uint32_t>(inputs.testValue);
                        }
                        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                        desc.size = size;
                        desc.format = WGPUTextureFormat_RGBA8Unorm;
                        desc.usage = WGPUTextureUsage_TextureBinding;
                        WGPUTexture texture = t.createTextureTracked(desc);
                        if (!inputs.shouldError && texture != nullptr) {
                            wgpuTextureDestroy(texture);
                        }
                    }, inputs.shouldError);
                }
            });
    });

CTS_TEST(testGroup, "configure,at_over")
    .desc("Test using at and over maxTextureDimension2D limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .unimplemented("GPUCanvasContext is a Web API with no native WebGPU C equivalent");

CTS_TEST(testGroup, "getCurrentTexture,at_over")
    .desc("Test using at and over maxTextureDimension2D limit")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .unimplemented("GPUCanvasContext is a Web API with no native WebGPU C equivalent");

} // namespace
