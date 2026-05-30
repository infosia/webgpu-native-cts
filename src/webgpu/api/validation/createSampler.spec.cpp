// Ported from gpuweb/cts src/webgpu/api/validation/createSampler.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,createSampler",
    "createSampler validation tests.");

CTS_TEST(g, "lodMinAndMaxClamp")
    .desc("test different combinations of min and max clamp values")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("lodMinClamp", {-4e-30, -1, 0, 0.5, 1, 10, 4e30})
            .combine("lodMaxClamp", {-4e-30, -1, 0, 0.5, 1, 10, 4e30});
    })
    .fn([](GpuTest& t) {
        const double lodMinClamp = t.param<double>("lodMinClamp");
        const double lodMaxClamp = t.param<double>("lodMaxClamp");
        const bool shouldError = lodMinClamp > lodMaxClamp || lodMinClamp < 0 || lodMaxClamp < 0;

        t.expectValidationError([&] {
            WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
            desc.lodMinClamp = static_cast<float>(lodMinClamp);
            desc.lodMaxClamp = static_cast<float>(lodMaxClamp);
            t.createSamplerTracked(desc);
        }, shouldError);
    });

CTS_TEST(g, "maxAnisotropy")
    .desc("test different maxAnisotropy values and combinations with min/mag/mipmapFilter")
    .unimplemented("relies on WebIDL number coercion with no faithful C uint16 representation");

} // namespace
