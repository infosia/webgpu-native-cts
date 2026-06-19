// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/align.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - required_alignment mirrors upstream's `t.hasLanguageFeature('uniform_buffer_standard_layout')`
//     gate; the enabler returns the true per-backend answer (Dawn via the WGSL feature query,
//     non-Dawn via a canonical trial-compile probe).
//   - placement: the `attribute` param is a single map of scope->allowed; `scope=undefined`
//     is modeled as the literal string "undefined" and always passes.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,align",
    "Validation tests for @align");

// ---------------------------------------------------------------------------
// parsing
// ---------------------------------------------------------------------------
struct ParseCase {
    const char* name;
    const char* src;
    bool pass;
};

static const std::vector<ParseCase>& kParseTests() {
    static const std::vector<ParseCase> cases = {
        {"blank", "", true},
        {"one", "@align(1)", false},
        {"four_a", "@align(4)", true},
        {"four_i", "@align(4i)", true},
        {"four_u", "@align(4u)", true},
        {"four_hex", "@align(0x4)", true},
        {"trailing_comma", "@align(4,)", true},
        {"const_u", "@align(u_val)", true},
        {"const_i", "@align(i_val)", true},
        {"const_expr", "@align(i_val + 4 - 6)", false},
        {"const_expr_2", "@align(i_val + 8 - 4)", true},
        {"large", "@align(1073741824)", true},
        {"tabs", "@\talign\t(4)", true},
        {"comment", "@/*comment*/align/*comment*/(4)", true},
        {"misspelling", "@malign(4)", false},
        {"empty", "@align()", false},
        {"missing_left_paren", "@align 4)", false},
        {"missing_right_paren", "@align(4", false},
        {"multiple_values", "@align(4, 2)", false},
        {"non_power_two", "@align(3)", false},
        {"const_f", "@align(f_val)", false},
        {"one_f", "@align(1.0)", false},
        {"four_f", "@align(4f)", false},
        {"four_h", "@align(4h)", false},
        {"no_params", "@align", false},
        {"zero_a", "@align(0)", false},
        {"negative", "@align(-4)", false},
        {"large_no_power_two", "@align(2147483646)", false},
        {"larger_than_max_i32", "@align(2147483648)", false},
        {"duplicate", "@align(4) @align(4)", false},
    };
    return cases;
}

static std::vector<Value> parseCaseNames() {
    std::vector<Value> values;
    for (const ParseCase& c : kParseTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ParseCase& findParseCase(const std::string& name) {
    for (const ParseCase& c : kParseTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ParseCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "parsing")
    .desc("Test that @align is parsed correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("align", parseCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const ParseCase& curr = findParseCase(t.param<std::string>("align"));
        const std::string code =
            "\nconst i_val: i32 = 4;"
            "\nconst u_val: u32 = 4;"
            "\nconst f_val: f32 = 4.2;"
            "\nstruct B {"
            "\n  " + std::string(curr.src) + " a: i32,"
            "\n}"
            "\n"
            "\n@group(0) @binding(0)"
            "\nvar<uniform> uniform_buffer: B;"
            "\n"
            "\n@fragment"
            "\nfn main() -> @location(0) vec4<f32> {"
            "\n  return vec4<f32>(.4, .2, .3, .1);"
            "\n}";
        t.expectCompileResult(curr.pass, code);
    });

// ---------------------------------------------------------------------------
// required_alignment
// ---------------------------------------------------------------------------
struct AlignType {
    const char* name;
    int storage;
    int uniform;
};

static const std::vector<AlignType>& kAlignTypes() {
    static const std::vector<AlignType> v = {
        {"i32", 4, 4},
        {"u32", 4, 4},
        {"f32", 4, 4},
        {"f16", 2, 2},
        {"atomic<i32>", 4, 4},
        {"vec2<i32>", 8, 8},
        {"vec2<f16>", 4, 4},
        {"vec3<u32>", 16, 16},
        {"vec3<f16>", 8, 8},
        {"vec4<f32>", 16, 16},
        {"vec4<f16>", 8, 8},
        {"mat2x2<f32>", 8, 8},
        {"mat3x2<f32>", 8, 8},
        {"mat4x2<f32>", 8, 8},
        {"mat2x2<f16>", 4, 4},
        {"mat3x2<f16>", 4, 4},
        {"mat4x2<f16>", 4, 4},
        {"mat2x3<f32>", 16, 16},
        {"mat3x3<f32>", 16, 16},
        {"mat4x3<f32>", 16, 16},
        {"mat2x3<f16>", 8, 8},
        {"mat3x3<f16>", 8, 8},
        {"mat4x3<f16>", 8, 8},
        {"mat2x4<f32>", 16, 16},
        {"mat3x4<f32>", 16, 16},
        {"mat4x4<f32>", 16, 16},
        {"mat2x4<f16>", 8, 8},
        {"mat3x4<f16>", 8, 8},
        {"mat4x4<f16>", 8, 8},
        {"array<vec2<i32>, 2>", 8, 16},
        {"array<vec4<i32>, 2>", 16, 16},
        {"S", 8, 16},
    };
    return v;
}

static std::vector<Value> alignTypeNames() {
    std::vector<Value> values;
    for (const AlignType& a : kAlignTypes()) {
        values.emplace_back(std::string(a.name));
    }
    return values;
}

static const AlignType& findAlignType(const std::string& name) {
    for (const AlignType& a : kAlignTypes()) {
        if (name == a.name) {
            return a;
        }
    }
    static const AlignType dummy{"", 0, 0};
    return dummy;
}

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

static bool contains(const std::string& s, const std::string& needle) {
    return s.find(needle) != std::string::npos;
}

CTS_TEST(g, "required_alignment")
    .desc("Test that the align with an invalid size is an error")
    .params([](ParamsBuilder u) {
        return u.combine("address_space",
                         {Value(std::string("storage")), Value(std::string("uniform"))})
            .combine("align", {Value(1), Value(2), Value(std::string("alignment")), Value(32)})
            .combine("type", alignTypeNames())
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        // If the `uniform_buffer_standard_layout` feature is supported, the `uniform` address space
        // has the same layout constraints as `storage`.
        const bool hasUboStdLayout = t.hasLanguageFeature("uniform_buffer_standard_layout");

        const std::string addressSpaceParam = t.param<std::string>("address_space");
        const std::string typeName = t.param<std::string>("type");
        const AlignType& type = findAlignType(typeName);

        // align param is either an integer (1, 2, 32) or the string "alignment".
        const bool alignIsString = t.paramIsString("align");
        const int alignInt = alignIsString ? 0 : t.param<int>("align");

        // While this would fail validation, it doesn't fail for any reasons related to alignment.
        // Atomics are not allowed in uniform address space as they have to be read_write.
        if (addressSpaceParam == "uniform" && startsWith(typeName, "atomic")) {
            t.skip("No atomics in uniform address space");
        }

        std::string code;
        if (contains(typeName, "f16")) {
            code += "enable f16;\n";
        }

        // Testing the struct case, generate the struct.
        if (typeName == "S") {
            const int innerVec = (addressSpaceParam == "storage" || hasUboStdLayout) ? 2 : 4;
            code += "struct S {"
                    "\n        a: mat4x2<f32>,          // Align 8"
                    "\n        b: array<vec" + std::to_string(innerVec) +
                    "<i32>, 2>,  // Storage align 8, uniform 16"
                    "\n      }"
                    "\n      ";
        }

        // Alignment value listed in the spec.
        const int minAlign = (addressSpaceParam == "storage" || hasUboStdLayout)
                                 ? type.storage
                                 : type.uniform;
        const std::string alignStr = alignIsString ? std::to_string(minAlign)
                                                    : std::to_string(alignInt);

        std::string addressSpace = "uniform";
        if (addressSpaceParam == "storage") {
            // atomics require read_write, not just the default of read.
            addressSpace = "storage, read_write";
        }

        code += "struct MyStruct {"
                "\n      @align(" + alignStr + ") a: " + typeName + ","
                "\n    }"
                "\n"
                "\n    @group(0) @binding(0)"
                "\n    var<" + addressSpace + "> a : MyStruct;";

        code += "\n    @fragment"
                "\n    fn main() -> @location(0) vec4<f32> {"
                "\n      return vec4<f32>(.4, .2, .3, .1);"
                "\n    }";

        // align < min_align. For the string "alignment" case, align == min_align so this is false.
        const int alignValue = alignIsString ? minAlign : alignInt;
        bool fails = alignValue < minAlign;
        if (!hasUboStdLayout) {
            // An array of `vec2` in uniform will not validate.
            fails = fails || (addressSpaceParam == "uniform" && startsWith(typeName, "array<vec2"));
        }

        t.expectCompileResult(!fails, code);
    });

// ---------------------------------------------------------------------------
// placement
// ---------------------------------------------------------------------------
struct PlacementScope {
    const char* name;
    bool allowed;
};

// Mirrors upstream's `scope` list (with `undefined` -> "undefined") and the single
// `attribute` map of scope->allowed.
static const std::vector<PlacementScope>& kPlacementScopes() {
    static const std::vector<PlacementScope> v = {
        {"private-var", false},
        {"storage-var", false},
        {"struct-member", true},
        {"fn-decl", false},
        {"fn-param", false},
        {"fn-var", false},
        {"fn-return", false},
        {"while-stmt", false},
        {"undefined", true},
    };
    return v;
}

static std::vector<Value> placementScopeNames() {
    std::vector<Value> values;
    for (const PlacementScope& s : kPlacementScopes()) {
        values.emplace_back(std::string(s.name));
    }
    return values;
}

static bool placementAllowed(const std::string& name) {
    for (const PlacementScope& s : kPlacementScopes()) {
        if (name == s.name) {
            return s.allowed;
        }
    }
    return false;
}

CTS_TEST(g, "placement")
    .desc("Tests the locations @align is allowed to appear")
    .params([](ParamsBuilder u) {
        return u.combine("scope", placementScopeNames()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string scope = t.param<std::string>("scope");
        const std::string attr = "@align(32)";
        auto at = [&](const char* s) { return scope == s ? attr : std::string(""); };
        const std::string code =
            "\n      " + at("private-var") +
            "\n      var<private> priv_var : i32;"
            "\n"
            "\n      " + at("storage-var") +
            "\n      @group(0) @binding(0)"
            "\n      var<storage> stor_var : i32;"
            "\n"
            "\n      struct A {"
            "\n        " + at("struct-member") +
            "\n        a : i32,"
            "\n      }"
            "\n"
            "\n      @vertex"
            "\n      " + at("fn-decl") +
            "\n      fn f("
            "\n        " + at("fn-param") +
            "\n        @location(0) b : i32,"
            "\n      ) -> " + at("fn-return") + " @builtin(position) vec4f {"
            "\n        " + at("fn-var") +
            "\n        var<function> func_v : i32;"
            "\n"
            "\n        " + at("while-stmt") +
            "\n        while false {}"
            "\n"
            "\n        return vec4(1, 1, 1, 1);"
            "\n      }"
            "\n    ";
        // scope === undefined || attribute[scope]
        t.expectCompileResult(scope == "undefined" || placementAllowed(scope), code);
    });

// ---------------------------------------------------------------------------
// multi_align
// ---------------------------------------------------------------------------
CTS_TEST(g, "multi_align")
    .desc("Tests that align multiple times is an error")
    .params([](ParamsBuilder u) {
        return u.combine("multi", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const bool multi = t.param<bool>("multi");
        std::string code =
            "struct A {"
            "\n      @align(128) ";
        if (multi) {
            code += "@align(128) ";
        }
        code += "a : i32,"
                "\n      }"
                "\n"
                "\n      @fragment"
                "\n      fn main() -> @location(0) vec4<f32> {"
                "\n        return vec4(1., 1., 1., 1.);"
                "\n      }";
        t.expectCompileResult(!multi, code);
    });

} // namespace
