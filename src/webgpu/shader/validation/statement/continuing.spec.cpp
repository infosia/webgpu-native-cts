// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/continuing.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,statement,continuing",
    "Validation tests for continuing");

struct Test {
    const char* name;
    const char* src;
    bool pass;
};

// Mirrors upstream kTests (object key order preserved).
static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"continuing_break_if", "loop { continuing { break if true; } }", true},
        {"continuing_empty", "loop { if a == 4 { break; } continuing { } }", true},
        {"continuing_break_if_parens", "loop { continuing { break if (true); } }", true},
        {"continuing_discard", "loop { if a == 4 { break; } continuing { discard; } }", true},
        {"continuing_continue_nested",
         "loop { if a == 4 { break; } continuing { loop { if a == 4 { break; } continue; } } }",
         true},
        {"continuing_continue", "loop { if a == 4 { break; } continuing { continue; } }", false},
        {"continuing_break", "loop { continuing { break; } }", false},
        {"continuing_for", "loop { if a == 4 { break; } continuing { for(;a < 4;) { } } }", true},
        {"continuing_for_break",
         "loop { if a == 4 { break; } continuing { for(;;) { break; } } }", true},
        {"continuing_while", "loop { if a == 4 { break; } continuing { while a < 4 { } } }", true},
        {"continuing_while_break",
         "loop { if a == 4 { break; } continuing { while true { break; } } }", true},
        {"continuing_semicolon", "loop { if a == 4 { break; } continuing { ; } }", true},
        {"continuing_functionn_call",
         "loop { if a == 4 { break; } continuing { _ = b(); } }", true},
        {"continuing_let", "loop { if a == 4 { break; } continuing { let c = b(); } }", true},
        {"continuing_var", "loop { if a == 4 { break; } continuing { var a = b(); } }", true},
        {"continuing_const", "loop { if a == 4 { break; } continuing { const a = 1; } }", true},
        {"continuing_block", "loop { if a == 4 { break; } continuing { { } } }", true},
        {"continuing_const_assert",
         "loop { if a == 4 { break; } continuing { const_assert(1 != 2); } }", true},
        {"continuing_loop", "loop { if a == 4 { break; } continuing { loop { break; } } }", true},
        {"continuing_if",
         "loop { if a == 4 { break; } continuing { if true { } else if false { } else { } } }",
         true},
        {"continuing_switch",
         "loop { if a == 4 { break; } continuing { switch 2 { default: { } } } }", true},
        {"continuing_switch_break",
         "loop { if a == 4 { break; } continuing { switch 2 { default: { break; } } } }", true},
        {"continuing_loop_nested_continuing",
         "loop { if a == 4 { break; } continuing { loop { if a == 4 { break; } continuing { } } } }",
         true},
        {"continuing_inc", "loop { if a == 4 { break; } continuing { a += 1; } }", true},
        {"continuing_dec", "loop { if a == 4 { break; } continuing { a -= 1; } }", true},
        {"while", "while a < 4 { continuing { break if true; } }", false},
        {"for", "for (;a < 4;) { continuing { break if true; } }", false},
        {"switch_case", "switch(1) { default: { continuing { break if true; } } }", false},
        {"switch", "switch(1) { continuing { break if true; } }", false},
        {"continuing", "continuing { break if true; }", false},
        {"return", "return continuing { break if true; }", false},
        {"if_body", "if true { continuing { break if true; } }", false},
        {"if", "if true { } continuing { break if true; } }", false},
        {"if_else", "if true { } else { } continuing { break if true; } }", false},
        {"continuing_continuing",
         "loop { if a == 4 { break; } continuing { continuing { break if true; } } }", false},
        {"no_body", "loop { if a == 4 { break; } continuing }", false},
        {"return_in_continue",
         "loop { if a == 4 { break; } continuing { return vec4f(2); } }", false},
        {"return_if_nested_in_continue",
         "loop { if a == 4 { break; } continuing { if true { return vec4f(2); } } }", false},
        {"return_for_nested_in_continue",
         "loop { if a == 4 { break; } continuing { for(;a < 4;) { return vec4f(2); } } }", false},
        {"continuing_semicolon_break_if",
         "loop { continuing { ; break if (true); } }", true},
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
    .desc("Test that continuing placement is validated correctly")
    .params([](ParamsBuilder u) { return u.combine("stmt", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("stmt"));
        const std::string code =
            "\nfn b() -> i32 {\n  return 1;\n}\n\n@fragment\nfn frag() -> @location(0) vec4f {\n"
            "  var a = 0;\n  " +
            std::string(test.src) + "\n  return vec4f(1);\n}\n    ";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
