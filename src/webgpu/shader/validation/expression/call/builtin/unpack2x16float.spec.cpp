// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/call/builtin/unpack2x16float.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cstdint>
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

constexpr const char* kBuiltin = "unpack2x16float";
constexpr const char* kReturnType = "vec2<f32>";

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,call,builtin,unpack2x16float",
    "Validation tests for the unpack2x16float builtin.");

static std::vector<Value> validArgumentTypes() {
    return {std::string("u32"), std::string("abstract-int")};
}

// kBit.f16 zero/infinity bit patterns (util/constants.ts).
constexpr uint32_t kF16PositiveZero = 0x0000;
constexpr uint32_t kF16PositiveInfinity = 0x7c00;
constexpr uint32_t kF16NegativeZero = 0x8000;
constexpr uint32_t kF16NegativeInfinity = 0xfc00;

// Return true iff f16AsU16 is a valid f16 bit pattern (a uint16) that is not
// NaN nor Inf. Mirrors isValidF16AsU16.
static bool isValidF16AsU16(uint32_t f16AsU16) {
    return (kF16PositiveZero <= f16AsU16 && f16AsU16 < kF16PositiveInfinity) ||
           (kF16NegativeZero <= f16AsU16 && f16AsU16 < kF16NegativeInfinity);
}

// Mirrors isValidPacked2xF16: value is an integer in [u32.min, u32.max] and both
// 16-bit halves are valid f16 bit patterns (not NaN/Inf).
static bool isValidPacked2xF16(int64_t value) {
    if (value < 0 || value > static_cast<int64_t>(b::kU32Max)) {
        return false;
    }
    const uint32_t packed = static_cast<uint32_t>(value);
    return isValidF16AsU16(packed & 0xffffu) && isValidF16AsU16(packed >> 16);
}

struct ArgCase {
    const char* name;
    const char* args;
    bool hasF16;
    bool good;
};
static const std::vector<ArgCase>& kArgCases() {
    static const std::vector<ArgCase> v = {
        {"good", "1u", false, true},
        {"bad_no_args", "", false, false},
        {"bad_more_args", "1u, 2u", false, false},
        {"bad_i32", "i32(1)", false, false},
        {"bad_f32", "1.0f", false, false},
        {"bad_f16", "1.0h", true, false},
        {"bad_bool", "false", false, false},
        {"bad_vec2u", "vec2(1u, 2u)", false, false},
        {"bad_vec3u", "vec3(1u, 2u, 3u)", false, false},
        {"bad_vec4u", "vec4(1u, 2u, 3u, 4u)", false, false},
        {"bad_array", "array(1u)", false, false},
    };
    return v;
}
static const ArgCase& argByName(const std::string& name) {
    for (const ArgCase& c : kArgCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ArgCase dummy{"", "", false, false};
    return dummy;
}
static std::vector<Value> argCaseNames() {
    std::vector<Value> out;
    for (const ArgCase& c : kArgCases()) {
        out.emplace_back(std::string(c.name));
    }
    return out;
}

CTS_TEST(g, "values")
    .desc("Validates that constant evaluation and override evaluation of unpack2x16float rejects "
          "invalid values.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", b::kConstantAndOverrideStages())
            .combine("type", validArgumentTypes())
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
        const int64_t value = rv.isInt ? rv.i : static_cast<int64_t>(rv.d);
        const bool expectedResult = isValidPacked2xF16(value);
        b::validateConstOrOverrideBuiltinEval(t, kBuiltin, expectedResult,
                                              {b::createBuiltinValue(ty, rv)}, stage);
    });

CTS_TEST(g, "arguments")
    .desc("Test that unpack2x16float is validated correctly when called with different arguments.")
    .params([](ParamsBuilder u) {
        return u.combine("args", argCaseNames())
            .beginSubcases()
            .expand("returnType", [](const ParamRecord& p) {
                const std::string args = valueAs<std::string>(*findParam(p, "args"));
                if (args == "good") {
                    return bt::typeNames(bt::kAllScalarsAndVectors());
                }
                return std::vector<Value>{std::string(kReturnType)};
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string args = t.param<std::string>("args");
        const std::string returnType = t.param<std::string>("returnType");
        const ArgCase& c = argByName(args);
        const bool expectedResult = (args == "good") && returnType == kReturnType;
        const std::string enables = c.hasF16 ? "enable f16;" : "";
        const std::string code = enables + "\nconst v : " + returnType + " = " + kBuiltin + "(" +
                                 c.args + ");";
        t.expectCompileResult(expectedResult, code);
    });

CTS_TEST(g, "must_use")
    .desc("Result of unpack2x16float must be used")
    .params([](ParamsBuilder u) { return u.combine("use", {Value(true), Value(false)}); })
    .fn([](ShaderValidationTest& t) {
        const bool use = t.param<bool>("use");
        const std::string useIt = use ? "_ = " : "";
        t.expectCompileResult(use, std::string("fn f() { ") + useIt + kBuiltin + "(1u); }");
    });

}  // namespace
