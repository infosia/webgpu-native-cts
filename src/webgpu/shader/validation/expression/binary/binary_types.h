// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/util/conversion.ts and
// src/webgpu/shader/validation/expression/binary/result_type.ts @
// b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Minimal faithful port of the conversion.ts Type model used by the binary
// expression validation specs: the scalar/vector Type abstraction, the
// kAllScalarsAndVectors / kConcreteNumericScalarsAndVectors ordered tables
// (keyed by Type.toString(), so vectors stringify as `vecN<elementType>`),
// scalarTypeOf / numElementsOf / isAbstractType / isIntegerType / isFloatType /
// isConvertible / concreteTypeOf, the `create(N).wgsl()` value spelling, and the
// resultType() helper from result_type.ts.

#pragma once

#include <string>
#include <vector>

#include "cts/test.h"

namespace cts::shader_validation::binary {

// Scalar kinds used by the validation tests (mirrors conversion.ts ScalarKind
// subset). The string spelling matches ScalarType.toString() == kind.
enum class ScalarKind {
    Bool,
    AbstractInt,
    AbstractFloat,
    F32,
    F16,
    I32,
    U32,
};

inline std::string scalarKindString(ScalarKind k) {
    switch (k) {
        case ScalarKind::Bool: return "bool";
        case ScalarKind::AbstractInt: return "abstract-int";
        case ScalarKind::AbstractFloat: return "abstract-float";
        case ScalarKind::F32: return "f32";
        case ScalarKind::F16: return "f16";
        case ScalarKind::I32: return "i32";
        case ScalarKind::U32: return "u32";
    }
    return "";
}

// A Type is either a scalar (width 0 marker) or a vector of `width` (2..4)
// elements of a scalar kind. Mirrors ScalarType / VectorType.
struct Type {
    ScalarKind kind = ScalarKind::Bool;
    int width = 0;  // 0 => scalar, 2/3/4 => vector

    bool isScalar() const { return width == 0; }
    bool isVector() const { return width != 0; }

    bool operator==(const Type& o) const { return kind == o.kind && width == o.width; }
    bool operator!=(const Type& o) const { return !(*this == o); }

    // Mirrors objectsToRecord key == Type.toString().
    std::string toString() const {
        if (isScalar()) {
            return scalarKindString(kind);
        }
        return "vec" + std::to_string(width) + "<" + scalarKindString(kind) + ">";
    }
};

inline Type scalar(ScalarKind k) { return Type{k, 0}; }
inline Type vec(int width, ScalarKind k) { return Type{k, width}; }

// scalarTypeOf: element scalar type.
inline Type scalarTypeOf(const Type& ty) { return scalar(ty.kind); }

// numElementsOf.
inline int numElementsOf(const Type& ty) { return ty.isScalar() ? 1 : ty.width; }

// isAbstractType (scalar-only per upstream).
inline bool isAbstractType(const Type& ty) {
    return ty.isScalar() &&
           (ty.kind == ScalarKind::AbstractInt || ty.kind == ScalarKind::AbstractFloat);
}

// isFloatType / isIntegerType (scalar-only per upstream).
inline bool isFloatType(const Type& ty) {
    return ty.isScalar() &&
           (ty.kind == ScalarKind::AbstractFloat || ty.kind == ScalarKind::F32 ||
            ty.kind == ScalarKind::F16);
}
inline bool isIntegerType(const Type& ty) {
    return ty.isScalar() &&
           (ty.kind == ScalarKind::AbstractInt || ty.kind == ScalarKind::I32 ||
            ty.kind == ScalarKind::U32);
}

// isConvertible(src, dst) — faithful port (conversion.ts). Shapes (scalar/vecN)
// must match; only abstract-float/abstract-int element kinds widen.
inline bool isConvertible(const Type& src, const Type& dst) {
    if (src == dst) {
        return true;
    }
    // shapeOf: scalar vs vecN
    if (src.width != dst.width) {
        return false;
    }
    const ScalarKind elSrc = src.kind;
    const ScalarKind elDst = dst.kind;
    switch (elSrc) {
        case ScalarKind::AbstractFloat:
            switch (elDst) {
                case ScalarKind::AbstractFloat:
                case ScalarKind::F16:
                case ScalarKind::F32:
                    return true;
                default:
                    return false;
            }
        case ScalarKind::AbstractInt:
            switch (elDst) {
                case ScalarKind::AbstractInt:
                case ScalarKind::AbstractFloat:
                case ScalarKind::F16:
                case ScalarKind::F32:
                case ScalarKind::U32:
                case ScalarKind::I32:
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

// concreteTypeOf (no allowedScalarTypes): abstract-int -> i32, abstract-float -> f32.
inline Type concreteTypeOf(const Type& ty) {
    if (ty.kind == ScalarKind::AbstractInt) {
        return Type{ScalarKind::I32, ty.width};
    }
    if (ty.kind == ScalarKind::AbstractFloat) {
        return Type{ScalarKind::F32, ty.width};
    }
    return ty;
}

// resultType (result_type.ts) — result of a binary arithmetic op, or nullptr.
// Returns whether a result exists (out) and the result Type.
inline bool resultType(const Type& lhs, const Type& rhs, bool canConvertScalarToVector,
                       Type& out) {
    if (lhs == rhs) {
        out = lhs;
        return true;
    }

    if (lhs.isVector() && rhs.isVector()) {
        if (lhs.width != rhs.width) {
            return false;
        }
        Type elem;
        if (resultType(scalarTypeOf(lhs), scalarTypeOf(rhs), canConvertScalarToVector, elem)) {
            out = vec(lhs.width, elem.kind);
            return true;
        }
        return false;
    }

    if (canConvertScalarToVector) {
        if (lhs.isVector() && !rhs.isVector()) {
            Type elem;
            if (resultType(scalarTypeOf(lhs), rhs, canConvertScalarToVector, elem)) {
                out = vec(lhs.width, elem.kind);
                return true;
            }
            return false;
        }
        if (!lhs.isVector() && rhs.isVector()) {
            Type elem;
            if (resultType(lhs, scalarTypeOf(rhs), canConvertScalarToVector, elem)) {
                out = vec(rhs.width, elem.kind);
                return true;
            }
            return false;
        }
    }

    if (isAbstractType(lhs) || isAbstractType(rhs)) {
        if (isConvertible(lhs, rhs)) {
            out = rhs;
            return true;
        }
        if (isConvertible(rhs, lhs)) {
            out = lhs;
            return true;
        }
    }
    return false;
}

// ---- Value spellings (create(N).wgsl()) ------------------------------------
// Mirrors ScalarValue.wgsl(): bool -> "false"/"true", abstract-int -> "N",
// abstract-float -> withPoint(N), f32 -> "N.0f", f16 -> "N.0h", i32 -> "i32(N)",
// u32 -> "Nu". withPoint adds ".0" to integers. VectorValue.wgsl() ->
// "vecN(el0, el1, ...)".
inline std::string withPoint(long long v) { return std::to_string(v) + ".0"; }

inline std::string scalarValueWgsl(ScalarKind k, long long n) {
    switch (k) {
        case ScalarKind::Bool: return n != 0 ? "true" : "false";
        case ScalarKind::AbstractInt: return std::to_string(n);
        case ScalarKind::AbstractFloat: return withPoint(n);
        case ScalarKind::F32: return withPoint(n) + "f";
        case ScalarKind::F16: return withPoint(n) + "h";
        case ScalarKind::I32: return "i32(" + std::to_string(n) + ")";
        case ScalarKind::U32: return std::to_string(n) + "u";
    }
    return "";
}

// create(N).wgsl() for a scalar or vector type filled with N.
inline std::string createWgsl(const Type& ty, long long n) {
    if (ty.isScalar()) {
        return scalarValueWgsl(ty.kind, n);
    }
    std::string els;
    const std::string el = scalarValueWgsl(ty.kind, n);
    for (int i = 0; i < ty.width; ++i) {
        if (i) {
            els += ", ";
        }
        els += el;
    }
    return "vec" + std::to_string(ty.width) + "(" + els + ")";
}

// ---- Ordered type tables (keys == Type.toString()) -------------------------
// kAllScalarsAndVectors = kAllBoolScalarsAndVectors ++ kAllNumericScalarsAndVectors,
// where kAllNumericScalarsAndVectors = kConvertableToFloatScalarsAndVectors ++
// kConcreteIntegerScalarsAndVectors. Order preserved exactly.
inline const std::vector<Type>& kAllScalarsAndVectors() {
    static const std::vector<Type> v = {
        // kAllBoolScalarsAndVectors: bool, vec2b, vec3b, vec4b
        scalar(ScalarKind::Bool), vec(2, ScalarKind::Bool), vec(3, ScalarKind::Bool),
        vec(4, ScalarKind::Bool),
        // kConvertableToFloatScalarsAndVectors:
        //   abstractInt, kFloatScalars(af,f32,f16), kConvertableToFloatVectors
        scalar(ScalarKind::AbstractInt),
        scalar(ScalarKind::AbstractFloat), scalar(ScalarKind::F32), scalar(ScalarKind::F16),
        //   kConvertableToFloatVectors: vec2ai,vec2af,vec2f,vec2h, vec3..., vec4...
        vec(2, ScalarKind::AbstractInt), vec(2, ScalarKind::AbstractFloat),
        vec(2, ScalarKind::F32), vec(2, ScalarKind::F16),
        vec(3, ScalarKind::AbstractInt), vec(3, ScalarKind::AbstractFloat),
        vec(3, ScalarKind::F32), vec(3, ScalarKind::F16),
        vec(4, ScalarKind::AbstractInt), vec(4, ScalarKind::AbstractFloat),
        vec(4, ScalarKind::F32), vec(4, ScalarKind::F16),
        // kConcreteIntegerScalarsAndVectors: i32,vec2i,vec3i,vec4i, u32,vec2u,vec3u,vec4u
        scalar(ScalarKind::I32), vec(2, ScalarKind::I32), vec(3, ScalarKind::I32),
        vec(4, ScalarKind::I32),
        scalar(ScalarKind::U32), vec(2, ScalarKind::U32), vec(3, ScalarKind::U32),
        vec(4, ScalarKind::U32),
    };
    return v;
}

// kConcreteNumericScalarsAndVectors = kConcreteIntegerScalarsAndVectors ++
// kConcreteF16ScalarsAndVectors ++ kConcreteF32ScalarsAndVectors.
inline const std::vector<Type>& kConcreteNumericScalarsAndVectors() {
    static const std::vector<Type> v = {
        // kConcreteIntegerScalarsAndVectors
        scalar(ScalarKind::I32), vec(2, ScalarKind::I32), vec(3, ScalarKind::I32),
        vec(4, ScalarKind::I32),
        scalar(ScalarKind::U32), vec(2, ScalarKind::U32), vec(3, ScalarKind::U32),
        vec(4, ScalarKind::U32),
        // kConcreteF16ScalarsAndVectors: f16,vec2h,vec3h,vec4h
        scalar(ScalarKind::F16), vec(2, ScalarKind::F16), vec(3, ScalarKind::F16),
        vec(4, ScalarKind::F16),
        // kConcreteF32ScalarsAndVectors: f32,vec2f,vec3f,vec4f
        scalar(ScalarKind::F32), vec(2, ScalarKind::F32), vec(3, ScalarKind::F32),
        vec(4, ScalarKind::F32),
    };
    return v;
}

// Lookup a Type by its toString() key (for param reconstruction). Searches the
// union of both tables (kConcreteNumeric is a subset of kAll, so kAll suffices).
inline Type typeByName(const std::string& name) {
    for (const Type& ty : kAllScalarsAndVectors()) {
        if (ty.toString() == name) {
            return ty;
        }
    }
    // abstract types and concrete forms are all in kAll; fall back to scalar.
    return scalar(ScalarKind::Bool);
}

inline std::vector<cts::Value> typeNames(const std::vector<Type>& table) {
    std::vector<cts::Value> values;
    for (const Type& ty : table) {
        values.emplace_back(ty.toString());
    }
    return values;
}

// Names filtered to skip vec3*/vec4* (the RHS reduction used by several tests).
inline std::vector<cts::Value> typeNamesNoVec34(const std::vector<Type>& table) {
    std::vector<cts::Value> values;
    for (const Type& ty : table) {
        const std::string name = ty.toString();
        if (name.rfind("vec3", 0) == 0 || name.rfind("vec4", 0) == 0) {
            continue;
        }
        values.emplace_back(name);
    }
    return values;
}

}  // namespace cts::shader_validation::binary
