// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/binary/parse.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,binary,parse",
    "Validation tests for binary ops");

// Mirrors upstream kTests (object key order preserved).
struct ParseTest {
    const char* name;
    const char* src;
    bool pass;
};

static const std::vector<ParseTest>& kTests() {
    static const std::vector<ParseTest> v = {
        {"and_bool_literal_bool_literal", "let a = true & true;", true},
        {"and_bool_expr_bool_expr", "let a = (1 == 2) & (3 == 4);", true},
        {"and_bool_literal_bool_expr", "let a = true & (1 == 2);", true},
        {"and_bool_expr_bool_literal", "let a = (1 == 2) & true;", true},
        {"and_bool_literal_int_literal", "let a = true & 1;", false},
        {"and_int_literal_bool_literal", "let a = 1 & true;", false},
        {"and_bool_expr_int_literal", "let a = (1 == 2) & 1;", false},
        {"and_int_literal_bool_expr", "let a = 1 & (1 == 2);", false},

        {"or_bool_literal_bool_literal", "let a = true | true;", true},
        {"or_bool_expr_bool_expr", "let a = (1 == 2) | (3 == 4);", true},
        {"or_bool_literal_bool_expr", "let a = true | (1 == 2);", true},
        {"or_bool_expr_bool_literal", "let a = (1 == 2) | true;", true},
        {"or_bool_literal_int_literal", "let a = true | 1;", false},
        {"or_int_literal_bool_literal", "let a = 1 | true;", false},
        {"or_bool_expr_int_literal", "let a = (1 == 2) | 1;", false},
        {"or_int_literal_bool_expr", "let a = 1 | (1 == 2);", false},
    };
    return v;
}

static std::vector<Value> testNames() {
    std::vector<Value> values;
    for (const ParseTest& tst : kTests()) {
        values.emplace_back(std::string(tst.name));
    }
    return values;
}

static const ParseTest& findTest(const std::string& name) {
    for (const ParseTest& tst : kTests()) {
        if (name == tst.name) {
            return tst;
        }
    }
    static const ParseTest dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "all")
    .desc("Test that binary operators are validated correctly")
    .params([](ParamsBuilder u) { return u.combine("stmt", testNames()); })
    .fn([](ShaderValidationTest& t) {
        const ParseTest& tst = findTest(t.param<std::string>("stmt"));
        const std::string code =
            "\n@vertex\nfn vtx() -> @builtin(position) vec4f {\n  " + std::string(tst.src) +
            "\n  return vec4f(1);\n}\n    ";
        t.expectCompileResult(tst.pass, code);
    });

}  // namespace
