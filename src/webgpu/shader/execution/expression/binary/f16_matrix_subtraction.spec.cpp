// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/f16_matrix_subtraction.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for matrix f16 subtraction (x - y, componentwise). Accuracy: correctly rounded.
// Mirrors f32_matrix_subtraction but uses the f16 traits + f16 ranges; gated on the 'shader-f16' feature.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,f16_matrix_subtraction",
    "Execution Tests for matrix f16 subtraction expression");

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
void requireF16(AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

fp::MatrixPairToMatrix subMatOp() {
    return [](const std::vector<std::vector<double>>& x,
              const std::vector<std::vector<double>>& y) {
        return fp::subtractionMatrixMatrixInterval(fp::FPKind::F16, x, y);
    };
}

std::vector<Case> matCases(int cols, int rows, bool isConstStage) {
    return fp::generateMatrixPairToMatrixCases(
        fp::FPKind::F16, fp::sparseMatrixF16Range(cols, rows), fp::sparseMatrixF16Range(cols, rows),
        /*finite=*/isConstStage, subMatOp());
}

} // namespace

CTS_TEST(g, "matrix").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F16);
    auto cases = matCases(cols, rows, isConst(t));
    run(t, binaryOp("-"), {mt, mt}, mt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "matrix_compound").params(sourceColsRows).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int cols = static_cast<int>(t.param<int64_t>("cols"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const ExprType mt = matType(cols, rows, ScalarKind::F16);
    auto cases = matCases(cols, rows, isConst(t));
    runCompound(t, "-=", {mt, mt}, mt, cfgInputSource(t), 0, cases);
});
