// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/smoothstep.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'smoothstep' builtin function. t = clamp((x-low)/(high-low),0,1);
// result = t*t*(3-2t). Inherited accuracy (abstract as accurate as f32). For const inputs low must
// differ from high (validForConst). f16 is deferred (no Metal oracle).

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,smoothstep",
    "Execution tests for the 'smoothstep' builtin function");

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

// smoothStepInterval as a single ScalarTripleToInterval (low, high, x). Inherited accuracy:
// abstract uses f32 math, f16 uses f16.
std::vector<fp::ScalarTripleToInterval> smoothOps(fp::FPKind kind) {
    const fp::FPKind mathKind = kind == fp::FPKind::F16 ? fp::FPKind::F16 : fp::FPKind::F32;
    return {[mathKind](double low, double high, double x) {
        return fp::smoothStepInterval(mathKind, low, high, x);
    }};
}

// Decode a 16-bit IEEE-754 binary16 pattern to a double.
double f16BitsToF64(uint16_t bits) {
    const uint32_t sign = (bits >> 15) & 0x1u;
    const uint32_t exp = (bits >> 10) & 0x1fu;
    const uint32_t mant = bits & 0x3ffu;
    double value;
    if (exp == 0) {
        value = std::ldexp(static_cast<double>(mant), -24); // subnormal/zero: mant * 2^-24
    } else if (exp == 0x1f) {
        value = mant == 0 ? std::numeric_limits<double>::infinity()
                          : std::numeric_limits<double>::quiet_NaN();
    } else {
        value = std::ldexp(1.0 + static_cast<double>(mant) / 1024.0, static_cast<int>(exp) - 15);
    }
    return sign ? -value : value;
}

// Decode a case input scalar (f32/f16 bit pattern or abstract-float f64) back to its double value.
double inputValue(fp::FPKind kind, const Scalar& s) {
    if (kind == fp::FPKind::Abstract) {
        return s.f64;
    }
    if (kind == fp::FPKind::F16) {
        return f16BitsToF64(static_cast<uint16_t>(s.bits));
    }
    float f;
    std::memcpy(&f, &s.bits, 4);
    return static_cast<double>(f);
}

// validForConst: low (input 0) != high (input 1). Filter applies only for const input source.
std::vector<Case> smoothCases(fp::FPKind kind, bool constStage) {
    const std::vector<double>& r = fp::sparseScalarRange(kind);
    std::vector<Case> all = fp::generateScalarTripleToIntervalCases(
        kind, r, r, r, /*finite=*/constStage, smoothOps(kind));
    if (!constStage) {
        return all;
    }
    std::vector<Case> filtered;
    for (Case& c : all) {
        const double low = inputValue(kind, c.inputs[0].elements[0]);
        const double high = inputValue(kind, c.inputs[1].elements[0]);
        if (low != high) {
            filtered.push_back(std::move(c));
        }
    }
    return filtered;
}

} // namespace

CTS_TEST(g, "abstract_float").params(constOnlyVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = smoothCases(fp::FPKind::Abstract, /*constStage=*/true);
    run(t, builtin("smoothstep"),
        {scalarType(ScalarKind::AbstractFloat), scalarType(ScalarKind::AbstractFloat),
         scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::AbstractFloat), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(g, "f32").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = smoothCases(fp::FPKind::F32, isConst(t));
    run(t, builtin("smoothstep"),
        {scalarType(ScalarKind::F32), scalarType(ScalarKind::F32), scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(g, "f16").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    auto cases = smoothCases(fp::FPKind::F16, isConst(t));
    run(t, builtin("smoothstep"),
        {scalarType(ScalarKind::F16), scalarType(ScalarKind::F16), scalarType(ScalarKind::F16)},
        scalarType(ScalarKind::F16), cfgInputSource(t), cfgVectorize(t), cases);
});
