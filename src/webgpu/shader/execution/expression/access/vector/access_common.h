// Ported from gpuweb/cts src/webgpu/shader/execution/expression/access/vector/*.spec.ts helpers
// @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Shared element/value-construction helpers for the vector access (index / components) ports.
// Mirrors upstream Type[...] and elementType.create(...) for the i32/u32/f32/f16/bool concrete
// element types and the abstract-int/abstract-float types, plus the f16 round-to-nearest-even
// encoder used to store small exact-integer element values.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include "cts/gpu.h"
#include "webgpu/shader/execution/expression/expression.h"

namespace cts {
namespace expression {
namespace access {

// Maps an element/index-type param string to its ScalarKind.
inline ScalarKind elementKind(const std::string& name) {
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
    if (name == "bool") {
        return ScalarKind::Bool;
    }
    if (name == "abstract-int") {
        return ScalarKind::AbstractInt;
    }
    if (name == "abstract-float") {
        return ScalarKind::AbstractFloat;
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

// f16 (binary16) bit pattern of a finite double using round-to-nearest-even. Sufficient for the
// small exact-integer element values used by these tests (0, 1, 10, 20, 30, 40).
inline uint16_t f16BitsOfDouble(double value) {
    if (value == 0.0) {
        // Preserve sign of zero (these tests only use +0).
        return std::signbit(value) ? 0x8000u : 0x0000u;
    }
    const uint32_t sign = value < 0.0 ? 0x8000u : 0x0000u;
    double mag = value < 0.0 ? -value : value;

    // Find the binary exponent e such that 1 <= mag / 2^e < 2.
    int e = 0;
    while (mag >= 2.0) {
        mag /= 2.0;
        ++e;
    }
    while (mag < 1.0) {
        mag *= 2.0;
        --e;
    }
    // Now mag in [1,2), unbiased exponent e.
    if (e < -14) {
        // Subnormal / underflow: not used by these tests; flush to signed zero.
        return static_cast<uint16_t>(sign);
    }
    if (e > 15) {
        // Overflow to infinity: not used by these tests.
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    // Mantissa: round (1.m)*2^10 to nearest even.
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

// Builds a concrete-element scalar for integer value 'v' of the given kind (upstream
// elementType.create(v) for the concrete element types).
inline Scalar makeConcrete(ScalarKind kind, int v) {
    switch (kind) {
        case ScalarKind::I32:
            return i32(v);
        case ScalarKind::U32:
            return u32(static_cast<uint32_t>(v));
        case ScalarKind::F32:
            return f32Bits(f32BitsOfInt(v));
        case ScalarKind::F16:
            return f16Bits(f16BitsOfDouble(static_cast<double>(v)));
        case ScalarKind::Bool:
            return boolean(v != 0);
        default:
            return u32(static_cast<uint32_t>(v));
    }
}

// f32 scalar of an exact small integer (the abstract-variant expected result).
inline Scalar f32FromInt(int v) {
    return f32Bits(f32BitsOfInt(v));
}

// Builds an abstract-element scalar holding (scale * 0x100000000), i.e. scale * 2^32.
// For abstract-int this is emitted as a 64-bit integer literal; for abstract-float as an exact
// f32 value (scale * 2^32 is exactly representable in f32 for small scale).
inline Scalar makeAbstractScaled(ScalarKind kind, int scale) {
    const int64_t intVal = static_cast<int64_t>(scale) * static_cast<int64_t>(0x100000000LL);
    if (kind == ScalarKind::AbstractInt) {
        return abstractInt64(intVal);
    }
    // abstract-float: exact f32 of scale * 2^32.
    float f = static_cast<float>(static_cast<double>(intVal));
    uint32_t b;
    std::memcpy(&b, &f, 4);
    return abstractFloatBits(b);
}

// Skips the current case if the device lacks the 'shader-f16' feature (upstream
// skipIfDeviceDoesNotHaveFeature('shader-f16')).
inline void skipIfNoF16(GpuTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

} // namespace access
} // namespace expression
} // namespace cts
