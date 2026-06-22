// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/quantizeToF16.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cmath>
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
    "shader,validation,expression,call,builtin,quantizeToF16",
    "Validation tests for the quantizeToF16() builtin.");

// kValidArgumentTypes = [abstractFloat, vec2/3/4 abstractFloat, ...kConcreteF32ScalarsAndVectors]
static std::vector<bt::Type> kValidArgumentTypes() {
    std::vector<bt::Type> out = {
        bt::scalar(bt::ScalarKind::AbstractFloat),
        bt::vec(2, bt::ScalarKind::AbstractFloat),
        bt::vec(3, bt::ScalarKind::AbstractFloat),
        bt::vec(4, bt::ScalarKind::AbstractFloat),
    };
    for (const bt::Type& t : b::kConcreteF32ScalarsAndVectors()) {
        out.push_back(t);
    }
    return out;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of quantizeToF16() error on "
          "invalid inputs.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(kValidArgumentTypes()))
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

        // Should be invalid if the quantized value exceeds the maximum
        // representable 16-bit float value.
        bool expectedResult = true;
        const double f16Value = cts::shader_validation::binary::quantizeToF16(num);
        if (std::isinf(f16Value)) {
            expectedResult = false;
        }
        b::validateConstOrOverrideBuiltinEval(t, "quantizeToF16", expectedResult,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

struct ArgCase {
    const char* name;
    const char* suffix;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "(vec3f())"},
        {"bad_no_parens", ""},
        {"bad_0args", "()"},
        {"bad_2arg", "(1.0, 2.0)"},
        {"bad_0bool", "(false)"},
        {"bad_0array", "(array(1.1,2.2))"},
        {"bad_0struct", "(modf(2.2))"},
        {"bad_0uint", "(1u)"},
        {"bad_0int", "(1i)"},
        {"bad_0vec2i", "(vec2i())"},
        {"bad_0vec2u", "(vec2u())"},
        {"bad_0vec3i", "(vec3i())"},
        {"bad_0vec3u", "(vec3u())"},
        {"bad_0vec4i", "(vec4i())"},
        {"bad_0vec4u", "(vec4u())"},
        {"bad_0f16", "(1h)"},
        {"bad_0vec2h", "(vec2h())"},
        {"bad_0vec3h", "(vec3h())"},
        {"bad_0vec4h", "(vec4h())"},
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
    static const ArgCase dummy{"", ""};
    return dummy;
}

CTS_TEST(g, "args")
    .desc("Test compilation failure of quantizeToF16 with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const ArgCase& c = findArg(t.param<std::string>("arg"));
        t.expectCompileResult(std::string(c.name) == "good",
                              std::string("const c = quantizeToF16") + c.suffix + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of quantizeToF16 must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, "fn f() { " + useIt + "quantizeToF16(vec3f()); }");
    });

}  // namespace
