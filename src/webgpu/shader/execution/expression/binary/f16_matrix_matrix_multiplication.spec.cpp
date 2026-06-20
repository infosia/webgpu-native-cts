// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/f16_matrix_matrix_multiplication.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix-matrix f16 multiplication (x * y). Accuracy: correctly rounded.
// Mirrors f32_matrix_matrix_multiplication but uses the f16 traits + f16 ranges; gated on 'shader-f16'.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,f16_matrix_matrix_multiplication",
    "Execution Tests for matrix-matrix f16 multiplication expression");

ParamsBuilder sourceMatrixMatrix(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("common_dim", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                Value(static_cast<int64_t>(4))})
        .combine("x_rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                            Value(static_cast<int64_t>(4))})
        .combine("y_cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                            Value(static_cast<int64_t>(4))});
}
ParamsBuilder sourceMatrixMatrixCompound(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("common_dim", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                Value(static_cast<int64_t>(4))})
        .combine("x_rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                            Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }
void requireF16(AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

std::vector<Case> matMatCases(int xCols, int xRows, int yCols, int yRows, bool isConstStage) {
    auto op = [](const std::vector<std::vector<double>>& x,
                 const std::vector<std::vector<double>>& y) {
        return fp::multiplicationMatrixMatrixInterval(fp::FPKind::F16, x, y);
    };
    return fp::generateMatrixPairToMatrixCases(fp::FPKind::F16, fp::sparseMatrixF16Range(xCols, xRows),
                                               fp::sparseMatrixF16Range(yCols, yRows),
                                               /*finite=*/isConstStage, op);
}

} // namespace

CTS_TEST(g, "matrix_matrix").params(sourceMatrixMatrix).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int commonDim = static_cast<int>(t.param<int64_t>("common_dim"));
    const int xRows = static_cast<int>(t.param<int64_t>("x_rows"));
    const int yCols = static_cast<int>(t.param<int64_t>("y_cols"));
    const int xCols = commonDim;
    const int yRows = commonDim;
    const ExprType xt = matType(xCols, xRows, ScalarKind::F16);
    const ExprType yt = matType(yCols, yRows, ScalarKind::F16);
    const ExprType rt = matType(yCols, xRows, ScalarKind::F16);
    auto cases = matMatCases(xCols, xRows, yCols, yRows, isConst(t));
    run(t, binaryOp("*"), {xt, yt}, rt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "matrix_matrix_compound")
    .params(sourceMatrixMatrixCompound)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        requireF16(t);
        const int commonDim = static_cast<int>(t.param<int64_t>("common_dim"));
        const int xRows = static_cast<int>(t.param<int64_t>("x_rows"));
        const int xCols = commonDim;
        const int yCols = xCols;
        const int yRows = commonDim;
        const ExprType xt = matType(xCols, xRows, ScalarKind::F16);
        const ExprType yt = matType(yCols, yRows, ScalarKind::F16);
        const ExprType rt = matType(yCols, xRows, ScalarKind::F16);
        auto cases = matMatCases(xCols, xRows, yCols, yRows, isConst(t));
        runCompound(t, "*=", {xt, yt}, rt, cfgInputSource(t), 0, cases);
    });
