// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/break_if.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// The kTestTypes table is imported from ./test_types.ts upstream; it is ported
// verbatim here (one local copy per importing file, mirroring the import).

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
    "shader,validation,statement,break_if",
    "Validation tests for 'break if' statements'");

CTS_TEST(g, "condition_type")
    .desc("Tests that an 'break if' condition must be a bool type")
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
    const char* src;
    bool pass;
};

// Mirrors upstream kTests (object key order preserved).
static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"compound_break", "{ break if true; }", false},
        {"loop_break", "loop { break if true; }", false},
        {"loop_if_break", "loop { if true { break if false; } }", false},
        {"continuing_break_if", "loop { continuing { break if true; } }", true},
        {"continuing_break_if_parens", "loop { continuing { break if (true); } }", true},
        {"continuing_break_if_not_last",
         "loop { continuing { break if (true); let a = 4;} }", false},
        {"while_break", "while true { break if true; }", false},
        {"while_if_break", "while true { if true { break if true; } }", false},
        {"for_break", "for (;;) { break if true; }", false},
        {"for_if_break", "for (;;) { if true { break if true; } }", false},
        {"switch_case_break", "switch(1) { default: { break if true; } }", false},
        {"switch_case_if_break", "switch(1) { default: { if true { break if true; } } }", false},
        {"break", "break if true;", false},
        {"return_break", "return break if true;", false},
        {"if_break", "if true { break if true; }", false},
        {"continuing_if_break", "loop { continuing { if (true) { break if true; } } }", false},
        {"switch_break", "switch(1) { break if true; }", false},
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

CTS_TEST(g, "placement")
    .desc("Test that break if placement is validated correctly")
    .params([](ParamsBuilder u) { return u.combine("stmt", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("stmt"));
        const std::string code =
            "\n@vertex\nfn vtx() -> @builtin(position) vec4f {\n  " + std::string(test.src) +
            "\n  return vec4f(1);\n}\n    ";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
