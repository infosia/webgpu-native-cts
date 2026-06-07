// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/vertex_only_render_pipeline.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream registers the single test as `.unimplemented()` (a TODO stub with no body); this port mirrors
// that so the suite catalog stays faithful.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pipeline,vertex_only_render_pipeline",
    "Test vertex-only render pipeline.");

CTS_TEST(g, "draw_depth_and_stencil_with_vertex_only_pipeline")
    .unimplemented("vertex-only render pipeline depth/stencil draw is unimplemented upstream (TODO stub)");

} // namespace
