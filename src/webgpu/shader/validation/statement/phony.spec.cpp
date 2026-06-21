// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/phony.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// `value` strings in kConstructibleCases use `Type[t].create(1).wgsl()`, the
// value-spelling form per conversion.ts (e.g. i32 -> `i32(1)`, u32 -> `1u`,
// f32 -> `1.0f`, vec2f -> `vec2(1.0f, 1.0f)`, mat2x3f -> `mat2x3(1.0f, ...)`).

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,phony",
    "Validation for phony assignment statements");

// ---- rhs_constructible: kConstructibleCases (object key order preserved) ----
struct ConstructibleCase {
    const char* name;
    const char* value;  // Type[t].create(1).wgsl()
    bool pass;
    bool usesF16;
    const char* gdecl;
};

static const std::vector<ConstructibleCase>& kConstructibleCases() {
    static const std::vector<ConstructibleCase> v = {
        {"bool", "true", true, false, ""},
        {"i32", "i32(1)", true, false, ""},
        {"u32", "1u", true, false, ""},
        {"f32", "1.0f", true, false, ""},
        {"f16", "1.0h", true, true, ""},
        {"vec2f", "vec2(1.0f, 1.0f)", true, false, ""},
        {"vec3h", "vec3(1.0h, 1.0h, 1.0h)", true, true, ""},
        {"vec4u", "vec4(1u, 1u, 1u, 1u)", true, false, ""},
        {"vec3b", "vec3(true, true, true)", true, false, ""},
        {"mat2x3f", "mat2x3(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f)", true, false, ""},
        {"mat4x2h", "mat4x2(1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h, 1.0h)", true, true, ""},
        {"abstractInt", "1", true, false, ""},
        {"abstractFloat", "1.0", true, false, ""},
        {"array", "array(1,2,3)", true, false, ""},
        {"struct", "S(1,2)", true, false, "struct S{ a:u32, b:u32}"},
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

CTS_TEST(g, "rhs_constructible")
    .desc("Test that the rhs of 'phony assignment' can be a constructible.")
    .params([](ParamsBuilder u) { return u.combine("type", constructibleNames()); })
    .fn([](ShaderValidationTest& t) {
        const ConstructibleCase& c = findConstructible(t.param<std::string>("type"));
        const std::string code =
            "\n" + (c.usesF16 ? std::string("enable f16;") : std::string("")) + "\n" +
            std::string(c.gdecl) + "\nfn f() {\n  _ = " + c.value + ";\n}";
        t.expectCompileResult(c.pass, code);
    });

// ---- rhs_with_decl: kVarCases (object key order preserved) ------------------
struct VarCase {
    const char* name;
    const char* value;
    const char* gdecl;
    const char* ldecl;
    bool pass;
};

static const std::vector<VarCase>& kVarCases() {
    static const std::vector<VarCase> v = {
        {"storage", "x", "@group(0) @binding(0) var<storage> x: array<u32,1>;", "", true},
        {"storage_unsized", "x", "@group(0) @binding(0) var<storage> x: array<u32>;", "", false},
        {"storage_atomic", "x", "@group(0) @binding(0) var<storage,read_write> x: atomic<u32>;", "",
         false},
        {"uniform", "x", "@group(0) @binding(0) var<uniform> x: u32;", "", true},
        {"texture", "x", "@group(0) @binding(0) var x: texture_2d<f32>;", "", true},
        {"sampler", "x", "@group(0) @binding(0) var x: sampler;", "", true},
        {"sampler_comparison", "x", "@group(0) @binding(0) var x: sampler_comparison;", "", true},
        {"private", "x", "var<private> x: u32;", "", true},
        {"workgroup", "x", "var<workgroup> x: u32;", "", true},
        {"workgroup_atomic", "x", "var<workgroup> x: atomic<u32>;", "", false},
        {"override", "o", "override o: u32;", "", true},
        {"function_var", "x", "", "var x: u32;", true},
        {"let", "v", "", "let v = 1;", true},
        {"const", "c", "const c = 1;", "", true},
        {"function_const", "c", "", "const c = 1;", true},
        {"ptr", "&x", "", "var x: u32;", true},
        {"ptr_to_unsized", "&x", "@group(0) @binding(0) var<storage> x: array<u32>;", "", true},
        {"indexed", "x[0]", "@group(0) @binding(0) var<storage> x: array<u32>;", "", true},
        {"user_fn", "f", "", "", false},
        {"builtin", "max", "", "", false},
        {"builtin_call", "max(1,1)", "", "", true},
        {"user_call", "callee()", "fn callee() -> i32 { return 0; }", "", true},
        {"undeclared", "does_not_exist", "", "", false},
    };
    return v;
}

static std::vector<Value> varCaseNames() {
    std::vector<Value> values;
    for (const VarCase& c : kVarCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const VarCase& findVarCase(const std::string& name) {
    for (const VarCase& c : kVarCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const VarCase dummy{"", "", "", "", false};
    return dummy;
}

CTS_TEST(g, "rhs_with_decl")
    .desc("Test rhs of 'phony assignment' involving declared objects.")
    .params([](ParamsBuilder u) { return u.combine("test", varCaseNames()); })
    .fn([](ShaderValidationTest& t) {
        const VarCase& c = findVarCase(t.param<std::string>("test"));
        const std::string code =
            "\n" + std::string(c.gdecl) + "\n@compute @workgroup_size(1)\nfn f() {\n  " +
            std::string(c.ldecl) + "\n  _ = " + c.value + ";\n}";
        t.expectCompileResult(c.pass, code);
    });

// ---- parse: kTests (object key order preserved) -----------------------------
struct Test {
    const char* name;
    const char* wgsl;
    bool pass;
};

static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"literal", "_ = 1;", true},
        {"expr", "_ = (1+v);", true},
        {"var", "_ = v;", true},
        {"in_for_init", "for (_ = v;false;) {}", true},
        {"in_for_init_semi", "for (_ = v;;false;) {}", false},
        {"in_for_update", "for (;false; _ = v) {}", true},
        {"in_for_update_semi", "for (;false; _ = v;) {}", false},
        {"in_block", "{_ = v;}", true},
        {"in_continuing", "loop { continuing { _ = v; break if true;}}", true},
        {"in_paren", "(_ = v;)", false},
        {"underscore", "_", false},
        {"underscore_semi", "_;", false},
        {"underscore_equal", "_=", false},
        {"underscore_equal_semi", "_=;", false},
        {"underscore_equal_underscore_semi", "_=_;", false},
        {"paren_underscore_paren", "(_) = 1;", false},
        // LHS is not a reference type
        {"star_ampersand_undsscore", "*&_ = 1;", false},
        {"compound", "_ += 1;", false},
        {"equality", "_ == 1;", false},
        {"block", "_ = {};", false},
        {"return", "_ = return;", false},
    };
    return v;
}

static std::vector<Value> testNames() {
    std::vector<Value> values;
    for (const Test& t : kTests()) {
        values.emplace_back(std::string(t.name));
    }
    return values;
}

static const Test& findTest(const std::string& name) {
    for (const Test& t : kTests()) {
        if (name == t.name) {
            return t;
        }
    }
    static const Test dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "parse")
    .desc("Test that 'phony assignment' statements are parsed correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("test"));
        const std::string code =
            "\nfn f() {\n  var v: u32;\n  " + std::string(test.wgsl) + "\n}";
        t.expectCompileResult(test.pass, code);
    });

CTS_TEST(g, "module_scope")
    .desc("Phony assignment is not valid at module scope")
    .fn([](ShaderValidationTest& t) {
        const std::string code = "_ = 1; ";
        t.expectCompileResult(false, code);
    });

}  // namespace
