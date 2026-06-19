// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/enumerant.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,types,enumerant",
    "Validation tests for enumerant types.\n\n* Values cannot be declared with the type\n* "
    "Enumerant values cannot be used as values");

static std::vector<Value> kEnumerantTypes() {
    return {std::string("access_mode"), std::string("address_space"), std::string("texel_format")};
}

static std::vector<Value> kValueDecls() {
    return {std::string("var"), std::string("let"), std::string("const"), std::string("override")};
}

CTS_TEST(g, "type_declaration")
    .desc("Tests that enumerants cannot be used as a type")
    .params([](ParamsBuilder u) {
        return u.combine("enum", kEnumerantTypes());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string code = "alias T = " + t.param<std::string>("enum") + ";";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "value_type")
    .desc("Tests that enumerant types cannot be the type of declaration")
    .params([](ParamsBuilder u) {
        return u.combine("enum", kEnumerantTypes()).beginSubcases().combine("decl", kValueDecls());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string decl =
            t.param<std::string>("decl") + " x : " + t.param<std::string>("enum") + ";";
        std::string code;
        if (t.param<std::string>("decl") == "override") {
            code = decl;
        } else {
            code = "fn foo() {\n        " + decl + "\n      }";
        }
        t.expectCompileResult(false, code);
    });

static std::vector<Value> kEnumerantValues() {
    return {
        // Access modes
        std::string("read"), std::string("write"), std::string("read_write"),
        // Address spaces
        std::string("function"), std::string("private"), std::string("workgroup"),
        std::string("storage"), std::string("uniform"), std::string("handle"),
        // Texel formats
        std::string("rgba8unorm"), std::string("rgba8snorm"), std::string("rgba8uint"),
        std::string("rgba8sint"), std::string("rgba16uint"), std::string("rgba16sint"),
        std::string("rgba16float"), std::string("r32uint"), std::string("r32sint"),
        std::string("r32float"), std::string("rg32uint"), std::string("rg32sint"),
        std::string("rg32float"), std::string("rgba32uint"), std::string("rgba32sint"),
        std::string("rgba32float"), std::string("bgra8unorm"),
    };
}

CTS_TEST(g, "decl_value")
    .desc("Tests that enumerant values cannot be used as declaration value")
    .params([](ParamsBuilder u) {
        return u.combine("value", kEnumerantValues())
            .beginSubcases()
            .combine("decl", kValueDecls());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string decl =
            t.param<std::string>("decl") + " x = " + t.param<std::string>("value") + ";";
        std::string code;
        if (t.param<std::string>("decl") == "override") {
            code = decl;
        } else {
            code = "fn foo() {\n        " + decl + "\n      }";
        }
        t.expectCompileResult(false, code);
    });

}  // namespace
