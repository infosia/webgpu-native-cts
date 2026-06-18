// Ported from gpuweb/cts src/webgpu/shader/execution/expression/expression.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Generic expression-test harness for the integer/bit builtins. This is a faithful
// port of upstream's run()/Config/allInputSources and basicExpressionBuilder shader
// generation, restricted to the exact integer/bool typed cases (u32/i32/bool, plus the
// bit-typed u32Bits/i32Bits which are just bit patterns). FP-interval comparison is out
// of scope. The GPU mechanics (compute pipeline, buffers, readback) follow the existing
// atomics harness and derivatives.cpp.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "cts/gpu.h"

namespace cts {
namespace expression {

// The scalar element kind of a value/type. Bit-typed inputs (u32Bits/i32Bits) carry the
// same kind as their non-bit counterpart; they differ only in how the test author chose
// to spell the constant, which the comparator ignores (bit-exact either way).
enum class ScalarKind {
    U32,
    I32,
    Bool,
};

// The input value source, mirroring upstream's InputSource.
enum class InputSource {
    Const,     // Shader-creation-time constant values
    Uniform,   // Uniform buffer
    StorageR,  // Read-only storage buffer
    StorageRW, // Read-write storage buffer
};

// All possible input sources (allInputSources).
inline const std::vector<InputSource>& allInputSources() {
    static const std::vector<InputSource> v = {
        InputSource::Const, InputSource::Uniform, InputSource::StorageR, InputSource::StorageRW};
    return v;
}

const char* inputSourceName(InputSource source);
InputSource inputSourceFromParam(const std::string& value);

// A type: a scalar kind with a width (1 = scalar, 2/3/4 = vecN).
struct ExprType {
    ScalarKind kind = ScalarKind::U32;
    int width = 1;
};

inline ExprType scalarType(ScalarKind kind) {
    return ExprType{kind, 1};
}
inline ExprType vecType(int width, ScalarKind kind) {
    return ExprType{kind, width};
}

// A scalar value. For U32/I32 'bits' holds the 32-bit pattern; for Bool it is 0 or 1.
struct Scalar {
    ScalarKind kind = ScalarKind::U32;
    uint32_t bits = 0;
};

inline Scalar u32(uint32_t v) {
    return Scalar{ScalarKind::U32, v};
}
inline Scalar u32Bits(uint32_t v) {
    return Scalar{ScalarKind::U32, v};
}
inline Scalar i32(int32_t v) {
    return Scalar{ScalarKind::I32, static_cast<uint32_t>(v)};
}
inline Scalar i32Bits(uint32_t v) {
    return Scalar{ScalarKind::I32, v};
}
inline Scalar boolean(bool v) {
    return Scalar{ScalarKind::Bool, v ? 1u : 0u};
}

// A value: a scalar (width 1) or a vector (width 2/3/4). Always holds 'width' elements.
struct CaseValue {
    int width = 1;
    std::vector<Scalar> elements; // size == width

    CaseValue() = default;
    CaseValue(Scalar s) : width(1), elements{s} {}
    static CaseValue vec(std::vector<Scalar> els) {
        CaseValue v;
        v.width = static_cast<int>(els.size());
        v.elements = std::move(els);
        return v;
    }
};

inline CaseValue vec2(Scalar a, Scalar b) {
    return CaseValue::vec({a, b});
}
inline CaseValue vec3(Scalar a, Scalar b, Scalar c) {
    return CaseValue::vec({a, b, c});
}
inline CaseValue vec4(Scalar a, Scalar b, Scalar c, Scalar d) {
    return CaseValue::vec({a, b, c, d});
}

// A single expression test case: one input value per parameter and the expected result.
struct Case {
    std::vector<CaseValue> inputs;
    CaseValue expected;
};

// ExpressionBuilder returns the WGSL used to evaluate an expression with the given input
// value expressions (one per parameter).
using ExpressionBuilder = std::function<std::string(const std::vector<std::string>&)>;

// Returns a basic builtin-call expression builder, i.e. `name(v0, v1, ...)`.
ExpressionBuilder builtin(const std::string& name);

// Runs the list of expression tests, batching as needed and packing scalar cases into
// vectors when 'vectorize' is non-zero (2/3/4). Bit-exact comparison.
void run(
    GpuTest& t,
    const ExpressionBuilder& exprBuilder,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    int vectorize, // 0 == scalar (undefined), else 2/3/4
    const std::vector<Case>& cases);

} // namespace expression
} // namespace cts
