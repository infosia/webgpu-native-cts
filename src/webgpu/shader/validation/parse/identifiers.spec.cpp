// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/identifiers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting note: upstream combines on `new Set([...kValidIdentifiers,
// ...kInvalidIdentifiers])`. The two sets are disjoint, so the combined order is
// the valid list followed by the invalid list. Validity in each body is decided
// by membership in kValidIdentifiers. Non-ASCII identifiers are written as
// explicit UTF-8 byte escapes so the bytes match upstream regardless of the
// compiler's source/execution charset.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,parse,identifiers",
    "Validation tests for identifiers");

static const std::vector<std::string>& kValidIdentifiers() {
    static const std::vector<std::string> v = {
        "foo",
        "Foo",
        "FOO",
        "_0",
        "_foo0",
        "_0foo",
        "foo__0",
        "\xCE\x94\xCE\xAD\xCE\xBB\xCF\x84\xCE\xB1",  // Delta-epsilon-lambda-tau-alpha
        "r\xC3\xA9""flexion",                          // reflexion
        "\xD0\x9A\xD1\x8B\xD0\xB7\xD1\x8B\xD0\xBB",  // Kyzyl
        "\xF0\x90\xB0\x93\xF0\x90\xB0\x8F\xF0\x90\xB0\x87",  // Old Turkic
        "\xE6\x9C\x9D\xE7\x84\xBC\xE3\x81\x91",      // Japanese
        "\xD8\xB3\xD9\x84\xD8\xA7\xD9\x85",          // Arabic salam
        "\xEA\xB2\x80\xEC\xA0\x95",                  // Korean
        "\xD7\xA9\xD6\xB8\xD7\x81\xD7\x9C\xD7\x95\xD6\xB9\xD7\x9D",  // Hebrew shalom
        "\xE0\xA4\x97\xE0\xA5\x81\xE0\xA4\xB2\xE0\xA4\xBE\xE0\xA4\xAC\xE0\xA5\x80",  // Hindi
        "\xD6\x83\xD5\xAB\xD6\x80\xD5\xB8\xD6\x82\xD5\xA6",  // Armenian
        // Builtin type identifiers:
        "array",
        "atomic",
        "bool",
        "binding_array",
        "bf16",
        "bitcast",
        "f32",
        "f16",
        "f64",
        "i32",
        "i16",
        "i64",
        "i8",
        "mat2x2",
        "mat2x3",
        "mat2x4",
        "mat3x2",
        "mat3x3",
        "mat3x4",
        "mat4x2",
        "mat4x3",
        "mat4x4",
        "ptr",
        "quat",
        "sampler",
        "sampler_comparison",
        "signed",
        "texture_1d",
        "texture_2d",
        "texture_2d_array",
        "texture_3d",
        "texture_cube",
        "texture_cube_array",
        "texture_multisampled_2d",
        "texture_storage_1d",
        "texture_storage_2d",
        "texture_storage_2d_array",
        "texture_storage_3d",
        "texture_depth_2d",
        "texture_depth_2d_array",
        "texture_depth_cube",
        "texture_depth_cube_array",
        "texture_depth_multisampled_2d",
        "u32",
        "u16",
        "u64",
        "u8",
        "unsigned",
        "vec2",
        "vec3",
        "vec4",
    };
    return v;
}

static const std::vector<std::string>& kInvalidIdentifiers() {
    static const std::vector<std::string> v = {
        "_",
        "__",
        "__foo",
        "0foo",
        // No punctuation:
        "foo.bar",
        "foo-bar",
        "foo+bar",
        "foo#bar",
        "foo!bar",
        "foo\\bar",
        "foo/bar",
        "foo,bar",
        "foo@bar",
        "foo::bar",
        // Keywords:
        "alias",
        "break",
        "case",
        "const",
        "const_assert",
        "continue",
        "continuing",
        "default",
        "diagnostic",
        "discard",
        "else",
        "enable",
        "false",
        "fn",
        "for",
        "if",
        "let",
        "loop",
        "override",
        "requires",
        "return",
        "struct",
        "switch",
        "true",
        "var",
        "while",
        // Reserved Words
        "NULL",
        "Self",
        "abstract",
        "active",
        "alignas",
        "alignof",
        "as",
        "asm",
        "asm_fragment",
        "async",
        "attribute",
        "auto",
        "await",
        "become",
        "cast",
        "catch",
        "class",
        "co_await",
        "co_return",
        "co_yield",
        "coherent",
        "column_major",
        "common",
        "compile",
        "compile_fragment",
        "concept",
        "const_cast",
        "consteval",
        "constexpr",
        "constinit",
        "crate",
        "debugger",
        "decltype",
        "delete",
        "demote",
        "demote_to_helper",
        "do",
        "dynamic_cast",
        "enum",
        "explicit",
        "export",
        "extends",
        "extern",
        "external",
        "fallthrough",
        "filter",
        "final",
        "finally",
        "friend",
        "from",
        "fxgroup",
        "get",
        "goto",
        "groupshared",
        "highp",
        "impl",
        "implements",
        "import",
        "inline",
        "instanceof",
        "interface",
        "layout",
        "lowp",
        "macro",
        "macro_rules",
        "match",
        "mediump",
        "meta",
        "mod",
        "module",
        "move",
        "mut",
        "mutable",
        "namespace",
        "new",
        "nil",
        "noexcept",
        "noinline",
        "nointerpolation",
        "non_coherent",
        "noncoherent",
        "noperspective",
        "null",
        "nullptr",
        "of",
        "operator",
        "package",
        "packoffset",
        "partition",
        "pass",
        "patch",
        "pixelfragment",
        "precise",
        "precision",
        "premerge",
        "priv",
        "protected",
        "pub",
        "public",
        "readonly",
        "ref",
        "regardless",
        "register",
        "reinterpret_cast",
        "require",
        "resource",
        "restrict",
        "self",
        "set",
        "shared",
        "sizeof",
        "smooth",
        "snorm",
        "static",
        "static_assert",
        "static_cast",
        "std",
        "subroutine",
        "super",
        "target",
        "template",
        "this",
        "thread_local",
        "throw",
        "trait",
        "try",
        "type",
        "typedef",
        "typeid",
        "typename",
        "typeof",
        "union",
        "unless",
        "unorm",
        "unsafe",
        "unsized",
        "use",
        "using",
        "varying",
        "virtual",
        "volatile",
        "wgsl",
        "where",
        "with",
        "writeonly",
        "yield",
    };
    return v;
}

// The combined value list (valid first, then invalid).
static std::vector<Value> identifierValues() {
    std::vector<Value> values;
    for (const std::string& s : kValidIdentifiers()) {
        values.emplace_back(s);
    }
    for (const std::string& s : kInvalidIdentifiers()) {
        values.emplace_back(s);
    }
    return values;
}

static bool isValidIdentifier(const std::string& ident) {
    for (const std::string& s : kValidIdentifiers()) {
        if (s == ident) {
            return true;
        }
    }
    return false;
}

CTS_TEST(g, "module_var_name")
    .desc("Test that valid identifiers are accepted for names of module-scope 'var's, and invalid "
          "identifiers are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string type = ident == "i32" ? "u32" : "i32";
        const std::string code = "var<private> " + ident + " : " + type + ";";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "module_const_name")
    .desc("Test that valid identifiers are accepted for names of module-scope 'const's, and invalid "
          "identifiers are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string type = ident == "i32" ? "u32" : "i32";
        const std::string code = "const " + ident + " : " + type + " = 0;";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "override_name")
    .desc("Test that valid identifiers are accepted for names of 'override's, and invalid "
          "identifiers are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string type = ident == "i32" ? "u32" : "i32";
        const std::string code = "override " + ident + " : " + type + " = 0;";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "function_name")
    .desc("Test that valid identifiers are accepted for names of functions, and invalid identifiers "
          "are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string code = "fn " + ident + "() {}";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "struct_name")
    .desc("Test that valid identifiers are accepted for names of structs, and invalid identifiers "
          "are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string type = ident == "i32" ? "u32" : "i32";
        const std::string code = "struct " + ident + " { i : " + type + " }";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "alias_name")
    .desc("Test that valid identifiers are accepted for names of aliases, and invalid identifiers "
          "are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string type = ident == "i32" ? "u32" : "i32";
        const std::string code = "alias " + ident + " = " + type + ";";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "function_param_name")
    .desc("Test that valid identifiers are accepted for names of function parameters, and invalid "
          "identifiers are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string type = ident == "i32" ? "u32" : "i32";
        const std::string code = "fn F(" + ident + " : " + type + ") {}";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "function_const_name")
    .desc("Test that valid identifiers are accepted for names of function-scoped 'const's, and "
          "invalid identifiers are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string code = "fn F() {\n  const " + ident + " = 1;\n}";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "function_let_name")
    .desc("Test that valid identifiers are accepted for names of function-scoped 'let's, and "
          "invalid identifiers are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string code = "fn F() {\n  let " + ident + " = 1;\n}";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "function_var_name")
    .desc("Test that valid identifiers are accepted for names of function-scoped 'var's, and "
          "invalid identifiers are rejected.")
    .params([](ParamsBuilder u) {
        return u.combine("ident", identifierValues()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ident = t.param<std::string>("ident");
        const std::string code = "fn F() {\n  var " + ident + " = 1;\n}";
        t.expectCompileResult(isValidIdentifier(ident), code);
    });

CTS_TEST(g, "non_normalized")
    .desc("Test that identifiers are not unicode normalized")
    .fn([](ShaderValidationTest& t) {
        // U+212B ANGSTROM SIGN; U+00C5 LATIN CAPITAL LETTER A WITH RING ABOVE.
        // The first normalizes with NFC to the second; they must stay distinct.
        const std::string angstrom = "\xE2\x84\xAB";  // U+212B
        const std::string aRing = "\xC3\x85";          // U+00C5
        const std::string code =
            "var<private> " + angstrom + " : i32;  // " + angstrom +
            " normalizes with NFC to " + aRing +
            "\nvar<private> " + aRing + " : i32;";
        t.expectCompileResult(true, code);
    });

}  // namespace
