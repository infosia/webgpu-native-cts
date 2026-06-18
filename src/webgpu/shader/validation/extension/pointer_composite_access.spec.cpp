// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/extension/pointer_composite_access.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - hasLanguageFeature('pointer_composite_access') returns the TRUE per-backend
//     answer: Dawn via wgpuInstanceHasWGSLLanguageFeature; non-Dawn via a canonical
//     trial-compile probe. Used (per upstream) to set the expected compile result.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,extension,pointer_composite_access",
    "Validation tests for pointer_composite_access extension");

struct Case {
    const char* name;
    const char* module;
    const char* init_expr;
    const char* via_deref;
    const char* via_pointer;
};

// Mirrors upstream kCases (object key order preserved for query identity).
static const std::vector<Case>& kCases() {
    static const std::vector<Case> cases = {
        // Via identifier 'a'
        {"array_index_access_via_identifier", "", "array<i32, 3>()", "(*(&a))[0]", "(&a)[0]"},
        {"vector_index_access_via_identifier", "", "vec3<i32>()", "(*(&a))[0]", "(&a)[0]"},
        {"vector_member_access_via_identifier", "", "vec3<i32>()", "(*(&a)).x", "(&a).x"},
        {"matrix_index_access_via_identifier", "", "mat2x3<f32>()", "(*(&a))[0]", "(&a)[0]"},
        {"struct_member_access_via_identifier", "struct S { a : i32, }", "S()", "(*(&a)).a", "(&a).a"},
        {"builtin_struct_modf_via_identifier", "", "modf(1.5)",
         "vec2((*(&a)).fract, (*(&a)).whole)", "vec2((&a).fract, (&a).whole)"},
        {"builtin_struct_frexp_via_identifier", "", "frexp(1.5)",
         "vec2((*(&a)).fract, f32((*(&a)).exp))", "vec2((&a).fract, f32((&a).exp))"},

        // Via pointer 'p'
        {"array_index_access_via_pointer", "", "array<i32, 3>()", "(*p)[0]", "p[0]"},
        {"vector_index_access_via_pointer", "", "vec3<i32>()", "(*p)[0]", "p[0]"},
        {"vector_member_access_via_pointer", "", "vec3<i32>()", "(*p).x", "p.x"},
        {"matrix_index_access_via_pointer", "", "mat2x3<f32>()", "(*p)[0]", "p[0]"},
        {"struct_member_access_via_pointer", "struct S { a : i32, }", "S()", "(*p).a", "p.a"},
        {"builtin_struct_modf_via_pointer", "", "modf(1.5)",
         "vec2((*p).fract, (*p).whole)", "vec2(p.fract, p.whole)"},
        {"builtin_struct_frexp_via_pointer", "", "frexp(1.5)",
         "vec2((*p).fract, f32((*p).exp))", "vec2(p.fract, f32(p.exp))"},
    };
    return cases;
}

static std::vector<Value> caseNames() {
    std::vector<Value> values;
    for (const Case& c : kCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const Case& findCase(const std::string& name) {
    for (const Case& c : kCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const Case dummy{"", "", "", "", ""};
    return dummy;
}

// Mirrors upstream makeSource().
static std::string makeSource(const std::string& module,
                              const std::string& init_expr,
                              const std::string& pointer_read_expr) {
    return "\n    " + module +
           "\n    fn f() {"
           "\n        var a = " + init_expr + ";"
           "\n        let p = &a;"
           "\n        let r = " + pointer_read_expr + ";"
           "\n    }";
}

CTS_TEST(g, "deref")
    .desc("Baseline test: pointer deref is always valid")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Case& curr = findCase(t.param<std::string>("case"));
        const std::string source = makeSource(curr.module, curr.init_expr, curr.via_deref);
        t.expectCompileResult(true, source);
    });

CTS_TEST(g, "pointer")
    .desc("Tests that direct pointer access is valid if pointer_composite_access is supported, else it should fail")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Case& curr = findCase(t.param<std::string>("case"));
        const std::string source = makeSource(curr.module, curr.init_expr, curr.via_pointer);
        // Mirrors upstream `const should_pass = t.hasLanguageFeature('pointer_composite_access')`.
        // hasLanguageFeature returns the TRUE per-backend answer (Dawn via the
        // instance query; non-Dawn via a canonical trial-compile probe), so the
        // expected result tracks actual feature support.
        const bool shouldPass = t.hasLanguageFeature("pointer_composite_access");
        t.expectCompileResult(shouldPass, source);
    });

} // namespace
