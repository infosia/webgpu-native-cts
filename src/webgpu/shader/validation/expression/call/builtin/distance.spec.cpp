// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/distance.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,distance",
    "Validation tests for the distance() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of distance() never errors")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatScalarsAndVectors()))
            .filter([](const ParamRecord& p) {
                return b::stageSupportsType(valueAs<std::string>(*findParam(p, "stage")),
                                           bt::typeByName(valueAs<std::string>(*findParam(p, "type"))));
            })
            .beginSubcases()
            .expand("a",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
                    })
            .expand("b", [](const ParamRecord& p) {
                return b::rangeValues(b::fullRangeForType(
                    bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bt::Type scalarType = bt::scalarTypeOf(ty);
        b::ConstantOrOverrideValueChecker vCheck(t, scalarType);

        const b::RangeValue arv = b::rangeValueFromParam(*findParam(t.params(), "a"));
        const b::RangeValue brv = b::rangeValueFromParam(*findParam(t.params(), "b"));
        const double a = rvNum(arv);
        const double bb = rvNum(brv);
        const double ab = vCheck.checkedResult(a - bb);

        // Only calculates the full length if the type is a vector.
        if (ty.isVector() && ty.width > 1) {
            const double ab2 = vCheck.checkedResult(ab * ab);
            const double sqrLen = vCheck.checkedResult(ab2 * ty.width);
            if (vCheck.isNearZero(sqrLen)) {
                t.skip("Squared length is at or near 0.");
            }
        }

        b::validateConstOrOverrideBuiltinEval(
            t, "distance", vCheck.allChecksPassed(),
            {b::createBuiltinValue(ty, arv), b::createBuiltinValue(ty, brv)}, stage);
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(vec3(0), vec3(1))"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_1arg", "(vec3(0))"},
        {"bad_3arg", "(vec3(0), vec3(1), vec3(2))"},
        {"bad_0bool", "(false, vec3(1))"},
        {"bad_0array", "(array(1.1,2.2), vec3(1))"},
        {"bad_0struct", "(modf(2.2), vec3(1))"},
        {"bad_0int", "(0i, vec3(1))"},
        {"bad_0vec2i", "(vec2i(), vec3(1))"},
        {"bad_0vec3i", "(vec3i(), vec3(1))"},
        {"bad_0vec4i", "(vec4i(), vec3(1))"},
        {"bad_0uint", "(0u, vec3(1))"},
        {"bad_0vec2u", "(vec2u(), vec3(1))"},
        {"bad_0vec3u", "(vec3u(), vec3(1))"},
        {"bad_0vec4u", "(vec4u(), vec3(1))"},
        {"bad_1bool", "(vec3(0), true)"},
        {"bad_1array", "(vec3(0), array(1.1,2.2))"},
        {"bad_1struct", "(vec3(0), modf(2.2))"},
        {"bad_1int", "(vec3(0), 0i)"},
        {"bad_1vec2i", "(vec3(0), vec2i())"},
        {"bad_1vec3i", "(vec3(0), vec3i())"},
        {"bad_1vec4i", "(vec3(0), vec4i())"},
        {"bad_1uint", "(vec3(0), 0u)"},
        {"bad_1vec2u", "(vec3(0), vec2u())"},
        {"bad_1vec3u", "(vec3(0), vec3u())"},
        {"bad_1vec4u", "(vec3(0), vec4u())"},
    };
    return v;
}
static std::vector<Value> argNames() {
    std::vector<Value> out;
    for (const ArgCase& a : kArgCases()) {
        out.emplace_back(std::string(a.name));
    }
    return out;
}
static const char* argSuffix(const std::string& name) {
    for (const ArgCase& a : kArgCases()) {
        if (name == a.name) {
            return a.suffix;
        }
    }
    return "";
}

CTS_TEST(g, "args")
    .desc("Test compilation failure of distance with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good",
                              std::string("const c = distance") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of distance must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "distance" +
                                       argSuffix("good") + "; }");
    });

}  // namespace
