// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureDimensions.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,textureDimensions",
    "Validation tests for the textureDimension() builtin.");

// kValidTextureDimensionParameterTypesForNonStorageTextures: returnType + hasLevelArg.
struct DimArguments {
    std::string textureType;
    bt::Type returnType;
    bool hasLevelArg;
};
static const std::vector<DimArguments>& kNonStorageDimTable() {
    static const std::vector<DimArguments> v = {
        {"texture_1d", bt::scalar(bt::ScalarKind::U32), true},
        {"texture_2d", bt::vec(2, bt::ScalarKind::U32), true},
        {"texture_2d_array", bt::vec(2, bt::ScalarKind::U32), true},
        {"texture_cube", bt::vec(2, bt::ScalarKind::U32), true},
        {"texture_cube_array", bt::vec(2, bt::ScalarKind::U32), true},
        {"texture_3d", bt::vec(3, bt::ScalarKind::U32), true},
        {"texture_multisampled_2d", bt::vec(2, bt::ScalarKind::U32), false},
        {"texture_depth_2d", bt::vec(2, bt::ScalarKind::U32), true},
        {"texture_depth_2d_array", bt::vec(2, bt::ScalarKind::U32), true},
        {"texture_depth_cube", bt::vec(2, bt::ScalarKind::U32), true},
        {"texture_depth_cube_array", bt::vec(2, bt::ScalarKind::U32), true},
        {"texture_depth_multisampled_2d", bt::vec(2, bt::ScalarKind::U32), false},
        {"texture_external", bt::vec(2, bt::ScalarKind::U32), false},
    };
    return v;
}
// kValidTextureDimensionParameterTypesForStorageTextures: returnType (no level).
static const std::vector<DimArguments>& kStorageDimTable() {
    static const std::vector<DimArguments> v = {
        {"texture_storage_1d", bt::scalar(bt::ScalarKind::U32), false},
        {"texture_storage_2d", bt::vec(2, bt::ScalarKind::U32), false},
        {"texture_storage_2d_array", bt::vec(2, bt::ScalarKind::U32), false},
        {"texture_storage_3d", bt::vec(3, bt::ScalarKind::U32), false},
    };
    return v;
}
static const DimArguments* findNonStorageDim(const std::string& name) {
    for (const DimArguments& d : kNonStorageDimTable()) {
        if (d.textureType == name) {
            return &d;
        }
    }
    return nullptr;
}
static const DimArguments* findStorageDim(const std::string& name) {
    for (const DimArguments& d : kStorageDimTable()) {
        if (d.textureType == name) {
            return &d;
        }
    }
    return nullptr;
}
static std::vector<Value> nonStorageDimKeys() {
    std::vector<Value> out;
    for (const DimArguments& d : kNonStorageDimTable()) {
        out.emplace_back(d.textureType);
    }
    return out;
}
static std::vector<Value> storageDimKeys() {
    std::vector<Value> out;
    for (const DimArguments& d : kStorageDimTable()) {
        out.emplace_back(d.textureType);
    }
    return out;
}

CTS_TEST(g, "return_type,non_storage")
    .desc("Validates the return type of textureDimension is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("textureType", nonStorageDimKeys())
            .beginSubcases()
            .combine("let", {Value(false), Value(true)})
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
        const bool useLet = t.param<bool>("let");
        if (useLet) {
            t.skipIfLanguageFeatureNotSupported("texture_and_sampler_let");
        }
        const std::string returnType = t.param<std::string>("returnType");
        const std::string textureType = t.param<std::string>("textureType");
        const std::string texelType = t.param<std::string>("texelType");
        const bt::Type returnVarType = bt::typeByName(returnType);
        const DimArguments* info = findNonStorageDim(textureType);

        const std::string varWGSL = returnVarType.toString();
        const bt::Type texelArgType = b::stringToTexelType(texelType);
        const std::string textureWGSL = b::getNonStorageTextureTypeWGSL(textureType, texelArgType);
        const std::string levelWGSL = info->hasLevelArg ? ", 0" : "";
        const std::string t_let = useLet ? "let t_let = t;" : "";
        const std::string param = useLet ? "t_let" : ("t" + levelWGSL);

        const std::string code = "\n@group(0) @binding(0) var t: " + textureWGSL +
                                 ";\n@fragment fn fs() -> @location(0) vec4f {\n  " + t_let +
                                 "\n  let v: " + varWGSL + " = textureDimensions(" + param +
                                 ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(info->returnType, returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "return_type,storage")
    .desc("Validates the return type of textureDimension is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("textureType", storageDimKeys())
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
        const DimArguments* info = findStorageDim(textureType);

        const std::string varWGSL = returnVarType.toString();
        const std::string levelWGSL = info->hasLevelArg ? ", 0" : "";

        const std::string code = "\n@group(0) @binding(0) var t: " + textureType + "<" + format +
                                 ", write>;\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " +
                                 varWGSL + " = textureDimensions(t" + levelWGSL +
                                 ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(info->returnType, returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "level_argument,non_storage")
    .desc("Validates that only incorrect level arguments are rejected by textureDimension")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", nonStorageDimKeys())
            .filter([](const ParamRecord& p) {
                return findNonStorageDim(valueAs<std::string>(*findParam(p, "textureType")))
                    ->hasLevelArg;
            })
            .combine("levelType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .expand("texelType", [](const ParamRecord& p) {
                const b::TextureTypeInfo* info =
                    b::nonStorageInfo(valueAs<std::string>(*findParam(p, "textureType")));
                std::vector<Value> out;
                for (const bt::Type& ty : info->texelTypes) {
                    out.emplace_back(ty.toString());
                }
                return out;
            })
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                const bt::Type levelType =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "levelType")));
                const int64_t value = valueAs<int64_t>(*findParam(p, "value"));
                return !b::isUnsignedType(levelType) || value >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string levelType = t.param<std::string>("levelType");
        const std::string texelType = t.param<std::string>("texelType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type levelArgType = bt::typeByName(levelType);

        const bt::Type texelArgType = b::stringToTexelType(texelType);
        const std::string textureWGSL = b::getNonStorageTextureTypeWGSL(textureType, texelArgType);
        const std::string levelWGSL = bt::createWgsl(levelArgType, static_cast<long long>(value));

        const std::string code = "\n@group(0) @binding(0) var t: " + textureWGSL +
                                 ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = "
                                 "textureDimensions(t, " +
                                 levelWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(levelArgType, bt::scalar(bt::ScalarKind::I32)) ||
            bt::isConvertible(levelArgType, bt::scalar(bt::ScalarKind::U32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type,non_storage")
    .desc("Validates that incompatible texture types don't work with textureDimension")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes())
            .beginSubcases()
            .combine("textureType", nonStorageDimKeys())
            .expand("hasLevelArg", [](const ParamRecord& p) {
                const DimArguments* info =
                    findNonStorageDim(valueAs<std::string>(*findParam(p, "textureType")));
                if (info->hasLevelArg) {
                    return std::vector<Value>{Value(false), Value(true)};
                }
                return std::vector<Value>{Value(false)};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const bool hasLevelArg = t.param<bool>("hasLevelArg");

        const std::string levelWGSL = hasLevelArg ? ", 0" : "";

        const std::string code = "\n@group(0) @binding(1) var t: " + testTextureType +
                                 ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = "
                                 "textureDimensions(t" +
                                 levelWGSL + ");\n  return vec4f(0);\n}\n";

        const b::BaseAndSample bs = b::getSampleAndBaseTextureTypeForTextureType(testTextureType);
        const std::string baseTestTextureType = bs.base;

        bool expectSuccess = true;
        const DimArguments* types = findNonStorageDim(baseTestTextureType);
        if (types == nullptr) {
            types = findStorageDim(baseTestTextureType);
        }
        if (types != nullptr) {
            const bool typesMatch = !hasLevelArg || types->hasLevelArg;
            expectSuccess = typesMatch;
        }

        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string code =
            "\n    @group(0) @binding(0) var t : texture_2d<f32>;\n    fn foo() {\n      " +
            std::string(use ? "_ =" : "") + " textureDimensions(t);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
