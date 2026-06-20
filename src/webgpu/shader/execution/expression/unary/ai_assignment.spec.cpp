// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/ai_assignment.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for assignment of AbstractInts (to abstract-int / i32 / u32).

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/unary/unary_ranges_common.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::unary_ranges;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,ai_assignment",
    "Execution Tests for assignment of AbstractInts");

ParamsBuilder constParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"});
}

// Identity assignment expression: '(value)'.
ExpressionBuilder identity() {
    return prefixOp("");
}

const ExprType AI = scalarType(ScalarKind::AbstractInt);

CTS_TEST(testGroup, "abstract").params(constParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int64_t n : fullI64Range()) {
        cases.push_back({{CaseValue(abstractInt64(n))}, CaseValue(abstractInt64(n))});
    }
    run(t, identity(), {AI}, AI, InputSource::Const, 0, cases);
});
CTS_TEST(testGroup, "i32").params(constParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t n : fullI32Range()) {
        cases.push_back({{CaseValue(abstractInt64(static_cast<int64_t>(n)))}, CaseValue(i32(n))});
    }
    run(t, identity(), {AI}, scalarType(ScalarKind::I32), InputSource::Const, 0, cases);
});
CTS_TEST(testGroup, "u32").params(constParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t n : fullU32Range()) {
        cases.push_back({{CaseValue(abstractInt64(static_cast<int64_t>(n)))}, CaseValue(u32(n))});
    }
    run(t, identity(), {AI}, scalarType(ScalarKind::U32), InputSource::Const, 0, cases);
});

} // namespace
