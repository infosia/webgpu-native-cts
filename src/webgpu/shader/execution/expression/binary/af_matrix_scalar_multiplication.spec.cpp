// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/af_matrix_scalar_multiplication.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix-scalar and scalar-matrix abstract-float multiplication. Accuracy:
// correctly rounded (abstract-float, computed natively at f64). selectNCases trims to 50.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,af_matrix_scalar_multiplication",
    "Execution Tests for matrix-scalar and scalar-matrix abstract-float multiplication expression");

ParamsBuilder sourceColsRows(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}

std::vector<std::vector<std::vector<double>>> sparseMatrixAFValues(int cols, int rows) {
    std::vector<std::vector<std::vector<double>>> out;
    for (double f : fp::sparseScalarF64Range()) {
        std::vector<std::vector<double>> m(static_cast<size_t>(cols),
                                           std::vector<double>(static_cast<size_t>(rows), f));
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<Case> matScalarCases(int cols, int rows) {
    auto op = [](const std::vector<std::vector<double>>& m, double s) {
        return fp::multiplicationMatrixScalarInterval(fp::FPKind::Abstract, m, s);
    };
    auto cases = fp::generateMatrixScalarToMatrixCases(
        fp::FPKind::Abstract, sparseMatrixAFValues(cols, rows), fp::sparseScalarF64Range(),
        /*finite=*/true, op);
    return fp::selectNCases("binary/af_matrix_scalar_multiplication_mat_scalar", 50, cases);
}
std::vector<Case> scalarMatCases(int cols, int rows) {
    auto op = [](double s, const std::vector<std::vector<double>>& m) {
        return fp::multiplicationMatrixScalarInterval(fp::FPKind::Abstract, m, s);
    };
    auto cases = fp::generateScalarMatrixToMatrixCases(
        fp::FPKind::Abstract, fp::sparseScalarF64Range(), sparseMatrixAFValues(cols, rows),
        /*finite=*/true, op);
    return fp::selectNCases("binary/af_matrix_scalar_multiplication_scalar_mat", 50, cases);
}

} // namespace

CTS_TEST(g, "matrix_scalar").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::AbstractFloat);
    const ExprType AF = scalarType(ScalarKind::AbstractFloat);
    auto cases = matScalarCases(cols, rows);
    run(t, binaryOp("*"), {mt, AF}, mt, InputSource::Const, 0, cases);
});

CTS_TEST(g, "scalar_matrix").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::AbstractFloat);
    const ExprType AF = scalarType(ScalarKind::AbstractFloat);
    auto cases = scalarMatCases(cols, rows);
    run(t, binaryOp("*"), {AF, mt}, mt, InputSource::Const, 0, cases);
});
