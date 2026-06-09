// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/setImmediates.spec.ts
// STUBBED: wgpuComputePassEncoderSetImmediates / wgpuRenderPassEncoderSetImmediates /
//   wgpuRenderBundleEncoderSetImmediates are declared in webgpu-headers but not exported
//   by any of the three backends (yawgpu, wgpu-native, Dawn) at this revision.
//   Stubbed with .unimplemented() to unblock the batch build.
//   Re-enable when the backends ship the SetImmediates entry points.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,setImmediates",
    "setImmediates validation tests — STUBBED pending backend SetImmediates support.");

CTS_TEST(g, "alignment")
    .desc("STUBBED: wgpuXxxSetImmediates not exported by any backend.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        t.skip("setImmediates not implemented in any backend");
    });

CTS_TEST(g, "overflow")
    .desc("STUBBED: wgpuXxxSetImmediates not exported by any backend.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        t.skip("setImmediates not implemented in any backend");
    });

CTS_TEST(g, "out_of_bounds")
    .desc("STUBBED: wgpuXxxSetImmediates not exported by any backend.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        t.skip("setImmediates not implemented in any backend");
    });

} // namespace
