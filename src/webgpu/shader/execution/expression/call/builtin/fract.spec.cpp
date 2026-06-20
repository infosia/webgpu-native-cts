// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/fract.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'fract' builtin function. fract(x) = x - floor(x). f32 uses fractInterval;
// abstract uses the precomputed kFractTable (af_data.ts) of correctly-rounded-at-f64 intervals.
// f16 is deferred (no Metal oracle).

#include <cstring>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,fract",
    "Execution tests for the 'fract' builtin function");

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

double f64FromBits(uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}

// kCommonValues (fract.cache.ts).
const std::vector<double> kCommonValues = {
    0.5, 0.9, 1.0, 2.0, 1.11, -0.1, -0.5, -0.9, -1.0, -2.0, -1.11,
};
// f32-specific values: 10.0001, -10.0001, 0x8000_0000 (a number).
const std::vector<double> kF32Specific = {10.0001, -10.0001, 2147483648.0};

std::vector<double> f32Range() {
    std::vector<double> v = kCommonValues;
    v.insert(v.end(), kF32Specific.begin(), kF32Specific.end());
    const std::vector<double> base = fp::scalarF32Range();
    v.insert(v.end(), base.begin(), base.end());
    return v;
}

// kFractTable (af_data.ts): abstract inputs -> spanIntervals of correctly-rounded(expected) at f64.
struct FractEntry {
    double input;
    std::vector<double> expected;
};
std::vector<FractEntry> kFractTable() {
    const double f64NegMin = f64FromBits(0xffefffffffffffffull);
    const double f64NegMax = f64FromBits(0x8010000000000000ull);
    const double f64NegSubMin = f64FromBits(0x800fffffffffffffull);
    const double f64NegSubMax = f64FromBits(0x8000000000000001ull);
    const double f64PosSubMin = f64FromBits(0x0000000000000001ull);
    const double f64PosSubMax = f64FromBits(0x000fffffffffffffull);
    const double f64PosMin = f64FromBits(0x0010000000000000ull);
    const double f64PosMax = f64FromBits(0x7fefffffffffffffull);
    return {
        {f64NegMin, {0.0}},
        {-10.0, {0.0}},
        {-1.0, {0.0}},
        {-0.125, {f64FromBits(0x3fec000000000000ull)}}, // 0.875
        {f64NegMax, {1.0}},
        {f64NegSubMin, {1.0}},
        {f64NegSubMax, {1.0}},
        {0.0, {0.0}},
        {f64PosSubMin, {0.0, f64PosSubMin}},
        {f64PosSubMax, {0.0, f64PosSubMax}},
        {f64PosMin, {f64PosMin}},
        {0.125, {0.125}},
        {1.0, {0.0}},
        {10.0, {0.0}},
        {f64PosMax, {0.0}},
        {-10.0000999999999997669, {f64FromBits(0x3fefff2e48e8a720ull)}},
        {-2.0, {0.0}},
        {-1.0, {0.0}},
        {-0.5, {f64FromBits(0x3fe0000000000000ull)}}, // 0.5
        {0.5, {f64FromBits(0x3fe0000000000000ull)}},  // 0.5
        {1.0, {0.0}},
        {2.0, {0.0}},
        {10.0000999999999997669, {f64FromBits(0x3f1a36e2eb1c0000ull)}},
        {3937509.87755102012306, {f64FromBits(0x3fec14e5e0800000ull)}},
    };
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const std::vector<FractEntry> table = kFractTable();
    // JS Map semantics over [...keys()]: unique keys in first-insertion order; value = last write.
    std::vector<double> keys;
    for (const FractEntry& e : table) {
        bool seen = false;
        for (double k : keys) {
            if (k == e.input) {
                seen = true;
                break;
            }
        }
        if (!seen) {
            keys.push_back(e.input);
        }
    }
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::Abstract, keys, /*finite=*/true, [&table](double n) {
            // Map value = the LAST entry whose input == n (later writes overwrite).
            const std::vector<double>* expected = nullptr;
            for (const FractEntry& e : table) {
                if (e.input == n) {
                    expected = &e.expected;
                }
            }
            if (expected == nullptr) {
                return fp::unboundedInterval(fp::FPKind::Abstract);
            }
            std::vector<fp::FPInterval> ivs;
            for (double ex : *expected) {
                ivs.push_back(fp::correctlyRoundedInterval(fp::FPKind::Abstract, ex));
            }
            return fp::spanIntervals(ivs);
        });
    run(t, builtin("fract"), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = fp::generateScalarToIntervalCases(
        fp::FPKind::F32, f32Range(), /*finite=*/false,
        [](double n) { return fp::fractInterval(fp::FPKind::F32, n); });
    run(t, builtin("fract"), {scalarType(ScalarKind::F32)}, scalarType(ScalarKind::F32),
        cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    t.skip("f16 deferred: shader-f16 has no Metal oracle (phaseY13 Stage B follow-up)");
});
