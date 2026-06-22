// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/dot.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// dot() validation. AbstractInt arguments are handled with exact int64 (BigInt
// upstream) and OOB is detected against the int64 range (kValue.i64.isOOB).

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
    "shader,validation,expression,call,builtin,dot", "Validation tests for the dot() builtin.");

static double rvNum(const b::RangeValue& rv) {
    return rv.isInt ? static_cast<double>(rv.i) : rv.d;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of dot() never errors")
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
        bool checksPassed = true;
        if (scalarType.kind == bt::ScalarKind::AbstractInt) {
            const int64_t a = arv.i;
            const int64_t bb = brv.i;
            const int64_t vecSize = ty.width;
            // ab = a * b ; ab * vecSize. Each product OOB of int64 fails the check.
            const bool abOOB = b::i64MulOOB(a, bb);
            const int64_t ab = a * bb;  // wraps; only meaningful when not OOB.
            const bool abVecOOB = abOOB || b::i64MulOOB(ab, vecSize);
            checksPassed = !abOOB && !abVecOOB;
        } else {
            const double a = rvNum(arv);
            const double bb = rvNum(brv);
            const double vecSize = ty.width;
            const double ab = vCheck.checkedResult(a * bb);
            vCheck.checkedResult(ab * vecSize);
            checksPassed = vCheck.allChecksPassed();
        }

        b::validateConstOrOverrideBuiltinEval(
            t, "dot", checksPassed,
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
        {"bad_vec_size", "(vec2(0), vec3(1))"},
        {"bad_0bool", "(false, vec3(1))"},
        {"bad_0array", "(array(1.1,2.2), vec3(1))"},
        {"bad_0struct", "(modf(2.2), vec3(1))"},
        {"bad_0int", "(0i, vec3(1))"},
        {"bad_0uint", "(0u, vec3(1))"},
        {"bad_0f32", "(0.0, vec3(1))"},
        {"bad_0f16", "(0.0h, vec3(1))"},
        {"bad_0abstract", "(0, vec3(1))"},
        {"bad_1bool", "(vec3(0), true)"},
        {"bad_1array", "(vec3(0), array(1.1,2.2))"},
        {"bad_1struct", "(vec3(0), modf(2.2))"},
        {"bad_1int", "(vec3(0), 0i)"},
        {"bad_1uint", "(vec3(0), 0u)"},
        {"bad_1f32", "(vec3(0), 0.0)"},
        {"bad_1f16", "(vec3(0), 0.0h)"},
        {"bad_1abstract", "(vec3(0), 0)"},
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
    .desc("Test compilation failure of dot with variously shaped and typed arguments")
    .params([](ParamsBuilder u) { return u.combine("arg", argNames()); })
    .fn([](ShaderValidationTest& t) {
        const std::string arg = t.param<std::string>("arg");
        t.expectCompileResult(arg == "good", std::string("const c = dot") + argSuffix(arg) + ";");
    });

CTS_TEST(g, "must_use")
    .desc("Result of dot must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + "dot" + argSuffix("good") +
                                       "; }");
    });

}  // namespace
