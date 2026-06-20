// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/af_matrix_addition.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix abstract-float addition (x + y, componentwise). Accuracy: correctly
// rounded (abstract-float, computed natively at f64). The matrix input range uses the AF-simplified
// kSparseMatrixAFValues (each interesting f64 value replicated across the matrix), and selectNCases
// trims to 50 cases per (cols, rows), matching upstream.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,af_matrix_addition",
    "Execution Tests for matrix abstract-float addition expression");

ParamsBuilder sourceColsRows(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}

// kSparseMatrixAFValues[cols][rows]: for each f in sparseScalarF64Range(), a cols x rows matrix with
// every element = f (column-major m[col][row]).
std::vector<std::vector<std::vector<double>>> sparseMatrixAFValues(int cols, int rows) {
    std::vector<std::vector<std::vector<double>>> out;
    for (double f : fp::sparseScalarF64Range()) {
        std::vector<std::vector<double>> m(static_cast<size_t>(cols),
                                           std::vector<double>(static_cast<size_t>(rows), f));
        out.push_back(std::move(m));
    }
    return out;
}

std::vector<Case> matCases(int cols, int rows) {
    auto op = [](const std::vector<std::vector<double>>& x,
                 const std::vector<std::vector<double>>& y) {
        return fp::additionMatrixMatrixInterval(fp::FPKind::Abstract, x, y);
    };
    auto mats = sparseMatrixAFValues(cols, rows);
    auto cases = fp::generateMatrixPairToMatrixCases(fp::FPKind::Abstract, mats, mats,
                                                     /*finite=*/true, op);
    return fp::selectNCases("binary/af_matrix_addition", 50, cases);
}

} // namespace

CTS_TEST(g, "matrix").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::AbstractFloat);
    auto cases = matCases(cols, rows);
    run(t, binaryOp("+"), {mt, mt}, mt, InputSource::Const, 0, cases);
});
