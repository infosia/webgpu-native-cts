// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/const_assert.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,statement,const_assert",
    "Parser validation tests for const_assert");

struct Case {
    const char* name;
    const char* code;
    bool pass;
};

// Mirrors upstream kCases (object key order preserved).
static const std::vector<Case>& kCases() {
    static const std::vector<Case> v = {
        {"no_parentheses", "const_assert true;", true},
        {"left_parenthesis_only", "const_assert(true;", false},
        {"right_parenthesis_only", "const_assert true);", false},
        {"both_parentheses", "const_assert(true);", true},
        {"condition_on_newline", "const_assert\ntrue;", true},
        {"multiline_with_parentheses", "const_assert\n(\n  true\n);", true},
        {"invalid_expression", "const_assert(1!2);", false},
        {"no_condition_no_parentheses", "const_assert;", false},
        {"no_condition_with_parentheses", "const_assert();", false},
        {"not_a_boolean", "const_assert 42;", false},
    };
    return v;
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
    static const Case dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "parse")
    .desc("Tests that the const_assert statement parses correctly.")
    .params([](ParamsBuilder u) { return u.combine("case", caseNames()); })
    .fn([](ShaderValidationTest& t) {
        const Case& c = findCase(t.param<std::string>("case"));
        t.expectCompileResult(c.pass, c.code);
    });

}  // namespace
