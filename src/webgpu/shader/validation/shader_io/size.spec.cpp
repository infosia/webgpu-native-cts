// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/size.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,shader_io,size", "Validation tests for size");

// ---- kSizeTests -----------------------------------------------------------
struct SizeTest {
    const char* name;
    const char* src;
    bool pass;
};

static const std::vector<SizeTest>& kSizeTests() {
    static const std::vector<SizeTest> tests = {
        {"valid", "@size(4)", true},
        {"non_align_size", "@size(5)", true},
        {"i32", "@size(4i)", true},
        {"u32", "@size(4u)", true},
        {"constant", "@size(z)", true},
        {"const_expr", "@size(z + 4)", true},
        {"trailing_comma", "@size(4,)", true},
        {"hex", "@size(0x4)", true},
        {"whitespace", "@\nsize(4)", true},
        {"comment", "@/* comment */size(4)", true},
        {"large", "@size(2147483647)", true},
        {"misspelling", "@msize(4)", false},
        {"no_value", "@size()", false},
        {"missing_left_paren", "@size 4)", false},
        {"missing_right_paren", "@size(4", false},
        {"missing_parens", "@size", false},
        {"multiple_values", "@size(4, 8)", false},
        {"override", "@size(over)", false},
        {"zero", "@size(0)", false},
        {"negative", "@size(-4)", false},
        {"f32_literal", "@size(4.0)", false},
        {"f32", "@size(4f)", false},
        {"duplicate1", "@size(4) @size(4)", false},
        {"duplicate2", "@size(4) @size(8)", false},
        {"too_small", "@size(1)", false},
    };
    return tests;
}

static std::vector<Value> sizeTestNames() {
    std::vector<Value> values;
    for (const SizeTest& s : kSizeTests()) {
        values.emplace_back(std::string(s.name));
    }
    return values;
}

static const SizeTest& findSizeTest(const std::string& name) {
    for (const SizeTest& s : kSizeTests()) {
        if (name == s.name) {
            return s;
        }
    }
    static const SizeTest dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "size")
    .desc("Test validation of size")
    .params([](ParamsBuilder u) {
        return u.combine("attr", sizeTestNames());
    })
    .fn([](ShaderValidationTest& t) {
        const SizeTest& test = findSizeTest(t.param<std::string>("attr"));
        const std::string code =
            "\noverride over: i32 = 4;"
            "\nconst z: i32 = 4;"
            "\n"
            "\nstruct S {"
            "\n  " + std::string(test.src) + " a: f32,"
            "\n};"
            "\n@group(0) @binding(0)"
            "\nvar<storage> a: S;"
            "\n"
            "\n@workgroup_size(1)"
            "\n@compute fn main() {"
            "\n  _ = a;"
            "\n}";
        t.expectCompileResult(test.pass, code);
    });

CTS_TEST(g, "size_fp16")
    .desc("Test validation of size with fp16")
    .params([](ParamsBuilder u) {
        return u.combine("ext", {std::string(""), std::string("h")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ext = t.param<std::string>("ext");
        const std::string code =
            "\nstruct S {"
            "\n  @size(4" + ext + ") a: f32,"
            "\n}"
            "\n@group(0) @binding(0)"
            "\nvar<storage> a: S;"
            "\n"
            "\n@workgroup_size(1)"
            "\n@compute fn main() {"
            "\n  _ = a;"
            "\n}";
        t.expectCompileResult(ext == "", code);
    });

// ---- kNonStructTests ------------------------------------------------------
struct NonStructTest {
    const char* name;
    const char* mod_src;
    const char* func_src;
    int size;
    bool pass;
};

static const std::vector<NonStructTest>& kNonStructTests() {
    static const std::vector<NonStructTest> tests = {
        {"control", "", "", 0, true},
        {"struct", "struct S { a: f32 }", "", 4, false},
        {"constant", "const a: f32 = 4.0;", "", 4, false},
        {"vec", "", "vec4<f32>", 16, false},
        {"mat", "", "mat4x4<f32>", 64, false},
        {"array", "", "array<f32, 4>", 16, false},
        {"scalar", "", "f32", 4, false},
    };
    return tests;
}

static std::vector<Value> nonStructTestNames() {
    std::vector<Value> values;
    for (const NonStructTest& s : kNonStructTests()) {
        values.emplace_back(std::string(s.name));
    }
    return values;
}

static const NonStructTest& findNonStructTest(const std::string& name) {
    for (const NonStructTest& s : kNonStructTests()) {
        if (name == s.name) {
            return s;
        }
    }
    static const NonStructTest dummy{"", "", "", 0, false};
    return dummy;
}

CTS_TEST(g, "size_non_struct")
    .desc("Test validation of size outside of a struct")
    .params([](ParamsBuilder u) {
        return u.combine("attr", nonStructTestNames());
    })
    .fn([](ShaderValidationTest& t) {
        const NonStructTest& data = findNonStructTest(t.param<std::string>("attr"));
        const std::string sizeStr = std::to_string(data.size);
        std::string code;
        if (std::string(data.mod_src) != "") {
            code += "@size(" + sizeStr + ") " + std::string(data.mod_src);
        }
        code +=
            "\n@workgroup_size(1)"
            "\n@compute fn main() {\n";
        if (std::string(data.func_src) != "") {
            code += "@size(" + sizeStr + ") var a: " + std::string(data.func_src) + ";";
        }
        code += "}";
        t.expectCompileResult(data.pass, code);
    });

CTS_TEST(g, "size_creation_fixed_footprint")
    .desc("Test that @size is only valid on types that have creation-fixed footprint.")
    .params([](ParamsBuilder u) {
        return u.combine("array_size", {std::string(", 4"), std::string("")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string arraySize = t.param<std::string>("array_size");
        const std::string code =
            "\nstruct S {"
            "\n  @size(64) a: array<f32" + arraySize + ">,"
            "\n};"
            "\n@group(0) @binding(0)"
            "\nvar<storage> a: S;"
            "\n"
            "\n@workgroup_size(1)"
            "\n@compute fn main() {"
            "\n  _ = a.a[0];"
            "\n}";
        t.expectCompileResult(arraySize != "", code);
    });

} // namespace
