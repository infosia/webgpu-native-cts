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
#include <memory>
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
    F32,
    F16,
    // Abstract numeric types. These only appear as *parameter* (input) kinds for const-eval
    // cases; they are emitted as untyped numeric literals (AbstractInt / AbstractFloat) and
    // never used as storage/result kinds.
    AbstractInt,
    AbstractFloat,
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

// The structural form of an ExprType.
//   ScalarVec : a scalar (width 1) or a vecN (width 2/3/4) of 'kind'.
//   Matrix    : a matCxR of 'kind' (kind is f32/f16/abstract-float); 'cols' = C, 'width' = R (rows).
//   Array     : array<element, count>; 'element' holds the element ExprType, 'count' the length.
enum class TypeForm {
    ScalarVec,
    Matrix,
    Array,
};

// A type. For ScalarVec: a scalar kind with a width (1 = scalar, 2/3/4 = vecN). For Matrix and
// Array the additional fields describe the composite (see TypeForm). The default value is a u32
// scalar, preserving all existing scalar/vector callers unchanged.
struct ExprType {
    ScalarKind kind = ScalarKind::U32; // element scalar kind (for ScalarVec/Matrix; unused for Array)
    int width = 1;                     // ScalarVec: vector width; Matrix: rows
    TypeForm form = TypeForm::ScalarVec;
    int cols = 0;                      // Matrix: number of columns
    int count = 0;                     // Array: number of elements
    std::shared_ptr<ExprType> element; // Array: element type

    // The scalar element kind of this type (recursing into array elements).
    ScalarKind scalarKind() const {
        if (form == TypeForm::Array && element) {
            return element->scalarKind();
        }
        return kind;
    }
};

inline ExprType scalarType(ScalarKind kind) {
    ExprType ty;
    ty.kind = kind;
    ty.width = 1;
    return ty;
}
inline ExprType vecType(int width, ScalarKind kind) {
    ExprType ty;
    ty.kind = kind;
    ty.width = width;
    return ty;
}
// A matCxR (columns x rows) of the given float element kind.
inline ExprType matType(int cols, int rows, ScalarKind kind) {
    ExprType ty;
    ty.kind = kind;
    ty.width = rows;
    ty.cols = cols;
    ty.form = TypeForm::Matrix;
    return ty;
}
// An array<element, count>.
inline ExprType arrayType(int count, ExprType element) {
    ExprType ty;
    ty.form = TypeForm::Array;
    ty.count = count;
    ty.element = std::make_shared<ExprType>(std::move(element));
    ty.kind = ty.element->scalarKind();
    return ty;
}

// A scalar value. For U32/I32 'bits' holds the 32-bit pattern; for Bool it is 0 or 1; for
// F32 it holds the 32-bit IEEE pattern; for F16 the 16-bit IEEE pattern (in the low 16 bits).
// AbstractInt holds an i32 bit-pattern (the spec only bitcasts AbstractInt via its i32 value)
// and AbstractFloat holds an f32 bit-pattern (emitted as an AbstractFloat literal).
struct Scalar {
    ScalarKind kind = ScalarKind::U32;
    uint32_t bits = 0;
    // For AbstractInt only: the full 64-bit signed value to emit as an untyped integer literal.
    // AbstractInt in WGSL is 64-bit, so element values may exceed the 32-bit 'bits' field (e.g.
    // vector-access tests use elements of (i+1)*2^32). 'bits' still carries the low 32 bits / i32
    // bit-pattern used by the bit-exact comparator; 'bits64' carries the literal to emit.
    int64_t bits64 = 0;
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
// f32 from an existing 32-bit pattern.
inline Scalar f32Bits(uint32_t v) {
    return Scalar{ScalarKind::F32, v};
}
// f16 from an existing 16-bit pattern.
inline Scalar f16Bits(uint16_t v) {
    return Scalar{ScalarKind::F16, static_cast<uint32_t>(v)};
}
// AbstractInt carrying an i32 bit pattern. 'bits' holds the i32 pattern (used by the comparator
// when this value is bitcast); 'bits64' holds the literal value emitted (sign-extended i32).
inline Scalar abstractInt(int32_t v) {
    Scalar s;
    s.kind = ScalarKind::AbstractInt;
    s.bits = static_cast<uint32_t>(v);
    s.bits64 = static_cast<int64_t>(v);
    return s;
}
// AbstractInt carrying a full 64-bit literal value (for values outside the 32-bit range). 'bits'
// holds the low 32 bits; 'bits64' holds the emitted literal.
inline Scalar abstractInt64(int64_t v) {
    Scalar s;
    s.kind = ScalarKind::AbstractInt;
    s.bits = static_cast<uint32_t>(static_cast<uint64_t>(v));
    s.bits64 = v;
    return s;
}
// AbstractFloat carrying an f32 bit pattern.
inline Scalar abstractFloatBits(uint32_t v) {
    return Scalar{ScalarKind::AbstractFloat, v};
}

// A value. For a scalar (width 1) or vecN (width 2/3/4) it holds 'width' elements. For a composite
// (matrix or array) it holds all the scalar elements flattened in canonical order:
//   Matrix : column-major (column 0 rows 0..R-1, column 1 rows 0..R-1, ...), 'width' == cols*rows.
//   Array  : element 0's scalars, element 1's scalars, ... (each element flattened the same way),
//            'width' == count * scalarsPerElement.
// The structure (how to slice these scalars into columns/rows/elements with the right byte layout)
// is supplied by the ExprType passed to run(); CaseValue only carries the ordered scalar payload.
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
    // A composite value (matrix or array) from its flattened scalar payload (canonical order).
    static CaseValue composite(std::vector<Scalar> els) {
        return vec(std::move(els));
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

// Per-element acceptance for a result whose exact bits may be canonicalized by the GPU
// (used for bitcast-to-float). 'any' accepts any bit pattern (the value is unbounded, e.g.
// an Inf/NaN result in runtime mode). Otherwise the read-back bits must match one of
// 'acceptBits' exactly. For float results this set is { exact } plus the flushed zero bit
// patterns when the exact value is subnormal, plus any-NaN handling encoded by 'anyNaN'.
struct ExpectedElement {
    bool any = false;        // accept any bit pattern (unbounded result)
    bool anyNaN = false;     // additionally accept any NaN bit pattern (float result is NaN)
    int floatWidth = 0;      // 0 == integer-exact; 32 or 16 == float result element width
    std::vector<uint32_t> acceptBits; // accepted exact bit patterns (low 16 bits used if width 16)
    // Float acceptance interval (used by the pack/unpack float-result builtins). When
    // 'interval' is set the read-back element is decoded as a 'floatWidth'-bit IEEE float
    // and accepted iff its value lies within [lo, hi] inclusive (an exact match of an FP
    // acceptance interval; the interval endpoints already incorporate any subnormal flush,
    // so no extra flushing of the read-back value is performed). NaN is never accepted by an
    // interval (use 'any' for the unbounded case).
    bool interval = false;
    double lo = 0.0;
    double hi = 0.0;
};

// Builds an ExpectedElement that accepts any float value in the inclusive interval [lo, hi]
// for a result element of the given IEEE width (32 or 16). The endpoints must already
// incorporate subnormal-flush handling per the upstream acceptance interval.
inline ExpectedElement acceptInterval(int floatWidth, double lo, double hi) {
    ExpectedElement ee;
    ee.floatWidth = floatWidth;
    ee.interval = true;
    ee.lo = lo;
    ee.hi = hi;
    return ee;
}

// Builds an ExpectedElement that accepts any value (the unbounded interval).
inline ExpectedElement acceptAny() {
    ExpectedElement ee;
    ee.any = true;
    return ee;
}

// Builds an ExpectedElement that accepts any of the given exact bit patterns.
inline ExpectedElement acceptBitsSet(std::vector<uint32_t> bits) {
    ExpectedElement ee;
    ee.acceptBits = std::move(bits);
    return ee;
}

// A single expression test case: one input value per parameter and the expected result.
// If 'expectedAccept' is non-empty it overrides bit-exact comparison of 'expected' and is used
// per result element (its length must equal the result width). This is how bitcast expresses
// NaN-any / subnormal-flush acceptance for float results without inflating integer comparisons.
struct Case {
    std::vector<CaseValue> inputs;
    CaseValue expected;
    std::vector<ExpectedElement> expectedAccept;

    Case() = default;
    Case(std::vector<CaseValue> in, CaseValue exp)
        : inputs(std::move(in)), expected(std::move(exp)) {}
    Case(std::vector<CaseValue> in, CaseValue exp, std::vector<ExpectedElement> acc)
        : inputs(std::move(in)), expected(std::move(exp)), expectedAccept(std::move(acc)) {}
};

// ExpressionBuilder returns the WGSL used to evaluate an expression with the given input
// value expressions (one per parameter).
using ExpressionBuilder = std::function<std::string(const std::vector<std::string>&)>;

// Returns a basic builtin-call expression builder, i.e. `name(v0, v1, ...)`.
ExpressionBuilder builtin(const std::string& name);

// ---------------------------------------------------------------------------
// Layout helpers (shared with the structure-access port's custom run()).
// These mirror upstream expression.ts's structLayout / structStride / align /
// sizeAndAlignmentOf, exposed so the structure/index.spec.cpp port can pack a
// heterogeneous-member struct buffer at the exact WGSL member offsets.
// ---------------------------------------------------------------------------

// Rounds 'n' up to the next multiple of 'alignment' (upstream util/math align()).
uint32_t align(uint32_t n, uint32_t alignment);

// size and alignment in bytes of a type, per upstream sizeAndAlignmentOf. 'source' affects only
// array alignment (uniform requires array element alignment to be a multiple of 16).
struct SizeAlign {
    uint32_t size;
    uint32_t alignment;
};
SizeAlign sizeAndAlignmentOf(const ExprType& ty, InputSource source);

// Per-member layout info produced by structLayout().
struct MemberLayout {
    int index;       // member ordinal
    ExprType type;   // member type
    uint32_t size;   // member size in bytes (sizeAndAlignmentOf().size)
    uint32_t alignment;
    uint32_t offset; // byte offset of the member within the struct
};

// Whole-struct layout: 'size' is the offset past the last member, 'stride' is the size rounded up
// to the struct alignment (whole struct size), 'alignment' the max member alignment.
struct StructLayoutResult {
    uint32_t size;
    uint32_t stride;
    uint32_t alignment;
};

// Walks the members of a struct, invoking 'cb' per member with its computed offset, returning the
// whole-struct {size, stride, alignment}. 'cb' may be null (to compute the stride only).
StructLayoutResult structLayout(
    const std::vector<ExprType>& members,
    InputSource source,
    const std::function<void(const MemberLayout&)>& cb);

// The whole-struct stride (size rounded to struct alignment) of the member types.
uint32_t structStride(const std::vector<ExprType>& members, InputSource source);

// WGSL type spelling of an ExprType (e.g. "u32", "vec3<f32>", "mat3x2<f32>"). bool is spelled as
// 'bool' (not its u32 storage type); use storageWgslTypeName() for the storage-buffer spelling.
std::string wgslTypeName(const ExprType& ty);
// WGSL type spelling using the storage representation (bool -> u32).
std::string storageWgslTypeName(const ExprType& ty);

// WGSL constructor literal for a value of type 'ty' from its flattened scalar payload (canonical
// order), e.g. "vec3<f32>(0.0, 0.0, 0.0)" or "5u". Mirrors upstream Value.wgsl().
std::string valueWgsl(const CaseValue& v, const ExprType& ty);

// Serializes a value of type 'ty' (scalar/vec/mat) into 'data' at byte offset 'offset', writing
// each canonical scalar slot at its WGSL byte offset within the value (vec3 / matrix-column padding
// is left untouched). 'data' must be large enough. Mirrors upstream Value.copyTo().
void copyValueTo(
    const CaseValue& v,
    const ExprType& ty,
    InputSource source,
    std::vector<uint8_t>& data,
    uint32_t offset);

// Reads a value of type 'ty' back from 'data' at byte offset 'offset', producing its canonical
// flattened scalar payload. Mirrors upstream Type.read(). Returns false on out-of-bounds.
bool readValueFrom(
    const ExprType& ty,
    InputSource source,
    const uint8_t* data,
    size_t len,
    uint32_t offset,
    CaseValue& out);

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
