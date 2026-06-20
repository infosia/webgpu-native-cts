// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/bool_logical.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the boolean unary logical expression operations (logical negation).

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,bool_logical",
    "Execution Tests for the boolean unary logical expression operations");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

CTS_TEST(testGroup, "negation").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases = {
        {{CaseValue(boolean(true))}, CaseValue(boolean(false))},
        {{CaseValue(boolean(false))}, CaseValue(boolean(true))},
    };
    const ExprType BOOL = scalarType(ScalarKind::Bool);
    const InputSource src = inputSourceFromParam(t.param<std::string>("inputSource"));
    const int vec = t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
    run(t, prefixOp("!"), {BOOL}, BOOL, src, vec, cases);
});

} // namespace
