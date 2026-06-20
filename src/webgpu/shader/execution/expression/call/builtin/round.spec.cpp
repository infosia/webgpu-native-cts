// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/round.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'round' builtin function. Ties-to-even correctly-rounded; f32 and
// abstract use their own trait. f16 is deferred (no Metal oracle).

#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,round",
    "Execution tests for the 'round' builtin function");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder constOnlyVectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// [kIssue2766Value, ...scalarRange()].
std::vector<double> roundRange(fp::FPKind kind) {
    // kIssue2766Value: 0x8000_0000 (f32) / 0x8000_0000_0000_0000 (abstract) — as a plain number.
    const double issue = kind == fp::FPKind::Abstract ? 9223372036854775808.0 : 2147483648.0;
    std::vector<double> v = {issue};
    const std::vector<double> base = fp::scalarRangeForKind(kind);
    v.insert(v.end(), base.begin(), base.end());
    return v;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, roundRange(fp::FPKind::Abstract), /*finite=*/false,
        [](double n) { return fp::roundInterval(fp::FPKind::Abstract, n); });
    run(t, builtin("round"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, roundRange(fp::FPKind::F32), /*finite=*/false,
        [](double n) { return fp::roundInterval(fp::FPKind::F32, n); });
    run(t, builtin("round"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
