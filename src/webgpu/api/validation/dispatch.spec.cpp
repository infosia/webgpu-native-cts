// Ported from gpuweb/cts src/webgpu/api/validation/dispatch.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Both tests require WGSL language feature query (wgpuInstanceGetWGSLLanguageFeatures)
// which is not exposed by the GpuTest harness; marked .unimplemented() accordingly.

#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,dispatch",
    "Compute dispatch validation tests.");

// dispatch,linear_indexing_range
// Tests validation of total invocations for linear_indexing built-in values.
// Upstream: skipIf(!t.hasLanguageFeature('linear_indexing')) — the C WebGPU API
// exposes wgpuInstanceGetWGSLLanguageFeatures / WGPUWGSLLanguageFeatureName_LinearIndexing
// but the GpuTest harness does not surface the WGPUInstance to test bodies, so the
// language-feature guard cannot be implemented without harness changes.
CTS_TEST(g, "dispatch,linear_indexing_range")
    .desc("Tests validation of total invocations for linear_indexing built-in values")
    .params([](ParamsBuilder u) {
        return u
            .combine("builtin", {Value(std::string("global_invocation_index")), Value(std::string("workgroup_index"))})
            .beginSubcases()
            .combine("size", {Value(std::string("max")), Value(std::string("valid"))});
    })
    .unimplemented(
        "requires wgpuInstanceGetWGSLLanguageFeatures / linear_indexing language-feature guard "
        "not exposed by the GpuTest harness");

// dispatchIndirect,linear_indexing_range
// Tests dispatchIndirect skips when linear_indexing is out of range.
// Same language-feature guard issue as above; additionally this is a GPU-execution
// (not pure validation) test that relies on readback of a storage buffer written by
// a shader using the linear_indexing built-in.
CTS_TEST(g, "dispatchIndirect,linear_indexing_range")
    .desc("Tests dispatchIndirect skips when linear_indexing is out of range")
    .params([](ParamsBuilder u) {
        return u
            .combine("builtin", {Value(std::string("global_invocation_index")), Value(std::string("workgroup_index"))})
            .beginSubcases()
            .combine("size", {Value(std::string("max")), Value(std::string("valid"))});
    })
    .unimplemented(
        "requires wgpuInstanceGetWGSLLanguageFeatures / linear_indexing language-feature guard "
        "not exposed by the GpuTest harness");

} // namespace
