// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureSampleCompare.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for the textureSampleCompare() builtin: coords / array_index /
// depth_ref / offset argument types, const-offset range, fragment-only stage
// restriction, return type, and incompatible texture types. Faithful port of the
// upstream local kValidTextureSampleCompareParameterTypes table and per-test
// expectSuccess logic; reuses shader_builtin_utils.h (kTestTextureTypes,
// kEntryPointsToValidateFragmentOnlyBuiltins) and binary_types.h (Type model,
// create(N).wgsl() spellings, isConvertible, kAllScalarsAndVectors).

#include <array>
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
    "shader,validation,expression,call,builtin,textureSampleCompare",
    "Validation tests for the textureSampleCompare() builtin.");

// kValidTextureSampleCompareParameterTypes (upstream local table).
struct SampleCompareArgs {
    bt::Type coordsArgType;
    bool hasArrayIndexArg;
    bt::Type offsetArgType;
    bool hasOffset;
};
const SampleCompareArgs* validParams(const std::string& textureType) {
    static const SampleCompareArgs depth2d{bt::vec(2, bt::ScalarKind::F32), false,
                                           bt::vec(2, bt::ScalarKind::I32), true};
    static const SampleCompareArgs depth2dArray{bt::vec(2, bt::ScalarKind::F32), true,
                                                bt::vec(2, bt::ScalarKind::I32), true};
    static const SampleCompareArgs depthCube{bt::vec(3, bt::ScalarKind::F32), false, bt::Type{}, false};
    static const SampleCompareArgs depthCubeArray{bt::vec(3, bt::ScalarKind::F32), true, bt::Type{},
                                                  false};
    if (textureType == "texture_depth_2d") return &depth2d;
    if (textureType == "texture_depth_2d_array") return &depth2dArray;
    if (textureType == "texture_depth_cube") return &depthCube;
    if (textureType == "texture_depth_cube_array") return &depthCubeArray;
    return nullptr;
}
// keysOf(kValidTextureSampleCompareParameterTypes).
std::vector<Value> kTextureTypes() {
    return {Value(std::string("texture_depth_2d")), Value(std::string("texture_depth_2d_array")),
            Value(std::string("texture_depth_cube")),
            Value(std::string("texture_depth_cube_array"))};
}

// keysOf(kValuesTypes) and kValuesTypes[name].
std::vector<Value> kValuesTypeNames() { return bt::typeNames(bt::kAllScalarsAndVectors()); }

CTS_TEST(g, "return_type")
    .desc("Validates the return type of textureSampleCompare is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", kValuesTypeNames())
            .combine("textureType", kTextureTypes())
            .beginSubcases()
            .expand("offset", [](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                std::vector<Value> out;
                out.emplace_back(false);
                if (a != nullptr && a->hasOffset) {
                    out.emplace_back(true);
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string returnType = t.param<std::string>("returnType");
        const std::string textureType = t.param<std::string>("textureType");
        const bool offset = t.param<bool>("offset");
        const bt::Type returnVarType = bt::typeByName(returnType);
        const SampleCompareArgs* a = validParams(textureType);

        const std::string varWGSL = returnVarType.toString();
        const std::string coordWGSL = bt::createWgsl(a->coordsArgType, 0);
        const std::string arrayWGSL = a->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " + varWGSL +
            " = textureSampleCompare(t, s, " + coordWGSL + arrayWGSL + ", 0" + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(bt::scalar(bt::ScalarKind::F32), returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "coords_argument")
    .desc("Validates that only incorrect coords arguments are rejected by textureSampleCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", kTextureTypes())
            .combine("coordType", kValuesTypeNames())
            .beginSubcases()
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "coordType")));
                return !b::isUnsignedType(ty) || valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                std::vector<Value> out;
                out.emplace_back(false);
                if (a != nullptr && a->hasOffset) {
                    out.emplace_back(true);
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string coordType = t.param<std::string>("coordType");
        const int64_t value = t.param<int64_t>("value");
        const bool offset = t.param<bool>("offset");
        const bt::Type coordArgType = bt::typeByName(coordType);
        const SampleCompareArgs* a = validParams(textureType);

        const std::string coordWGSL = bt::createWgsl(coordArgType, static_cast<long long>(value));
        const std::string arrayWGSL = a->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType + ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureSampleCompare(t, s, " +
            coordWGSL + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(coordArgType, a->coordsArgType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "array_index_argument")
    .desc("Validates that only incorrect array_index arguments are rejected by textureSampleCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", kTextureTypes())
            .filter([](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                return a != nullptr && a->hasArrayIndexArg;
            })
            .combine("arrayIndexType", kValuesTypeNames())
            .beginSubcases()
            .combine("value", {Value(int64_t(-9)), Value(int64_t(-8)), Value(int64_t(0)),
                               Value(int64_t(7)), Value(int64_t(8))})
            .filter([](const ParamRecord& p) {
                const bt::Type ty =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "arrayIndexType")));
                return !b::isUnsignedType(ty) || valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                std::vector<Value> out;
                out.emplace_back(false);
                if (a != nullptr && a->hasOffset) {
                    out.emplace_back(true);
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string arrayIndexType = t.param<std::string>("arrayIndexType");
        const int64_t value = t.param<int64_t>("value");
        const bool offset = t.param<bool>("offset");
        const bt::Type arrayIndexArgType = bt::typeByName(arrayIndexType);
        const SampleCompareArgs* a = validParams(textureType);

        const std::string coordWGSL = bt::createWgsl(a->coordsArgType, 0);
        const std::string arrayWGSL =
            bt::createWgsl(arrayIndexArgType, static_cast<long long>(value));
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType + ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureSampleCompare(t, s, " +
            coordWGSL + ", " + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::I32)) ||
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::U32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "depth_ref_argument")
    .desc("Validates that only incorrect depth_ref arguments are rejected by textureSampleCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", kTextureTypes())
            .combine("depthRefType", kValuesTypeNames())
            .beginSubcases()
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                const bt::Type ty =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "depthRefType")));
                return !b::isUnsignedType(ty) || valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                std::vector<Value> out;
                out.emplace_back(false);
                if (a != nullptr && a->hasOffset) {
                    out.emplace_back(true);
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string depthRefType = t.param<std::string>("depthRefType");
        const int64_t value = t.param<int64_t>("value");
        const bool offset = t.param<bool>("offset");
        const bt::Type depthRefArgType = bt::typeByName(depthRefType);
        const SampleCompareArgs* a = validParams(textureType);

        const std::string coordWGSL = bt::createWgsl(a->coordsArgType, 0);
        const std::string arrayWGSL = a->hasArrayIndexArg ? ", 0" : "";
        const std::string depthRefWGSL =
            bt::createWgsl(depthRefArgType, static_cast<long long>(value));
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType + ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureSampleCompare(t, s, " +
            coordWGSL + arrayWGSL + ", " + depthRefWGSL + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(depthRefArgType, bt::scalar(bt::ScalarKind::F32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "offset_argument")
    .desc("Validates that only incorrect offset arguments are rejected by textureSampleCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", kTextureTypes())
            .filter([](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                return a != nullptr && a->hasOffset;
            })
            .combine("offsetType", kValuesTypeNames())
            .beginSubcases()
            .combine("value", {Value(int64_t(-9)), Value(int64_t(-8)), Value(int64_t(0)),
                               Value(int64_t(7)), Value(int64_t(8))})
            .filter([](const ParamRecord& p) {
                const bt::Type ty =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "offsetType")));
                return !b::isUnsignedType(ty) || valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string offsetType = t.param<std::string>("offsetType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type offsetArgType = bt::typeByName(offsetType);
        const SampleCompareArgs* a = validParams(textureType);

        const std::string coordWGSL = bt::createWgsl(a->coordsArgType, 0);
        const std::string arrayWGSL = a->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = bt::createWgsl(offsetArgType, static_cast<long long>(value));

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType + ";\n@fragment fn fs() -> @location(0) vec4f {\n  _ = textureSampleCompare(t, s, " +
            coordWGSL + arrayWGSL + ", 0, " + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(offsetArgType, a->offsetArgType) && value >= -8 && value <= 7;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "offset_argument,non_const")
    .desc("Validates that only non-const offset arguments are rejected by textureSampleCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", kTextureTypes())
            .combine("varType", {Value(std::string("c")), Value(std::string("u")),
                                 Value(std::string("l"))})
            .filter([](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                return a != nullptr && a->hasOffset;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string varType = t.param<std::string>("varType");
        const SampleCompareArgs* a = validParams(textureType);

        const std::string coordWGSL = bt::createWgsl(a->coordsArgType, 0);
        const std::string arrayWGSL = a->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetTypeStr = a->offsetArgType.toString();
        const std::string offsetWGSL = offsetTypeStr + "(" + varType + ")";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType + ";\n@group(0) @binding(2) var<uniform> u: " + offsetTypeStr +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  const c = 1;\n  let l = " +
            bt::createWgsl(a->offsetArgType, 0) + ";\n  _ = textureSampleCompare(t, s, " + coordWGSL +
            arrayWGSL + ", 0, " + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = varType == "c";
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "only_in_fragment")
    .desc("Validates that textureSampleCompare must not be used in a compute or vertex shader.")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", kTextureTypes())
            .combine("entryPoint", b::kEntryPointsToValidateFragmentOnlyBuiltins())
            .expand("offset", [](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                std::vector<Value> out;
                out.emplace_back(false);
                if (a != nullptr && a->hasOffset) {
                    out.emplace_back(true);
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string entryPoint = t.param<std::string>("entryPoint");
        const bool offset = t.param<bool>("offset");
        const SampleCompareArgs* a = validParams(textureType);

        const std::string coordWGSL = bt::createWgsl(a->coordsArgType, 0);
        const std::string arrayWGSL = a->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a->offsetArgType, 0)) : "";

        const b::EntryPointConfig& config = b::entryPointConfig(entryPoint);
        const std::string code = "\n" + config.code +
                                 "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) "
                                 "@binding(1) var t: " +
                                 textureType + ";\n\nfn foo() {\n  _ = textureSampleCompare(t, s, " +
                                 coordWGSL + arrayWGSL + ", 0" + offsetWGSL + ");\n}";
        t.expectCompileResult(config.expectSuccess, code);
    });

CTS_TEST(g, "texture_type")
    .desc("Validates that incompatible texture types don't work with textureSampleCompare")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes())
            .beginSubcases()
            .combine("textureType", kTextureTypes())
            .expand("offset", [](const ParamRecord& p) {
                const SampleCompareArgs* a =
                    validParams(valueAs<std::string>(*findParam(p, "textureType")));
                std::vector<Value> out;
                out.emplace_back(false);
                if (a != nullptr && a->hasOffset) {
                    out.emplace_back(true);
                }
                return out;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string textureType = t.param<std::string>("textureType");
        const bool offset = t.param<bool>("offset");
        const SampleCompareArgs* a = validParams(textureType);

        const std::string coordWGSL = bt::createWgsl(a->coordsArgType, 0);
        const std::string arrayWGSL = a->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            testTextureType + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureSampleCompare(t, s, " +
            coordWGSL + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";

        const SampleCompareArgs* types = validParams(testTextureType);
        bool typesMatch = false;
        if (types != nullptr) {
            typesMatch = types->coordsArgType == a->coordsArgType &&
                         types->hasArrayIndexArg == a->hasArrayIndexArg &&
                         (offset ? (types->offsetArgType == a->offsetArgType) : true);
        }
        const bool expectSuccess = testTextureType == textureType || typesMatch;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string code =
            "\n    @group(0) @binding(0) var t : texture_depth_2d;\n    @group(0) @binding(1) var s "
            ": sampler_comparison;\n    fn foo() {\n      " +
            std::string(use ? "_ =" : "") +
            " textureSampleCompare(t,s,vec2(0,0),0);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
