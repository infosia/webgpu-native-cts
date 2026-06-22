// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/const_override_validation.ts
// and src/webgpu/util/{math,constants,conversion}.ts @
// b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Faithful port of the shared helpers used by the builtin call validation specs
// (abs/acos/.../select). Reuses binary_types.h (Type model, create(N).wgsl()
// spellings) and const_override.h (withPoint, quantizeToF16/F32, reinterpret
// helpers). Adds:
//   - the dense float/int range generators from util/math.ts
//     (linearRange/lerp, linearRangeBigInt/lerpBigInt, scalarF32/F16/F64Range)
//     and the kBit/kValue constants they need (util/constants.ts);
//   - rangeForType / minusTwoToTwoRangeForType / minusThreePiToThreePiRangeForType
//     / sparseMinusThreePiToThreePiRangeForType / fullRangeForType / unique;
//   - the BuiltinValue model (a typed value carrying its scalar elements and
//     the create(value).wgsl() spelling — fills ALL elements with `value`),
//     and validateConstOrOverrideBuiltinEval / stageSupportsType /
//     kConstantAndOverrideStages;
//   - ConstantOrOverrideValueChecker (overflow/near-zero quantization guard).
//
// Range-value encoding for harness params (Value can hold int64_t or double):
// float element kinds (abstract-float/f32/f16) key the swept `value` as a
// `double`; integer element kinds (abstract-int/i32/u32) key it as `int64_t`.
// The .fn() reconstructs the numeric value from whichever variant is present.
//
// Number formatting: withPoint() (from const_override.h) uses std::to_chars
// shortest round-trip; the compiled VALUE is identical to V8's, so const/
// override evaluation (and thus pass/fail) is unaffected (see const_override.h).

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/expression/binary/const_override.h"
#include "webgpu/shader/validation/shader_validation_test.h"

namespace cts::shader_validation::builtin {

namespace bt = cts::shader_validation::binary;
using bt::ScalarKind;
using bt::Type;

// ---------------------------------------------------------------------------
// Constants (util/constants.ts kBit / kValue subset)
// ---------------------------------------------------------------------------
struct KBit {
    // f32 bit patterns
    static constexpr uint32_t f32_pos_min = 0x00800000;
    static constexpr uint32_t f32_pos_max = 0x7f7fffff;
    static constexpr uint32_t f32_pos_sub_min = 0x00000001;
    static constexpr uint32_t f32_pos_sub_max = 0x007fffff;
    static constexpr uint32_t f32_neg_max = 0x80800000;
    static constexpr uint32_t f32_neg_min = 0xff7fffff;
    static constexpr uint32_t f32_neg_sub_max = 0x80000001;
    static constexpr uint32_t f32_neg_sub_min = 0x807fffff;
    // f16 bit patterns
    static constexpr uint16_t f16_pos_min = 0x0400;
    static constexpr uint16_t f16_pos_max = 0x7bff;
    static constexpr uint16_t f16_pos_sub_min = 0x0001;
    static constexpr uint16_t f16_pos_sub_max = 0x03ff;
    static constexpr uint16_t f16_neg_max = 0x8400;
    static constexpr uint16_t f16_neg_min = 0xfbff;
    static constexpr uint16_t f16_neg_sub_max = 0x8001;
    static constexpr uint16_t f16_neg_sub_min = 0x83ff;
    // f64 bit patterns (as uint64)
    static constexpr uint64_t f64_pos_min = 0x0010000000000000ull;
    static constexpr uint64_t f64_pos_max = 0x7fefffffffffffffull;
    static constexpr uint64_t f64_pos_sub_min = 0x0000000000000001ull;
    static constexpr uint64_t f64_pos_sub_max = 0x000fffffffffffffull;
    static constexpr uint64_t f64_neg_max = 0x8010000000000000ull;
    static constexpr uint64_t f64_neg_min = 0xffefffffffffffffull;
    static constexpr uint64_t f64_neg_sub_max = 0x8000000000000001ull;
    static constexpr uint64_t f64_neg_sub_min = 0x800fffffffffffffull;
};

// Float value-domain limits (util/constants.ts kValue.{f32,f16}).
struct FloatLimits {
    double posMax;
    double posMin;
    double negMin;
    double negMax;
    int emax;
};
inline const FloatLimits& kValueF32() {
    static const FloatLimits v{
        binary::reinterpretU32AsF32(KBit::f32_pos_max),  // positive.max
        binary::reinterpretU32AsF32(KBit::f32_pos_min),  // positive.min
        -binary::reinterpretU32AsF32(KBit::f32_pos_max), // negative.min == -posMax
        -binary::reinterpretU32AsF32(KBit::f32_pos_min), // negative.max == -posMin
        127};
    return v;
}
inline const FloatLimits& kValueF16() {
    static const FloatLimits v{
        binary::reinterpretU16AsF16(KBit::f16_pos_max),
        binary::reinterpretU16AsF16(KBit::f16_pos_min),
        -binary::reinterpretU16AsF16(KBit::f16_pos_max),
        -binary::reinterpretU16AsF16(KBit::f16_pos_min),
        15};
    return v;
}
inline constexpr int32_t kI32Min = -2147483647 - 1;
inline constexpr int32_t kI32Max = 2147483647;
inline constexpr uint32_t kU32Max = 4294967295u;
inline constexpr int64_t kI64Min = INT64_MIN;
inline constexpr int64_t kI64Max = INT64_MAX;

// ---------------------------------------------------------------------------
// reinterpretU64AsF64
// ---------------------------------------------------------------------------
inline double reinterpretU64AsF64(uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, sizeof(d));
    return d;
}

// ---------------------------------------------------------------------------
// lerp / linearRange (util/math.ts) — operates on doubles (bit fields are
// passed as doubles, matching JS).
// ---------------------------------------------------------------------------
inline double lerp(double a, double b, double t) {
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
    return ((t > 1.0) == (b > a)) ? std::max(b, x) : std::min(b, x);
}

inline std::vector<double> linearRange(double a, double b, int numSteps) {
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

// ---------------------------------------------------------------------------
// Portable signed 128-bit mul/div for lerpBigInt (avoids __int128 / MSVC gaps).
// Computes (a * b) / c with a,b,c 64-bit signed, exact (intermediate fits 128).
// ---------------------------------------------------------------------------
inline int64_t mulDivS64(int64_t a, int64_t b, int64_t c) {
    const bool neg = (a < 0) ^ (b < 0) ^ (c < 0);
    auto absu = [](int64_t v) -> uint64_t {
        return v < 0 ? (~static_cast<uint64_t>(v) + 1ull) : static_cast<uint64_t>(v);
    };
    uint64_t ua = absu(a), ub = absu(b), uc = absu(c);
    // 128-bit product ua*ub.
    const uint64_t aLo = ua & 0xffffffffull, aHi = ua >> 32;
    const uint64_t bLo = ub & 0xffffffffull, bHi = ub >> 32;
    uint64_t ll = aLo * bLo;
    uint64_t lh = aLo * bHi;
    uint64_t hl = aHi * bLo;
    uint64_t hh = aHi * bHi;
    uint64_t mid = (ll >> 32) + (lh & 0xffffffffull) + (hl & 0xffffffffull);
    uint64_t lo = (ll & 0xffffffffull) | (mid << 32);
    uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    // Divide (hi:lo) by uc via long division.
    uint64_t q = 0, rem = 0;
    for (int bit = 127; bit >= 0; --bit) {
        rem = (rem << 1) | ((bit >= 64 ? (hi >> (bit - 64)) : (lo >> bit)) & 1ull);
        if (rem >= uc) {
            rem -= uc;
            if (bit < 64) {
                q |= (1ull << bit);
            }
        }
    }
    const int64_t res = static_cast<int64_t>(q);
    return neg ? -res : res;
}

inline int64_t lerpBigInt(int64_t a, int64_t b, int idx, int steps) {
    if (steps == 1 || idx == 0) {
        return a;
    }
    if (idx == steps - 1) {
        return b;
    }
    const int64_t bigIdx = idx;
    const int64_t bigStepsM1 = steps - 1;
    if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0)) {
        return mulDivS64(b, bigIdx, bigStepsM1) + (a - mulDivS64(a, bigIdx, bigStepsM1));
    }
    const int64_t x = a + mulDivS64(b, bigIdx, bigStepsM1) - mulDivS64(a, bigIdx, bigStepsM1);
    if (!(b > a)) {
        return std::max(b, x);
    }
    return std::min(b, x);
}

inline std::vector<int64_t> linearRangeBigInt(int64_t a, int64_t b, int numSteps) {
    std::vector<int64_t> out;
    if (numSteps <= 0) {
        return out;
    }
    if (numSteps == 1) {
        out.push_back(a);
        return out;
    }
    out.reserve(static_cast<size_t>(numSteps));
    for (int i = 0; i < numSteps; ++i) {
        out.push_back(lerpBigInt(a, b, i, numSteps));
    }
    return out;
}

// ---------------------------------------------------------------------------
// scalarF32Range / scalarF16Range / scalarF64Range (util/math.ts)
// ---------------------------------------------------------------------------
inline double jsTrunc(double v) { return std::trunc(v); }

inline std::vector<double> scalarF32Range(int posSub, int posNorm) {
    const int negNorm = posNorm;
    const int negSub = posSub;
    std::vector<double> special;
    if (posNorm >= 4) {
        special = {static_cast<double>(0x4effffff), static_cast<double>(0x4f7fffff)};
    }
    std::vector<double> bits;
    auto append = [&](const std::vector<double>& v) {
        bits.insert(bits.end(), v.begin(), v.end());
    };
    append(linearRange(KBit::f32_neg_min, KBit::f32_neg_max, negNorm));
    append(linearRange(KBit::f32_neg_sub_min, KBit::f32_neg_sub_max, negSub));
    bits.push_back(static_cast<double>(0x80000000u));
    bits.push_back(0.0);
    append(linearRange(KBit::f32_pos_sub_min, KBit::f32_pos_sub_max, posSub));
    // positive normals: linearRange then concat special, then sorted ascending.
    std::vector<double> posNormals =
        linearRange(KBit::f32_pos_min, KBit::f32_pos_max,
                    posNorm - static_cast<int>(special.size()));
    posNormals.insert(posNormals.end(), special.begin(), special.end());
    std::sort(posNormals.begin(), posNormals.end());
    append(posNormals);
    std::vector<double> out;
    out.reserve(bits.size());
    for (double b : bits) {
        out.push_back(binary::reinterpretU32AsF32(static_cast<uint32_t>(jsTrunc(b))));
    }
    return out;
}

inline std::vector<double> scalarF16Range(int posSub, int posNorm) {
    const int negNorm = posNorm;
    const int negSub = posSub;
    std::vector<double> bits;
    auto append = [&](const std::vector<double>& v) {
        bits.insert(bits.end(), v.begin(), v.end());
    };
    append(linearRange(KBit::f16_neg_min, KBit::f16_neg_max, negNorm));
    append(linearRange(KBit::f16_neg_sub_min, KBit::f16_neg_sub_max, negSub));
    bits.push_back(static_cast<double>(0x8000));
    bits.push_back(0.0);
    append(linearRange(KBit::f16_pos_sub_min, KBit::f16_pos_sub_max, posSub));
    append(linearRange(KBit::f16_pos_min, KBit::f16_pos_max, posNorm));
    std::vector<double> out;
    out.reserve(bits.size());
    for (double b : bits) {
        out.push_back(binary::reinterpretU16AsF16(static_cast<uint16_t>(jsTrunc(b))));
    }
    return out;
}

inline std::vector<double> scalarF64Range(int posSub, int posNorm) {
    const int negNorm = posNorm;
    const int negSub = posSub;
    std::vector<int64_t> bits;
    auto append = [&](const std::vector<int64_t>& v) {
        bits.insert(bits.end(), v.begin(), v.end());
    };
    append(linearRangeBigInt(static_cast<int64_t>(KBit::f64_neg_min),
                             static_cast<int64_t>(KBit::f64_neg_max), negNorm));
    append(linearRangeBigInt(static_cast<int64_t>(KBit::f64_neg_sub_min),
                             static_cast<int64_t>(KBit::f64_neg_sub_max), negSub));
    bits.push_back(static_cast<int64_t>(0x8000000000000000ull));
    bits.push_back(0);
    append(linearRangeBigInt(static_cast<int64_t>(KBit::f64_pos_sub_min),
                             static_cast<int64_t>(KBit::f64_pos_sub_max), posSub));
    append(linearRangeBigInt(static_cast<int64_t>(KBit::f64_pos_min),
                             static_cast<int64_t>(KBit::f64_pos_max), posNorm));
    std::vector<double> out;
    out.reserve(bits.size());
    for (int64_t b : bits) {
        out.push_back(reinterpretU64AsF64(static_cast<uint64_t>(b)));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Range-for-type helpers (const_override_validation.ts)
// ---------------------------------------------------------------------------
inline bool isFloatKind(ScalarKind k) {
    return k == ScalarKind::AbstractFloat || k == ScalarKind::F32 || k == ScalarKind::F16;
}
inline bool isAbstractIntKind(ScalarKind k) { return k == ScalarKind::AbstractInt; }

// A swept value carries either a double (float element kinds) or an int64
// (integer element kinds). Mirrors the `number | bigint` union upstream sweeps.
struct RangeValue {
    bool isInt = false;
    double d = 0.0;
    int64_t i = 0;
    static RangeValue makeD(double v) { return RangeValue{false, v, 0}; }
    static RangeValue makeI(int64_t v) { return RangeValue{true, 0.0, v}; }
    cts::Value toValue() const { return isInt ? cts::Value(i) : cts::Value(d); }
};

// rangeForType(number_range, bigint_range): pick by element kind.
inline std::vector<RangeValue> rangeForType(const Type& type, const std::vector<double>& numberRange,
                                            const std::vector<int64_t>& bigintRange) {
    const ScalarKind k = bt::scalarTypeOf(type).kind;
    std::vector<RangeValue> out;
    if (isFloatKind(k)) {
        for (double v : numberRange) {
            out.push_back(RangeValue::makeD(v));
        }
    } else if (isAbstractIntKind(k)) {
        for (int64_t v : bigintRange) {
            out.push_back(RangeValue::makeI(v));
        }
    }
    return out;
}

inline std::vector<RangeValue> minusTwoToTwoRangeForType(const Type& type) {
    return rangeForType(type, linearRange(-2.0, 2.0, 10), {-2, -1, 0, 1, 2});
}

inline std::vector<RangeValue> minusThreePiToThreePiRangeForType(const Type& type) {
    const double PI = 3.141592653589793;
    const std::vector<double> nums = {
        -3 * PI,      -2.999 * PI,  -2.501 * PI, -2.5 * PI,   -2.499 * PI, -2.001 * PI,
        -2.0 * PI,    -1.999 * PI,  -1.501 * PI, -1.5 * PI,   -1.499 * PI, -1.001 * PI,
        -1.0 * PI,    -0.999 * PI,  -0.501 * PI, -0.5 * PI,   -0.499 * PI, -0.001,
        0,            0.001,        0.499 * PI,  0.5 * PI,    0.501 * PI,  0.999 * PI,
        1.0 * PI,     1.001 * PI,   1.499 * PI,  1.5 * PI,    1.501 * PI,  1.999 * PI,
        2.0 * PI,     2.001 * PI,   2.499 * PI,  2.5 * PI,    2.501 * PI,  2.999 * PI,
        3 * PI,
    };
    return rangeForType(type, nums, {-2, -1, 0, 1, 2});
}

inline std::vector<RangeValue> sparseMinusThreePiToThreePiRangeForType(const Type& type) {
    const double PI = 3.141592653589793;
    const std::vector<double> nums = {
        -3 * PI,   -2.5 * PI, -2.0 * PI, -1.5 * PI, -1.0 * PI, -0.5 * PI, 0,
        0.5 * PI,  PI,        1.5 * PI,  2.0 * PI,  2.5 * PI,  3 * PI,
    };
    return rangeForType(type, nums, {-2, -1, 0, 1, 2});
}

// fullRangeForType (const_override_validation.ts). count defaults to 25.
inline std::vector<RangeValue> fullRangeForType(const Type& type, int count = 25) {
    const ScalarKind k = bt::scalarTypeOf(type).kind;
    const int posSub = static_cast<int>(std::ceil((count * 1.0) / 5.0));
    const int posNorm = static_cast<int>(std::ceil((count * 4.0) / 5.0));
    std::vector<RangeValue> out;
    switch (k) {
        case ScalarKind::AbstractFloat:
            for (double v : scalarF64Range(posSub, posNorm)) {
                out.push_back(RangeValue::makeD(v));
            }
            break;
        case ScalarKind::F32:
            for (double v : scalarF32Range(posSub, posNorm)) {
                out.push_back(RangeValue::makeD(v));
            }
            break;
        case ScalarKind::F16:
            for (double v : scalarF16Range(posSub, posNorm)) {
                out.push_back(RangeValue::makeD(v));
            }
            break;
        case ScalarKind::I32:
            for (double v : linearRange(static_cast<double>(kI32Min), static_cast<double>(kI32Max),
                                        count)) {
                out.push_back(RangeValue::makeI(static_cast<int64_t>(std::floor(v))));
            }
            break;
        case ScalarKind::U32:
            for (double v : linearRange(0.0, static_cast<double>(kU32Max), count)) {
                out.push_back(RangeValue::makeI(static_cast<int64_t>(std::floor(v))));
            }
            break;
        case ScalarKind::AbstractInt:
            for (int64_t v : linearRangeBigInt(kI64Min, kI64Max, count)) {
                out.push_back(RangeValue::makeI(v));
            }
            break;
        default:
            break;
    }
    return out;
}

// unique(...arrays) preserving first-seen order. Floats compared by bit pattern
// (matches JS Set identity for distinct numbers); ints by value.
inline std::vector<RangeValue> uniqueRanges(const std::vector<std::vector<RangeValue>>& arrays) {
    std::vector<RangeValue> out;
    std::set<int64_t> seenI;
    std::set<uint64_t> seenD;
    for (const auto& arr : arrays) {
        for (const RangeValue& rv : arr) {
            if (rv.isInt) {
                if (seenI.insert(rv.i).second) {
                    out.push_back(rv);
                }
            } else {
                uint64_t bits;
                std::memcpy(&bits, &rv.d, sizeof(bits));
                if (seenD.insert(bits).second) {
                    out.push_back(rv);
                }
            }
        }
    }
    return out;
}

// Reconstruct a RangeValue from a swept harness Value (int64 => integer kind,
// double => float kind).
inline RangeValue rangeValueFromParam(const cts::Value& v) {
    if (std::holds_alternative<int64_t>(v.data())) {
        return RangeValue::makeI(std::get<int64_t>(v.data()));
    }
    return RangeValue::makeD(std::get<double>(v.data()));
}

// Convert a RangeValue list to harness Values for an .expand().
inline std::vector<cts::Value> rangeValues(const std::vector<RangeValue>& range) {
    std::vector<cts::Value> out;
    out.reserve(range.size());
    for (const RangeValue& rv : range) {
        out.push_back(rv.toValue());
    }
    return out;
}

// ---------------------------------------------------------------------------
// stages
// ---------------------------------------------------------------------------
inline std::vector<cts::Value> kConstantAndOverrideStages() {
    return {cts::Value(std::string("constant")), cts::Value(std::string("override"))};
}

// stageSupportsType: override stage rejects abstract element types.
inline bool stageSupportsType(const std::string& stage, const Type& type) {
    if (stage == "override" && bt::isAbstractType(bt::scalarTypeOf(type))) {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Matrix type model (conversion.ts MatrixType). Matrices are always float.
// toString() == matCxR<elementKind>. create(value) fills cols*rows elements.
// ---------------------------------------------------------------------------
struct MatType {
    int cols = 0;
    int rows = 0;
    ScalarKind kind = ScalarKind::F32;
    std::string toString() const {
        return "mat" + std::to_string(cols) + "x" + std::to_string(rows) + "<" +
               bt::scalarKindString(kind) + ">";
    }
};
// kAllMatrices order: kMatTypes object order = (2x2f,2x2h,3x2f,3x2h,4x2f,4x2h,
// 2x3f,2x3h,3x3f,3x3h,4x3f,4x3h,2x4f,2x4h,3x4f,3x4h,4x4f,4x4h).
inline const std::vector<MatType>& kAllMatrices() {
    static std::vector<MatType> v = [] {
        std::vector<MatType> out;
        const int rowsOrder[] = {2, 3, 4};
        const int colsOrder[] = {2, 3, 4};
        for (int r : rowsOrder) {
            for (int c : colsOrder) {
                out.push_back(MatType{c, r, ScalarKind::F32});
                out.push_back(MatType{c, r, ScalarKind::F16});
            }
        }
        return out;
    }();
    return v;
}
inline std::vector<cts::Value> matrixKeys(const std::vector<MatType>& table) {
    std::vector<cts::Value> out;
    for (const MatType& m : table) {
        out.emplace_back(m.toString());
    }
    return out;
}
inline MatType matByName(const std::string& name) {
    for (const MatType& m : kAllMatrices()) {
        if (m.toString() == name) {
            return m;
        }
    }
    return MatType{};
}

// ---------------------------------------------------------------------------
// BuiltinValue: a typed value with all elements equal to `value` (create()).
// May be a scalar/vector (Type) or a matrix (MatType).
// ---------------------------------------------------------------------------
struct BuiltinValue {
    Type type;
    std::vector<double> elements;  // float elements stored as double; int as exact double/int64
    std::vector<int64_t> intElements;  // for integer kinds (exact)
    bool isInt = false;
    bool isMatrix = false;
    MatType mat;  // valid when isMatrix
};

// quantize a numeric value to the element kind's stored representation (mirrors
// f32()/f16()/abstractFloat() factory quantization).
inline double quantizeToKind(ScalarKind k, double v) {
    switch (k) {
        case ScalarKind::F32:
            return binary::quantizeToF32(v);
        case ScalarKind::F16:
            return binary::quantizeToF16(v);
        default:
            return v;  // abstract-float keeps f64
    }
}

// create(type, RangeValue): scalar or vector, all elements = value.
inline BuiltinValue createBuiltinValue(const Type& type, const RangeValue& rv) {
    BuiltinValue v;
    v.type = type;
    const ScalarKind k = type.kind;
    const int n = type.isScalar() ? 1 : type.width;
    if (isFloatKind(k)) {
        v.isInt = false;
        const double q = quantizeToKind(k, rv.isInt ? static_cast<double>(rv.i) : rv.d);
        v.elements.assign(static_cast<size_t>(n), q);
    } else {
        v.isInt = true;
        int64_t iv = rv.isInt ? rv.i : static_cast<int64_t>(rv.d);
        // i32/u32 wrap; abstract-int exact.
        if (k == ScalarKind::I32) {
            iv = static_cast<int32_t>(iv);
        } else if (k == ScalarKind::U32) {
            iv = static_cast<int64_t>(static_cast<uint32_t>(iv));
        }
        v.intElements.assign(static_cast<size_t>(n), iv);
    }
    return v;
}

// create(type, [v0,v1,...]): vector with per-element values (VectorType.create
// with an array argument). `elems.size()` must equal the vector width.
inline BuiltinValue createBuiltinValueVec(const Type& type, const std::vector<RangeValue>& elems) {
    BuiltinValue v;
    v.type = type;
    const ScalarKind k = type.kind;
    if (isFloatKind(k)) {
        v.isInt = false;
        for (const RangeValue& rv : elems) {
            v.elements.push_back(quantizeToKind(k, rv.isInt ? static_cast<double>(rv.i) : rv.d));
        }
    } else {
        v.isInt = true;
        for (const RangeValue& rv : elems) {
            int64_t iv = rv.isInt ? rv.i : static_cast<int64_t>(rv.d);
            if (k == ScalarKind::I32) {
                iv = static_cast<int32_t>(iv);
            } else if (k == ScalarKind::U32) {
                iv = static_cast<int64_t>(static_cast<uint32_t>(iv));
            }
            v.intElements.push_back(iv);
        }
    }
    return v;
}

// create(matType, value): matrix filled with `value` in all cols*rows elements.
inline BuiltinValue createMatrixValue(const MatType& mat, double value) {
    BuiltinValue v;
    v.isMatrix = true;
    v.mat = mat;
    v.isInt = false;
    v.elements.assign(static_cast<size_t>(mat.cols * mat.rows), quantizeToKind(mat.kind, value));
    return v;
}

// Element wgsl spelling (mirrors conversion.ts ScalarValue.wgsl()).
inline std::string elementWgslFloat(ScalarKind k, double v) {
    switch (k) {
        case ScalarKind::AbstractFloat:
            return binary::withPoint(v);
        case ScalarKind::F32:
            return binary::withPoint(v) + "f";
        case ScalarKind::F16:
            return binary::withPoint(v) + "h";
        default:
            return binary::withPoint(v);
    }
}
inline std::string elementWgslInt(ScalarKind k, int64_t v) {
    switch (k) {
        case ScalarKind::Bool:
            return v != 0 ? "true" : "false";
        case ScalarKind::AbstractInt:
            // WGSL parses -N as negate(N); the abstract-int min is special-cased.
            if (v == kI64Min) {
                return "(-9223372036854775807 - 1)";
            }
            return std::to_string(v);
        case ScalarKind::I32:
            return "i32(" + std::to_string(v) + ")";
        case ScalarKind::U32:
            return std::to_string(static_cast<uint32_t>(v)) + "u";
        default:
            return std::to_string(v);
    }
}

inline std::string builtinValueWgsl(const BuiltinValue& v) {
    if (v.isMatrix) {
        std::string els;
        for (size_t i = 0; i < v.elements.size(); ++i) {
            if (i) {
                els += ", ";
            }
            els += elementWgslFloat(v.mat.kind, v.elements[i]);
        }
        return "mat" + std::to_string(v.mat.cols) + "x" + std::to_string(v.mat.rows) + "(" + els +
               ")";
    }
    auto el = [&](size_t i) -> std::string {
        return v.isInt ? elementWgslInt(v.type.kind, v.intElements[i])
                       : elementWgslFloat(v.type.kind, v.elements[i]);
    };
    if (v.type.isScalar()) {
        return el(0);
    }
    std::string els;
    const size_t n = v.isInt ? v.intElements.size() : v.elements.size();
    for (size_t i = 0; i < n; ++i) {
        if (i) {
            els += ", ";
        }
        els += el(i);
    }
    return "vec" + std::to_string(v.type.width) + "(" + els + ")";
}

// scalar elements as numeric (for override constants).
inline std::vector<double> builtinScalarElements(const BuiltinValue& v) {
    std::vector<double> out;
    if (v.isInt) {
        for (int64_t e : v.intElements) {
            out.push_back(static_cast<double>(e));
        }
    } else {
        out = v.elements;
    }
    return out;
}

// ---------------------------------------------------------------------------
// validateConstOrOverrideBuiltinEval
// ---------------------------------------------------------------------------
inline ScalarKind argElementKind(const BuiltinValue& a) {
    return a.isMatrix ? a.mat.kind : a.type.kind;
}
inline std::string argTypeString(const BuiltinValue& a) {
    return a.isMatrix ? a.mat.toString() : a.type.toString();
}
inline bool anyArgF16(const std::vector<BuiltinValue>& args) {
    for (const BuiltinValue& a : args) {
        if (argElementKind(a) == ScalarKind::F16) {
            return true;
        }
    }
    return false;
}

// returnType: optional explicit `: T` annotation (empty => implicit).
inline void validateConstOrOverrideBuiltinEval(ShaderValidationTest& t, const std::string& builtin,
                                               bool expectedResult,
                                               const std::vector<BuiltinValue>& args,
                                               const std::string& stage,
                                               const std::string& returnType = "") {
    const std::string enables = anyArgF16(args) ? "enable f16;" : "";
    const std::string optionalVarType = returnType.empty() ? "" : (": " + returnType);

    if (stage == "constant") {
        std::string callArgs;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i) {
                callArgs += ", ";
            }
            callArgs += builtinValueWgsl(args[i]);
        }
        const std::string code =
            enables + "\nconst v " + optionalVarType + " = " + builtin + "(" + callArgs + ");";
        t.expectCompileResult(expectedResult, code);
        return;
    }

    // override
    std::map<std::string, double> constants;
    std::vector<std::string> overrideDecls;
    std::vector<std::string> callArgsList;
    int numOverrides = 0;
    for (const BuiltinValue& arg : args) {
        std::vector<std::string> argOverrides;
        const std::vector<double> els = builtinScalarElements(arg);
        const std::string elemTy = bt::scalarKindString(argElementKind(arg));
        for (double e : els) {
            const std::string name = "o" + std::to_string(numOverrides++);
            overrideDecls.push_back("override " + name + " : " + elemTy + ";");
            constants[name] = e;
            argOverrides.push_back(name);
        }
        std::string joined;
        for (size_t i = 0; i < argOverrides.size(); ++i) {
            if (i) {
                joined += ", ";
            }
            joined += argOverrides[i];
        }
        callArgsList.push_back(argTypeString(arg) + "(" + joined + ")");
    }
    std::string decls;
    for (size_t i = 0; i < overrideDecls.size(); ++i) {
        if (i) {
            decls += "\n";
        }
        decls += overrideDecls[i];
    }
    std::string callArgs;
    for (size_t i = 0; i < callArgsList.size(); ++i) {
        if (i) {
            callArgs += ", ";
        }
        callArgs += callArgsList[i];
    }
    ShaderValidationTest::PipelineArgs pargs;
    pargs.expectedResult = expectedResult;
    pargs.code = enables + "\n" + decls + "\nvar<private> v " + optionalVarType + " = " + builtin +
                 "(" + callArgs + ");";
    pargs.constants = constants;
    pargs.reference = {"v"};
    t.expectPipelineResult(pargs);
}

// ---------------------------------------------------------------------------
// ConstantOrOverrideValueChecker (const_override_validation.ts)
// ---------------------------------------------------------------------------
class ConstantOrOverrideValueChecker {
  public:
    ConstantOrOverrideValueChecker(ShaderValidationTest& t, const Type& scalarType) : t_(t) {
        if (scalarType.kind == ScalarKind::F32) {
            hasLimits_ = true;
            limits_ = kValueF32();
            quantizeKind_ = ScalarKind::F32;
        } else if (scalarType.kind == ScalarKind::F16) {
            hasLimits_ = true;
            limits_ = kValueF16();
            quantizeKind_ = ScalarKind::F16;
        } else {
            hasLimits_ = false;
            quantizeKind_ = scalarType.kind;
        }
    }

    double quantize(double value) const {
        switch (quantizeKind_) {
            case ScalarKind::F32:
                return binary::quantizeToF32(value);
            case ScalarKind::F16:
                return binary::quantizeToF16(value);
            default:
                return value;
        }
    }

    bool isAmbiguousOverflow(double value) const {
        if (!std::isfinite(value)) {
            return false;
        }
        if (!hasLimits_ || (value <= limits_.posMax && value >= limits_.negMin)) {
            return false;
        }
        return std::abs(value) < std::pow(2.0, limits_.emax + 1);
    }

    bool isNearZero(double value) const {
        if (!std::isfinite(value)) {
            return false;
        }
        if (!hasLimits_) {
            return value == 0.0;
        }
        return value < limits_.posMin && value > limits_.negMax;
    }

    double checkedResult(double value) {
        if (isAmbiguousOverflow(value)) {
            t_.skip("Checked value was within the ambiguous overflow rounding range.");
        }
        const double q = quantize(value);
        if (!std::isfinite(q)) {
            allChecksPassed_ = false;
        }
        return q;
    }

    int64_t checkedResultBigInt(int64_t value) {
        // i64.isOOB is always false for int64 inputs (they fit); kept for parity.
        return value;
    }

    double skipIfCheckFails(double value) {
        if (isAmbiguousOverflow(value)) {
            t_.skip("Checked value was within the ambiguous overflow rounding range.");
        }
        const double q = quantize(value);
        if (!std::isfinite(q)) {
            t_.skip("Checked value was not finite after quantization.");
        }
        return value;
    }

    bool allChecksPassed() const { return allChecksPassed_; }

  private:
    ShaderValidationTest& t_;
    bool allChecksPassed_ = true;
    bool hasLimits_ = false;
    FloatLimits limits_{};
    ScalarKind quantizeKind_ = ScalarKind::F32;
};

// absBigInt helper (util/math.ts) for acos/asin/atanh-style |v| <= 1 checks.
inline int64_t absBigInt(int64_t v) { return v < 0 ? -v : v; }

// Build a BuiltinValue for a type-key that may name a scalar/vector OR a matrix,
// filled with integer `value` (matrices fill float). Used by tests whose type
// table mixes scalars/vectors/bools/matrices (e.g. atan2 invalid_argument).
inline bool isMatrixKey(const std::string& name) { return name.rfind("mat", 0) == 0; }
inline BuiltinValue createByKey(const std::string& name, int64_t value) {
    if (isMatrixKey(name)) {
        return createMatrixValue(matByName(name), static_cast<double>(value));
    }
    return createBuiltinValue(bt::typeByName(name), RangeValue::makeI(value));
}

// isConvertibleToFloatType(ty) (conversion.ts): scalar with float-convertible kind.
inline bool isConvertibleToFloatType(const Type& ty) {
    if (!ty.isScalar()) {
        return false;
    }
    const ScalarKind k = ty.kind;
    return k == ScalarKind::AbstractInt || k == ScalarKind::AbstractFloat ||
           k == ScalarKind::F32 || k == ScalarKind::F16;
}

// concreteTypeOf(ty, [f32]) (conversion.ts): both abstract-int and abstract-float
// concretize to f32 (since f32 is the only allowed scalar type); other kinds keep.
inline Type concreteTypeOfFloat(const Type& ty) {
    if (ty.kind == ScalarKind::AbstractInt || ty.kind == ScalarKind::AbstractFloat) {
        return Type{ScalarKind::F32, ty.width};
    }
    return ty;
}

// isRepresentable(value, floatKind) (util/floating_point.ts): finite AND within
// [negative.min, positive.max] of the float trait. floatKind is the element kind
// (abstract-float / f32 / f16). abstract-float uses the f64 representable range.
inline bool isRepresentable(double value, ScalarKind floatKind) {
    if (!std::isfinite(value)) {
        return false;
    }
    switch (floatKind) {
        case ScalarKind::F32:
            return value >= kValueF32().negMin && value <= kValueF32().posMax;
        case ScalarKind::F16:
            return value >= kValueF16().negMin && value <= kValueF16().posMax;
        case ScalarKind::AbstractFloat: {
            // f64 trait: ±max normal f64.
            const double f64Max = reinterpretU64AsF64(KBit::f64_pos_max);
            return value >= -f64Max && value <= f64Max;
        }
        default:
            return false;
    }
}

// ---------------------------------------------------------------------------
// Ordered type tables (conversion.ts), in addition to binary_types.h's
// kAllScalarsAndVectors / kConcreteNumericScalarsAndVectors. Keys == Type.toString().
// ---------------------------------------------------------------------------
inline Type S(ScalarKind k) { return bt::scalar(k); }
inline Type V(int w, ScalarKind k) { return bt::vec(w, k); }

// kFloatScalars = [af, f32, f16]
inline const std::vector<Type>& kFloatScalars() {
    static const std::vector<Type> v = {S(ScalarKind::AbstractFloat), S(ScalarKind::F32),
                                        S(ScalarKind::F16)};
    return v;
}
// kConcreteFloatScalars = [f32, f16]
inline const std::vector<Type>& kConcreteFloatScalars() {
    static const std::vector<Type> v = {S(ScalarKind::F32), S(ScalarKind::F16)};
    return v;
}
// kFloatVectors = vec2(af,f32,f16), vec3(...), vec4(...)
inline const std::vector<Type>& kFloatVectors() {
    static const std::vector<Type> v = {
        V(2, ScalarKind::AbstractFloat), V(2, ScalarKind::F32), V(2, ScalarKind::F16),
        V(3, ScalarKind::AbstractFloat), V(3, ScalarKind::F32), V(3, ScalarKind::F16),
        V(4, ScalarKind::AbstractFloat), V(4, ScalarKind::F32), V(4, ScalarKind::F16),
    };
    return v;
}
// kFloatScalarsAndVectors = kFloatScalars ++ kFloatVectors
inline const std::vector<Type>& kFloatScalarsAndVectors() {
    static std::vector<Type> v = [] {
        std::vector<Type> out = kFloatScalars();
        for (const Type& t : kFloatVectors()) {
            out.push_back(t);
        }
        return out;
    }();
    return v;
}
// kConcreteF32ScalarsAndVectors = f32, vec2f, vec3f, vec4f
inline const std::vector<Type>& kConcreteF32ScalarsAndVectors() {
    static const std::vector<Type> v = {S(ScalarKind::F32), V(2, ScalarKind::F32),
                                        V(3, ScalarKind::F32), V(4, ScalarKind::F32)};
    return v;
}
// kConcreteF16ScalarsAndVectors = f16, vec2h, vec3h, vec4h
inline const std::vector<Type>& kConcreteF16ScalarsAndVectors() {
    static const std::vector<Type> v = {S(ScalarKind::F16), V(2, ScalarKind::F16),
                                        V(3, ScalarKind::F16), V(4, ScalarKind::F16)};
    return v;
}
// kConcreteSignedIntegerScalarsAndVectors = i32, vec2i, vec3i, vec4i
inline const std::vector<Type>& kConcreteSignedIntegerScalarsAndVectors() {
    static const std::vector<Type> v = {S(ScalarKind::I32), V(2, ScalarKind::I32),
                                        V(3, ScalarKind::I32), V(4, ScalarKind::I32)};
    return v;
}
// kConcreteIntegerScalarsAndVectors = i32,vec2i,vec3i,vec4i, u32,vec2u,vec3u,vec4u
inline const std::vector<Type>& kConcreteIntegerScalarsAndVectors() {
    static const std::vector<Type> v = {
        S(ScalarKind::I32), V(2, ScalarKind::I32), V(3, ScalarKind::I32), V(4, ScalarKind::I32),
        S(ScalarKind::U32), V(2, ScalarKind::U32), V(3, ScalarKind::U32), V(4, ScalarKind::U32),
    };
    return v;
}
// kConvertableToFloatScalar = [abstractInt, af, f32, f16]
inline const std::vector<Type>& kConvertableToFloatScalar() {
    static const std::vector<Type> v = {S(ScalarKind::AbstractInt), S(ScalarKind::AbstractFloat),
                                        S(ScalarKind::F32), S(ScalarKind::F16)};
    return v;
}
// kConvertableToFloatVectors = vec2ai,vec3ai,vec4ai, then kFloatVectors
inline const std::vector<Type>& kConvertableToFloatVectors() {
    static std::vector<Type> v = [] {
        std::vector<Type> out = {V(2, ScalarKind::AbstractInt), V(3, ScalarKind::AbstractInt),
                                 V(4, ScalarKind::AbstractInt)};
        for (const Type& t : kFloatVectors()) {
            out.push_back(t);
        }
        return out;
    }();
    return v;
}
// kConvertableToFloatScalarsAndVectors = abstractInt, kFloatScalars, kConvertableToFloatVectors
inline const std::vector<Type>& kConvertableToFloatScalarsAndVectors() {
    static std::vector<Type> v = [] {
        std::vector<Type> out = {S(ScalarKind::AbstractInt)};
        for (const Type& t : kFloatScalars()) {
            out.push_back(t);
        }
        for (const Type& t : kConvertableToFloatVectors()) {
            out.push_back(t);
        }
        return out;
    }();
    return v;
}
// kAllNumericScalarsAndVectors = kConvertableToFloatScalarsAndVectors ++ kConcreteIntegerScalarsAndVectors
inline const std::vector<Type>& kAllNumericScalarsAndVectors() {
    static std::vector<Type> v = [] {
        std::vector<Type> out = kConvertableToFloatScalarsAndVectors();
        for (const Type& t : kConcreteIntegerScalarsAndVectors()) {
            out.push_back(t);
        }
        return out;
    }();
    return v;
}

// kConvertableToFloatVec2/3/4 = [vecNai, vecNaf, vecNf, vecNh]
inline const std::vector<Type>& kConvertableToFloatVec2() {
    static const std::vector<Type> v = {V(2, ScalarKind::AbstractInt), V(2, ScalarKind::AbstractFloat),
                                        V(2, ScalarKind::F32), V(2, ScalarKind::F16)};
    return v;
}
inline const std::vector<Type>& kConvertableToFloatVec3() {
    static const std::vector<Type> v = {V(3, ScalarKind::AbstractInt), V(3, ScalarKind::AbstractFloat),
                                        V(3, ScalarKind::F32), V(3, ScalarKind::F16)};
    return v;
}
inline const std::vector<Type>& kConvertableToFloatVec4() {
    static const std::vector<Type> v = {V(4, ScalarKind::AbstractInt), V(4, ScalarKind::AbstractFloat),
                                        V(4, ScalarKind::F32), V(4, ScalarKind::F16)};
    return v;
}

// isSubnormalNumberF32/F16 (util/math.ts): n in (negative.max, positive.min).
inline bool isSubnormalNumberF32(double n) {
    return n > kValueF32().negMax && n < kValueF32().posMin;
}
inline bool isSubnormalNumberF16(double n) {
    return n > kValueF16().negMax && n < kValueF16().posMin;
}
inline bool isSubnormalForKind(ScalarKind k, double n) {
    if (k == ScalarKind::F32) {
        return isSubnormalNumberF32(n);
    }
    if (k == ScalarKind::F16) {
        return isSubnormalNumberF16(n);
    }
    return false;
}

// quantizeForKind: f32->fround, f16->hfround, else identity (mirrors the
// per-spec quantizeFunctionForScalarType closures).
inline double quantizeForKind(ScalarKind k, double v) { return quantizeToKind(k, v); }

// i64 OOB check (util/constants.ts kValue.i64.isOOB) — value out of int64 range.
// Detects whether the EXACT product a*b (or product*vecSize) overflows int64,
// using a 128-bit magnitude comparison (mirrors BigInt arithmetic upstream).
inline bool i64MulOOB(int64_t a, int64_t b) {
    if (a == 0 || b == 0) {
        return false;
    }
    auto absu = [](int64_t v) -> uint64_t {
        return v < 0 ? (~static_cast<uint64_t>(v) + 1ull) : static_cast<uint64_t>(v);
    };
    const uint64_t ua = absu(a), ub = absu(b);
    const bool negative = (a < 0) ^ (b < 0);
    // 128-bit product ua*ub.
    const uint64_t aLo = ua & 0xffffffffull, aHi = ua >> 32;
    const uint64_t bLo = ub & 0xffffffffull, bHi = ub >> 32;
    const uint64_t ll = aLo * bLo;
    const uint64_t lh = aLo * bHi;
    const uint64_t hl = aHi * bLo;
    const uint64_t hh = aHi * bHi;
    const uint64_t mid = (ll >> 32) + (lh & 0xffffffffull) + (hl & 0xffffffffull);
    const uint64_t lo = (ll & 0xffffffffull) | (mid << 32);
    const uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
    // i64 range: |product| <= 2^63 - 1 for positive, <= 2^63 for negative.
    if (hi != 0) {
        return true;  // magnitude >= 2^64 > i64 range.
    }
    const uint64_t limit = negative ? 0x8000000000000000ull : 0x7fffffffffffffffull;
    return lo > limit;
}

// Build a Value list of Type.toString() keys (for combine('type', keysOf(...))).
inline std::vector<cts::Value> typeKeys(const std::vector<Type>& table) {
    std::vector<cts::Value> out;
    out.reserve(table.size());
    for (const Type& t : table) {
        out.emplace_back(t.toString());
    }
    return out;
}

}  // namespace cts::shader_validation::builtin
