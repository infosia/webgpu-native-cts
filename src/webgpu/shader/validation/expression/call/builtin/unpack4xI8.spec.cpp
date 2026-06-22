// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/unpack4xI8.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/call/builtin/const_override_builtin.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace b = cts::shader_validation::builtin;
namespace bt = cts::shader_validation::binary;

namespace {

constexpr const char* kFeature = "packed_4x8_integer_dot_product";
constexpr const char* kBuiltin = "unpack4xI8";
constexpr const char* kReturnType = "vec4<i32>";

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,unpack4xI8",
    "Validation tests for the unpack4xI8 builtin.");

// kValidArgumentTypes = ['u32', 'abstract-int'].
static std::vector<Value> kValidArgumentTypeKeys() {
    return {Value(std::string("u32")), Value(std::string("abstract-int"))};
}

CTS_TEST(g, "unsupported")
    .desc("Test absence of unpack4xI8 when packed_4x8_integer_dot_product is not supported.")
    .params([](ParamsBuilder u) { return u.combine("requires", {Value(false), Value(true)}); })
    .fn([](ShaderValidationTest& t) {
        // Upstream: skipIfLanguageFeatureSupported (skip when the feature IS supported).
        t.skipIf(t.hasLanguageFeature(kFeature),
                 std::string("WGSL language feature supported: ") + kFeature);
        const bool req = t.param<bool>("requires");
        const std::string preamble = req ? std::string("requires ") + kFeature + "; " : "";
        const std::string code = preamble + "const c = " + kBuiltin + "(1u);";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "supported")
    .desc("Test presence of unpack4xI8 when packed_4x8_integer_dot_product is supported.")
    .params([](ParamsBuilder u) { return u.combine("requires", {Value(false), Value(true)}); })
    .fn([](ShaderValidationTest& t) {
        t.skipIfLanguageFeatureNotSupported(kFeature);
        const bool req = t.param<bool>("requires");
        const std::string preamble = req ? std::string("requires ") + kFeature + "; " : "";
        const std::string code = preamble + "const c = " + kBuiltin + "(1u);";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of unpack4xI8 rejects invalid "
          "values.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", kValidArgumentTypeKeys())
            .filter([](const ParamRecord& p) {
                const std::string stage = valueAs<std::string>(*findParam(p, "stage"));
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
                return b::stageSupportsType(stage, ty);
            })
            .beginSubcases()
            .expand("value", [](const ParamRecord& p) {
                const bt::Type ty = bt::typeByName(valueAs<std::string>(*findParam(p, "type")));
                return b::rangeValues(b::fullRangeForType(ty));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const b::RangeValue rv = b::rangeValueFromParam(*findParam(t.params(), "value"));
        const double num = rv.isInt ? static_cast<double>(rv.i) : rv.d;
        const bool expectedResult = num >= 0.0 && num <= static_cast<double>(b::kU32Max);
        b::validateConstOrOverrideBuiltinEval(t, kBuiltin, expectedResult,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

// kArgCases: each is a list of raw WGSL arg spellings, plus whether it needs `enable f16;`.
struct ArgCase {
    const char* name;
    std::vector<std::string> args;
    bool needsF16;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", {"1u"}, false},
        {"bad_no_args", {}, false},
        {"bad_more_args", {"1u", "2u"}, false},
        {"bad_i32", {"i32(1)"}, false},
        {"bad_f32", {"1.0f"}, false},
        {"bad_f16", {"1.0h"}, true},
        {"bad_bool", {"false"}, false},
        {"bad_vec2u", {"vec2(1u, 2u)"}, false},
        {"bad_vec3u", {"vec3(1u, 2u, 3u)"}, false},
        {"bad_vec4u", {"vec4(1u, 2u, 3u, 4u)"}, false},
        {"bad_array", {"array(1u)"}, false},
    };
    return v;
}
static std::vector<Value> argNames() {
    std::vector<Value> out;
    for (const ArgCase& c : kArgCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}
static const ArgCase& findArg(const std::string& name) {
    for (const ArgCase& c : kArgCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ArgCase dummy{"", {}, false};
    return dummy;
}

CTS_TEST(g, "arguments")
    .desc("Test that unpack4xI8 is validated correctly when called with different arguments.")
    .params([](ParamsBuilder u) {
        return u.combine("args", argNames())
            .beginSubcases()
            .expand("returnType", [](const ParamRecord& p) {
                const std::string args = valueAs<std::string>(*findParam(p, "args"));
                if (args == "good") {
                    return b::typeKeys(bt::kAllScalarsAndVectors());
                }
                return std::vector<Value>{Value(std::string(kReturnType))};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string argsName = t.param<std::string>("args");
        const std::string returnType = t.param<std::string>("returnType");
        const ArgCase& c = findArg(argsName);
        const bool expectedResult = (argsName == "good") && (returnType == kReturnType);

        // Faithful reproduction of validateConstOrOverrideBuiltinEval's constant
        // path (stage is hardcoded 'constant' upstream): emit
        //   [enable f16;]\nconst v : <returnType> = unpack4xI8(<args>);
        const std::string enables = c.needsF16 ? "enable f16;" : "";
        std::string callArgs;
        for (size_t i = 0; i < c.args.size(); ++i) {
            if (i) {
                callArgs += ", ";
            }
            callArgs += c.args[i];
        }
        const std::string code = enables + "\nconst v : " + returnType + " = " + kBuiltin + "(" +
                                 callArgs + ");";
        t.expectCompileResult(expectedResult, code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of unpack4xI8 must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + kBuiltin + "(1u); }");
    });

}  // namespace
