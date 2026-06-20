// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/af_matrix_vector_multiplication.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix-vector and vector-matrix abstract-float multiplication. Matrix-vector
// multiplication has inherited accuracy, so abstract is only as accurate as f32: the acceptance
// interval is computed with the f32 trait but materialized as abstract-float. selectNCases trims 50.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,af_matrix_vector_multiplication",
    "Execution Tests for matrix-vector and vector-matrix abstract-float multiplication expression");

ParamsBuilder sourceColsRows(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}

std::vector<Case> matVecCases(int cols, int rows) {
    auto op = [](const std::vector<std::vector<double>>& m, const std::vector<double>& v) {
        // Inherited accuracy: compute with the f32 trait.
        return fp::multiplicationMatrixVectorInterval(fp::FPKind::F32, m, v);
    };
    auto cases = fp::generateMatrixVectorToVectorCases(
        fp::FPKind::Abstract, fp::sparseMatrixF64Range(cols, rows), fp::sparseVectorF64Range(cols),
        /*finite=*/true, op);
    return fp::selectNCases("binary/af_matrix_vector_multiplication_mat_vec", 50, cases);
}
std::vector<Case> vecMatCases(int cols, int rows) {
    auto op = [](const std::vector<double>& v, const std::vector<std::vector<double>>& m) {
        return fp::multiplicationVectorMatrixInterval(fp::FPKind::F32, v, m);
    };
    auto cases = fp::generateVectorMatrixToVectorCases(
        fp::FPKind::Abstract, fp::sparseVectorF64Range(rows), fp::sparseMatrixF64Range(cols, rows),
        /*finite=*/true, op);
    return fp::selectNCases("binary/af_matrix_vector_multiplication_vec_mat", 50, cases);
}

} // namespace

CTS_TEST(g, "matrix_vector").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::AbstractFloat);
    const ExprType vIn = vecType(cols, ScalarKind::AbstractFloat);
    const ExprType vOut = vecType(rows, ScalarKind::AbstractFloat);
    auto cases = matVecCases(cols, rows);
    run(t, binaryOp("*"), {mt, vIn}, vOut, InputSource::Const, 0, cases);
});

CTS_TEST(g, "vector_matrix").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::AbstractFloat);
    const ExprType vIn = vecType(rows, ScalarKind::AbstractFloat);
    const ExprType vOut = vecType(cols, ScalarKind::AbstractFloat);
    auto cases = vecMatCases(cols, rows);
    run(t, binaryOp("*"), {vIn, mt}, vOut, InputSource::Const, 0, cases);
});
