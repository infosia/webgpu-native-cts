// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/ai_comparison.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the abstract-int comparison expressions (const-eval only).

#include <cstdint>
#include <functional>
#include <vector>

#include "webgpu/shader/execution/expression/binary/binary_ops_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::binary_ops;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,ai_comparison",
    "Execution Tests for the abstract-int comparison expressions");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

const ExprType AI = scalarType(ScalarKind::AbstractInt);
const ExprType BOOL = scalarType(ScalarKind::Bool);

using Cmp = std::function<bool(int64_t, int64_t)>;

void runCmp(AllFeaturesMaxLimitsGpuTest& t, const std::string& op, const Cmp& cmp) {
    std::vector<Case> cases;
    for (const std::vector<int64_t>& v : vectorI64Range(2)) {
        cases.push_back({{CaseValue(abstractInt64(v[0])), CaseValue(abstractInt64(v[1]))},
                         CaseValue(boolean(cmp(v[0], v[1])))});
    }
    run(t, binaryOp(op), {AI, AI}, BOOL, InputSource::Const, cfgVectorize(t), cases);
}

CTS_TEST(testGroup, "equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCmp(t, "==", [](int64_t a, int64_t b) { return a == b; });
});
CTS_TEST(testGroup, "not_equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCmp(t, "!=", [](int64_t a, int64_t b) { return a != b; });
});
CTS_TEST(testGroup, "less_than").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCmp(t, "<", [](int64_t a, int64_t b) { return a < b; });
});
CTS_TEST(testGroup, "less_equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCmp(t, "<=", [](int64_t a, int64_t b) { return a <= b; });
});
CTS_TEST(testGroup, "greater_than").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCmp(t, ">", [](int64_t a, int64_t b) { return a > b; });
});
CTS_TEST(testGroup, "greater_equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCmp(t, ">=", [](int64_t a, int64_t b) { return a >= b; });
});

} // namespace
