// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureNumLayers.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,textureNumLayers",
    "Validation tests for the textureNumLayers() builtin.");

// kTextureNumLayersTextureTypesForNonStorageTextures.
static std::vector<Value> kTextureNumLayersTextureTypesForNonStorageTextures() {
    return {Value(std::string("texture_2d_array")),
            Value(std::string("texture_cube_array")),
            Value(std::string("texture_depth_2d_array")),
            Value(std::string("texture_depth_cube_array"))};
}

// kTextureNumLayersTextureTypesForStorageTextures.
static std::vector<Value> kTextureNumLayersTextureTypesForStorageTextures() {
    return {Value(std::string("texture_storage_2d_array"))};
}

CTS_TEST(g, "return_type,non_storage")
    .desc("Validates the return type of textureNumLayers is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("textureType", kTextureNumLayersTextureTypesForNonStorageTextures())
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
                                 " = textureNumLayers(t);\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(bt::scalar(bt::ScalarKind::U32), returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "return_type,storage")
    .desc("Validates the return type of textureNumLayers is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("textureType", kTextureNumLayersTextureTypesForStorageTextures())
            .beginSubcases()
            .combine("format", b::kPossibleStorageTextureFormats());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string returnType = t.param<std::string>("returnType");
        const std::string textureType = t.param<std::string>("textureType");
        const std::string format = t.param<std::string>("format");
        b::skipIfTextureFormatNotSupported(t, format);
        b::skipIfTextureFormatNotUsableWithStorageAccessMode(t, "write-only", format);

        const bt::Type returnVarType = bt::typeByName(returnType);
        const std::string varWGSL = returnVarType.toString();

        const std::string code = "\n@group(0) @binding(0) var t: " + textureType + "<" + format +
                                 ", write>;\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " +
                                 varWGSL + " = textureNumLayers(t);\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(bt::scalar(bt::ScalarKind::U32), returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type")
    .desc("Validates that incompatible texture types don't work with textureNumLayers")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string code = "\n@group(0) @binding(1) var t: " + testTextureType +
                                 ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = "
                                 "textureNumLayers(t);\n  return vec4f(0);\n}\n";
        const bool expectSuccess = testTextureType.find("array") != std::string::npos;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string code =
            "\n    @group(0) @binding(0) var t : texture_2d_array<f32>;\n    fn foo() {\n      " +
            std::string(use ? "_ =" : "") + " textureNumLayers(t);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
