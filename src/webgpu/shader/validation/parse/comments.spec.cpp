// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/comments.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting note: upstream combines on `[string, name]` array literals. The
// harness `Value` cannot store arrays, so the `blankspace` param is keyed by the
// array's NAME (its second element); the corresponding string is reconstructed
// in the body. Case count and order match upstream.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,parse,comments",
    "Validation tests for comments");

CTS_TEST(g, "comments")
    .desc("Test that valid comments are handled correctly, including nesting.")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n/**"
            "\n * Here is my shader."
            "\n *"
            "\n * /* I can nest /**/ comments. */"
            "\n * // I can nest line comments too."
            "\n **/"
            "\n@fragment // This is the stage"
            "\nfn main(/*"
            "\nno"
            "\nparameters"
            "\n*/) -> @location(0) vec4<f32> {"
            "\n  return/*block_comments_delimit_tokens*/vec4<f32>(.4, .2, .3, .1);"
            "\n}/* terminated block comments are OK at EOF...*/";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "line_comment_eof")
    .desc("Test that line comments can come at EOF.")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@fragment"
            "\nfn main() -> @location(0) vec4<f32> {"
            "\n  return vec4<f32>(.4, .2, .3, .1);"
            "\n}"
            "\n// line comments are OK at EOF...";
        t.expectCompileResult(true, code);
    });

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

struct Terminator {
    const char* name;
    std::string value;  // the blankspace string
    bool isSpaceOrTab;   // upstream: [' ', '\t'].includes(blankspace[0])
};

static const std::vector<Terminator>& kTerminators() {
    static const std::vector<Terminator> v = {
        {"space", " ", true},
        {"tab", "\t", true},
        {"line_feed", utf8(0x000a), false},
        {"vertical_tab", utf8(0x000b), false},
        {"form_feed", utf8(0x000c), false},
        {"carriage_return", utf8(0x000d), false},
        {"carriage_return_line_feed", utf8(0x000d) + utf8(0x000a), false},
        {"next_line", utf8(0x0085), false},
        {"line_separator", utf8(0x2028), false},
        {"paragraph_separator", utf8(0x2029), false},
    };
    return v;
}

static std::vector<Value> terminatorNames() {
    std::vector<Value> values;
    for (const Terminator& term : kTerminators()) {
        values.emplace_back(std::string(term.name));
    }
    return values;
}

static const Terminator& findTerminator(const std::string& name) {
    for (const Terminator& term : kTerminators()) {
        if (name == term.name) {
            return term;
        }
    }
    static const Terminator dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "line_comment_terminators")
    .desc("Test that line comments are terminated by any blankspace other than space and \t")
    .params([](ParamsBuilder u) {
        return u.combine("blankspace", terminatorNames()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const Terminator& term = findTerminator(t.param<std::string>("blankspace"));
        const std::string code =
            "// Line comment" + term.value + "const invalid_outside_comment = should_fail";
        t.expectCompileResult(term.isSpaceOrTab, code);
    });

CTS_TEST(g, "unterminated_block_comment")
    .desc("Test that unterminated block comments cause an error")
    .params([](ParamsBuilder u) {
        return u.combine("terminated", {Value(true), Value(false)}).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const bool terminated = t.param<bool>("terminated");
        const std::string code =
            "\n/**"
            "\n * Unterminated block comment."
            "\n *"
            "\n " + std::string(terminated ? "*/" : "");
        t.expectCompileResult(terminated, code);
    });

}  // namespace
