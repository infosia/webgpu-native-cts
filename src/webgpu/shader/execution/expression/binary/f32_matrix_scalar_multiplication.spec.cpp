// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/f32_matrix_scalar_multiplication.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix-scalar and scalar-matrix f32 multiplication. Accuracy: correctly rounded.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,f32_matrix_scalar_multiplication",
    "Execution Tests for matrix-scalar and scalar-matrix f32 multiplication expression");

ParamsBuilder sourceColsRows(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }

std::vector<Case> matScalarCases(int cols, int rows, bool isConstStage) {
    auto op = [](const std::vector<std::vector<double>>& m, double s) {
        return fp::multiplicationMatrixScalarInterval(fp::FPKind::F32, m, s);
    };
    return fp::generateMatrixScalarToMatrixCases(fp::FPKind::F32, fp::sparseMatrixF32Range(cols, rows),
                                                 fp::sparseScalarF32Range(), /*finite=*/isConstStage,
                                                 op);
}
std::vector<Case> scalarMatCases(int cols, int rows, bool isConstStage) {
    auto op = [](double s, const std::vector<std::vector<double>>& m) {
        // x * y with x scalar, y matrix == multiplicationMatrixScalarInterval(y, x).
        return fp::multiplicationMatrixScalarInterval(fp::FPKind::F32, m, s);
    };
    return fp::generateScalarMatrixToMatrixCases(fp::FPKind::F32, fp::sparseScalarF32Range(),
                                                 fp::sparseMatrixF32Range(cols, rows),
                                                 /*finite=*/isConstStage, op);
}

} // namespace

CTS_TEST(g, "matrix_scalar").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F32);
    const ExprType F32 = scalarType(ScalarKind::F32);
    auto cases = matScalarCases(cols, rows, isConst(t));
    run(t, binaryOp("*"), {mt, F32}, mt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "matrix_scalar_compound").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F32);
    const ExprType F32 = scalarType(ScalarKind::F32);
    auto cases = matScalarCases(cols, rows, isConst(t));
    runCompound(t, "*=", {mt, F32}, mt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "scalar_matrix").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F32);
    const ExprType F32 = scalarType(ScalarKind::F32);
    auto cases = scalarMatCases(cols, rows, isConst(t));
    run(t, binaryOp("*"), {F32, mt}, mt, cfgInputSource(t), 0, cases);
});
