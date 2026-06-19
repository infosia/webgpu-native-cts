// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/override.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// NOTE: every g.test in this file validates at module-COMPILE time
// (expectCompileResult) in upstream, including array_size (override-sized arrays
// are a compile-time validity question on the var declaration, not a pipeline
// constant value). So this whole file is compile-only.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,decl,override",
    "Validation tests for override declarations");

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
    .desc("Test that direct recursion of override declarations is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"a", "b"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl =
            "\noverride a : i32 = 42;\noverride b : i32 = " + target + ";\n";
        t.expectCompileResult(target == "a", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion")
    .desc("Test that indirect recursion of override declarations is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"a", "b"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl =
            "\noverride a : i32 = 42;\noverride b : i32 = c;\noverride c : i32 = " + target +
            ";\n";
        t.expectCompileResult(target == "a", wgsl);
    });

// Mirrors upstream kIdCases (object key order preserved).
static const std::vector<CodeCase>& kIdCases() {
    static const std::vector<CodeCase> cases = {
        {"min", "@id(0) override x = 1;", true},
        {"max", "@id(65535) override x = 1;", true},
        {"neg", "@id(-1) override x = 1;", false},
        {"too_large", "@id(65536) override x = 1;", false},
        {"duplicate", "\n      @id(1) override x = 1;\n      @id(1) override y = 1;", false},
    };
    return cases;
}

CTS_TEST(g, "id")
    .desc("Test id attributes")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kIdCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const CodeCase& c = findCase(kIdCases(), t.param<std::string>("case"));
        t.expectCompileResult(c.valid, c.code);
    });

// Mirrors upstream kTypeCases (object key order preserved).
static const std::vector<CodeCase>& kTypeCases() {
    static const std::vector<CodeCase> cases = {
        {"bool", "override x : bool;", true},
        {"i32", "override x : i32;", true},
        {"u32", "override x : u32;", true},
        {"f32", "override x : f32;", true},
        {"f16", "enable f16;\noverride x : f16;", true},
        {"abs_int_conversion", "override x = 1;", true},
        {"abs_float_conversion", "override x = 1.0;", true},
        {"vec2_bool", "override x : vec2<bool>;", false},
        {"vec2i", "override x : vec2i;", false},
        {"vec3u", "override x : vec3u;", false},
        {"vec4f", "override x : vec4f;", false},
        {"mat2x2f", "override x : mat2x2f;", false},
        {"matrix", "override x : mat4x3<f32>;", false},
        {"array", "override x : array<u32, 4>;", false},
        {"struct", "struct S { x : u32 }\noverride x : S;", false},
        {"atomic", "override x : atomic<u32>;", false},
    };
    return cases;
}

CTS_TEST(g, "type")
    .desc("Test override types")
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
        {"no_init_no_type", "override x;", false},
        {"no_init", "override x : u32;", true},
        {"no_type", "override x = 1;", true},
        {"init_matching_type", "override x : u32 = 1u;", true},
        {"init_mismatch_type", "override x : u32 = 1i;", false},
        {"init_mismatch_vector", "override x : u32 = vec2i();", false},
        {"abs_int_init_convert", "override x : f32 = 1;", true},
        {"abs_float_init_convert", "override x : f32 = 1.0;", true},
        {"init_const_expr", "const x = 1;\noverride y = 2 * x;", true},
        {"init_override_expr", "override x = 1;\noverride y = x + 2;", true},
        {"init_runtime_expr", "var<private> x = 2;\noverride y = x;", false},
        {"const_func_init", "override x = max(1, 2);", true},
        {"non_const_func_init",
         "override x = foo(1);\n    fn foo(p : i32) -> i32 { return p; }", false},
        {"mix_order_init", "override x = y;\n    override y : i32;", true},
        {"logical_lhs_override", "override x = 2;\n      override y = x == 2 && 1 < 2;", true},
        {"logical_rhs_override", "override x = 2;\n      override y = 1 < 2 || x == 2;", true},
        {"logical_both_override", "override x = 2;\n      override y = x > 2 || x == 2;", true},
    };
    return cases;
}

CTS_TEST(g, "initializer")
    .desc("Test override initializers")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kInitCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const CodeCase& c = findCase(kInitCases(), t.param<std::string>("case"));
        t.expectCompileResult(c.valid, c.code);
    });

CTS_TEST(g, "function_scope")
    .desc("Test that override declarations are disallowed in functions")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "fn foo() { override x : u32; }");
    });

struct ArrayCase {
    const char* name;
    const char* code;
    bool validWithOverrideSize;
};

// Mirrors upstream kArrayCases (object key order preserved).
static const std::vector<ArrayCase>& kArrayCases() {
    static const std::vector<ArrayCase> cases = {
        {"workgroup_var", "var<workgroup> a: array<u32, size>;", true},
        {"private_var", "var<private> a: array<u32, size>;", false},
        {"uniform_var", "@group(0) @binding(0) var<uniform> a: array<vec4u, size>;", false},
        {"storage_var", "@group(0) @binding(0) var<storage> a: array<u32, size>;", false},
        {"function_var", "fn f() { var a: array<u32, size>; }", false},
        {"workgroup_ptr_param", "fn f(a: ptr<workgroup, array<u32, size>>) {}", true},
        {"private_ptr_param", "fn f(a: ptr<private, array<u32, size>>) {}", false},
        {"workgroup_ptr_let", "var<workgroup> a: array<u32, size>; fn f() { let a = &a; }", true},
        {"private_ptr_let", "var<private> a: array<u32, size>; fn f() { let a = &a; }", false},
        {"array_in_struct", "struct S { a: array<u32, size> };", false},
        {"array_in_array", "var<workgroup> a: array<array<u32, size>, 4>;", false},
        {"construct_array", "fn a() -> u32 { return array<u32, size>()[0]; }", false},
    };
    return cases;
}

static std::vector<Value> arrayCaseNames() {
    std::vector<Value> values;
    for (const ArrayCase& c : kArrayCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "array_size")
    .desc("Test that override-sized arrays are only allowed in the workgroup address space, and "
          "cannot be nested in other types or constructed.")
    .params([](ParamsBuilder u) {
        return u.combine("case", arrayCaseNames())
            .combine("stage", {"const", "override"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string caseName = t.param<std::string>("case");
        const std::string stage = t.param<std::string>("stage");
        const ArrayCase* tc = nullptr;
        for (const ArrayCase& c : kArrayCases()) {
            if (caseName == c.name) {
                tc = &c;
                break;
            }
        }
        const std::string wgsl =
            "\n" + stage + " size = 1;\n" + (tc != nullptr ? tc->code : "") + "\n";
        // Using a `const` size is the control case that should always pass.
        const bool expect = stage == "const" || (tc != nullptr && tc->validWithOverrideSize);
        t.expectCompileResult(expect, wgsl);
    });

} // namespace
