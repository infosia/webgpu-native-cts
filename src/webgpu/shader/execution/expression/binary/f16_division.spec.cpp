// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/f16_division.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for non-matrix f16 division (x / y). Accuracy: 2.5 ULP for |y| in [2^-14, 2^14].
// Mirrors f32_division but uses the f16 traits + f16 ranges; gated on the 'shader-f16' feature.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,f16_division",
    "Execution Tests for non-matrix f16 division expression");

ParamsBuilder sourceOnly(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"});
}
ParamsBuilder sourceVectorize(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))});
}
ParamsBuilder sourceVectorizeUndef(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
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
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}
void requireF16(AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

// FP.f16.divisionInterval(x, y).
fp::FPInterval divOp(double x, double y) { return fp::divisionInterval(fp::FPKind::F16, x, y); }

std::vector<Case> scalarCases(bool nonConst) {
    return fp::generateScalarPairToIntervalCases(fp::FPKind::F16, fp::sparseScalarF16Range(),
                                                 fp::sparseScalarF16Range(), /*finite=*/!nonConst,
                                                 divOp);
}

const ExprType F16 = scalarType(ScalarKind::F16);

} // namespace

CTS_TEST(g, "scalar").params(sourceOnly).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = scalarCases(!isConst(t));
    run(t, binaryOp("/"), {F16, F16}, F16, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "vector").params(sourceVectorize).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = scalarCases(!isConst(t));
    run(t, binaryOp("/"), {F16, F16}, F16, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "scalar_compound").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = scalarCases(!isConst(t));
    runCompound(t, "/=", {F16, F16}, F16, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "vector_scalar").params(sourceDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const ExprType vt = vecType(dim, ScalarKind::F16);
    auto cases = fp::generateVectorScalarToVectorCases(
        fp::FPKind::F16, fp::sparseVectorF16Range(dim), fp::sparseScalarF16Range(),
        /*finite=*/isConst(t), divOp);
    run(t, binaryOp("/"), {vt, F16}, vt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "vector_scalar_compound").params(sourceDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const ExprType vt = vecType(dim, ScalarKind::F16);
    auto cases = fp::generateVectorScalarToVectorCases(
        fp::FPKind::F16, fp::sparseVectorF16Range(dim), fp::sparseScalarF16Range(),
        /*finite=*/isConst(t), divOp);
    runCompound(t, "/=", {vt, F16}, vt, cfgInputSource(t), 0, cases);
});

CTS_TEST(g, "scalar_vector").params(sourceDim).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const int dim = static_cast<int>(t.param<int64_t>("dim"));
    const ExprType vt = vecType(dim, ScalarKind::F16);
    auto cases = fp::generateScalarVectorToVectorCases(
        fp::FPKind::F16, fp::sparseScalarF16Range(), fp::sparseVectorF16Range(dim),
        /*finite=*/isConst(t), divOp);
    run(t, binaryOp("/"), {F16, vt}, vt, cfgInputSource(t), 0, cases);
});
