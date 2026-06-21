// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/while.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,statement,while",
    "Validation tests for 'while' statements'");

CTS_TEST(g, "condition_type")
    .desc("Tests that a 'while' condition must be a bool type")
    .params([](ParamsBuilder u) { return u.combine("type", testTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const TestType& type = kTestTypes().at(typeName);
        const std::string code =
            "\n" + (type.requires_.empty() ? std::string("") : ("enable " + type.requires_ + ";")) +
            "\n\n" + type.header + "\n\nfn f() -> bool {\n  while (" + type.value +
            ") {\n    return true;\n  }\n  return false;\n}\n";
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
        {"true", "while true {}", true},
        {"paren_true", "while (true) {}", true},
        {"true_break", "while true { break; }", true},
        {"true_discard", "while true { discard; }", true},
        {"true_return", "while true { return; }", true},
        {"expr", "while expr {}", true},
        {"paren_expr", "while (expr) {}", true},
        {"while", "while", false},
        {"block", "while{}", false},
        {"semicolon", "while;", false},
        {"true_lbrace", "while true {", false},
        {"true_rbrace", "while true }", false},
        {"lparen_true", "while (true {}", false},
        {"rparen_true", "while )true {}", false},
        {"true_lparen", "while true( {}", false},
        {"true_rparen", "while true) {}", false},
        {"lparen_true_lparen", "while (true( {}", false},
        {"rparen_true_rparen", "while )true) {}", false},
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
    .desc("Test that 'while' statements are parsed correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("test"));
        const std::string code =
            "\nfn f() {\n  let expr = true;\n  " + std::string(test.wgsl) + "\n}";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
