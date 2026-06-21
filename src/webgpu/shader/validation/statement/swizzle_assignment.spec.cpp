// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/statement/swizzle_assignment.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,statement,swizzle_assignment",
    "Validation tests for swizzle assignments.\n");

CTS_TEST(g, "valid")
    .desc("Valid swizzle assignments")
    .params([](ParamsBuilder u) {
        return u.combine("elemType", {"f32", "i32", "u32"}).combine("vecSize", {2, 3, 4});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string elemType = t.param<std::string>("elemType");
        const int vecSize = t.param<int>("vecSize");
        const std::string swizzle = std::string("xyzw").substr(0, static_cast<size_t>(vecSize));
        const std::string vecType =
            "vec" + std::to_string(swizzle.length()) + "<" + elemType + ">";
        const std::string code = "\n@fragment\nfn main() {\n  var v = vec4<" + elemType +
                                 ">(0);\n  v." + swizzle + " = " + vecType + "(1);\n}\n";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "invalid_lhs_not_reference")
    .desc("Invalid swizzle assignment where LHS is not a reference")
    .params([](ParamsBuilder u) {
        return u.combine("lhs", {"vec4f()", "const_vec", "foo()"});
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string lhs = t.param<std::string>("lhs");
        const std::string code =
            "\nconst const_vec = vec4f();\n\nfn foo() -> vec4f {\n  return vec4f();\n}\n\n@fragment"
            "\nfn main() {\n  var v = vec4f();\n  " +
            lhs + ".xyz = vec3(0.0);\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_duplicate_components")
    .desc("Invalid swizzle assignment with duplicate LHS components")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  v.xx = vec2f();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_component_mismatch")
    .desc("Invalid swizzle assignment with mismatched number of components")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  v.xy = vec3f();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_component_oob")
    .desc("Invalid swizzle assignment with components out of bounds")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec3f();\n  v.xyzw = vec4f();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_type_mismatch")
    .desc("Invalid swizzle assignment with mismatched types")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  v.xy = vec2i();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_mixed_letter_schemes")
    .desc("Invalid swizzle assignment with mixed letter schemes (xyzw vs. rgba)")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  v.xr = vec2i();\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_address_of_swizzle_view")
    .desc("Invalid to take the address of a swizzle view")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec4f();\n  let p = &v.xy;\n}\n";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "invalid_index_into_swizzle_view")
    .desc("Invalid to index into a swizzle view on the lhs")
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("swizzle_assignment");
        const std::string code =
            "\n@fragment\nfn main() {\n  var v = vec2u();\n  v.xy[0] = 1;\n";
        t.expectCompileResult(false, code);
    });

}  // namespace
