// Shared helpers for the constructor expression ports (zero_value / non_zero).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#pragma once

#include <cstdint>
#include <cstring>
#include <string>

#include "webgpu/shader/execution/expression/expression.h"

namespace cts {
namespace expression {
namespace constructor {

// Maps a scalar type name to its ScalarKind.
inline ScalarKind scalarKind(const std::string& name) {
    if (name == "bool") {
        return ScalarKind::Bool;
    }
    if (name == "i32") {
        return ScalarKind::I32;
    }
    if (name == "u32") {
        return ScalarKind::U32;
    }
    if (name == "f32") {
        return ScalarKind::F32;
    }
    if (name == "f16") {
        return ScalarKind::F16;
    }
    return ScalarKind::U32;
}

// f32 bit pattern of an exact small integer value.
inline uint32_t f32BitsOfInt(int v) {
    float f = static_cast<float>(v);
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

// f16 (binary16) bit pattern of a finite double (round-to-nearest-even). Sufficient for the small
// exact-integer constructor element values.
inline uint16_t f16BitsOfDouble(double value) {
    if (value == 0.0) {
        return 0x0000u;
    }
    const uint32_t sign = value < 0.0 ? 0x8000u : 0x0000u;
    double mag = value < 0.0 ? -value : value;
    int e = 0;
    while (mag >= 2.0) {
        mag /= 2.0;
        ++e;
    }
    while (mag < 1.0) {
        mag *= 2.0;
        --e;
    }
    if (e < -14) {
        return static_cast<uint16_t>(sign);
    }
    if (e > 15) {
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    const double scaled = (mag - 1.0) * 1024.0;
    uint32_t mant = static_cast<uint32_t>(scaled);
    const double frac = scaled - static_cast<double>(mant);
    if (frac > 0.5 || (frac == 0.5 && (mant & 1u) != 0)) {
        ++mant;
        if (mant == 1024u) {
            mant = 0;
            ++e;
        }
    }
    const uint32_t biasedExp = static_cast<uint32_t>(e + 15);
    return static_cast<uint16_t>(sign | (biasedExp << 10) | (mant & 0x3FFu));
}

// A scalar of the given concrete kind holding the exact integer value 'v'.
inline Scalar scalarOfInt(ScalarKind kind, int v) {
    switch (kind) {
        case ScalarKind::Bool:
            return boolean(v != 0);
        case ScalarKind::I32:
            return i32(v);
        case ScalarKind::U32:
            return u32(static_cast<uint32_t>(v));
        case ScalarKind::F32:
            return f32Bits(f32BitsOfInt(v));
        case ScalarKind::F16:
            return f16Bits(f16BitsOfDouble(static_cast<double>(v)));
        default:
            return u32(static_cast<uint32_t>(v));
    }
}

}  // namespace constructor
}  // namespace expression
}  // namespace cts
