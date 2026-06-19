// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/let.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,decl,let",
    "Validation tests for let declarations");

// Validity of a case. kTrue/kFalse are simple booleans; kTextureAndSamplerLet
// means the expectation is t.hasLanguageFeature('texture_and_sampler_let').
enum class Validity { kTrue, kFalse, kTextureAndSamplerLet };

struct LetCase {
    const char* name;
    const char* code;
    Validity valid;
    const char* decls; // optional leading declarations; "" if none
};

static std::vector<Value> caseNames(const std::vector<LetCase>& cases) {
    std::vector<Value> values;
    for (const LetCase& c : cases) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const LetCase& findCase(const std::vector<LetCase>& cases, const std::string& name) {
    for (const LetCase& c : cases) {
        if (name == c.name) {
            return c;
        }
    }
    static const LetCase dummy{"", "", Validity::kFalse, ""};
    return dummy;
}

// Mirrors upstream kTypeCases (object key order preserved).
static const std::vector<LetCase>& kTypeCases() {
    static const std::vector<LetCase> cases = {
        {"bool", "let x : bool = true;", Validity::kTrue, ""},
        {"i32", "let x : i32 = 1i;", Validity::kTrue, ""},
        {"u32", "let x : u32 = 1u;", Validity::kTrue, ""},
        {"f32", "let x : f32 = 1f;", Validity::kTrue, ""},
        {"f16", "let x : f16 = 1h;", Validity::kTrue, ""},
        {"vec2i", "let x : vec2i = vec2i();", Validity::kTrue, ""},
        {"vec3u", "let x : vec3u = vec3u();", Validity::kTrue, ""},
        {"vec4f", "let x : vec4f = vec4f();", Validity::kTrue, ""},
        {"mat2x2", "let x : mat2x2f = mat2x2f();", Validity::kTrue, ""},
        {"mat4x3f", "let x : mat4x3<f32> = mat4x3<f32>();", Validity::kTrue, ""},
        {"array_sized", "let x : array<u32, 4> = array(1,2,3,4);", Validity::kTrue, ""},
        {"array_runtime", "let x : array<u32> = array(1,2,3);", Validity::kFalse, ""},
        {"struct", "let x : S = S(0);", Validity::kTrue, "struct S { x : u32 }"},
        {"atomic", "let x : atomic<u32> = 0;", Validity::kFalse, ""},
        {"ptr_function", "\n      var x : i32;\n      let y : ptr<function, i32> = &x;",
         Validity::kTrue, ""},
        {"ptr_storage", "let y : ptr<storage, i32> = &x[0];", Validity::kTrue,
         "@group(0) @binding(0) var<storage> x : array<i32, 4>;"},
        {"load_rule", "\n      var x : i32 = 1;\n      let y : i32 = x;", Validity::kTrue, ""},
        {"texture_2d", "let x = tex2d;", Validity::kTextureAndSamplerLet,
         "@group(0) @binding(0) var tex2d : texture_2d<f32>;"},
        {"texture_storage_1d",
         "let x : texture_storage_1d<rgba32float, write> = tex1d;",
         Validity::kTextureAndSamplerLet,
         "@group(0) @binding(0) var tex1d : texture_storage_1d<rgba32float, write>;"},
        {"sampler", "let s = samp;", Validity::kTextureAndSamplerLet,
         "@group(0) @binding(0) var samp : sampler;"},
        {"sampler_comparison", "let s : sampler_comparison = samp_comp;",
         Validity::kTextureAndSamplerLet,
         "@group(0) @binding(0) var samp_comp : sampler_comparison;"},
    };
    return cases;
}

CTS_TEST(g, "type")
    .desc("Test let types")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kTypeCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("case");
        if (name == "f16") {
            t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_ShaderF16, "shader-f16");
        }
        const LetCase& c = findCase(kTypeCases(), name);
        const std::string code =
            std::string("\n") + (name == "f16" ? "enable f16;" : "") + "\n" + c.decls +
            "\nfn foo() {\n  " + c.code + "\n}";
        bool expect = c.valid == Validity::kTrue;
        if (c.valid == Validity::kTextureAndSamplerLet) {
            expect = t.hasLanguageFeature("texture_and_sampler_let");
        }
        t.expectCompileResult(expect, code);
    });

// Mirrors upstream kInitCases (object key order preserved).
static const std::vector<LetCase>& kInitCases() {
    static const std::vector<LetCase> cases = {
        {"no_init", "let x : u32;", Validity::kFalse, ""},
        {"no_type", "let x = 1;", Validity::kTrue, ""},
        {"init_matching_type", "let x : u32 = 1u;", Validity::kTrue, ""},
        {"init_mismatch_type", "let x : u32 = 1i;", Validity::kFalse, ""},
        {"ptr_type_mismatch", "var x : i32;\nlet y : ptr<function, u32> = &x;", Validity::kFalse,
         ""},
        {"ptr_access_mismatch", "let y : ptr<storage, u32, read> = &x;", Validity::kFalse,
         "@group(0) @binding(0) var<storage, read_write> x : u32;"},
        {"ptr_addrspace_mismatch", "let y = ptr<storage, u32> = &x;", Validity::kFalse,
         "@group(0) @binding(0) var<uniform> x : u32;"},
        {"init_const_expr", "let y = x * 2;", Validity::kTrue, "const x = 1;"},
        {"init_override_expr", "let y = x + 1;", Validity::kTrue, "override x = 1;"},
        {"init_runtime_expr", "var x = 1;\nlet y = x << 1;", Validity::kTrue, ""},
    };
    return cases;
}

CTS_TEST(g, "initializer")
    .desc("Test let initializers")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kInitCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const LetCase& c = findCase(kInitCases(), t.param<std::string>("case"));
        const std::string code =
            std::string("\n") + c.decls + "\nfn foo() {\n  " + c.code + "\n}";
        t.expectCompileResult(c.valid == Validity::kTrue, code);
    });

CTS_TEST(g, "module_scope")
    .desc("Test that let declarations are disallowed module scope")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "let x = 0;");
    });

// Mirrors upstream kTestTypes (order preserved).
static const std::vector<std::string>& kTestTypes() {
    static const std::vector<std::string> types = {
        "f32",         "i32",         "u32",         "bool",
        "vec2<f32>",   "vec2<i32>",   "vec2<u32>",   "vec2<bool>",
        "vec3<f32>",   "vec3<i32>",   "vec3<u32>",   "vec3<bool>",
        "vec4<f32>",   "vec4<i32>",   "vec4<u32>",   "vec4<bool>",
        "mat2x2<f32>", "mat2x3<f32>", "mat2x4<f32>", "mat3x2<f32>",
        "mat3x3<f32>", "mat3x4<f32>", "mat4x2<f32>", "mat4x3<f32>",
        "mat4x4<f32>", "array<f32, 12>", "array<i32, 12>", "array<u32, 12>",
        "array<bool, 12>",
    };
    return types;
}

static std::vector<Value> testTypeValues() {
    std::vector<Value> values;
    for (const std::string& s : kTestTypes()) {
        values.emplace_back(s);
    }
    return values;
}

CTS_TEST(g, "initializer_type")
    .desc("\n  If present, the initializer's type must match the store type of the variable.\n  "
          "Testing scalars, vectors, and matrices of every dimension and type.\n  TODO: add test "
          "for: structs - arrays of vectors and matrices - arrays of different length\n")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("lhsType", testTypeValues())
            .combine("rhsType", testTypeValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string lhsType = t.param<std::string>("lhsType");
        const std::string rhsType = t.param<std::string>("rhsType");
        const std::string code =
            "\n      @fragment\n      fn main() {\n        let a : " + lhsType + " = " + rhsType +
            "();\n      }\n    ";
        t.expectCompileResult(lhsType == rhsType, code);
    });

} // namespace
