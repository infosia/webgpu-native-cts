// Ported from gpuweb/cts src/webgpu/util/floating_point.ts + util/math.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/floating_point.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace cts {
namespace expression {
namespace fp {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

// --- f32 constants (from util/constants.ts kValue.f32 / kBit.f32) ---
float f32FromBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
const double kF32PosMax = static_cast<double>(f32FromBits(0x7f7fffffu));   // largest finite f32
const double kF32NegMin = static_cast<double>(f32FromBits(0xff7fffffu));   // most-negative finite f32
const double kF32PosMin = static_cast<double>(f32FromBits(0x00800000u));   // smallest positive normal
const double kF32NegMax = static_cast<double>(f32FromBits(0x80800000u));   // largest negative normal
const double kF32PosSubMin = static_cast<double>(f32FromBits(0x00000001u)); // smallest positive subnormal
const double kF32PosSubMax = static_cast<double>(f32FromBits(0x007fffffu)); // largest positive subnormal
const double kF32NegSubMin = static_cast<double>(f32FromBits(0x807fffffu)); // most-negative subnormal
const double kF32PosPiWhole = static_cast<double>(f32FromBits(0x40490fdbu));
const double kF32NegPiWhole = static_cast<double>(f32FromBits(0xc0490fdbu));
const double kF32MaxULP = static_cast<double>(f32FromBits(0x73800000u));
constexpr int kF32Emax = 127;

// --- f64 constants (kValue.f64) ---
double f64FromBits(uint64_t bits) {
    double d;
    std::memcpy(&d, &bits, 8);
    return d;
}
const double kF64PosMin = f64FromBits(0x0010000000000000ull);    // smallest positive normal
const double kF64NegMax = f64FromBits(0x8010000000000000ull);    // largest negative normal
const double kF64PosSubMax = f64FromBits(0x000fffffffffffffull); // largest positive subnormal
const double kF64NegSubMin = f64FromBits(0x800fffffffffffffull); // most-negative subnormal

// --- f32 helpers (math.ts) ---
bool isSubnormalF32(double n) {
    return n > kF32NegMax && n < kF32PosMin && n != 0.0;
}
bool isFiniteF32(double n) {
    return n >= kF32NegMin && n <= kF32PosMax;
}
double flushSubnormalF32(double n) {
    return isSubnormalF32(n) ? 0.0 : n;
}
double quantizeToF32(double n) {
    // Math.fround: round to nearest f32, preserving inf/NaN.
    return static_cast<double>(static_cast<float>(n));
}

bool isSubnormalF64(double n) {
    return n > kF64NegMax && n < kF64PosMin && n != 0.0;
}
double flushSubnormalF64(double n) {
    return isSubnormalF64(n) ? 0.0 : n;
}

// nextAfterF32(val, dir, flush): next representable f32 toward +inf (positive) or -inf (negative).
// Mirrors util/math.ts nextAfterF32 with mode == 'flush'.
double nextAfterF32Flush(double val, bool positive) {
    if (std::isnan(val)) {
        return val;
    }
    if (val == kInf) {
        return kInf;
    }
    if (val == -kInf) {
        return -kInf;
    }
    val = flushSubnormalF32(val);

    if (val == 0.0) {
        // +/-0 -> closest normal in the direction (flush mode).
        return positive ? kF32PosMin : kF32NegMax;
    }

    // Quantize to f32 then step the integer bit pattern in the correct direction.
    float q = static_cast<float>(val);
    uint32_t bits;
    std::memcpy(&bits, &q, 4);
    const double qd = static_cast<double>(q);
    if ((positive && qd <= val) || (!positive && qd >= val)) {
        const bool isPositive = (bits & 0x80000000u) == 0;
        if (isPositive == positive) {
            bits += 1u;
        } else {
            bits -= 1u;
        }
    }
    // Overflow to infinity.
    if ((bits & 0x7f800000u) == 0x7f800000u) {
        return positive ? kInf : -kInf;
    }
    std::memcpy(&q, &bits, 4);
    return flushSubnormalF32(static_cast<double>(q));
}

// oneULPF32 (flush mode). Mirrors util/math.ts oneULPF32.
double oneULPF32(double target) {
    if (std::isnan(target)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    target = flushSubnormalF32(target);
    if (target == kInf || target >= kF32PosMax || target == -kInf || target <= kF32NegMin) {
        return kF32MaxULP;
    }
    const double before = nextAfterF32Flush(target, false);
    const double after = nextAfterF32Flush(target, true);
    const double converted = quantizeToF32(target);
    if (converted == target) {
        return std::min(target - before, after - target);
    }
    return after - before;
}

// correctlyRoundedF32(n): the 1 or 2 valid f32 roundings of n. Mirrors util/math.ts.
std::vector<double> correctlyRoundedF32(double n) {
    if (std::isnan(n)) {
        return {n};
    }
    if (n >= std::ldexp(1.0, kF32Emax + 1)) {
        return {kInf};
    }
    if (n > kF32PosMax) {
        return {kF32PosMax, kInf};
    }
    if (n <= kF32PosMax && n >= kF32NegMin) {
        const double n32 = quantizeToF32(n);
        if (n == n32) {
            return {n};
        }
        if (n32 > n) {
            // n32 rounded towards +inf, so its no-flush predecessor is the other rounding.
            // no-flush nextAfter: we replicate by stepping the bit pattern without flushing.
            float q = static_cast<float>(n32);
            uint32_t bits;
            std::memcpy(&bits, &q, 4);
            // step toward -inf (no flush)
            const bool isPositive = (bits & 0x80000000u) == 0;
            if (isPositive) {
                bits -= 1u;
            } else {
                bits += 1u;
            }
            std::memcpy(&q, &bits, 4);
            return {static_cast<double>(q), n32};
        } else {
            float q = static_cast<float>(n32);
            uint32_t bits;
            std::memcpy(&bits, &q, 4);
            const bool isPositive = (bits & 0x80000000u) == 0;
            if (isPositive) {
                bits += 1u;
            } else {
                bits -= 1u;
            }
            std::memcpy(&q, &bits, 4);
            return {n32, static_cast<double>(q)};
        }
    }
    if (n > -std::ldexp(1.0, kF32Emax + 1)) {
        return {-kInf, kF32NegMin};
    }
    return {-kInf};
}

// correctlyRoundedF64(n): for abstract. Only inf maps to a 2-element range; finite is exact.
std::vector<double> correctlyRoundedF64(double n) {
    if (n == kInf) {
        return {std::numeric_limits<double>::max(), kInf};
    }
    if (n == -kInf) {
        return {-kInf, std::numeric_limits<double>::lowest()};
    }
    return {n};
}

// addFlushedIfNeeded: append 0 if values contain a (non-zero) subnormal. Mirrors upstream.
std::vector<double> addFlushedIfNeeded(FPKind kind, const std::vector<double>& values) {
    bool anySub = false;
    for (double v : values) {
        if (kind == FPKind::F32 ? isSubnormalF32(v) : isSubnormalF64(v)) {
            anySub = true;
            break;
        }
    }
    if (!anySub) {
        return values;
    }
    std::vector<double> out = values;
    out.push_back(0.0);
    return out;
}

std::vector<double> correctlyRounded(FPKind kind, double n) {
    return kind == FPKind::F32 ? correctlyRoundedF32(n) : correctlyRoundedF64(n);
}
double flushSubnormal(FPKind kind, double n) {
    return kind == FPKind::F32 ? flushSubnormalF32(n) : flushSubnormalF64(n);
}

// A ScalarToIntervalOp: impl plus optional domain (rejects -> unbounded) for the kind.
struct ScalarOp {
    std::function<FPInterval(double)> impl;
    bool hasDomain = false;
    std::function<bool(double)> inDomain; // true if the (rounded/flushed) input is in domain
};

// roundAndFlushScalarToInterval + runScalarToIntervalOp collapsed for a point input n.
FPInterval runScalarOpPoint(FPKind kind, double n, const ScalarOp& op) {
    if (std::isnan(n)) {
        return unboundedInterval(kind);
    }
    const std::vector<double> values = correctlyRounded(kind, n);
    const std::vector<double> inputs = addFlushedIfNeeded(kind, values);
    if (op.hasDomain) {
        for (double i : inputs) {
            if (!op.inDomain(i)) {
                return unboundedInterval(kind);
            }
        }
    }
    std::vector<FPInterval> results;
    results.reserve(inputs.size());
    for (double i : inputs) {
        results.push_back(op.impl(i));
    }
    return spanIntervals(results);
}

FPInterval runScalarToIntervalOp(FPKind kind, const FPInterval& x, const ScalarOp& op) {
    if (!x.isFinite()) {
        return unboundedInterval(kind);
    }
    // span over the endpoints (no extrema for the Stage-1 ops).
    std::vector<FPInterval> spans;
    spans.push_back(runScalarOpPoint(kind, x.begin, op));
    if (!x.isPoint()) {
        spans.push_back(runScalarOpPoint(kind, x.end, op));
    }
    const FPInterval result = spanIntervals(spans);
    return result.isFinite() ? result : unboundedInterval(kind);
}

// runScalarPairToIntervalOp for point inputs (no extrema/domain for addition).
FPInterval runScalarPairPoint(
    FPKind kind, double x, double y, const std::function<FPInterval(double, double)>& impl) {
    if (std::isnan(x) || std::isnan(y)) {
        return unboundedInterval(kind);
    }
    const std::vector<double> xs = addFlushedIfNeeded(kind, correctlyRounded(kind, x));
    const std::vector<double> ys = addFlushedIfNeeded(kind, correctlyRounded(kind, y));
    std::vector<FPInterval> intervals;
    for (double ix : xs) {
        for (double iy : ys) {
            intervals.push_back(impl(ix, iy));
        }
    }
    return spanIntervals(intervals);
}

// correctlyRoundedIntervalWithUnboundedPrecisionForAddition (addition only).
FPInterval crUnboundedAddition(FPKind kind, double val, double large, double small) {
    if (val == large && small != 0.0) {
        // The interval spans [large, nextAfter(large, dir)] then correctly-rounded.
        if (std::signbit(small) == false) {
            const double na = nextAfterF32Flush(large, true);
            FPInterval span = toInterval(kind, std::min(large, na), std::max(large, na));
            // correctlyRoundedInterval over an interval: span endpoints' correctly-rounded.
            ScalarOp op;
            op.impl = [kind](double n) { return correctlyRoundedInterval(kind, n); };
            return runScalarToIntervalOp(kind, span, op);
        } else {
            const double na = nextAfterF32Flush(large, false);
            FPInterval span = toInterval(kind, std::min(na, large), std::max(na, large));
            ScalarOp op;
            op.impl = [kind](double n) { return correctlyRoundedInterval(kind, n); };
            return runScalarToIntervalOp(kind, span, op);
        }
    }
    return correctlyRoundedInterval(kind, val);
}

} // namespace

// --- FPInterval members ---
bool FPInterval::isFinite() const {
    if (kind == FPKind::F32) {
        return isFiniteF32(begin) && isFiniteF32(end);
    }
    return std::isfinite(begin) && std::isfinite(end);
}
bool FPInterval::contains(double n) const {
    if (std::isnan(n)) {
        return begin == -kInf && end == kInf;
    }
    return begin <= n && end >= n;
}

FPInterval unboundedInterval(FPKind kind) {
    return FPInterval(kind, -kInf, kInf);
}

FPInterval toInterval(FPKind kind, double begin, double end) {
    return FPInterval(kind, begin, end);
}
FPInterval toIntervalPoint(FPKind kind, double n) {
    return FPInterval(kind, n, n);
}

FPInterval spanIntervals(const std::vector<FPInterval>& intervals) {
    double begin = kInf;
    double end = -kInf;
    FPKind kind = intervals.empty() ? FPKind::F32 : intervals.front().kind;
    for (const FPInterval& i : intervals) {
        begin = std::min(i.begin, begin);
        end = std::max(i.end, end);
    }
    return FPInterval(kind, begin, end);
}

FPInterval correctlyRoundedInterval(FPKind kind, double n) {
    ScalarOp op;
    op.impl = [kind](double m) { return toIntervalPoint(kind, m); };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}

FPInterval absoluteErrorInterval(FPKind kind, double n, double errorRange) {
    errorRange = std::abs(errorRange);
    ScalarOp op;
    op.impl = [kind, errorRange](double m) -> FPInterval {
        const bool finite = kind == FPKind::F32 ? isFiniteF32(m) : std::isfinite(m);
        if (!finite) {
            return unboundedInterval(kind);
        }
        return toInterval(kind, m - errorRange, m + errorRange);
    };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}

FPInterval ulpInterval(FPKind kind, double n, double numULP) {
    numULP = std::abs(numULP);
    ScalarOp op;
    op.impl = [kind, numULP](double m) -> FPInterval {
        // f32 ULP only (abstract never calls ulpInterval directly; sqrt/cos abstract use f32).
        const double ulp = oneULPF32(m);
        const double begin = m - numULP * ulp;
        const double end = m + numULP * ulp;
        return toInterval(kind, std::min(begin, flushSubnormal(kind, begin)),
                          std::max(end, flushSubnormal(kind, end)));
    };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}

FPInterval absInterval(FPKind kind, double n) {
    ScalarOp op;
    op.impl = [kind](double m) { return correctlyRoundedInterval(kind, std::abs(m)); };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}
FPInterval floorInterval(FPKind kind, double n) {
    ScalarOp op;
    op.impl = [kind](double m) { return correctlyRoundedInterval(kind, std::floor(m)); };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}
FPInterval ceilInterval(FPKind kind, double n) {
    ScalarOp op;
    op.impl = [kind](double m) { return correctlyRoundedInterval(kind, std::ceil(m)); };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}
FPInterval truncInterval(FPKind kind, double n) {
    ScalarOp op;
    op.impl = [kind](double m) { return correctlyRoundedInterval(kind, std::trunc(m)); };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}

FPInterval inverseSqrtInterval(FPKind kind, double n) {
    ScalarOp op;
    op.impl = [kind](double m) { return ulpInterval(kind, 1.0 / std::sqrt(m), 2.0); };
    op.hasDomain = true;
    // greaterThanZeroInterval: [smallest positive subnormal, max].
    const double lo = kind == FPKind::F32 ? kF32PosSubMin : f64FromBits(0x0000000000000001ull);
    const double hi = kind == FPKind::F32 ? kF32PosMax : std::numeric_limits<double>::max();
    op.inDomain = [lo, hi](double m) { return m >= lo && m <= hi; };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}

FPInterval divisionInterval(FPKind kind, double x, double y) {
    // domain: x in [neg.min, pos.max]; y in [-2^126,-2^-126] U [2^-126, 2^126] (f32/abstract).
    const double xLo = kind == FPKind::F32 ? kF32NegMin : std::numeric_limits<double>::lowest();
    const double xHi = kind == FPKind::F32 ? kF32PosMax : std::numeric_limits<double>::max();
    const double yMag1 = std::ldexp(1.0, -126); // 2^-126
    const double yMag2 = std::ldexp(1.0, 126);  // 2^126
    auto inXDomain = [xLo, xHi](double v) { return v >= xLo && v <= xHi; };
    auto inYDomain = [yMag1, yMag2](double v) {
        return (v >= -yMag2 && v <= -yMag1) || (v >= yMag1 && v <= yMag2);
    };
    if (std::isnan(x) || std::isnan(y)) {
        return unboundedInterval(kind);
    }
    const std::vector<double> xs = addFlushedIfNeeded(kind, correctlyRounded(kind, x));
    std::vector<double> ys = addFlushedIfNeeded(kind, correctlyRounded(kind, y));
    // extrema: division has a discontinuity at y = 0 (only relevant when y is an interval; for a
    // point input it is handled by the impl's y==0 -> unbounded).
    for (double v : xs) {
        if (!inXDomain(v)) {
            return unboundedInterval(kind);
        }
    }
    for (double v : ys) {
        if (!inYDomain(v)) {
            return unboundedInterval(kind);
        }
    }
    std::vector<FPInterval> intervals;
    for (double ix : xs) {
        for (double iy : ys) {
            if (iy == 0.0) {
                intervals.push_back(unboundedInterval(kind));
            } else {
                intervals.push_back(ulpInterval(kind, ix / iy, 2.5));
            }
        }
    }
    const FPInterval result = spanIntervals(intervals);
    return result.isFinite() ? result : unboundedInterval(kind);
}

FPInterval sqrtInterval(FPKind kind, double n) {
    // sqrt(n) = 1 / inverseSqrt(n). Composed over the inverseSqrt interval endpoints.
    ScalarOp op;
    op.impl = [kind](double m) -> FPInterval {
        const FPInterval inv = inverseSqrtInterval(kind, m);
        if (!inv.isFinite()) {
            return unboundedInterval(kind);
        }
        // divisionInterval(1.0, inv) over inv's endpoints.
        std::vector<FPInterval> parts;
        parts.push_back(divisionInterval(kind, 1.0, inv.begin));
        if (!inv.isPoint()) {
            parts.push_back(divisionInterval(kind, 1.0, inv.end));
        }
        const FPInterval r = spanIntervals(parts);
        return r.isFinite() ? r : unboundedInterval(kind);
    };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}

FPInterval cosInterval(FPKind kind, double n) {
    ScalarOp op;
    // cos: f32 absolute error 2^-11 (abstract uses the f32 op per the cache).
    const double absError = std::ldexp(1.0, -11);
    op.impl = [kind, absError](double m) {
        return absoluteErrorInterval(kind, std::cos(m), absError);
    };
    op.hasDomain = true;
    const double piLo = kind == FPKind::F32 ? kF32NegPiWhole : kF32NegPiWhole;
    const double piHi = kind == FPKind::F32 ? kF32PosPiWhole : kF32PosPiWhole;
    op.inDomain = [piLo, piHi](double m) { return m >= piLo && m <= piHi; };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, n), op);
}

FPInterval additionInterval(FPKind kind, double x, double y) {
    auto impl = [kind](double a, double b) -> FPInterval {
        const double sum = a + b;
        const double large = std::abs(a) > std::abs(b) ? a : b;
        const double small = std::abs(a) > std::abs(b) ? b : a;
        return crUnboundedAddition(kind, sum, large, small);
    };
    if (std::isnan(x) || std::isnan(y)) {
        return unboundedInterval(kind);
    }
    const FPInterval r = runScalarPairPoint(kind, x, y, impl);
    return r.isFinite() ? r : unboundedInterval(kind);
}

double quantize(FPKind kind, double n) {
    return kind == FPKind::F32 ? quantizeToF32(n) : n;
}

// --- Range generators (math.ts) ---
namespace {
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

std::vector<double> linearRangeU32(uint32_t aBits, uint32_t bBits, int numSteps) {
    std::vector<double> out;
    if (numSteps <= 0) {
        return out;
    }
    const double a = static_cast<double>(aBits);
    const double b = static_cast<double>(bBits);
    if (numSteps == 1) {
        out.push_back(a);
        return out;
    }
    for (int i = 0; i < numSteps; ++i) {
        out.push_back(std::trunc(lerp(a, b, static_cast<double>(i) / (numSteps - 1))));
    }
    return out;
}
} // namespace

std::vector<double> linearRange(double a, double b, int numSteps) {
    std::vector<double> out;
    if (numSteps <= 0) {
        return out;
    }
    if (numSteps == 1) {
        out.push_back(a);
        return out;
    }
    for (int i = 0; i < numSteps; ++i) {
        out.push_back(lerp(a, b, static_cast<double>(i) / (numSteps - 1)));
    }
    return out;
}

std::vector<double> scalarF32Range() {
    // counts: neg_norm = pos_norm = 50, neg_sub = pos_sub = 10.
    const int negNorm = 50, negSub = 10, posSub = 10, posNorm = 50;
    // special_pos (pos_norm >= 4): largest float as signed/unsigned integer bit patterns.
    const std::vector<uint32_t> specialPos = {0x4effffffu, 0x4f7fffffu};
    std::vector<double> bitFields;
    // negative normals [neg.min=0xff7fffff, neg.max=0x80800000]
    for (double v : linearRangeU32(0xff7fffffu, 0x80800000u, negNorm)) {
        bitFields.push_back(v);
    }
    // negative subnormals [neg.subnormal.min=0x807fffff, neg.subnormal.max=0x80000001]
    for (double v : linearRangeU32(0x807fffffu, 0x80000001u, negSub)) {
        bitFields.push_back(v);
    }
    bitFields.push_back(static_cast<double>(0x80000000u)); // -0.0
    bitFields.push_back(0.0);                              // +0.0
    // positive subnormals [pos.subnormal.min=0x00000001, pos.subnormal.max=0x007fffff]
    for (double v : linearRangeU32(0x00000001u, 0x007fffffu, posSub)) {
        bitFields.push_back(v);
    }
    // positive normals [pos.min=0x00800000, pos.max=0x7f7fffff], (posNorm - special.size) steps,
    // then append the special values, then sort ascending.
    std::vector<double> posBits;
    for (double v : linearRangeU32(0x00800000u, 0x7f7fffffu, posNorm - static_cast<int>(specialPos.size()))) {
        posBits.push_back(v);
    }
    for (uint32_t s : specialPos) {
        posBits.push_back(static_cast<double>(s));
    }
    std::sort(posBits.begin(), posBits.end());
    for (double v : posBits) {
        bitFields.push_back(v);
    }
    // Map bit fields (truncated to integers) to f32 values.
    std::vector<double> out;
    out.reserve(bitFields.size());
    for (double bf : bitFields) {
        const uint32_t bits = static_cast<uint32_t>(std::trunc(bf));
        out.push_back(static_cast<double>(f32FromBits(bits)));
    }
    return out;
}

const std::vector<double>& sparseScalarF32Range() {
    // kInterestingF32Values: [neg.min, -10, -1, -0.125, neg.max, neg.subnormal.min,
    //   neg.subnormal.max, -0, 0, pos.subnormal.min, pos.subnormal.max, pos.min, 0.125, 1, 10,
    //   pos.max]. neg.subnormal.max (0x80000001) is the negative subnormal closest to zero
    //   (= -pos.subnormal.min).
    static const std::vector<double> v = {
        kF32NegMin,    -10.0,          -1.0,          -0.125,
        kF32NegMax,    kF32NegSubMin,  -kF32PosSubMin,
        -0.0,          0.0,
        kF32PosSubMin, kF32PosSubMax,  kF32PosMin,    0.125,
        1.0,           10.0,           kF32PosMax,
    };
    return v;
}

std::vector<std::vector<double>> sparseVectorF32Range(int dim) {
    const std::vector<double>& f = sparseScalarF32Range();
    std::vector<std::vector<double>> out;
    out.reserve(f.size());
    for (size_t idx = 0; idx < f.size(); ++idx) {
        const double fv = f[idx];
        const double i = static_cast<double>(idx);
        if (dim == 2) {
            out.push_back({(idx % 2 == 0) ? fv : i, (idx % 2 == 1) ? fv : -i});
        } else if (dim == 3) {
            out.push_back({(idx % 3 == 0) ? fv : i, (idx % 3 == 1) ? fv : -i,
                           (idx % 3 == 2) ? fv : i});
        } else {
            out.push_back({(idx % 4 == 0) ? fv : i, (idx % 4 == 1) ? fv : -i,
                           (idx % 4 == 2) ? fv : i, (idx % 4 == 3) ? fv : -i});
        }
    }
    return out;
}

// Computes (a * idx) / div for signed 64-bit a, with small positive idx/div, exactly, via a
// portable 128-bit intermediate (no compiler 128-bit type — MSVC /W4 /WX clean). Truncates toward
// zero, matching BigInt division semantics.
int64_t mulDivS64(int64_t a, int64_t idx, int64_t div) {
    const bool neg = (a < 0);
    uint64_t ua = neg ? static_cast<uint64_t>(-(a + 1)) + 1u : static_cast<uint64_t>(a);
    // 128-bit product ua * idx (idx fits in 64 bits, small).
    const uint64_t uidx = static_cast<uint64_t>(idx);
    const uint64_t aLo = ua & 0xffffffffull;
    const uint64_t aHi = ua >> 32;
    const uint64_t p0 = aLo * uidx;
    const uint64_t p1 = aHi * uidx;
    uint64_t lo = p0 + (p1 << 32);
    uint64_t hi = (p1 >> 32) + (((p0 >> 32) + (p1 & 0xffffffffull)) >> 32);
    // Divide the 128-bit (hi:lo) by div (small positive) -> 64-bit quotient (fits, given inputs).
    const uint64_t udiv = static_cast<uint64_t>(div);
    uint64_t rem = 0;
    uint64_t qHi = 0;
    uint64_t qLo = 0;
    for (int bit = 127; bit >= 0; --bit) {
        rem = (rem << 1) | ((bit >= 64 ? (hi >> (bit - 64)) : (lo >> bit)) & 1u);
        if (rem >= udiv) {
            rem -= udiv;
            if (bit >= 64) {
                qHi |= (1ull << (bit - 64));
            } else {
                qLo |= (1ull << bit);
            }
        }
    }
    (void)qHi; // quotient fits in 64 bits for the ranges used here.
    const uint64_t q = qLo;
    return neg ? -static_cast<int64_t>(q) : static_cast<int64_t>(q);
}

std::vector<double> scalarF64Range() {
    // counts: neg_norm = pos_norm = 50, neg_sub = pos_sub = 10. Spread over the f64 bit space via
    // integer (bigint) lerp. Mirrors util/math.ts linearRangeBigInt / lerpBigInt exactly.
    const int negNorm = 50, negSub = 10, posSub = 10, posNorm = 50;
    auto lerpBig = [](uint64_t aBits, uint64_t bBits, int idx, int steps) -> uint64_t {
        if (steps == 1 || idx == 0) {
            return aBits;
        }
        if (idx == steps - 1) {
            return bBits;
        }
        const int64_t A = static_cast<int64_t>(aBits);
        const int64_t B = static_cast<int64_t>(bBits);
        const int64_t denom = steps - 1;
        int64_t r;
        if ((A <= 0 && B >= 0) || (A >= 0 && B <= 0)) {
            r = mulDivS64(B, idx, denom) + (A - mulDivS64(A, idx, denom));
        } else {
            const int64_t x = A + mulDivS64(B, idx, denom) - mulDivS64(A, idx, denom);
            r = !(B > A) ? std::max(B, x) : std::min(B, x);
        }
        return static_cast<uint64_t>(r);
    };
    auto linRangeBig = [&](uint64_t aBits, uint64_t bBits, int steps) {
        std::vector<uint64_t> out;
        for (int i = 0; i < steps; ++i) {
            out.push_back(lerpBig(aBits, bBits, i, steps));
        }
        return out;
    };
    std::vector<uint64_t> bits;
    for (uint64_t b : linRangeBig(0xffefffffffffffffull, 0x8010000000000000ull, negNorm)) {
        bits.push_back(b);
    }
    for (uint64_t b : linRangeBig(0x800fffffffffffffull, 0x8000000000000001ull, negSub)) {
        bits.push_back(b);
    }
    bits.push_back(0x8000000000000000ull); // -0.0
    bits.push_back(0x0ull);                 // +0.0
    for (uint64_t b : linRangeBig(0x0000000000000001ull, 0x000fffffffffffffull, posSub)) {
        bits.push_back(b);
    }
    for (uint64_t b : linRangeBig(0x0010000000000000ull, 0x7fefffffffffffffull, posNorm)) {
        bits.push_back(b);
    }
    std::vector<double> out;
    out.reserve(bits.size());
    for (uint64_t b : bits) {
        out.push_back(f64FromBits(b));
    }
    return out;
}

namespace {

// lerpBigInt(a, b, idx, steps) for int64 a/b (math.ts). Uses mulDivS64 for the exact products.
int64_t lerpBigIntS64(int64_t a, int64_t b, int64_t idx, int64_t steps) {
    if (steps == 1 || idx == 0) {
        return a;
    }
    if (idx == steps - 1) {
        return b;
    }
    const int64_t denom = steps - 1;
    if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0)) {
        return mulDivS64(b, idx, denom) + (a - mulDivS64(a, idx, denom));
    }
    const int64_t x = a + mulDivS64(b, idx, denom) - mulDivS64(a, idx, denom);
    return !(b > a) ? std::max(b, x) : std::min(b, x);
}

// biasedRangeBigInt(a, b, numSteps) for int64 (math.ts).
std::vector<int64_t> biasedRangeBigIntS64(int64_t a, int64_t b, int numSteps) {
    std::vector<int64_t> out;
    if (numSteps <= 0) {
        return out;
    }
    if (numSteps == 1) {
        out.push_back(a);
        return out;
    }
    const int c = 2;
    const int scaling = 1000;
    const int64_t scaledNumSteps = static_cast<int64_t>(numSteps) * scaling;
    for (int i = 0; i < numSteps; ++i) {
        const double biasedI = std::pow(static_cast<double>(i) / (numSteps - 1), c);
        const int64_t scaledI =
            static_cast<int64_t>(std::trunc(static_cast<double>(scaledNumSteps - 1) * biasedI));
        out.push_back(lerpBigIntS64(a, b, scaledI, scaledNumSteps));
    }
    return out;
}

} // namespace

std::vector<int64_t> fullI64Range() {
    const int64_t i64Min = INT64_MIN;
    const int64_t i64Max = INT64_MAX;
    std::vector<int64_t> out;
    for (int64_t v : biasedRangeBigIntS64(i64Min, -1, 50)) {
        out.push_back(v);
    }
    out.push_back(0);
    for (int64_t v : biasedRangeBigIntS64(1, i64Max, 50)) {
        out.push_back(v);
    }
    return out;
}

namespace {

// Encode one scalar interval onto an ExpectedElement of the result kind.
ExpectedElement intervalToExpected(FPKind kind, const FPInterval& iv) {
    const int width = kind == FPKind::F32 ? 32 : 64;
    const bool unbounded = (iv.begin == -kInf && iv.end == kInf);
    if (unbounded) {
        return acceptUnbounded(width);
    }
    return acceptInterval(width, iv.begin, iv.end);
}

// A scalar input value of the result kind: f32 emitted via its bit pattern; abstract-float via
// an AbstractFloat literal carrying the (quantized) f64 value's f32 bit pattern is NOT enough for
// abstract (needs full f64). For abstract inputs we emit via abstractFloatBits using the f64's
// nearest f32 ... but abstract is f64. The expression harness emits AbstractFloat from an f32 bit
// pattern only; to carry an exact f64 literal we extend via abstractFloatValue() below.
Scalar scalarInput(FPKind kind, double v) {
    if (kind == FPKind::F32) {
        const float f = static_cast<float>(v);
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        return f32Bits(bits);
    }
    // Abstract-float input: carry the exact f64 value (emitted as a decimal AbstractFloat literal).
    return abstractFloatValue(v);
}

} // namespace

std::vector<Case> generateScalarToIntervalCases(
    FPKind kind,
    const std::vector<double>& params,
    bool finiteFilter,
    const ScalarToInterval& op) {
    std::vector<Case> cases;
    for (double e : params) {
        const double q = quantize(kind, e);
        const FPInterval iv = op(q);
        if (finiteFilter && !iv.isFinite()) {
            continue;
        }
        Case c;
        c.inputs.push_back(CaseValue(scalarInput(kind, q)));
        // Placeholder expected (unused by the interval comparator) — width 1.
        c.expected = CaseValue(scalarInput(kind, q));
        c.expectedAccept.push_back(intervalToExpected(kind, iv));
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateScalarPairToIntervalCases(
    FPKind kind,
    const std::vector<double>& param0s,
    const std::vector<double>& param1s,
    bool finiteFilter,
    const ScalarPairToInterval& op) {
    std::vector<Case> cases;
    for (double a : param0s) {
        for (double b : param1s) {
            const double qa = quantize(kind, a);
            const double qb = quantize(kind, b);
            const FPInterval iv = op(qa, qb);
            if (finiteFilter && !iv.isFinite()) {
                continue;
            }
            Case c;
            c.inputs.push_back(CaseValue(scalarInput(kind, qa)));
            c.inputs.push_back(CaseValue(scalarInput(kind, qb)));
            c.expected = CaseValue(scalarInput(kind, qa));
            c.expectedAccept.push_back(intervalToExpected(kind, iv));
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

std::vector<Case> generateVectorScalarToVectorCases(
    FPKind kind,
    const std::vector<std::vector<double>>& vectors,
    const std::vector<double>& scalars,
    bool finiteFilter,
    const ScalarPairToInterval& op) {
    std::vector<Case> cases;
    for (const std::vector<double>& vec : vectors) {
        for (double s : scalars) {
            std::vector<double> qv;
            qv.reserve(vec.size());
            for (double e : vec) {
                qv.push_back(quantize(kind, e));
            }
            const double qs = quantize(kind, s);
            std::vector<FPInterval> intervals;
            bool nonFinite = false;
            for (double e : qv) {
                FPInterval iv = op(e, qs);
                if (!iv.isFinite()) {
                    nonFinite = true;
                }
                intervals.push_back(iv);
            }
            if (finiteFilter && nonFinite) {
                continue;
            }
            Case c;
            std::vector<Scalar> vEls;
            for (double e : qv) {
                vEls.push_back(scalarInput(kind, e));
            }
            c.inputs.push_back(CaseValue::vec(vEls));
            c.inputs.push_back(CaseValue(scalarInput(kind, qs)));
            c.expected = CaseValue::vec(vEls);
            for (const FPInterval& iv : intervals) {
                c.expectedAccept.push_back(intervalToExpected(kind, iv));
            }
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

std::vector<Case> generateScalarVectorToVectorCases(
    FPKind kind,
    const std::vector<double>& scalars,
    const std::vector<std::vector<double>>& vectors,
    bool finiteFilter,
    const ScalarPairToInterval& op) {
    std::vector<Case> cases;
    for (double s : scalars) {
        for (const std::vector<double>& vec : vectors) {
            const double qs = quantize(kind, s);
            std::vector<double> qv;
            qv.reserve(vec.size());
            for (double e : vec) {
                qv.push_back(quantize(kind, e));
            }
            std::vector<FPInterval> intervals;
            bool nonFinite = false;
            for (double e : qv) {
                FPInterval iv = op(qs, e);
                if (!iv.isFinite()) {
                    nonFinite = true;
                }
                intervals.push_back(iv);
            }
            if (finiteFilter && nonFinite) {
                continue;
            }
            Case c;
            std::vector<Scalar> vEls;
            for (double e : qv) {
                vEls.push_back(scalarInput(kind, e));
            }
            c.inputs.push_back(CaseValue(scalarInput(kind, qs)));
            c.inputs.push_back(CaseValue::vec(vEls));
            c.expected = CaseValue::vec(vEls);
            for (const FPInterval& iv : intervals) {
                c.expectedAccept.push_back(intervalToExpected(kind, iv));
            }
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

} // namespace fp
} // namespace expression
} // namespace cts
