// Ported from gpuweb/cts util/math.ts and util/floating_point.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Shared helpers for the float pack/unpack builtin execution tests
// (pack4x8snorm/unorm, pack2x16snorm/unorm/float and their unpack counterparts).
// Reproduces the upstream input ranges (vectorF32Range, fullU32Range), the
// quantizeToF32 / f16 conversions, and the ULP / f16 acceptance intervals so the
// expected values and acceptance match upstream exactly, without deserializing the
// generated .cache.ts data.

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

namespace cts {
namespace packunpack {

// --- f32 / f16 bit helpers -------------------------------------------------

inline float f32FromBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}
inline uint32_t f32ToBits(float f) {
    uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}

// quantizeToF32 = Math.fround: round a double to the nearest f32 value.
inline double quantizeToF32(double n) {
    return static_cast<double>(static_cast<float>(n));
}

// --- f32 constants (built from bit patterns, like reinterpretU32AsF32) -----

namespace kF32 {
inline double negMin() { return static_cast<double>(f32FromBits(0xFF7FFFFFu)); }            // -3.4028235e38
inline double negMax() { return static_cast<double>(f32FromBits(0x80800000u)); }            // largest negative normal
inline double negSubMin() { return static_cast<double>(f32FromBits(0x807FFFFFu)); }
inline double negSubMax() { return static_cast<double>(f32FromBits(0x80000001u)); }
inline double posSubMin() { return static_cast<double>(f32FromBits(0x00000001u)); }
inline double posSubMax() { return static_cast<double>(f32FromBits(0x007FFFFFu)); }
inline double posMin() { return static_cast<double>(f32FromBits(0x00800000u)); }
inline double posMax() { return static_cast<double>(f32FromBits(0x7F7FFFFFu)); }
} // namespace kF32

// kInterestingF32Values (util/math.ts).
inline const std::vector<double>& kInterestingF32Values() {
    static const std::vector<double> v = {
        kF32::negMin(),
        -10.0,
        -1.0,
        -0.125,
        kF32::negMax(),
        kF32::negSubMin(),
        kF32::negSubMax(),
        -0.0,
        0.0,
        kF32::posSubMin(),
        kF32::posSubMax(),
        kF32::posMin(),
        0.125,
        1.0,
        10.0,
        kF32::posMax(),
    };
    return v;
}

// vectorF32Range(dim) (util/math.ts kVectorF32Values). Inserts each interesting
// value into each position of the vector with fixed fillers in the others.
inline std::vector<std::vector<double>> vectorF32Range(int dim) {
    std::vector<std::vector<double>> out;
    const std::vector<double>& f = kInterestingF32Values();
    if (dim == 2) {
        for (double x : f) {
            out.push_back({x, 1.0});
            out.push_back({-1.0, x});
        }
    } else if (dim == 3) {
        for (double x : f) {
            out.push_back({x, 1.0, -2.0});
            out.push_back({-1.0, x, 2.0});
            out.push_back({1.0, -2.0, x});
        }
    } else { // dim == 4
        for (double x : f) {
            out.push_back({x, -1.0, 2.0, 3.0});
            out.push_back({1.0, x, -2.0, 3.0});
            out.push_back({1.0, 2.0, x, -3.0});
            out.push_back({-1.0, 2.0, -3.0, x});
        }
    }
    return out;
}

// fullU32Range(count=50) = [0, ...biasedRange(1, u32.max, 50)].map(trunc) (util/math.ts).
// biasedRange uses lerp(1, max, (i/(steps-1))^2); for this positive range it reduces to
// 1 + t^2 * (max - 1), truncated.
inline std::vector<uint32_t> fullU32Range(int count = 50) {
    std::vector<uint32_t> out;
    out.push_back(0u);
    const double a = 1.0;
    const double b = 4294967295.0; // u32.max
    for (int i = 0; i < count; ++i) {
        double t = (count == 1) ? 0.0 : (static_cast<double>(i) / static_cast<double>(count - 1));
        t = t * t; // biasedRange exponent c = 2
        // lerp(a, b, t) with a,b > 0, t in [0,1]: x = a + t*(b-a), min(b, x).
        double x = a + t * (b - a);
        if (x > b) {
            x = b;
        }
        out.push_back(static_cast<uint32_t>(std::trunc(x)));
    }
    return out;
}

// linearRange(a, b, num_steps) (util/math.ts), operating on doubles.
inline std::vector<double> linearRange(double a, double b, int numSteps) {
    std::vector<double> out;
    if (numSteps <= 0) {
        return out;
    }
    if (numSteps == 1) {
        out.push_back(a);
        return out;
    }
    for (int i = 0; i < numSteps; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(numSteps - 1);
        // lerp(a, b, t): for the bit-pattern ranges here a and b are same-sign integers, but
        // the spanning ranges (subnormal min..max) may straddle; replicate lerp's logic.
        double v;
        if ((a <= 0.0 && b >= 0.0) || (a >= 0.0 && b <= 0.0)) {
            v = t * b + (1.0 - t) * a;
        } else {
            const double x = a + t * (b - a);
            v = (t > 1.0) == (b > a) ? std::max(b, x) : std::min(b, x);
        }
        out.push_back(v);
    }
    return out;
}

// scalarF32Range() with default counts {neg_norm:50, neg_sub:10, pos_sub:10, pos_norm:50}.
// Bit patterns are generated, truncated, then reinterpreted as f32 (util/math.ts).
inline std::vector<double> scalarF32Range() {
    const int neg_norm = 50, neg_sub = 10, pos_sub = 10, pos_norm = 50;
    std::vector<double> bits;
    auto append = [&](const std::vector<double>& r) {
        for (double v : r) {
            bits.push_back(v);
        }
    };
    // f32 bit-pattern endpoints.
    append(linearRange(0xFF7FFFFFu, 0x80800000u, neg_norm));        // negative.min .. negative.max
    append(linearRange(0x807FFFFFu, 0x80000001u, neg_sub));         // negative subnormal min .. max
    bits.push_back(0x80000000u);                                    // -0.0
    bits.push_back(0.0);                                            // +0.0
    append(linearRange(0x00000001u, 0x007FFFFFu, pos_sub));         // positive subnormal min .. max
    // positive normals, with two special values appended then the whole positive-normal set sorted.
    std::vector<double> pos;
    {
        const std::vector<double> special_pos = {
            static_cast<double>(0x4EFFFFFFu), static_cast<double>(0x4F7FFFFFu)};
        std::vector<double> lin =
            linearRange(0x00800000u, 0x7F7FFFFFu, pos_norm - static_cast<int>(special_pos.size()));
        for (double v : lin) {
            pos.push_back(v);
        }
        for (double v : special_pos) {
            pos.push_back(v);
        }
        std::sort(pos.begin(), pos.end());
    }
    append(pos);

    std::vector<double> out;
    out.reserve(bits.size());
    for (double b : bits) {
        const uint32_t u = static_cast<uint32_t>(std::trunc(b));
        out.push_back(static_cast<double>(f32FromBits(u)));
    }
    return out;
}

// --- ULP acceptance interval (FP.f32) --------------------------------------

inline double flushSubnormalF32(double n) {
    if (n == 0.0) {
        return n;
    }
    const double posSubMin = kF32::posSubMin();
    const double posSubMax = kF32::posSubMax();
    if (n > 0.0 && n >= posSubMin && n <= posSubMax) {
        return 0.0;
    }
    if (n < 0.0 && n <= -posSubMin && n >= -posSubMax) {
        return 0.0;
    }
    return n;
}

// nextAfter in f32 space (no-flush) toward +/- inf.
inline double nextAfterF32(double n, bool positive) {
    float f = static_cast<float>(n);
    float r = std::nextafterf(f, positive ? std::numeric_limits<float>::infinity()
                                          : -std::numeric_limits<float>::infinity());
    return static_cast<double>(r);
}

// oneULPF32(target, 'flush') (util/math.ts).
inline double oneULPF32(double target) {
    target = flushSubnormalF32(target);
    const double posMax = kF32::posMax();
    const double negMin = kF32::negMin();
    // kValue.f32.max_ulp == oneULP(posMax) == 2^104.
    const double maxUlp = std::ldexp(1.0, 104);
    if (std::isinf(target) || target >= posMax || target <= negMin) {
        return maxUlp;
    }
    const double before = nextAfterF32(target, false);
    const double after = nextAfterF32(target, true);
    const double converted = quantizeToF32(target);
    if (converted == target) {
        return std::min(target - before, after - target);
    }
    return after - before;
}

struct Interval {
    double lo;
    double hi;
};

// ulpInterval(n, numULP) for f32 (FP.f32). The ULP op flushes the endpoints, then
// roundAndFlushScalarToInterval adds the flushed input value into the span: since the
// input 'n' here is always an exact small value the span is dominated by the ULP bounds.
inline Interval ulpIntervalF32(double n, double numULP) {
    numULP = std::abs(numULP);
    const double ulp = oneULPF32(n);
    const double begin = n - numULP * ulp;
    const double end = n + numULP * ulp;
    const double lo = std::min(begin, flushSubnormalF32(begin));
    const double hi = std::max(end, flushSubnormalF32(end));
    // Also span the (flushed) input value, matching roundAndFlushScalarToInterval over n.
    const double nLo = std::min(n, flushSubnormalF32(n));
    const double nHi = std::max(n, flushSubnormalF32(n));
    return Interval{std::min(lo, nLo), std::max(hi, nHi)};
}

// --- f16 conversion --------------------------------------------------------

inline bool isSubnormalF32Value(double n) {
    if (n == 0.0 || !std::isfinite(n)) {
        return false;
    }
    const double a = std::abs(n);
    return a >= kF32::posSubMin() && a <= kF32::posSubMax();
}

inline bool isFiniteF16Value(double n) {
    // f16.positive.max = 65504, anything with magnitude <= that (after rounding) is finite.
    return std::isfinite(n) && std::abs(n) <= 65504.0;
}

inline double f16BitsToDouble(uint16_t bits) {
    const uint32_t sign = (bits >> 15) & 0x1u;
    const uint32_t exp = (bits >> 10) & 0x1Fu;
    const uint32_t mant = bits & 0x3FFu;
    double value;
    if (exp == 0) {
        value = static_cast<double>(mant) * (1.0 / 16777216.0); // mant * 2^-24
    } else if (exp == 0x1F) {
        value = (mant == 0) ? std::numeric_limits<double>::infinity()
                            : std::numeric_limits<double>::quiet_NaN();
    } else {
        value = (1.0 + static_cast<double>(mant) / 1024.0) *
                std::ldexp(1.0, static_cast<int>(exp) - 15);
    }
    return sign ? -value : value;
}

inline bool isSubnormalF16Bits(uint16_t bits) {
    return ((bits >> 10) & 0x1Fu) == 0 && (bits & 0x3FFu) != 0;
}

// Round a finite double to the nearest f16, ties-to-even, returning the u16 bit pattern.
// Assumes |n| <= f16.max (finite); does not produce infinities.
inline uint16_t doubleToF16Bits(double n) {
    const uint32_t sign = std::signbit(n) ? 0x8000u : 0u;
    double a = std::abs(n);
    if (a == 0.0) {
        return static_cast<uint16_t>(sign);
    }
    // Decompose into mantissa and exponent: a = m * 2^e, m in [1,2).
    int e;
    double m = std::frexp(a, &e); // a = m * 2^e, m in [0.5, 1)
    // Convert to [1,2): m2 = m*2, e2 = e-1.
    double m2 = m * 2.0;
    int e2 = e - 1;
    // f16 normal exponent range: e2 in [-14, 15]. Subnormals: e2 < -14.
    // Build value scaled so that the f16 significand is an integer in [0, 2048).
    // For normals, significand = round(m2 * 1024), exponent field = e2 + 15.
    if (e2 < -14) {
        // Subnormal: value = round(a * 2^24) gives the 10-bit-or-less mantissa with implicit 2^-24 scale.
        double scaled = a * 16777216.0; // 2^24
        double r = std::nearbyint(scaled); // round-to-nearest-even (default FP mode)
        uint32_t mant = static_cast<uint32_t>(r);
        if (mant >= 1024u) {
            // Rounded up into the smallest normal.
            return static_cast<uint16_t>(sign | (1u << 10));
        }
        return static_cast<uint16_t>(sign | mant);
    }
    // Normal: significand integer = round(m2 * 1024) in [1024, 2048].
    double sigF = m2 * 1024.0;
    double r = std::nearbyint(sigF);
    uint32_t sig = static_cast<uint32_t>(r);
    if (sig == 2048u) {
        // Carried into next exponent.
        sig = 1024u;
        ++e2;
    }
    if (e2 > 15) {
        // Overflow to inf (caller should have filtered, but be safe).
        return static_cast<uint16_t>(sign | (0x1Fu << 10));
    }
    const uint32_t expField = static_cast<uint32_t>(e2 + 15);
    const uint32_t mant = sig & 0x3FFu; // drop implicit bit
    return static_cast<uint16_t>(sign | (expField << 10) | mant);
}

// correctlyRoundedF16(n) for finite n with |n| <= f16.max: returns the set of u16 bit
// patterns that n may correctly round to. If n is exactly representable, one pattern;
// otherwise the two bracketing f16 values. (util/math.ts correctlyRoundedF16.)
inline std::vector<uint16_t> correctlyRoundedF16Bits(double n) {
    // Round-to-nearest-even gives one candidate; determine whether n is exactly that value.
    const uint16_t nearestBits = doubleToF16Bits(n);
    const double nearest = f16BitsToDouble(nearestBits);
    if (nearest == n) {
        return {nearestBits};
    }
    // n is between two f16 values. The other neighbor is the f16 adjacent to 'nearest' on
    // the opposite side of n. Use f16 bit-pattern adjacency (no-flush).
    const bool wantLarger = (nearest < n); // need the f16 just above 'nearest'
    uint16_t otherBits;
    const bool negative = (nearestBits & 0x8000u) != 0;
    // Moving toward +inf increases the bit pattern for non-negative values and decreases it
    // (in magnitude-of-low-bits) for negative values.
    if (!negative) {
        otherBits = wantLarger ? static_cast<uint16_t>(nearestBits + 1)
                               : static_cast<uint16_t>(nearestBits - 1);
    } else {
        otherBits = wantLarger ? static_cast<uint16_t>(nearestBits - 1)
                               : static_cast<uint16_t>(nearestBits + 1);
    }
    return {nearestBits, otherBits};
}

} // namespace packunpack
} // namespace cts
