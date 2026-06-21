// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/break.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,statement,break",
    "Validation tests for break");

struct Test {
    const char* name;
    const char* src;
    bool pass;
};

// Mirrors upstream kTests (object key order preserved).
static const std::vector<Test>& kTests() {
    static const std::vector<Test> v = {
        {"loop_break", "loop { break; }", true},
        {"loop_if_break", "loop { if true { break; } }", true},
        {"while_break", "while true { break; }", true},
        {"while_if_break", "while true { if true { break; } }", true},
        {"for_break", "for (;;) { break; }", true},
        {"for_if_break", "for (;;) { if true { break; } }", true},
        {"switch_case_break", "switch(1) { default: { break; } }", true},
        {"switch_case_if_break", "switch(1) { default: { if true { break; } } }", true},
        {"break", "break;", false},
        {"return_break", "return break;", false},
        {"if_break", "if true { break; }", false},
        {"continuing_break", "loop { continuing { break; } }", false},
        {"continuing_if_break", "loop { continuing { if (true) { break; } } }", false},
        {"switch_break", "switch(1) { break; }", false},
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
    .desc("Test that break placement is validated correctly")
    .params([](ParamsBuilder u) { return u.combine("stmt", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const Test& test = findTest(t.param<std::string>("stmt"));
        const std::string code =
            "\n@vertex\nfn vtx() -> @builtin(position) vec4f {\n  " + std::string(test.src) +
            "\n  return vec4f(1);\n}\n    ";
        t.expectCompileResult(test.pass, code);
    });

}  // namespace
