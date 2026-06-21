// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/continue.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,continue",
    "Validation tests for continue");

struct Test {
    const char* name;
    const char* src;
    bool pass;
};

// Mirrors upstream kTests (object key order preserved).
static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"continue", "continue;", false},
        {"compound_continue", "{ continue; }", false},
        {"loop_continue", "loop { if false { break; } continue; }", true},
        {"while_continue", "while true { continue; }", true},
        {"for_continue", "for (;true;) { continue; }", true},
        {"continuing_continue", "loop { continuing { continue; } }", false},
        {"continuing_nested_loop_continue",
         "loop { if false { break; } continuing { loop { if false { break; } continue; } } }",
         true},
        {"if_continue", "if true { continue; }", false},
        {"nested_if_continue", "while true { if true { continue; } }", true},
        {"switch_case_continue", "switch(1) { default: { continue; } }", false},
        {"nested_switch_case_continue", "while true { switch(1) { default: { continue; } } }", true},
        {"return_continue", "return continue;", false},
        {"loop_continue_after_decl_used_in_continuing",
         "loop { let cond = false; continue; continuing { break if cond; } }", true},
        {"loop_continue_before_decl_used_in_continuing",
         "loop { continue; let cond = false; continuing { break if cond; } }", false},
        {"loop_continue_before_decl_not_used_in_continuing",
         "loop { continue; let cond = false; continuing { break if false; } }", true},
        {"loop_nested_continue_before_decl_used_in_continuing",
         "loop { if false { continue; } let cond = false; continuing { break if cond; } }", false},
        {"loop_continue_expression", "loop { if false { break; } continue true; }", false},
        {"for_init_continue", "for (continue;;) { break; }", false},
        {"for_condition_continue", "for (;continue;) { break; }", false},
        {"for_continue_continue", "for (;;continue) { break; }", false},
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
    .desc("Test that continue placement is validated correctly")
    .params([](ParamsBuilder u) { return u.combine("stmt", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("stmt"));
        const std::string code =
            "\n@vertex\nfn vtx() -> @builtin(position) vec4f {\n  " + std::string(test.src) +
            "\n  return vec4f(1);\n}\n    ";
        t.expectCompileResult(test.pass, code);
    });

CTS_TEST(g, "module_scope")
    .desc("Test that continue is not valid at module-scope.")
    .fn([](ShaderValidationTest& t) {
        const std::string code = "\ncontinue;\n    ";
        t.expectCompileResult(false, code);
    });

}  // namespace
