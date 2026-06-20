// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/f16_matrix_vector_multiplication.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix-vector and vector-matrix f16 multiplication. Accuracy: correctly rounded.
// Mirrors f32_matrix_vector_multiplication but uses the f16 traits + f16 ranges; gated on 'shader-f16'.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,f16_matrix_vector_multiplication",
    "Execution Tests for matrix-vector and vector-matrix f16 multiplication expression");

ParamsBuilder sourceColsRows(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("cols", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))});
}
ParamsBuilder sourceDim(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("dim", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
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

std::vector<Case> matVecCases(int cols, int rows, bool isConstStage) {
    auto op = [](const std::vector<std::vector<double>>& m, const std::vector<double>& v) {
        return fp::multiplicationMatrixVectorInterval(fp::FPKind::F16, m, v);
    };
    return fp::generateMatrixVectorToVectorCases(fp::FPKind::F16, fp::sparseMatrixF16Range(cols, rows),
                                                 fp::sparseVectorF16Range(cols),
                                                 /*finite=*/isConstStage, op);
}
std::vector<Case> vecMatCases(int cols, int rows, bool isConstStage) {
    auto op = [](const std::vector<double>& v, const std::vector<std::vector<double>>& m) {
        return fp::multiplicationVectorMatrixInterval(fp::FPKind::F16, v, m);
    };
    return fp::generateVectorMatrixToVectorCases(fp::FPKind::F16, fp::sparseVectorF16Range(rows),
                                                 fp::sparseMatrixF16Range(cols, rows),
                                                 /*finite=*/isConstStage, op);
}

} // namespace

CTS_TEST(g, "matrix_vector").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F16);
    const ExprType vIn = vecType(cols, ScalarKind::F16);
    const ExprType vOut = vecType(rows, ScalarKind::F16);
    auto cases = matVecCases(cols, rows, isConst(t));
    run(t, binaryOp("*"), {mt, vIn}, vOut, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "vector_matrix").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F16);
    const ExprType vIn = vecType(rows, ScalarKind::F16);
    const ExprType vOut = vecType(cols, ScalarKind::F16);
    auto cases = vecMatCases(cols, rows, isConst(t));
    run(t, binaryOp("*"), {vIn, mt}, vOut, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "vector_matrix_compound").params(sourceDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const int cols = dim;
    const int rows = dim;
    const ExprType mt = matType(cols, rows, ScalarKind::F16);
    const ExprType vIn = vecType(rows, ScalarKind::F16);
    const ExprType vOut = vecType(cols, ScalarKind::F16);
    auto cases = vecMatCases(cols, rows, isConst(t));
    runCompound(t, "*=", {vIn, mt}, vOut, cfgInputSource(t), 0, cases);
});
