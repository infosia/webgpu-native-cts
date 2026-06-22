// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureLoad.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for the textureLoad() builtin: coords/array_index/level/
// sample_index argument types, return type, and incompatible texture types
// (both non-storage and storage variants). Ports the two local parameter tables
// (kValidTextureLoadParameterTypesForNonStorageTextures / ...ForStorageTextures)
// and reuses the shared texture helpers (kTestTextureTypes, the non-storage
// texture-type info, getNonStorageTextureTypeWGSL,
// getSampleAndBaseTextureTypeForTextureType, kPossibleStorageTextureFormats, and
// the format/load skip helpers) from shader_builtin_utils.h. The storage
// `texture_type` test combines over the full kAllTextureFormats list (its format
// param only multiplies subcase count — it is not used in the shader), sourced
// from texture_format.h's kUncompressed/kCompressed format infos (49 + 52 = 101).

#include <array>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/expression/call/builtin/shader_builtin_utils.h"
#include "webgpu/shader/validation/shader_validation_test.h"
#include "webgpu/texture_format.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace b = cts::shader_validation::builtin;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,textureLoad",
    "Validation tests for the textureLoad() builtin.");

// ---------------------------------------------------------------------------
// Parameter tables (kValidTextureLoadParameterTypesForNonStorageTextures /
// ...ForStorageTextures). coords holds the two acceptable coordinate types
// (signed, unsigned). The ordered key list mirrors keysOf(table).
// ---------------------------------------------------------------------------
struct TextureLoadArguments {
    std::string name;
    std::array<bt::Type, 2> coords;
    bool hasArrayIndexArg;
    bool hasLevelArg;
    bool hasSampleIndexArg;
};

static bt::Type i32() { return bt::scalar(bt::ScalarKind::I32); }
static bt::Type u32() { return bt::scalar(bt::ScalarKind::U32); }
static bt::Type vec2i() { return bt::vec(2, bt::ScalarKind::I32); }
static bt::Type vec2u() { return bt::vec(2, bt::ScalarKind::U32); }
static bt::Type vec3i() { return bt::vec(3, bt::ScalarKind::I32); }
static bt::Type vec3u() { return bt::vec(3, bt::ScalarKind::U32); }

static const std::vector<TextureLoadArguments>& nonStorageTable() {
    static const std::vector<TextureLoadArguments> v = {
        {"texture_1d", {i32(), u32()}, false, true, false},
        {"texture_2d", {vec2i(), vec2u()}, false, true, false},
        {"texture_2d_array", {vec2i(), vec2u()}, true, true, false},
        {"texture_3d", {vec3i(), vec3u()}, false, true, false},
        {"texture_multisampled_2d", {vec2i(), vec2u()}, false, false, true},
        {"texture_depth_2d", {vec2i(), vec2u()}, false, true, false},
        {"texture_depth_2d_array", {vec2i(), vec2u()}, true, true, false},
        {"texture_depth_multisampled_2d", {vec2i(), vec2u()}, false, false, true},
        {"texture_external", {vec2i(), vec2u()}, false, false, false},
    };
    return v;
}

static const std::vector<TextureLoadArguments>& storageTable() {
    static const std::vector<TextureLoadArguments> v = {
        {"texture_storage_1d", {i32(), u32()}, false, false, false},
        {"texture_storage_2d", {vec2i(), vec2u()}, false, false, false},
        {"texture_storage_2d_array", {vec2i(), vec2u()}, true, false, false},
        {"texture_storage_3d", {vec3i(), vec3u()}, false, false, false},
    };
    return v;
}

static const TextureLoadArguments* findArgs(const std::vector<TextureLoadArguments>& table,
                                            const std::string& name) {
    for (const TextureLoadArguments& a : table) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

static std::vector<Value> tableKeys(const std::vector<TextureLoadArguments>& table) {
    std::vector<Value> out;
    for (const TextureLoadArguments& a : table) {
        out.emplace_back(a.name);
    }
    return out;
}

// keysOf(kValuesTypes) — all scalar/vector type names.
static std::vector<Value> valuesTypeKeys() {
    return bt::typeNames(bt::kAllScalarsAndVectors());
}

// kNonStorageTextureTypeInfo[textureType].texelTypes.map(toString).
static std::vector<Value> texelTypeNames(const std::string& textureType) {
    std::vector<Value> out;
    const b::TextureTypeInfo* info = b::nonStorageInfo(textureType);
    if (info != nullptr) {
        for (const bt::Type& ty : info->texelTypes) {
            out.emplace_back(ty.toString());
        }
    }
    return out;
}

// kAllTextureFormats (101) as WGSL format-name strings. Used only as a subcase
// multiplier in texture_type,storage (format is not referenced in the shader).
static std::vector<Value> allTextureFormatNames() {
    std::vector<Value> out;
    for (const cts::TextureFormatInfo& info : cts::kUncompressedTextureFormatInfos) {
        out.emplace_back(std::string(info.identifier));
    }
    for (const cts::TextureFormatInfo& info : cts::kCompressedTextureFormatInfos) {
        out.emplace_back(std::string(info.identifier));
    }
    return out;
}

// expectSuccess shared by texture_type,non_storage and texture_type,storage.
static bool textureTypeExpectSuccess(const std::string& baseTestTextureType,
                                     const TextureLoadArguments& expected) {
    const TextureLoadArguments* types = findArgs(nonStorageTable(), baseTestTextureType);
    if (types == nullptr) {
        types = findArgs(storageTable(), baseTestTextureType);
    }
    if (types == nullptr) {
        return false;
    }
    const int numTestNumberArgs = (types->hasArrayIndexArg ? 1 : 0) + (types->hasLevelArg ? 1 : 0) +
                                  (types->hasSampleIndexArg ? 1 : 0);
    const int numExpectNumberArgs = (expected.hasArrayIndexArg ? 1 : 0) +
                                    (expected.hasLevelArg ? 1 : 0) +
                                    (expected.hasSampleIndexArg ? 1 : 0);
    return types->coords[0] == expected.coords[0] && numTestNumberArgs == numExpectNumberArgs;
}

CTS_TEST(g, "return_type,non_storage")
    .desc("Validates the return type of textureLoad is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", valuesTypeKeys())
            .combine("textureType", tableKeys(nonStorageTable()))
            .beginSubcases()
            .expand("texelType", [](const ParamRecord& p) {
                return texelTypeNames(valueAs<std::string>(*findParam(p, "textureType")));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        b::skipIfTextureLoadNotSupportedForTextureType(t, textureType);

        const std::string returnType = t.param<std::string>("returnType");
        const std::string texelType = t.param<std::string>("texelType");
        const bt::Type returnVarType = bt::typeByName(returnType);
        const TextureLoadArguments* args = findArgs(nonStorageTable(), textureType);

        const std::string varWGSL = returnVarType.toString();
        const bt::Type texelArgType = b::stringToTexelType(texelType);
        const std::string textureWGSL = b::getNonStorageTextureTypeWGSL(textureType, texelArgType);
        const std::string coordWGSL = bt::createWgsl(args->coords[0], 0);
        const std::string arrayWGSL = args->hasArrayIndexArg ? ", 0" : "";
        const std::string levelWGSL = args->hasLevelArg ? ", 0" : "";
        const std::string sampleIndexWGSL = args->hasSampleIndexArg ? ", 0" : "";

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureWGSL +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " + varWGSL +
            " = textureLoad(t, " + coordWGSL + arrayWGSL + levelWGSL + sampleIndexWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(texelArgType, returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "coords_argument,non_storage")
    .desc("Validates that only incorrect coords arguments are rejected by textureLoad")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", tableKeys(nonStorageTable()))
            .combine("coordType", valuesTypeKeys())
            .beginSubcases()
            .expand("texelType", [](const ParamRecord& p) {
                return texelTypeNames(valueAs<std::string>(*findParam(p, "textureType")));
            })
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(bt::typeByName(valueAs<std::string>(
                           *findParam(p, "coordType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        b::skipIfTextureLoadNotSupportedForTextureType(t, textureType);

        const std::string coordType = t.param<std::string>("coordType");
        const std::string texelType = t.param<std::string>("texelType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type coordArgType = bt::typeByName(coordType);
        const TextureLoadArguments* args = findArgs(nonStorageTable(), textureType);

        const bt::Type texelArgType = b::stringToTexelType(texelType);
        const std::string textureWGSL = b::getNonStorageTextureTypeWGSL(textureType, texelArgType);
        const std::string coordWGSL = bt::createWgsl(coordArgType, static_cast<long long>(value));
        const std::string arrayWGSL = args->hasArrayIndexArg ? ", 0" : "";
        const std::string levelWGSL = args->hasLevelArg ? ", 0" : "";
        const std::string sampleIndexWGSL = args->hasSampleIndexArg ? ", 0" : "";

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureWGSL +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureLoad(t, " + coordWGSL +
            arrayWGSL + levelWGSL + sampleIndexWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(coordArgType, args->coords[0]) ||
                                   bt::isConvertible(coordArgType, args->coords[1]);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "coords_argument,storage")
    .desc("Validates that only incorrect coords arguments are rejected by textureLoad")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", tableKeys(storageTable()))
            .combine("coordType", valuesTypeKeys())
            .beginSubcases()
            .combine("format", b::kPossibleStorageTextureFormats())
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(bt::typeByName(valueAs<std::string>(
                           *findParam(p, "coordType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("readonly_and_readwrite_storage_textures");

        const std::string textureType = t.param<std::string>("textureType");
        const std::string coordType = t.param<std::string>("coordType");
        const std::string format = t.param<std::string>("format");
        const int64_t value = t.param<int64_t>("value");
        b::skipIfTextureFormatNotSupported(t, format);
        b::skipIfTextureFormatNotUsableWithStorageAccessMode(t, "read-only", format);

        const bt::Type coordArgType = bt::typeByName(coordType);
        const TextureLoadArguments* args = findArgs(storageTable(), textureType);

        const std::string coordWGSL = bt::createWgsl(coordArgType, static_cast<long long>(value));
        const std::string arrayWGSL = args->hasArrayIndexArg ? ", 0" : "";

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureType + "<" + format +
            ", read>;\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureLoad(t, " + coordWGSL +
            arrayWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(coordArgType, args->coords[0]) ||
                                   bt::isConvertible(coordArgType, args->coords[1]);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "array_index_argument,non_storage")
    .desc("Validates that only incorrect array_index arguments are rejected by textureLoad")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", tableKeys(nonStorageTable()))
            .filter([](const ParamRecord& p) {
                const TextureLoadArguments* a =
                    findArgs(nonStorageTable(), valueAs<std::string>(*findParam(p, "textureType")));
                return a != nullptr && a->hasArrayIndexArg;
            })
            .combine("arrayIndexType", valuesTypeKeys())
            .beginSubcases()
            .expand("texelType", [](const ParamRecord& p) {
                return texelTypeNames(valueAs<std::string>(*findParam(p, "textureType")));
            })
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(bt::typeByName(valueAs<std::string>(
                           *findParam(p, "arrayIndexType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        b::skipIfTextureLoadNotSupportedForTextureType(t, textureType);

        const std::string arrayIndexType = t.param<std::string>("arrayIndexType");
        const std::string texelType = t.param<std::string>("texelType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type arrayIndexArgType = bt::typeByName(arrayIndexType);
        const TextureLoadArguments* args = findArgs(nonStorageTable(), textureType);

        const bt::Type texelArgType = b::stringToTexelType(texelType);
        const std::string textureWGSL = b::getNonStorageTextureTypeWGSL(textureType, texelArgType);
        const std::string coordWGSL = bt::createWgsl(args->coords[0], 0);
        const std::string arrayWGSL = bt::createWgsl(arrayIndexArgType, static_cast<long long>(value));
        const std::string levelWGSL = args->hasLevelArg ? ", 0" : "";

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureWGSL +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureLoad(t, " + coordWGSL +
            ", " + arrayWGSL + levelWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(arrayIndexArgType, i32()) ||
                                   bt::isConvertible(arrayIndexArgType, u32());
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "array_index_argument,storage")
    .desc("Validates that only incorrect array_index arguments are rejected by textureLoad")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", tableKeys(storageTable()))
            .filter([](const ParamRecord& p) {
                const TextureLoadArguments* a =
                    findArgs(storageTable(), valueAs<std::string>(*findParam(p, "textureType")));
                return a != nullptr && a->hasArrayIndexArg;
            })
            .combine("arrayIndexType", valuesTypeKeys())
            .beginSubcases()
            .combine("format", b::kPossibleStorageTextureFormats())
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(bt::typeByName(valueAs<std::string>(
                           *findParam(p, "arrayIndexType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported("readonly_and_readwrite_storage_textures");

        const std::string textureType = t.param<std::string>("textureType");
        const std::string arrayIndexType = t.param<std::string>("arrayIndexType");
        const std::string format = t.param<std::string>("format");
        const int64_t value = t.param<int64_t>("value");
        b::skipIfTextureFormatNotSupported(t, format);
        b::skipIfTextureFormatNotUsableWithStorageAccessMode(t, "read-only", format);

        const bt::Type arrayIndexArgType = bt::typeByName(arrayIndexType);
        const TextureLoadArguments* args = findArgs(storageTable(), textureType);

        const std::string coordWGSL = bt::createWgsl(args->coords[0], 0);
        const std::string arrayWGSL = bt::createWgsl(arrayIndexArgType, static_cast<long long>(value));
        const std::string levelWGSL = args->hasLevelArg ? ", 0" : "";

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureType + "<" + format +
            ", read>;\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureLoad(t, " + coordWGSL +
            ", " + arrayWGSL + levelWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(arrayIndexArgType, i32()) ||
                                   bt::isConvertible(arrayIndexArgType, u32());
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "level_argument,non_storage")
    .desc("Validates that only incorrect level arguments are rejected by textureLoad")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", tableKeys(nonStorageTable()))
            .filter([](const ParamRecord& p) {
                const TextureLoadArguments* a =
                    findArgs(nonStorageTable(), valueAs<std::string>(*findParam(p, "textureType")));
                return a != nullptr && a->hasLevelArg;
            })
            .combine("levelType", valuesTypeKeys())
            .beginSubcases()
            .expand("texelType", [](const ParamRecord& p) {
                return texelTypeNames(valueAs<std::string>(*findParam(p, "textureType")));
            })
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(bt::typeByName(valueAs<std::string>(
                           *findParam(p, "levelType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        b::skipIfTextureLoadNotSupportedForTextureType(t, textureType);

        const std::string levelType = t.param<std::string>("levelType");
        const std::string texelType = t.param<std::string>("texelType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type levelArgType = bt::typeByName(levelType);
        const TextureLoadArguments* args = findArgs(nonStorageTable(), textureType);

        const bt::Type texelArgType = b::stringToTexelType(texelType);
        const std::string textureWGSL = b::getNonStorageTextureTypeWGSL(textureType, texelArgType);
        const std::string coordWGSL = bt::createWgsl(args->coords[0], 0);
        const std::string arrayWGSL = args->hasArrayIndexArg ? ", 0" : "";
        const std::string levelWGSL = bt::createWgsl(levelArgType, static_cast<long long>(value));

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureWGSL +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureLoad(t, " + coordWGSL +
            arrayWGSL + ", " + levelWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(levelArgType, i32()) || bt::isConvertible(levelArgType, u32());
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "sample_index_argument,non_storage")
    .desc("Validates that only incorrect sample_index arguments are rejected by textureLoad")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", tableKeys(nonStorageTable()))
            .filter([](const ParamRecord& p) {
                const TextureLoadArguments* a =
                    findArgs(nonStorageTable(), valueAs<std::string>(*findParam(p, "textureType")));
                return a != nullptr && a->hasSampleIndexArg;
            })
            .combine("sampleIndexType", valuesTypeKeys())
            .beginSubcases()
            .expand("texelType", [](const ParamRecord& p) {
                return texelTypeNames(valueAs<std::string>(*findParam(p, "textureType")));
            })
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(bt::typeByName(valueAs<std::string>(
                           *findParam(p, "sampleIndexType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        b::skipIfTextureLoadNotSupportedForTextureType(t, textureType);

        const std::string sampleIndexType = t.param<std::string>("sampleIndexType");
        const std::string texelType = t.param<std::string>("texelType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type sampleIndexArgType = bt::typeByName(sampleIndexType);
        const TextureLoadArguments* args = findArgs(nonStorageTable(), textureType);
        // upstream asserts !hasLevelArg here (always true for these texture types).

        const bt::Type texelArgType = b::stringToTexelType(texelType);
        const std::string textureWGSL = b::getNonStorageTextureTypeWGSL(textureType, texelArgType);
        const std::string coordWGSL = bt::createWgsl(args->coords[0], 0);
        const std::string arrayWGSL = args->hasArrayIndexArg ? ", 0" : "";
        const std::string sampleIndexWGSL =
            bt::createWgsl(sampleIndexArgType, static_cast<long long>(value));

        const std::string code =
            "\n@group(0) @binding(0) var t: " + textureWGSL +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureLoad(t, " + coordWGSL +
            arrayWGSL + ", " + sampleIndexWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(sampleIndexArgType, i32()) ||
                                   bt::isConvertible(sampleIndexArgType, u32());
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type,non_storage")
    .desc("Validates that incompatible texture types don't work with textureLoad")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes())
            .beginSubcases()
            .combine("textureType", tableKeys(nonStorageTable()));
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string textureType = t.param<std::string>("textureType");
        b::skipIfTextureLoadNotSupportedForTextureType(t, textureType);
        const TextureLoadArguments* args = findArgs(nonStorageTable(), textureType);

        const std::string coordWGSL = bt::createWgsl(args->coords[0], 0);
        const std::string arrayWGSL = args->hasArrayIndexArg ? ", 0" : "";
        const std::string levelWGSL = args->hasLevelArg ? ", 0" : "";
        const std::string sampleIndexWGSL = args->hasSampleIndexArg ? ", 0" : "";

        const std::string code =
            "\n@group(0) @binding(1) var t: " + testTextureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureLoad(t, " + coordWGSL +
            arrayWGSL + levelWGSL + sampleIndexWGSL + ");\n  return vec4f(0);\n}\n";

        const b::BaseAndSample bs = b::getSampleAndBaseTextureTypeForTextureType(testTextureType);
        const bool expectSuccess = textureTypeExpectSuccess(bs.base, *args);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type,storage")
    .desc("Validates that incompatible texture types don't work with textureLoad")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes())
            .beginSubcases()
            .combine("textureType", tableKeys(storageTable()))
            .combine("format", allTextureFormatNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string textureType = t.param<std::string>("textureType");
        b::skipIfTextureLoadNotSupportedForTextureType(t, textureType);
        const TextureLoadArguments* args = findArgs(storageTable(), textureType);

        const std::string coordWGSL = bt::createWgsl(args->coords[0], 0);
        const std::string arrayWGSL = args->hasArrayIndexArg ? ", 0" : "";
        const std::string levelWGSL = args->hasLevelArg ? ", 0" : "";
        const std::string sampleIndexWGSL = args->hasSampleIndexArg ? ", 0" : "";

        const std::string code =
            "\n@group(0) @binding(1) var t: " + testTextureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureLoad(t, " + coordWGSL +
            arrayWGSL + levelWGSL + sampleIndexWGSL + ");\n  return vec4f(0);\n}\n";

        const b::BaseAndSample bs = b::getSampleAndBaseTextureTypeForTextureType(testTextureType);
        const bool expectSuccess = textureTypeExpectSuccess(bs.base, *args);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string code =
            "\n    @group(0) @binding(0) var t : texture_2d<f32>;\n    fn foo() {\n      " +
            std::string(use ? "_ =" : "") + " textureLoad(t, vec2(0,0), 0);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
