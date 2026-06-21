// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/if.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,statement,if",
    "Validation tests for 'if' statements'");

CTS_TEST(g, "condition_type")
    .desc("Tests that an 'if' condition must be a bool type")
    .params([](ParamsBuilder u) { return u.combine("type", testTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const TestType& type = kTestTypes().at(typeName);
        const std::string code =
            "\n" + (type.requires_.empty() ? std::string("") : ("enable " + type.requires_ + ";")) +
            "\n\n" + type.header + "\n\nfn f() -> bool {\n  if " + type.value +
            " {\n    return true;\n  }\n  return false;\n}\n";
        const bool pass = typeName == "bool";
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "else_condition_type")
    .desc("Tests that an 'else if' condition must be a bool type")
    .params([](ParamsBuilder u) { return u.combine("type", testTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const TestType& type = kTestTypes().at(typeName);
        const std::string code =
            "\n" + (type.requires_.empty() ? std::string("") : ("enable " + type.requires_ + ";")) +
            "\n\n" + type.header + "\n\nfn f(c : bool) -> bool {\n  if (c) {\n    return true;\n  "
            "} else if (" +
            type.value + ") {\n    return true;\n  }\n  return false;\n}\n";
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
        {"true", "if true {}", true},
        {"paren_true", "if (true) {}", true},
        {"expr", "if expr {}", true},
        {"paren_expr", "if (expr) {}", true},
        {"true_else", "if true {} else {}", true},
        {"paren_true_else", "if (true) {} else {}", true},
        {"expr_else", "if expr {} else {}", true},
        {"paren_expr_else", "if (expr) {} else {}", true},
        {"true_else_if_true", "if true {} else if true {}", true},
        {"paren_true_else_if_paren_true", "if (true) {} else if (true) {}", true},
        {"true_else_if_paren_true", "if true {} else if (true) {}", true},
        {"paren_true_else_if_true", "if (true) {} else if true {}", true},
        {"expr_else_if_expr", "if expr {} else if expr {}", true},
        {"paren_expr_else_if_paren_expr", "if (expr) {} else if (expr) {}", true},
        {"expr_else_if_paren_expr", "if expr {} else if (expr) {}", true},
        {"paren_expr_else_if_expr", "if (expr) {} else if expr {}", true},
        {"if", "if", false},
        {"block", "if{}", false},
        {"semicolon", "if;", false},
        {"true_lbrace", "if true {", false},
        {"true_rbrace", "if true }", false},
        {"lparen_true", "if (true {}", false},
        {"rparen_true", "if )true {}", false},
        {"true_lparen", "if true( {}", false},
        {"true_rparen", "if true) {}", false},
        {"true_else_if_no_block", "if true {} else if ", false},
        {"true_else_if", "if true {} else if {}", false},
        {"true_else_if_semicolon", "if true {} else if ;", false},
        {"true_else_if_true_lbrace", "if true {} else if true {", false},
        {"true_else_if_true_rbrace", "if true {} else if true }", false},
        {"true_else_if_lparen_true", "if true {} else if (true {}", false},
        {"true_else_if_rparen_true", "if true {} else if )true {}", false},
        {"true_else_if_true_lparen", "if true {} else if true( {}", false},
        {"true_else_if_true_rparen", "if true {} else if true) {}", false},
        {"else", "else { }", false},
        {"else_if", "else if true { }", false},
        {"true_elif", "if (true) { } elif (true) {}", false},
        {"true_elsif", "if (true) { } elsif (true) {}", false},
        {"elif", "elif (true) {}", false},
        {"elsif", "elsif (true) {}", false},
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
    .desc("Test that 'if' statements are parsed correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("test"));
        const std::string code =
            "\nfn f() {\n  let expr = true;\n  " + std::string(test.wgsl) + "\n}";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
