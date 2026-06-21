// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/loop.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,statement,loop",
    "Validation tests for 'loop' statements'");

CTS_TEST(g, "break_if_type")
    .desc("Tests that a 'break if' condition must be a bool type")
    .params([](ParamsBuilder u) { return u.combine("type", testTypeNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const TestType& type = kTestTypes().at(typeName);
        const std::string code =
            "\n" + (type.requires_.empty() ? std::string("") : ("enable " + type.requires_ + ";")) +
            "\n\n" + type.header + "\n\nfn f() {\n  loop {\n    continuing {\n      break if " +
            type.value + ";\n    }\n  }\n}\n";
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
        {"break", "loop { break; }", true},
        {"return", "loop { return; }", true},
        {"break_continuing", "loop { break; continuing {} }", true},
        {"var_break", "loop { var a = 1; break; }", true},
        {"var_break_continuing_inc", "loop { var a = 1; break; continuing { a += 1; }}", true},
        {"var_break_continuing_discard", "loop { var a = 1; break; continuing { discard; }}", true},
        {"continuing_break_if", "loop { continuing { break if true; } }", true},
        {"expr_break", "loop expr { break; }", false},
        {"loop", "loop", false},
        {"continuing_break", "loop { continuing {} break; }", false},
        {"break_continuing_continue", "loop { break; continuing { continue; } }", false},
        {"break_continuing_return", "loop { break; continuing { return; } }", false},
        {"break_continuing_if_break", "loop { break; continuing { if true { break; } }", false},
        {"break_continuing_if_return", "loop { break; continuing { if true { return; } }", false},
        {"break_continuing_lbrace", "loop { break; continuing { }", false},
        {"break_continuing_rbrace", "loop { break; continuing } }", false},
        {"continuing", "loop { continuing {} }", false},
        {"semicolon", "loop;", false},
        {"lbrace", "loop {", false},
        {"rbrace", "loop }", false},
        {"lparen", "loop ({}", false},
        {"rparen", "loop ){}", false},
        // note: these parse, but fails due to behavior-analysis
        {"continue", "loop { continue; }", false},
        {"discard", "loop { discard; }", false},
        {"empty", "loop{}", false},
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
    .desc("Test that 'loop' statements are parsed correctly.")
    .params([](ParamsBuilder u) { return u.combine("test", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("test"));
        const std::string code =
            "\nfn f() {\n  let expr = true;\n  " + std::string(test.wgsl) + "\n}";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
