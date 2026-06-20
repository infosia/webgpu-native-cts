// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/f16_comparison.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the f16 comparison operations (== != < <= > >=). The result is bool, so the
// expectation is exact (no FP interval); subnormal flushing is folded in via the anyOf of the
// flushed/unflushed truth values (generateComparisonCases). Mirrors f32_comparison but uses the f16
// traits + f16 ranges; gated on the 'shader-f16' feature.

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,f16_comparison",
    "Execution Tests for the f16 comparison operations");

ParamsBuilder sourceVectorizeUndef(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}
void requireF16(AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

const ExprType F16 = scalarType(ScalarKind::F16);
const ExprType BOOL = scalarType(ScalarKind::Bool);

std::vector<Case> compareCases(const fp::TruthFunc& tf) {
    return fp::generateComparisonCases(fp::FPKind::F16, fp::vectorF16Range(2), tf);
}

} // namespace

CTS_TEST(g, "equals").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = compareCases([](double a, double b) { return a == b; });
    run(t, binaryOp("=="), {F16, F16}, BOOL, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "not_equals").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = compareCases([](double a, double b) { return a != b; });
    run(t, binaryOp("!="), {F16, F16}, BOOL, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "less_than").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = compareCases([](double a, double b) { return a < b; });
    run(t, binaryOp("<"), {F16, F16}, BOOL, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "less_equals").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = compareCases([](double a, double b) { return a <= b; });
    run(t, binaryOp("<="), {F16, F16}, BOOL, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "greater_than").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = compareCases([](double a, double b) { return a > b; });
    run(t, binaryOp(">"), {F16, F16}, BOOL, cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "greater_equals").params(sourceVectorizeUndef).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    auto cases = compareCases([](double a, double b) { return a >= b; });
    run(t, binaryOp(">="), {F16, F16}, BOOL, cfgInputSource(t), cfgVectorize(t), cases);
});
