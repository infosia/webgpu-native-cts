// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/textureSampleBias.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for the textureSampleBias() builtin: coords / array_index /
// bias / offset parameter types, the const-expression and [-8,7] range
// requirements on offset, the fragment-only stage restriction, the return type,
// and incompatible texture types. The kValidTextureSampleBiasParameterTypes
// table is ported locally (coordsArgType / hasArrayIndexArg / offsetArgType per
// textureType), preserving the upstream object key order. The bias_argument
// test passes fractional values (e.g. 15.99) so they are carried as Value(double)
// and spelled via a local create(N).wgsl() equivalent for fractional literals.
// .specURL is intentionally dropped (unsupported by our TestBuilder).

#include <cmath>
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
    "shader,validation,expression,call,builtin,textureSampleBias",
    "Validation tests for the textureSampleBias() builtin.");

// kValidTextureSampleBiasParameterTypes (object key order preserved).
struct SampleArgs {
    std::string textureType;
    bt::Type coordsArgType;
    bool hasArrayIndexArg;
    bt::Type offsetArgType;
    bool hasOffset;
};
const std::vector<SampleArgs>& kValidTextureSampleBiasParameterTypes() {
    using bt::ScalarKind;
    static const bt::Type none{};  // placeholder when hasOffset == false
    static const std::vector<SampleArgs> v = {
        {"texture_2d<f32>", bt::vec(2, ScalarKind::F32), false, bt::vec(2, ScalarKind::I32), true},
        {"texture_2d_array<f32>", bt::vec(2, ScalarKind::F32), true, bt::vec(2, ScalarKind::I32),
         true},
        {"texture_3d<f32>", bt::vec(3, ScalarKind::F32), false, bt::vec(3, ScalarKind::I32), true},
        {"texture_cube<f32>", bt::vec(3, ScalarKind::F32), false, none, false},
        {"texture_cube_array<f32>", bt::vec(3, ScalarKind::F32), true, none, false},
    };
    return v;
}
const SampleArgs* sampleArgsByName(const std::string& name) {
    for (const SampleArgs& s : kValidTextureSampleBiasParameterTypes()) {
        if (s.textureType == name) {
            return &s;
        }
    }
    return nullptr;
}
std::vector<Value> sampleTextureTypeKeys() {
    std::vector<Value> out;
    for (const SampleArgs& s : kValidTextureSampleBiasParameterTypes()) {
        out.emplace_back(s.textureType);
    }
    return out;
}

// create(N).wgsl() for a possibly-fractional value N. Mirrors conversion.ts:
// integer-valued floats keep the trailing ".0", fractional values keep their
// decimal spelling; f32 -> suffix "f", f16 -> "h", abstract-float -> none.
std::string formatFloatLiteral(double n) {
    if (n == std::floor(n)) {
        return std::to_string(static_cast<long long>(n)) + ".0";
    }
    // Trim trailing zeros from the default formatting (e.g. 15.990000 -> 15.99).
    std::string s = std::to_string(n);
    const std::string::size_type dot = s.find('.');
    if (dot != std::string::npos) {
        std::string::size_type last = s.find_last_not_of('0');
        if (last == dot) {
            last += 1;  // keep one digit after the dot
        }
        s.erase(last + 1);
    }
    return s;
}
std::string scalarValueWgslD(bt::ScalarKind k, double n) {
    using bt::ScalarKind;
    switch (k) {
        case ScalarKind::Bool: return n != 0 ? "true" : "false";
        case ScalarKind::AbstractInt: return std::to_string(static_cast<long long>(n));
        case ScalarKind::AbstractFloat: return formatFloatLiteral(n);
        case ScalarKind::F32: return formatFloatLiteral(n) + "f";
        case ScalarKind::F16: return formatFloatLiteral(n) + "h";
        case ScalarKind::I32: return "i32(" + std::to_string(static_cast<long long>(n)) + ")";
        case ScalarKind::U32: return std::to_string(static_cast<long long>(n)) + "u";
    }
    return "";
}
std::string createWgslD(const bt::Type& ty, double n) {
    if (ty.isScalar()) {
        return scalarValueWgslD(ty.kind, n);
    }
    std::string els;
    const std::string el = scalarValueWgslD(ty.kind, n);
    for (int i = 0; i < ty.width; ++i) {
        if (i) {
            els += ", ";
        }
        els += el;
    }
    return "vec" + std::to_string(ty.width) + "(" + els + ")";
}

CTS_TEST(g, "return_type")
    .desc("Validates the return type of textureSampleBias is the expected type.")
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
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v: " + varWGSL +
            " = textureSampleBias(t, s, " + coordWGSL + arrayWGSL + ", 0" + offsetWGSL +
            ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(bt::vec(4, bt::ScalarKind::F32), returnVarType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "coords_argument")
    .desc("Validates that only incorrect coords arguments are rejected by textureSampleBias")
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
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureSampleBias(t, s, " +
            coordWGSL + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(coordArgType, s->coordsArgType);
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "array_index_argument")
    .desc("Validates that only incorrect array_index arguments are rejected by textureSampleBias")
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
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureSampleBias(t, s, " +
            coordWGSL + ", " + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::I32)) ||
            bt::isConvertible(arrayIndexArgType, bt::scalar(bt::ScalarKind::U32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "bias_argument")
    .desc("Validates that only incorrect bias arguments are rejected by textureSampleBias")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", sampleTextureTypeKeys())
            .filter([](const ParamRecord& p) {
                // filter out types with no offset
                const SampleArgs* s =
                    sampleArgsByName(valueAs<std::string>(*findParam(p, "textureType")));
                return s != nullptr && s->hasOffset;
            })
            .combine("biasType", bt::typeNames(bt::kAllScalarsAndVectors()))
            .beginSubcases()
            // The spec mentions limits of > -16 and < 15.99 so pass some values around there.
            // No error is mentioned for out of range values so make sure no error is generated.
            .combine("value", {Value(-17.0), Value(-16.0), Value(-8.0), Value(0.0), Value(7.0),
                               Value(15.99), Value(16.0)})
            .filter([](const ParamRecord& p) {
                return !b::isUnsignedType(
                           bt::typeByName(valueAs<std::string>(*findParam(p, "biasType")))) ||
                       valueAs<double>(*findParam(p, "value")) >= 0;
            })
            .filter([](const ParamRecord& p) {
                // filter out non-integer values passed to integer types.
                const double value = valueAs<double>(*findParam(p, "value"));
                const bt::Type biasType =
                    bt::typeByName(valueAs<std::string>(*findParam(p, "biasType")));
                return value == std::floor(value) ||
                       bt::isFloatType(bt::scalarTypeOf(biasType));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string textureType = t.param<std::string>("textureType");
        const std::string biasType = t.param<std::string>("biasType");
        const double value = t.param<double>("value");
        const bt::Type biasArgType = bt::typeByName(biasType);
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string biasWGSL = createWgslD(biasArgType, value);

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureSampleBias(t, s, " +
            coordWGSL + arrayWGSL + ", " + biasWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = bt::isConvertible(biasArgType, bt::scalar(bt::ScalarKind::F32));
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "offset_argument")
    .desc("Validates that only incorrect offset arguments are rejected by textureSampleBias")
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
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = textureSampleBias(t, s, " +
            coordWGSL + arrayWGSL + ", 0, " + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess =
            bt::isConvertible(offsetArgType, s->offsetArgType) && value >= -8 && value <= 7;
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "offset_argument,non_const")
    .desc("Validates that only non-const offset arguments are rejected by textureSampleBias")
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
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " + textureType +
            ";\n@group(0) @binding(2) var<uniform> u: " + offsetTypeStr +
            ";\n@fragment fn fs() -> @location(0) vec4f {\n  const c = 1;\n  let l = " +
            bt::createWgsl(s->offsetArgType, 0) + ";\n  let v = textureSampleBias(t, s, " +
            coordWGSL + arrayWGSL + ", 0, " + offsetWGSL + ");\n  return vec4f(0);\n}\n";
        const bool expectSuccess = varType == "c";
        t.expectCompileResult(expectSuccess, code);
    });

CTS_TEST(g, "only_in_fragment")
    .desc("Validates that textureSampleBias must not be used in a compute or vertex shader.")
    .params([](ParamsBuilder u) {
        return u.combine("textureType", sampleTextureTypeKeys())
            .combine("entryPoint", b::kEntryPointsToValidateFragmentOnlyBuiltins())
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
        const std::string entryPoint = t.param<std::string>("entryPoint");
        const bool offset = t.param<bool>("offset");
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(s->offsetArgType, 0)) : "";

        const b::EntryPointConfig& config = b::entryPointConfig(entryPoint);
        const std::string code =
            "\n" + config.code + "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var "
            "t: " +
            textureType + ";\n\nfn foo() {\n  _ = textureSampleBias(t, s, " + coordWGSL + arrayWGSL +
            ", 0" + offsetWGSL + ");\n}";
        t.expectCompileResult(config.expectSuccess, code);
    });

CTS_TEST(g, "texture_type")
    .desc("Validates that incompatible texture types don't work with textureSampleBias")
    .params([](ParamsBuilder u) {
        return u.combine("testTextureType", b::kTestTextureTypes())
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
        const std::string testTextureType = t.param<std::string>("testTextureType");
        const std::string textureType = t.param<std::string>("textureType");
        const bool offset = t.param<bool>("offset");
        const SampleArgs* s = sampleArgsByName(textureType);

        const std::string coordWGSL = bt::createWgsl(s->coordsArgType, 0);
        const std::string arrayWGSL = s->hasArrayIndexArg ? ", 0" : "";
        const std::string offsetWGSL = offset ? (", " + bt::createWgsl(s->offsetArgType, 0)) : "";

        const std::string code =
            "\n@group(0) @binding(0) var s: sampler;\n@group(0) @binding(1) var t: " +
            testTextureType + ";\n@fragment fn fs() -> @location(0) vec4f {\n  let v = "
            "textureSampleBias(t, s, " +
            coordWGSL + arrayWGSL + ", 0" + offsetWGSL + ");\n  return vec4f(0);\n}\n";

        const SampleArgs* types = sampleArgsByName(testTextureType);
        bool typesMatch = false;
        if (types != nullptr) {
            typesMatch = types->coordsArgType == s->coordsArgType &&
                         types->hasArrayIndexArg == s->hasArrayIndexArg &&
                         (offset ? (types->hasOffset == s->hasOffset &&
                                    types->offsetArgType == s->offsetArgType)
                                 : true);
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
            "\n    @group(0) @binding(0) var t : texture_2d<f32>;\n    @group(0) @binding(1) var s : "
            "sampler;\n    fn foo() {\n      " +
            std::string(use ? "_ =" : "") + " textureSampleBias(t, s, vec2(0,0), 0);\n    }";
        t.expectCompileResult(use, code);
    });

}  // namespace
