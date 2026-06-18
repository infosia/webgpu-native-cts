// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/fwidthCoarse.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'fwidthCoarse' builtin: abs(dpdxCoarse(e)) + abs(dpdyCoarse(e)).

#include "webgpu/shader/execution/expression/call/builtin/derivatives.h"

using namespace cts;

namespace {

const char* kBuiltin = "fwidthCoarse";

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,fwidthCoarse",
    "Execution tests for the 'fwidthCoarse' builtin function.");

CTS_TEST(testGroup, "f32")
    .params([](ParamsBuilder u) {
        return u
            .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                                   Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))})
            .combine("non_uniform_discard", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int vectorize =
            t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
        const bool non_uniform_discard = t.param<bool>("non_uniform_discard");
        derivatives::runFWidthTest(t, kBuiltin, non_uniform_discard, vectorize);
    });

} // namespace
