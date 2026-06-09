// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/render_pass.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// NOTE: The upstream file at this commit is a stub (no test cases) — only the
// group registration is present. The TODO in the upstream mentions:
//   executeBundles: with {zero, one, multiple} bundles where {zero, one} of them are invalid objects
// These tests have not been written upstream and are therefore not ported here.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,render_pass",
    "Validation tests for render pass encoding.\n"
    "Does **not** test usage scopes (resource_usages/), GPUProgrammablePassEncoder (programmable_pass),\n"
    "dynamic state (dynamic_render_state.spec.ts), or GPURenderEncoderBase (render.spec.ts).\n"
    "\n"
    "TODO:\n"
    "- executeBundles:\n"
    "    - with {zero, one, multiple} bundles where {zero, one} of them are invalid objects");

// No test cases: the upstream spec at this commit is a stub with no test
// functions defined.  When upstream adds tests, port them here.

} // namespace
