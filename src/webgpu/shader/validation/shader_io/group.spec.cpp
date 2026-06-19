// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/group.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,shader_io,group",
    "Validation tests for group");

struct AttrCase {
    const char* name;
    const char* src;
    bool pass;
};

// Mirrors upstream kTests (key order preserved for query identity).
static const std::vector<AttrCase>& kTests() {
    static const std::vector<AttrCase> cases = {
        {"const_expr", "const z = 5;\n    const y = 2;\n    @group(z + y)", true},
        {"override_expr", "override z = 5;\n    @group(z)", false},

        {"zero", "@group(0)", true},
        {"one", "@group(1)", true},
        {"comment", "@/* comment */group(1)", true},
        {"split_line", "@ \n group(1)", true},
        {"trailing_comma", "@group(1,)", true},
        {"int_literal", "@group(1i)", true},
        {"uint_literal", "@group(1u)", true},
        {"hex_literal", "@group(0x1)", true},

        {"negative", "@group(-1)", false},
        {"missing_value", "@group()", false},
        {"missing_left_paren", "@group 1)", false},
        {"missing_right_paren", "@group(1", false},
        {"multiple_values", "@group(1,2)", false},
        {"f32_val_literal", "@group(1.0)", false},
        {"f32_val", "@group(1f)", false},
        {"no_params", "@group", false},
        {"misspelling", "@agroup(1)", false},
        {"multi_group", "@group(1) @group(1)", false},
    };
    return cases;
}

static std::vector<Value> attrNames() {
    std::vector<Value> values;
    for (const AttrCase& c : kTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const AttrCase& findCase(const std::string& name) {
    for (const AttrCase& c : kTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const AttrCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "group")
    .desc("Test validation of group")
    .params([](ParamsBuilder u) {
        return u.combine("attr", attrNames());
    })
    .fn([](ShaderValidationTest& t) {
        const AttrCase& curr = findCase(t.param<std::string>("attr"));
        const std::string code =
            "\n" + std::string(curr.src) + " @binding(1)"
            "\nvar<storage> a: i32;"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  _ = a;"
            "\n}";
        t.expectCompileResult(curr.pass, code);
    });

CTS_TEST(g, "group_f16")
    .desc("Test validation of group with f16")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@group(1h) @binding(1)"
            "\nvar<storage> a: i32;"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  _ = a;"
            "\n}";
        t.expectCompileResult(false, code);
    });

} // namespace
