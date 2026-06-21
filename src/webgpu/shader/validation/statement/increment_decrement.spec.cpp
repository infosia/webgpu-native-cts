// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/increment_decrement.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// The constructible-type tables below use `Type[t].create(1).wgsl()` upstream.
// That WGSL representation is the *value* form (e.g. i32 -> `i32(1)`, u32 ->
// `1u`, f32 -> `1.0f`, vec2f -> `vec2(1.0f, 1.0f)`, mat2x3f ->
// `mat2x3(1.0f, ...)`), distinct from the type-spelling form. Each constructed
// value string is precomputed here to match conversion.ts wgsl() exactly.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,increment_decrement",
    "Validation for phony assignment statements");

// ---- var_init_type: kConstructibleCases (object key order preserved) -------
struct ConstructibleCase {
    const char* name;
    const char* value;  // Type[t].create(1).wgsl()
    bool pass;
    bool usesF16;
    const char* gdecl;
};

static const std::vector<ConstructibleCase>& kConstructibleCases() {
    static const std::vector<ConstructibleCase> v = {
        {"bool", "true", false, false, ""},
        {"i32", "i32(1)", true, false, ""},
        {"u32", "1u", true, false, ""},
        {"f32", "1.0f", false, false, ""},
        {"f16", "1.0h", false, true, ""},
        {"vec2f", "vec2(1.0f, 1.0f)", false, false, ""},
        {"vec3h", "vec3(1.0h, 1.0h, 1.0h)", false, true, ""},
        {"vec4u", "vec4(1u, 1u, 1u, 1u)", false, false, ""},
        {"vec3b", "vec3(true, true, true)", false, false, ""},
        {"mat2x3f", "mat2x3(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f)", false, false, ""},
        {"mat4x2h", "mat4x2(1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h)", false, true, ""},
        {"abstractInt", "1", true, false, ""},
        {"abstractFloat", "1.0", false, false, ""},
        {"array", "array(1,2,3)", false, false, ""},
        {"struct", "S(1,2)", false, false, "struct S{ a:u32, b:u32}"},
        {"atomic_u32", "xu", false, false, "var<workgroup> xu: atomic<u32>;"},
        {"atomic_i32", "xi", false, false, "var<workgroup> xi: atomic<i32>;"},
    };
    return v;
}

static std::vector<Value> constructibleNames() {
    std::vector<Value> values;
    for (const ConstructibleCase& c : kConstructibleCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ConstructibleCase& findConstructible(const std::string& name) {
    for (const ConstructibleCase& c : kConstructibleCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ConstructibleCase dummy{"", "", false, false, ""};
    return dummy;
}

CTS_TEST(g, "var_init_type")
    .desc("Test increment and decrement of a variable of various types")
    .params([](ParamsBuilder u) {
        return u.combine("type", constructibleNames()).combine("direction", {"up", "down"});
    })
    .fn([](ShaderValidationTest& t) {
        const ConstructibleCase& c = findConstructible(t.param<std::string>("type"));
        const std::string operator_ = t.param<std::string>("direction") == "up" ? "++" : "--";
        const std::string code =
            "\n" + (c.usesF16 ? std::string("enable f16;") : std::string("")) + "\n" +
            std::string(c.gdecl) + "\nfn f() {\n  var a = " + c.value + ";\n  a" + operator_ +
            ";\n}";
        t.expectCompileResult(c.pass, code);
    });

// ---- component: kComponentCases (object key order preserved) ----------------
struct ComponentCase {
    const char* name;
    const char* type;
    const char* wgsl;
    bool pass;
    bool usesF16;
    const char* gdecl;
};

static const std::vector<ComponentCase>& kComponentCases() {
    static const std::vector<ComponentCase> v = {
        {"v2u_x", "vec2u", "a.x", true, false, ""},
        {"v2u_y", "vec2u", "a.y", true, false, ""},
        {"v3u_x", "vec3u", "a.x", true, false, ""},
        {"v3u_y", "vec3u", "a.y", true, false, ""},
        {"v3u_z", "vec3u", "a.z", true, false, ""},
        {"v4u_x", "vec4u", "a.x", true, false, ""},
        {"v4u_y", "vec4u", "a.y", true, false, ""},
        {"v4u_z", "vec4u", "a.z", true, false, ""},
        {"v4u_w", "vec4u", "a.w", true, false, ""},
        {"v2i_x", "vec2i", "a.x", true, false, ""},
        {"v2i_y", "vec2i", "a.y", true, false, ""},
        {"v3i_x", "vec3i", "a.x", true, false, ""},
        {"v3i_y", "vec3i", "a.y", true, false, ""},
        {"v3i_z", "vec3i", "a.z", true, false, ""},
        {"v4i_x", "vec4i", "a.x", true, false, ""},
        {"v4i_y", "vec4i", "a.y", true, false, ""},
        {"v4i_z", "vec4i", "a.z", true, false, ""},
        {"v4i_w", "vec4i", "a.w", true, false, ""},
        {"v2u_xx", "vec2u", "a.xx", false, false, ""},
        {"v2u_indexed", "vec2u", "a[0]", true, false, ""},
        {"v2f_x", "vec2f", "a.x", false, false, ""},
        {"v2h_x", "vec2h", "a.x", false, true, ""},
        {"mat2x2f", "mat2x2f", "a[0][0]", false, false, ""},
        {"mat2x2h", "mat2x2h", "a[0][0]", false, true, ""},
        {"array", "array<i32,2>", "a", false, false, ""},
        {"array_i", "array<i32,2>", "a[0]", true, false, ""},
        {"array_f", "array<f32,2>", "a[0]", false, false, ""},
        {"struct", "S", "S", false, false, "struct S{field:i32}"},
        {"struct_var", "S", "a", false, false, "struct S{field:i32}"},
        {"struct_field", "S", "a.field", true, false, "struct S{field:i32}"},
    };
    return v;
}

static std::vector<Value> componentNames() {
    std::vector<Value> values;
    for (const ComponentCase& c : kComponentCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ComponentCase& findComponent(const std::string& name) {
    for (const ComponentCase& c : kComponentCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ComponentCase dummy{"", "", "", false, false, ""};
    return dummy;
}

CTS_TEST(g, "component")
    .desc("Test increment and decrement of component of various types")
    .params([](ParamsBuilder u) {
        return u.combine("type", componentNames()).combine("direction", {"up", "down"});
    })
    .fn([](ShaderValidationTest& t) {
        const ComponentCase& c = findComponent(t.param<std::string>("type"));
        const std::string operator_ = t.param<std::string>("direction") == "up" ? "++" : "--";
        const std::string code =
            "\n" + (c.usesF16 ? std::string("enable f16;") : std::string("")) + "\n" +
            std::string(c.gdecl) + "\nfn f() {\n  var a: " + c.type + ";\n  " + c.wgsl + operator_ +
            ";\n}";
        t.expectCompileResult(c.pass, code);
    });

// ---- parse: kTests (object key order preserved) -----------------------------
struct ParseCase {
    const char* name;
    const char* wgsl;
    bool pass;
    const char* gdecl;
};

static const std::vector<ParseCase>& kTests() {
    static const std::vector<ParseCase> v = {
        {"var", "a++;", true, ""},
        {"vector", "v++;", false, ""},
        {"paren_var_paren", "(a)++;", true, ""},
        {"star_and_var", "*&a++;", true, ""},
        {"paren_star_and_var_paren", "(*&a)++;", true, ""},
        {"many_star_and_var", "*&*&*&a++;", true, ""},
        {"space", "a ++;", true, ""},
        {"tab", "a\t++;", true, ""},
        {"newline", "a\n++;", true, ""},
        {"cr", "a\r++;", true, ""},
        {"space_space", "a ++ ;", true, ""},
        {"plus_space_plus", "a+ +;", false, ""},
        {"minux_space_minus", "a- -;", false, ""},
        {"no_var", "++;", false, ""},
        {"no_semi", "a++", false, ""},
        {"prefix", "++a;", false, ""},
        {"postfix_x", "v++.x;", false, ""},
        {"postfix_r", "v++.r;", false, ""},
        {"postfix_index", "v++[0];", false, ""},
        {"postfix_field", "a++.foo;", false, ""},
        {"literal_i32", "12i++;", false, ""},
        {"literal_u32", "12u++;", false, ""},
        {"literal_abstract_int", "12++;", false, ""},
        {"literal_abstract_float", "12.0++;", false, ""},
        {"literal_f32", "12.0f++;", false, ""},
        {"assign_to", "a++ = 1;", false, ""},
        {"at_global", "", false, "var<private> g:i32; g++;"},
        {"private", "g++;", true, "var<private> g:i32;"},
        {"workgroup", "g++;", true, "var<workgroup> g:i32;"},
        {"storage_rw", "g++;", true, "@group(0) @binding(0) var<storage,read_write> g: i32;"},
        {"storage_r", "g++;", false, "@group(0) @binding(0) var<storage,read> g: i32;"},
        {"storage", "g++;", false, "@group(0) @binding(0) var<storage,read> g: i32;"},
        {"uniform", "g++;", false, "@group(0) @binding(0) var<uniform> g: i32;"},
        {"texture", "g++;", false, "@group(0) @binding(0) var g: texture_2d<u32>;"},
        {"texture_x", "g.x++;", false, "@group(0) @binding(0) var g: texture_2d<u32>;"},
        {"texture_storage", "g++;", false,
         "@group(0) @binding(0) var g: texture_storage_2d<r32uint>;"},
        {"texture_storage_x", "g.x++;", false,
         "@group(0) @binding(0) var g: texture_storage_2d<r32uint>;"},
        {"sampler", "g++;", false, "@group(0) @binding(0) var g: sampler;"},
        {"sampler_comparison", "g++;", false, "@group(0) @binding(0) var g: sampler_comparison;"},
        {"override", "g++;", false, "override g:i32;"},
        {"global_const", "g++;", false, "const g:i32 = 0;"},
        {"workgroup_atomic", "g++;", false, "var<workgroup> g:atomic<i32>;"},
        {"storage_atomic", "g++;", false,
         "@group(0) @binding(0) var<storage,read_write> g:atomic<u32>;"},
        {"subexpr", "a = b++;", false, ""},
        {"expr_paren", "(a++);", false, ""},
        {"expr_add", "0 + a++;", false, ""},
        {"expr_negate", "-a++;", false, ""},
        {"inc_inc", "a++++;", false, ""},
        {"inc_space_inc", "a++ ++;", false, ""},
        {"inc_dec", "a++--;", false, ""},
        {"inc_space_dec", "a++ --;", false, ""},
        {"paren_inc", "(a++)++;", false, ""},
        {"paren_dec", "(a++)--;", false, ""},
        {"in_block", "{ a++; }", true, ""},
        {"in_for_init", "for (a++;false;) {}", true, ""},
        {"in_for_cond", "for (;a++;) {}", false, ""},
        {"in_for_update", "for (;false;a++) {}", true, ""},
        {"in_for_update_semi", "for (;false;a++;) {}", false, ""},
        {"in_continuing", "loop { continuing { a++; break if true;}}", true, ""},
        {"let", "let c = a; c++;", false, ""},
        {"const", "const c = 1; c++;", false, ""},
        {"builtin", "max++", false, ""},
        {"enum", "r32uint++", false, ""},
        {"param", "", false, "fn bump(p: i32) { p++;}"},
    };
    return v;
}

static std::vector<Value> testNames() {
    std::vector<Value> values;
    for (const ParseCase& c : kTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ParseCase& findTest(const std::string& name) {
    for (const ParseCase& c : kTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ParseCase dummy{"", "", false, ""};
    return dummy;
}

CTS_TEST(g, "parse")
    .desc("Test that increment and decrement statements are parsed correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("test", testNames()).combine("direction", {"up", "down"});
    })
    .fn([](ShaderValidationTest& t) {
        const ParseCase& c = findTest(t.param<std::string>("test"));
        std::string wgsl = c.wgsl;
        std::string gdecl = c.gdecl;
        // Upstream uses String.replace (first occurrence only) for '++' -> '--'.
        if (t.param<std::string>("direction") == "down") {
            const std::string::size_type wp = wgsl.find("++");
            if (wp != std::string::npos) {
                wgsl.replace(wp, 2, "--");
            }
            const std::string::size_type gp = gdecl.find("++");
            if (gp != std::string::npos) {
                gdecl.replace(gp, 2, "--");
            }
        }
        const std::string code = "\n" + gdecl +
                                 "\nfn f() {\n  var a: u32;\n  var b: u32;\n  var v: vec4u;\n  " +
                                 wgsl + "\n}";
        t.expectCompileResult(c.pass, code);
    });

}  // namespace
