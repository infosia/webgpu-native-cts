// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/overload_resolution.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for implicit conversions and overload resolution.
//
// Port notes:
//   * implicit_conversions keys cases by their scalar name; the {expr, valid,
//     f16} triple is reconstructed in the body from a local lookup.
//   * overload_resolution iterates kAllNumericScalarsAndVectors. Upstream builds a
//     Record keyed by each Type's toString() (e.g. 'f32', 'vec2<abstract-int>'),
//     so the arg1/arg2 case-query names ARE those toString() strings. The exact
//     ordered list (24 types) and each type's create(N).wgsl() value spelling,
//     requiresF16(), byte-size, and vector-ness are reproduced here. isConvertible
//     is ported verbatim from util/conversion.ts. NOTE: upstream's filter compares
//     `t1.size !== t2.size` for two vectors, where `.size` is the BYTE size
//     (elementBytes * width), NOT the element count — so e.g. vec4<f16> (8 bytes)
//     and vec2<f32> (8 bytes) are NOT filtered apart. This byte-size semantics is
//     preserved exactly.

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,overload_resolution",
    "Validation tests for implicit conversions and overload resolution");

// ---------------------------------------------------------------------------
// implicit_conversions
// ---------------------------------------------------------------------------
struct ImplicitCase {
    const char* name;
    const char* expr;
    bool valid;
    bool f16;
};

// Mirrors upstream kImplicitConversionCases (object key order preserved).
static const std::vector<ImplicitCase>& kImplicitConversionCases() {
    static const std::vector<ImplicitCase> v = {
        {"absint_to_bool", "any(1)", false, false},
        {"absint_to_u32", "1 == 1u", true, false},
        {"absint_to_i32", "1 == 1i", true, false},
        {"absint_to_f32", "1 == 1f", true, false},
        {"absint_to_f16", "1 == 1h", true, true},
        {"absfloat_to_bool", "any(1.0)", false, false},
        {"absfloat_to_u32", "1.0 == 1u", false, false},
        {"absfloat_to_i32", "1.0 == 1i", false, false},
        {"absfloat_to_f32", "1.0 == 1f", true, false},
        {"absfloat_to_f16", "1.0 == 1h", true, true},
        {"vector_absint_to_bool", "any(vec2(1))", false, false},
        {"vector_absint_to_u32", "all(vec2(1) == vec2u(1u))", true, false},
        {"vector_absint_to_i32", "all(vec3(1) == vec3i(1i))", true, false},
        {"vector_absint_to_f32", "all(vec4(1) == vec4f(1f))", true, false},
        {"vector_absint_to_f16", "all(vec2(1) == vec2h(1h))", true, true},
        {"vector_absfloat_to_bool", "any(vec2(1.0))", false, false},
        {"vector_absfloat_to_u32", "all(vec2(1.0) == vec2u(1u))", false, false},
        {"vector_absfloat_to_i32", "all(vec3(1.0) == vec2i(1i))", false, false},
        {"vector_absfloat_to_f32", "all(vec4(1.0) == vec4f(1f))", true, false},
        {"vector_absfloat_to_f16", "all(vec2(1.0) == vec2h(1h))", true, true},
        {"vector_swizzle_integer", "vec2(1).x == 1i", true, false},
        {"vector_swizzle_float", "vec2(1).y == 1f", true, false},
        {"vector_default_ctor_integer", "all(vec3().xy == vec2i())", true, false},
        {"vector_default_ctor_abstract", "all(vec3().xy == vec2())", true, false},
        {"vector_swizzle_abstract", "vec4(1f).x == 1", true, false},
        {"vector_abstract_to_integer", "all(vec4(1) == vec4i(1))", true, false},
        {"vector_wrong_result_i32", "vec2(1,2f).x == 1i", false, false},
        {"vector_wrong_result_f32", "vec2(1,2i).y == 2f", false, false},
        {"vector_wrong_result_splat", "vec2(1.0).x == 1i", false, false},
        {"array_absint_to_bool", "any(array(1)[0])", false, false},
        {"array_absint_to_u32", "array(1)[0] == array<u32,1>(1u)[0]", true, false},
        {"array_absint_to_i32", "array(1)[0] == array<i32,1>(1i)[0]", true, false},
        {"array_absint_to_f32", "array(1)[0] == array<f32,1>(1f)[0]", true, false},
        {"array_absint_to_f16", "array(1)[0] == array<f16,1>(1h)[0]", true, true},
        {"array_absfloat_to_bool", "any(array(1.0)[0])", false, false},
        {"array_absfloat_to_u32", "array(1.0)[0] == array<u32,1>(1u)[0]", false, false},
        {"array_absfloat_to_i32", "array(1.0)[0] == array<i32,1>(1i)[0]", false, false},
        {"array_absfloat_to_f32", "array(1.0)[0] == array<f32,1>(1f)[0]", true, false},
        {"array_absfloat_to_f16", "array(1.0)[0] == array<f16,1>(1h)[0]", true, true},
        {"mat2x2_index_absint", "all(mat2x2(1,2,3,4)[0] == vec2(1,2))", true, false},
        {"mat2x2_index_absfloat", "all(mat2x2(1,2,3,4)[1] == vec2(3.0,4.0))", true, false},
        {"mat2x2_index_float", "all(mat2x2(0,0,0,0)[1] == vec2f())", true, false},
        {"mat2x2_wrong_result", "all(mat2x2(0f,0,0,0)[0] == vec2h())", false, true},
    };
    return v;
}

static std::vector<Value> implicitCaseNames() {
    std::vector<Value> values;
    for (const ImplicitCase& c : kImplicitConversionCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ImplicitCase& findImplicitCase(const std::string& name) {
    for (const ImplicitCase& c : kImplicitConversionCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ImplicitCase dummy{"", "", false, false};
    return dummy;
}

CTS_TEST(g, "implicit_conversions")
    .desc("Test implicit conversions")
    .params([](ParamsBuilder u) { return u.combine("case", implicitCaseNames()); })
    .fn([](ShaderValidationTest& t) {
        const ImplicitCase& c = findImplicitCase(t.param<std::string>("case"));
        const std::string code =
            (c.f16 ? std::string("enable f16;") : std::string("")) + "\n    const_assert " +
            c.expr + ";";
        t.expectCompileResult(c.valid, code);
    });

// ---------------------------------------------------------------------------
// overload_resolution: numeric type model
// ---------------------------------------------------------------------------
// Scalar element kinds (mirrors ScalarKind for the numeric types used here).
enum class ScalarKind {
    AbstractInt,
    AbstractFloat,
    F32,
    F16,
    I32,
    U32,
};

// Element byte sizes (ScalarType._size): abstract* = 8, f32/i32/u32 = 4, f16 = 2.
static int elementBytes(ScalarKind k) {
    switch (k) {
        case ScalarKind::AbstractInt:
        case ScalarKind::AbstractFloat:
            return 8;
        case ScalarKind::F32:
        case ScalarKind::I32:
        case ScalarKind::U32:
            return 4;
        case ScalarKind::F16:
            return 2;
    }
    return 0;
}

static bool kindRequiresF16(ScalarKind k) {
    return k == ScalarKind::F16;
}

// Scalar value wgsl() for an integer magnitude `n` (50 or 100 here; no fraction).
static std::string scalarValueWgsl(ScalarKind k, int n) {
    const std::string num = std::to_string(n);
    switch (k) {
        case ScalarKind::AbstractInt:
            return num;                // AbstractIntValue.wgsl() -> "50"
        case ScalarKind::AbstractFloat:
            return num + ".0";         // withPoint(50) -> "50.0"
        case ScalarKind::F32:
            return num + ".0f";        // withPoint(50)+"f"
        case ScalarKind::F16:
            return num + ".0h";        // withPoint(50)+"h"
        case ScalarKind::I32:
            return "i32(" + num + ")"; // I32Value.wgsl() -> "i32(50)"
        case ScalarKind::U32:
            return num + "u";          // U32Value.wgsl() -> "50u"
    }
    return num;
}

// A numeric scalar or vector type (width 1 => scalar).
struct NumType {
    std::string key;  // toString(): e.g. "f32", "vec2<abstract-int>"
    ScalarKind elem;
    int width;  // 1 for scalar; 2..4 for vector
};

static bool isVector(const NumType& t) {
    return t.width > 1;
}

// .size byte size: scalar -> elementBytes; vector -> elementBytes * width.
static int byteSize(const NumType& t) {
    return elementBytes(t.elem) * t.width;
}

static bool requiresF16(const NumType& t) {
    return kindRequiresF16(t.elem);
}

// create(n).wgsl(): scalar -> scalar spelling; vector -> vecK(elem, ... width times).
static std::string createWgsl(const NumType& t, int n) {
    const std::string e = scalarValueWgsl(t.elem, n);
    if (!isVector(t)) {
        return e;
    }
    std::string out = "vec" + std::to_string(t.width) + "(";
    for (int i = 0; i < t.width; ++i) {
        if (i != 0) {
            out += ", ";
        }
        out += e;
    }
    out += ")";
    return out;
}

// "shape" string for isConvertible (scalar vs vecN), mirroring shapeOf().
static std::string shapeOf(const NumType& t) {
    if (!isVector(t)) {
        return "scalar";
    }
    return "vec" + std::to_string(t.width);
}

// isConvertible(src, dst), ported verbatim from util/conversion.ts for the numeric
// scalar/vector types in this table.
static bool isConvertible(const NumType& src, const NumType& dst) {
    if (src.key == dst.key) {
        return true;
    }
    if (shapeOf(src) != shapeOf(dst)) {
        return false;
    }
    const ScalarKind elSrc = src.elem;
    const ScalarKind elDst = dst.elem;
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

// kAllNumericScalarsAndVectors, in upstream order (24 types). See conversion.ts:
//   kConvertableToFloatScalarsAndVectors = [abstractInt, abstractFloat, f32, f16,
//      vec2ai, vec3ai, vec4ai, vec2af, vec2f, vec2h, vec3af, vec3f, vec3h,
//      vec4af, vec4f, vec4h]
//   kConcreteIntegerScalarsAndVectors = [i32, vec2i, vec3i, vec4i,
//      u32, vec2u, vec3u, vec4u]
static const std::vector<NumType>& kAllNumericScalarsAndVectors() {
    static const std::vector<NumType> v = {
        {"abstract-int", ScalarKind::AbstractInt, 1},
        {"abstract-float", ScalarKind::AbstractFloat, 1},
        {"f32", ScalarKind::F32, 1},
        {"f16", ScalarKind::F16, 1},
        {"vec2<abstract-int>", ScalarKind::AbstractInt, 2},
        {"vec3<abstract-int>", ScalarKind::AbstractInt, 3},
        {"vec4<abstract-int>", ScalarKind::AbstractInt, 4},
        {"vec2<abstract-float>", ScalarKind::AbstractFloat, 2},
        {"vec2<f32>", ScalarKind::F32, 2},
        {"vec2<f16>", ScalarKind::F16, 2},
        {"vec3<abstract-float>", ScalarKind::AbstractFloat, 3},
        {"vec3<f32>", ScalarKind::F32, 3},
        {"vec3<f16>", ScalarKind::F16, 3},
        {"vec4<abstract-float>", ScalarKind::AbstractFloat, 4},
        {"vec4<f32>", ScalarKind::F32, 4},
        {"vec4<f16>", ScalarKind::F16, 4},
        {"i32", ScalarKind::I32, 1},
        {"vec2<i32>", ScalarKind::I32, 2},
        {"vec3<i32>", ScalarKind::I32, 3},
        {"vec4<i32>", ScalarKind::I32, 4},
        {"u32", ScalarKind::U32, 1},
        {"vec2<u32>", ScalarKind::U32, 2},
        {"vec3<u32>", ScalarKind::U32, 3},
        {"vec4<u32>", ScalarKind::U32, 4},
    };
    return v;
}

static std::vector<Value> numTypeKeys() {
    std::vector<Value> values;
    for (const NumType& t : kAllNumericScalarsAndVectors()) {
        values.emplace_back(t.key);
    }
    return values;
}

static const NumType& findNumType(const std::string& key) {
    for (const NumType& t : kAllNumericScalarsAndVectors()) {
        if (t.key == key) {
            return t;
        }
    }
    static const NumType dummy{"", ScalarKind::AbstractInt, 1};
    return dummy;
}

CTS_TEST(g, "overload_resolution")
    .desc("Test overload resolution")
    .params([](ParamsBuilder u) {
        return u.combine("arg1", numTypeKeys())
            .combine("arg2", numTypeKeys())
            .beginSubcases()
            .combine("op", {"min", "max"})
            .filter([](const ParamRecord& p) {
                const std::string a1 = valueAs<std::string>(*findParam(p, "arg1"));
                const std::string a2 = valueAs<std::string>(*findParam(p, "arg2"));
                if (a1 == a2) {
                    return false;
                }
                const NumType& t1 = findNumType(a1);
                const NumType& t2 = findNumType(a2);
                const bool t1IsVector = isVector(t1);
                const bool t2IsVector = isVector(t2);
                if (t1IsVector != t2IsVector) {
                    return false;
                }
                if (t1IsVector && t2IsVector && byteSize(t1) != byteSize(t2)) {
                    return false;
                }
                return true;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const NumType& t1 = findNumType(t.param<std::string>("arg1"));
        const NumType& t2 = findNumType(t.param<std::string>("arg2"));
        const std::string op = t.param<std::string>("op");
        const NumType& resTy = isConvertible(t1, t2) ? t2 : t1;
        const std::string enable =
            (requiresF16(t1) || requiresF16(t2)) ? std::string("enable f16;") : std::string("");
        const int kMin = 50;
        const int kMax = 100;
        const int res = op == "min" ? kMin : kMax;
        const std::string v1 = createWgsl(t1, kMin);
        const std::string v2 = createWgsl(t2, kMax);
        const std::string resV = createWgsl(resTy, res);
        const std::string expr = op + "(" + v1 + ", " + v2 + ") == " + resV;
        const std::string assertExpr = isVector(t1) ? ("all(" + expr + ")") : expr;
        const std::string code = enable + "\n    const_assert " + assertExpr + ";";
        t.expectCompileResult(isConvertible(t1, t2) || isConvertible(t2, t1), code);
    });

}  // namespace
