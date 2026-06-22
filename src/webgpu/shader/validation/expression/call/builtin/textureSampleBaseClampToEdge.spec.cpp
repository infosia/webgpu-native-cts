// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureSampleBaseClampToEdge.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for the textureSampleBaseClampToEdge() builtin: coords
// parameter type, return type, and incompatible texture types. Mirrors the
// upstream .params order and case names exactly. The valid texture types are
// texture_2d<f32> and texture_external.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/expression/call/builtin/shader_builtin_utils.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace b = cts::shader_validation::builtin;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,textureSampleBaseClampToEdge",
    "Validation tests for the textureSampleBaseClampToEdge() builtin.");

// kTextureSampleBaseClampToEdgeTextureTypes
const std::vector<std::string>& kTextureSampleBaseClampToEdgeTextureTypes() {
    static const std::vector<std::string> v = {"texture_2d<f32>", "texture_external"};
    return v;
}
std::vector<Value> textureTypeValues() {
    std::vector<Value> out;
    for (const std::string& s : kTextureSampleBaseClampToEdgeTextureTypes()) {
        out.emplace_back(s);
    }
    return out;
}

CTS_TEST(g, "return_type")
    .desc("Validates the return type of textureSampleBaseClampToEdge is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("textureType", textureTypeValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string returnType = t.param<std::string>("returnType");
        const std::string textureType = t.param<std::string>("textureType");
        const bt::Type returnVarType = bt::typeByName(returnType);

        const std::string varWGSL = returnVarType.toString();

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " + varWGSL +
            " = textureSampleBaseClampToEdge(t, s, vec2f(0));\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(bt::vec(4, bt::ScalarKind::F32), returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "coords_argument")
    .desc("Validates that only incorrect coords arguments are rejected by "
          "textureSampleBaseClampToEdge")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", textureTypeValues())
            .combine("coordType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("value", {Value(-1), Value(0), Value(1)})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(
                           bt::typeByName(valueAs<std::string>(*findParam(p, "coordType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string coordType = t.param<std::string>("coordType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type coordArgType = bt::typeByName(coordType);
        const std::string coordWGSL = bt::createWgsl(coordArgType, static_cast<long long>(value));

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = "
            "textureSampleBaseClampToEdge(t, s, " +
            coordWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(coordArgType, bt::vec(2, bt::ScalarKind::F32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type")
    .desc("Validates that incompatible texture types don't work with textureSampleBaseClampToEdge")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " +
            testTextureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = "
            "textureSampleBaseClampToEdge(t, s, vec2f(0));\n  return vec4f(0);\n}\n";
        bool expectSuccess = false;
        for (const std::string& s : kTextureSampleBaseClampToEdgeTextureTypes()) {
            if (s == testTextureType) {
                expectSuccess = true;
                break;
            }
        }
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string code =
            "\n    @group(0) @binding(0) var t : texture_2d<f32>;\n    @group(0) @binding(1) var s : "
            "sampler;\n    fn foo() {\n      " +
            std::string(use ? "_ =" : "") + " textureSampleBaseClampToEdge(t,s, vec2(0,0));\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
