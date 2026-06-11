// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/render/dynamic_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream has no g.test() calls — stub file; all tests are TODO upstream.

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// NOLINTNEXTLINE(misc-unused-parameters)
TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,render,dynamic_state",
    R"(Tests of the behavior of the viewport/scissor/blend/reference states.

TODO:
- {viewport, scissor rect, blend color, stencil reference}:
  Test rendering result with {various values}.
    - Set the state in different ways to make sure it gets the correct value in the end: {
        - state unset (= default)
        - state explicitly set once to {default value, another value}
        - persistence: [set, draw, draw] (fn should differentiate from [set, draw] + [draw])
        - overwriting: [set(1), draw, set(2), draw] (fn should differentiate from [set(1), set(2), draw, draw])
        - overwriting: [set(1), set(2), draw] (fn should differentiate from [set(1), draw] but not [set(2), draw])
        - }
)");

// No tests: the upstream file has no g.test() definitions (all deferred as TODO).

} // namespace
