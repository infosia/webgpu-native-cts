// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/enable.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,parse,enable",
    "Parser validation tests for enable");

struct EnableCase {
    const char* name;
    const char* code;
    bool pass;
};

// Mirrors upstream kCases (object key order preserved).
static const std::vector<EnableCase>& kCases() {
    static const std::vector<EnableCase> v = {
        {"f16", "enable f16;", true},
        {"decl_before", "alias i = i32;\nenable f16;", false},
        {"decl_after", "enable f16;\nalias i = i32;", true},
        {"requires_before",
         "requires readonly_and_readwrite_storage_textures;\nenable f16;", true},
        {"diagnostic_before",
         "diagnostic(info, derivative_uniformity);\nenable f16;", true},
        {"const_assert_before", "const_assert 1 == 1;\nenable f16;", false},
        {"const_assert_after", "enable f16;\nconst_assert 1 == 1;", true},
        {"embedded_comment", "/* comment\n\n*/enable f16;", true},
        {"parens", "enable(f16);", false},
        {"multi_line", "enable\nf16;", true},
        {"multiple_enables", "enable f16;\nenable f16;", true},
        {"multiple_entries", "enable f16, f16, f16;", true},
        {"unknown", "enable unknown;", false},
        {"subgroups", "enable subgroups;", true},
        {"subgroups_f16_pass1", "\n    enable f16;\n    enable subgroups;", true},
        {"subgroups_f16_pass2", "\n    enable subgroups;\n    enable f16;", true},
        {"in_comment_f16", "\n    /* enable f16; */\n    var<private> v: f16;\n    ", false},
    };
    return v;
}

static std::vector<Value> caseNames() {
    std::vector<Value> values;
    for (const EnableCase& c : kCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const EnableCase& findCase(const std::string& name) {
    for (const EnableCase& c : kCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const EnableCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "enable")
    .desc("Tests that enables are validated correctly")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string caseName = t.param<std::string>("case");
        if (caseName == "requires_before") {
            t.skipIfLanguageFeatureNotSupported("readonly_and_readwrite_storage_textures");
        }
        const EnableCase& c = findCase(caseName);
        t.expectCompileResult(c.pass, c.code);
    });

}  // namespace
