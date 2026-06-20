// Ported from gpuweb/cts src/webgpu/util/math.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Faithful reproductions of the upstream full{I32,U32,I64}Range biased-range value sets used by the
// unary expression ports. JS numbers are IEEE-754 doubles, so lerp/biasedRange are computed in
// 'double' here to match exactly; fullI64Range uses a small 128-bit helper for the bigint products.

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

namespace cts {
namespace expression {
namespace unary_ranges {

// lerp(a, b, t) for finite a, b (math.ts).
inline double lerp(double a, double b, double t) {
    if (!std::isfinite(a) || !std::isfinite(b)) {
        return std::nan("");
    }
    if ((a <= 0.0 && b >= 0.0) || (a >= 0.0 && b <= 0.0)) {
        return t * b + (1.0 - t) * a;
    }
    if (t == 1.0) {
        return b;
    }
    const double x = a + t * (b - a);
    const bool cond = (t > 1.0) == (b > a);
    return cond ? std::fmax(b, x) : std::fmin(b, x);
}

// biasedRange(a, b, num_steps): quadratic bias towards a (math.ts, c == 2).
inline std::vector<double> biasedRange(double a, double b, int num_steps) {
    std::vector<double> out;
    if (num_steps <= 0) {
        return out;
    }
    if (num_steps == 1) {
        out.push_back(a);
        return out;
    }
    for (int i = 0; i < num_steps; ++i) {
        const double frac = static_cast<double>(i) / static_cast<double>(num_steps - 1);
        out.push_back(lerp(a, b, std::pow(frac, 2.0)));
    }
    return out;
}

// fullI32Range({negative: 50, positive: 50}) (math.ts). Values are Math.trunc'd.
inline std::vector<int32_t> fullI32Range() {
    std::vector<int32_t> out;
    for (double v : biasedRange(-2147483648.0, -1.0, 50)) {
        out.push_back(static_cast<int32_t>(std::trunc(v)));
    }
    out.push_back(0);
    for (double v : biasedRange(1.0, 2147483647.0, 50)) {
        out.push_back(static_cast<int32_t>(std::trunc(v)));
    }
    return out;
}

// fullU32Range(50) (math.ts). [0, ...biasedRange(1, u32.max, 50)].map(Math.trunc).
inline std::vector<uint32_t> fullU32Range() {
    std::vector<uint32_t> out;
    out.push_back(0u);
    for (double v : biasedRange(1.0, 4294967295.0, 50)) {
        out.push_back(static_cast<uint32_t>(std::trunc(v)));
    }
    return out;
}

// ---- 128-bit signed helper for fullI64Range's bigint lerp products ----------------------------

struct I128 {
    // value = (hi << 64) | lo, two's complement.
    uint64_t lo;
    int64_t hi;
};

inline I128 i128FromI64(int64_t v) {
    return I128{static_cast<uint64_t>(v), v < 0 ? -1 : 0};
}

inline I128 i128Mul(int64_t a, int64_t b) {
    // Signed 64x64 -> 128.
    const bool neg = (a < 0) != (b < 0);
    uint64_t ua = static_cast<uint64_t>(a < 0 ? -(static_cast<unsigned long long>(a)) : a);
    uint64_t ub = static_cast<uint64_t>(b < 0 ? -(static_cast<unsigned long long>(b)) : b);
    // Unsigned 64x64 -> 128.
    const uint64_t a0 = ua & 0xFFFFFFFFull, a1 = ua >> 32;
    const uint64_t b0 = ub & 0xFFFFFFFFull, b1 = ub >> 32;
    const uint64_t p00 = a0 * b0;
    const uint64_t p01 = a0 * b1;
    const uint64_t p10 = a1 * b0;
    const uint64_t p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);
    uint64_t lo = (p00 & 0xFFFFFFFFull) | (mid << 32);
    uint64_t hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    if (neg) {
        // Negate the 128-bit magnitude.
        lo = ~lo + 1;
        hi = ~hi + (lo == 0 ? 1 : 0);
    }
    return I128{lo, static_cast<int64_t>(hi)};
}

// Divide a 128-bit signed value by a positive int64 divisor, truncating towards zero (matches
// bigint '/'). Only used with non-negative magnitudes path handled by sign tracking.
inline int64_t i128DivToI64(I128 x, int64_t divisor) {
    // Determine sign.
    const bool neg = x.hi < 0;
    // Magnitude.
    uint64_t lo = x.lo;
    uint64_t hi = static_cast<uint64_t>(x.hi);
    if (neg) {
        lo = ~lo + 1;
        hi = ~hi + (lo == 0 ? 1 : 0);
    }
    const uint64_t d = static_cast<uint64_t>(divisor);
    // 128 / 64 -> 64 (long division on 128-bit magnitude; result fits in 64 for our ranges).
    uint64_t rem = 0;
    uint64_t quot = 0;
    for (int bit = 127; bit >= 0; --bit) {
        rem <<= 1;
        const uint64_t b = bit >= 64 ? ((hi >> (bit - 64)) & 1ull) : ((lo >> bit) & 1ull);
        rem |= b;
        if (rem >= d) {
            rem -= d;
            if (bit < 64) {
                quot |= (1ull << bit);
            }
            // bits >= 64 of quotient are zero for our ranges.
        }
    }
    int64_t r = static_cast<int64_t>(quot);
    return neg ? -r : r;
}

// lerpBigInt(a, b, idx, steps) (math.ts).
inline int64_t lerpBigInt(int64_t a, int64_t b, int idx, int steps) {
    if (steps == 1) {
        return a;
    }
    if (idx == 0) {
        return a;
    }
    if (idx == steps - 1) {
        return b;
    }
    const int64_t denom = static_cast<int64_t>(steps) - 1;
    if ((a <= 0 && b >= 0) || (a >= 0 && b <= 0)) {
        // (b*idx)/(steps-1) + (a - (a*idx)/(steps-1))
        const int64_t t1 = i128DivToI64(i128Mul(b, static_cast<int64_t>(idx)), denom);
        const int64_t t2 = i128DivToI64(i128Mul(a, static_cast<int64_t>(idx)), denom);
        return t1 + (a - t2);
    }
    // x = a + (b*idx)/(steps-1) - (a*idx)/(steps-1)
    const int64_t t1 = i128DivToI64(i128Mul(b, static_cast<int64_t>(idx)), denom);
    const int64_t t2 = i128DivToI64(i128Mul(a, static_cast<int64_t>(idx)), denom);
    const int64_t x = a + t1 - t2;
    if (!(b > a)) {
        return x > b ? x : b;  // max(b, x)
    }
    return x < b ? x : b;  // min(b, x)
}

// biasedRangeBigInt(a, b, num_steps) (math.ts). scaling == 1000, c == 2.
inline std::vector<int64_t> biasedRangeBigInt(int64_t a, int64_t b, int num_steps) {
    std::vector<int64_t> out;
    if (num_steps <= 0) {
        return out;
    }
    if (num_steps == 1) {
        out.push_back(a);
        return out;
    }
    const int scaling = 1000;
    const int scaled_num_steps = num_steps * scaling;
    for (int i = 0; i < num_steps; ++i) {
        const double frac = static_cast<double>(i) / static_cast<double>(num_steps - 1);
        const double biased_i = std::pow(frac, 2.0);
        const int scaled_i = static_cast<int>(std::trunc((scaled_num_steps - 1) * biased_i));
        out.push_back(lerpBigInt(a, b, scaled_i, scaled_num_steps));
    }
    return out;
}

// fullI64Range({negative: 50, positive: 50}) (math.ts).
inline std::vector<int64_t> fullI64Range() {
    std::vector<int64_t> out;
    for (int64_t v : biasedRangeBigInt(INT64_MIN, -1, 50)) {
        out.push_back(v);
    }
    out.push_back(0);
    for (int64_t v : biasedRangeBigInt(1, INT64_MAX, 50)) {
        out.push_back(v);
    }
    return out;
}

}  // namespace unary_ranges
}  // namespace expression
}  // namespace cts
