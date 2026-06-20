// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/af_comparison.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the abstract-float comparison operations. The result is a bool, so the value
// is exact (FP comparison is exact); subnormal-flush ambiguity is encoded via the anyOf rule.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,af_comparison",
    "Execution Tests for the abstract-float comparison operations");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

const ExprType AF = scalarType(ScalarKind::AbstractFloat);
const ExprType BOOL = scalarType(ScalarKind::Bool);

void runComparison(AllFeaturesMaxLimitsGpuTest& t, const char* op, const fp::TruthFunc& truthFunc) {
    auto cases = fp::generateComparisonCases(fp::FPKind::Abstract, fp::vectorF64Range(2), truthFunc);
    run(t, binaryOp(op), {AF, AF}, BOOL, InputSource::Const, cfgVectorize(t), cases);
}

} // namespace

CTS_TEST(g, "equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runComparison(t, "==", [](double lhs, double rhs) { return lhs == rhs; });
});

CTS_TEST(g, "not_equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runComparison(t, "!=", [](double lhs, double rhs) { return lhs != rhs; });
});

CTS_TEST(g, "less_than").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runComparison(t, "<", [](double lhs, double rhs) { return lhs < rhs; });
});

CTS_TEST(g, "less_equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runComparison(t, "<=", [](double lhs, double rhs) { return lhs <= rhs; });
});

CTS_TEST(g, "greater_than").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runComparison(t, ">", [](double lhs, double rhs) { return lhs > rhs; });
});

CTS_TEST(g, "greater_equals").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runComparison(t, ">=", [](double lhs, double rhs) { return lhs >= rhs; });
});
