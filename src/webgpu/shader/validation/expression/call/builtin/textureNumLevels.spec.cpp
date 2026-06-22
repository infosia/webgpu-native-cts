// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureNumLevels.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

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
    "shader,validation,expression,call,builtin,textureNumLevels",
    "Validation tests for the textureNumLevels() builtin.");

// kTextureNumLevelsTextureTypesForNonStorageTextures.
static std::vector<Value> kTextureNumLevelsTextureTypesForNonStorageTextures() {
    return {Value(std::string("texture_1d")),
            Value(std::string("texture_2d")),
            Value(std::string("texture_2d_array")),
            Value(std::string("texture_3d")),
            Value(std::string("texture_cube")),
            Value(std::string("texture_cube_array")),
            Value(std::string("texture_depth_2d")),
            Value(std::string("texture_depth_2d_array")),
            Value(std::string("texture_depth_cube")),
            Value(std::string("texture_depth_cube_array"))};
}

CTS_TEST(g, "return_type")
    .desc("Validates the return type of textureNumLevels is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("textureType", kTextureNumLevelsTextureTypesForNonStorageTextures())
            .beginSubcases()
            .expand("texelType", [](const ParamRecord& p) {
                const b::TextureTypeInfo* info =
                    b::nonStorageInfo(valueAs<std::string>(*findParam(p, "textureType")));
                std::vector<Value> out;
                for (const bt::Type& ty : info->texelTypes) {
                    out.emplace_back(ty.toString());
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string returnType = t.param<std::string>("returnType");
        const std::string textureType = t.param<std::string>("textureType");
        const std::string texelType = t.param<std::string>("texelType");
        const bt::Type returnVarType = bt::typeByName(returnType);

        const std::string varWGSL = returnVarType.toString();
        const bt::Type texelArgType = b::stringToTexelType(texelType);
        const std::string textureWGSL = b::getNonStorageTextureTypeWGSL(textureType, texelArgType);

        const std::string code = "\n@group(0) @binding(0) var t: " + textureWGSL +
                                 ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " + varWGSL +
                                 " = textureNumLevels(t);\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(bt::scalar(bt::ScalarKind::U32), returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type")
    .desc("Validates that incompatible texture types don't work with textureNumLevels")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string code = "\n@group(0) @binding(1) var t: " + testTextureType +
                                 ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = "
                                 "textureNumLevels(t);\n  return vec4f(0);\n}\n";
        const bool expectSuccess = testTextureType.find("storage") == std::string::npos &&
                                   testTextureType.find("multisampled") == std::string::npos &&
                                   testTextureType.find("external") == std::string::npos;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string code =
            "\n    @group(0) @binding(0) var t : texture_2d<f32>;\n    fn foo() {\n      " +
            std::string(use ? "_ =" : "") + " textureNumLevels(t);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
