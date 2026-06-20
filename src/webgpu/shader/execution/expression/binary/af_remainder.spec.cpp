// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/af_remainder.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for non-matrix abstract-float remainder (x % y). Remainder has inherited
// accuracy, so abstract is only expected to be as accurate as f32: the acceptance interval is the
// f32 remainder interval, materialized as abstract-float.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,af_remainder",
    "Execution Tests for non-matrix abstract-float remainder expression");

ParamsBuilder sourceConst(ParamsBuilder u) { return u.combine("inputSource", {"const"}); }
ParamsBuilder sourceConstVectorize(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))});
}
ParamsBuilder sourceConstDim(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("dim", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                         Value(static_cast<int64_t>(4))});
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// Remainder has an inherited accuracy, so abstract is only expected to be as accurate as f32.
fp::FPInterval remOp(double x, double y) { return fp::remainderInterval(fp::FPKind::F32, x, y); }

std::vector<Case> scalarCases() {
    return fp::generateScalarPairToIntervalCases(fp::FPKind::Abstract, fp::sparseScalarF64Range(),
                                                 fp::sparseScalarF64Range(), /*finite=*/true, remOp);
}

const ExprType AF = scalarType(ScalarKind::AbstractFloat);

} // namespace

CTS_TEST(g, "scalar").params(sourceConst).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = scalarCases();
    run(t, binaryOp("%"), {AF, AF}, AF, InputSource::Const, 0, cases);
});

CTS_TEST(g, "vector").params(sourceConstVectorize).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = scalarCases();
    run(t, binaryOp("%"), {AF, AF}, AF, InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "vector_scalar").params(sourceConstDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const ExprType vt = vecType(dim, ScalarKind::AbstractFloat);
    auto cases = fp::generateVectorScalarToVectorCases(
        fp::FPKind::Abstract, fp::sparseVectorF64Range(dim), fp::sparseScalarF64Range(),
        /*finite=*/true, remOp);
    run(t, binaryOp("%"), {vt, AF}, vt, InputSource::Const, 0, cases);
});

CTS_TEST(g, "scalar_vector").params(sourceConstDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const ExprType vt = vecType(dim, ScalarKind::AbstractFloat);
    auto cases = fp::generateScalarVectorToVectorCases(
        fp::FPKind::Abstract, fp::sparseScalarF64Range(), fp::sparseVectorF64Range(dim),
        /*finite=*/true, remOp);
    run(t, binaryOp("%"), {AF, vt}, vt, InputSource::Const, 0, cases);
});
