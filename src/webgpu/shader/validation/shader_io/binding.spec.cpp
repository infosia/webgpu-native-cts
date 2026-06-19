// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/binding.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,binding",
    "Validation tests for binding");

struct Case {
    const char* name;
    const char* src;
    bool pass;
};

// Mirrors upstream kTests (object key order preserved for query identity).
static const std::vector<Case>& kTests() {
    static const std::vector<Case> cases = {
        {"const_expr", "const z = 5;\n    const y = 2;\n    @binding(z + y)", true},
        {"override_expr", "override z = 5;\n    @binding(z)", false},

        {"zero", "@binding(0)", true},
        {"one", "@binding(1)", true},
        {"comment", "@/* comment */binding(1)", true},
        {"split_line", "@ \n binding(1)", true},
        {"trailing_comma", "@binding(1,)", true},
        {"int_literal", "@binding(1i)", true},
        {"uint_literal", "@binding(1u)", true},
        {"hex_literal", "@binding(0x1)", true},

        {"negative", "@binding(-1)", false},
        {"missing_value", "@binding()", false},
        {"missing_left_paren", "@binding 1)", false},
        {"missing_right_paren", "@binding(1", false},
        {"multiple_values", "@binding(1,2)", false},
        {"f32_val_literal", "@binding(1.0)", false},
        {"f32_val", "@binding(1f)", false},
        {"no_params", "@binding", false},
        {"misspelling", "@abinding(1)", false},
        {"multi_binding", "@binding(1) @binding(1)", false},
    };
    return cases;
}

static std::vector<Value> caseNames() {
    std::vector<Value> values;
    for (const Case& c : kTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const Case& findCase(const std::string& name) {
    for (const Case& c : kTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const Case dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "binding")
    .desc("Test validation of binding")
    .params([](ParamsBuilder u) {
        return u.combine("attr", caseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Case& curr = findCase(t.param<std::string>("attr"));
        const std::string code =
            std::string("\n") + curr.src + " @group(1)"
            "\nvar<storage> a: i32;"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  _ = a;"
            "\n}";
        t.expectCompileResult(curr.pass, code);
    });

CTS_TEST(g, "binding_f16")
    .desc("Test validation of binding with f16")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@group(1) @binding(1h)"
            "\nvar<storage> a: i32;"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  _ = a;"
            "\n}";
        t.expectCompileResult(false, code);
    });

} // namespace
