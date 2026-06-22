// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureGatherCompare.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for the textureGatherCompare() builtin: coords / array_index
// / depth_ref / offset parameter types, the const-expression and [-8,7] range
// requirements on offset, the vec4f return type, and incompatible texture types.
// Only the four depth texture types are valid; the sampler is a
// sampler_comparison. The texture_type test resolves the candidate via
// getSampleAndBaseTextureTypeForTextureType (shared helper) and accepts only a
// structural match (no identity short-circuit). The local
// kValidTextureGatherCompareParameterTypes table preserves the upstream object
// key order. .specURL is dropped (unsupported by our TestBuilder).

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
    "shader,validation,expression,call,builtin,textureGatherCompare",
    "Validation tests for the textureGatherCompare() builtin.");

// kValidTextureGatherCompareParameterTypes (object key order preserved).
struct SampleArgs {
    std::string textureType;
    bt::Type coordsArgType;
    bool hasArrayIndexArg;
    bt::Type offsetArgType;
    bool hasOffset;
};
const std::vector<SampleArgs>& kValidTextureGatherCompareParameterTypes() {
    using bt::ScalarKind;
    static const bt::Type none{};  // placeholder when hasOffset == false
    static const std::vector<SampleArgs> v = {
        {"texture_depth_2d", bt::vec(2, ScalarKind::F32), false, bt::vec(2, ScalarKind::I32), true},
        {"texture_depth_2d_array", bt::vec(2, ScalarKind::F32), true, bt::vec(2, ScalarKind::I32),
         true},
        {"texture_depth_cube", bt::vec(3, ScalarKind::F32), false, none, false},
        {"texture_depth_cube_array", bt::vec(3, ScalarKind::F32), true, none, false},
    };
    return v;
}
const SampleArgs* sampleArgsByName(const std::string& name) {
    for (const SampleArgs& s : kValidTextureGatherCompareParameterTypes()) {
        if (s.textureType == name) {
            return &s;
        }
    }
    return nullptr;
}
std::vector<Value> sampleTextureTypeKeys() {
    std::vector<Value> out;
    for (const SampleArgs& s : kValidTextureGatherCompareParameterTypes()) {
        out.emplace_back(s.textureType);
    }
    return out;
}

CTS_TEST(g, "return_type")
    .desc("Validates the return type of textureGatherCompare is the expected type.")
    .params([](ParamsBuilder u) {
        return u.combine("returnType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("textureType", sampleTextureTypeKeys())
            .beginSubcases()
            .expand("offset", [](const ParamRecord& p) {
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                if (s != nullptr && s->hasOffset) {
                    return std::vector<Value>{Value(false), Value(true)};
                }
                return std::vector<Value>{Value(false)};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string returnType = t.param<std::string>("returnType");
        const std::string textureType = t.param<std::string>("textureType");
        const bool offset = t.param<bool>("offset");
        const bt::Type returnVarType = bt::typeByName(returnType);
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string varWGSL = returnVarType.toString();
        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(s->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " + varWGSL +
            " = textureGatherCompare(t, s, " + coordWGSL + arrayWGSL + ", 0" + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(bt::vec(4, bt::ScalarKind::F32), returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "coords_argument")
    .desc("Validates that only incorrect coords arguments are rejected by textureGatherCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", sampleTextureTypeKeys())
            .combine("coordType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("value", {Value(-1), Value(0), Value(1)})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(
                           bt::typeByName(valueAs<std::string>(*findParam(p, "coordType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                if (s != nullptr && s->hasOffset) {
                    return std::vector<Value>{Value(false), Value(true)};
                }
                return std::vector<Value>{Value(false)};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string coordType = t.param<std::string>("coordType");
        const bool offset = t.param<bool>("offset");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type coordArgType = bt::typeByName(coordType);
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(coordArgType, static_cast<long long>(value));
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(s->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGatherCompare(t, s, " +
            coordWGSL + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(coordArgType, s->coordsArgType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "array_index_argument")
    .desc("Validates that only incorrect array_index arguments are rejected by "
          "textureGatherCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", sampleTextureTypeKeys())
            .filter([](const ParamRecord& p) {
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                return s != nullptr && s->hasArrayIndexArg;
            })
            .combine("arrayIndexType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("value", {Value(-9), Value(-8), Value(0), Value(7), Value(8)})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(
                           bt::typeByName(valueAs<std::string>(*findParam(p, "arrayIndexType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                if (s != nullptr && s->hasOffset) {
                    return std::vector<Value>{Value(false), Value(true)};
                }
                return std::vector<Value>{Value(false)};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string arrayIndexType = t.param<std::string>("arrayIndexType");
        const int64_t value = t.param<int64_t>("value");
        const bool offset = t.param<bool>("offset");
        const bt::Type arrayIndexArgType = bt::typeByName(arrayIndexType);
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL =
            bt::createWgsl(arrayIndexArgType, static_cast<long long>(value));
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(s->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGatherCompare(t, s, " +
            coordWGSL + ", " + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::I32)) ||
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::U32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "depth_ref_argument")
    .desc("Validates that only incorrect depth_ref arguments are rejected by textureGatherCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", sampleTextureTypeKeys())
            .combine("depthRefType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("value", {Value(-1), Value(0), Value(1)})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(
                           bt::typeByName(valueAs<std::string>(*findParam(p, "depthRefType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            })
            .expand("offset", [](const ParamRecord& p) {
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                if (s != nullptr && s->hasOffset) {
                    return std::vector<Value>{Value(false), Value(true)};
                }
                return std::vector<Value>{Value(false)};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string depthRefType = t.param<std::string>("depthRefType");
        const bool offset = t.param<bool>("offset");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type depthRefArgType = bt::typeByName(depthRefType);
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string depthRefWGSL =
            bt::createWgsl(depthRefArgType, static_cast<long long>(value));
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(s->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGatherCompare(t, s, " +
            coordWGSL + arrayWGSL + ", " + depthRefWGSL + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(depthRefArgType, bt::scalar(bt::ScalarKind::F32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "offset_argument")
    .desc("Validates that only incorrect offset arguments are rejected by textureGatherCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", sampleTextureTypeKeys())
            .filter([](const ParamRecord& p) {
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                return s != nullptr && s->hasOffset;
            })
            .combine("offsetType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            .combine("value", {Value(-9), Value(-8), Value(0), Value(7), Value(8)})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(
                           bt::typeByName(valueAs<std::string>(*findParam(p, "offsetType")))) ||
                       valueAs<int64_t>(*findParam(p, "value")) >= 0;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string offsetType = t.param<std::string>("offsetType");
        const int64_t value = t.param<int64_t>("value");
        const bt::Type offsetArgType = bt::typeByName(offsetType);
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL =
            bt::createWgsl(offsetArgType, static_cast<long long>(value));

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureGatherCompare(t, s, " +
            coordWGSL + arrayWGSL + ", 0, " + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(offsetArgType, s->offsetArgType) && value >= -8 && value <= 7;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "offset_argument,non_const")
    .desc("Validates that only non-const offset arguments are rejected by textureGatherCompare")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", sampleTextureTypeKeys())
            .combine("varType", {Value(std::string("c")), Value(std::string("u")),
                                 Value(std::string("l"))})
            .filter([](const ParamRecord& p) {
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                return s != nullptr && s->hasOffset;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string varType = t.param<std::string>("varType");
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetTypeStr = s->offsetArgType.toString();
        const std::string offsetWGSL = offsetTypeStr + "(" + varType + ")";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            textureType + ";\n@group(0) @binding(2) var<uniform> u: " + offsetTypeStr +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  const c = 1;\n  let l = " +
            bt::createWgsl(s->offsetArgType, 0) + ";\n  let v = textureGatherCompare(t, s, " +
            coordWGSL + arrayWGSL + ", 0, " + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = varType == "c";
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "texture_type")
    .desc("Validates that incompatible texture types don't work with textureGatherCompare")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes())
            .combine("textureType", sampleTextureTypeKeys())
            .expand("offset", [](const ParamRecord& p) {
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                if (s != nullptr && s->hasOffset) {
                    return std::vector<Value>{Value(false), Value(true)};
                }
                return std::vector<Value>{Value(false)};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string textureType = t.param<std::string>("textureType");
        const bool offset = t.param<bool>("offset");
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(s->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler_comparison;\n@group(0) @binding(1) var t: " +
            testTextureType + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = "
            "textureGatherCompare(t, s, " +
            coordWGSL + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";

        const std::string baseTestTextureType =
            b::getSampleAndBaseTextureTypeForTextureType(testTextureType).base;

        const SampleArgs* types = sampleArgsByName(baseTestTextureType);
        bool typesMatch = false;
        if (types != nullptr) {
            typesMatch = types->coordsArgType == s->coordsArgType &&
                         types->hasArrayIndexArg == s->hasArrayIndexArg &&
                         (offset ? (types->hasOffset == s->hasOffset &&
                                    types->offsetArgType == s->offsetArgType)
                                 : true);
        }
        const bool expectSuccess = typesMatch;
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
            std::string(use ? "_ =" : "") + " textureGatherCompare(t, s, vec2(0,0), 0);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
