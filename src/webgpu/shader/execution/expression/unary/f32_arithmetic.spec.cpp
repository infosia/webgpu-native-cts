// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/f32_arithmetic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the f32 arithmetic unary expression operations. Expression: -x. Accuracy:
// correctly rounded. The negation cases use scalarF32Range({neg_norm:250, neg_sub:20, pos_sub:20,
// pos_norm:250}) (540 values), matching upstream.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,f32_arithmetic",
    "Execution Tests for the f32 arithmetic unary expression operations");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// FP.f32.negationInterval(n).
fp::FPInterval negOp(double n) { return fp::negationInterval(fp::FPKind::F32, n); }

const ExprType F32 = scalarType(ScalarKind::F32);

} // namespace

CTS_TEST(g, "negation").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    // 'unfiltered' for all sources (the cache uses 'unfiltered' regardless of const-ness).
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, fp::scalarF32RangeCounts(250, 20, 20, 250), /*finiteFilter=*/false, negOp);
    run(t, prefixOp("-"), {F32}, F32, cfgInputSource(t), cfgVectorize(t), cases);
});
