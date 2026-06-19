// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/const.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,decl,const",
    "Validation tests for const declarations");

struct CodeCase {
    const char* name;
    const char* code;
    bool valid;
};

static std::vector<Value> caseNames(const std::vector<CodeCase>& cases) {
    std::vector<Value> values;
    for (const CodeCase& c : cases) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const CodeCase& findCase(const std::vector<CodeCase>& cases, const std::string& name) {
    for (const CodeCase& c : cases) {
        if (name == c.name) {
            return c;
        }
    }
    static const CodeCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "no_direct_recursion")
    .desc("Test that direct recursion of const declarations is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"a", "b"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl =
            "\nconst a : i32 = 42;\nconst b : i32 = " + target + ";\n";
        t.expectCompileResult(target == "a", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion")
    .desc("Test that indirect recursion of const declarations is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"a", "b"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl =
            "\nconst a : i32 = 42;\nconst b : i32 = c;\nconst c : i32 = " + target + ";\n";
        t.expectCompileResult(target == "a", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_array_size")
    .desc("Test that indirect recursion of const declarations via array size expressions is "
          "rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"a", "b"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl =
            "\nconst a = 4;\nconst b = c[0];\nconst c = array<i32, " + target +
            ">(4, 4, 4, 4);\n";
        t.expectCompileResult(target == "a", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_struct_attribute")
    .desc("Test that indirect recursion of const declarations via struct members is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"a", "b"})
            .combine("attribute", {"align", "location", "size"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string attribute = t.param<std::string>("attribute");
        const std::string wgsl =
            "\nstruct S {\n  @" + attribute + "(" + target + ") a : i32\n}\nconst a = 4;\nconst "
            "b = S(4).a;\n";
        t.expectCompileResult(target == "a", wgsl);
    });

// Mirrors upstream kTypeCases (object key order preserved).
static const std::vector<CodeCase>& kTypeCases() {
    static const std::vector<CodeCase> cases = {
        {"bool", "const x : bool = true;", true},
        {"i32", "const x : i32 = 1i;", true},
        {"u32", "const x : u32 = 1u;", true},
        {"f32", "const x : f32 = 1f;", true},
        {"f16", "enable f16;\nconst x : f16 = 1h;", true},
        {"abstract_int",
         "\n      const x = 0xffffffffff;\n      const_assert x == 0xffffffffff;", true},
        {"abstract_float",
         "\n      const x = 3937509.87755102;\n      const_assert x != 3937510.0;\n      "
         "const_assert x != 3937509.75;",
         true},
        {"vec2i", "const x : vec2i = vec2i();", true},
        {"vec3u", "const x : vec3u = vec3u();", true},
        {"vec4f", "const x : vec4f = vec4f();", true},
        {"mat2x2", "const x : mat2x2f = mat2x2f();", true},
        {"mat4x3f", "const x : mat4x3<f32> = mat4x3<f32>();", true},
        {"array_sized", "const x : array<u32, 4> = array(1,2,3,4);", true},
        {"array_runtime", "const x : array<u32> = array(1,2,3);", false},
        {"struct", "struct S { x : u32 }\nconst x : S = S(0);", true},
        {"atomic", "const x : atomic<u32> = 0;", false},
        {"vec_abstract_int",
         "\n      const x = vec2(0xffffffffff,0xfffffffff0);\n      const_assert x.x == "
         "0xffffffffff;\n      const_assert x.y == 0xfffffffff0;",
         true},
        {"array_abstract_int",
         "\n      const x = array(0xffffffffff,0xfffffffff0);\n      const_assert x[0] == "
         "0xffffffffff;\n      const_assert x[1] == 0xfffffffff0;",
         true},
    };
    return cases;
}

CTS_TEST(g, "type")
    .desc("Test const types")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kTypeCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("case");
        if (name == "f16") {
            t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_ShaderF16, "shader-f16");
        }
        const CodeCase& c = findCase(kTypeCases(), name);
        t.expectCompileResult(c.valid, c.code);
    });

// Mirrors upstream kInitCases (object key order preserved).
static const std::vector<CodeCase>& kInitCases() {
    static const std::vector<CodeCase> cases = {
        {"no_init", "const x : u32;", false},
        {"no_type", "const x = 0;", true},
        {"no_init_no_type", "const x;", false},
        {"init_matching_type", "const x : i32 = 1i;", true},
        {"init_mismatch_type", "const x : u32 = 1i;", false},
        {"abs_int_init_convert", "const x : u32 = 1;", true},
        {"abs_float_init_convert", "const x : f32 = 1.0;", true},
        {"init_const_expr", "const x = 0;\nconst y = x + 2;", true},
        {"init_override_expr", "override x : u32;\nconst y = x * 2;", false},
        {"init_runtime_expr", "var<private> x = 1i;\nconst y = x - 1;", false},
        {"init_func", "const x = max(1,2);", true},
        {"init_non_const_func",
         "const x = foo(1);\n    fn foo(p : i32) -> i32 { return p; }", false},
    };
    return cases;
}

CTS_TEST(g, "initializer")
    .desc("Test const initializers")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kInitCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const CodeCase& c = findCase(kInitCases(), t.param<std::string>("case"));
        t.expectCompileResult(c.valid, c.code);
    });

CTS_TEST(g, "function_scope")
    .desc("Test that const declarations are allowed in functions")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "fn foo() { const x = 0; }");
    });

CTS_TEST(g, "immutable")
    .desc("Test that const declarations are immutable")
    .fn([](ShaderValidationTest& t) {
        const std::string code = "\n    const x = 0;\n    fn foo() {\n      x = 1;\n    }";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "assert")
    .desc("Test value can be checked by a const_assert")
    .fn([](ShaderValidationTest& t) {
        const std::string code = "\n    const x = 0;\n    const_assert x == 0;";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "placement")
    .desc("Tests @const is not allowed to appear")
    .params([](ParamsBuilder u) {
        return u.combine("scope", {Value("private-var"), Value("storage-var"),
                                   Value("struct-member"), Value("fn-decl"), Value("fn-param"),
                                   Value("fn-var"), Value("fn-return"), Value("while-stmt"),
                                   Value::undef()});
    })
    .fn([](ShaderValidationTest& t) {
        const bool isUndef = t.paramIsUndefined("scope");
        const std::string scope = isUndef ? std::string() : t.param<std::string>("scope");
        const std::string attr = "@const";
        auto at = [&](const char* s) { return scope == s ? attr : std::string(); };

        const std::string code =
            "\n      " + at("private-var") +
            "\n      var<private> priv_var : i32;\n"
            "\n      " + at("storage-var") +
            "\n      @group(0) @binding(0)"
            "\n      var<storage> stor_var : i32;\n"
            "\n      struct A {"
            "\n        " + at("struct-member") +
            "\n        a : i32,"
            "\n      }\n"
            "\n      @vertex"
            "\n      " + at("fn-decl") +
            "\n      fn f("
            "\n        " + at("fn-param") +
            "\n        @location(0) b : i32,"
            "\n      ) -> " + at("fn-return") +
            " @builtin(position) vec4f {"
            "\n        " + at("fn-var") +
            "\n        var<function> func_v : i32;\n"
            "\n        " + at("while-stmt") +
            "\n        while false {}\n"
            "\n        return vec4(1, 1, 1, 1);"
            "\n      }"
            "\n    ";
        t.expectCompileResult(isUndef, code);
    });

} // namespace
