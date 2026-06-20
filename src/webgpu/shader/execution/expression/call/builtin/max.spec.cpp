// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/max.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'max' builtin function. Integer variants are bit-exact max; float variants
// use the correctly-rounded maxInterval. f32 and abstract use their own trait. f16 is deferred.

#include <algorithm>
#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,max",
    "Execution tests for the 'max' builtin function");

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

std::vector<Case> floatCases(fp::FPKind kind) {
    const std::vector<double>& r = kind == fp::FPKind::Abstract ? fp::sparseScalarF64Range()
                                                                : fp::sparseScalarF32Range();
    return fp::generateScalarPairToIntervalCases(
        kind, r, r, /*finite=*/false,
        [kind](double x, double y) { return fp::maxInterval(kind, x, y); });
}

} // namespace

CTS_TEST(g, "abstract_int").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const std::vector<int64_t> vals = {-0x70000000, -2, -1, 0, 1, 2, 0x70000000};
    std::vector<Case> cases;
    for (int64_t e : vals) {
        for (int64_t f : vals) {
            cases.push_back(Case({CaseValue(abstractInt64(e)), CaseValue(abstractInt64(f))},
                                 CaseValue(abstractInt64(std::max(e, f)))));
        }
    }
    run(t, builtin("max"), {scalarType(ScalarKind::AbstractInt), scalarType(ScalarKind::AbstractInt)},
        scalarType(ScalarKind::AbstractInt), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "u32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const std::vector<uint32_t> vals = {0u, 1u, 2u, 0x70000000u, 0x80000000u, 0xffffffffu};
    std::vector<Case> cases;
    for (uint32_t e : vals) {
        for (uint32_t f : vals) {
            cases.push_back(Case({CaseValue(u32(e)), CaseValue(u32(f))}, CaseValue(u32(std::max(e, f)))));
        }
    }
    run(t, builtin("max"), {scalarType(ScalarKind::U32), scalarType(ScalarKind::U32)},
        scalarType(ScalarKind::U32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "i32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const std::vector<int32_t> vals = {-0x70000000, -2, -1, 0, 1, 2, 0x70000000};
    std::vector<Case> cases;
    for (int32_t e : vals) {
        for (int32_t f : vals) {
            cases.push_back(Case({CaseValue(i32(e)), CaseValue(i32(f))}, CaseValue(i32(std::max(e, f)))));
        }
    }
    run(t, builtin("max"), {scalarType(ScalarKind::I32), scalarType(ScalarKind::I32)},
        scalarType(ScalarKind::I32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = floatCases(fp::FPKind::Abstract);
    run(t, builtin("max"), {scalarType(ScalarKind::AbstractFloat), scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = floatCases(fp::FPKind::F32);
    run(t, builtin("max"), {scalarType(ScalarKind::F32), scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
