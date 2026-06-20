// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/ai_complement.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the abstract-int bitwise complement operation.

#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/unary/unary_ranges_common.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::unary_ranges;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,ai_complement",
    "Execution Tests for the abstract-int bitwise complement operation");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

CTS_TEST(testGroup, "complement").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int64_t e : fullI64Range()) {
        cases.push_back({{CaseValue(abstractInt64(e))}, CaseValue(abstractInt64(~e))});
    }
    const ExprType AI = scalarType(ScalarKind::AbstractInt);
    const int vec = t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
    run(t, prefixOp("~"), {AI}, AI, InputSource::Const, vec, cases);
});

} // namespace
