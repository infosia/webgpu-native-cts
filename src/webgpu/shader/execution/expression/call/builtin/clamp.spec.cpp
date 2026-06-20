// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/clamp.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'clamp' builtin function. Integer variants return min(max(e,low),high).
// Float variants accept EITHER min(max(...)) OR the median of the three values (both formulations
// tested via anyOf). f32 and abstract use their own trait. f16 is deferred (no Metal oracle).

#include <algorithm>
#include <cstdint>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,clamp",
    "Execution tests for the 'clamp' builtin function");

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
bool isConst(const Fixture& t) { return cfgInputSource(t) == InputSource::Const; }
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

const std::vector<uint32_t> u32Values = {0u, 1u, 2u, 3u, 0x70000000u, 0x80000000u, 0xffffffffu};
const std::vector<int32_t> i32Values = {INT32_MIN, -3, -2, -1, 0, 1, 2, 3, 0x70000000, INT32_MAX};
const std::vector<int64_t> abstractIntValues = {
    INT64_MIN, -3, -2, -1, 0, 1, 2, 3, 0x70000000, INT64_MAX};

// median + min(max) formulations as ScalarTripleToInterval (clampIntervals order: median, minmax).
std::vector<fp::ScalarTripleToInterval> clampIntervals(fp::FPKind kind) {
    return {[kind](double x, double y, double z) { return fp::clampMedianInterval(kind, x, y, z); },
            [kind](double x, double y, double z) { return fp::clampMinMaxInterval(kind, x, y, z); }};
}

std::vector<Case> floatCases(fp::FPKind kind, bool constStage) {
    const std::vector<double>& r = fp::sparseScalarRange(kind);
    // Cartesian over low, high, e, with const dropping low > high; non-const keeps all.
    std::vector<Case> cases;
    for (double low : r) {
        for (double high : r) {
            if (constStage && low > high) {
                continue;
            }
            std::vector<Case> sub = fp::generateScalarTripleToIntervalCases(
                kind, r, {low}, {high}, /*finite=*/constStage, clampIntervals(kind));
            // generateScalarTripleToIntervalCases iterates (e, low, high) — but clamp's signature is
            // clamp(e, low, high) and the cache calls makeScalarTripleToIntervalCase(e, low, high).
            for (Case& c : sub) {
                cases.push_back(std::move(c));
            }
        }
    }
    return cases;
}

} // namespace

CTS_TEST(g, "abstract_int").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int64_t low : abstractIntValues) {
        for (int64_t high : abstractIntValues) {
            if (low > high) {
                continue;
            }
            for (int64_t e : abstractIntValues) {
                const int64_t r = std::min(std::max(e, low), high);
                cases.push_back(Case({CaseValue(abstractInt64(e)), CaseValue(abstractInt64(low)),
                                      CaseValue(abstractInt64(high))},
                                     CaseValue(abstractInt64(r))));
            }
        }
    }
    run(t, builtin("clamp"),
        {scalarType(ScalarKind::AbstractInt), scalarType(ScalarKind::AbstractInt),
         scalarType(ScalarKind::AbstractInt)},
        scalarType(ScalarKind::AbstractInt), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "u32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool stageConst = isConst(t);
    std::vector<Case> cases;
    for (uint32_t low : u32Values) {
        for (uint32_t high : u32Values) {
            if (stageConst && low > high) {
                continue;
            }
            for (uint32_t e : u32Values) {
                const uint32_t r = std::min(std::max(e, low), high);
                cases.push_back(Case({CaseValue(u32(e)), CaseValue(u32(low)), CaseValue(u32(high))},
                                     CaseValue(u32(r))));
            }
        }
    }
    run(t, builtin("clamp"),
        {scalarType(ScalarKind::U32), scalarType(ScalarKind::U32), scalarType(ScalarKind::U32)},
        scalarType(ScalarKind::U32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "i32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool stageConst = isConst(t);
    std::vector<Case> cases;
    for (int32_t low : i32Values) {
        for (int32_t high : i32Values) {
            if (stageConst && low > high) {
                continue;
            }
            for (int32_t e : i32Values) {
                const int32_t r = std::min(std::max(e, low), high);
                cases.push_back(Case({CaseValue(i32(e)), CaseValue(i32(low)), CaseValue(i32(high))},
                                     CaseValue(i32(r))));
            }
        }
    }
    run(t, builtin("clamp"),
        {scalarType(ScalarKind::I32), scalarType(ScalarKind::I32), scalarType(ScalarKind::I32)},
        scalarType(ScalarKind::I32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = floatCases(fp::FPKind::Abstract, /*constStage=*/true);
    run(t, builtin("clamp"),
        {scalarType(ScalarKind::AbstractFloat), scalarType(ScalarKind::AbstractFloat),
         scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = floatCases(fp::FPKind::F32, isConst(t));
    run(t, builtin("clamp"),
        {scalarType(ScalarKind::F32), scalarType(ScalarKind::F32), scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = floatCases(fp::FPKind::F16, isConst(t));
    run(t, builtin("clamp"),
        {scalarType(ScalarKind::F16), scalarType(ScalarKind::F16), scalarType(ScalarKind::F16)},
        scalarType(ScalarKind::F16), cfgInputSource(t), cfgVectorize(t), cases);
});
