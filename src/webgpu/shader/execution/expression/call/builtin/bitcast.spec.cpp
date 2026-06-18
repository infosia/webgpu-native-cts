// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/bitcast.spec.ts
// (and its bitcast.cache.ts) @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'bitcast' builtin function: pure bit reinterpretation across the
// concrete numeric scalar/vector types u32/i32/f32 (+f16 when shader-f16 is available) and the
// abstract numeric types. The expected results are computed in C++ by reinterpreting the input
// bits to the result type. For float-typed results a per-element acceptance set is used: any NaN
// is accepted when the reinterpreted value is NaN, and the subnormal-flushed +/-0 is accepted in
// addition to the exact value (a GPU may canonicalize NaN bit patterns and flush subnormals on a
// bitcast-to-float). Non-NaN finite float results, and all integer results, are bit-exact.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,bitcast",
    "Execution tests for the 'bitcast' builtin function.");

// ---------------------------------------------------------------------------------------------
// Bit reinterpretation helpers (port of util/reinterpret.ts).
// ---------------------------------------------------------------------------------------------

uint32_t f32AsU32(float f) {
    uint32_t u;
    std::memcpy(&u, &f, 4);
    return u;
}
float u32AsF32(uint32_t u) {
    float f;
    std::memcpy(&f, &u, 4);
    return f;
}

// ---------------------------------------------------------------------------------------------
// Constants (port of util/constants.ts kBit / kValue, only the parts bitcast needs).
// ---------------------------------------------------------------------------------------------

namespace kBitF32 {
constexpr uint32_t posMin = 0x00800000u;       // smallest positive normal
constexpr uint32_t posMax = 0x7f7fffffu;       // largest positive normal
constexpr uint32_t posSubMin = 0x00000001u;
constexpr uint32_t posSubMax = 0x007fffffu;
constexpr uint32_t posInf = 0x7f800000u;
constexpr uint32_t negMin = 0xff7fffffu;       // most-negative normal
constexpr uint32_t negMax = 0x80800000u;       // largest-magnitude... (closest to 0) normal
constexpr uint32_t negSubMax = 0x80000001u;
constexpr uint32_t negSubMin = 0x807fffffu;
constexpr uint32_t negInf = 0xff800000u;
constexpr uint32_t negZero = 0x80000000u;
} // namespace kBitF32

namespace kBitI32 {
constexpr uint32_t posMax = 0x7fffffffu;
} // namespace kBitI32

namespace kBitU32 {
constexpr uint32_t max = 0xffffffffu;
} // namespace kBitU32

namespace kBitF16 {
constexpr uint16_t posMin = 0x0400u;
constexpr uint16_t posMax = 0x7bffu;
constexpr uint16_t posSubMin = 0x0001u;
constexpr uint16_t posSubMax = 0x03ffu;
constexpr uint16_t posInf = 0x7c00u;
constexpr uint16_t negMin = 0xfbffu;
constexpr uint16_t negMax = 0x8400u;
constexpr uint16_t negSubMax = 0x8001u;
constexpr uint16_t negSubMin = 0x83ffu;
constexpr uint16_t negInf = 0xfc00u;
constexpr uint16_t negZero = 0x8000u;
} // namespace kBitF16

// kValue.f32/f16 boundary values (reinterpreted from the kBit patterns).
const double kF32PosMin = static_cast<double>(u32AsF32(kBitF32::posMin));
const double kF32PosMax = static_cast<double>(u32AsF32(kBitF32::posMax));
const double kF32NegMin = static_cast<double>(u32AsF32(kBitF32::negMin));
const double kF32NegMax = static_cast<double>(u32AsF32(kBitF32::negMax));

// f16 boundary values decoded to double (exact for finite f16).
double f16BitsToDouble(uint16_t h) {
    const uint32_t sign = (h >> 15) & 0x1u;
    const uint32_t exp = (h >> 10) & 0x1Fu;
    const uint32_t mant = h & 0x3FFu;
    double value;
    if (exp == 0) {
        value = static_cast<double>(mant) * (1.0 / 16777216.0);
    } else if (exp == 0x1F) {
        value = mant == 0 ? std::numeric_limits<double>::infinity()
                          : std::numeric_limits<double>::quiet_NaN();
    } else {
        const double frac = 1.0 + static_cast<double>(mant) / 1024.0;
        value = std::ldexp(frac, static_cast<int>(exp) - 15);
    }
    return sign ? -value : value;
}
const double kF16PosMin = f16BitsToDouble(kBitF16::posMin);
const double kF16PosMax = f16BitsToDouble(kBitF16::posMax);
const double kF16NegMin = f16BitsToDouble(kBitF16::negMin);
const double kF16NegMax = f16BitsToDouble(kBitF16::negMax);

// ---------------------------------------------------------------------------------------------
// Finite / subnormal predicates (port of util/math.ts).
// ---------------------------------------------------------------------------------------------

bool isSubnormalNumberF32(double n) {
    return n > kF32NegMax && n < kF32PosMin;
}
bool isFiniteF32(double n) {
    return n >= kF32NegMin && n <= kF32PosMax;
}
bool isSubnormalNumberF16(double n) {
    return n > kF16NegMax && n < kF16PosMin;
}
bool isFiniteF16(double n) {
    return n >= kF16NegMin && n <= kF16PosMax;
}

// f16 (binary16) bit-pattern of a finite f32 value cast to f16, using round-to-nearest-even.
// Only used to determine subnormal-flush on the f16 element of a reinterpreted f32 value; the
// reinterpret path itself never goes f32->f16. Not needed here, retained for clarity.

// ---------------------------------------------------------------------------------------------
// Range generators (port of util/math.ts), all in double per JS semantics.
// ---------------------------------------------------------------------------------------------

double lerp(double a, double b, double t) {
    if (!std::isfinite(a) || !std::isfinite(b)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if ((a <= 0.0 && b >= 0.0) || (a >= 0.0 && b <= 0.0)) {
        return t * b + (1.0 - t) * a;
    }
    if (t == 1.0) {
        return b;
    }
    const double x = a + t * (b - a);
    return (t > 1.0) == (b > a) ? std::max(b, x) : std::min(b, x);
}

std::vector<double> linearRange(double a, double b, int numSteps) {
    std::vector<double> out;
    if (numSteps <= 0) {
        return out;
    }
    if (numSteps == 1) {
        out.push_back(a);
        return out;
    }
    out.reserve(static_cast<size_t>(numSteps));
    for (int i = 0; i < numSteps; ++i) {
        out.push_back(lerp(a, b, static_cast<double>(i) / static_cast<double>(numSteps - 1)));
    }
    return out;
}

std::vector<double> biasedRange(double a, double b, int numSteps) {
    const double c = 2.0;
    std::vector<double> out;
    if (numSteps <= 0) {
        return out;
    }
    if (numSteps == 1) {
        out.push_back(a);
        return out;
    }
    out.reserve(static_cast<size_t>(numSteps));
    for (int i = 0; i < numSteps; ++i) {
        const double t = std::pow(static_cast<double>(i) / static_cast<double>(numSteps - 1), c);
        out.push_back(lerp(a, b, t));
    }
    return out;
}

double jsTrunc(double v) {
    return std::trunc(v);
}

// fullI32Range: biasedRange(min,-1,50) ++ [0] ++ biasedRange(1,max,50), all truncated.
std::vector<int32_t> fullI32Range() {
    std::vector<int32_t> out;
    auto append = [&](const std::vector<double>& r) {
        for (double v : r) {
            out.push_back(static_cast<int32_t>(static_cast<int64_t>(jsTrunc(v))));
        }
    };
    append(biasedRange(-2147483648.0, -1.0, 50));
    out.push_back(0);
    append(biasedRange(1.0, 2147483647.0, 50));
    return out;
}

// fullU32Range: [0] ++ biasedRange(1, u32.max, 50), truncated.
std::vector<uint32_t> fullU32Range() {
    std::vector<uint32_t> out;
    out.push_back(0);
    for (double v : biasedRange(1.0, 4294967295.0, 50)) {
        out.push_back(static_cast<uint32_t>(static_cast<uint64_t>(jsTrunc(v))));
    }
    return out;
}

// scalarF32Range with default counts {pos_sub:10, pos_norm:50} (neg mirror the pos counts).
std::vector<float> scalarF32Range() {
    const int neg_norm = 50, neg_sub = 10, pos_sub = 10, pos_norm = 50;
    // special_pos appended (pos_norm >= 4): largest float as signed/unsigned integer.
    const uint32_t special_pos[] = {0x4effffffu, 0x4f7fffffu};
    const int numSpecial = 2;

    std::vector<double> bitFields;
    auto append = [&](const std::vector<double>& r) {
        for (double v : r) {
            bitFields.push_back(v);
        }
    };
    append(linearRange(static_cast<double>(kBitF32::negMin), static_cast<double>(kBitF32::negMax),
                       neg_norm));
    append(linearRange(static_cast<double>(kBitF32::negSubMin),
                       static_cast<double>(kBitF32::negSubMax), neg_sub));
    bitFields.push_back(static_cast<double>(0x80000000u)); // -0.0
    bitFields.push_back(0.0);                              // +0.0
    append(linearRange(static_cast<double>(kBitF32::posSubMin),
                       static_cast<double>(kBitF32::posSubMax), pos_sub));
    // Positive normals: linearRange(min,max,pos_norm - special.length) ++ special, then sorted.
    std::vector<double> posNorm =
        linearRange(static_cast<double>(kBitF32::posMin), static_cast<double>(kBitF32::posMax),
                    pos_norm - numSpecial);
    for (int i = 0; i < numSpecial; ++i) {
        posNorm.push_back(static_cast<double>(special_pos[i]));
    }
    std::sort(posNorm.begin(), posNorm.end());
    append(posNorm);

    std::vector<float> out;
    out.reserve(bitFields.size());
    for (double v : bitFields) {
        const uint32_t bits = static_cast<uint32_t>(static_cast<uint64_t>(jsTrunc(v)));
        out.push_back(u32AsF32(bits));
    }
    return out;
}

// scalarF16Range with default counts {pos_sub:10, pos_norm:50}.
std::vector<uint16_t> scalarF16RangeInU16() {
    const int neg_norm = 50, neg_sub = 10, pos_sub = 10, pos_norm = 50;
    std::vector<double> bitFields;
    auto append = [&](const std::vector<double>& r) {
        for (double v : r) {
            bitFields.push_back(v);
        }
    };
    append(linearRange(static_cast<double>(kBitF16::negMin), static_cast<double>(kBitF16::negMax),
                       neg_norm));
    append(linearRange(static_cast<double>(kBitF16::negSubMin),
                       static_cast<double>(kBitF16::negSubMax), neg_sub));
    bitFields.push_back(static_cast<double>(0x8000u)); // -0.0
    bitFields.push_back(0.0);                          // +0.0
    append(linearRange(static_cast<double>(kBitF16::posSubMin),
                       static_cast<double>(kBitF16::posSubMax), pos_sub));
    append(linearRange(static_cast<double>(kBitF16::posMin), static_cast<double>(kBitF16::posMax),
                       pos_norm));

    std::vector<uint16_t> out;
    out.reserve(bitFields.size());
    for (double v : bitFields) {
        out.push_back(static_cast<uint16_t>(static_cast<uint64_t>(jsTrunc(v)) & 0xFFFFu));
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// f16 special-value bit patterns (port of cache.ts f32InfAndNaN / f16InfAndNaN / zeros).
// ---------------------------------------------------------------------------------------------

constexpr int kNumNaNs = 11;

std::vector<uint32_t> f32InfAndNaNInU32() {
    std::vector<uint32_t> out;
    for (double v : linearRange(static_cast<double>(kBitF32::posInf + 1u),
                                static_cast<double>(kBitI32::posMax), kNumNaNs)) {
        out.push_back(static_cast<uint32_t>(static_cast<uint64_t>(jsTrunc(v))));
    }
    for (double v : linearRange(static_cast<double>(kBitF32::negInf + 1u),
                                static_cast<double>(kBitU32::max), kNumNaNs)) {
        out.push_back(static_cast<uint32_t>(static_cast<uint64_t>(jsTrunc(v))));
    }
    out.push_back(kBitF32::posInf);
    out.push_back(kBitF32::negInf);
    return out;
}

std::vector<uint16_t> f16InfAndNaNInU16() {
    std::vector<uint16_t> out;
    for (double v : linearRange(static_cast<double>(kBitF16::posInf + 1u), 32767.0, kNumNaNs)) {
        out.push_back(static_cast<uint16_t>(static_cast<uint64_t>(std::ceil(v)) & 0xFFFFu));
    }
    for (double v : linearRange(static_cast<double>(kBitF16::negInf + 1u), 65535.0, kNumNaNs)) {
        out.push_back(static_cast<uint16_t>(static_cast<uint64_t>(std::floor(v)) & 0xFFFFu));
    }
    out.push_back(kBitF16::posInf);
    out.push_back(kBitF16::negInf);
    return out;
}

const uint32_t kF32ZerosInU32[] = {0u, kBitF32::negZero};
const uint16_t kF16ZerosInU16[] = {kBitF16::negZero, 0u};

// ---------------------------------------------------------------------------------------------
// Combinators (port of cache.ts slidingSlice / cartesianProduct usage).
// ---------------------------------------------------------------------------------------------

template <typename T>
std::vector<std::vector<T>> slidingSlice(const std::vector<T>& input, int len) {
    std::vector<std::vector<T>> result;
    const size_t n = input.size();
    for (size_t i = 0; i < n; ++i) {
        std::vector<T> sub;
        sub.reserve(static_cast<size_t>(len));
        for (int j = 0; j < len; ++j) {
            sub.push_back(input[(i + static_cast<size_t>(j)) % n]);
        }
        result.push_back(std::move(sub));
    }
    return result;
}

// u32 <-> two u16 (little-endian).
uint32_t u16x2ToU32(uint16_t lo, uint16_t hi) {
    return static_cast<uint32_t>(lo) | (static_cast<uint32_t>(hi) << 16);
}
void u32ToU16x2(uint32_t u, uint16_t& lo, uint16_t& hi) {
    lo = static_cast<uint16_t>(u & 0xFFFFu);
    hi = static_cast<uint16_t>((u >> 16) & 0xFFFFu);
}

bool canU32BitcastToFiniteVec2F16(uint32_t u) {
    uint16_t lo, hi;
    u32ToU16x2(u, lo, hi);
    return isFiniteF16(f16BitsToDouble(lo)) && isFiniteF16(f16BitsToDouble(hi));
}

// ---------------------------------------------------------------------------------------------
// Expectation builders. Each produces an ExpectedElement per result element.
// ---------------------------------------------------------------------------------------------

// f32 result element from a f32 *value* known to be the exact reinterpretation. Accepts the exact
// 32-bit pattern; if the value is NaN/Inf (not finite f32) accepts any pattern; if subnormal also
// accepts the +/-0 patterns; if exactly zero accepts both signed zeros.
ExpectedElement f32ResultFromValue(double value, uint32_t exactBits) {
    ExpectedElement ee;
    ee.floatWidth = 32;
    if (!isFiniteF32(value)) {
        ee.any = true;
        return ee;
    }
    if (value == 0.0) {
        ee.acceptBits.push_back(0u);
        ee.acceptBits.push_back(kBitF32::negZero);
        return ee;
    }
    ee.acceptBits.push_back(exactBits);
    if (isSubnormalNumberF32(value)) {
        ee.acceptBits.push_back(0u);
        ee.acceptBits.push_back(kBitF32::negZero);
    }
    return ee;
}

// f16 result element from a 16-bit pattern. Accepts exact; if NaN accepts any NaN; if Inf accepts
// any; if zero accepts both signed zeros; if subnormal also accepts the zeros.
ExpectedElement f16ResultFromBits(uint16_t bits) {
    ExpectedElement ee;
    ee.floatWidth = 16;
    const double v = f16BitsToDouble(bits);
    if (!isFiniteF16(v)) {
        // Inf or NaN: unbounded (any 16-bit pattern accepted).
        ee.any = true;
        return ee;
    }
    if (v == 0.0) {
        ee.acceptBits.push_back(0u);
        ee.acceptBits.push_back(kBitF16::negZero);
        return ee;
    }
    ee.acceptBits.push_back(bits);
    if (isSubnormalNumberF16(v)) {
        ee.acceptBits.push_back(0u);
        ee.acceptBits.push_back(kBitF16::negZero);
    }
    return ee;
}

// ---------------------------------------------------------------------------------------------
// Param helpers.
// ---------------------------------------------------------------------------------------------

ParamsBuilder inputSourceVectorizeAliasParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))})
        .combine("alias", {Value(false), Value(true)});
}

ParamsBuilder inputSourceAliasParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("alias", {Value(false), Value(true)});
}

ParamsBuilder constVectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

ParamsBuilder constVectorizeAliasParams(ParamsBuilder u) {
    return constVectorizeParams(u).combine("alias", {Value(false), Value(true)});
}

ParamsBuilder constAliasParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"}).combine("alias", {Value(false), Value(true)});
}

InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}
bool isConst(const Fixture& t) {
    return cfgInputSource(t) == InputSource::Const;
}

// Builds a bitcast expression builder mirroring upstream's bitcastBuilder: the destination type
// becomes vecN<canonicalDestType> when vectorize is set, otherwise canonicalDestType. The 'alias'
// param is a pure rename of the destination type and does not affect results, so it is ignored.
ExpressionBuilder bitcastBuilder(const std::string& canonicalDestType, int vectorize) {
    std::string destType = canonicalDestType;
    if (vectorize != 0) {
        destType = "vec" + std::to_string(vectorize) + "<" + canonicalDestType + ">";
    }
    return builtin("bitcast<" + destType + ">");
}

void requireF16(AllFeaturesMaxLimitsGpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

// ---------------------------------------------------------------------------------------------
// 32-bit scalar/vector cases.
// ---------------------------------------------------------------------------------------------

// i32 -> i32 identity.
CTS_TEST(testGroup, "i32_to_i32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t e : fullI32Range()) {
        cases.push_back({{i32(e)}, i32(e)});
    }
    run(t, bitcastBuilder("i32", cfgVectorize(t)), {scalarType(ScalarKind::I32)},
        scalarType(ScalarKind::I32), cfgInputSource(t), cfgVectorize(t), cases);
});

CTS_TEST(testGroup, "u32_to_u32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t e : fullU32Range()) {
        cases.push_back({{u32(e)}, u32(e)});
    }
    run(t, bitcastBuilder("u32", cfgVectorize(t)), {scalarType(ScalarKind::U32)},
        scalarType(ScalarKind::U32), cfgInputSource(t), cfgVectorize(t), cases);
});

// f32 -> f32 identity. const: finite only; runtime: includes Inf/NaN.
CTS_TEST(testGroup, "f32_to_f32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    std::vector<float> vals = scalarF32Range();
    vals.push_back(u32AsF32(kBitF32::negZero)); // f32FiniteRange adds -0.0
    if (!isConst(t)) {
        for (uint32_t u : f32InfAndNaNInU32()) {
            vals.push_back(u32AsF32(u));
        }
    }
    for (float f : vals) {
        const uint32_t bits = f32AsU32(f);
        cases.push_back({{f32Bits(bits)}, f32Bits(bits), {f32ResultFromValue(f, bits)}});
    }
    run(t, bitcastBuilder("f32", cfgVectorize(t)), {scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

// u32 -> i32.
CTS_TEST(testGroup, "u32_to_i32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t e : fullU32Range()) {
        cases.push_back({{u32(e)}, i32Bits(e)});
    }
    run(t, bitcastBuilder("i32", cfgVectorize(t)), {scalarType(ScalarKind::U32)},
        scalarType(ScalarKind::I32), cfgInputSource(t), cfgVectorize(t), cases);
});

// f32 -> i32.
CTS_TEST(testGroup, "f32_to_i32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    std::vector<float> vals = scalarF32Range();
    vals.push_back(u32AsF32(kBitF32::negZero));
    if (!isConst(t)) {
        for (uint32_t u : f32InfAndNaNInU32()) {
            vals.push_back(u32AsF32(u));
        }
    }
    for (float f : vals) {
        const uint32_t bits = f32AsU32(f);
        ExpectedElement ee;
        if (!isFiniteF32(static_cast<double>(f))) {
            ee.any = true;
        } else {
            ee.acceptBits.push_back(bits);
            if (isSubnormalNumberF32(static_cast<double>(f))) {
                ee.acceptBits.push_back(0u);
                ee.acceptBits.push_back(kBitF32::negZero);
            }
        }
        cases.push_back({{f32Bits(bits)}, i32Bits(bits), {ee}});
    }
    run(t, bitcastBuilder("i32", cfgVectorize(t)), {scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::I32), cfgInputSource(t), cfgVectorize(t), cases);
});

// i32 -> u32.
CTS_TEST(testGroup, "i32_to_u32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t e : fullI32Range()) {
        cases.push_back({{i32(e)}, u32(static_cast<uint32_t>(e))});
    }
    run(t, bitcastBuilder("u32", cfgVectorize(t)), {scalarType(ScalarKind::I32)},
        scalarType(ScalarKind::U32), cfgInputSource(t), cfgVectorize(t), cases);
});

// f32 -> u32.
CTS_TEST(testGroup, "f32_to_u32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    std::vector<float> vals = scalarF32Range();
    vals.push_back(u32AsF32(kBitF32::negZero));
    if (!isConst(t)) {
        for (uint32_t u : f32InfAndNaNInU32()) {
            vals.push_back(u32AsF32(u));
        }
    }
    for (float f : vals) {
        const uint32_t bits = f32AsU32(f);
        ExpectedElement ee;
        if (!isFiniteF32(static_cast<double>(f))) {
            ee.any = true;
        } else {
            ee.acceptBits.push_back(bits);
            if (isSubnormalNumberF32(static_cast<double>(f))) {
                ee.acceptBits.push_back(0u);
                ee.acceptBits.push_back(kBitF32::negZero);
            }
        }
        cases.push_back({{f32Bits(bits)}, u32(bits), {ee}});
    }
    run(t, bitcastBuilder("u32", cfgVectorize(t)), {scalarType(ScalarKind::F32)},
        scalarType(ScalarKind::U32), cfgInputSource(t), cfgVectorize(t), cases);
});

// i32 -> f32. const: finite f32 only; runtime: includes Inf/NaN.
CTS_TEST(testGroup, "i32_to_f32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    // i32RangeForF32(Finite|InfNaN): fullI32Range ++ f32ZerosInI32 ++ f32InfAndNaNInI32.
    std::vector<uint32_t> u32vals;
    for (int32_t e : fullI32Range()) {
        u32vals.push_back(static_cast<uint32_t>(e));
    }
    for (uint32_t z : kF32ZerosInU32) {
        u32vals.push_back(z);
    }
    for (uint32_t n : f32InfAndNaNInU32()) {
        u32vals.push_back(n);
    }
    for (uint32_t u : u32vals) {
        const float f = u32AsF32(u);
        if (isConst(t) && !isFiniteF32(static_cast<double>(f))) {
            continue; // const-eval: finite only.
        }
        cases.push_back({{i32Bits(u)}, f32Bits(u), {f32ResultFromValue(static_cast<double>(f), u)}});
    }
    run(t, bitcastBuilder("f32", cfgVectorize(t)), {scalarType(ScalarKind::I32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

// u32 -> f32.
CTS_TEST(testGroup, "u32_to_f32").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    std::vector<uint32_t> u32vals = fullU32Range();
    for (uint32_t z : kF32ZerosInU32) {
        u32vals.push_back(z);
    }
    for (uint32_t n : f32InfAndNaNInU32()) {
        u32vals.push_back(n);
    }
    for (uint32_t u : u32vals) {
        const float f = u32AsF32(u);
        if (isConst(t) && !isFiniteF32(static_cast<double>(f))) {
            continue;
        }
        cases.push_back({{u32(u)}, f32Bits(u), {f32ResultFromValue(static_cast<double>(f), u)}});
    }
    run(t, bitcastBuilder("f32", cfgVectorize(t)), {scalarType(ScalarKind::U32)},
        scalarType(ScalarKind::F32), cfgInputSource(t), cfgVectorize(t), cases);
});

// ---------------------------------------------------------------------------------------------
// f16 identity.
// ---------------------------------------------------------------------------------------------

CTS_TEST(testGroup, "f16_to_f16").params(inputSourceVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    std::vector<uint16_t> vals = scalarF16RangeInU16();
    vals.push_back(kBitF16::negZero); // f16FiniteInF16 adds -0.0
    if (!isConst(t)) {
        for (uint16_t u : f16InfAndNaNInU16()) {
            vals.push_back(u);
        }
    }
    std::vector<Case> cases;
    for (uint16_t h : vals) {
        cases.push_back({{f16Bits(h)}, f16Bits(h), {f16ResultFromBits(h)}});
    }
    run(t, bitcastBuilder("f16", cfgVectorize(t)), {scalarType(ScalarKind::F16)},
        scalarType(ScalarKind::F16), cfgInputSource(t), cfgVectorize(t), cases);
});

// ---------------------------------------------------------------------------------------------
// 32-bit scalar -> vec2<f16>.
// ---------------------------------------------------------------------------------------------

// Per-element f16 expectation for a vec2<f16> result reinterpreted from a u32.
std::vector<ExpectedElement> vec2f16AcceptFromU32(uint32_t u) {
    uint16_t lo, hi;
    u32ToU16x2(u, lo, hi);
    return {f16ResultFromBits(lo), f16ResultFromBits(hi)};
}

// u32 range for vec2<f16>: fullU32Range ++ f16Vec2ZerosInU32 ++ f16Vec2InfAndNaNInU32.
std::vector<uint32_t> u32RangeForF16Vec2FiniteInfNaN() {
    std::vector<uint32_t> out = fullU32Range();
    // f16Vec2ZerosInU32 = cartesian(f16Zeros, f16Zeros)
    for (uint16_t a : kF16ZerosInU16) {
        for (uint16_t b : kF16ZerosInU16) {
            out.push_back(u16x2ToU32(a, b));
        }
    }
    // f16Vec2InfAndNaNInU32 = cartesian(infnan, infnan++finite) ++ cartesian(finite, infnan)
    std::vector<uint16_t> infnan = f16InfAndNaNInU16();
    std::vector<uint16_t> finite = scalarF16RangeInU16();
    finite.push_back(kBitF16::negZero);
    std::vector<uint16_t> infnanPlusFinite = infnan;
    for (uint16_t f : finite) {
        infnanPlusFinite.push_back(f);
    }
    for (uint16_t a : infnan) {
        for (uint16_t b : infnanPlusFinite) {
            out.push_back(u16x2ToU32(a, b));
        }
    }
    for (uint16_t a : finite) {
        for (uint16_t b : infnan) {
            out.push_back(u16x2ToU32(a, b));
        }
    }
    return out;
}

void addScalarToVec2hCases(
    AllFeaturesMaxLimitsGpuTest& t,
    ScalarKind paramKind,
    const std::string& destType) {
    const bool constMode = isConst(t);
    std::vector<uint32_t> range = u32RangeForF16Vec2FiniteInfNaN();
    std::vector<Case> cases;
    for (uint32_t u : range) {
        if (constMode && !canU32BitcastToFiniteVec2F16(u)) {
            continue;
        }
        Scalar in = paramKind == ScalarKind::I32 ? i32Bits(u) : u32(u);
        // expected placeholder (overridden by acceptance).
        cases.push_back({{in}, vec2(f16Bits(0), f16Bits(0)), vec2f16AcceptFromU32(u)});
    }
    run(t, builtin(destType), {scalarType(paramKind)}, vecType(2, ScalarKind::F16),
        cfgInputSource(t), 0, cases);
}

CTS_TEST(testGroup, "i32_to_vec2h").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addScalarToVec2hCases(t, ScalarKind::I32, "bitcast<vec2<f16>>");
});

CTS_TEST(testGroup, "u32_to_vec2h").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addScalarToVec2hCases(t, ScalarKind::U32, "bitcast<vec2<f16>>");
});

// f32 -> vec2<f16>. f32 range: f32RangeWithInfAndNaN ++ u32RangeForF16Vec2(reinterpreted as f32).
CTS_TEST(testGroup, "f32_to_vec2h").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const bool constMode = isConst(t);
    // f32RangeWithInfAndNaN = scalarF32Range ++ -0.0 ++ f32InfAndNaNInF32.
    std::vector<uint32_t> f32bitsList;
    for (float f : scalarF32Range()) {
        f32bitsList.push_back(f32AsU32(f));
    }
    f32bitsList.push_back(kBitF32::negZero);
    for (uint32_t u : f32InfAndNaNInU32()) {
        f32bitsList.push_back(u);
    }
    for (uint32_t u : u32RangeForF16Vec2FiniteInfNaN()) {
        f32bitsList.push_back(u); // reinterpreted as f32 (same bits)
    }
    std::vector<Case> cases;
    for (uint32_t u : f32bitsList) {
        const float f = u32AsF32(u);
        const bool finiteF32 = isFiniteF32(static_cast<double>(f));
        if (constMode) {
            // Finite f32 AND bitcasts to finite vec2<f16>.
            if (!finiteF32 || !canU32BitcastToFiniteVec2F16(u)) {
                continue;
            }
        }
        std::vector<ExpectedElement> acc;
        if (!finiteF32) {
            // Non-finite f32 source: both f16 elements unbounded.
            ExpectedElement a;
            a.floatWidth = 16;
            a.any = true;
            acc = {a, a};
        } else {
            acc = vec2f16AcceptFromU32(u);
        }
        cases.push_back({{f32Bits(u)}, vec2(f16Bits(0), f16Bits(0)), acc});
    }
    run(t, builtin("bitcast<vec2<f16>>"), {scalarType(ScalarKind::F32)}, vecType(2, ScalarKind::F16),
        cfgInputSource(t), 0, cases);
});

// ---------------------------------------------------------------------------------------------
// vec2<32-bit> -> vec4<f16>.
// ---------------------------------------------------------------------------------------------

std::vector<ExpectedElement> vec4f16AcceptFromU32x2(uint32_t a, uint32_t b) {
    uint16_t a0, a1, b0, b1;
    u32ToU16x2(a, a0, a1);
    u32ToU16x2(b, b0, b1);
    return {f16ResultFromBits(a0), f16ResultFromBits(a1), f16ResultFromBits(b0),
            f16ResultFromBits(b1)};
}

void addVec2ToVec4hCases(
    AllFeaturesMaxLimitsGpuTest& t,
    ScalarKind paramKind,
    const std::string& destType) {
    const bool constMode = isConst(t);
    std::vector<uint32_t> range = u32RangeForF16Vec2FiniteInfNaN();
    if (constMode) {
        std::vector<uint32_t> filtered;
        for (uint32_t u : range) {
            if (canU32BitcastToFiniteVec2F16(u)) {
                filtered.push_back(u);
            }
        }
        range = filtered;
    }
    std::vector<std::vector<uint32_t>> pairs = slidingSlice(range, 2);
    std::vector<Case> cases;
    for (const auto& p : pairs) {
        const uint32_t a = p[0], b = p[1];
        Scalar e0 = paramKind == ScalarKind::I32 ? i32Bits(a) : u32(a);
        Scalar e1 = paramKind == ScalarKind::I32 ? i32Bits(b) : u32(b);
        cases.push_back({{vec2(e0, e1)},
                         vec4(f16Bits(0), f16Bits(0), f16Bits(0), f16Bits(0)),
                         vec4f16AcceptFromU32x2(a, b)});
    }
    run(t, builtin(destType), {vecType(2, paramKind)}, vecType(4, ScalarKind::F16),
        cfgInputSource(t), 0, cases);
}

CTS_TEST(testGroup, "vec2i_to_vec4h").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addVec2ToVec4hCases(t, ScalarKind::I32, "bitcast<vec4<f16>>");
});

CTS_TEST(testGroup, "vec2u_to_vec4h").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addVec2ToVec4hCases(t, ScalarKind::U32, "bitcast<vec4<f16>>");
});

// vec2f -> vec4h. f32 range as in f32_to_vec2h.
CTS_TEST(testGroup, "vec2f_to_vec4h").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    const bool constMode = isConst(t);
    std::vector<uint32_t> f32bitsList;
    for (float f : scalarF32Range()) {
        f32bitsList.push_back(f32AsU32(f));
    }
    f32bitsList.push_back(kBitF32::negZero);
    for (uint32_t u : f32InfAndNaNInU32()) {
        f32bitsList.push_back(u);
    }
    for (uint32_t u : u32RangeForF16Vec2FiniteInfNaN()) {
        f32bitsList.push_back(u);
    }
    if (constMode) {
        std::vector<uint32_t> filtered;
        for (uint32_t u : f32bitsList) {
            if (isFiniteF32(static_cast<double>(u32AsF32(u))) && canU32BitcastToFiniteVec2F16(u)) {
                filtered.push_back(u);
            }
        }
        f32bitsList = filtered;
    }
    std::vector<std::vector<uint32_t>> pairs = slidingSlice(f32bitsList, 2);
    std::vector<Case> cases;
    for (const auto& p : pairs) {
        const uint32_t a = p[0], b = p[1];
        std::vector<ExpectedElement> acc(4);
        auto setPair = [&](uint32_t v, int base) {
            if (!isFiniteF32(static_cast<double>(u32AsF32(v)))) {
                ExpectedElement any;
                any.floatWidth = 16;
                any.any = true;
                acc[static_cast<size_t>(base)] = any;
                acc[static_cast<size_t>(base) + 1] = any;
            } else {
                uint16_t lo, hi;
                u32ToU16x2(v, lo, hi);
                acc[static_cast<size_t>(base)] = f16ResultFromBits(lo);
                acc[static_cast<size_t>(base) + 1] = f16ResultFromBits(hi);
            }
        };
        setPair(a, 0);
        setPair(b, 2);
        cases.push_back({{vec2(f32Bits(a), f32Bits(b))},
                         vec4(f16Bits(0), f16Bits(0), f16Bits(0), f16Bits(0)), acc});
    }
    run(t, builtin("bitcast<vec4<f16>>"), {vecType(2, ScalarKind::F32)}, vecType(4, ScalarKind::F16),
        cfgInputSource(t), 0, cases);
});

// ---------------------------------------------------------------------------------------------
// vec2<f16> -> 32-bit scalar.
// ---------------------------------------------------------------------------------------------

// Acceptance for a single 32-bit scalar result (i32/u32/f32) bitcasted from a pair of f16 (in u16).
// If either f16 element is Inf/NaN, the 32-bit result is unbounded. Otherwise the accepted bit
// patterns are the cartesian product of each element's {exact} (+ zeros if subnormal).
ExpectedElement scalar32FromF16x2(uint16_t a, uint16_t b, ScalarKind resultKind) {
    ExpectedElement ee;
    if (resultKind == ScalarKind::F32) {
        ee.floatWidth = 32;
    }
    const bool aFinite = isFiniteF16(f16BitsToDouble(a));
    const bool bFinite = isFiniteF16(f16BitsToDouble(b));
    if (!aFinite || !bFinite) {
        ee.any = true;
        return ee;
    }
    // possibleBitsInU16FromFiniteF16: {a} (+ zeros if subnormal).
    auto possible = [](uint16_t h) {
        std::vector<uint16_t> v = {h};
        if (isSubnormalNumberF16(f16BitsToDouble(h))) {
            v.push_back(kBitF16::negZero);
            v.push_back(0);
        }
        return v;
    };
    std::vector<uint16_t> pa = possible(a);
    std::vector<uint16_t> pb = possible(b);
    for (uint16_t x : pa) {
        for (uint16_t y : pb) {
            const uint32_t u = u16x2ToU32(x, y);
            if (resultKind == ScalarKind::F32) {
                const double fv = static_cast<double>(u32AsF32(u));
                if (!isFiniteF32(fv)) {
                    ee.any = true;
                    return ee;
                }
                if (fv == 0.0) {
                    ee.acceptBits.push_back(0u);
                    ee.acceptBits.push_back(kBitF32::negZero);
                    continue;
                }
                ee.acceptBits.push_back(u);
                if (isSubnormalNumberF32(fv)) {
                    ee.acceptBits.push_back(0u);
                    ee.acceptBits.push_back(kBitF32::negZero);
                }
            } else {
                ee.acceptBits.push_back(u); // i32/u32 share the bit pattern.
            }
        }
    }
    return ee;
}

void addVec2hToScalarCases(
    AllFeaturesMaxLimitsGpuTest& t,
    ScalarKind resultKind,
    const std::string& destType) {
    const bool constMode = isConst(t);
    std::vector<uint16_t> finite = scalarF16RangeInU16();
    finite.push_back(kBitF16::negZero);
    std::vector<uint16_t> base = finite;
    if (!constMode) {
        for (uint16_t h : f16InfAndNaNInU16()) {
            base.push_back(h);
        }
    }
    std::vector<std::vector<uint16_t>> pairs = slidingSlice(base, 2);
    std::vector<Case> cases;
    for (const auto& p : pairs) {
        const uint16_t a = p[0], b = p[1];
        // const-mode f32 result also requires the reinterpreted f32 to be finite.
        if (constMode && resultKind == ScalarKind::F32) {
            if (!isFiniteF32(static_cast<double>(u32AsF32(u16x2ToU32(a, b))))) {
                continue;
            }
        }
        Scalar expectedPlaceholder = resultKind == ScalarKind::F32
                                         ? f32Bits(0)
                                         : (resultKind == ScalarKind::I32 ? i32Bits(0) : u32(0));
        cases.push_back({{vec2(f16Bits(a), f16Bits(b))}, expectedPlaceholder,
                         {scalar32FromF16x2(a, b, resultKind)}});
    }
    run(t, builtin(destType), {vecType(2, ScalarKind::F16)}, scalarType(resultKind),
        cfgInputSource(t), 0, cases);
}

CTS_TEST(testGroup, "vec2h_to_i32").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addVec2hToScalarCases(t, ScalarKind::I32, "bitcast<i32>");
});

CTS_TEST(testGroup, "vec2h_to_u32").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addVec2hToScalarCases(t, ScalarKind::U32, "bitcast<u32>");
});

CTS_TEST(testGroup, "vec2h_to_f32").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addVec2hToScalarCases(t, ScalarKind::F32, "bitcast<f32>");
});

// ---------------------------------------------------------------------------------------------
// vec4<f16> -> vec2<32-bit scalar>.
// ---------------------------------------------------------------------------------------------

void addVec4hToVec2Cases(
    AllFeaturesMaxLimitsGpuTest& t,
    ScalarKind resultKind,
    const std::string& destType) {
    const bool constMode = isConst(t);
    std::vector<uint16_t> finite = scalarF16RangeInU16();
    finite.push_back(kBitF16::negZero);
    std::vector<uint16_t> base = finite;
    if (!constMode) {
        for (uint16_t h : f16InfAndNaNInU16()) {
            base.push_back(h);
        }
    }
    std::vector<std::vector<uint16_t>> quads = slidingSlice(base, 4);
    std::vector<Case> cases;
    for (const auto& q : quads) {
        const uint16_t e0 = q[0], e1 = q[1], e2 = q[2], e3 = q[3];
        if (constMode && resultKind == ScalarKind::F32) {
            if (!isFiniteF32(static_cast<double>(u32AsF32(u16x2ToU32(e0, e1)))) ||
                !isFiniteF32(static_cast<double>(u32AsF32(u16x2ToU32(e2, e3))))) {
                continue;
            }
        }
        std::vector<ExpectedElement> acc = {scalar32FromF16x2(e0, e1, resultKind),
                                            scalar32FromF16x2(e2, e3, resultKind)};
        Scalar zero = resultKind == ScalarKind::F32
                          ? f32Bits(0)
                          : (resultKind == ScalarKind::I32 ? i32Bits(0) : u32(0));
        cases.push_back({{vec4(f16Bits(e0), f16Bits(e1), f16Bits(e2), f16Bits(e3))},
                         vec2(zero, zero), acc});
    }
    run(t, builtin(destType), {vecType(4, ScalarKind::F16)}, vecType(2, resultKind),
        cfgInputSource(t), 0, cases);
}

CTS_TEST(testGroup, "vec4h_to_vec2i").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addVec4hToVec2Cases(t, ScalarKind::I32, "bitcast<vec2<i32>>");
});

CTS_TEST(testGroup, "vec4h_to_vec2u").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addVec4hToVec2Cases(t, ScalarKind::U32, "bitcast<vec2<u32>>");
});

CTS_TEST(testGroup, "vec4h_to_vec2f").params(inputSourceAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    addVec4hToVec2Cases(t, ScalarKind::F32, "bitcast<vec2<f32>>");
});

// ---------------------------------------------------------------------------------------------
// Abstract Float (const only).
// ---------------------------------------------------------------------------------------------

CTS_TEST(testGroup, "af_to_f32").params(constVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    // scalarF32Range, each accepting the value or its subnormal-flushed-to-zero form.
    std::vector<Case> cases;
    for (float f : scalarF32Range()) {
        const uint32_t bits = f32AsU32(f);
        cases.push_back(
            {{abstractFloatBits(bits)}, f32Bits(bits), {f32ResultFromValue(static_cast<double>(f), bits)}});
    }
    run(t, bitcastBuilder("f32", cfgVectorize(t)), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::F32), InputSource::Const, cfgVectorize(t), cases);
});

// af_to_i32 / af_to_u32 use a fixed list of f32-as-u32 values (upstream uses u32Bits patterns).
const uint32_t kAfToIntValues[] = {
    0u, 1u, 10u, 256u,
    0b11111111011111111111111111111111u, 0b11111111010000000000000000000000u,
    0b11111110110000000000000000000000u, 0b11111101110000000000000000000000u,
    0b11111011110000000000000000000000u, 0b11110111110000000000000000000000u,
    0b11101111110000000000000000000000u, 0b11011111110000000000000000000000u,
    0b10111111110000000000000000000000u, 0b01111111011111111111111111111111u,
    0b01111111010000000000000000000000u, 0b01111110110000000000000000000000u,
    0b01111101110000000000000000000000u, 0b01111011110000000000000000000000u,
    0b01110111110000000000000000000000u, 0b01101111110000000000000000000000u,
    0b01011111110000000000000000000000u, 0b00111111110000000000000000000000u};

CTS_TEST(testGroup, "af_to_i32").params(constVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t u : kAfToIntValues) {
        cases.push_back({{abstractFloatBits(u)}, i32Bits(u)});
    }
    run(t, bitcastBuilder("i32", cfgVectorize(t)), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::I32), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(testGroup, "af_to_u32").params(constVectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (uint32_t u : kAfToIntValues) {
        cases.push_back({{abstractFloatBits(u)}, u32(u)});
    }
    run(t, bitcastBuilder("u32", cfgVectorize(t)), {scalarType(ScalarKind::AbstractFloat)},
        scalarType(ScalarKind::U32), InputSource::Const, cfgVectorize(t), cases);
});

// af -> vec2<f16>: f32FiniteRangeForF16Vec2Finite values reinterpreted.
std::vector<uint32_t> f32FiniteRangeForF16Vec2Finite() {
    std::vector<uint32_t> f32bitsList;
    for (float f : scalarF32Range()) {
        f32bitsList.push_back(f32AsU32(f));
    }
    f32bitsList.push_back(kBitF32::negZero);
    for (uint32_t u : f32InfAndNaNInU32()) {
        f32bitsList.push_back(u);
    }
    for (uint32_t u : u32RangeForF16Vec2FiniteInfNaN()) {
        f32bitsList.push_back(u);
    }
    std::vector<uint32_t> out;
    for (uint32_t u : f32bitsList) {
        if (isFiniteF32(static_cast<double>(u32AsF32(u))) && canU32BitcastToFiniteVec2F16(u)) {
            out.push_back(u);
        }
    }
    return out;
}

CTS_TEST(testGroup, "af_to_vec2f16").params([](ParamsBuilder u) {
    return u.combine("inputSource", {"const"});
}).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    std::vector<Case> cases;
    for (uint32_t u : f32FiniteRangeForF16Vec2Finite()) {
        cases.push_back({{abstractFloatBits(u)}, vec2(f16Bits(0), f16Bits(0)), vec2f16AcceptFromU32(u)});
    }
    run(t, builtin("bitcast<vec2<f16>>"), {scalarType(ScalarKind::AbstractFloat)},
        vecType(2, ScalarKind::F16), InputSource::Const, 0, cases);
});

CTS_TEST(testGroup, "vec2af_to_vec4f16").params([](ParamsBuilder u) {
    return u.combine("inputSource", {"const"});
}).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    std::vector<std::vector<uint32_t>> pairs = slidingSlice(f32FiniteRangeForF16Vec2Finite(), 2);
    std::vector<Case> cases;
    for (const auto& p : pairs) {
        const uint32_t a = p[0], b = p[1];
        cases.push_back({{vec2(abstractFloatBits(a), abstractFloatBits(b))},
                         vec4(f16Bits(0), f16Bits(0), f16Bits(0), f16Bits(0)),
                         vec4f16AcceptFromU32x2(a, b)});
    }
    run(t, builtin("bitcast<vec4<f16>>"), {vecType(2, ScalarKind::AbstractFloat)},
        vecType(4, ScalarKind::F16), InputSource::Const, 0, cases);
});

// ---------------------------------------------------------------------------------------------
// Abstract Int (const only). Converted to i32 (no explicit overload).
// ---------------------------------------------------------------------------------------------

CTS_TEST(testGroup, "ai_to_i32").params(constVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    for (int32_t e : fullI32Range()) {
        cases.push_back({{abstractInt(e)}, i32(e)});
    }
    run(t, bitcastBuilder("i32", cfgVectorize(t)), {scalarType(ScalarKind::AbstractInt)},
        scalarType(ScalarKind::I32), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(testGroup, "ai_to_u32").params(constVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    std::vector<Case> cases;
    // fullU32Range, but AbstractInt is emitted as a signed literal; bitcast<u32> of it gives u.
    for (uint32_t e : fullU32Range()) {
        cases.push_back({{abstractInt(static_cast<int32_t>(e))}, u32(e)});
    }
    run(t, bitcastBuilder("u32", cfgVectorize(t)), {scalarType(ScalarKind::AbstractInt)},
        scalarType(ScalarKind::U32), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(testGroup, "ai_to_f32").params(constVectorizeAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    // i32RangeForF32Finite: fullI32Range ++ f32ZerosInI32 ++ f32InfAndNaNInI32, finite-f32 only.
    std::vector<uint32_t> u32vals;
    for (int32_t e : fullI32Range()) {
        u32vals.push_back(static_cast<uint32_t>(e));
    }
    for (uint32_t z : kF32ZerosInU32) {
        u32vals.push_back(z);
    }
    for (uint32_t n : f32InfAndNaNInU32()) {
        u32vals.push_back(n);
    }
    std::vector<Case> cases;
    for (uint32_t u : u32vals) {
        const float f = u32AsF32(u);
        if (!isFiniteF32(static_cast<double>(f))) {
            continue;
        }
        cases.push_back({{abstractInt(static_cast<int32_t>(u))}, f32Bits(u),
                         {f32ResultFromValue(static_cast<double>(f), u)}});
    }
    run(t, bitcastBuilder("f32", cfgVectorize(t)), {scalarType(ScalarKind::AbstractInt)},
        scalarType(ScalarKind::F32), InputSource::Const, cfgVectorize(t), cases);
});

CTS_TEST(testGroup, "ai_to_vec2h").params(constAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    // i32RangeForF16Vec2Finite: u32RangeForF16Vec2FiniteInfNaN filtered to finite vec2<f16>.
    std::vector<Case> cases;
    for (uint32_t u : u32RangeForF16Vec2FiniteInfNaN()) {
        if (!canU32BitcastToFiniteVec2F16(u)) {
            continue;
        }
        cases.push_back({{abstractInt(static_cast<int32_t>(u))}, vec2(f16Bits(0), f16Bits(0)),
                         vec2f16AcceptFromU32(u)});
    }
    run(t, builtin("bitcast<vec2<f16>>"), {scalarType(ScalarKind::AbstractInt)},
        vecType(2, ScalarKind::F16), InputSource::Const, 0, cases);
});

CTS_TEST(testGroup, "vec2ai_to_vec4h").params(constAliasParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    requireF16(t);
    std::vector<uint32_t> range;
    for (uint32_t u : u32RangeForF16Vec2FiniteInfNaN()) {
        if (canU32BitcastToFiniteVec2F16(u)) {
            range.push_back(u);
        }
    }
    std::vector<std::vector<uint32_t>> pairs = slidingSlice(range, 2);
    std::vector<Case> cases;
    for (const auto& p : pairs) {
        const uint32_t a = p[0], b = p[1];
        cases.push_back({{vec2(abstractInt(static_cast<int32_t>(a)), abstractInt(static_cast<int32_t>(b)))},
                         vec4(f16Bits(0), f16Bits(0), f16Bits(0), f16Bits(0)),
                         vec4f16AcceptFromU32x2(a, b)});
    }
    run(t, builtin("bitcast<vec4<f16>>"), {vecType(2, ScalarKind::AbstractInt)},
        vecType(4, ScalarKind::F16), InputSource::Const, 0, cases);
});

} // namespace
