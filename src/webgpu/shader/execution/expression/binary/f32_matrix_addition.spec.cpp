// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/f32_matrix_addition.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix f32 addition (x + y, componentwise). Accuracy: correctly rounded.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,f32_matrix_addition",
    "Execution Tests for matrix f32 addition expression");

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

fp::MatrixPairToMatrix addMatOp() {
    return [](const std::vector<std::vector<double>>& x,
              const std::vector<std::vector<double>>& y) {
        return fp::additionMatrixMatrixInterval(fp::FPKind::F32, x, y);
    };
}

std::vector<Case> matCases(int cols, int rows, bool isConstStage) {
    return fp::generateMatrixPairToMatrixCases(
        fp::FPKind::F32, fp::sparseMatrixF32Range(cols, rows), fp::sparseMatrixF32Range(cols, rows),
        /*finite=*/isConstStage, addMatOp());
}

} // namespace

CTS_TEST(g, "matrix").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F32);
    auto cases = matCases(cols, rows, isConst(t));
    run(t, binaryOp("+"), {mt, mt}, mt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "matrix_compound").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F32);
    auto cases = matCases(cols, rows, isConst(t));
    runCompound(t, "+=", {mt, mt}, mt, cfgInputSource(t), 0, cases);
});
