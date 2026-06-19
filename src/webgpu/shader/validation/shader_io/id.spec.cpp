// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/id.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,shader_io,id",
    "Validation tests for id");

struct AttrCase {
    const char* name;
    const char* src;
    bool pass;
};

// Mirrors upstream kTests (key order preserved for query identity).
static const std::vector<AttrCase>& kTests() {
    static const std::vector<AttrCase> cases = {
        {"zero", "@id(0)", true},
        {"one", "@id(1)", true},
        {"hex", "@id(0x1)", true},
        {"trailing_comma", "@id(1,)", true},
        {"i32", "@id(1i)", true},
        {"ui32", "@id(1u)", true},
        {"largest", "@id(65535)", true},
        {"newline", "@\nid(1)", true},
        {"comment", "@/* comment */id(1)", true},
        {"const_expr", "const z = 5;\n      const y = 2;\n      @id(z + y)", true},

        {"misspelling", "@aid(1)", false},
        {"empty", "@id()", false},
        {"missing_left_paren", "@id 1)", false},
        {"missing_right_paren", "@id(1", false},
        {"multi_value", "@id(1, 2)", false},
        {"overide_expr", "override z = 5;\n      override y = 2;\n      @id(z + y)", false},
        {"f32_literal", "@id(1.0)", false},
        {"f32", "@id(1f)", false},
        {"negative", "@id(-1)", false},
        {"too_large", "@id(65536)", false},
        {"no_params", "@id", false},
        {"duplicate", "@id(1) @id(1)", false},
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

CTS_TEST(g, "id")
    .desc("Test validation of id")
    .params([](ParamsBuilder u) {
        return u.combine("attr", attrNames());
    })
    .fn([](ShaderValidationTest& t) {
        const AttrCase& curr = findCase(t.param<std::string>("attr"));
        const std::string code =
            "\n" + std::string(curr.src) +
            "\noverride a = 4;"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {}";
        t.expectCompileResult(curr.pass, code);
    });

CTS_TEST(g, "id_fp16")
    .desc("Test validation of id with fp16")
    .params([](ParamsBuilder u) {
        return u.combine("ext", {std::string(""), std::string("h")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ext = t.param<std::string>("ext");
        const std::string code =
            "\n@id(1" + ext + ")"
            "\noverride a = 4;"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {}";
        t.expectCompileResult(ext.empty(), code);
    });

CTS_TEST(g, "id_struct_member")
    .desc("Test validation of id with struct member")
    .params([](ParamsBuilder u) {
        return u.combine("id", {std::string("@id(1) override"), std::string("@id(1)"), std::string("")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string id = t.param<std::string>("id");
        const std::string code =
            "\nstruct S {"
            "\n  " + id + " a: i32,"
            "\n}"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {}";
        t.expectCompileResult(id.empty(), code);
    });

CTS_TEST(g, "id_non_override")
    .desc("Test validation of id with non-override")
    .params([](ParamsBuilder u) {
        return u.combine("type", {std::string("var"), std::string("const"), std::string("override")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string code =
            "\n@id(1) " + type + " a = 4;"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {}";
        t.expectCompileResult(type == "override", code);
    });

CTS_TEST(g, "id_in_function")
    .desc("Test validation of id inside a function")
    .params([](ParamsBuilder u) {
        return u.combine("id", {std::string("@id(1)"), std::string("")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string id = t.param<std::string>("id");
        const std::string code =
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  " + id + " var a = 4;"
            "\n}";
        t.expectCompileResult(id.empty(), code);
    });

} // namespace
