// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/for.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/statement/test_types.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
using cts::shader_validation::statement::kTestTypes;
using cts::shader_validation::statement::TestType;
using cts::shader_validation::statement::testTypeNames;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,for",
    "Validation tests for 'for' statements'");

CTS_TEST(g, "condition_type")
    .desc("Tests that a 'for' condition must be a bool type")
    .params([](ParamsBuilder u) { return u.combine("type", testTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const TestType& type = kTestTypes().at(typeName);
        const std::string code =
            "\n" + (type.requires_.empty() ? std::string("") : ("enable " + type.requires_ + ";")) +
            "\n\n" + type.header + "\n\nfn f() -> bool {\n  for (; " + type.value +
            ";) {\n    return true;\n  }\n  return false;\n}\n";
        const bool pass = typeName == "bool";
        t.expectCompileResult(pass, code);
    });

struct Test {
    const char* name;
    const char* wgsl;
    bool pass;
};

// Mirrors upstream kTests (object key order preserved).
static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"break", "for (;;) { break; }", true},
        {"init_var", "for (var i = 1;;) { break; }", true},
        {"init_var_type", "for (var i : i32 = 1;;) { break; }", true},
        {"init_var_function", "for (var<function> i = 1;;) { break; }", true},
        {"init_var_function_type", "for (var<function> i : i32 = 1;;) { break; }", true},
        {"init_let", "for (let i = 1;;) { break; }", true},
        {"init_let_type", "for (let i : u32 = 1;;) { break; }", true},
        {"init_const", "for (const i = 1;;) { break; }", true},
        {"init_const_type", "for (const i : f32 = 1;;) { break; }", true},
        {"init_call", "for (x();;) { break; }", true},
        {"init_phony", "for (_ = v;;) { break; }", true},
        {"init_increment", "for (v++;;) { break; }", true},
        {"init_compound_assign", "for (v += 3;;) { break; }", true},
        {"cond_true", "for (;true;) { break; }", true},
        {"cont_call", "for (;;x()) { break; }", true},
        {"cont_phony", "for (;;_ = v) { break; }", true},
        {"cont_increment", "for (;;v++) { break; }", true},
        {"cont_compound_assign", "for (;;v += 3) { break; }", true},
        {"init_cond", "for (var i = 1; i < 5;) {}", true},
        {"cond_cont", "for (;v < 5; v++) {}", true},
        {"init_cond_cont", "for (var i = 0; i < 5; i++) {}", true},
        {"init_shadow", "for (var f = 0; f < 5; f++) {}", true},
        {"no_semicolon", "for () { break; }", false},
        {"one_semicolon", "for (;) { break; }", false},
        {"no_paren", "for ;; { break; }", false},
        {"empty", "for (;;) {}", false},  // note: fails due to behavior-analysis
        {"init_expr", "for (true;;) { break; }", false},
        {"cond_stmt", "for (;var i = 0;) { break; }", false},
        {"cont_expr", "for (;;true) { break; }", false},
        {"cont_var", "for (;;var i = 1) { break; }", false},
        {"cont_var_type", "for (;;var i : i32 = 1) { break; }", false},
        {"cont_var_function", "for (;;var<function> i = 1) { break; }", false},
        {"cont_var_function_type", "for (;;var<function> i : i32 = 1) { break; }", false},
        {"cont_let", "for (;;let i = 1) { break; }", false},
        {"cont_let_type", "for (;;let i : u32 = 1) { break; }", false},
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
    .desc("Test that 'for' statements are parsed correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("test"));
        const std::string code =
            "\nfn f() {\n  var v = 1;\n  " + std::string(test.wgsl) + "\n}\n\nfn x() {}\n";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
