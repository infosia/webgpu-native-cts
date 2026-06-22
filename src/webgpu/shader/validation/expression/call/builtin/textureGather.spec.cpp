// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureGather.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for the textureGather() builtin. Faithful port: the local
// kValidTextureGatherParameterTypes table (sampleTypes / hasComponentArg /
// coordsArgType / hasArrayIndexArg / offsetArgType) is encoded as a struct keyed
// by texture-type string; sample/value Type spellings come from binary_types.h
// (create(N).wgsl(), Type.toString(), scalarTypeOf). Shared texture helpers
// (kTestTextureTypes, getSampleAndBaseTextureTypeForTextureType) are reused from
// shader_builtin_utils.h. .specURL is intentionally dropped (unsupported).

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
    "shader,validation,expression,call,builtin,textureGather",
    "Validation tests for the textureGather() builtin.");

// kColorSampleTypes = [vec4f, vec4i, vec4u]; kDepthSampleTypes = [vec4f].
struct GatherArgs {
    std::vector<bt::Type> sampleTypes;
    bool hasComponentArg;
    bt::Type coordsArgType;
    bool hasArrayIndexArg;
    bt::Type offsetArgType;
    bool hasOffset;
};

static const std::vector<std::pair<std::string, GatherArgs>>& gatherTable() {
    static const std::vector<bt::Type> color = {bt::vec(4, bt::ScalarKind::F32),
                                                bt::vec(4, bt::ScalarKind::I32),
                                                bt::vec(4, bt::ScalarKind::U32)};
    static const std::vector<bt::Type> depth = {bt::vec(4, bt::ScalarKind::F32)};
    static const bt::Type vec2f = bt::vec(2, bt::ScalarKind::F32);
    static const bt::Type vec3f = bt::vec(3, bt::ScalarKind::F32);
    static const bt::Type vec2i = bt::vec(2, bt::ScalarKind::I32);
    static const bt::Type none{};
    static const std::vector<std::pair<std::string, GatherArgs>> v = {
        {"texture_2d", {color, true, vec2f, false, vec2i, true}},
        {"texture_2d_array", {color, true, vec2f, true, vec2i, true}},
        {"texture_cube", {color, true, vec3f, false, none, false}},
        {"texture_cube_array", {color, true, vec3f, true, none, false}},
        {"texture_depth_2d", {depth, false, vec2f, false, vec2i, true}},
        {"texture_depth_2d_array", {depth, false, vec2f, true, vec2i, true}},
        {"texture_depth_cube", {depth, false, vec3f, false, none, false}},
        {"texture_depth_cube_array", {depth, false, vec3f, true, none, false}},
    };
    return v;
}
static const GatherArgs& gatherArgs(const std::string& textureType) {
    for (const auto& kv : gatherTable()) {
        if (kv.first == textureType) {
            return kv.second;
        }
    }
    static const GatherArgs dummy{};
    return dummy;
}
static std::vector<Value> gatherTypeKeys() {
    std::vector<Value> out;
    for (const auto& kv : gatherTable()) {
        out.emplace_back(kv.first);
    }
    return out;
}
static std::vector<Value> sampleTypeValues(const std::string& textureType) {
    std::vector<Value> out;
    for (const bt::Type& s : gatherArgs(textureType).sampleTypes) {
        out.emplace_back(s.toString());
    }
    return out;
}
static std::vector<Value> offsetValues(const std::string& textureType) {
    if (gatherArgs(textureType).hasOffset) {
        return {Value(false), Value(true)};
    }
    return {Value(false)};
}
static std::vector<Value> valuesTypeKeys() {
    return bt::typeNames(bt::kAllScalarsAndVectors());
}

static std::string sampleTypeSuffix(const std::string& textureType, const bt::Type& sampleVarType) {
    if (textureType.find("depth") != std::string::npos) {
        return "";
    }
    return "<" + bt::scalarKindString(bt::scalarTypeOf(sampleVarType).kind) + ">";
}

CTS_TEST(g, "return_type")
    .desc("Validates the return type of textureGather is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", valuesTypeKeys())
            .combine("textureType", gatherTypeKeys())
            .expand("sampleType",
                    [](const ParamRecord& p) {
                        return sampleTypeValues(valueAs<std::string>(*findParam(p, "textureType")));
                    })
            .beginSubcases()
            .expand("offset", [](const ParamRecord& p) {
                return offsetValues(valueAs<std::string>(*findParam(p, "textureType")));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string returnType = t.param<std::string>("returnType");
        const std::string textureType = t.param<std::string>("textureType");
        const std::string sampleType = t.param<std::string>("sampleType");
        const bool offset = t.param<bool>("offset");
        const bt::Type returnVarType = bt::typeByName(returnType);
        const bt::Type sampleVarType = b::stringToTexelType(sampleType);
        const GatherArgs& a = gatherArgs(textureType);

        const std::string sampleTypeWGSL = sampleTypeSuffix(textureType, sampleVarType);
        const std::string componentWGSL = a.hasComponentArg ? "0, " : "";
        const std::string varWGSL = returnVarType.toString();
        const std::string coordWGSL = bt::createWgsl(a.coordsArgType, 0);
        const std::string arrayWGSL = a.hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a.offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            sampleTypeWGSL + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " + varWGSL +
            " = textureGather(" + componentWGSL + "t, s, " + coordWGSL + arrayWGSL + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(sampleVarType, returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "component_argument")
    .desc("Validates that only incorrect components arguments are rejected by textureGather")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", gatherTypeKeys())
            .filter([](const ParamRecord& p) {
                return gatherArgs(valueAs<std::string>(*findParam(p, "textureType"))).hasComponentArg;
            })
            .combine("componentType", valuesTypeKeys())
            .expand("sampleType",
                    [](const ParamRecord& p) {
                        return sampleTypeValues(valueAs<std::string>(*findParam(p, "textureType")));
                    })
            .beginSubcases()
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1)),
                               Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
            .filter([](const ParamRecord& p) {
                const bt::Type ct = bt::typeByName(valueAs<std::string>(*findParam(p, "componentType")));
                return !b::isUnsignedType(ct) || valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                return offsetValues(valueAs<std::string>(*findParam(p, "textureType")));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string componentType = t.param<std::string>("componentType");
        const std::string sampleType = t.param<std::string>("sampleType");
        const bool offset = t.param<bool>("offset");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type sampleVarType = b::stringToTexelType(sampleType);
        const bt::Type componentArgType = bt::typeByName(componentType);
        const GatherArgs& a = gatherArgs(textureType);

        const std::string sampleTypeWGSL = sampleTypeSuffix(textureType, sampleVarType);
        const std::string componentWGSL = bt::createWgsl(componentArgType, value);
        const std::string coordWGSL = bt::createWgsl(a.coordsArgType, 0);
        const std::string arrayWGSL = a.hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a.offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            sampleTypeWGSL + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGather(" +
            componentWGSL + ", t, s, " + coordWGSL + arrayWGSL + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = (bt::isConvertible(componentArgType, bt::scalar(bt::ScalarKind::I32)) ||
                                    bt::isConvertible(componentArgType, bt::scalar(bt::ScalarKind::U32))) &&
                                   value >= 0 && value <= 3;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "component_argument,non_const")
    .desc("Validates that only non-const components arguments are rejected by textureGather")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", gatherTypeKeys())
            .filter([](const ParamRecord& p) {
                return gatherArgs(valueAs<std::string>(*findParam(p, "textureType"))).hasComponentArg;
            })
            .expand("sampleType",
                    [](const ParamRecord& p) {
                        return sampleTypeValues(valueAs<std::string>(*findParam(p, "textureType")));
                    })
            .combine("varType", {Value(std::string("c")), Value(std::string("u")),
                                 Value(std::string("l"))})
            .beginSubcases()
            .expand("offset", [](const ParamRecord& p) {
                return offsetValues(valueAs<std::string>(*findParam(p, "textureType")));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string sampleType = t.param<std::string>("sampleType");
        const std::string varType = t.param<std::string>("varType");
        const bool offset = t.param<bool>("offset");
        const bt::Type sampleVarType = b::stringToTexelType(sampleType);
        const bt::Type componentArgType = bt::scalar(bt::ScalarKind::U32);
        const GatherArgs& a = gatherArgs(textureType);

        const std::string sampleTypeWGSL = sampleTypeSuffix(textureType, sampleVarType);
        const std::string componentWGSL = componentArgType.toString() + "(" + varType + ")";
        const std::string coordWGSL = bt::createWgsl(a.coordsArgType, 0);
        const std::string arrayWGSL = a.hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a.offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            sampleTypeWGSL + ";\n@group(0) @binding(2) var<uniform> u: " + componentArgType.toString() +
            ";\n\n@fragment fn fs() -> @location(0) vec4f {\n  const c = 1;\n  let l = 1;\n  let v = "
            "textureGather(" +
            componentWGSL + ", t, s, " + coordWGSL + arrayWGSL + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = varType == "c";
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "coords_argument")
    .desc("Validates that only incorrect coords arguments are rejected by textureGather")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", gatherTypeKeys())
            .expand("sampleType",
                    [](const ParamRecord& p) {
                        return sampleTypeValues(valueAs<std::string>(*findParam(p, "textureType")));
                    })
            .combine("coordType", valuesTypeKeys())
            .beginSubcases()
            .combine("value", {Value(int64_t(-1)), Value(int64_t(0)), Value(int64_t(1))})
            .filter([](const ParamRecord& p) {
                const bt::Type ct = bt::typeByName(valueAs<std::string>(*findParam(p, "coordType")));
                return !b::isUnsignedType(ct) || valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                return offsetValues(valueAs<std::string>(*findParam(p, "textureType")));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string sampleType = t.param<std::string>("sampleType");
        const std::string coordType = t.param<std::string>("coordType");
        const bool offset = t.param<bool>("offset");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type sampleVarType = b::stringToTexelType(sampleType);
        const bt::Type coordArgType = bt::typeByName(coordType);
        const GatherArgs& a = gatherArgs(textureType);

        const std::string sampleTypeWGSL = sampleTypeSuffix(textureType, sampleVarType);
        const std::string componentWGSL = a.hasComponentArg ? "0, " : "";
        const std::string coordWGSL = bt::createWgsl(coordArgType, value);
        const std::string arrayWGSL = a.hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a.offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            sampleTypeWGSL + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGather(" +
            componentWGSL + "t, s, " + coordWGSL + arrayWGSL + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(coordArgType, a.coordsArgType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "array_index_argument")
    .desc("Validates that only incorrect array_index arguments are rejected by textureGather")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", gatherTypeKeys())
            .filter([](const ParamRecord& p) {
                return gatherArgs(valueAs<std::string>(*findParam(p, "textureType"))).hasArrayIndexArg;
            })
            .expand("sampleType",
                    [](const ParamRecord& p) {
                        return sampleTypeValues(valueAs<std::string>(*findParam(p, "textureType")));
                    })
            .combine("arrayIndexType", valuesTypeKeys())
            .beginSubcases()
            .combine("value", {Value(int64_t(-9)), Value(int64_t(-8)), Value(int64_t(0)),
                               Value(int64_t(7)), Value(int64_t(8))})
            .filter([](const ParamRecord& p) {
                const bt::Type at = bt::typeByName(valueAs<std::string>(*findParam(p, "arrayIndexType")));
                return !b::isUnsignedType(at) || valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                return offsetValues(valueAs<std::string>(*findParam(p, "textureType")));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string sampleType = t.param<std::string>("sampleType");
        const std::string arrayIndexType = t.param<std::string>("arrayIndexType");
        const int64_t value = t.param<int64_t>("value");
        const bool offset = t.param<bool>("offset");
        const bt::Type sampleVarType = b::stringToTexelType(sampleType);
        const bt::Type arrayIndexArgType = bt::typeByName(arrayIndexType);
        const GatherArgs& a = gatherArgs(textureType);

        const std::string sampleTypeWGSL = sampleTypeSuffix(textureType, sampleVarType);
        const std::string componentWGSL = a.hasComponentArg ? "0, " : "";
        const std::string coordWGSL = bt::createWgsl(a.coordsArgType, 0);
        const std::string arrayWGSL = bt::createWgsl(arrayIndexArgType, value);
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a.offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            sampleTypeWGSL + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGather(" +
            componentWGSL + "t, s, " + coordWGSL + ", " + arrayWGSL + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::I32)) ||
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::U32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "offset_argument")
    .desc("Validates that only incorrect offset arguments are rejected by textureGather")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", gatherTypeKeys())
            .filter([](const ParamRecord& p) {
                return gatherArgs(valueAs<std::string>(*findParam(p, "textureType"))).hasOffset;
            })
            .expand("sampleType",
                    [](const ParamRecord& p) {
                        return sampleTypeValues(valueAs<std::string>(*findParam(p, "textureType")));
                    })
            .combine("offsetType", valuesTypeKeys())
            .beginSubcases()
            .combine("value", {Value(int64_t(-9)), Value(int64_t(-8)), Value(int64_t(0)),
                               Value(int64_t(7)), Value(int64_t(8))})
            .filter([](const ParamRecord& p) {
                const bt::Type ot = bt::typeByName(valueAs<std::string>(*findParam(p, "offsetType")));
                return !b::isUnsignedType(ot) || valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string sampleType = t.param<std::string>("sampleType");
        const std::string offsetType = t.param<std::string>("offsetType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type sampleVarType = b::stringToTexelType(sampleType);
        const bt::Type offsetArgType = bt::typeByName(offsetType);
        const GatherArgs& a = gatherArgs(textureType);

        const std::string sampleTypeWGSL = sampleTypeSuffix(textureType, sampleVarType);
        const std::string componentWGSL = a.hasComponentArg ? "0, " : "";
        const std::string coordWGSL = bt::createWgsl(a.coordsArgType, 0);
        const std::string arrayWGSL = a.hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = bt::createWgsl(offsetArgType, value);

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            sampleTypeWGSL + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGather(" +
            componentWGSL + "t, s, " + coordWGSL + arrayWGSL + ", " + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(offsetArgType, a.offsetArgType) && value >= -8 && value <= 7;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "offset_argument,non_const")
    .desc("Validates that only non-const offset arguments are rejected by textureGather")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", gatherTypeKeys())
            .expand("sampleType",
                    [](const ParamRecord& p) {
                        return sampleTypeValues(valueAs<std::string>(*findParam(p, "textureType")));
                    })
            .combine("varType", {Value(std::string("c")), Value(std::string("u")),
                                 Value(std::string("l"))})
            .filter([](const ParamRecord& p) {
                return gatherArgs(valueAs<std::string>(*findParam(p, "textureType"))).hasOffset;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string sampleType = t.param<std::string>("sampleType");
        const std::string varType = t.param<std::string>("varType");
        const bt::Type sampleVarType = b::stringToTexelType(sampleType);
        const GatherArgs& a = gatherArgs(textureType);

        const std::string sampleTypeWGSL = sampleTypeSuffix(textureType, sampleVarType);
        const std::string componentWGSL = a.hasComponentArg ? "0, " : "";
        const std::string coordWGSL = bt::createWgsl(a.coordsArgType, 0);
        const std::string arrayWGSL = a.hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = a.offsetArgType.toString() + "(" + varType + ")";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            sampleTypeWGSL + ";\n@group(0) @binding(2) var<uniform> u: " + a.offsetArgType.toString() +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  const c = 1;\n  let l = " +
            bt::createWgsl(a.offsetArgType, 0) + ";\n  let v = textureGather(" + componentWGSL +
            "t, s, " + coordWGSL + arrayWGSL + ", " + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = varType == "c";
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type")
    .desc("Validates that incompatible texture types don't work with textureGather")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes())
            .combine("textureType", gatherTypeKeys())
            .expand("offset", [](const ParamRecord& p) {
                return offsetValues(valueAs<std::string>(*findParam(p, "textureType")));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string textureType = t.param<std::string>("textureType");
        const bool offset = t.param<bool>("offset");
        const GatherArgs& a = gatherArgs(textureType);

        const std::string componentWGSL = a.hasComponentArg ? "0, " : "";
        const std::string coordWGSL = bt::createWgsl(a.coordsArgType, 0);
        const std::string arrayWGSL = a.hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(a.offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + testTextureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGather(" + componentWGSL +
            "t, s, " + coordWGSL + arrayWGSL + offsetWGSL + ");\n  return vec4f(0);\n}\n";

        const std::string baseTestTextureType =
            b::getSampleAndBaseTextureTypeForTextureType(testTextureType).base;

        bool typesMatch = false;
        for (const auto& kv : gatherTable()) {
            if (kv.first == baseTestTextureType) {
                const GatherArgs& types = kv.second;
                typesMatch = types.hasComponentArg == a.hasComponentArg &&
                             types.coordsArgType == a.coordsArgType &&
                             types.hasArrayIndexArg == a.hasArrayIndexArg &&
                             (offset ? types.offsetArgType == a.offsetArgType : true);
                break;
            }
        }
        t.expectCompileResult(typesMatch, code);
    });

CTS_TEST(g, "must_use")
    .desc("Tests that the result must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string code =
            "\n    @group(0) @binding(0) var t : texture_2d<f32>;\n    @group(0) @binding(1) var s : "
            "sampler;\n    fn foo() {\n      " +
            std::string(use ? "_ =" : "") + " textureGather(0, t, s, vec2(0,0));\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
