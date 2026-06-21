// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/matrix/add_sub.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// The overflow/underflow tests combine over a numeric `rhs`/`lhs` param whose two
// values are a floating-point extreme (kValue.<f32|f16|f64>.{positive.max,negative.min})
// and the integer 1. Upstream passes them as JS numbers, so the case-query name is the
// JS Number.toString() spelling and the same spelling is embedded into the WGSL. We use
// double-valued params so the case names render unquoted (matching upstream), and embed
// the exact JS decimal spellings precomputed below. `... === 1` becomes `rhs == 1.0`.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,matrix,add_sub",
    "Validation tests for matrix addition and subtraction expressions.");

// ---- kValue spellings (exact JS Number.toString() of the IEEE values) -------
// kValue.f32.positive.max = reinterpretU32AsF32(0x7f7fffff)
static const char* kF32Max = "3.4028234663852886e+38";
static const char* kF32MaxHalf = "1.7014117331926443e+38";  // f32.positive.max / 2
// kValue.f32.negative.min = reinterpretU32AsF32(0xff7fffff)
static const char* kF32NegMinHalf = "-1.7014117331926443e+38";  // f32.negative.min / 2
// kValue.f16.positive.max = reinterpretU16AsF16(0x7bff) = 65504
static const char* kF16Max = "65504";
static const char* kF16MaxHalf = "32752";  // f16.positive.max / 2
// kValue.f16.negative.min = reinterpretU16AsF16(0xfbff) = -65504
static const char* kF16NegMinHalf = "-32752";  // f16.negative.min / 2
// kValue.f64.positive.max = Number.MAX_VALUE
static const char* kF64Max = "1.7976931348623157e+308";
static const char* kF64MaxHalf = "8.988465674311579e+307";  // f64.positive.max / 2
static const char* kF64NegMinHalf = "-8.988465674311579e+307";  // f64.negative.min / 2

// kOperators (object key order preserved).
struct Operator {
    const char* name;
    const char* op;
};
static const std::vector<Operator>& kOperators() {
    static const std::vector<Operator> v = {
        {"add", "+"},
        {"sub", "-"},
    };
    return v;
}

// kTests (object key order preserved). `src` is the WGSL value spelling.
struct Argument {
    const char* name;
    const char* src;
    bool is_f16;
};
static const std::vector<Argument>& kTests() {
    static const std::vector<Argument> v = {
        {"bool", "false", false},
        {"vec", "vec2()", false},
        {"i32", "1i", false},
        {"u32", "1u", false},
        {"ai", "1", false},
        {"f32", "1f", false},
        {"f16", "1h", true},
        {"af", "1.0", false},
        {"texture", "t", false},
        {"sampler", "s", false},
        {"atomic", "a", false},
        {"struct", "str", false},
        {"array", "arr", false},
        {"matf_ai_matching", "mat2x4(0, 0, 0, 0, 0, 0, 0, 0)", false},
        {"matf_ai_no_matching", "mat2x2(0, 0, 0, 0)", false},
        {"matf_size_matching", "mat2x3f()", false},
        {"matf_size_no_match", "mat4x4f()", false},
        {"math_size_matching", "mat2x3h()", true},
        {"math_size_no_matching", "mat4x4h()", true},
    };
    return v;
}

static std::vector<Value> operatorNames() {
    std::vector<Value> values;
    for (const Operator& o : kOperators()) {
        values.emplace_back(std::string(o.name));
    }
    return values;
}
static const Operator& findOperator(const std::string& name) {
    for (const Operator& o : kOperators()) {
        if (name == o.name) {
            return o;
        }
    }
    static const Operator dummy{"", ""};
    return dummy;
}
static std::vector<Value> testNames() {
    std::vector<Value> values;
    for (const Argument& a : kTests()) {
        values.emplace_back(std::string(a.name));
    }
    return values;
}
static const Argument& findTest(const std::string& name) {
    for (const Argument& a : kTests()) {
        if (name == a.name) {
            return a;
        }
    }
    static const Argument dummy{"", "", false};
    return dummy;
}

static bool startsWith(const std::string& s, const std::string& prefix) {
    return s.rfind(prefix, 0) == 0;
}

CTS_TEST(g, "invalid")
    .desc("Validates that add and subtract are valid if the matrix types match")
    .params([](ParamsBuilder u) {
        return u.combine("op", operatorNames())
            .combine("rhs", {"ai", "mat2x3f()", "mat2x3h()"})
            .combine("test", testNames());
    })
    .fn([](ShaderValidationTest& t) {
        const Argument& test = findTest(t.param<std::string>("test"));
        const std::string rhsParam = t.param<std::string>("rhs");
        const std::string lhs = test.src;
        const std::string rhs =
            rhsParam == "ai" ? std::string("mat2x4(0, 0, 0, 0, 0, 0, 0, 0)") : rhsParam;

        const std::string enables =
            (test.is_f16 || startsWith(rhsParam, "mat2x3h(")) ? "enable f16;" : "";
        const std::string code =
            "\n" + enables +
            "\n@group(0) @binding(0) var t : texture_2d<f32>;"
            "\n@group(0) @binding(1) var s : sampler;"
            "\n@group(0) @binding(2) var<storage, read_write> a : atomic<i32>;"
            "\n"
            "\nstruct S { u : u32 }"
            "\n"
            "\nvar<private> arr : array<u32, 4>;"
            "\nvar<private> str : S;"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = " + lhs + " " + findOperator(t.param<std::string>("op")).op + " " + rhs + ";"
            "\n}"
            "\n";

        t.expectCompileResult(lhs == rhs, code);
    });

CTS_TEST(g, "with_abstract")
    .desc("Validates that add and subtract are valid if when done against an abstract")
    .params([](ParamsBuilder u) {
        return u.combine("op", operatorNames())
            .combine("rhs", {"mat2x3f()", "mat2x3h()"})
            .combine("swap", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string rhsParam = t.param<std::string>("rhs");
        std::string lhs = "mat2x3(0, 0, 0, 0, 0, 0)";
        std::string rhs = rhsParam;

        if (t.param<bool>("swap")) {
            std::string a = lhs;
            lhs = rhs;
            rhs = a;
        }

        const std::string enables = startsWith(rhsParam, "mat2x3h(") ? "enable f16;" : "";
        const std::string code =
            "\n" + enables +
            "\n@group(0) @binding(0) var t : texture_2d<f32>;"
            "\n@group(0) @binding(1) var s : sampler;"
            "\n@group(0) @binding(2) var<storage, read_write> a : atomic<i32>;"
            "\n"
            "\nstruct S { u : u32 }"
            "\n"
            "\nvar<private> arr : array<u32, 4>;"
            "\nvar<private> str : S;"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = " + lhs + " " + findOperator(t.param<std::string>("op")).op + " " + rhs + ";"
            "\n}"
            "\n";

        t.expectCompileResult(true, code);
    });

// ---- Shared overflow/underflow body -----------------------------------------
// `rhsMax`/`lhsHalf` are the precise decimal spellings; `op` is '+' or '-';
// `prefixEnable` is "enable f16;\n" for f16 variants; `suffix` is the matrix
// element type suffix ("f", "h", or "" for abstract).
static void runOverUnderflow(ShaderValidationTest& t,
                             const char* op,
                             const char* enables,
                             const char* matSuffix,
                             const char* lhsHalf,
                             const char* rhsMaxSpelling) {
    const int64_t c = t.param<int64_t>("c");
    const int64_t r = t.param<int64_t>("r");
    const double rhsVal = t.param<double>("rhs");
    const bool rhsIsOne = rhsVal == 1.0;
    const std::string rhsStr = rhsIsOne ? std::string("1") : std::string(rhsMaxSpelling);

    std::string lhs = "mat" + std::to_string(c) + "x" + std::to_string(r) + matSuffix + "(";
    std::string rhs = "mat" + std::to_string(c) + "x" + std::to_string(r) + matSuffix + "(";
    for (int64_t i = 0; i < c; i++) {
        for (int64_t k = 0; k < r; k++) {
            lhs += std::string(lhsHalf) + ",";
            rhs += rhsStr + ",";
        }
    }
    rhs += ")";
    lhs += ")";

    const std::string code =
        std::string("\n") + enables +
        "@compute @workgroup_size(1)"
        "\nfn main() {"
        "\n  const foo = " + lhs + " " + op + " " + rhs + ";"
        "\n}"
        "\n";

    t.expectCompileResult(rhsIsOne, code);
}

static ParamsBuilder overUnderflowParams(ParamsBuilder u, double maxVal) {
    return u.combine("rhs", std::vector<Value>{Value(maxVal), Value(1.0)})
        .combine("c", std::vector<Value>{Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))})
        .combine("r", std::vector<Value>{Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))});
}

CTS_TEST(g, "overflow_f32")
    .desc("Validates that f32 add overflows in shader creation")
    .params([](ParamsBuilder u) { return overUnderflowParams(u, 3.4028234663852886e+38); })
    .fn([](ShaderValidationTest& t) {
        runOverUnderflow(t, "+", "", "f", kF32MaxHalf, kF32Max);
    });

CTS_TEST(g, "underflow_f32")
    .desc("Validates that f32 add underflows in shader creation")
    .params([](ParamsBuilder u) { return overUnderflowParams(u, 3.4028234663852886e+38); })
    .fn([](ShaderValidationTest& t) {
        runOverUnderflow(t, "-", "", "f", kF32NegMinHalf, kF32Max);
    });

CTS_TEST(g, "overflow_f16")
    .desc("Validates that f16 add overflows in shader creation")
    .params([](ParamsBuilder u) { return overUnderflowParams(u, 65504.0); })
    .fn([](ShaderValidationTest& t) {
        runOverUnderflow(t, "+", "enable f16;\n", "h", kF16MaxHalf, kF16Max);
    });

CTS_TEST(g, "underflow_f16")
    .desc("Validates that f16 add underflows in shader creation")
    .params([](ParamsBuilder u) { return overUnderflowParams(u, 65504.0); })
    .fn([](ShaderValidationTest& t) {
        runOverUnderflow(t, "-", "enable f16;\n", "h", kF16NegMinHalf, kF16Max);
    });

CTS_TEST(g, "overflow_abstract")
    .desc("Validates that abstract add overflows in shader creation")
    .params([](ParamsBuilder u) { return overUnderflowParams(u, 1.7976931348623157e+308); })
    .fn([](ShaderValidationTest& t) {
        runOverUnderflow(t, "+", "", "", kF64MaxHalf, kF64Max);
    });

CTS_TEST(g, "underflow_abstract")
    .desc("Validates that abstract add underflows in shader creation")
    .params([](ParamsBuilder u) { return overUnderflowParams(u, 1.7976931348623157e+308); })
    .fn([](ShaderValidationTest& t) {
        runOverUnderflow(t, "-", "", "", kF64NegMinHalf, kF64Max);
    });

}  // namespace
