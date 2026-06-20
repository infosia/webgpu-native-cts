// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/af_matrix_matrix_multiplication.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix-matrix abstract-float multiplication (x * y). Matrix-matrix
// multiplication has inherited accuracy, so abstract is only as accurate as f32: the acceptance
// interval is computed with the f32 trait but materialized as abstract-float. selectNCases trims 10.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,af_matrix_matrix_multiplication",
    "Execution Tests for matrix-matrix abstract-float multiplication expression");

ParamsBuilder sourceMatrixMatrix(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("common_dim", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                Value(static_cast<int64_t>(4))})
        .combine("x_rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                            Value(static_cast<int64_t>(4))})
        .combine("y_cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                            Value(static_cast<int64_t>(4))});
}

std::vector<Case> matMatCases(int xCols, int xRows, int yCols, int yRows) {
    auto op = [](const std::vector<std::vector<double>>& x,
                 const std::vector<std::vector<double>>& y) {
        return fp::multiplicationMatrixMatrixInterval(fp::FPKind::F32, x, y);
    };
    auto cases = fp::generateMatrixPairToMatrixCases(
        fp::FPKind::Abstract, fp::sparseMatrixF64Range(xCols, xRows),
        fp::sparseMatrixF64Range(yCols, yRows), /*finite=*/true, op);
    return fp::selectNCases("binary/af_matrix_matrix_multiplication", 10, cases);
}

} // namespace

CTS_TEST(g, "matrix_matrix").params(sourceMatrixMatrix).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int commonDim = static_cast<int>(t.param<int64_t>("common_dim"));
    const int xRows = static_cast<int>(t.param<int64_t>("x_rows"));
    const int yCols = static_cast<int>(t.param<int64_t>("y_cols"));
    const int xCols = commonDim;
    const int yRows = commonDim;
    const ExprType xt = matType(xCols, xRows, ScalarKind::AbstractFloat);
    const ExprType yt = matType(yCols, yRows, ScalarKind::AbstractFloat);
    const ExprType rt = matType(yCols, xRows, ScalarKind::AbstractFloat);
    auto cases = matMatCases(xCols, xRows, yCols, yRows);
    run(t, binaryOp("*"), {xt, yt}, rt, InputSource::Const, 0, cases);
});
