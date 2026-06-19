// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/ref.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,types,ref",
    "Validation tests for ref types");

// Mirrors upstream kTypes.
static std::vector<Value> kTypes() {
    return {std::string("bool"),       std::string("i32"),    std::string("f32"),
            std::string("vec2i"),      std::string("mat2x2f"), std::string("array<i32, 4>"),
            std::string("S")};
}

CTS_TEST(g, "not_typeable_var")
    .desc("Test that `ref` cannot be typed in a shader as an explicit var decl type.")
    .params([](ParamsBuilder u) {
        return u.combine("type", kTypes()).combine("ref", {false, true});
    })
    .fn([](ShaderValidationTest& t) {
        std::string ty = t.param<std::string>("type");
        const bool ref = t.param<bool>("ref");
        if (ref) {
            ty = "ref<private, " + ty + ">";
        }
        const std::string code = "\n    struct S { a : u32 }\n    var<private> foo : " + ty + ";";
        t.expectCompileResult(!ref, code);
    });

CTS_TEST(g, "not_typeable_let")
    .desc("Test that `ref` cannot be typed in a shader as a let decl type.")
    .params([](ParamsBuilder u) {
        return u.combine("type", kTypes()).combine("view", {"ptr", "ref"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string view = t.param<std::string>("view");
        const std::string code =
            "\n      struct S { a : u32 }\n      fn foo() {\n        var a : " + type +
            ";\n        let b : " + view + "<function, " + type + "> = &a;\n      }";
        t.expectCompileResult(view == "ptr", code);
    });

CTS_TEST(g, "not_typeable_param")
    .desc("Test that `ref` cannot be typed in a shader as a function parameter type.")
    .params([](ParamsBuilder u) {
        return u.combine("type", kTypes()).combine("view", {"ptr", "ref"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string view = t.param<std::string>("view");
        const std::string code =
            "\n    struct S { a : u32 }\n    fn foo(a : " + view + "<private, " + type + ">) {}";
        t.expectCompileResult(view == "ptr", code);
    });

CTS_TEST(g, "not_typeable_alias")
    .desc("Test that `ref` cannot be typed in a shader as an alias type.")
    .params([](ParamsBuilder u) {
        return u.combine("type", kTypes()).combine("view", {"ptr", "ref"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string type = t.param<std::string>("type");
        const std::string view = t.param<std::string>("view");
        const std::string code =
            "\n    struct S { a : u32 }\n    alias a = " + view + "<private, " + type + ">;";
        t.expectCompileResult(view == "ptr", code);
    });

}  // namespace
