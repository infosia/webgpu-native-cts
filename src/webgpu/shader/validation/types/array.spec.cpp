// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/array.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,types,array",
    "Validation tests for array types");

struct CodeCase {
    const char* name;
    const char* code;
};

// Mirrors upstream kValidCases (object key order preserved).
static const std::vector<CodeCase>& kValidCases() {
    static const std::vector<CodeCase> cases = {
        // Basic element types.
        {"i32", "alias T = array<i32>;"},
        {"u32", "alias T = array<u32>;"},
        {"f32", "alias T = array<f32>;"},
        {"f16", "enable f16;\nalias T = array<f16>;"},
        {"bool", "alias T = array<bool>;"},

        // Composite elements
        {"vec2u", "alias T = array<vec2u>;"},
        {"vec3i", "alias T = array<vec3i>;"},
        {"vec4f", "alias T = array<vec4f>;"},
        {"array", "alias T = array<array<u32, 4>>;"},
        {"struct", "struct S { x : u32 }\nalias T = array<S>;"},
        {"mat2x2f", "alias T = array<mat2x2f>;"},
        {"mat4x4h", "enable f16;\nalias T = array<mat4x4h>;"},

        // Atomic elements
        {"atomicu", "alias T = array<atomic<u32>>;"},
        {"atomici", "alias T = array<atomic<i32>>;"},

        // Count expressions
        {"literal_count", "alias T = array<u32, 4>;"},
        {"literali_count", "alias T = array<u32, 4i>;"},
        {"literalu_count", "alias T = array<u32, 4u>;"},
        {"const_count", "const x = 8;\nalias T = array<u32, x>;"},
        {"const_expr_count1", "alias T = array<u32, 1 + 3>;"},
        {"const_expr_count2", "const x = 4;\nalias T = array<u32, x * 2>;"},
        {"const_expr_func", "alias T = array<u32, max(1,2)>;"},
        {"override_count", "override x : u32;\nalias T = array<u32, x>;"},
        {"override_expr1", "override x = 2;\nalias T = array<u32, vec2(x,x).x>;"},
        {"override_expr2", "override x = 1;\nalias T = array<u32, x + 1>;"},
        {"override_zero", "override x = 0;\nalias T = array<u32, x>;"},
        {"override_neg", "override x = -1;\nalias T = array<u32, x>;"},

        // Same array types
        {"same_const_value1",
         "\n    const x = 8;\n    const y = 8;\n    var<private> v : array<u32, x> = array<u32, "
         "y>();"},
        {"same_const_value2",
         "\n    const x = 8;\n    var<private> v : array<u32, x> = array<u32, 8>();"},
        {"same_const_value3", "\n    var<private> v : array<u32, 8i> = array<u32, 8u>();"},
        {"same_override",
         "\n    requires unrestricted_pointer_parameters;\n    override x : u32;\n    "
         "var<workgroup> v : array<u32, x>;\n    fn bar(p : ptr<workgroup, array<u32, x>>) { }\n   "
         " fn foo() { bar(&v); }"},
        {"same_rta",
         "\n    requires unrestricted_pointer_parameters;\n    @group(0) @binding(0) var<storage> "
         "x : array<u32>;\n    fn foo(p : ptr<storage, array<u32>>) { }\n    fn bar() { foo(&x); "
         "}"},

        // Shadow
        {"shadow", "alias array = vec2f;"},

        {"trailing_comma1", "alias T = array<u32,4,>;"},
        {"trailing_comma2", "alias T = array<u32,>;"},

        {"alias_element", "alias T = u32; alias U = array<T>;"},
    };
    return cases;
}

// Mirrors upstream kInvalidCases (object key order preserved).
static const std::vector<CodeCase>& kInvalidCases() {
    static const std::vector<CodeCase> cases = {
        {"f16_without_enable", "alias T = array<f16>;"},
        {"texture", "alias T = array<texture_2d<f32>, 4>;"},
        {"sampler", "alias T = array<sampler>;"},
        {"runtime_nested", "alias T = array<array<u32>, 4>;"},
        {"override_nested", "\n    override x : u32;\n    alias T = array<array<u32, x>, 4>;"},
        {"override_nested_struct",
         "\n    override x : u32;\n    struct T { x : array<u32, x> }"},
        {"zero_size", "alias T = array<u32, 0>;"},
        {"negative_size", "alias T = array<u32, 1 - 2>;"},
        {"const_zero", "const x = 0;\nalias T = array<u32, x>;"},
        {"const_neg", "const x = 1;\nconst y = 2;\nalias T = array<u32, x - y>;"},
        {"float_size", "alias T = array<u32, max(1f, 2f)>;"},
        {"incompatible_overrides",
         "\n    requires unrestricted_pointer_parameters;\n    override x = 8;\n    override y = "
         "8;\n    var<workgroup> v : array<u32, x>\n    fn bar(p : ptr<workgroup, array<u32 y>>) "
         "{ }\n    fn foo() { bar(&v); }"},
        {"incompatible_size",
         "\n    var<private> x : array<u32, 4>;\n    fn foo(a : array<u32, 2>) { }\n    fn bar() "
         "{ foo(x); }"},
        {"incompatible_element",
         "\n    const x : array<i32, 4> = array(1,2,3,4);\n    var<private> y : array<u32, 4>  = "
         "x;"},
        {"incompatible_rta",
         "\n    requires unrestricted_pointer_parameters;\n    @group(0) @binding(0) var<storage> "
         "x : array<u32>;\n    fn foo(p : ptr<storage, array<i32>>) { }\n    fn bar() { foo(&x); "
         "}"},
        {"incompatible_override_element",
         "\n    requires unrestricted_pointer_parameters;\n    override x : i32;\n    "
         "var<workgroup> v : array<u32, v>;\n    fn bar(p : ptr<workgroup, array<i32 c>>) { }\n   "
         " fn foo() { bar(&v); }"},
        {"override_function",
         "\n    override x : i32;\n    fn foo() { var v : array<u32, x>; }"},
        {"override_private", "\n    override x : u32;\n    var<private> v : array<u32, x>;"},
        {"override_uniform",
         "\n    override x : u32;\n    @group(0) @binding(0) var<uniform> v : array<u32, x>;"},
        {"override_storage",
         "\n    override x : u32;\n    @group(0) @binding(0) var<storage> v : array<u32, x>;"},

        // Parsing failures
        {"missing_r_template", "alias T = array<u32, 4;"},
        {"missing_l_template", "alias T = arrayu32,4>;"},
        {"missing_type", "alias T = array<4>;"},
        {"bad_type", "alias T = array<bad_type, 4>;"},
        {"missing_l_template_rta", "alias T = arrayu32>;"},
        {"missing_r_template_rta", "alias T = array<u32;"},
        {"bad_size", "alias T = array<u32,u32>;"},
        {"inline_struct", "alias T = array<struct S { x : u32 }, 4>;"},
    };
    return cases;
}

static const CodeCase& findCase(const std::vector<CodeCase>& cases, const std::string& name) {
    for (const CodeCase& c : cases) {
        if (name == c.name) {
            return c;
        }
    }
    static const CodeCase dummy{"", ""};
    return dummy;
}

static std::vector<Value> caseNames(const std::vector<CodeCase>& cases) {
    std::vector<Value> values;
    for (const CodeCase& c : cases) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "valid")
    .desc("Valid array type tests")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kValidCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const CodeCase& c = findCase(kValidCases(), t.param<std::string>("case"));
        const std::string code = c.code;
        t.skipIf(code.find("unrestricted") != std::string::npos &&
                     !t.hasLanguageFeature("unrestricted_pointer_parameters"),
                 "Test requires unrestricted_pointer_parameters");
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "invalid")
    .desc("Invalid array type tests")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kInvalidCases()));
    })
    .fn([](ShaderValidationTest& t) {
        const CodeCase& c = findCase(kInvalidCases(), t.param<std::string>("case"));
        const std::string code = c.code;
        t.skipIf(code.find("unrestricted") != std::string::npos &&
                     !t.hasLanguageFeature("unrestricted_pointer_parameters"),
                 "Test requires unrestricted_pointer_parameters");
        t.expectCompileResult(false, code);
    });

}  // namespace
