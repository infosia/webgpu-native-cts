// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/refract.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
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
    "shader,validation,expression,call,builtin,refract",
    "Validation tests for the refract() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of refract() only errors in "
          "cases where a the calculations result in a non-representable value for the given type.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", b::typeKeys(b::kConvertableToFloatVectors()))
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
            .expand("b",
                    [](const ParamRecord& p) {
                        return b::rangeValues(b::fullRangeForType(
                            bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
                    })
            .expand("c", [](const ParamRecord& p) {
                return b::rangeValues(b::fullRangeForType(
                    bt::typeByName(valueAs<std::string>(*findParam(p, "type"))), 5));
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const bt::Type ty = bt::typeByName(t.param<std::string>("type"));
        const bt::Type scalarType = bt::scalarTypeOf(ty);
        const bt::ScalarKind k = scalarType.kind;
        b::ConstantOrOverrideValueChecker vCheck(t, scalarType);

        const b::RangeValue arv = b::rangeValueFromParam(*findParam(t.params(), "a"));
        const b::RangeValue brv = b::rangeValueFromParam(*findParam(t.params(), "b"));
        const b::RangeValue crv = b::rangeValueFromParam(*findParam(t.params(), "c"));
        const double a = rvNum(arv);
        const double bb = rvNum(brv);
        const double c = rvNum(crv);

        const double b_dot_a = vCheck.checkedResult(bb * a * ty.width);
        const double b_dot_a_2 = vCheck.checkedResult(b_dot_a * b_dot_a);
        const double one_minus_b_dot_a_2 = vCheck.checkedResult(1.0 - b_dot_a_2);
        const double c2 = vCheck.checkedResult(c * c);
        const double c2_one_minus_b_dot_a_2 = vCheck.checkedResult(c2 * one_minus_b_dot_a_2);
        const double kVal = vCheck.checkedResult(1.0 - c2_one_minus_b_dot_a_2);

        t.skipIf(b::isSubnormalForKind(k, b::quantizeForKind(k, b_dot_a)) ||
                 b::isSubnormalForKind(k, b::quantizeForKind(k, b_dot_a_2)) ||
                 b::isSubnormalForKind(k, b::quantizeForKind(k, c2)) ||
                 b::isSubnormalForKind(k, b::quantizeForKind(k, kVal)));

        if (kVal >= 0) {
            if (vCheck.isNearZero(kVal)) {
                t.skip("K value is at or near 0.");
            }
            const double ca = vCheck.checkedResult(c * a);
            const double cbda = vCheck.checkedResult(c * b_dot_a);
            const double sqrt_k = vCheck.checkedResult(std::sqrt(kVal));
            const double cdba_sqrt_k = vCheck.checkedResult(cbda + sqrt_k);
            const double cdba_sqrt_k_b = vCheck.checkedResult(cdba_sqrt_k * bb);
            vCheck.checkedResult(ca - cdba_sqrt_k_b);
        }

        b::validateConstOrOverrideBuiltinEval(
            t, "refract", vCheck.allChecksPassed(),
            {b::createBuiltinValue(ty, arv), b::createBuiltinValue(ty, brv),
             b::createBuiltinValue(scalarType, crv)},
            stage);
    });

// kValidArgs (used by args + return + must_use). The `args` test passes iff the
// case is one of these three valid forms.
struct ArgCase {
    const char* name;
    const char* suffix;
    bool valid;  // present in kValidArgs
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"vec2f", "(vec2(0), vec2(1), 2.0)", true},
        {"vec3f", "(vec3(0), vec3(1), 2.0)", true},
        {"vec4f", "(vec4(0), vec4(1), 2.0)", true},
        {"bad_no_parens", "", false},
        {"bad_0args", "()", false},
        {"bad_1arg", "(vec3(0))", false},
        {"bad_2arg", "(vec3(0), vec3(1))", false},
        {"bad_3arg", "(vec3(0), vec3(1), 2.0, vec3(3))", false},
        {"bad_vec2_vec3", "(vec2(0), vec3(1), 2.0)", false},
        {"bad_vec3_vec4", "(vec3(0), vec4(1), 2.0)", false},
        {"bad_vec4_vec2", "(vec4(0), vec2(1), 2.0)", false},
        {"bad_0bool", "(false, vec3(1), 2.0)", false},
        {"bad_0array", "(array(1.1,2.2), vec3(1), 2.0)", false},
        {"bad_0struct", "(modf(2.2), vec3(1), 2.0)", false},
        {"bad_0int", "(0i, vec3(1), 2.0)", false},
        {"bad_0uint", "(0u, vec3(1), 2.0)", false},
        {"bad_0f32", "(0.0, vec3(1), 2.0)", false},
        {"bad_0f16", "(0.0h, vec3(1), 2.0)", false},
        {"bad_0veci", "(vec3i(0), vec3(1), 2.0)", false},
        {"bad_0vecu", "(vec3u(0), vec3(1), 2.0)", false},
        {"bad_1bool", "(vec3(0), true, 2.0)", false},
        {"bad_1array", "(vec3(0), array(1.1,2.2), 2.0)", false},
        {"bad_1struct", "(vec3(0), modf(2.2), 2.0)", false},
        {"bad_1int", "(vec3(0), 1i, 2.0)", false},
        {"bad_1uint", "(vec3(0), 1u, 2.0)", false},
        {"bad_1f32", "(vec3(0), 1.0, 2.0)", false},
        {"bad_1f16", "(vec3(0), 1.0h, 2.0)", false},
        {"bad_1veci", "(vec3(0), vec3i(1), 2.0)", false},
        {"bad_1vecu", "(vec3(0), vec3u(1), 2.0)", false},
        {"bad_2bool", "(vec3(0), vec3(1), true)", false},
        {"bad_2array", "(vec3(0), vec3(1), array(1.1,2.2))", false},
        {"bad_2struct", "(vec3(0), vec3(1), modf(2.2))", false},
        {"bad_2int", "(vec3(0), vec3(1), 2i)", false},
        {"bad_2uint", "(vec3(0), vec3(1), 2u)", false},
        {"bad_2veci", "(vec3(0), vec3(1), vec3i(2))", false},
        {"bad_2vecu", "(vec3(0), vec3(1), vec3u(2))", false},
        {"bad_2vecf", "(vec3(0), vec3(1), vec3f(2))", false},
        {"bad_2vech", "(vec3(0), vec3(1), vec3h(2))", false},
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
static const ArgCase& findArg(const std::string& name) {
    for (const ArgCase& a : kArgCases()) {
        if (name == a.name) {
            return a;
        }
    }
    static const ArgCase dummy{"", "", false};
    return dummy;
}

// kValidArgs only (vec2f/vec3f/vec4f).
static std::vector<Value> validArgNames() {
    std::vector<Value> out;
    for (const ArgCase& a : kArgCases()) {
        if (a.valid) {
            out.emplace_back(std::string(a.name));
        }
    }
    return out;
}

CTS_TEST(g, "args")
    .desc("Test compilation failure of refract with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const ArgCase& a = findArg(t.param<std::string>("arg"));
        t.expectCompileResult(a.valid, std::string("const c = refract") + a.suffix + ";");
    });

CTS_TEST(g, "return")
    .desc("Test refract return value type")
    .params([](ParamsBuilder u) {
        std::vector<Value> returnTypes;
        for (const Value& v : validArgNames()) {
            returnTypes.push_back(v);
        }
        for (const char* extra : {"vec3u", "vec3i", "u32", "i32", "f32", "bool"}) {
            returnTypes.emplace_back(std::string(extra));
        }
        return u.combine("arg", validArgNames()).combine("returnType", returnTypes);
    })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        const std::string returnType = t.param<std::string>("returnType");
        const ArgCase& a = findArg(arg);
        t.expectCompileResult(returnType == arg, std::string("const c: ") + returnType +
                                                     " = refract" + a.suffix + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of refract must be used")
    .params([](ParamsBuilder u) {
        return u.combine("arg", validArgNames()).combine("use", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        const ArgCase& a = findArg(arg);
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "refract" + a.suffix + "; }");
    });

}  // namespace
