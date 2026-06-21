// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/const_override_validation.ts
// and src/webgpu/util/{math,reinterpret,constants}.ts @
// b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Faithful port of validateConstOrOverrideBinaryOpEval and the supporting Value
// model (a typed value carrying its scalar element numbers), plus the float
// helpers used by the add/sub/mul and div/rem out-of-range tests:
// nextAfterF32/nextAfterF16, reinterpretU32AsF32/reinterpretU16AsF16, and the
// withPoint() number->WGSL spelling.
//
// Number formatting note: JS uses Number.prototype.toString() (shortest
// round-trip decimal) inside withPoint(); this port uses std::to_chars shortest
// round-trip, which produces the same VALUE (it round-trips to the identical
// float) though the exponent spelling for very large magnitudes may differ from
// V8 (e.g. "1.8e19" vs "18000000000000000000"). The compiled value is identical,
// so const/override evaluation (and thus the pass/fail verdict) is unaffected.

#pragma once

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/shader_validation_test.h"

namespace cts::shader_validation::binary {

// ---- reinterpret helpers ----------------------------------------------------
inline float reinterpretU32AsF32(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// f16: decode a 16-bit half to its double value.
inline double f16BitsToDouble(uint16_t h) {
    const uint16_t sign = (h & 0x8000) >> 15;
    const uint16_t exp = (h & 0x7c00) >> 10;
    const uint16_t mant = h & 0x03ff;
    double value;
    if (exp == 0) {
        value = std::ldexp(static_cast<double>(mant), -24);
    } else if (exp == 0x1f) {
        value = mant ? std::nan("") : INFINITY;
    } else {
        value = std::ldexp(static_cast<double>(mant + 1024), exp - 25);
    }
    return sign ? -value : value;
}
inline double reinterpretU16AsF16(uint16_t bits) { return f16BitsToDouble(bits); }

// Quantize a double to the nearest f16, returning (a) the resulting double value
// and (b) the 16-bit encoding. Round-to-nearest-even.
inline uint16_t doubleToF16Bits(double x) {
    if (std::isnan(x)) return 0x7e00;
    uint16_t sign = 0;
    if (std::signbit(x)) {
        sign = 0x8000;
        x = -x;
    }
    if (std::isinf(x) || x > 65504.0 * (1.0 + 1.0 / 2048.0)) {
        return sign | 0x7c00;  // overflow to inf
    }
    if (x == 0.0) return sign;
    int exp;
    double m = std::frexp(x, &exp);  // x = m * 2^exp, m in [0.5,1)
    // normalized half exponent e2 in [-14,15]; bias 15.
    int e = exp - 1;  // since m in [0.5,1), value = (2m) * 2^(exp-1), 2m in [1,2)
    double mant = m * 2.0 - 1.0;  // in [0,1)
    if (e < -14) {
        // subnormal
        double scaled = std::ldexp(x, 24);  // x * 2^24
        long long r = std::llround(scaled);
        if (r >= 1024) {
            // rounded up into normal min
            return sign | 0x0400;
        }
        return static_cast<uint16_t>(sign | (r & 0x03ff));
    }
    if (e > 15) {
        return sign | 0x7c00;
    }
    long long mantBits = std::llround(mant * 1024.0);
    int eBits = e + 15;
    if (mantBits == 1024) {
        mantBits = 0;
        eBits += 1;
        if (eBits >= 0x1f) {
            return sign | 0x7c00;
        }
    }
    return static_cast<uint16_t>(sign | (eBits << 10) | (mantBits & 0x03ff));
}
inline double quantizeToF16(double x) { return f16BitsToDouble(doubleToF16Bits(x)); }
inline double quantizeToF32(double x) { return static_cast<double>(static_cast<float>(x)); }

// ---- nextAfterF32 / nextAfterF16 (positive dir, no-flush) -------------------
inline double nextAfterF32Positive(double val) {
    if (std::isnan(val)) return val;
    float q = static_cast<float>(val);
    uint32_t bits;
    std::memcpy(&bits, &q, sizeof(bits));
    if (static_cast<double>(q) <= val) {
        const bool isPositive = (bits & 0x80000000u) == 0;
        if (isPositive) {
            bits += 1;
        } else {
            bits -= 1;
        }
    }
    if ((bits & 0x7f800000u) == 0x7f800000u) {
        return INFINITY;
    }
    std::memcpy(&q, &bits, sizeof(q));
    return static_cast<double>(q);
}

inline double nextAfterF16Positive(double val) {
    if (std::isnan(val)) return val;
    uint16_t bits = doubleToF16Bits(val);
    double q = f16BitsToDouble(bits);
    if (q <= val) {
        const bool isPositive = (bits & 0x8000) == 0;
        if (isPositive) {
            bits += 1;
        } else {
            bits -= 1;
        }
    }
    if ((bits & 0x7c00) == 0x7c00) {
        return INFINITY;
    }
    return f16BitsToDouble(bits);
}

// ---- withPoint(x): JS Number.toString()-style + ensure decimal point --------
inline std::string numberToString(double x) {
    if (x == static_cast<double>(static_cast<long long>(x)) && std::abs(x) < 1e15) {
        // Integral magnitude: print as integer (JS prints integers without ".0").
        return std::to_string(static_cast<long long>(x));
    }
    char buf[64];
    auto res = std::to_chars(buf, buf + sizeof(buf), x);
    return std::string(buf, res.ptr);
}
inline std::string withPoint(double x) {
    std::string s = numberToString(x);
    if (s.find('.') != std::string::npos || s.find('e') != std::string::npos ||
        s.find('E') != std::string::npos) {
        return s;
    }
    return s + ".0";
}

// ---- Value model ------------------------------------------------------------
// A typed value carrying its scalar element numbers (doubles for floats,
// exact for ints). Mirrors the conversion.ts Value subset used by these tests.
struct EvalValue {
    Type type;                    // scalar or vector
    std::vector<double> elements; // one per scalar element

    // scalarElementsOf: scalar element values.
    const std::vector<double>& scalarElements() const { return elements; }
};

// element scalar kind.
inline ScalarKind elementKind(const EvalValue& v) { return v.type.kind; }

// scalarValueWgsl for a numeric element value of a given scalar kind, matching
// the conversion.ts ScalarValue.wgsl() spellings.
inline std::string elementWgsl(ScalarKind k, double v) {
    switch (k) {
        case ScalarKind::Bool:
            return v != 0.0 ? "true" : "false";
        case ScalarKind::AbstractInt:
            return std::to_string(static_cast<long long>(v));
        case ScalarKind::I32:
            return "i32(" + std::to_string(static_cast<long long>(v)) + ")";
        case ScalarKind::U32:
            return std::to_string(static_cast<long long>(v)) + "u";
        case ScalarKind::AbstractFloat:
            return withPoint(v);
        case ScalarKind::F32:
            return withPoint(v) + "f";
        case ScalarKind::F16:
            return withPoint(v) + "h";
    }
    return "";
}

// value.wgsl(): scalar -> element spelling; vector -> vecN(el0, el1, ...).
inline std::string valueWgsl(const EvalValue& v) {
    if (v.type.isScalar()) {
        return elementWgsl(v.type.kind, v.elements[0]);
    }
    std::string els;
    for (size_t i = 0; i < v.elements.size(); ++i) {
        if (i) {
            els += ", ";
        }
        els += elementWgsl(v.type.kind, v.elements[i]);
    }
    return "vec" + std::to_string(v.type.width) + "(" + els + ")";
}

// value.type WGSL constructor spelling (== Type.toString()).
inline std::string typeWgsl(const Type& ty) { return ty.toString(); }

// Mirror the conversion.ts integer factories i32()/u32(), which store the JS
// number into an Int32Array/Uint32Array — i.e. they wrap out-of-range (e.g.
// negative) values to the type's two's-complement representation BEFORE the
// value is used for either .wgsl() or the override constant. Without this, a
// `sub` left operand `-value` on a u32 would be spelled `-Nu` (invalid WGSL)
// and the override constant would carry a negative (not representable in u32).
inline double wrapIntegerElement(ScalarKind k, double value) {
    if (k == ScalarKind::U32) {
        return static_cast<double>(static_cast<uint32_t>(static_cast<int64_t>(value)));
    }
    if (k == ScalarKind::I32) {
        return static_cast<double>(static_cast<int32_t>(static_cast<int64_t>(value)));
    }
    return value;
}

// ---- create(type, index, value): scalar, or vector filled with `fill` with
// `value` at `index` (mirrors the upstream create() closures). --------------
inline EvalValue createIndexed(const Type& ty, int index, double value, double fill) {
    EvalValue v;
    v.type = ty;
    const double wrapped = wrapIntegerElement(ty.kind, value);
    const double wrappedFill = wrapIntegerElement(ty.kind, fill);
    if (ty.isScalar()) {
        v.elements = {wrapped};
    } else {
        v.elements.assign(static_cast<size_t>(ty.width), wrappedFill);
        v.elements[static_cast<size_t>(index)] = wrapped;
    }
    return v;
}

// ---- validateConstOrOverrideBinaryOpEval ------------------------------------
// stage is "runtime" | "constant" | "override".
inline void validateConstOrOverrideBinaryOpEval(ShaderValidationTest& t, const std::string& binaryOp,
                                                bool expectedResult, const std::string& leftStage,
                                                const EvalValue& left, const std::string& rightStage,
                                                const EvalValue& right) {
    const bool hasF16 =
        elementKind(left) == ScalarKind::F16 || elementKind(right) == ScalarKind::F16;
    const std::string enables = hasF16 ? "enable f16;" : "";

    std::vector<std::string> codeLines;
    codeLines.push_back(enables);
    std::map<std::string, double> constants;
    int numOverrides = 0;

    auto scalarTypeName = [](ScalarKind k) { return scalarKindString(k); };

    auto addOperand = [&](const std::string& name, const std::string& stage,
                          const EvalValue& value) -> std::string {
        if (stage == "runtime") {
            codeLines.push_back("var<private> " + name + " = " + valueWgsl(value) + ";");
            return name;
        }
        if (stage == "constant") {
            codeLines.push_back("const " + name + " = " + valueWgsl(value) + ";");
            return name;
        }
        // override
        std::vector<std::string> argOverrides;
        for (double el : value.scalarElements()) {
            const std::string elName = "o" + std::to_string(numOverrides++);
            codeLines.push_back("override " + elName + " : " + scalarTypeName(value.type.kind) +
                                ";");
            constants[elName] = el;
            argOverrides.push_back(elName);
        }
        std::string joined;
        for (size_t i = 0; i < argOverrides.size(); ++i) {
            if (i) {
                joined += ", ";
            }
            joined += argOverrides[i];
        }
        return typeWgsl(value.type) + "(" + joined + ")";
    };

    const std::string leftOperand = addOperand("left", leftStage, left);
    const std::string rightOperand = addOperand("right", rightStage, right);

    std::string code;
    for (size_t i = 0; i < codeLines.size(); ++i) {
        if (i) {
            code += "\n";
        }
        code += codeLines[i];
    }

    if (leftStage == "override" || rightStage == "override") {
        ShaderValidationTest::PipelineArgs args;
        args.expectedResult = expectedResult;
        args.code = code;
        args.constants = constants;
        args.reference = {leftOperand + " " + binaryOp + " " + rightOperand};
        t.expectPipelineResult(args);
    } else {
        code += "\nfn f() { _ = " + leftOperand + " " + binaryOp + " " + rightOperand + "; }";
        t.expectCompileResult(expectedResult, code);
    }
}

}  // namespace cts::shader_validation::binary
