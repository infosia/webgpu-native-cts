// Ported from gpuweb/cts src/webgpu/shader/execution/expression/expression.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/expression.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

namespace cts {
namespace expression {

namespace {

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

uint32_t alignUp(uint32_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

const char* scalarKindName(ScalarKind kind) {
    switch (kind) {
        case ScalarKind::U32:
            return "u32";
        case ScalarKind::I32:
            return "i32";
        case ScalarKind::Bool:
            return "bool";
        case ScalarKind::F32:
            return "f32";
        case ScalarKind::F16:
            return "f16";
        case ScalarKind::AbstractInt:
        case ScalarKind::AbstractFloat:
            // Abstract numeric types are spelled implicitly via untyped literals; there is no
            // concrete WGSL type name for them in this harness.
            break;
    }
    std::abort();
}

// Element byte width: f16 is 2 bytes, everything else (u32/i32/f32/bool-as-u32) is 4.
uint32_t elementBytes(ScalarKind kind) {
    return kind == ScalarKind::F16 ? 2u : 4u;
}

// WGSL type spelling of an ExprType (e.g. "u32", "vec3<i32>", "mat2x3<f32>", "array<i32, 3>").
std::string typeName(ExprType ty) {
    if (ty.form == TypeForm::Matrix) {
        return "mat" + std::to_string(ty.cols) + "x" + std::to_string(ty.width) + "<" +
               scalarKindName(ty.kind) + ">";
    }
    if (ty.form == TypeForm::Array) {
        return "array<" + typeName(*ty.element) + ", " + std::to_string(ty.count) + ">";
    }
    if (ty.width == 1) {
        return scalarKindName(ty.kind);
    }
    return "vec" + std::to_string(ty.width) + "<" + scalarKindName(ty.kind) + ">";
}

// The storage element kind: bool is stored as u32. The width is preserved.
ScalarKind storageKind(ScalarKind kind) {
    return kind == ScalarKind::Bool ? ScalarKind::U32 : kind;
}

ExprType storageType(ExprType ty) {
    if (ty.form == TypeForm::Matrix) {
        // Matrix element types are always float; storage type is unchanged.
        return ty;
    }
    if (ty.form == TypeForm::Array) {
        return arrayType(ty.count, storageType(*ty.element));
    }
    return vecType(ty.width, storageKind(ty.kind));
}

std::string storageTypeName(ExprType ty) {
    return typeName(storageType(ty));
}

// size and alignment in bytes of the type, per upstream sizeAndAlignmentOf.
struct SizeAlignImpl {
    uint32_t size;
    uint32_t alignment;
};

// size and alignment in bytes, per upstream sizeAndAlignmentOf. 'source' affects only the array
// alignment (uniform requires array element alignment to be a multiple of 16; the
// uniform_buffer_standard_layout gate is handled by the spec ports skipping such cases).
SizeAlignImpl sizeAndAlignmentOfImpl(ExprType ty, InputSource source) {
    if (ty.form == TypeForm::Matrix) {
        // matCxR: element-aligned columns; vec3 columns pad to 4 rows. size = colAlign * cols.
        const uint32_t eb = elementBytes(ty.kind);
        const uint32_t n = ty.width == 3 ? 4u : static_cast<uint32_t>(ty.width);
        const uint32_t colAlign = eb * n;
        SizeAlignImpl out{colAlign * static_cast<uint32_t>(ty.cols), colAlign};
        return out;
    }
    if (ty.form == TypeForm::Array) {
        SizeAlignImpl out = sizeAndAlignmentOfImpl(*ty.element, source);
        // MAINTENANCE_TODO(#4485): remove when implementors support uniform_buffer_standard_layout.
        if (source == InputSource::Uniform) {
            out.alignment = alignUp(out.alignment, 16);
        }
        // Upstream: out.size *= count (element size, not element stride). This matches the @size()
        // used in the input struct member; the actual per-element WGSL stride is arrayElementStride().
        out.size *= static_cast<uint32_t>(ty.count);
        return out;
    }
    // scalar: size = element bytes (4 for u32/i32/f32/bool, 2 for f16), alignment == size.
    const uint32_t eb = elementBytes(ty.kind);
    SizeAlignImpl out{eb, eb};
    if (ty.width > 1) {
        const uint32_t n = ty.width == 3 ? 4u : static_cast<uint32_t>(ty.width);
        out.size = eb * n;
        out.alignment = eb * n;
    }
    return out;
}

uint32_t strideOf(ExprType ty, InputSource source) {
    SizeAlignImpl sa = sizeAndAlignmentOfImpl(ty, source);
    return alignUp(sa.size, sa.alignment);
}

// The per-element WGSL stride of an array type (align(elementSize, elementAlignment)). This is the
// byte distance between consecutive array elements in the buffer, which can differ from the
// per-element size when the element needs padding (and from sizeAndAlignmentOf().size/count, which
// upstream computes from the un-strided element size).
uint32_t arrayElementStride(const ExprType& arrayTy, InputSource source) {
    SizeAlignImpl elem = sizeAndAlignmentOfImpl(*arrayTy.element, source);
    return alignUp(elem.size, elem.alignment);
}

// Walks the canonical scalar slots of a (possibly composite) value of type 'ty', calling
// cb(scalarIndex, byteOffset, scalarKind) for each scalar, in the same order CaseValue stores its
// flattened scalar payload:
//   ScalarVec : the 'width' scalars at consecutive element-byte offsets (vec3 leaves the 4th slot
//               unwritten, i.e. no scalar is emitted for the pad).
//   Matrix    : column-major; each column occupies an element-aligned stride (vec3 columns pad to 4
//               rows) and within a column the rows are at consecutive element-byte offsets.
//   Array     : each element laid out at its WGSL array stride, recursing into the element type.
// 'baseOffset' is the byte offset of the value, 'scalarIndex' counts scalars across the whole value.
void forEachStorageScalar(
    const ExprType& ty,
    InputSource source,
    uint32_t baseOffset,
    int& scalarIndex,
    const std::function<void(int, uint32_t, ScalarKind)>& cb) {
    const ScalarKind sk = storageKind(ty.scalarKind());
    if (ty.form == TypeForm::Array) {
        const uint32_t stride = arrayElementStride(ty, source);
        for (int i = 0; i < ty.count; ++i) {
            forEachStorageScalar(*ty.element, source, baseOffset + static_cast<uint32_t>(i) * stride,
                                 scalarIndex, cb);
        }
        return;
    }
    const uint32_t eb = elementBytes(ty.kind);
    if (ty.form == TypeForm::Matrix) {
        const uint32_t n = ty.width == 3 ? 4u : static_cast<uint32_t>(ty.width);
        const uint32_t colStride = eb * n;
        for (int c = 0; c < ty.cols; ++c) {
            const uint32_t colBase = baseOffset + static_cast<uint32_t>(c) * colStride;
            for (int r = 0; r < ty.width; ++r) {
                cb(scalarIndex++, colBase + static_cast<uint32_t>(r) * eb, sk);
            }
        }
        return;
    }
    // Scalar / vector: 'width' consecutive elements (the vec3 pad slot is not a scalar).
    for (int e = 0; e < ty.width; ++e) {
        cb(scalarIndex++, baseOffset + static_cast<uint32_t>(e) * eb, sk);
    }
}

// The number of canonical scalar slots in a value of type 'ty'.
int scalarSlotCount(const ExprType& ty) {
    if (ty.form == TypeForm::Array) {
        return ty.count * scalarSlotCount(*ty.element);
    }
    if (ty.form == TypeForm::Matrix) {
        return ty.cols * ty.width;
    }
    return ty.width;
}

// structLayout over the member types, invoking 'cb' per member, returning {size, stride}.
struct MemberInfoImpl {
    int index;
    ExprType type;
    uint32_t size;
    uint32_t alignment;
    uint32_t offset;
};

struct StructLayoutImpl {
    uint32_t size;
    uint32_t stride;
    uint32_t alignment;
};

StructLayoutImpl structLayoutImpl(
    const std::vector<ExprType>& members,
    InputSource source,
    const std::function<void(const MemberInfoImpl&)>& cb) {
    uint32_t offset = 0;
    uint32_t alignment = 1;
    for (size_t i = 0; i < members.size(); ++i) {
        SizeAlignImpl sa = sizeAndAlignmentOfImpl(members[i], source);
        offset = alignUp(offset, sa.alignment);
        if (cb) {
            cb(MemberInfoImpl{static_cast<int>(i), members[i], sa.size, sa.alignment, offset});
        }
        offset += sa.size;
        if (sa.alignment > alignment) {
            alignment = sa.alignment;
        }
    }
    // MAINTENANCE_TODO(#4485): remove when implementors support uniform_buffer_standard_layout.
    if (source == InputSource::Uniform) {
        alignment = alignUp(alignment, 16);
    }
    const uint32_t size = offset;
    const uint32_t stride = alignUp(size, alignment);
    return StructLayoutImpl{size, stride, alignment};
}

uint32_t structStrideImpl(const std::vector<ExprType>& members, InputSource source) {
    return structLayoutImpl(members, source, nullptr).stride;
}

// WGSL spelling of the struct members for the Input struct.
std::string wgslMembers(
    const std::vector<ExprType>& members,
    InputSource source,
    const std::function<std::string(int)>& memberName) {
    std::ostringstream out;
    int line = 0;
    StructLayoutImpl layout = structLayoutImpl(members, source, [&](const MemberInfoImpl& m) {
        out << "  @size(" << m.size << ") " << memberName(line) << " : " << typeName(m.type) << ",\n";
        ++line;
    });
    const uint32_t padding = layout.stride - layout.size;
    if (padding > 0) {
        // Pad with an 'f16' if the padding requires an odd multiple of 2 bytes.
        const char* ty = (padding & 2u) != 0 ? "f16" : "i32";
        out << "  @size(" << padding << ") padding : " << ty << ",\n";
    }
    return out.str();
}

// Accumulates module-scope WGSL helper functions emitted by [from|to]Storage and hands out unique
// identifiers, mirroring upstream's TypeConversionHelpers.
struct TypeConversionHelpers {
    std::string wgsl;
    int next = 0;
    std::string uniqueID() { return "cts_symbol_" + std::to_string(next++); }
};

// WGSL expression converting a value from the storage type (bool: != 0; array<bool>: per-element).
std::string fromStorage(ExprType ty, const std::string& expr, TypeConversionHelpers& helpers) {
    if (ty.form == TypeForm::Array && ty.element && ty.element->kind == ScalarKind::Bool &&
        ty.element->form == TypeForm::ScalarVec && ty.element->width == 1) {
        // array<u32, N> -> array<bool, N>
        const std::string conv = helpers.uniqueID();
        const std::string inTy = typeName(arrayType(ty.count, scalarType(ScalarKind::U32)));
        const std::string outTy = typeName(ty);
        helpers.wgsl += "\nfn " + conv + "(in : " + inTy + ") -> " + outTy + " {\n" +
                        "  var out : " + outTy + ";\n" + "  for (var i = 0; i < " +
                        std::to_string(ty.count) + "; i++) {\n" + "    out[i] = in[i] != 0;\n" +
                        "  }\n" + "  return out;\n" + "}\n";
        return conv + "(" + expr + ")";
    }
    if (ty.form == TypeForm::ScalarVec && ty.kind == ScalarKind::Bool) {
        if (ty.width == 1) {
            return expr + " != 0u";
        }
        return "(" + expr + " != vec" + std::to_string(ty.width) + "<u32>(0u))";
    }
    return expr;
}

// WGSL expression converting a value to the storage type (bool: select(0,1,e)). Result types in
// these tests are scalar/vector/matrix, or array-of-bool (constructor tests).
std::string toStorage(ExprType ty, const std::string& expr) {
    if (ty.form == TypeForm::ScalarVec && ty.kind == ScalarKind::Bool) {
        if (ty.width == 1) {
            return "select(0u, 1u, " + expr + ")";
        }
        const std::string z = "vec" + std::to_string(ty.width) + "<u32>(0u)";
        const std::string o = "vec" + std::to_string(ty.width) + "<u32>(1u)";
        return "select(" + z + ", " + o + ", " + expr + ")";
    }
    if (ty.form == TypeForm::Array && ty.element && ty.element->form == TypeForm::ScalarVec &&
        ty.element->kind == ScalarKind::Bool && ty.element->width == 1) {
        // array<bool, N> -> array<u32, N>, converting each element with select(). The result
        // expression is evaluated once per element; the constructor cases use const expressions, so
        // this is well-defined.
        std::ostringstream out;
        out << "array<u32, " << ty.count << ">(";
        for (int i = 0; i < ty.count; ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << "select(0u, 1u, (" << expr << ")[" << i << "])";
        }
        out << ")";
        return out.str();
    }
    return expr;
}

// Emits a decimal literal that exactly represents the given finite float value, with an
// explicit decimal point and the given WGSL suffix ('f' for f32, '' for AbstractFloat).
// Uses round-trip-precise digits. Only used for finite values.
std::string floatLiteral(double value, const char* suffix) {
    std::ostringstream out;
    out.precision(17);
    out << value;
    std::string s = out.str();
    // Ensure a decimal point / exponent so it is parsed as a float, not an int.
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos && s.find("inf") == std::string::npos &&
        s.find("nan") == std::string::npos) {
        s += ".0";
    }
    return s + suffix;
}

float f32FromBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// Decodes a 16-bit IEEE-754 binary16 bit pattern to its exact value as a double.
double f16BitsToDouble(uint16_t bits) {
    const uint32_t sign = (bits >> 15) & 0x1u;
    const uint32_t exp = (bits >> 10) & 0x1Fu;
    const uint32_t mant = bits & 0x3FFu;
    double value;
    if (exp == 0) {
        // Zero or subnormal: value = mant * 2^-24.
        value = static_cast<double>(mant) * (1.0 / 16777216.0);
    } else if (exp == 0x1F) {
        value = (mant == 0) ? std::numeric_limits<double>::infinity()
                            : std::numeric_limits<double>::quiet_NaN();
    } else {
        // Normal: (1 + mant/1024) * 2^(exp-15).
        value = (1.0 + static_cast<double>(mant) / 1024.0) * std::ldexp(1.0, static_cast<int>(exp) - 15);
    }
    return sign ? -value : value;
}

// WGSL literal for a scalar value as a const-expression input.
std::string scalarWgsl(const Scalar& s) {
    switch (s.kind) {
        case ScalarKind::U32:
            return std::to_string(s.bits) + "u";
        case ScalarKind::I32: {
            // i32Bits: reinterpret as a signed value, then emit bitcast<i32> of the u32 pattern
            // to avoid literal-range issues for high-bit-set values.
            return "bitcast<i32>(" + std::to_string(s.bits) + "u)";
        }
        case ScalarKind::Bool:
            return s.bits != 0 ? "true" : "false";
        case ScalarKind::F32:
            // Exact: reinterpret the stored 32-bit pattern (handles all finite values precisely).
            return "bitcast<f32>(" + std::to_string(s.bits) + "u)";
        case ScalarKind::F16:
            // Exact: pack the 16-bit pattern into the low half of a u32 and take the low element.
            return "bitcast<vec2<f16>>(" + std::to_string(s.bits & 0xFFFFu) + "u).x";
        case ScalarKind::AbstractInt:
            // AbstractInt literal: emit the signed 64-bit decimal value (no suffix). AbstractInt
            // is 64-bit in WGSL, so 'bits64' carries the full literal (set from the i32 pattern
            // by abstractInt(), or directly by abstractInt64()). WGSL parses negative numbers as a
            // negated positive, so INT64_MIN ('-9223372036854775808') is invalid because
            // '9223372036854775808' is not a valid AbstractInt; emit it as '(-max - 1)'.
            if (s.bits64 == INT64_MIN) {
                return "(-9223372036854775807 - 1)";
            }
            return std::to_string(s.bits64);
        case ScalarKind::AbstractFloat:
            // AbstractFloat literal. When carrying an exact f64 value (FP-interval framework),
            // emit it as an exact hex-float literal (%a) so no precision is lost; WGSL accepts
            // hex-float literals for abstract-float. Otherwise emit an exact decimal of the f32
            // value reinterpreted from the 32-bit pattern (no suffix).
            if (s.hasF64) {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%a", s.f64);
                return std::string(buf);
            }
            return floatLiteral(static_cast<double>(f32FromBits(s.bits)), "");
    }
    std::abort();
}

bool isAbstractKind(ScalarKind k) {
    return k == ScalarKind::AbstractInt || k == ScalarKind::AbstractFloat;
}

// WGSL literal for a value (scalar, vecN, matCxR, or array constructor). 'scalars' is the value's
// flattened scalar payload (CaseValue::elements, in canonical order); 'next' is the index of the
// next scalar to consume and is advanced as the recursion walks the value. 'ty' supplies structure.
std::string valueWgslRec(const ExprType& ty, const std::vector<Scalar>& scalars, size_t& next) {
    if (ty.form == TypeForm::Array) {
        // Abstract arrays are spelled without an explicit element type (array(...)).
        const bool isAbstract = isAbstractKind(ty.element->scalarKind());
        std::ostringstream out;
        out << (isAbstract ? std::string("array(") : ("array<" + typeName(*ty.element) + ", " +
                                                       std::to_string(ty.count) + ">("));
        for (int i = 0; i < ty.count; ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << valueWgslRec(*ty.element, scalars, next);
        }
        out << ")";
        return out.str();
    }
    if (ty.form == TypeForm::Matrix) {
        const bool isAbstract = isAbstractKind(ty.kind);
        std::ostringstream out;
        if (isAbstract) {
            out << "mat" << ty.cols << "x" << ty.width << "(";
        } else {
            out << "mat" << ty.cols << "x" << ty.width << "<" << scalarKindName(ty.kind) << ">(";
        }
        const int n = ty.cols * ty.width;
        for (int i = 0; i < n; ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << scalarWgsl(scalars[next++]);
        }
        out << ")";
        return out.str();
    }
    if (ty.width == 1) {
        return scalarWgsl(scalars[next++]);
    }
    // vecN. Abstract vectors are spelled without an explicit element type (vecN(...)).
    const bool isAbstract = isAbstractKind(ty.kind);
    std::ostringstream out;
    if (isAbstract) {
        out << "vec" << ty.width << "(";
    } else {
        out << "vec" << ty.width << "<" << scalarKindName(ty.kind) << ">(";
    }
    for (int i = 0; i < ty.width; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << scalarWgsl(scalars[next++]);
    }
    out << ")";
    return out.str();
}

// WGSL literal for a value of type 'ty' from its flattened scalar payload.
std::string valueWgslImpl(const CaseValue& v, const ExprType& ty) {
    size_t next = 0;
    return valueWgslRec(ty, v.elements, next);
}

bool isAbstractIntResult(const ExprType& ty) {
    return ty.scalarKind() == ScalarKind::AbstractInt;
}

// size/alignment of a result type for the abstract-int output encoding (each abstract-int scalar is
// a struct of 2x u32, i.e. 8 bytes / 8-byte aligned). Mirrors upstream sizeAndAlignmentOf for
// abstract numerics. Only scalar / vecN result types are exercised here.
struct AISizeAlign {
    uint32_t size;
    uint32_t alignment;
};
AISizeAlign abstractIntSizeAlign(const ExprType& ty) {
    AISizeAlign sa{8u, 8u};
    if (ty.form == TypeForm::ScalarVec && ty.width > 1) {
        const uint32_t n = ty.width == 3 ? 4u : static_cast<uint32_t>(ty.width);
        sa.size = 8u * n;
        sa.alignment = 8u * n;
    } else if (ty.form == TypeForm::Matrix) {
        // Abstract-float matrix result: a flat array<AF, cols*rows> of 8-byte {low,high} slots in
        // column-major order. AF is 8 bytes / align 8; the flat array's size is 8*cols*rows.
        const uint32_t n = static_cast<uint32_t>(ty.cols * ty.width);
        sa.size = 8u * n;
        sa.alignment = 8u;
    }
    return sa;
}

uint32_t abstractIntStride(const ExprType& ty) {
    const AISizeAlign sa = abstractIntSizeAlign(ty);
    return alignUp(sa.size, sa.alignment);
}

// WGSL output struct + bound output array declaration for an abstract-int result. The result is
// materialized as a per-element struct AF { low: u32, high: u32 }.
std::string wgslAbstractIntOutputs(const ExprType& resultType, size_t count) {
    std::ostringstream out;
    const uint32_t stride = abstractIntStride(resultType);
    out << "struct AF {\n  low: u32,\n  high: u32,\n};\n\n";
    out << "struct Output {\n  @size(" << stride << ") value: ";
    if (resultType.form == TypeForm::ScalarVec && resultType.width > 1) {
        out << "array<AF, " << resultType.width << ">";
    } else if (resultType.form == TypeForm::Matrix) {
        out << "array<AF, " << (resultType.cols * resultType.width) << ">";
    } else {
        out << "AF";
    }
    out << ",\n};\n";
    out << "@group(0) @binding(0) var<storage, read_write> outputs : array<Output, " << count
        << ">;\n";
    return out.str();
}

// Inlined snippet that splits an abstract-int expression value into the low/high u32 fields of the
// output. Mirrors upstream abstractIntSnippet. 'accessor' is "" for scalars or "[i]" for vectors.
std::string abstractIntSnippet(const std::string& expr, size_t caseIdx, const std::string& accessor) {
    const std::string i = std::to_string(caseIdx);
    std::ostringstream out;
    out << "  {\n"
        << "    outputs[" << i << "].value" << accessor << ".high = bitcast<u32>(i32((" << expr << ")"
        << accessor << " >> 32)) & 0xFFFFFFFF;\n"
        << "    const low_sign = ((" << expr << ")" << accessor << " & (1 << 31));\n"
        << "    outputs[" << i << "].value" << accessor << ".low = bitcast<u32>(((" << expr << ")"
        << accessor << " & 0x7FFFFFFF)) | low_sign;\n"
        << "  }\n";
    return out.str();
}

bool isAbstractFloatResult(const ExprType& ty) {
    return ty.scalarKind() == ScalarKind::AbstractFloat;
}

// Inlined snippet that splits an abstract-float (f64) expression value into the low/high u32 fields
// of the output, applying FTZ for subnormals. Mirrors upstream abstractFloatSnippet. 'accessor' is
// "" for scalars or "[i]" for vectors. The reconstructed f64 is rebuilt host-side from low/high.
std::string abstractFloatSnippet(const std::string& expr, size_t caseIdx,
                                 const std::string& inAccessor, const std::string& outAccessor) {
    const std::string i = std::to_string(caseIdx);
    const std::string e = "(" + expr + ")" + inAccessor;
    const std::string& accessor = outAccessor;
    std::ostringstream out;
    out << "  {\n"
        << "    const kExponentBias = 1022;\n"
        << "    const subnormal_or_zero : bool = (" << e
        << " <= 0x0.fffffffffffffp-1022) && (" << e << " >= -0x0.fffffffffffffp-1022);\n"
        << "    const sign_bit : u32 = select(0, 0x80000000, " << e << " < 0);\n"
        << "    const f = frexp(abs(" << e << "));\n"
        << "    const f_fract = select(f.fract, 0, subnormal_or_zero);\n"
        << "    const f_exp = select(f.exp, -kExponentBias, subnormal_or_zero);\n"
        << "    const exponent_bits : u32 = u32(f_exp + kExponentBias) << 20;\n"
        << "    const high_mantissa = ldexp(f_fract, 21);\n"
        << "    const high_mantissa_bits : u32 = u32(ldexp(f_fract, 21)) & 0x000fffff;\n"
        << "    const low_mantissa = f_fract - ldexp(floor(high_mantissa), -21);\n"
        << "    const low_mantissa_bits = u32(ldexp(low_mantissa, 53));\n"
        << "    outputs[" << i << "].value" << accessor
        << ".high = sign_bit | exponent_bits | high_mantissa_bits;\n"
        << "    outputs[" << i << "].value" << accessor << ".low = low_mantissa_bits;\n"
        << "  }\n";
    return out.str();
}

// WGSL output struct + bound output array declaration.
std::string wgslOutputs(ExprType resultType, size_t count) {
    std::ostringstream out;
    out << "struct Output {\n"
        << "  @size(" << strideOf(resultType, InputSource::StorageRW)
        << ") value : " << storageTypeName(resultType) << "\n"
        << "};\n"
        << "@group(0) @binding(0) var<storage, read_write> outputs : array<Output, " << count
        << ">;\n";
    return out.str();
}

// WGSL var declaration for a runtime input source.
std::string wgslInputVar(InputSource source, size_t count) {
    const std::string suffix = "inputs : array<Input, " + std::to_string(count) + ">;";
    switch (source) {
        case InputSource::StorageR:
            return "@group(0) @binding(1) var<storage, read> " + suffix;
        case InputSource::StorageRW:
            return "@group(0) @binding(1) var<storage, read_write> " + suffix;
        case InputSource::Uniform:
            return "@group(0) @binding(1) var<uniform> " + suffix;
        case InputSource::Const:
            break;
    }
    std::abort();
}

// Builds the WGSL shader for a compound-assignment batch (e.g. 'lhs op= rhs'). Mirrors upstream
// compoundAssignmentBuilder. parameterTypes are [lhsType, rhsType]; resultType == lhsType
// (concrete only). 'op' is the compound operator spelling (e.g. "+=", "<<=").
std::string buildCompoundShader(
    const std::string& op,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    const std::vector<Case>& cases) {
    std::ostringstream out;

    bool usesF16 = resultType.kind == ScalarKind::F16;
    for (ExprType ty : parameterTypes) {
        usesF16 = usesF16 || ty.kind == ScalarKind::F16;
    }
    if (usesF16) {
        out << "enable f16;\n";
    }

    const ExprType lhsType = parameterTypes[0];
    const ExprType rhsType = parameterTypes[1];

    if (inputSource == InputSource::Const) {
        out << wgslOutputs(resultType, cases.size()) << "\n";
        // lhs/rhs const arrays.
        out << "const lhs = array(\n";
        for (size_t i = 0; i < cases.size(); ++i) {
            out << "  " << valueWgslImpl(cases[i].inputs[0], lhsType)
                << (i + 1 < cases.size() ? "," : "") << "\n";
        }
        out << ");\n";
        out << "const rhs = array(\n";
        for (size_t i = 0; i < cases.size(); ++i) {
            out << "  " << valueWgslImpl(cases[i].inputs[1], rhsType)
                << (i + 1 < cases.size() ? "," : "") << "\n";
        }
        out << ");\n\n";
        out << "@compute @workgroup_size(1)\nfn main() {\n";
        for (size_t i = 0; i < cases.size(); ++i) {
            out << "  var ret_" << i << " = lhs[" << i << "];\n"
                << "  ret_" << i << " " << op << " rhs[" << i << "];\n"
                << "  outputs[" << i << "].value = " << toStorage(resultType, "ret_" + std::to_string(i))
                << ";\n";
        }
        out << "}\n";
        return out.str();
    }

    // Runtime eval.
    std::vector<ExprType> storageParams = {storageType(lhsType), storageType(rhsType)};
    out << "struct Input {\n"
        << wgslMembers(storageParams, inputSource,
                       [](int i) { return std::string(i == 0 ? "lhs" : "rhs"); })
        << "}\n\n";
    out << wgslOutputs(resultType, cases.size()) << "\n";
    out << wgslInputVar(inputSource, cases.size()) << "\n\n";

    out << "@compute @workgroup_size(1)\nfn main() {\n"
        << "  for (var i = 0; i < " << cases.size() << "; i++) {\n"
        << "    var ret = " << wgslTypeName(lhsType) << "(inputs[i].lhs);\n"
        << "    ret " << op << " " << wgslTypeName(rhsType) << "(inputs[i].rhs);\n"
        << "    outputs[i].value = " << toStorage(resultType, "ret") << ";\n"
        << "  }\n"
        << "}\n";
    return out.str();
}

// Builds the WGSL shader for a batch of cases.
std::string buildShader(
    const ExpressionBuilder& exprBuilder,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    const std::vector<Case>& cases) {
    std::ostringstream out;

    // Emit `enable f16;` if any parameter or the result type is an f16 type.
    bool usesF16 = resultType.kind == ScalarKind::F16;
    for (ExprType ty : parameterTypes) {
        usesF16 = usesF16 || ty.kind == ScalarKind::F16;
    }
    if (usesF16) {
        out << "enable f16;\n";
    }

    if (inputSource == InputSource::Const) {
        if (isAbstractIntResult(resultType)) {
            // Abstract-int result: materialize each value via the low/high u32 split protocol.
            out << wgslAbstractIntOutputs(resultType, cases.size()) << "\n";
            out << "@compute @workgroup_size(1)\nfn main() {\n";
            const bool isVec = resultType.form == TypeForm::ScalarVec && resultType.width > 1;
            for (size_t i = 0; i < cases.size(); ++i) {
                std::vector<std::string> args;
                args.reserve(parameterTypes.size());
                for (size_t p = 0; p < parameterTypes.size(); ++p) {
                    args.push_back(valueWgslImpl(cases[i].inputs[p], parameterTypes[p]));
                }
                const std::string expr = exprBuilder(args);
                if (isVec) {
                    for (int e = 0; e < resultType.width; ++e) {
                        out << abstractIntSnippet(expr, i, "[" + std::to_string(e) + "]");
                    }
                } else {
                    out << abstractIntSnippet(expr, i, "");
                }
            }
            out << "}\n";
            return out.str();
        }
        if (isAbstractFloatResult(resultType)) {
            // Abstract-float result: materialize each value via the f64 low/high split protocol.
            out << wgslAbstractIntOutputs(resultType, cases.size()) << "\n";
            out << "@compute @workgroup_size(1)\nfn main() {\n";
            const bool isVec = resultType.form == TypeForm::ScalarVec && resultType.width > 1;
            for (size_t i = 0; i < cases.size(); ++i) {
                std::vector<std::string> args;
                args.reserve(parameterTypes.size());
                for (size_t p = 0; p < parameterTypes.size(); ++p) {
                    args.push_back(valueWgslImpl(cases[i].inputs[p], parameterTypes[p]));
                }
                const std::string expr = exprBuilder(args);
                if (resultType.form == TypeForm::Matrix) {
                    // Matrix result: column-major flat output slots. Input element m[c][r], output
                    // slot index (c*rows + r) into the flat array<AF, cols*rows>.
                    int slot = 0;
                    for (int c = 0; c < resultType.cols; ++c) {
                        for (int r = 0; r < resultType.width; ++r) {
                            const std::string inAcc =
                                "[" + std::to_string(c) + "][" + std::to_string(r) + "]";
                            const std::string outAcc = "[" + std::to_string(slot) + "]";
                            out << abstractFloatSnippet(expr, i, inAcc, outAcc);
                            ++slot;
                        }
                    }
                } else if (isVec) {
                    for (int e = 0; e < resultType.width; ++e) {
                        const std::string acc = "[" + std::to_string(e) + "]";
                        out << abstractFloatSnippet(expr, i, acc, acc);
                    }
                } else {
                    out << abstractFloatSnippet(expr, i, "", "");
                }
            }
            out << "}\n";
            return out.str();
        }
        // Constant eval, 'direct' mode: assign each case's evaluated expression to the output.
        out << wgslOutputs(resultType, cases.size()) << "\n";
        out << "@compute @workgroup_size(1)\nfn main() {\n";
        for (size_t i = 0; i < cases.size(); ++i) {
            std::vector<std::string> args;
            args.reserve(parameterTypes.size());
            for (size_t p = 0; p < parameterTypes.size(); ++p) {
                args.push_back(valueWgslImpl(cases[i].inputs[p], parameterTypes[p]));
            }
            const std::string expr = exprBuilder(args);
            out << "  outputs[" << i << "].value = " << toStorage(resultType, expr) << ";\n";
        }
        out << "}\n";
        return out.str();
    }

    // Runtime eval (uniform / storage_r / storage_rw).
    std::vector<ExprType> storageParams;
    storageParams.reserve(parameterTypes.size());
    for (ExprType ty : parameterTypes) {
        storageParams.push_back(storageType(ty));
    }

    out << "struct Input {\n"
        << wgslMembers(storageParams, inputSource,
                       [](int i) { return "param" + std::to_string(i); })
        << "}\n\n";
    out << wgslOutputs(resultType, cases.size()) << "\n";
    out << wgslInputVar(inputSource, cases.size()) << "\n\n";

    TypeConversionHelpers helpers;
    std::vector<std::string> args;
    args.reserve(parameterTypes.size());
    for (size_t p = 0; p < parameterTypes.size(); ++p) {
        args.push_back(
            fromStorage(parameterTypes[p], "inputs[i].param" + std::to_string(p), helpers));
    }
    const std::string expr = toStorage(resultType, exprBuilder(args));
    if (!helpers.wgsl.empty()) {
        out << helpers.wgsl << "\n";
    }

    out << "@compute @workgroup_size(1)\nfn main() {\n"
        << "  for (var i = 0; i < " << cases.size() << "; i++) {\n"
        << "    outputs[i].value = " << expr << ";\n"
        << "  }\n"
        << "}\n";
    return out.str();
}

WGPUComputePipeline createComputePipelineAuto(GpuTest& t, const std::string& wgsl) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

// Packs scalar cases into vectors of the given width (packScalarsToVector).
std::vector<Case> packScalarsToVector(
    std::vector<ExprType>& parameterTypes,
    ExprType& resultType,
    const std::vector<Case>& cases,
    int vectorWidth) {
    std::vector<Case> packed;
    auto clampIdx = [&](size_t idx) { return idx < cases.size() ? idx : cases.size() - 1; };

    size_t caseIdx = 0;
    while (caseIdx < cases.size()) {
        Case pc;
        pc.inputs.resize(parameterTypes.size());
        for (size_t p = 0; p < parameterTypes.size(); ++p) {
            std::vector<Scalar> els;
            els.reserve(static_cast<size_t>(vectorWidth));
            for (int i = 0; i < vectorWidth; ++i) {
                const Case& src = cases[clampIdx(caseIdx + static_cast<size_t>(i))];
                els.push_back(src.inputs[p].elements[0]);
            }
            pc.inputs[p] = CaseValue::vec(std::move(els));
        }
        std::vector<Scalar> rels;
        rels.reserve(static_cast<size_t>(vectorWidth));
        bool anyAccept = false;
        for (int i = 0; i < vectorWidth; ++i) {
            const Case& src = cases[clampIdx(caseIdx + static_cast<size_t>(i))];
            rels.push_back(src.expected.elements[0]);
            anyAccept = anyAccept || !src.expectedAccept.empty();
        }
        pc.expected = CaseValue::vec(std::move(rels));
        if (anyAccept) {
            pc.expectedAccept.reserve(static_cast<size_t>(vectorWidth));
            for (int i = 0; i < vectorWidth; ++i) {
                const Case& src = cases[clampIdx(caseIdx + static_cast<size_t>(i))];
                if (!src.expectedAccept.empty()) {
                    pc.expectedAccept.push_back(src.expectedAccept[0]);
                } else {
                    // No acceptance override on this source: require exact match of its expected.
                    ExpectedElement ee;
                    ee.acceptBits.push_back(src.expected.elements[0].bits);
                    pc.expectedAccept.push_back(ee);
                }
            }
        }
        packed.push_back(std::move(pc));
        caseIdx += static_cast<size_t>(vectorWidth);
    }

    for (ExprType& ty : parameterTypes) {
        ty.width = vectorWidth;
    }
    resultType.width = vectorWidth;
    return packed;
}

std::string formatValue(const CaseValue& v) {
    std::ostringstream out;
    if (v.width > 1) {
        out << "(";
    }
    for (int i = 0; i < v.width; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << v.elements[static_cast<size_t>(i)].bits;
    }
    if (v.width > 1) {
        out << ")";
    }
    return out.str();
}

// Runs one batch of cases (all of which fit within the binding limits). When 'compoundOp' is
// non-empty the batch evaluates a compound assignment ('lhs op= rhs') instead of exprBuilder.
void submitBatch(
    GpuTest& t,
    const ExpressionBuilder& exprBuilder,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    const std::vector<Case>& cases,
    const std::string& compoundOp = "") {
    const std::string source =
        compoundOp.empty()
            ? buildShader(exprBuilder, parameterTypes, resultType, inputSource, cases)
            : buildCompoundShader(compoundOp, parameterTypes, resultType, inputSource, cases);
    WGPUComputePipeline pipeline = createComputePipelineAuto(t, source);

    // Output buffer: one Output (stride-padded) per case. Abstract-int/float results use the 2x u32
    // (low/high) struct encoding and have their own stride.
    const bool abstractIntResult = isAbstractIntResult(resultType);
    const bool abstractFloatResult = isAbstractFloatResult(resultType);
    const uint32_t outputStride = (abstractIntResult || abstractFloatResult)
                                      ? abstractIntStride(resultType)
                                      : structStrideImpl({resultType}, InputSource::StorageRW);
    const uint64_t outputBufferSize = alignUp(static_cast<uint32_t>(cases.size()) * outputStride, 4);
    WGPUBufferDescriptor obDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    obDesc.size = outputBufferSize;
    obDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer outputBuffer = t.createBufferTracked(obDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    std::vector<WGPUBindGroupEntry> entries;

    WGPUBuffer inputBuffer = nullptr;
    uint32_t caseStride = 0;
    if (inputSource != InputSource::Const) {
        caseStride = structStrideImpl(parameterTypes, inputSource);
        const uint32_t inputSize = alignUp(static_cast<uint32_t>(cases.size()) * caseStride, 4);
        std::vector<uint8_t> inputData(inputSize, 0);
        for (size_t caseIdx = 0; caseIdx < cases.size(); ++caseIdx) {
            const uint32_t base = static_cast<uint32_t>(caseIdx) * caseStride;
            structLayoutImpl(parameterTypes, inputSource, [&](const MemberInfoImpl& m) {
                const CaseValue& arg = cases[caseIdx].inputs[static_cast<size_t>(m.index)];
                // Walk the member's canonical scalar slots, writing each scalar at its WGSL byte
                // offset within the member (handles scalar/vector/matrix/array, with vec3 / matrix
                // column / array stride padding left zero).
                int slot = 0;
                const uint32_t eb = elementBytes(storageKind(m.type.scalarKind()));
                forEachStorageScalar(
                    m.type, inputSource, base + m.offset, slot,
                    [&](int idx, uint32_t byteOff, ScalarKind) {
                        const uint32_t bits = arg.elements[static_cast<size_t>(idx)].bits;
                        std::memcpy(&inputData[byteOff], &bits, eb);
                    });
            });
        }
        WGPUBufferUsage usage = WGPUBufferUsage_CopySrc |
                                (inputSource == InputSource::Uniform ? WGPUBufferUsage_Uniform
                                                                     : WGPUBufferUsage_Storage);
        inputBuffer = t.makeBufferWithContents(inputData.data(), inputData.size(), usage);
    }

    WGPUBindGroupEntry e0 = WGPU_BIND_GROUP_ENTRY_INIT;
    e0.binding = 0;
    e0.buffer = outputBuffer;
    e0.offset = 0;
    e0.size = outputBufferSize;
    entries.push_back(e0);
    if (inputBuffer != nullptr) {
        WGPUBindGroupEntry e1 = WGPU_BIND_GROUP_ENTRY_INIT;
        e1.binding = 1;
        e1.buffer = inputBuffer;
        e1.offset = 0;
        // Storage/uniform binding sizes must be a multiple of 4; the buffer itself is allocated
        // 4-aligned, so binding the rounded-up size stays within bounds (matters for f16, whose
        // case stride can be 2 bytes).
        e1.size = alignUp(static_cast<uint32_t>(cases.size()) * caseStride, 4);
        entries.push_back(e1);
    }

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    WGPUBindGroup group = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    // Read back and compare (bit-exact unless the case carries a per-element acceptance set).
    const ExprType result = resultType;
    const uint32_t stride = outputStride;
    const uint32_t resElemBytes = elementBytes(storageKind(resultType.scalarKind()));
    const int resSlots = scalarSlotCount(resultType);
    std::vector<Case> casesCopy = cases;

    if (abstractIntResult) {
        // Abstract-int result: each element occupies an 8-byte {low:u32, high:u32} slot at offset
        // e*8 within the case's output value. Reconstruct the signed 64-bit value (low ++ high in
        // little-endian order) and compare against the expected scalar's full 64-bit 'bits64'.
        const int aiSlots = resSlots;
        t.expectGPUBufferValuesPassCheck(
            outputBuffer,
            [stride, aiSlots, casesCopy](const uint8_t* data,
                                         size_t len) -> std::optional<std::string> {
                std::ostringstream errs;
                int failures = 0;
                for (size_t caseIdx = 0; caseIdx < casesCopy.size(); ++caseIdx) {
                    const uint32_t off = static_cast<uint32_t>(caseIdx) * stride;
                    const CaseValue& exp = casesCopy[caseIdx].expected;
                    if (exp.width != aiSlots) {
                        return std::string("abstract-int result width mismatch");
                    }
                    bool matched = true;
                    for (int e = 0; matched && e < aiSlots; ++e) {
                        const uint32_t elemOff = off + static_cast<uint32_t>(e) * 8u;
                        if (static_cast<size_t>(elemOff) + 8u > len) {
                            return std::string("readback buffer too small");
                        }
                        uint32_t low = 0;
                        uint32_t high = 0;
                        std::memcpy(&low, data + elemOff, 4);
                        std::memcpy(&high, data + elemOff + 4, 4);
                        const int64_t got = static_cast<int64_t>(
                            (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low));
                        if (got != exp.elements[static_cast<size_t>(e)].bits64) {
                            matched = false;
                        }
                    }
                    if (!matched) {
                        if (failures < 8) {
                            errs << "\ncase " << caseIdx << " abstract-int mismatch";
                        }
                        ++failures;
                    }
                }
                if (failures > 0) {
                    return "expression mismatch (" + std::to_string(failures) + " failures):" +
                           errs.str();
                }
                return std::nullopt;
            },
            0,
            outputBufferSize);
        return;
    }

    if (abstractFloatResult) {
        // Abstract-float result: each element occupies an 8-byte {low:u32, high:u32} slot at offset
        // e*8 within the case's output value, packing the f64 bits (high = upper 32 bits, low =
        // lower 32 bits). Reconstruct the f64 and accept iff it lies in the per-element acceptance
        // interval (carried in expectedAccept, floatWidth==64). The unbounded interval accepts any
        // value (NaN/inf included). The snippet applies FTZ, so subnormal results read back as 0.
        const int afSlots = resSlots;
        t.expectGPUBufferValuesPassCheck(
            outputBuffer,
            [stride, afSlots, casesCopy](const uint8_t* data,
                                         size_t len) -> std::optional<std::string> {
                std::ostringstream errs;
                int failures = 0;
                for (size_t caseIdx = 0; caseIdx < casesCopy.size(); ++caseIdx) {
                    const uint32_t off = static_cast<uint32_t>(caseIdx) * stride;
                    const std::vector<ExpectedElement>& acc = casesCopy[caseIdx].expectedAccept;
                    if (static_cast<int>(acc.size()) != afSlots) {
                        return std::string("abstract-float result width mismatch");
                    }
                    bool matched = true;
                    for (int e = 0; matched && e < afSlots; ++e) {
                        const uint32_t elemOff = off + static_cast<uint32_t>(e) * 8u;
                        if (static_cast<size_t>(elemOff) + 8u > len) {
                            return std::string("readback buffer too small");
                        }
                        uint32_t low = 0;
                        uint32_t high = 0;
                        std::memcpy(&low, data + elemOff, 4);
                        std::memcpy(&high, data + elemOff + 4, 4);
                        const uint64_t bits =
                            (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);
                        double got;
                        std::memcpy(&got, &bits, 8);
                        const ExpectedElement& ee = acc[static_cast<size_t>(e)];
                        if (ee.unbounded) {
                            continue;
                        }
                        bool inAny = !std::isnan(got) && got >= ee.lo && got <= ee.hi;
                        for (const auto& iv : ee.extraIntervals) {
                            if (!std::isnan(got) && got >= iv.first && got <= iv.second) {
                                inAny = true;
                                break;
                            }
                        }
                        if (!inAny) {
                            matched = false;
                        }
                    }
                    if (!matched) {
                        if (failures < 8) {
                            errs << "\ncase " << caseIdx << " abstract-float interval mismatch";
                        }
                        ++failures;
                    }
                }
                if (failures > 0) {
                    return "expression mismatch (" + std::to_string(failures) + " failures):" +
                           errs.str();
                }
                return std::nullopt;
            },
            0,
            outputBufferSize);
        return;
    }

    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [result, stride, resElemBytes, resSlots, casesCopy](
            const uint8_t* data, size_t len) -> std::optional<std::string> {
            std::ostringstream errs;
            int failures = 0;
            for (size_t caseIdx = 0; caseIdx < casesCopy.size(); ++caseIdx) {
                const uint32_t off = static_cast<uint32_t>(caseIdx) * stride;
                CaseValue got;
                got.width = resSlots;
                got.elements.resize(static_cast<size_t>(resSlots));
                bool overflow = false;
                // Walk the result value's canonical scalar slots (handles matrix column / vec3
                // padding) and read each scalar from its WGSL byte offset within the output value.
                int slot = 0;
                const ScalarKind resStoreKind = storageKind(result.scalarKind());
                forEachStorageScalar(
                    result, InputSource::StorageRW, off, slot,
                    [&](int idx, uint32_t byteOff, ScalarKind) {
                        if (static_cast<size_t>(byteOff) + resElemBytes > len) {
                            overflow = true;
                            return;
                        }
                        uint32_t bits = 0;
                        std::memcpy(&bits, data + byteOff, resElemBytes);
                        got.elements[static_cast<size_t>(idx)] = Scalar{resStoreKind, bits};
                    });
                if (overflow) {
                    return std::string("readback buffer too small");
                }
                const CaseValue& exp = casesCopy[caseIdx].expected;
                const std::vector<ExpectedElement>& acc = casesCopy[caseIdx].expectedAccept;
                bool matched = exp.width == got.width;
                if (matched && !acc.empty()) {
                    // Per-element acceptance: 'any' passes; otherwise the read-back bits must equal
                    // one of acceptBits, with optional any-NaN acceptance for float results.
                    for (int e = 0; matched && e < got.width; ++e) {
                        const ExpectedElement& ee = acc[static_cast<size_t>(e)];
                        const uint32_t gb = got.elements[static_cast<size_t>(e)].bits;
                        if (ee.any) {
                            continue;
                        }
                        if (ee.interval) {
                            if (ee.unbounded) {
                                // The unbounded interval accepts any bit pattern (NaN/inf included).
                                continue;
                            }
                            // Decode the read-back element as a float and accept iff it lies
                            // within the inclusive acceptance interval. NaN never matches.
                            double v;
                            if (ee.floatWidth == 16) {
                                v = f16BitsToDouble(static_cast<uint16_t>(gb & 0xFFFFu));
                            } else {
                                float f;
                                std::memcpy(&f, &gb, 4);
                                v = static_cast<double>(f);
                            }
                            bool inAny = !std::isnan(v) && v >= ee.lo && v <= ee.hi;
                            for (const auto& iv : ee.extraIntervals) {
                                if (!std::isnan(v) && v >= iv.first && v <= iv.second) {
                                    inAny = true;
                                    break;
                                }
                            }
                            if (!inAny) {
                                matched = false;
                            }
                            continue;
                        }
                        bool ok = false;
                        for (uint32_t ab : ee.acceptBits) {
                            const uint32_t mask =
                                ee.floatWidth == 16 ? 0xFFFFu : 0xFFFFFFFFu;
                            if ((gb & mask) == (ab & mask)) {
                                ok = true;
                                break;
                            }
                        }
                        if (!ok && ee.anyNaN) {
                            // Accept any NaN bit pattern at the result element width.
                            if (ee.floatWidth == 16) {
                                const uint32_t exp16 = (gb >> 10) & 0x1Fu;
                                const uint32_t mant16 = gb & 0x3FFu;
                                ok = exp16 == 0x1Fu && mant16 != 0;
                            } else {
                                const uint32_t exp32 = (gb >> 23) & 0xFFu;
                                const uint32_t mant32 = gb & 0x7FFFFFu;
                                ok = exp32 == 0xFFu && mant32 != 0;
                            }
                        }
                        if (!ok) {
                            matched = false;
                        }
                    }
                } else {
                    for (int e = 0; matched && e < exp.width; ++e) {
                        if (exp.elements[static_cast<size_t>(e)].bits !=
                            got.elements[static_cast<size_t>(e)].bits) {
                            matched = false;
                        }
                    }
                }
                if (!matched) {
                    if (failures < 8) {
                        errs << "\ninput " << formatValue(casesCopy[caseIdx].inputs.empty()
                                                               ? CaseValue(Scalar{})
                                                               : casesCopy[caseIdx].inputs[0])
                             << " returned " << formatValue(got) << " expected "
                             << formatValue(exp);
                    }
                    ++failures;
                }
            }
            if (failures > 0) {
                return "expression mismatch (" + std::to_string(failures) + " failures):" +
                       errs.str();
            }
            return std::nullopt;
        },
        0,
        outputBufferSize);
}

} // namespace

uint32_t align(uint32_t n, uint32_t alignment) {
    return alignUp(n, alignment);
}

SizeAlign sizeAndAlignmentOf(const ExprType& ty, InputSource source) {
    // Delegate to the file-private implementation (struct names differ; copy fields).
    const SizeAlignImpl sa = sizeAndAlignmentOfImpl(ty, source);
    return SizeAlign{sa.size, sa.alignment};
}

StructLayoutResult structLayout(
    const std::vector<ExprType>& members,
    InputSource source,
    const std::function<void(const MemberLayout&)>& cb) {
    std::function<void(const MemberInfoImpl&)> innerCb;
    if (cb) {
        innerCb = [&](const MemberInfoImpl& m) {
            cb(MemberLayout{m.index, m.type, m.size, m.alignment, m.offset});
        };
    }
    const StructLayoutImpl sl = structLayoutImpl(members, source, innerCb);
    return StructLayoutResult{sl.size, sl.stride, sl.alignment};
}

uint32_t structStride(const std::vector<ExprType>& members, InputSource source) {
    return structStrideImpl(members, source);
}

std::string wgslTypeName(const ExprType& ty) {
    return typeName(ty);
}

std::string storageWgslTypeName(const ExprType& ty) {
    return storageTypeName(ty);
}

std::string valueWgsl(const CaseValue& v, const ExprType& ty) {
    return valueWgslImpl(v, ty);
}

void copyValueTo(
    const CaseValue& v,
    const ExprType& ty,
    InputSource source,
    std::vector<uint8_t>& data,
    uint32_t offset) {
    const uint32_t eb = elementBytes(storageKind(ty.scalarKind()));
    int slot = 0;
    forEachStorageScalar(
        ty, source, offset, slot, [&](int idx, uint32_t byteOff, ScalarKind) {
            const uint32_t bits = v.elements[static_cast<size_t>(idx)].bits;
            std::memcpy(&data[byteOff], &bits, eb);
        });
}

bool readValueFrom(
    const ExprType& ty,
    InputSource source,
    const uint8_t* data,
    size_t len,
    uint32_t offset,
    CaseValue& out) {
    const ScalarKind storeKind = storageKind(ty.scalarKind());
    const uint32_t eb = elementBytes(storeKind);
    const int slots = scalarSlotCount(ty);
    out.width = slots;
    out.elements.assign(static_cast<size_t>(slots), Scalar{storeKind, 0});
    bool overflow = false;
    int slot = 0;
    forEachStorageScalar(
        ty, source, offset, slot, [&](int idx, uint32_t byteOff, ScalarKind) {
            if (static_cast<size_t>(byteOff) + eb > len) {
                overflow = true;
                return;
            }
            uint32_t bits = 0;
            std::memcpy(&bits, data + byteOff, eb);
            out.elements[static_cast<size_t>(idx)] = Scalar{storeKind, bits};
        });
    return !overflow;
}

const char* inputSourceName(InputSource source) {
    switch (source) {
        case InputSource::Const:
            return "const";
        case InputSource::Uniform:
            return "uniform";
        case InputSource::StorageR:
            return "storage_r";
        case InputSource::StorageRW:
            return "storage_rw";
    }
    std::abort();
}

InputSource inputSourceFromParam(const std::string& value) {
    if (value == "const") {
        return InputSource::Const;
    }
    if (value == "uniform") {
        return InputSource::Uniform;
    }
    if (value == "storage_r") {
        return InputSource::StorageR;
    }
    if (value == "storage_rw") {
        return InputSource::StorageRW;
    }
    std::abort();
}

ExpressionBuilder builtin(const std::string& name) {
    return [name](const std::vector<std::string>& values) {
        std::ostringstream out;
        out << name << "(";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << values[i];
        }
        out << ")";
        return out.str();
    };
}

ExpressionBuilder binaryOp(const std::string& op) {
    return [op](const std::vector<std::string>& values) {
        std::ostringstream out;
        out << "(";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                out << op;
            }
            out << "(" << values[i] << ")";
        }
        out << ")";
        return out.str();
    };
}

ExpressionBuilder prefixOp(const std::string& op) {
    return [op](const std::vector<std::string>& values) {
        return op + "(" + values[0] + ")";
    };
}

ExpressionBuilder conversion(const std::string& typeName) {
    return [typeName](const std::vector<std::string>& values) {
        return typeName + "(" + values[0] + ")";
    };
}

namespace {

// Shared batching/dispatch core for run() and runCompound(). When 'compoundOp' is empty a plain
// expression is evaluated via 'exprBuilder'; otherwise a compound assignment is evaluated.
void runImpl(
    GpuTest& t,
    const ExpressionBuilder& exprBuilder,
    const std::string& compoundOp,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    int vectorize,
    const std::vector<Case>& cases) {
    std::vector<ExprType> params = parameterTypes;
    ExprType result = resultType;
    std::vector<Case> work = cases;

    if (vectorize != 0) {
        work = packScalarsToVector(params, result, work, vectorize);
    }

    // Determine cases-per-batch to keep within binding limits.
    WGPULimits limits = t.getLimits();
    size_t casesPerBatch;
    switch (inputSource) {
        case InputSource::Const:
            casesPerBatch = 32;
            break;
        case InputSource::Uniform: {
            const uint32_t stride = structStride(params, inputSource);
            const uint64_t maxUniform = limits.maxUniformBufferBindingSize;
            const uint64_t cap = maxUniform < 2048 ? maxUniform : 2048;
            casesPerBatch = static_cast<size_t>(cap / stride);
            if (casesPerBatch == 0) {
                casesPerBatch = 1;
            }
            break;
        }
        case InputSource::StorageR:
        case InputSource::StorageRW: {
            const uint32_t stride = structStride(params, inputSource);
            casesPerBatch = static_cast<size_t>(limits.maxStorageBufferBindingSize / stride);
            if (casesPerBatch == 0) {
                casesPerBatch = 1;
            }
            break;
        }
        default:
            casesPerBatch = 32;
            break;
    }

    for (size_t i = 0; i < work.size(); i += casesPerBatch) {
        const size_t end = std::min(i + casesPerBatch, work.size());
        std::vector<Case> batch(work.begin() + static_cast<std::ptrdiff_t>(i),
                                work.begin() + static_cast<std::ptrdiff_t>(end));
        submitBatch(t, exprBuilder, params, result, inputSource, batch, compoundOp);
    }
}

} // namespace

void run(
    GpuTest& t,
    const ExpressionBuilder& exprBuilder,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    int vectorize,
    const std::vector<Case>& cases) {
    runImpl(t, exprBuilder, "", parameterTypes, resultType, inputSource, vectorize, cases);
}

void runCompound(
    GpuTest& t,
    const std::string& compoundOp,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    int vectorize,
    const std::vector<Case>& cases) {
    runImpl(t, builtin("unused"), compoundOp, parameterTypes, resultType, inputSource, vectorize,
            cases);
}

} // namespace expression
} // namespace cts
