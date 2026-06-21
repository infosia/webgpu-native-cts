// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/access/vector.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kConcreteCases / kAbstractCases are Records whose `ok` field is either a bool
// or a `(width) => boolean` predicate. `Value` cannot hold a struct, so cases are
// keyed by `name` and looked up; `ok` is encoded as an enum (True / False / width>2
// / width>3). `result_width` (present only on some abstract cases) is encoded as
// hasResultWidth + resultWidth.
//
// The `abstract` test computes isConvertible(Type[abstract-X], Type[concrete]).
// Per conversion.ts isConvertible: abstract-int converts to {i32,u32,f32,f16};
// abstract-float converts to {f32,f16} but NOT {i32,u32}. Both are scalar->scalar
// here (shapes match), so the result is the precomputed table below.
//
// `c.wgsl.replace('V', vec_str)` upstream replaces only the FIRST 'V'; each wgsl
// string contains exactly one 'V', so a single find/replace is faithful.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,access,vector",
    "Validation tests for vector accesses");

enum class Ok { True, False, GtW2, GtW3 };

static bool okFor(Ok ok, int width) {
    switch (ok) {
        case Ok::True:
            return true;
        case Ok::False:
            return false;
        case Ok::GtW2:
            return width > 2;
        case Ok::GtW3:
            return width > 3;
    }
    return false;
}

// ---- kConcreteCases ----------------------------------------------------------
struct ConcreteCase {
    const char* name;
    const char* wgsl;
    Ok ok;
};

static const std::vector<ConcreteCase>& kConcreteCases() {
    static const std::vector<ConcreteCase> v = {
        // indexing with literal
        {"literal_0", "let r : T = v[0];", Ok::True},
        {"literal_1", "let r : T = v[1];", Ok::True},
        {"literal_2", "let r : T = v[2];", Ok::GtW2},
        {"literal_3", "let r : T = v[3];", Ok::GtW3},
        {"literal_0i", "let r : T = v[0i];", Ok::True},
        {"literal_1i", "let r : T = v[1i];", Ok::True},
        {"literal_2i", "let r : T = v[2i];", Ok::GtW2},
        {"literal_3i", "let r : T = v[3i];", Ok::GtW3},
        {"literal_0u", "let r : T = v[0u];", Ok::True},
        {"literal_1u", "let r : T = v[1u];", Ok::True},
        {"literal_2u", "let r : T = v[2u];", Ok::GtW2},
        {"literal_3u", "let r : T = v[3u];", Ok::GtW3},
        // indexing with 'const' variable
        {"const_0", "const i = 0; let r : T = v[i];", Ok::True},
        {"const_1", "const i = 1; let r : T = v[i];", Ok::True},
        {"const_2", "const i = 2; let r : T = v[i];", Ok::GtW2},
        {"const_3", "const i = 3; let r : T = v[i];", Ok::GtW3},
        {"const_0i", "const i = 0i; let r : T = v[i];", Ok::True},
        {"const_1i", "const i = 1i; let r : T = v[i];", Ok::True},
        {"const_2i", "const i = 2i; let r : T = v[i];", Ok::GtW2},
        {"const_3i", "const i = 3i; let r : T = v[i];", Ok::GtW3},
        {"const_0u", "const i = 0u; let r : T = v[i];", Ok::True},
        {"const_1u", "const i = 1u; let r : T = v[i];", Ok::True},
        {"const_2u", "const i = 2u; let r : T = v[i];", Ok::GtW2},
        {"const_3u", "const i = 3u; let r : T = v[i];", Ok::GtW3},
        // indexing with 'let' variable
        {"let_0", "let i = 0; let r : T = v[i];", Ok::True},
        {"let_1", "let i = 1; let r : T = v[i];", Ok::True},
        {"let_2", "let i = 2; let r : T = v[i];", Ok::True},
        {"let_3", "let i = 3; let r : T = v[i];", Ok::True},
        {"let_0i", "let i = 0i; let r : T = v[i];", Ok::True},
        {"let_1i", "let i = 1i; let r : T = v[i];", Ok::True},
        {"let_2i", "let i = 2i; let r : T = v[i];", Ok::True},
        {"let_3i", "let i = 3i; let r : T = v[i];", Ok::True},
        {"let_0u", "let i = 0u; let r : T = v[i];", Ok::True},
        {"let_1u", "let i = 1u; let r : T = v[i];", Ok::True},
        {"let_2u", "let i = 2u; let r : T = v[i];", Ok::True},
        {"let_3u", "let i = 3u; let r : T = v[i];", Ok::True},
        // indexing with 'var' variable
        {"var_0", "var i = 0; let r : T = v[i];", Ok::True},
        {"var_1", "var i = 1; let r : T = v[i];", Ok::True},
        {"var_2", "var i = 2; let r : T = v[i];", Ok::True},
        {"var_3", "var i = 3; let r : T = v[i];", Ok::True},
        {"var_0i", "var i = 0i; let r : T = v[i];", Ok::True},
        {"var_1i", "var i = 1i; let r : T = v[i];", Ok::True},
        {"var_2i", "var i = 2i; let r : T = v[i];", Ok::True},
        {"var_3i", "var i = 3i; let r : T = v[i];", Ok::True},
        {"var_0u", "var i = 0u; let r : T = v[i];", Ok::True},
        {"var_1u", "var i = 1u; let r : T = v[i];", Ok::True},
        {"var_2u", "var i = 2u; let r : T = v[i];", Ok::True},
        {"var_3u", "var i = 3u; let r : T = v[i];", Ok::True},
        // indexing with const expression
        {"const_expr_0", "let r : T = v[0 / 2];", Ok::True},
        {"const_expr_1", "let r : T = v[2 / 2];", Ok::True},
        {"const_expr_2", "let r : T = v[4 / 2];", Ok::GtW2},
        {"const_expr_3", "let r : T = v[6 / 2];", Ok::GtW3},
        {"const_expr_2_via_trig", "let r : T = v[i32(tan(1.10714872) + 0.5)];", Ok::GtW2},
        {"const_expr_3_via_trig", "let r : T = v[u32(tan(1.24904577) + 0.5)];", Ok::GtW3},
        {"const_expr_2_via_vec2", "let r : T = v[vec2(3, 2)[1]];", Ok::GtW2},
        {"const_expr_3_via_vec2", "let r : T = v[vec2(3, 2).x];", Ok::GtW3},
        {"const_expr_2_via_vec2u", "let r : T = v[vec2u(3, 2)[1]];", Ok::GtW2},
        {"const_expr_3_via_vec2i", "let r : T = v[vec2i(3, 2).x];", Ok::GtW3},
        {"const_expr_2_via_array", "let r : T = v[array<i32, 2>(3, 2)[1]];", Ok::GtW2},
        {"const_expr_3_via_array", "let r : T = v[array<i32, 2>(3, 2)[0]];", Ok::GtW3},
        {"const_expr_2_via_struct", "let r : T = v[S(2).i];", Ok::GtW2},
        {"const_expr_3_via_struct", "let r : T = v[S(3).i];", Ok::GtW3},
        // single element convenience name accesses
        {"x", "let r : T = v.x;", Ok::True},
        {"y", "let r : T = v.y;", Ok::True},
        {"z", "let r : T = v.z;", Ok::GtW2},
        {"w", "let r : T = v.w;", Ok::GtW3},
        {"r", "let r : T = v.r;", Ok::True},
        {"g", "let r : T = v.g;", Ok::True},
        {"b", "let r : T = v.b;", Ok::GtW2},
        {"a", "let r : T = v.a;", Ok::GtW3},
        // swizzles
        {"xy", "let r : vec2<T> = v.xy;", Ok::True},
        {"yx", "let r : vec2<T> = v.yx;", Ok::True},
        {"xyx", "let r : vec3<T> = v.xyx;", Ok::True},
        {"xyz", "let r : vec3<T> = v.xyz;", Ok::GtW2},
        {"zyx", "let r : vec3<T> = v.zyx;", Ok::GtW2},
        {"xyxy", "let r : vec4<T> = v.xyxy;", Ok::True},
        {"xyxz", "let r : vec4<T> = v.xyxz;", Ok::GtW2},
        {"xyzw", "let r : vec4<T> = v.xyzw;", Ok::GtW3},
        {"yxwz", "let r : vec4<T> = v.yxwz;", Ok::GtW3},
        {"rg", "let r : vec2<T> = v.rg;", Ok::True},
        {"gr", "let r : vec2<T> = v.gr;", Ok::True},
        {"rgg", "let r : vec3<T> = v.rgg;", Ok::True},
        {"rgb", "let r : vec3<T> = v.rgb;", Ok::GtW2},
        {"grb", "let r : vec3<T> = v.grb;", Ok::GtW2},
        {"rgbr", "let r : vec4<T> = v.rgbr;", Ok::GtW2},
        {"rgba", "let r : vec4<T> = v.rgba;", Ok::GtW3},
        {"gbra", "let r : vec4<T> = v.gbra;", Ok::GtW3},
        // swizzle chains
        {"xy_yx", "let r : vec2<T> = v.xy.yx;", Ok::True},
        {"xyx_xxy", "let r : vec3<T> = v.xyx.xxy;", Ok::True},
        {"xyz_zyx", "let r : vec3<T> = v.xyz.zyx;", Ok::GtW2},
        {"xyxy_rrgg", "let r : vec4<T> = v.xyxy.rrgg;", Ok::True},
        {"rbrg_xyzw", "let r : vec4<T> = v.rbrg.xyzw;", Ok::GtW2},
        {"xyxz_rbg_yx", "let r : vec2<T> = v.xyxz.rbg.yx;", Ok::GtW2},
        {"wxyz_bga_xy", "let r : vec2<T> = v.wxyz.bga.xy;", Ok::GtW3},
        // mixed swizzle and indexing
        {"xy_0", "let r = v.xy[0];", Ok::True},
        {"xy_3", "let r = v.xy[3];", Ok::False},
        // error: invalid convenience letterings
        {"xq", "let r : vec2<T> = v.xq;", Ok::False},
        {"py", "let r : vec2<T> = v.py;", Ok::False},
        // error: mixed convenience letterings
        {"xg", "let r : vec2<T> = v.xg;", Ok::False},
        {"ryb", "let r : vec3<T> = v.ryb;", Ok::False},
        {"xgza", "let r : vec4<T> = v.xgza;", Ok::False},
        // error: too many swizzle elements
        {"xxxxx", "let r = v.xxxxx;", Ok::False},
        {"rrrrr", "let r = v.rrrrr;", Ok::False},
        {"yxwxy", "let r = v.yxwxy;", Ok::False},
        {"rgbar", "let r = v.rgbar;", Ok::False},
        // error: invalid index value
        {"literal_5", "let r : T = v[5];", Ok::False},
        {"literal_minus_1", "let r : T = v[-1];", Ok::False},
        // error: invalid index type
        {"float_idx", "let r : T = v[1.0];", Ok::False},
        {"bool_idx", "let r : T = v[true];", Ok::False},
        {"array_idx", "let r : T = v[array<i32, 2>()];", Ok::False},
    };
    return v;
}

static std::vector<Value> concreteCaseNames() {
    std::vector<Value> values;
    for (const ConcreteCase& c : kConcreteCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ConcreteCase& findConcreteCase(const std::string& name) {
    for (const ConcreteCase& c : kConcreteCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ConcreteCase dummy{"", "", Ok::False};
    return dummy;
}

// ---- kAbstractCases ----------------------------------------------------------
struct AbstractCase {
    const char* name;
    const char* wgsl;
    bool hasResultWidth;
    int resultWidth;
    Ok ok;
};

static const std::vector<AbstractCase>& kAbstractCases() {
    static const std::vector<AbstractCase> v = {
        // indexing with literal
        {"literal_0", "const r = V[0];", true, 1, Ok::True},
        {"literal_1", "const r = V[1];", true, 1, Ok::True},
        {"literal_2", "const r = V[2];", true, 1, Ok::GtW2},
        {"literal_3", "const r = V[3];", true, 1, Ok::GtW3},
        {"literal_0i", "const r = V[0i];", true, 1, Ok::True},
        {"literal_1i", "const r = V[1i];", true, 1, Ok::True},
        {"literal_2i", "const r = V[2i];", true, 1, Ok::GtW2},
        {"literal_3i", "const r = V[3i];", true, 1, Ok::GtW3},
        {"literal_0u", "const r = V[0u];", true, 1, Ok::True},
        {"literal_1u", "const r = V[1u];", true, 1, Ok::True},
        {"literal_2u", "const r = V[2u];", true, 1, Ok::GtW2},
        {"literal_3u", "const r = V[3u];", true, 1, Ok::GtW3},
        // indexing with 'const' variable
        {"const_0", "const i = 0; const r = V[i];", true, 1, Ok::True},
        {"const_1", "const i = 1; const r = V[i];", true, 1, Ok::True},
        {"const_2", "const i = 2; const r = V[i];", true, 1, Ok::GtW2},
        {"const_3", "const i = 3; const r = V[i];", true, 1, Ok::GtW3},
        {"const_0i", "const i = 0i; const r = V[i];", true, 1, Ok::True},
        {"const_1i", "const i = 1i; const r = V[i];", true, 1, Ok::True},
        {"const_2i", "const i = 2i; const r = V[i];", true, 1, Ok::GtW2},
        {"const_3i", "const i = 3i; const r = V[i];", true, 1, Ok::GtW3},
        {"const_0u", "const i = 0u; const r = V[i];", true, 1, Ok::True},
        {"const_1u", "const i = 1u; const r = V[i];", true, 1, Ok::True},
        {"const_2u", "const i = 2u; const r = V[i];", true, 1, Ok::GtW2},
        {"const_3u", "const i = 3u; const r = V[i];", true, 1, Ok::GtW3},
        // indexing with 'let' variable
        {"let_0", "let i = 0; const r = V[i];", false, 0, Ok::False},
        {"let_1", "let i = 1; const r = V[i];", false, 0, Ok::False},
        {"let_2", "let i = 2; const r = V[i];", false, 0, Ok::False},
        {"let_3", "let i = 3; const r = V[i];", false, 0, Ok::False},
        {"let_0i", "let i = 0i; const r = V[i];", false, 0, Ok::False},
        {"let_1i", "let i = 1i; const r = V[i];", false, 0, Ok::False},
        {"let_2i", "let i = 2i; const r = V[i];", false, 0, Ok::False},
        {"let_3i", "let i = 3i; const r = V[i];", false, 0, Ok::False},
        {"let_0u", "let i = 0u; const r = V[i];", false, 0, Ok::False},
        {"let_1u", "let i = 1u; const r = V[i];", false, 0, Ok::False},
        {"let_2u", "let i = 2u; const r = V[i];", false, 0, Ok::False},
        {"let_3u", "let i = 3u; const r = V[i];", false, 0, Ok::False},
        // indexing with 'var' variable
        {"var_0", "var i = 0; const r = V[i];", false, 0, Ok::False},
        {"var_1", "var i = 1; const r = V[i];", false, 0, Ok::False},
        {"var_2", "var i = 2; const r = V[i];", false, 0, Ok::False},
        {"var_3", "var i = 3; const r = V[i];", false, 0, Ok::False},
        {"var_0i", "var i = 0i; const r = V[i];", false, 0, Ok::False},
        {"var_1i", "var i = 1i; const r = V[i];", false, 0, Ok::False},
        {"var_2i", "var i = 2i; const r = V[i];", false, 0, Ok::False},
        {"var_3i", "var i = 3i; const r = V[i];", false, 0, Ok::False},
        {"var_0u", "var i = 0u; const r = V[i];", false, 0, Ok::False},
        {"var_1u", "var i = 1u; const r = V[i];", false, 0, Ok::False},
        {"var_2u", "var i = 2u; const r = V[i];", false, 0, Ok::False},
        {"var_3u", "var i = 3u; const r = V[i];", false, 0, Ok::False},
        // indexing with const expression
        {"const_expr_0", "const r = V[0 / 2];", true, 1, Ok::True},
        {"const_expr_1", "const r = V[2 / 2];", true, 1, Ok::True},
        {"const_expr_2", "const r = V[4 / 2];", true, 1, Ok::GtW2},
        {"const_expr_3", "const r = V[6 / 2];", true, 1, Ok::GtW3},
        {"const_expr_2_via_trig", "const r = V[i32(tan(1.10714872) + 0.5)];", true, 1, Ok::GtW2},
        {"const_expr_3_via_trig", "const r = V[u32(tan(1.24904577) + 0.5)];", true, 1, Ok::GtW3},
        {"const_expr_2_via_vec2", "const r = V[vec2(3, 2)[1]];", true, 1, Ok::GtW2},
        {"const_expr_3_via_vec2", "const r = V[vec2(3, 2).x];", true, 1, Ok::GtW3},
        {"const_expr_2_via_vec2u", "const r = V[vec2u(3, 2)[1]];", true, 1, Ok::GtW2},
        {"const_expr_3_via_vec2i", "const r = V[vec2i(3, 2).x];", true, 1, Ok::GtW3},
        {"const_expr_2_via_array", "const r = V[array<i32, 2>(3, 2)[1]];", true, 1, Ok::GtW2},
        {"const_expr_3_via_array", "const r = V[array<i32, 2>(3, 2)[0]];", true, 1, Ok::GtW3},
        {"const_expr_2_via_struct", "const r = V[S(2).i];", true, 1, Ok::GtW2},
        {"const_expr_3_via_struct", "const r = V[S(3).i];", true, 1, Ok::GtW3},
        // single element convenience name accesses
        {"x", "const r = V.x;", true, 1, Ok::True},
        {"y", "const r = V.y;", true, 1, Ok::True},
        {"z", "const r = V.z;", true, 1, Ok::GtW2},
        {"w", "const r = V.w;", true, 1, Ok::GtW3},
        {"r", "const r = V.r;", true, 1, Ok::True},
        {"g", "const r = V.g;", true, 1, Ok::True},
        {"b", "const r = V.b;", true, 1, Ok::GtW2},
        {"a", "const r = V.a;", true, 1, Ok::GtW3},
        // swizzles
        {"xy", "const r = V.xy;", true, 2, Ok::True},
        {"yx", "const r = V.yx;", true, 2, Ok::True},
        {"xyx", "const r = V.xyx;", true, 3, Ok::True},
        {"xyz", "const r = V.xyz;", true, 3, Ok::GtW2},
        {"zyx", "const r = V.zyx;", true, 3, Ok::GtW2},
        {"xyxy", "const r = V.xyxy;", true, 4, Ok::True},
        {"xyxz", "const r = V.xyxz;", true, 4, Ok::GtW2},
        {"xyzw", "const r = V.xyzw;", true, 4, Ok::GtW3},
        {"yxwz", "const r = V.yxwz;", true, 4, Ok::GtW3},
        {"rg", "const r = V.rg;", true, 2, Ok::True},
        {"gr", "const r = V.gr;", true, 2, Ok::True},
        {"rgg", "const r = V.rgg;", true, 3, Ok::True},
        {"rgb", "const r = V.rgb;", true, 3, Ok::GtW2},
        {"grb", "const r = V.grb;", true, 3, Ok::GtW2},
        {"rgbr", "const r = V.rgbr;", true, 4, Ok::GtW2},
        {"rgba", "const r = V.rgba;", true, 4, Ok::GtW3},
        {"gbra", "const r = V.gbra;", true, 4, Ok::GtW3},
        // swizzle chains
        {"xy_yx", "const r = V.xy.yx;", true, 2, Ok::True},
        {"xyx_xxy", "const r = V.xyx.xxy;", true, 3, Ok::True},
        {"xyz_zyx", "const r = V.xyz.zyx;", true, 3, Ok::GtW2},
        {"xyxy_rrgg", "const r = V.xyxy.rrgg;", true, 4, Ok::True},
        {"rbrg_xyzw", "const r = V.rbrg.xyzw;", true, 4, Ok::GtW2},
        {"xyxz_rbg_yx", "const r = V.xyxz.rbg.yx;", true, 2, Ok::GtW2},
        {"wxyz_bga_xy", "const r = V.wxyz.bga.xy;", true, 2, Ok::GtW3},
        // mixed swizzle and indexing
        {"xy_0", "const r = V.xy[0];", true, 1, Ok::True},
        {"xy_3", "const r = V.xy[3];", false, 0, Ok::False},
        // error: invalid convenience letterings
        {"xq", "const r = V.xq;", false, 0, Ok::False},
        {"py", "const r = V.py;", false, 0, Ok::False},
        // error: mixed convenience letterings
        {"xg", "const r = V.xg;", false, 0, Ok::False},
        {"ryb", "const r = V.ryb;", false, 0, Ok::False},
        {"xgza", "const r = V.xgza;", false, 0, Ok::False},
        // error: too many swizzle elements
        {"xxxxx", "const r = V.xxxxx;", false, 0, Ok::False},
        {"rrrrr", "const r = V.rrrrr;", false, 0, Ok::False},
        {"yxwxy", "const r = V.yxwxy;", false, 0, Ok::False},
        {"rgbar", "const r = V.rgbar;", false, 0, Ok::False},
        // error: invalid index Value
        {"literal_5", "const r = V[5];", false, 0, Ok::False},
        {"literal_minus_1", "const r = V[-1];", false, 0, Ok::False},
        // error: invalid index type
        {"float_idx", "const r = V[1.0];", false, 0, Ok::False},
        {"bool_idx", "const r = V[true];", false, 0, Ok::False},
        {"array_idx", "const r = V[array<i32, 2>()];", false, 0, Ok::False},
    };
    return v;
}

static std::vector<Value> abstractCaseNames() {
    std::vector<Value> values;
    for (const AbstractCase& c : kAbstractCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const AbstractCase& findAbstractCase(const std::string& name) {
    for (const AbstractCase& c : kAbstractCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const AbstractCase dummy{"", "", false, 0, Ok::False};
    return dummy;
}

// isConvertible(Type[`abstract-${abstract_type}`], Type[concrete_type]) for the
// scalar->scalar shapes used here. See file header.
static bool abstractConvertible(const std::string& abstractType, const std::string& concreteType) {
    if (abstractType == "int") {
        // abstract-int -> {i32, u32, f32, f16} all true.
        return true;
    }
    // abstract-float -> {f32, f16} true; {i32, u32} false.
    return concreteType == "f32" || concreteType == "f16";
}

CTS_TEST(g, "concrete")
    .desc("Tests validation of vector indexed and swizzles for concrete data types")
    .params([](ParamsBuilder u) {
        return u.combine("vector_decl", {"const", "let", "var", "param"})
            .combine("vector_width", {2, 3, 4})
            .combine("element_type", {"i32", "u32", "f32", "f16", "bool"})
            .beginSubcases()
            .combine("case", concreteCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string vector_decl = t.param<std::string>("vector_decl");
        const int vector_width = t.param<int>("vector_width");
        const std::string element_type = t.param<std::string>("element_type");
        const ConcreteCase& c = findConcreteCase(t.param<std::string>("case"));

        const std::string enables = element_type == "f16" ? "enable f16;" : "";
        const std::string prefix = enables + "\n\nalias T = " + element_type +
                                   ";\n\nstruct S {\n  i : i32,\n}\n\n@compute @workgroup_size(1)\n";
        const std::string vw = std::to_string(vector_width);
        std::string code;
        if (vector_decl == "param") {
            code = prefix + "\nfn main() {\n  F(vec" + vw + "<T>());\n}\n\nfn F(v : vec" + vw +
                   "<T>) {\n  " + c.wgsl + "\n}\n";
        } else {
            code = prefix + "\nfn main() {\n  " + vector_decl + " v = vec" + vw + "<T>();\n  " +
                   c.wgsl + "\n}\n";
        }
        const bool pass = okFor(c.ok, vector_width);
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "abstract")
    .desc("Tests validation of vector indexed and swizzles for abstract data types")
    .params([](ParamsBuilder u) {
        return u.combine("vector_width", {2, 3, 4})
            .combine("abstract_type", {"int", "float"})
            .combine("concrete_type", {"u32", "i32", "f32", "f16"})
            .beginSubcases()
            .combine("case", abstractCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const int vector_width = t.param<int>("vector_width");
        const std::string abstract_type = t.param<std::string>("abstract_type");
        const std::string concrete_type = t.param<std::string>("concrete_type");
        const AbstractCase& c = findAbstractCase(t.param<std::string>("case"));

        const std::string enables = concrete_type == "f16" ? "enable f16;" : "";
        const std::string elem = abstract_type == "int" ? "0" : "0.0";
        std::string vec_str = "vec" + std::to_string(vector_width) + "(";
        for (int i = 0; i < vector_width; ++i) {
            if (i != 0) {
                vec_str += ", ";
            }
            vec_str += elem;
        }
        vec_str += ")";

        std::string conversion;
        if (c.hasResultWidth) {
            const std::string conversion_type =
                c.resultWidth == 1 ? concrete_type
                                   : "vec" + std::to_string(c.resultWidth) + "<" + concrete_type + ">";
            conversion = "const c: " + conversion_type + " = r;";
        }

        // c.wgsl.replace('V', vec_str) — replace the first 'V' only.
        std::string wgsl = c.wgsl;
        const std::string::size_type vp = wgsl.find('V');
        if (vp != std::string::npos) {
            wgsl.replace(vp, 1, vec_str);
        }

        const std::string code = enables +
                                 "\nstruct S {\n  i : i32,\n}\n\n@compute @workgroup_size(1)\nfn "
                                 "main() {\n  " +
                                 wgsl + "\n  " + conversion + "\n}\n";
        const bool convertible = abstractConvertible(abstract_type, concrete_type);
        const bool pass = convertible && okFor(c.ok, vector_width);
        t.expectCompileResult(pass, code);
    });

}  // namespace
