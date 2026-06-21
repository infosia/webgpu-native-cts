// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/access/structure.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,access,structure",
    "Validation tests for structure access expressions.");

CTS_TEST(g, "identifier_mismatch")
    .desc("Tests that the member identifier must match a member in the declaration")
    .params([](ParamsBuilder u) { return u.combine("decl", {"value", "ref"}); })
    .fn([](ShaderValidationTest& t) {
        const std::string declKw = t.param<std::string>("decl") == "value" ? "let" : "var";
        const std::string code =
            "\n    struct S {\n      x : u32\n    }\n    fn foo() {\n      " + declKw +
            " v : S = S();\n      _ = v.y;\n    }";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "shadowed_member")
    .desc("Tests that other declarations do not interfere with member determination")
    .params([](ParamsBuilder u) { return u.combine("decl", {"value", "ref"}); })
    .fn([](ShaderValidationTest& t) {
        const std::string declKw = t.param<std::string>("decl") == "value" ? "let" : "var";
        const std::string code =
            "\n    struct S {\n      x : u32\n    }\n    fn foo() {\n      var x : i32;\n      " +
            declKw + " v : S = S();\n      let tmp : u32 = v.x;\n    }";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "result_type")
    .desc("Tests correct result types are returned")
    .params([](ParamsBuilder u) { return u.combine("decl", {"value", "ref"}); })
    .fn([](ShaderValidationTest& t) {
        static const char* const types[] = {
            "i32",          "u32",   "f32",   "bool",   "array<u32, 4>", "array<T, 2>",
            "vec2f",        "vec3u", "vec4i", "mat2x2f", "T",
        };
        const size_t typeCount = sizeof(types) / sizeof(types[0]);

        std::string code = "\n    struct T {\n      a : f32\n    }\n    struct S {\n";
        for (size_t i = 0; i < typeCount; ++i) {
            code += "m" + std::to_string(i) + " : " + types[i] + ",\n";
        }

        const std::string declKw = t.param<std::string>("decl") == "value" ? "let" : "var";
        code += "}\n    fn foo() {\n      var x : i32;\n      " + declKw + " v : S = S();\n";

        for (size_t i = 0; i < typeCount; ++i) {
            const std::string is = std::to_string(i);
            code += "let tmp" + is + " : " + types[i] + " = v.m" + is + ";\n";
        }

        code += "}";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "result_type_f16")
    .desc("Tests correct type is returned for f16")
    .params([](ParamsBuilder u) { return u.combine("decl", {"value", "ref"}); })
    .fn([](ShaderValidationTest& t) {
        const std::string declKw = t.param<std::string>("decl") == "value" ? "let" : "var";
        const std::string code =
            "\n    enable f16;\n    struct S {\n      x : f16\n    }\n    fn foo() {\n      " +
            declKw + " v : S = S();\n      let tmp : f16 = v.x;\n    }";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "result_type_runtime_array")
    .desc("Tests correct type is returned for runtime arrays")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n    struct S {\n      x : array<u32>\n    }\n"
            "    @group(0) @binding(0) var<storage> v : S;\n"
            "    fn foo() {\n"
            "      let tmp : u32 = v.x[0];\n"
            "      let tmp_ptr : ptr<storage, array<u32>> = &v.x;\n"
            "    }";
        t.expectCompileResult(true, code);
    });

}  // namespace
