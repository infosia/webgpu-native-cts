// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/i32_arithmetic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the i32 arithmetic unary expression operations (negation).

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/unary/unary_ranges_common.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::unary_ranges;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,i32_arithmetic",
    "Execution Tests for the i32 arithmetic unary expression operations");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

CTS_TEST(testGroup, "negation").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t e : fullI32Range()) {
        const int32_t neg = static_cast<int32_t>(0u - static_cast<uint32_t>(e));
        cases.push_back({{CaseValue(i32(e))}, CaseValue(i32(neg))});
    }
    const ExprType I32 = scalarType(ScalarKind::I32);
    const InputSource src = inputSourceFromParam(t.param<std::string>("inputSource"));
    const int vec = t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
    run(t, prefixOp("-"), {I32}, I32, src, vec, cases);
});

} // namespace
