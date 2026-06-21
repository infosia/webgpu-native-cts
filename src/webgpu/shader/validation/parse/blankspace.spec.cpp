// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/blankspace.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting note: upstream combines on `[codepoint, name]` array literals. The
// harness `Value` cannot store arrays, so the `blankspace` param is keyed by the
// array's NAME (its second element, e.g. "line_feed"); the corresponding code
// point is reconstructed in the body. Case count and order match upstream.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,parse,blankspace",
    "Validation tests for blankspace handling");

// UTF-8 encoding of a Unicode code point.
static std::string utf8(unsigned int cp) {
    std::string s;
    if (cp < 0x80) {
        s.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        s.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        s.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        s.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        s.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    return s;
}

struct Blankspace {
    const char* name;
    unsigned int cp;
};

static const std::vector<Blankspace>& kBlankspaces() {
    static const std::vector<Blankspace> v = {
        {"space", 0x0020},
        {"horizontal_tab", 0x0009},
        {"line_feed", 0x000a},
        {"vertical_tab", 0x000b},
        {"form_feed", 0x000c},
        {"carriage_return", 0x000d},
        {"next_line", 0x0085},
        {"left_to_right_mark", 0x200e},
        {"right_to_left_mark", 0x200f},
        {"line_separator", 0x2028},
        {"paragraph_separator", 0x2029},
    };
    return v;
}

static std::vector<Value> blankspaceNames() {
    std::vector<Value> values;
    for (const Blankspace& b : kBlankspaces()) {
        values.emplace_back(std::string(b.name));
    }
    return values;
}

static unsigned int blankspaceCp(const std::string& name) {
    for (const Blankspace& b : kBlankspaces()) {
        if (name == b.name) {
            return b.cp;
        }
    }
    return 0;
}

CTS_TEST(g, "null_characters")
    .desc("Test that WGSL source containing a null character is rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("contains_null", {Value(true), Value(false)})
            .combine("placement", {"comment", "delimiter", "eol"})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const bool containsNull = t.param<bool>("contains_null");
        const std::string placement = t.param<std::string>("placement");
        const std::string nul(1, '\0');
        std::string code;
        if (placement == "comment") {
            code = "// Here is a " + (containsNull ? nul : std::string("Z")) + " character";
        } else if (placement == "delimiter") {
            code = "const" + (containsNull ? nul : std::string(" ")) + "name : i32 = 0;";
        } else if (placement == "eol") {
            code = "const name : i32 = 0;" + (containsNull ? nul : std::string(""));
        }
        t.expectCompileResult(!containsNull, code);
    });

CTS_TEST(g, "blankspace")
    .desc("Test that all blankspace characters act as delimiters.")
    .params([](ParamsBuilder u) {
        return u.combine("blankspace", blankspaceNames()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("blankspace");
        const std::string code = "const" + utf8(blankspaceCp(name)) + "ident : i32 = 0;";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "bom")
    .desc("Tests that including a BOM causes a shader compile error.")
    .params([](ParamsBuilder u) {
        return u.combine("include_bom", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const bool includeBom = t.param<bool>("include_bom");
        const std::string code =
            (includeBom ? utf8(0xFEFF) : std::string("")) + "const name : i32 = 0;";
        t.expectCompileResult(!includeBom, code);
    });

}  // namespace
