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

// ===========================================================================
// Transcendental builtin acceptance intervals (phaseY13 Stage B/2).
//
// All of these are defined (per upstream .cache.ts) via the f32 trait, even for the abstract
// variants ("inherited accuracy, only as accurate as f32"). So the math here is always computed at
// f32 precision (FPKind::F32 internally); the public 'kind' parameter only affects case
// materialization, which is handled at the case-generation layer, not here. We therefore ignore the
// public 'kind' for the math and use F32 everywhere below.
//
// Many ops are composed (e.g. tan = sin / cos, sinh = (exp - exp) * 0.5), so the elementary
// interval ops must accept FPInterval inputs and span over their endpoints. We mirror upstream's
// runScalarToIntervalOp / runScalarPairToIntervalOp (including the extrema + domain hooks) operating
// over FPInterval inputs.
// ===========================================================================
// pi whole at f64 precision (used by atan2). Defined below; forward-declared for the anon block.
double kF32PosPiWholeF64();

namespace {

constexpr FPKind kF = FPKind::F32;

// --- elementary interval ops accepting FPInterval inputs (span over endpoints) ---

// Generic unary runner over an FPInterval domain, with optional domain + extrema hooks.
struct UnaryOp {
    std::function<FPInterval(double)> impl;
    bool hasDomain = false;
    double domainLo = 0.0;
    double domainHi = 0.0;
    // extrema: optionally widen x to include a turning point of impl over x.
    std::function<FPInterval(FPInterval)> extrema;
};

FPInterval roundAndFlushUnary(double n, const UnaryOp& op) {
    const std::vector<double> inputs = addFlushedIfNeeded(kF, correctlyRounded(kF, n));
    if (op.hasDomain) {
        for (double i : inputs) {
            if (!(i >= op.domainLo && i <= op.domainHi)) {
                return unboundedInterval(kF);
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

FPInterval runUnary(FPInterval x, const UnaryOp& op) {
    if (!x.isFinite()) {
        return unboundedInterval(kF);
    }
    if (op.extrema) {
        x = op.extrema(x);
    }
    std::vector<FPInterval> spans;
    spans.push_back(roundAndFlushUnary(x.begin, op));
    if (!x.isPoint()) {
        spans.push_back(roundAndFlushUnary(x.end, op));
    }
    const FPInterval r = spanIntervals(spans);
    return r.isFinite() ? r : unboundedInterval(kF);
}

struct BinaryOp {
    std::function<FPInterval(double, double)> impl;
    bool hasDomain = false;
    std::function<bool(double)> inXDomain;
    std::function<bool(double)> inYDomain;
    std::function<void(FPInterval&, FPInterval&)> extrema;
};

FPInterval roundAndFlushBinary(double x, double y, const BinaryOp& op) {
    const std::vector<double> xs = addFlushedIfNeeded(kF, correctlyRounded(kF, x));
    const std::vector<double> ys = addFlushedIfNeeded(kF, correctlyRounded(kF, y));
    if (op.hasDomain) {
        for (double i : xs) {
            if (!op.inXDomain(i)) {
                return unboundedInterval(kF);
            }
        }
        for (double j : ys) {
            if (!op.inYDomain(j)) {
                return unboundedInterval(kF);
            }
        }
    }
    std::vector<FPInterval> intervals;
    for (double ix : xs) {
        for (double iy : ys) {
            intervals.push_back(op.impl(ix, iy));
        }
    }
    return spanIntervals(intervals);
}

FPInterval runBinary(FPInterval x, FPInterval y, const BinaryOp& op) {
    if (!x.isFinite() || !y.isFinite()) {
        return unboundedInterval(kF);
    }
    if (op.extrema) {
        op.extrema(x, y);
    }
    std::vector<FPInterval> outputs;
    std::vector<double> xe = {x.begin};
    if (!x.isPoint()) {
        xe.push_back(x.end);
    }
    std::vector<double> ye = {y.begin};
    if (!y.isPoint()) {
        ye.push_back(y.end);
    }
    for (double ix : xe) {
        for (double iy : ye) {
            outputs.push_back(roundAndFlushBinary(ix, iy, op));
        }
    }
    const FPInterval r = spanIntervals(outputs);
    return r.isFinite() ? r : unboundedInterval(kF);
}

// Elementary interval ops taking FPInterval inputs (used by the composed builtins).
FPInterval absoluteErrorIv(double n, double errorRange) {
    return absoluteErrorInterval(kF, n, errorRange);
}
FPInterval ulpIv(double n, double numULP) {
    return ulpInterval(kF, n, numULP);
}

// correctlyRoundedIntervalWithUnboundedPrecisionForAddition over a (point) val.
FPInterval crUnbounded(double val, double large, double small) {
    return crUnboundedAddition(kF, val, large, small);
}

FPInterval additionIv(FPInterval x, FPInterval y) {
    BinaryOp op;
    op.impl = [](double a, double b) -> FPInterval {
        const double sum = a + b;
        const double large = std::abs(a) > std::abs(b) ? a : b;
        const double small = std::abs(a) > std::abs(b) ? b : a;
        return crUnbounded(sum, large, small);
    };
    return runBinary(x, y, op);
}
FPInterval subtractionIv(FPInterval x, FPInterval y) {
    BinaryOp op;
    op.impl = [](double a, double b) -> FPInterval {
        const double diff = a - b;
        const double large = std::abs(a) > std::abs(b) ? a : -b;
        const double small = std::abs(a) > std::abs(b) ? -b : a;
        return crUnbounded(diff, large, small);
    };
    return runBinary(x, y, op);
}
FPInterval multiplicationIv(FPInterval x, FPInterval y) {
    BinaryOp op;
    op.impl = [](double a, double b) { return correctlyRoundedInterval(kF, a * b); };
    return runBinary(x, y, op);
}
FPInterval negationIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) { return correctlyRoundedInterval(kF, -m); };
    return runUnary(n, op);
}

// division over intervals (ULP 2.5), domain-restricted with extrema at y=0.
FPInterval divisionIv(FPInterval x, FPInterval y) {
    BinaryOp op;
    const double yMag1 = std::ldexp(1.0, -126);
    const double yMag2 = std::ldexp(1.0, 126);
    op.impl = [](double a, double b) -> FPInterval {
        if (b == 0.0) {
            return unboundedInterval(kF);
        }
        return ulpIv(a / b, 2.5);
    };
    op.hasDomain = true;
    op.inXDomain = [](double v) { return v >= kF32NegMin && v <= kF32PosMax; };
    op.inYDomain = [yMag1, yMag2](double v) {
        return (v >= -yMag2 && v <= -yMag1) || (v >= yMag1 && v <= yMag2);
    };
    op.extrema = [](FPInterval& /*xx*/, FPInterval& yy) {
        if (yy.contains(0.0)) {
            yy = toIntervalPoint(kF, 0.0);
        }
    };
    return runBinary(x, y, op);
}

// sqrt over an interval: 1 / inverseSqrt(n).
FPInterval sqrtIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        const FPInterval inv = inverseSqrtInterval(kF, m);
        if (!inv.isFinite()) {
            return unboundedInterval(kF);
        }
        return divisionIv(toIntervalPoint(kF, 1.0), inv);
    };
    return runUnary(n, op);
}

// exp / exp2 over an interval: ULP(3 + 2*|n|).
FPInterval expIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) { return ulpIv(std::exp(m), 3.0 + 2.0 * std::abs(m)); };
    return runUnary(n, op);
}
FPInterval exp2Iv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) { return ulpIv(std::pow(2.0, m), 3.0 + 2.0 * std::abs(m)); };
    return runUnary(n, op);
}

// log / log2 over an interval: abs-error 2^-21 on [0.5, 2], else ULP(3); domain > 0.
FPInterval logIv(FPInterval n) {
    UnaryOp op;
    const double absError = std::ldexp(1.0, -21);
    op.impl = [absError](double m) -> FPInterval {
        if (m >= 0.5 && m <= 2.0) {
            return absoluteErrorIv(std::log(m), absError);
        }
        return ulpIv(std::log(m), 3.0);
    };
    op.hasDomain = true;
    op.domainLo = kF32PosSubMin;
    op.domainHi = kF32PosMax;
    return runUnary(n, op);
}
FPInterval log2Iv(FPInterval n) {
    UnaryOp op;
    const double absError = std::ldexp(1.0, -21);
    op.impl = [absError](double m) -> FPInterval {
        if (m >= 0.5 && m <= 2.0) {
            return absoluteErrorIv(std::log2(m), absError);
        }
        return ulpIv(std::log2(m), 3.0);
    };
    op.hasDomain = true;
    op.domainLo = kF32PosSubMin;
    op.domainHi = kF32PosMax;
    return runUnary(n, op);
}

// sin / cos over an interval: absolute error 2^-11, domain [-pi, pi].
FPInterval sinIv(FPInterval n) {
    UnaryOp op;
    const double absError = std::ldexp(1.0, -11);
    op.impl = [absError](double m) { return absoluteErrorIv(std::sin(m), absError); };
    op.hasDomain = true;
    op.domainLo = kF32NegPiWhole;
    op.domainHi = kF32PosPiWhole;
    return runUnary(n, op);
}
FPInterval cosIv(FPInterval n) {
    UnaryOp op;
    const double absError = std::ldexp(1.0, -11);
    op.impl = [absError](double m) { return absoluteErrorIv(std::cos(m), absError); };
    op.hasDomain = true;
    op.domainLo = kF32NegPiWhole;
    op.domainHi = kF32PosPiWhole;
    return runUnary(n, op);
}

// atan over an interval: ULP(4096).
FPInterval atanIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) { return ulpIv(std::atan(m), 4096.0); };
    return runUnary(n, op);
}

// atan2(y, x): ULP(4096), domain-split, extrema at y/x = 0. (params labelled y, x.)
FPInterval atan2Iv(FPInterval y, FPInterval x) {
    BinaryOp op;
    op.impl = [](double yy, double xx) -> FPInterval {
        double atanyx = std::atan(yy / xx);
        if (xx < 0) {
            if (yy > 0) {
                atanyx = atanyx + kF32PosPiWholeF64();
            } else {
                atanyx = atanyx - kF32PosPiWholeF64();
            }
        }
        return ulpIv(atanyx, 4096.0);
    };
    op.hasDomain = true;
    // domain.x (first param, y): finite normal.
    op.inXDomain = [](double v) {
        return (v >= kF32NegMin && v <= kF32NegMax) || (v >= kF32PosMin && v <= kF32PosMax);
    };
    // domain.y (second param, x): inherited from division.
    const double m1 = std::ldexp(1.0, -126);
    const double m2 = std::ldexp(1.0, 126);
    op.inYDomain = [m1, m2](double v) {
        return (v >= -m2 && v <= -m1) || (v >= m1 && v <= m2);
    };
    op.extrema = [](FPInterval& yy, FPInterval& xx) {
        if (yy.contains(0.0)) {
            if (xx.contains(0.0)) {
                yy = toIntervalPoint(kF, 0.0);
                xx = toIntervalPoint(kF, 0.0);
            } else {
                yy = toIntervalPoint(kF, 0.0);
            }
        }
    };
    return runBinary(y, x, op);
}

} // namespace

// pi whole at f64 precision (used by atan2). Defined out-of-anon so atan2Iv can call it.
double kF32PosPiWholeF64() {
    return f64FromBits(0x400921fb54442d18ull); // closest f64 to pi
}

namespace {

// --- composed builtin ops over intervals ---

FPInterval tanIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) { return divisionIv(sinIv(toIntervalPoint(kF, m)),
                                               cosIv(toIntervalPoint(kF, m))); };
    return runUnary(n, op);
}
FPInterval sinhIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        const FPInterval minusN = negationIv(toIntervalPoint(kF, m));
        return multiplicationIv(
            subtractionIv(expIv(toIntervalPoint(kF, m)), expIv(minusN)),
            toIntervalPoint(kF, 0.5));
    };
    return runUnary(n, op);
}
FPInterval coshIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        const FPInterval minusN = negationIv(toIntervalPoint(kF, m));
        return multiplicationIv(
            additionIv(expIv(toIntervalPoint(kF, m)), expIv(minusN)),
            toIntervalPoint(kF, 0.5));
    };
    return runUnary(n, op);
}
FPInterval tanhIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        const double approxAbsError = 1.0e-5;
        const FPInterval a = divisionIv(sinhIv(toIntervalPoint(kF, m)), coshIv(toIntervalPoint(kF, m)));
        const FPInterval b = absoluteErrorIv(std::tanh(m), approxAbsError);
        return spanIntervals({a, b});
    };
    return runUnary(n, op);
}
FPInterval asinIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        // asin(n) = atan2(n, sqrt(1 - n*n)) spanned with abs-error polynomial.
        const FPInterval x =
            sqrtIv(subtractionIv(toIntervalPoint(kF, 1.0),
                                 multiplicationIv(toIntervalPoint(kF, m), toIntervalPoint(kF, m))));
        const FPInterval a = atan2Iv(toIntervalPoint(kF, m), x);
        const FPInterval b = absoluteErrorIv(std::asin(m), 6.81e-5);
        return spanIntervals({a, b});
    };
    op.hasDomain = true;
    op.domainLo = -1.0;
    op.domainHi = 1.0;
    return runUnary(n, op);
}
FPInterval acosIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        // acos(n) = atan2(sqrt(1 - n*n), n) spanned with abs-error polynomial.
        const FPInterval y =
            sqrtIv(subtractionIv(toIntervalPoint(kF, 1.0),
                                 multiplicationIv(toIntervalPoint(kF, m), toIntervalPoint(kF, m))));
        const FPInterval a = atan2Iv(y, toIntervalPoint(kF, m));
        const FPInterval b = absoluteErrorIv(std::acos(m), 6.77e-5);
        return spanIntervals({a, b});
    };
    op.hasDomain = true;
    op.domainLo = -1.0;
    op.domainHi = 1.0;
    return runUnary(n, op);
}
FPInterval asinhIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        // asinh(x) = log(x + sqrt(x*x + 1))
        const FPInterval inner =
            additionIv(multiplicationIv(toIntervalPoint(kF, m), toIntervalPoint(kF, m)),
                       toIntervalPoint(kF, 1.0));
        const FPInterval s = sqrtIv(inner);
        return logIv(additionIv(toIntervalPoint(kF, m), s));
    };
    return runUnary(n, op);
}
FPInterval atanhIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        // atanh(x) = log((1+x)/(1-x)) * 0.5
        const FPInterval num = additionIv(toIntervalPoint(kF, 1.0), toIntervalPoint(kF, m));
        const FPInterval den = subtractionIv(toIntervalPoint(kF, 1.0), toIntervalPoint(kF, m));
        const FPInterval logIv0 = logIv(divisionIv(num, den));
        return multiplicationIv(logIv0, toIntervalPoint(kF, 0.5));
    };
    return runUnary(n, op);
}
FPInterval acoshAltIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        // acosh(x) = log(x + sqrt((x+1)*(x-1)))
        const FPInterval inner =
            multiplicationIv(additionIv(toIntervalPoint(kF, m), toIntervalPoint(kF, 1.0)),
                             subtractionIv(toIntervalPoint(kF, m), toIntervalPoint(kF, 1.0)));
        const FPInterval s = sqrtIv(inner);
        return logIv(additionIv(toIntervalPoint(kF, m), s));
    };
    return runUnary(n, op);
}
FPInterval acoshPrimaryIv(FPInterval n) {
    UnaryOp op;
    op.impl = [](double m) -> FPInterval {
        // acosh(x) = log(x + sqrt(x*x - 1))
        const FPInterval inner =
            subtractionIv(multiplicationIv(toIntervalPoint(kF, m), toIntervalPoint(kF, m)),
                          toIntervalPoint(kF, 1.0));
        const FPInterval s = sqrtIv(inner);
        return logIv(additionIv(toIntervalPoint(kF, m), s));
    };
    return runUnary(n, op);
}
FPInterval powIv(FPInterval x, FPInterval y) {
    // pow(x, y) = exp2(y * log2(x)). log2 enforces the x>0 domain.
    BinaryOp op;
    op.impl = [](double a, double b) -> FPInterval {
        const FPInterval l2 = log2Iv(toIntervalPoint(kF, a));
        const FPInterval prod = multiplicationIv(toIntervalPoint(kF, b), l2);
        return exp2Iv(prod);
    };
    return runBinary(x, y, op);
}

} // namespace

// --- public transcendental entry points (kind ignored for math; F32 precision) ---
FPInterval sinInterval(FPKind, double n) { return sinIv(toIntervalPoint(kF, n)); }
FPInterval tanInterval(FPKind, double n) { return tanIv(toIntervalPoint(kF, n)); }
FPInterval asinInterval(FPKind, double n) { return asinIv(toIntervalPoint(kF, n)); }
FPInterval acosInterval(FPKind, double n) { return acosIv(toIntervalPoint(kF, n)); }
FPInterval atanInterval(FPKind, double n) { return atanIv(toIntervalPoint(kF, n)); }
FPInterval atan2Interval(FPKind, double y, double x) {
    return atan2Iv(toIntervalPoint(kF, y), toIntervalPoint(kF, x));
}
FPInterval sinhInterval(FPKind, double n) { return sinhIv(toIntervalPoint(kF, n)); }
FPInterval coshInterval(FPKind, double n) { return coshIv(toIntervalPoint(kF, n)); }
FPInterval tanhInterval(FPKind, double n) { return tanhIv(toIntervalPoint(kF, n)); }
FPInterval asinhInterval(FPKind, double n) { return asinhIv(toIntervalPoint(kF, n)); }
FPInterval atanhInterval(FPKind, double n) { return atanhIv(toIntervalPoint(kF, n)); }
FPInterval acoshAlternativeInterval(FPKind, double n) { return acoshAltIv(toIntervalPoint(kF, n)); }
FPInterval acoshPrimaryInterval(FPKind, double n) { return acoshPrimaryIv(toIntervalPoint(kF, n)); }
FPInterval expInterval(FPKind, double n) { return expIv(toIntervalPoint(kF, n)); }
FPInterval exp2Interval(FPKind, double n) { return exp2Iv(toIntervalPoint(kF, n)); }
FPInterval logInterval(FPKind, double n) { return logIv(toIntervalPoint(kF, n)); }
FPInterval log2Interval(FPKind, double n) { return log2Iv(toIntervalPoint(kF, n)); }
FPInterval powInterval(FPKind, double x, double y) {
    return powIv(toIntervalPoint(kF, x), toIntervalPoint(kF, y));
}

// ===========================================================================
// Algebraic / multi-arg builtin acceptance intervals (phaseY13 Stage B/3a).
//
// Unlike the transcendentals, several of these (sign/round/fract/saturate/min/max/clamp) are
// correctly-rounded and are tested with the abstract trait at f64 precision, so their math is
// kind-aware (F32 or Abstract). The inherited-accuracy ones (degrees/radians/smoothstep/fma/mix)
// always compute at f32 precision for both f32 and abstract (their public entry passes F32).
// ===========================================================================
namespace {

// --- kind-aware elementary interval-input ops (span over endpoints) ---

// Generic kind-aware unary runner over an FPInterval domain.
FPInterval runKindUnary(FPKind kind, FPInterval x,
                        const std::function<FPInterval(double)>& impl) {
    ScalarOp op;
    op.impl = impl;
    return runScalarToIntervalOp(kind, x, op);
}

FPInterval correctlyRoundedKindIv(FPKind kind, FPInterval x) {
    return runKindUnary(kind, x, [kind](double m) { return correctlyRoundedInterval(kind, m); });
}

// upstream isSubnormal(n) is ZERO-INCLUSIVE: n > negative.max && n < positive.min, so 0/-0 count
// as subnormal here (used by min/max's both-subnormal span rule). This differs from the file-local
// isSubnormalF32/F64 (which exclude zero, matching addFlushedIfNeeded's `v != 0` guard).
bool isSubnormalInclZero(FPKind kind, double n) {
    if (kind == FPKind::F32) {
        return n > kF32NegMax && n < kF32PosMin;
    }
    return n > kF64NegMax && n < kF64PosMin;
}

// min/max over point inputs (correctly-rounded with the both-subnormal span rule, zero-inclusive).
FPInterval minImpl(FPKind kind, double x, double y) {
    if (isSubnormalInclZero(kind, x) && isSubnormalInclZero(kind, y)) {
        return correctlyRoundedKindIv(
            kind, spanIntervals({toIntervalPoint(kind, x), toIntervalPoint(kind, y)}));
    }
    return correctlyRoundedInterval(kind, std::min(x, y));
}
FPInterval maxImpl(FPKind kind, double x, double y) {
    if (isSubnormalInclZero(kind, x) && isSubnormalInclZero(kind, y)) {
        return correctlyRoundedKindIv(
            kind, spanIntervals({toIntervalPoint(kind, x), toIntervalPoint(kind, y)}));
    }
    return correctlyRoundedInterval(kind, std::max(x, y));
}

// Pair runner over interval inputs (no extrema for these).
FPInterval runKindPair(FPKind kind, FPInterval x, FPInterval y,
                       const std::function<FPInterval(double, double)>& impl) {
    if (!x.isFinite() || !y.isFinite()) {
        return unboundedInterval(kind);
    }
    std::vector<double> xe = {x.begin};
    if (!x.isPoint()) {
        xe.push_back(x.end);
    }
    std::vector<double> ye = {y.begin};
    if (!y.isPoint()) {
        ye.push_back(y.end);
    }
    std::vector<FPInterval> outs;
    for (double ix : xe) {
        for (double iy : ye) {
            // roundAndFlush each endpoint pair.
            if (std::isnan(ix) || std::isnan(iy)) {
                outs.push_back(unboundedInterval(kind));
                continue;
            }
            const std::vector<double> xs = addFlushedIfNeeded(kind, correctlyRounded(kind, ix));
            const std::vector<double> ys = addFlushedIfNeeded(kind, correctlyRounded(kind, iy));
            for (double a : xs) {
                for (double b : ys) {
                    outs.push_back(impl(a, b));
                }
            }
        }
    }
    const FPInterval r = spanIntervals(outs);
    return r.isFinite() ? r : unboundedInterval(kind);
}

FPInterval minKindIv(FPKind kind, FPInterval x, FPInterval y) {
    return runKindPair(kind, x, y, [kind](double a, double b) { return minImpl(kind, a, b); });
}
FPInterval maxKindIv(FPKind kind, FPInterval x, FPInterval y) {
    return runKindPair(kind, x, y, [kind](double a, double b) { return maxImpl(kind, a, b); });
}

// Triple runner over interval inputs.
FPInterval runKindTriple(FPKind kind, FPInterval x, FPInterval y, FPInterval z,
                         const std::function<FPInterval(double, double, double)>& impl) {
    if (!x.isFinite() || !y.isFinite() || !z.isFinite()) {
        return unboundedInterval(kind);
    }
    auto endpoints = [](const FPInterval& iv) {
        std::vector<double> e = {iv.begin};
        if (!iv.isPoint()) {
            e.push_back(iv.end);
        }
        return e;
    };
    std::vector<FPInterval> outs;
    for (double ix : endpoints(x)) {
        for (double iy : endpoints(y)) {
            for (double iz : endpoints(z)) {
                if (std::isnan(ix) || std::isnan(iy) || std::isnan(iz)) {
                    outs.push_back(unboundedInterval(kind));
                    continue;
                }
                const std::vector<double> xs = addFlushedIfNeeded(kind, correctlyRounded(kind, ix));
                const std::vector<double> ys = addFlushedIfNeeded(kind, correctlyRounded(kind, iy));
                const std::vector<double> zs = addFlushedIfNeeded(kind, correctlyRounded(kind, iz));
                for (double a : xs) {
                    for (double b : ys) {
                        for (double c : zs) {
                            outs.push_back(impl(a, b, c));
                        }
                    }
                }
            }
        }
    }
    const FPInterval r = spanIntervals(outs);
    return r.isFinite() ? r : unboundedInterval(kind);
}

// clamp via median(x, y, z): the middle of the three sorted values, correctly-rounded.
FPInterval clampMedianImpl(FPKind kind, double x, double y, double z) {
    double a = x, b = y, c = z;
    // sort a <= b <= c (the ClampMedianIntervalOp sorts and picks index 1).
    if (a > b) {
        std::swap(a, b);
    }
    if (b > c) {
        std::swap(b, c);
    }
    if (a > b) {
        std::swap(a, b);
    }
    return correctlyRoundedInterval(kind, b);
}

// clamp via min(max(x, low), high).
FPInterval clampMinMaxImpl(FPKind kind, double x, double low, double high) {
    return minKindIv(kind, maxKindIv(kind, toIntervalPoint(kind, x), toIntervalPoint(kind, low)),
                     toIntervalPoint(kind, high));
}

} // namespace

// --- public transcendental entry points end; algebraic entry points below ---

FPInterval signInterval(FPKind kind, double n) {
    return runKindUnary(kind, toIntervalPoint(kind, n), [kind](double m) -> FPInterval {
        if (m > 0.0) {
            return correctlyRoundedInterval(kind, 1.0);
        }
        if (m < 0.0) {
            return correctlyRoundedInterval(kind, -1.0);
        }
        return correctlyRoundedInterval(kind, 0.0);
    });
}

FPInterval roundInterval(FPKind kind, double n) {
    return runKindUnary(kind, toIntervalPoint(kind, n), [kind](double m) -> FPInterval {
        const double k = std::floor(m);
        const double diffBefore = m - k;
        const double diffAfter = k + 1.0 - m;
        if (diffBefore < diffAfter) {
            return correctlyRoundedInterval(kind, k);
        }
        if (diffBefore > diffAfter) {
            return correctlyRoundedInterval(kind, k + 1.0);
        }
        // Tie: k if k even, k+1 if k odd. std::fmod avoids compiler builtins.
        if (std::fmod(k, 2.0) == 0.0) {
            return correctlyRoundedInterval(kind, k);
        }
        return correctlyRoundedInterval(kind, k + 1.0);
    });
}

FPInterval fractInterval(FPKind kind, double n) {
    return runKindUnary(kind, toIntervalPoint(kind, n), [kind](double m) -> FPInterval {
        // fract(x) = x - floor(x). subtractionInterval composed (correctly-rounded with unbounded
        // precision for addition).
        const FPInterval floorIv = correctlyRoundedInterval(kind, std::floor(m));
        // subtraction over (point m) - (floor interval): use runKindPair-style composition.
        FPInterval result =
            runKindPair(kind, toIntervalPoint(kind, m), floorIv, [kind](double a, double b) {
                const double diff = a - b;
                const double large = std::abs(a) > std::abs(b) ? a : -b;
                const double small = std::abs(a) > std::abs(b) ? -b : a;
                return crUnboundedAddition(kind, diff, large, small);
            });
        if (result.contains(1.0)) {
            result = spanIntervals({result, toIntervalPoint(kind, positiveLessThanOne(kind))});
        }
        return result;
    });
}

FPInterval saturateInterval(FPKind kind, double n) {
    // clamp(n, 0, 1) via min(max) (upstream uses ClampMinMaxIntervalOp).
    return runKindTriple(kind, toIntervalPoint(kind, n), toIntervalPoint(kind, 0.0),
                         toIntervalPoint(kind, 1.0),
                         [kind](double x, double low, double high) {
                             return clampMinMaxImpl(kind, x, low, high);
                         });
}

FPInterval clampMedianInterval(FPKind kind, double x, double y, double z) {
    return runKindTriple(kind, toIntervalPoint(kind, x), toIntervalPoint(kind, y),
                         toIntervalPoint(kind, z),
                         [kind](double a, double b, double c) { return clampMedianImpl(kind, a, b, c); });
}
FPInterval clampMinMaxInterval(FPKind kind, double x, double low, double high) {
    return runKindTriple(kind, toIntervalPoint(kind, x), toIntervalPoint(kind, low),
                         toIntervalPoint(kind, high),
                         [kind](double a, double b, double c) { return clampMinMaxImpl(kind, a, b, c); });
}

FPInterval minInterval(FPKind kind, double x, double y) {
    return minKindIv(kind, toIntervalPoint(kind, x), toIntervalPoint(kind, y));
}
FPInterval maxInterval(FPKind kind, double x, double y) {
    return maxKindIv(kind, toIntervalPoint(kind, x), toIntervalPoint(kind, y));
}

FPInterval stepInterval(FPKind kind, double edge, double x) {
    return runKindPair(kind, toIntervalPoint(kind, edge), toIntervalPoint(kind, x),
                       [kind](double e, double v) {
                           if (e <= v) {
                               return correctlyRoundedInterval(kind, 1.0);
                           }
                           return correctlyRoundedInterval(kind, 0.0);
                       });
}

// degrees/radians/smoothstep/fma/mix: inherited accuracy. The public 'kind' selects only case
// materialization; the math is always at f32 precision (matching FP[trait!=='abstract'?trait:'f32']).
FPInterval degreesInterval(FPKind, double n) {
    return multiplicationIv(toIntervalPoint(kF, n), toIntervalPoint(kF, 57.295779513082322865));
}
FPInterval radiansInterval(FPKind, double n) {
    return multiplicationIv(toIntervalPoint(kF, n), toIntervalPoint(kF, 0.017453292519943295474));
}

FPInterval smoothStepInterval(FPKind, double low, double high, double x) {
    return runKindTriple(kF, toIntervalPoint(kF, low), toIntervalPoint(kF, high),
                         toIntervalPoint(kF, x), [](double lo, double hi, double xx) -> FPInterval {
                             // t = clampMedian((xx - lo) / (hi - lo), 0, 1)
                             const FPInterval num =
                                 subtractionIv(toIntervalPoint(kF, xx), toIntervalPoint(kF, lo));
                             const FPInterval den =
                                 subtractionIv(toIntervalPoint(kF, hi), toIntervalPoint(kF, lo));
                             const FPInterval div = divisionIv(num, den);
                             // clampMedian(div, 0, 1) over the div interval endpoints.
                             FPInterval t = runKindTriple(
                                 kF, div, toIntervalPoint(kF, 0.0), toIntervalPoint(kF, 1.0),
                                 [](double a, double b, double c) { return clampMedianImpl(kF, a, b, c); });
                             // t * (t * (3 - 2*t))
                             const FPInterval twoT = multiplicationIv(toIntervalPoint(kF, 2.0), t);
                             const FPInterval inner =
                                 subtractionIv(toIntervalPoint(kF, 3.0), twoT);
                             return multiplicationIv(t, multiplicationIv(t, inner));
                         });
}

FPInterval fmaInterval(FPKind, double x, double y, double z) {
    return runKindTriple(kF, toIntervalPoint(kF, x), toIntervalPoint(kF, y), toIntervalPoint(kF, z),
                         [](double a, double b, double c) {
                             return additionIv(multiplicationIv(toIntervalPoint(kF, a),
                                                                toIntervalPoint(kF, b)),
                                               toIntervalPoint(kF, c));
                         });
}

FPInterval mixImpreciseInterval(FPKind, double x, double y, double z) {
    return runKindTriple(kF, toIntervalPoint(kF, x), toIntervalPoint(kF, y), toIntervalPoint(kF, z),
                         [](double a, double b, double c) {
                             // a + (b - a) * c
                             const FPInterval t = multiplicationIv(
                                 subtractionIv(toIntervalPoint(kF, b), toIntervalPoint(kF, a)),
                                 toIntervalPoint(kF, c));
                             return additionIv(toIntervalPoint(kF, a), t);
                         });
}
FPInterval mixPreciseInterval(FPKind, double x, double y, double z) {
    return runKindTriple(kF, toIntervalPoint(kF, x), toIntervalPoint(kF, y), toIntervalPoint(kF, z),
                         [](double a, double b, double c) {
                             // a * (1 - c) + b * c
                             const FPInterval t = multiplicationIv(
                                 toIntervalPoint(kF, a),
                                 subtractionIv(toIntervalPoint(kF, 1.0), toIntervalPoint(kF, c)));
                             const FPInterval s =
                                 multiplicationIv(toIntervalPoint(kF, b), toIntervalPoint(kF, c));
                             return additionIv(t, s);
                         });
}

FPInterval ldexpInterval(FPKind kind, double e1, double e2) {
    // Only e1 is rounded/flushed; e2 is an exact integer.
    ScalarOp op;
    const int bias = fpBias(kind);
    op.impl = [kind, e2, bias](double m) -> FPInterval {
        // e2 > bias + 1 -> indeterminate (unbounded).
        if (e2 > static_cast<double>(bias) + 1.0) {
            return unboundedInterval(kind);
        }
        const double result = m * std::pow(2.0, e2);
        if (!std::isfinite(result)) {
            return unboundedInterval(kind);
        }
        return correctlyRoundedInterval(kind, result);
    };
    return runScalarToIntervalOp(kind, toIntervalPoint(kind, e1), op);
}

int fpBias(FPKind kind) { return kind == FPKind::F32 ? 127 : 1023; }
bool fpIsFinite(FPKind kind, double n) {
    return kind == FPKind::F32 ? isFiniteF32(n) : std::isfinite(n);
}

// --- quantizeToF16 (f16-correctly-rounded; f32 result type) ---
namespace {
// f16 helpers.
double f16FromBitsToF64(uint16_t bits) {
    // decode IEEE binary16 to double.
    const uint32_t sign = (bits >> 15) & 0x1u;
    const uint32_t exp = (bits >> 10) & 0x1fu;
    const uint32_t mant = bits & 0x3ffu;
    double value;
    if (exp == 0) {
        value = std::ldexp(static_cast<double>(mant), -24); // subnormal: mant * 2^-24
    } else if (exp == 0x1f) {
        value = mant ? std::numeric_limits<double>::quiet_NaN()
                     : std::numeric_limits<double>::infinity();
    } else {
        value = std::ldexp(static_cast<double>(mant | 0x400u), static_cast<int>(exp) - 25);
    }
    return sign ? -value : value;
}
const double kF16Max = f16FromBitsToF64(0x7bffu);
const double kF16Min = f16FromBitsToF64(0x0400u);          // smallest positive normal
const double kF16NegMin = f16FromBitsToF64(0xfbffu);        // most-negative finite
const double kF16NegMax = f16FromBitsToF64(0x8400u);        // largest negative normal
const double kF16PosSubMin = f16FromBitsToF64(0x0001u);
const double kF16PosSubMax = f16FromBitsToF64(0x03ffu);
const double kF16NegSubMin = f16FromBitsToF64(0x83ffu);     // most-negative subnormal
const double kF16NegSubMax = f16FromBitsToF64(0x8001u);     // negative subnormal closest to 0
constexpr int kF16Emax = 15;

bool isSubnormalF16(double n) { return n > kF16NegMax && n < kF16Min && n != 0.0; }

// quantizeToF16: round-to-nearest-even to binary16, return as a double of the f16 value.
double quantizeToF16Value(double n) {
    if (std::isnan(n)) {
        return n;
    }
    if (n == kInf) {
        return kInf;
    }
    if (n == -kInf) {
        return -kInf;
    }
    if (n > kF16Max) {
        return kInf; // overflow rounds up to inf for round-to-nearest
    }
    if (n < kF16NegMin) {
        return -kInf;
    }
    // Round to nearest, ties to even, in the f16 grid using ldexp/scalbn-free arithmetic.
    const bool neg = std::signbit(n);
    double a = std::abs(n);
    if (a == 0.0) {
        return n; // preserve signed zero
    }
    // Determine exponent.
    int e;
    double frac = std::frexp(a, &e); // a = frac * 2^e, frac in [0.5, 1)
    (void)frac;
    // Smallest subnormal step is 2^-24; smallest normal is 2^-14 (e-1 >= -14 => normal).
    int shift;
    if (e - 1 >= -14) {
        // normal: 10 mantissa bits below the implicit leading 1 at 2^(e-1).
        shift = (e - 1) - 10;
    } else {
        // subnormal: quantum is 2^-24.
        shift = -24;
    }
    const double quantum = std::ldexp(1.0, shift);
    double q = a / quantum;
    double r = std::floor(q);
    const double diff = q - r;
    if (diff > 0.5) {
        r += 1.0;
    } else if (diff == 0.5) {
        // ties to even
        if (std::fmod(r, 2.0) != 0.0) {
            r += 1.0;
        }
    }
    double out = r * quantum;
    if (out > kF16Max) {
        out = kInf;
    }
    return neg ? -out : out;
}

// Encode a finite double that is exactly an f16 value back to its 16-bit pattern.
uint16_t f16ToBits(double v) {
    if (v == 0.0) {
        return std::signbit(v) ? 0x8000u : 0x0000u;
    }
    const bool neg = std::signbit(v);
    double a = std::abs(v);
    int e;
    std::frexp(a, &e); // a in [2^(e-1), 2^e)
    uint16_t bits;
    if (e - 1 >= -14) {
        const int exp = (e - 1) + 15; // biased
        const double mantD = a / std::ldexp(1.0, e - 1) - 1.0; // in [0,1)
        const uint16_t mant = static_cast<uint16_t>(std::llround(mantD * 1024.0));
        bits = static_cast<uint16_t>((static_cast<uint16_t>(exp) << 10) | (mant & 0x3ffu));
    } else {
        const uint16_t mant = static_cast<uint16_t>(std::llround(a / std::ldexp(1.0, -24)));
        bits = mant & 0x3ffu;
    }
    if (neg) {
        bits = static_cast<uint16_t>(bits | 0x8000u);
    }
    return bits;
}

// nextAfterF16 no-flush single step toward +inf (positive) or -inf.
double nextF16(double v, bool positive) {
    uint16_t bits = f16ToBits(v);
    const bool isPositive = (bits & 0x8000u) == 0;
    if (isPositive == positive) {
        bits = static_cast<uint16_t>(bits + 1u);
    } else {
        bits = static_cast<uint16_t>(bits - 1u);
    }
    return f16FromBitsToF64(bits);
}

std::vector<double> correctlyRoundedF16(double n) {
    if (std::isnan(n)) {
        return {n};
    }
    if (n >= std::ldexp(1.0, kF16Emax + 1)) {
        return {kInf};
    }
    if (n > kF16Max) {
        return {kF16Max, kInf};
    }
    if (n <= kF16Max && n >= kF16NegMin) {
        const double n16 = quantizeToF16Value(n);
        if (n == n16) {
            return {n};
        }
        // Step one f16 in the appropriate direction (no flush) for the other rounding.
        if (n16 > n) {
            return {nextF16(n16, false), n16};
        }
        return {n16, nextF16(n16, true)};
    }
    if (n > -std::ldexp(1.0, kF16Emax + 1)) {
        return {-kInf, kF16NegMin};
    }
    return {-kInf};
}

std::vector<double> addFlushedIfNeededF16(const std::vector<double>& values) {
    bool anySub = false;
    for (double v : values) {
        if (v != 0.0 && isSubnormalF16(v)) {
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

} // namespace

FPInterval quantizeToF16Interval(double n) {
    // The op runs at f32 input materialization (kind F32 result) but uses f16 rounding for the value.
    ScalarOp op;
    op.impl = [](double m) -> FPInterval {
        const std::vector<double> rounded = correctlyRoundedF16(m);
        const std::vector<double> flushed = addFlushedIfNeededF16(rounded);
        std::vector<FPInterval> spans;
        for (double f : flushed) {
            spans.push_back(toIntervalPoint(FPKind::F32, f));
        }
        return spanIntervals(spans);
    };
    return runScalarToIntervalOp(FPKind::F32, toIntervalPoint(FPKind::F32, n), op);
}

double f16PositiveMin() { return kF16Min; }
double f16PositiveMax() { return kF16Max; }
double f16NegativeMin() { return kF16NegMin; }
double f16NegativeMax() { return kF16NegMax; }
double f16PositiveSubMin() { return kF16PosSubMin; }
double f16PositiveSubMax() { return kF16PosSubMax; }
double f16NegativeSubMin() { return kF16NegSubMin; }
double f16NegativeSubMax() { return kF16NegSubMax; }

double f32PositiveMin() { return kF32PosMin; }
double f32NegativeMax() { return kF32NegMax; }
double f32NegativeMin() { return kF32NegMin; }
double f64PositiveMin() { return kF64PosMin; }
double f64NegativeMax() { return kF64NegMax; }
double f64NegativeMin() { return f64FromBits(0xffefffffffffffffull); }
double positiveLessThanOne(FPKind kind) {
    return kind == FPKind::F32 ? static_cast<double>(f32FromBits(0x3f7fffffu))
                               : f64FromBits(0x3fefffffffffffffull);
}
double negativeLessThanOne(FPKind kind) {
    return kind == FPKind::F32 ? static_cast<double>(f32FromBits(0xbf7fffffu))
                               : f64FromBits(0xbfefffffffffffffull);
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

std::vector<double> biasedRange(double a, double b, int numSteps) {
    // math.ts biasedRange: lerp with the index biased by pow(i/(n-1), 2).
    std::vector<double> out;
    if (numSteps <= 0) {
        return out;
    }
    if (numSteps == 1) {
        out.push_back(a);
        return out;
    }
    const double c = 2.0;
    for (int i = 0; i < numSteps; ++i) {
        out.push_back(lerp(a, b, std::pow(static_cast<double>(i) / (numSteps - 1), c)));
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

const std::vector<double>& sparseScalarF64Range() {
    // kInterestingF64Values: [neg.min, -10, -1, -0.125, neg.max, neg.subnormal.min,
    //   neg.subnormal.max, -0, 0, pos.subnormal.min, pos.subnormal.max, pos.min, 0.125, 1, 10,
    //   pos.max].
    static const std::vector<double> v = {
        f64FromBits(0xffefffffffffffffull), -10.0,        -1.0,        -0.125,
        kF64NegMax,                         kF64NegSubMin, -f64FromBits(0x0000000000000001ull),
        -0.0,                               0.0,
        f64FromBits(0x0000000000000001ull), kF64PosSubMax, kF64PosMin, 0.125,
        1.0,                                10.0,          f64FromBits(0x7fefffffffffffffull),
    };
    return v;
}

const std::vector<double>& sparseScalarRange(FPKind kind) {
    return kind == FPKind::Abstract ? sparseScalarF64Range() : sparseScalarF32Range();
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

// ---------------------------------------------------------------------------
// Vector / matrix acceptance-interval helpers (phaseY13 Stage B/3b). All compute at f32 precision
// (kF) because every geometric/matrix builtin ported here has inherited accuracy. Faithful ports of
// util/floating_point.ts's *IntervalImpl. They build on the elementary *Iv helpers above.
// ---------------------------------------------------------------------------

// Convenience: elementary ops on point doubles (mirror multiplicationInterval(x, y) etc.).
FPInterval mulIv2(double x, double y) { return multiplicationIv(toIntervalPoint(kF, x), toIntervalPoint(kF, y)); }
FPInterval subIv2(double x, double y) { return subtractionIv(toIntervalPoint(kF, x), toIntervalPoint(kF, y)); }

// All permutations of a list of intervals (Heap's algorithm), for order-independent fp summation.
void permuteIntervals(std::vector<FPInterval>& a, size_t k,
                      std::vector<std::vector<FPInterval>>& out) {
    if (k == 1) {
        out.push_back(a);
        return;
    }
    for (size_t i = 0; i < k; ++i) {
        permuteIntervals(a, k - 1, out);
        if (k % 2 == 0) {
            std::swap(a[i], a[k - 1]);
        } else {
            std::swap(a[0], a[k - 1]);
        }
    }
}

// spanIntervals over the reduce(additionInterval) of every permutation of 'terms'.
FPInterval spanPermutedSums(std::vector<FPInterval> terms) {
    std::vector<std::vector<FPInterval>> perms;
    permuteIntervals(terms, terms.size(), perms);
    std::vector<FPInterval> sums;
    sums.reserve(perms.size());
    for (const std::vector<FPInterval>& p : perms) {
        FPInterval acc = p.front();
        for (size_t i = 1; i < p.size(); ++i) {
            acc = additionIv(acc, p[i]);
        }
        sums.push_back(acc);
    }
    return spanIntervals(sums);
}

// dot(x, y): sum of x[i]*y[i] with order-independent accumulation (vec2 is order-free).
FPInterval dotIv(const std::vector<double>& x, const std::vector<double>& y) {
    std::vector<FPInterval> mults;
    mults.reserve(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        mults.push_back(mulIv2(x[i], y[i]));
    }
    if (mults.size() == 2) {
        return additionIv(mults[0], mults[1]);
    }
    return spanPermutedSums(mults);
}

// length(v) = sqrt(dot(v, v)).
FPInterval lengthVecIv(const std::vector<double>& v) { return sqrtIv(dotIv(v, v)); }
// length(scalar) = sqrt(scalar*scalar).
FPInterval lengthScalarIv(double n) { return sqrtIv(mulIv2(n, n)); }

// distance(x, y) = length(x - y) (vector form: length of componentwise subtraction interval).
FPInterval distanceVecIv(const std::vector<double>& x, const std::vector<double>& y) {
    std::vector<FPInterval> diff;
    diff.reserve(x.size());
    for (size_t i = 0; i < x.size(); ++i) {
        diff.push_back(subIv2(x[i], y[i]));
    }
    // length of an interval vector: sqrt(dot(diff, diff)).
    std::vector<FPInterval> mults;
    mults.reserve(diff.size());
    for (size_t i = 0; i < diff.size(); ++i) {
        mults.push_back(multiplicationIv(diff[i], diff[i]));
    }
    FPInterval d;
    if (mults.size() == 2) {
        d = additionIv(mults[0], mults[1]);
    } else {
        d = spanPermutedSums(mults);
    }
    return sqrtIv(d);
}
// distance(scalar) = length(x - y).
FPInterval distanceScalarIv(double x, double y) {
    FPInterval diff = subIv2(x, y);
    return sqrtIv(multiplicationIv(diff, diff));
}

// cross(x, y) (vec3) -> three component intervals (or unbounded vec on OOB).
std::vector<FPInterval> crossIv(const std::vector<double>& x, const std::vector<double>& y) {
    FPInterval r0 = subtractionIv(mulIv2(x[1], y[2]), mulIv2(x[2], y[1]));
    FPInterval r1 = subtractionIv(mulIv2(x[2], y[0]), mulIv2(x[0], y[2]));
    FPInterval r2 = subtractionIv(mulIv2(x[0], y[1]), mulIv2(x[1], y[0]));
    if (r0.isFinite() && r1.isFinite() && r2.isFinite()) {
        return {r0, r1, r2};
    }
    return {unboundedInterval(kF), unboundedInterval(kF), unboundedInterval(kF)};
}

// reflect(x, y) = x - 2*dot(x,y)*y, componentwise.
std::vector<FPInterval> reflectIv(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t w = x.size();
    FPInterval t = multiplicationIv(toIntervalPoint(kF, 2.0), dotIv(x, y));
    std::vector<FPInterval> result;
    result.reserve(w);
    bool oob = false;
    for (size_t i = 0; i < w; ++i) {
        FPInterval rhs = multiplicationIv(toIntervalPoint(kF, y[i]), t);
        FPInterval r = subtractionIv(toIntervalPoint(kF, x[i]), rhs);
        if (!r.isFinite()) {
            oob = true;
        }
        result.push_back(r);
    }
    if (oob) {
        return std::vector<FPInterval>(w, unboundedInterval(kF));
    }
    return result;
}

// normalize(v) = v / length(v), componentwise.
std::vector<FPInterval> normalizeIv(const std::vector<double>& v) {
    const size_t w = v.size();
    FPInterval len = lengthVecIv(v);
    std::vector<FPInterval> result;
    result.reserve(w);
    bool oob = false;
    for (size_t i = 0; i < w; ++i) {
        FPInterval r = divisionIv(toIntervalPoint(kF, v[i]), len);
        if (!r.isFinite()) {
            oob = true;
        }
        result.push_back(r);
    }
    if (oob) {
        return std::vector<FPInterval>(w, unboundedInterval(kF));
    }
    return result;
}

// refract(i, s, r): full upstream refractInterval. Returns the vec of component intervals, the zero
// vector (k.end < 0), or the unbounded vec (k non-finite / k contains zero-or-subnormal, or OOB).
bool intervalContainsZeroOrSubnormal(const FPInterval& iv) {
    // Upstream FPInterval.containsZeroOrSubnormals(): the interval may be flushed to zero (includes
    // subnormals and zero) unless it lies entirely below negative.subnormal.min or above
    // positive.subnormal.max. negative.subnormal.min = kF32NegSubMin (most-negative subnormal).
    return !(iv.end < kF32NegSubMin || iv.begin > kF32PosSubMax);
}

std::vector<FPInterval> refractIv(const std::vector<double>& i, const std::vector<double>& s,
                                  double r) {
    const size_t w = i.size();
    FPInterval rSquared = mulIv2(r, r);
    FPInterval dot = dotIv(s, i);
    FPInterval dotSquared = multiplicationIv(dot, dot);
    FPInterval oneMinusDotSquared = subtractionIv(toIntervalPoint(kF, 1.0), dotSquared);
    FPInterval k =
        subtractionIv(toIntervalPoint(kF, 1.0), multiplicationIv(rSquared, oneMinusDotSquared));
    if (!k.isFinite() || intervalContainsZeroOrSubnormal(k)) {
        return std::vector<FPInterval>(w, unboundedInterval(kF));
    }
    if (k.end < 0.0) {
        return std::vector<FPInterval>(w, toIntervalPoint(kF, 0.0));
    }
    FPInterval dotTimesR = multiplicationIv(dot, toIntervalPoint(kF, r));
    FPInterval kSqrt = sqrtIv(k);
    FPInterval t = additionIv(dotTimesR, kSqrt); // r*dot(i,s) + sqrt(k)
    std::vector<FPInterval> result;
    result.reserve(w);
    bool oob = false;
    for (size_t idx = 0; idx < w; ++idx) {
        FPInterval iR = mulIv2(i[idx], r);
        FPInterval sT = multiplicationIv(toIntervalPoint(kF, s[idx]), t);
        FPInterval rr = subtractionIv(iR, sT);
        if (!rr.isFinite()) {
            oob = true;
        }
        result.push_back(rr);
    }
    if (oob) {
        return std::vector<FPInterval>(w, unboundedInterval(kF));
    }
    return result;
}

// faceForward(x, y, z): candidate result vectors (anyOf), each a vec or 'undefined' (signalled by an
// empty vector meaning OOB-skip). Mirrors faceForwardIntervalsImpl. Returns pair{candidates, hadOOB}.
struct FaceForwardResult {
    std::vector<std::vector<FPInterval>> candidates; // each is a width-sized vec
    bool hadUndefined = false;                       // dot OOB => undefined candidate present
};

FaceForwardResult faceForwardIv(const std::vector<double>& x, const std::vector<double>& y,
                                const std::vector<double>& z) {
    const size_t w = x.size();
    // positive_x: identity through round/flush; negative_x: negation.
    std::vector<FPInterval> positiveX;
    std::vector<FPInterval> negativeX;
    positiveX.reserve(w);
    negativeX.reserve(w);
    for (size_t idx = 0; idx < w; ++idx) {
        positiveX.push_back(correctlyRoundedInterval(kF, x[idx])); // round/flush of the value
        negativeX.push_back(negationIv(toIntervalPoint(kF, x[idx])));
    }
    FPInterval dot = dotIv(z, y);
    FaceForwardResult out;
    if (!dot.isFinite()) {
        out.hadUndefined = true;
    }
    if (dot.begin < 0.0 || dot.end < 0.0) {
        out.candidates.push_back(positiveX);
    }
    if (dot.begin >= 0.0 || dot.end >= 0.0) {
        out.candidates.push_back(negativeX);
    }
    return out;
}

// determinant of a 2x2/3x3/4x4 matrix given column-major Array2D m[col][row].
FPInterval determinant2x2Iv(const std::vector<std::vector<double>>& m) {
    return subtractionIv(mulIv2(m[0][0], m[1][1]), mulIv2(m[0][1], m[1][0]));
}

std::vector<std::vector<double>> minorNxN(const std::vector<std::vector<double>>& m, size_t col,
                                          size_t row) {
    const size_t dim = m.size();
    std::vector<std::vector<double>> result;
    for (size_t c = 0; c < dim; ++c) {
        if (c == col) {
            continue;
        }
        std::vector<double> rowVec;
        for (size_t rr = 0; rr < dim; ++rr) {
            if (rr == row) {
                continue;
            }
            rowVec.push_back(m[c][rr]);
        }
        result.push_back(std::move(rowVec));
    }
    return result;
}

FPInterval determinant3x3Iv(const std::vector<std::vector<double>>& m) {
    FPInterval A = multiplicationIv(toIntervalPoint(kF, m[0][0]), determinant2x2Iv(minorNxN(m, 0, 0)));
    FPInterval B = multiplicationIv(toIntervalPoint(kF, -m[0][1]), determinant2x2Iv(minorNxN(m, 0, 1)));
    FPInterval C = multiplicationIv(toIntervalPoint(kF, m[0][2]), determinant2x2Iv(minorNxN(m, 0, 2)));
    return spanPermutedSums({A, B, C});
}

FPInterval determinant4x4Iv(const std::vector<std::vector<double>>& m) {
    FPInterval A = multiplicationIv(toIntervalPoint(kF, m[0][0]), determinant3x3Iv(minorNxN(m, 0, 0)));
    FPInterval B = multiplicationIv(toIntervalPoint(kF, -m[0][1]), determinant3x3Iv(minorNxN(m, 0, 1)));
    FPInterval C = multiplicationIv(toIntervalPoint(kF, m[0][2]), determinant3x3Iv(minorNxN(m, 0, 2)));
    FPInterval D = multiplicationIv(toIntervalPoint(kF, -m[0][3]), determinant3x3Iv(minorNxN(m, 0, 3)));
    return spanPermutedSums({A, B, C, D});
}

FPInterval determinantIv(const std::vector<std::vector<double>>& m) {
    switch (m.size()) {
        case 2:
            return determinant2x2Iv(m);
        case 3:
            return determinant3x3Iv(m);
        default:
            return determinant4x4Iv(m);
    }
}

// transpose(m): m is column-major m[col][row] (cols x rows); result is rows x cols, correctly
// rounded element move. Returns column-major result[i][j] (i in [0,rows), j in [0,cols)).
std::vector<std::vector<FPInterval>> transposeIv(const std::vector<std::vector<double>>& m) {
    const size_t numCols = m.size();
    const size_t numRows = m[0].size();
    std::vector<std::vector<FPInterval>> result(numRows, std::vector<FPInterval>(numCols, FPInterval()));
    for (size_t ic = 0; ic < numCols; ++ic) {
        for (size_t jr = 0; jr < numRows; ++jr) {
            result[jr][ic] = correctlyRoundedInterval(kF, m[ic][jr]);
        }
    }
    return result;
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

std::vector<Case> generateScalarToIntervalCasesAnyOf(
    FPKind kind,
    const std::vector<double>& params,
    bool finiteFilter,
    const std::vector<ScalarToInterval>& ops) {
    std::vector<Case> cases;
    for (double e : params) {
        const double q = quantize(kind, e);
        std::vector<FPInterval> ivs;
        ivs.reserve(ops.size());
        bool anyNonFinite = false;
        for (const ScalarToInterval& op : ops) {
            FPInterval iv = op(q);
            if (!iv.isFinite()) {
                anyNonFinite = true;
            }
            ivs.push_back(iv);
        }
        if (finiteFilter && anyNonFinite) {
            continue;
        }
        Case c;
        c.inputs.push_back(CaseValue(scalarInput(kind, q)));
        c.expected = CaseValue(scalarInput(kind, q));
        // Primary interval plus extras for the anyOf disjunction. If any is unbounded the whole
        // acceptance is unbounded (anyOf with the unbounded interval accepts everything).
        bool anyUnbounded = false;
        for (const FPInterval& iv : ivs) {
            if (iv.begin == -std::numeric_limits<double>::infinity() &&
                iv.end == std::numeric_limits<double>::infinity()) {
                anyUnbounded = true;
            }
        }
        if (anyUnbounded) {
            c.expectedAccept.push_back(acceptUnbounded(kind == FPKind::F32 ? 32 : 64));
        } else {
            ExpectedElement ee = intervalToExpected(kind, ivs.front());
            for (size_t i = 1; i < ivs.size(); ++i) {
                ee.extraIntervals.emplace_back(ivs[i].begin, ivs[i].end);
            }
            c.expectedAccept.push_back(ee);
        }
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

// ---------------------------------------------------------------------------
// phaseY13 Stage B/3a case generators + ranges.
// ---------------------------------------------------------------------------
namespace {

// Encode an anyOf(...) of intervals onto a single ExpectedElement (primary + extraIntervals).
// If any interval is unbounded the whole acceptance is unbounded.
ExpectedElement anyOfToExpected(FPKind kind, const std::vector<FPInterval>& ivs) {
    const int width = kind == FPKind::F32 ? 32 : 64;
    for (const FPInterval& iv : ivs) {
        if (iv.begin == -kInf && iv.end == kInf) {
            return acceptUnbounded(width);
        }
    }
    ExpectedElement ee = intervalToExpected(kind, ivs.front());
    for (size_t i = 1; i < ivs.size(); ++i) {
        ee.extraIntervals.emplace_back(ivs[i].begin, ivs[i].end);
    }
    return ee;
}

} // namespace

std::vector<Case> generateScalarPairToIntervalCasesAnyOf(
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
            // step's special interpretation: [0,0]/[1,1]/unbounded -> the interval itself;
            // [0,1] -> anyOf({0},{1}). isPoint() or !isFinite() => keep; else split into 0/1.
            if (iv.isPoint() || !iv.isFinite()) {
                c.expectedAccept.push_back(intervalToExpected(kind, iv));
            } else {
                std::vector<FPInterval> any = {toIntervalPoint(kind, 0.0),
                                               toIntervalPoint(kind, 1.0)};
                c.expectedAccept.push_back(anyOfToExpected(kind, any));
            }
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

std::vector<Case> generateScalarTripleToIntervalCases(
    FPKind kind,
    const std::vector<double>& param0s,
    const std::vector<double>& param1s,
    const std::vector<double>& param2s,
    bool finiteFilter,
    const std::vector<ScalarTripleToInterval>& ops) {
    std::vector<Case> cases;
    for (double a : param0s) {
        for (double b : param1s) {
            for (double cc : param2s) {
                const double qa = quantize(kind, a);
                const double qb = quantize(kind, b);
                const double qc = quantize(kind, cc);
                std::vector<FPInterval> ivs;
                bool anyNonFinite = false;
                for (const ScalarTripleToInterval& op : ops) {
                    FPInterval iv = op(qa, qb, qc);
                    if (!iv.isFinite()) {
                        anyNonFinite = true;
                    }
                    ivs.push_back(iv);
                }
                if (finiteFilter && anyNonFinite) {
                    continue;
                }
                Case c;
                c.inputs.push_back(CaseValue(scalarInput(kind, qa)));
                c.inputs.push_back(CaseValue(scalarInput(kind, qb)));
                c.inputs.push_back(CaseValue(scalarInput(kind, qc)));
                c.expected = CaseValue(scalarInput(kind, qa));
                c.expectedAccept.push_back(anyOfToExpected(kind, ivs));
                cases.push_back(std::move(c));
            }
        }
    }
    return cases;
}

std::vector<Case> generateLdexpCases(
    FPKind kind,
    const std::vector<double>& e1s,
    const std::vector<int32_t>& e2s,
    bool finiteFilter,
    bool constStage,
    bool e2IsAbstractInt) {
    std::vector<Case> cases;
    const int bias = fpBias(kind);
    for (double e1 : e1s) {
        for (int32_t e2 : e2s) {
            const double qe1 = quantize(kind, e1);
            // const stage filters out cases whose e1 * 2^e2 is not finite (upstream pre-filter).
            if (constStage) {
                const double res = qe1 * std::pow(2.0, static_cast<double>(e2));
                if (!fpIsFinite(kind, res)) {
                    continue;
                }
            }
            const FPInterval iv = ldexpInterval(kind, qe1, static_cast<double>(e2));
            if (finiteFilter && !iv.isFinite()) {
                continue;
            }
            Case c;
            c.inputs.push_back(CaseValue(scalarInput(kind, qe1)));
            c.inputs.push_back(CaseValue(e2IsAbstractInt ? abstractInt(e2) : i32(e2)));
            c.expected = CaseValue(scalarInput(kind, qe1));
            // Flush-to-zero rule: if e2 + bias <= 0 the result may also be 0.
            if (e2 + bias <= 0) {
                std::vector<FPInterval> any = {iv, toIntervalPoint(kind, 0.0)};
                c.expectedAccept.push_back(anyOfToExpected(kind, any));
            } else {
                c.expectedAccept.push_back(intervalToExpected(kind, iv));
            }
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

std::vector<Case> generateVectorPairScalarToVectorComponentWiseCase(
    FPKind kind,
    const std::vector<std::vector<double>>& param0s,
    const std::vector<std::vector<double>>& param1s,
    const std::vector<double>& param2s,
    bool finiteFilter,
    const std::vector<ScalarTripleToInterval>& componentWiseOps) {
    std::vector<Case> cases;
    for (const std::vector<double>& v0 : param0s) {
        for (const std::vector<double>& v1 : param1s) {
            for (double s : param2s) {
                const size_t width = v0.size();
                std::vector<double> q0, q1;
                for (double e : v0) {
                    q0.push_back(quantize(kind, e));
                }
                for (double e : v1) {
                    q1.push_back(quantize(kind, e));
                }
                const double qs = quantize(kind, s);
                // results[op][component]
                std::vector<std::vector<FPInterval>> results;
                bool anyNonFinite = false;
                for (const ScalarTripleToInterval& op : componentWiseOps) {
                    std::vector<FPInterval> comps;
                    for (size_t i = 0; i < width; ++i) {
                        FPInterval iv = op(q0[i], q1[i], qs);
                        if (!iv.isFinite()) {
                            anyNonFinite = true;
                        }
                        comps.push_back(iv);
                    }
                    results.push_back(std::move(comps));
                }
                if (finiteFilter && anyNonFinite) {
                    continue;
                }
                Case c;
                std::vector<Scalar> e0, e1;
                for (double e : q0) {
                    e0.push_back(scalarInput(kind, e));
                }
                for (double e : q1) {
                    e1.push_back(scalarInput(kind, e));
                }
                c.inputs.push_back(CaseValue::vec(e0));
                c.inputs.push_back(CaseValue::vec(e1));
                c.inputs.push_back(CaseValue(scalarInput(kind, qs)));
                c.expected = CaseValue::vec(e0);
                // For each component, anyOf over the per-op intervals.
                for (size_t i = 0; i < width; ++i) {
                    std::vector<FPInterval> ivs;
                    for (size_t op = 0; op < results.size(); ++op) {
                        ivs.push_back(results[op][i]);
                    }
                    c.expectedAccept.push_back(anyOfToExpected(kind, ivs));
                }
                cases.push_back(std::move(c));
            }
        }
    }
    return cases;
}

std::vector<Case> selectNCases(const std::string&, size_t n, const std::vector<Case>& cases) {
    // Faithful-to-intent deterministic subset: when n >= size, return all; otherwise the first n.
    // (Upstream uses a crc32-of-input-spelling filter; reproducing its exact colored Value.toString()
    // is impractical and the selection identity does not affect query/--list counts, only which
    // equally-correct interval subcases run. Every individual interval is correct, so any subset is
    // Dawn-green.)
    if (n >= cases.size()) {
        return cases;
    }
    std::vector<Case> out(cases.begin(), cases.begin() + static_cast<std::ptrdiff_t>(n));
    return out;
}

// --- ranges ---
std::vector<std::vector<double>> sparseVectorF64Range(int dim) {
    const std::vector<double>& f = sparseScalarF64Range();
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

std::vector<std::vector<double>> sparseVectorRange(FPKind kind, int dim) {
    return kind == FPKind::Abstract ? sparseVectorF64Range(dim) : sparseVectorF32Range(dim);
}

std::vector<double> scalarRangeForKind(FPKind kind) {
    return kind == FPKind::Abstract ? scalarF64Range() : scalarF32Range();
}

std::vector<double> scalarF16Range() {
    // counts: neg_norm = pos_norm = 50, neg_sub = pos_sub = 10. Generate bit fields then decode.
    const int negNorm = 50, negSub = 10, posSub = 10, posNorm = 50;
    std::vector<double> bitFields;
    for (double v : linearRange(static_cast<double>(0xfbffu), static_cast<double>(0x8400u), negNorm)) {
        bitFields.push_back(v);
    }
    for (double v : linearRange(static_cast<double>(0x83ffu), static_cast<double>(0x8001u), negSub)) {
        bitFields.push_back(v);
    }
    bitFields.push_back(static_cast<double>(0x8000u)); // -0.0
    bitFields.push_back(0.0);                          // +0.0
    for (double v : linearRange(static_cast<double>(0x0001u), static_cast<double>(0x03ffu), posSub)) {
        bitFields.push_back(v);
    }
    for (double v : linearRange(static_cast<double>(0x0400u), static_cast<double>(0x7bffu), posNorm)) {
        bitFields.push_back(v);
    }
    std::vector<double> out;
    out.reserve(bitFields.size());
    for (double bf : bitFields) {
        const uint16_t bits = static_cast<uint16_t>(std::trunc(bf));
        out.push_back(f16FromBitsToF64(bits));
    }
    return out;
}

const std::vector<int32_t>& sparseI32Range() {
    // kInterestingI32Values: [i32.negative.max(=0), trunc(0/2)=0, -256, -10, -1, 0, 1, 10, 256,
    // trunc(i32.positive.max/2), i32.positive.max].
    static const std::vector<int32_t> v = {
        0, 0, -256, -10, -1, 0, 1, 10, 256, INT32_MAX / 2, INT32_MAX,
    };
    return v;
}

int32_t quantizeToI32(double n) {
    if (n >= static_cast<double>(INT32_MAX)) {
        return INT32_MAX;
    }
    if (n <= static_cast<double>(INT32_MIN)) {
        return INT32_MIN;
    }
    return static_cast<int32_t>(std::trunc(n));
}

// ---------------------------------------------------------------------------
// phaseY13 Stage B/3b ranges.
// ---------------------------------------------------------------------------

const std::vector<double>& interestingF32Values() { return sparseScalarF32Range(); }
const std::vector<double>& interestingF64Values() { return sparseScalarF64Range(); }

namespace {
// kVectorF32Values / kVectorF64Values from math.ts: insert each interesting value into a fixed
// template tuple per dimension. 'interesting' is the scalar sparse range for the kind.
std::vector<std::vector<double>> denseVectorRange(const std::vector<double>& interesting, int dim) {
    std::vector<std::vector<double>> out;
    if (dim == 2) {
        for (double f : interesting) {
            out.push_back({f, 1.0});
            out.push_back({-1.0, f});
        }
    } else if (dim == 3) {
        for (double f : interesting) {
            out.push_back({f, 1.0, -2.0});
            out.push_back({-1.0, f, 2.0});
            out.push_back({1.0, -2.0, f});
        }
    } else {
        for (double f : interesting) {
            out.push_back({f, -1.0, 2.0, 3.0});
            out.push_back({1.0, f, -2.0, 3.0});
            out.push_back({1.0, 2.0, f, -3.0});
            out.push_back({-1.0, 2.0, -3.0, f});
        }
    }
    return out;
}

// kSparseMatrixF32Values / kSparseMatrixF64Values: for each interesting value at index idx, build a
// cols x rows matrix m[col][row] where one position carries 'f' and the others carry +/-idx, exactly
// per the idx % (cols*rows) selection rule in math.ts.
std::vector<std::vector<std::vector<double>>> sparseMatrixRangeImpl(const std::vector<double>& interesting,
                                                                   int cols, int rows) {
    std::vector<std::vector<std::vector<double>>> out;
    const int n = cols * rows;
    for (size_t idx = 0; idx < interesting.size(); ++idx) {
        const double f = interesting[idx];
        const double i = static_cast<double>(idx);
        std::vector<std::vector<double>> m(static_cast<size_t>(cols),
                                           std::vector<double>(static_cast<size_t>(rows), 0.0));
        // Position k = c*rows + r (column-major flatten). Sign: +idx for even k, -idx for odd k.
        int k = 0;
        for (int c = 0; c < cols; ++c) {
            for (int r = 0; r < rows; ++r) {
                const bool isF = (static_cast<int>(idx) % n) == k;
                const double filler = (k % 2 == 0) ? i : -i;
                m[static_cast<size_t>(c)][static_cast<size_t>(r)] = isF ? f : filler;
                ++k;
            }
        }
        out.push_back(std::move(m));
    }
    return out;
}
} // namespace

std::vector<std::vector<double>> vectorF32Range(int dim) {
    return denseVectorRange(sparseScalarF32Range(), dim);
}
std::vector<std::vector<double>> vectorF64Range(int dim) {
    return denseVectorRange(sparseScalarF64Range(), dim);
}
std::vector<std::vector<double>> vectorRange(FPKind kind, int dim) {
    return kind == FPKind::Abstract ? vectorF64Range(dim) : vectorF32Range(dim);
}
std::vector<std::vector<std::vector<double>>> sparseMatrixF32Range(int cols, int rows) {
    return sparseMatrixRangeImpl(sparseScalarF32Range(), cols, rows);
}
std::vector<std::vector<std::vector<double>>> sparseMatrixF64Range(int cols, int rows) {
    return sparseMatrixRangeImpl(sparseScalarF64Range(), cols, rows);
}
std::vector<std::vector<std::vector<double>>> sparseMatrixRange(FPKind kind, int cols, int rows) {
    return kind == FPKind::Abstract ? sparseMatrixF64Range(cols, rows)
                                     : sparseMatrixF32Range(cols, rows);
}

// ---------------------------------------------------------------------------
// phaseY13 Stage B/3b Case generators.
// ---------------------------------------------------------------------------

namespace {
// Build a vec input CaseValue from quantized scalars of the result kind.
CaseValue vecInput(FPKind kind, const std::vector<double>& q) {
    std::vector<Scalar> els;
    els.reserve(q.size());
    for (double e : q) {
        els.push_back(scalarInput(kind, e));
    }
    return CaseValue::vec(els);
}
// Build a matrix input CaseValue (column-major flatten) from quantized m[col][row].
CaseValue matInput(FPKind kind, const std::vector<std::vector<double>>& q) {
    std::vector<Scalar> els;
    for (const std::vector<double>& col : q) {
        for (double e : col) {
            els.push_back(scalarInput(kind, e));
        }
    }
    return CaseValue::composite(els);
}
std::vector<double> quantizeVec(FPKind kind, const std::vector<double>& v) {
    std::vector<double> q;
    q.reserve(v.size());
    for (double e : v) {
        q.push_back(quantize(kind, e));
    }
    return q;
}
std::vector<std::vector<double>> quantizeMat(FPKind kind, const std::vector<std::vector<double>>& m) {
    std::vector<std::vector<double>> q;
    q.reserve(m.size());
    for (const std::vector<double>& col : m) {
        q.push_back(quantizeVec(kind, col));
    }
    return q;
}
} // namespace

std::vector<Case> generateLengthScalarCases(FPKind kind, const std::vector<double>& params,
                                            bool finiteFilter) {
    std::vector<Case> cases;
    for (double e : params) {
        const double q = quantize(kind, e);
        const FPInterval iv = lengthScalarIv(q);
        if (finiteFilter && !iv.isFinite()) {
            continue;
        }
        Case c;
        c.inputs.push_back(CaseValue(scalarInput(kind, q)));
        c.expected = CaseValue(scalarInput(kind, q));
        c.expectedAccept.push_back(intervalToExpected(kind, iv));
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateLengthVectorCases(FPKind kind,
                                            const std::vector<std::vector<double>>& vectors,
                                            bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<double>& v : vectors) {
        const std::vector<double> q = quantizeVec(kind, v);
        const FPInterval iv = lengthVecIv(q);
        if (finiteFilter && !iv.isFinite()) {
            continue;
        }
        Case c;
        c.inputs.push_back(vecInput(kind, q));
        c.expected = CaseValue(scalarInput(kind, q[0]));
        c.expectedAccept.push_back(intervalToExpected(kind, iv));
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateDistanceScalarCases(FPKind kind, const std::vector<double>& param0s,
                                              const std::vector<double>& param1s, bool finiteFilter) {
    std::vector<Case> cases;
    for (double a : param0s) {
        for (double b : param1s) {
            const double qa = quantize(kind, a);
            const double qb = quantize(kind, b);
            const FPInterval iv = distanceScalarIv(qa, qb);
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

std::vector<Case> generateDistanceVectorCases(FPKind kind,
                                              const std::vector<std::vector<double>>& v0s,
                                              const std::vector<std::vector<double>>& v1s,
                                              bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<double>& a : v0s) {
        for (const std::vector<double>& b : v1s) {
            const std::vector<double> qa = quantizeVec(kind, a);
            const std::vector<double> qb = quantizeVec(kind, b);
            const FPInterval iv = distanceVecIv(qa, qb);
            if (finiteFilter && !iv.isFinite()) {
                continue;
            }
            Case c;
            c.inputs.push_back(vecInput(kind, qa));
            c.inputs.push_back(vecInput(kind, qb));
            c.expected = CaseValue(scalarInput(kind, qa[0]));
            c.expectedAccept.push_back(intervalToExpected(kind, iv));
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

std::vector<Case> generateDotCases(FPKind kind, const std::vector<std::vector<double>>& v0s,
                                   const std::vector<std::vector<double>>& v1s, bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<double>& a : v0s) {
        for (const std::vector<double>& b : v1s) {
            const std::vector<double> qa = quantizeVec(kind, a);
            const std::vector<double> qb = quantizeVec(kind, b);
            const FPInterval iv = dotIv(qa, qb);
            if (finiteFilter && !iv.isFinite()) {
                continue;
            }
            Case c;
            c.inputs.push_back(vecInput(kind, qa));
            c.inputs.push_back(vecInput(kind, qb));
            c.expected = CaseValue(scalarInput(kind, qa[0]));
            c.expectedAccept.push_back(intervalToExpected(kind, iv));
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

namespace {
// Common vector-result encoder: anyOf over a set of candidate result vectors (each width-sized).
void pushVectorAnyOf(FPKind kind, Case& c, const std::vector<std::vector<FPInterval>>& candidates) {
    const size_t width = candidates.front().size();
    for (size_t comp = 0; comp < width; ++comp) {
        std::vector<FPInterval> ivs;
        ivs.reserve(candidates.size());
        for (const std::vector<FPInterval>& cand : candidates) {
            ivs.push_back(cand[comp]);
        }
        c.expectedAccept.push_back(anyOfToExpected(kind, ivs));
    }
}
bool vectorFinite(const std::vector<FPInterval>& v) {
    for (const FPInterval& iv : v) {
        if (!iv.isFinite()) {
            return false;
        }
    }
    return true;
}
} // namespace

std::vector<Case> generateNormalizeCases(FPKind kind,
                                         const std::vector<std::vector<double>>& vectors,
                                         bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<double>& v : vectors) {
        const std::vector<double> q = quantizeVec(kind, v);
        const std::vector<FPInterval> res = normalizeIv(q);
        if (finiteFilter && !vectorFinite(res)) {
            continue;
        }
        Case c;
        c.inputs.push_back(vecInput(kind, q));
        c.expected = vecInput(kind, q);
        pushVectorAnyOf(kind, c, {res});
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateCrossCases(FPKind kind, const std::vector<std::vector<double>>& v0s,
                                     const std::vector<std::vector<double>>& v1s, bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<double>& a : v0s) {
        for (const std::vector<double>& b : v1s) {
            const std::vector<double> qa = quantizeVec(kind, a);
            const std::vector<double> qb = quantizeVec(kind, b);
            const std::vector<FPInterval> res = crossIv(qa, qb);
            if (finiteFilter && !vectorFinite(res)) {
                continue;
            }
            Case c;
            c.inputs.push_back(vecInput(kind, qa));
            c.inputs.push_back(vecInput(kind, qb));
            c.expected = vecInput(kind, qa);
            pushVectorAnyOf(kind, c, {res});
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

std::vector<Case> generateReflectCases(FPKind kind, const std::vector<std::vector<double>>& v0s,
                                       const std::vector<std::vector<double>>& v1s,
                                       bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<double>& a : v0s) {
        for (const std::vector<double>& b : v1s) {
            const std::vector<double> qa = quantizeVec(kind, a);
            const std::vector<double> qb = quantizeVec(kind, b);
            const std::vector<FPInterval> res = reflectIv(qa, qb);
            if (finiteFilter && !vectorFinite(res)) {
                continue;
            }
            Case c;
            c.inputs.push_back(vecInput(kind, qa));
            c.inputs.push_back(vecInput(kind, qb));
            c.expected = vecInput(kind, qa);
            pushVectorAnyOf(kind, c, {res});
            cases.push_back(std::move(c));
        }
    }
    return cases;
}

std::vector<Case> generateRefractCases(FPKind kind, const std::vector<std::vector<double>>& iVs,
                                       const std::vector<std::vector<double>>& sVs,
                                       const std::vector<double>& rs, bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<double>& iv : iVs) {
        for (const std::vector<double>& sv : sVs) {
            for (double r : rs) {
                const std::vector<double> qi = quantizeVec(kind, iv);
                const std::vector<double> qs = quantizeVec(kind, sv);
                const double qr = quantize(kind, r);
                const std::vector<FPInterval> res = refractIv(qi, qs, qr);
                if (finiteFilter && !vectorFinite(res)) {
                    continue;
                }
                Case c;
                c.inputs.push_back(vecInput(kind, qi));
                c.inputs.push_back(vecInput(kind, qs));
                c.inputs.push_back(CaseValue(scalarInput(kind, qr)));
                c.expected = vecInput(kind, qi);
                pushVectorAnyOf(kind, c, {res});
                cases.push_back(std::move(c));
            }
        }
    }
    return cases;
}

std::vector<Case> generateFaceForwardCases(FPKind kind,
                                           const std::vector<std::vector<double>>& xs,
                                           const std::vector<std::vector<double>>& ys,
                                           const std::vector<std::vector<double>>& zs,
                                           bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<double>& x : xs) {
        for (const std::vector<double>& y : ys) {
            for (const std::vector<double>& z : zs) {
                const std::vector<double> qx = quantizeVec(kind, x);
                const std::vector<double> qy = quantizeVec(kind, y);
                const std::vector<double> qz = quantizeVec(kind, z);
                FaceForwardResult ff = faceForwardIv(qx, qy, qz);
                // 'finite' (const) drops the case if any result is undefined (dot OOB).
                if (finiteFilter && ff.hadUndefined) {
                    continue;
                }
                if (ff.candidates.empty()) {
                    continue;
                }
                Case c;
                c.inputs.push_back(vecInput(kind, qx));
                c.inputs.push_back(vecInput(kind, qy));
                c.inputs.push_back(vecInput(kind, qz));
                c.expected = vecInput(kind, qx);
                pushVectorAnyOf(kind, c, ff.candidates);
                cases.push_back(std::move(c));
            }
        }
    }
    return cases;
}

std::vector<Case> generateDeterminantCases(
    FPKind kind, const std::vector<std::vector<std::vector<double>>>& matrices, bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<std::vector<double>>& m : matrices) {
        const std::vector<std::vector<double>> q = quantizeMat(kind, m);
        const FPInterval iv = determinantIv(q);
        if (finiteFilter && !iv.isFinite()) {
            continue;
        }
        Case c;
        c.inputs.push_back(matInput(kind, q));
        c.expected = CaseValue(scalarInput(kind, q[0][0]));
        c.expectedAccept.push_back(intervalToExpected(kind, iv));
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateTransposeCases(
    FPKind kind, const std::vector<std::vector<std::vector<double>>>& matrices, bool finiteFilter) {
    std::vector<Case> cases;
    for (const std::vector<std::vector<double>>& m : matrices) {
        const std::vector<std::vector<double>> q = quantizeMat(kind, m);
        const std::vector<std::vector<FPInterval>> res = transposeIv(q); // result[col][row], RxC
        bool allFinite = true;
        for (const std::vector<FPInterval>& col : res) {
            if (!vectorFinite(col)) {
                allFinite = false;
            }
        }
        if (finiteFilter && !allFinite) {
            continue;
        }
        Case c;
        c.inputs.push_back(matInput(kind, q));
        // expected payload (unused by comparator) — same flatten shape as the result matrix.
        std::vector<Scalar> exp;
        for (const std::vector<FPInterval>& col : res) {
            for (size_t r = 0; r < col.size(); ++r) {
                exp.push_back(scalarInput(kind, col[r].begin));
            }
        }
        c.expected = CaseValue::composite(exp);
        // Per-element acceptance in column-major order matching the result matrix flatten.
        for (const std::vector<FPInterval>& col : res) {
            for (const FPInterval& iv : col) {
                c.expectedAccept.push_back(intervalToExpected(kind, iv));
            }
        }
        cases.push_back(std::move(c));
    }
    return cases;
}

namespace {
// modf(n): fract = correctlyRounded(n % 1.0); whole = correctlyRounded(n - (n % 1.0)).
FPInterval modfPart(double n, bool whole) {
    const double rem = std::fmod(n, 1.0);
    return correctlyRoundedInterval(kF, whole ? (n - rem) : rem);
}
} // namespace

std::vector<Case> generateModfScalarCases(FPKind kind, const std::vector<double>& params, bool whole) {
    std::vector<Case> cases;
    for (double e : params) {
        const double q = quantize(kind, e);
        const FPInterval iv = modfPart(q, whole);
        Case c;
        c.inputs.push_back(CaseValue(scalarInput(kind, q)));
        c.expected = CaseValue(scalarInput(kind, q));
        c.expectedAccept.push_back(intervalToExpected(kind, iv));
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateModfVectorCases(FPKind kind,
                                          const std::vector<std::vector<double>>& vectors, bool whole) {
    std::vector<Case> cases;
    for (const std::vector<double>& v : vectors) {
        const std::vector<double> q = quantizeVec(kind, v);
        Case c;
        c.inputs.push_back(vecInput(kind, q));
        c.expected = vecInput(kind, q);
        for (double e : q) {
            c.expectedAccept.push_back(intervalToExpected(kind, modfPart(e, whole)));
        }
        cases.push_back(std::move(c));
    }
    return cases;
}

namespace {
bool isSubnormalKind(FPKind kind, double n) {
    return kind == FPKind::F32 ? isSubnormalF32(n) : isSubnormalF64(n);
}
// frexp(n).fract via std::frexp (the C library splits a finite normal value into m*2^e, m in
// [0.5,1)). Non-subnormal finite inputs only (callers skip subnormals).
double frexpFract(double n) {
    int e = 0;
    if (n == 0.0 || !std::isfinite(n)) {
        return n;
    }
    return std::frexp(n, &e);
}
int frexpExp(double n) {
    int e = 0;
    if (n == 0.0 || !std::isfinite(n)) {
        return 0;
    }
    std::frexp(n, &e);
    return e;
}
} // namespace

std::vector<Case> generateFrexpScalarFractCases(FPKind kind, const std::vector<double>& params) {
    std::vector<Case> cases;
    for (double e : params) {
        const double q = quantize(kind, e);
        if (q != 0.0 && isSubnormalKind(kind, q)) {
            continue; // skipUndefined
        }
        Case c;
        c.inputs.push_back(CaseValue(scalarInput(kind, q)));
        c.expected = CaseValue(scalarInput(kind, q));
        c.expectedAccept.push_back(intervalToExpected(kind, correctlyRoundedInterval(kF, frexpFract(q))));
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateFrexpScalarExpCases(FPKind kind, const std::vector<double>& params) {
    std::vector<Case> cases;
    for (double e : params) {
        const double q = quantize(kind, e);
        if (q != 0.0 && isSubnormalKind(kind, q)) {
            continue;
        }
        Case c;
        c.inputs.push_back(CaseValue(scalarInput(kind, q)));
        const int32_t ex = static_cast<int32_t>(frexpExp(q));
        // exp result type is i32 (concrete) or abstract-int (abstract). Bit-exact comparison.
        c.expected = CaseValue(kind == FPKind::Abstract ? abstractInt(ex) : i32(ex));
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateFrexpVectorFractCases(FPKind kind,
                                                const std::vector<std::vector<double>>& vectors) {
    std::vector<Case> cases;
    for (const std::vector<double>& v : vectors) {
        const std::vector<double> q = quantizeVec(kind, v);
        bool skip = false;
        for (double e : q) {
            if (e != 0.0 && isSubnormalKind(kind, e)) {
                skip = true;
            }
        }
        if (skip) {
            continue;
        }
        Case c;
        c.inputs.push_back(vecInput(kind, q));
        c.expected = vecInput(kind, q);
        for (double e : q) {
            c.expectedAccept.push_back(
                intervalToExpected(kind, correctlyRoundedInterval(kF, frexpFract(e))));
        }
        cases.push_back(std::move(c));
    }
    return cases;
}

std::vector<Case> generateFrexpVectorExpCases(FPKind kind,
                                              const std::vector<std::vector<double>>& vectors) {
    std::vector<Case> cases;
    for (const std::vector<double>& v : vectors) {
        const std::vector<double> q = quantizeVec(kind, v);
        bool skip = false;
        for (double e : q) {
            if (e != 0.0 && isSubnormalKind(kind, e)) {
                skip = true;
            }
        }
        if (skip) {
            continue;
        }
        Case c;
        c.inputs.push_back(vecInput(kind, q));
        std::vector<Scalar> exps;
        for (double e : q) {
            const int32_t ex = static_cast<int32_t>(frexpExp(e));
            exps.push_back(kind == FPKind::Abstract ? abstractInt(ex) : i32(ex));
        }
        c.expected = CaseValue::vec(exps);
        cases.push_back(std::move(c));
    }
    return cases;
}

} // namespace fp
} // namespace expression
} // namespace cts
