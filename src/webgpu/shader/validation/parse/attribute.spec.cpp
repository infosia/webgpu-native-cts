// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/attribute.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,parse,attribute",
    "Validation tests for attributes");

// Mirrors upstream kPossibleValues (object key order preserved).
struct PossibleValue {
    const char* key;
    const char* val;
};
static const std::vector<PossibleValue>& kPossibleValues() {
    static const std::vector<PossibleValue> v = {
        {"val", "32"},
        {"expr", "30 + 2"},
        {"override", "a_override"},
        {"user_func", "a_func()"},
        {"const_func", "min(4, 8)"},
        {"const", "a_const"},
    };
    return v;
}
static std::vector<Value> possibleValueKeys() {
    std::vector<Value> values;
    for (const PossibleValue& p : kPossibleValues()) {
        values.emplace_back(std::string(p.key));
    }
    return values;
}
static std::string possibleValue(const std::string& key) {
    for (const PossibleValue& p : kPossibleValues()) {
        if (key == p.key) {
            return p.val;
        }
    }
    return "";
}

// Mirrors upstream kAttributeUsage / kAllowedUsages (object key order preserved).
struct AttributeUsage {
    const char* key;
    const char* usage;                 // template with $val
    std::vector<const char*> allowed;  // allowed value keys
};
static const std::vector<AttributeUsage>& kAttributes() {
    static const std::vector<AttributeUsage> v = {
        {"align", "@align($val)", {"val", "expr", "const", "const_func"}},
        {"binding", "@binding($val) @group(0)", {"val", "expr", "const", "const_func"}},
        {"group", "@binding(1) @group($val)", {"val", "expr", "const", "const_func"}},
        {"id", "@id($val)", {"val", "expr", "const", "const_func"}},
        {"location", "@location($val)", {"val", "expr", "const", "const_func"}},
        {"size", "@size($val)", {"val", "expr", "const", "const_func"}},
        {"workgroup_size",
         "@workgroup_size($val, $val, $val)",
         {"val", "expr", "const", "const_func", "override"}},
    };
    return v;
}
static std::vector<Value> attributeKeys() {
    std::vector<Value> values;
    for (const AttributeUsage& a : kAttributes()) {
        values.emplace_back(std::string(a.key));
    }
    return values;
}
static const AttributeUsage& findAttribute(const std::string& key) {
    for (const AttributeUsage& a : kAttributes()) {
        if (key == a.key) {
            return a;
        }
    }
    static const AttributeUsage dummy{"", "", {}};
    return dummy;
}

static std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

CTS_TEST(g, "expressions")
    .desc("Tests attributes which allow expressions")
    .params([](ParamsBuilder u) {
        return u.combine("value", possibleValueKeys()).combine("attribute", attributeKeys());
    })
    .fn([](ShaderValidationTest& t) {
        // Default attribute strings (object key order preserved).
        std::string a_align = "";
        std::string a_binding = "@binding(0) @group(0)";
        std::string a_group = "@binding(1) @group(1)";
        std::string a_id = "@id(2)";
        std::string a_location = "@location(0)";
        std::string a_size = "";
        std::string a_workgroup_size = "@workgroup_size(1)";

        const std::string valueKey = t.param<std::string>("value");
        const std::string attributeKey = t.param<std::string>("attribute");
        const AttributeUsage& attr = findAttribute(attributeKey);
        const std::string val = possibleValue(valueKey);
        const std::string replaced = replaceAll(attr.usage, "$val", val);

        if (attributeKey == "align") {
            a_align = replaced;
        } else if (attributeKey == "binding") {
            a_binding = replaced;
        } else if (attributeKey == "group") {
            a_group = replaced;
        } else if (attributeKey == "id") {
            a_id = replaced;
        } else if (attributeKey == "location") {
            a_location = replaced;
        } else if (attributeKey == "size") {
            a_size = replaced;
        } else if (attributeKey == "workgroup_size") {
            a_workgroup_size = replaced;
        }

        const std::string code =
            "\nfn a_func() -> i32 {"
            "\n    return 4;"
            "\n}"
            "\n"
            "\nconst a_const = -2 + 10;"
            "\noverride a_override: i32 = 2;"
            "\n"
            "\n" + a_id + " override my_id: i32 = 4;"
            "\n"
            "\nstruct B {"
            "\n  " + a_align + " " + a_size + " a: i32,"
            "\n}"
            "\n"
            "\n" + a_binding +
            "\nvar<uniform> uniform_buffer_1: B;"
            "\n"
            "\n" + a_group +
            "\nvar<uniform> uniform_buffer_2: B;"
            "\n"
            "\n@fragment"
            "\nfn main() -> " + a_location + " vec4<f32> {"
            "\n  return vec4<f32>(.4, .2, .3, .1);"
            "\n}"
            "\n"
            "\n@compute"
            "\n" + a_workgroup_size +
            "\nfn compute_main() {}"
            "\n";

        bool pass = false;
        for (const char* allowedKey : attr.allowed) {
            if (valueKey == allowedKey) {
                pass = true;
                break;
            }
        }
        t.expectCompileResult(pass, code);
    });

}  // namespace
