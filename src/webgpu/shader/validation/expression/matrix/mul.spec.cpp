// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/matrix/mul.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// The overflow tests combine over a numeric `rhs`/`lhs` param whose two values are a
// floating-point extreme (kValue.<f32|f16|f64>.positive.max) and the integer 1. Upstream
// passes them as JS numbers, so the case-query name is the JS Number.toString() spelling
// and the same spelling is embedded into the WGSL. We use double-valued params so the case
// names render unquoted (matching upstream), and embed the exact JS decimal spellings
// precomputed below. `... === 1` becomes `val == 1.0`, `... !== max` becomes `val != max`.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,matrix,mul",
    "Validation tests for matrix multiplication expressions.");

// ---- kValue spellings (exact JS Number.toString() of the IEEE values) -------
static const char* kF32Max = "3.4028234663852886e+38";      // f32.positive.max
static const double kF32MaxD = 3.4028234663852886e+38;
static const char* kF16Max = "65504";                       // f16.positive.max
static const double kF16MaxD = 65504.0;
static const char* kF64Max = "1.7976931348623157e+308";     // f64.positive.max
static const double kF64MaxD = 1.7976931348623157e+308;

// kTests (object key order preserved). `src` is the WGSL value spelling.
struct Argument {
    const char* name;
    const char* src;
    bool is_f16;
};
static const std::vector<Argument>& kTests() {
    static const std::vector<Argument> v = {
        {"match", "mat3x2f()", false},
        {"bool", "false", false},
        {"vec", "vec4f()", false},
        {"i32", "1i", false},
        {"u32", "1u", false},
        {"texture", "t", false},
        {"sampler", "s", false},
        {"atomic", "a", false},
        {"struct", "str", false},
        {"array", "arr", false},
        {"matf_no_match", "mat4x4f()", false},
    };
    return v;
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
    .desc("Validates types for matrix multiplication")
    .params([](ParamsBuilder u) {
        return u.combine("rhs", {"ai", "mat2x3f()", "mat2x3h()"})
            .combine("test", testNames())
            .combine("swap", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const Argument& test = findTest(t.param<std::string>("test"));
        const std::string rhsParam = t.param<std::string>("rhs");
        std::string lhs = test.src;
        std::string rhs = rhsParam == "ai" ? std::string("mat3x2(0, 0, 0, 0, 0, 0)") : rhsParam;

        if (t.param<bool>("swap")) {
            std::string a = lhs;
            lhs = rhs;
            rhs = a;
        }

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
            "\nvar<private> arr : array<i32, 4>;"
            "\nvar<private> str : S;"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = " + lhs + " * " + rhs + ";"
            "\n}"
            "\n";

        const bool pass =
            std::string(test.src) == "mat3x2f()" && rhsParam == "mat2x3f()";
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "f16_and_f32_matrix")
    .desc("Validates that f16 multiplied by an f32 matrix is an error.")
    .params([](ParamsBuilder u) {
        return u.combine("rhs", {"mat2x3f()", "mat2x3h()"}).combine("swap", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string rhsParam = t.param<std::string>("rhs");
        std::string lhs = "1h";
        std::string rhs = rhsParam;
        if (t.param<bool>("swap")) {
            std::string a = lhs;
            lhs = rhs;
            rhs = a;
        }

        const std::string code =
            "\nenable f16;"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = " + lhs + " * " + rhs + ";"
            "\n}"
            "\n";

        const bool pass = rhsParam == "mat2x3h()";
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "f32_and_f16_matrix")
    .desc("Validates that f32 multiplied by an f16 matrix is an error")
    .params([](ParamsBuilder u) {
        return u.combine("rhs", {"mat2x3f()", "mat2x3h()"}).combine("swap", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string rhsParam = t.param<std::string>("rhs");
        std::string lhs = "1f";
        std::string rhs = rhsParam;
        if (t.param<bool>("swap")) {
            std::string a = lhs;
            lhs = rhs;
            rhs = a;
        }

        const std::string code =
            "\nenable f16;"
            "\n"
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = " + lhs + " * " + rhs + ";"
            "\n}"
            "\n";

        const bool pass = rhsParam == "mat2x3f()";
        t.expectCompileResult(pass, code);
    });

// ---- mat_by_mat / mat_by_vec / vec_by_mat dimension params ------------------
static std::vector<Value> dimValues() {
    return std::vector<Value>{Value(int64_t(2)), Value(int64_t(3)), Value(int64_t(4))};
}
static std::vector<Value> tyValues() {
    return std::vector<Value>{Value("f"), Value("h"), Value("")};
}

CTS_TEST(g, "mat_by_mat")
    .desc("Validates that mat * mat is only valid for kxR * Cxk.")
    .params([](ParamsBuilder u) {
        return u.combine("ty1", tyValues())
            .combine("ty2", tyValues())
            .beginSubcases()
            .combine("c1", dimValues())
            .combine("r1", dimValues())
            .combine("c2", dimValues())
            .combine("r2", dimValues());
    })
    .fn([](ShaderValidationTest& t) {
        const int64_t c1 = t.param<int64_t>("c1");
        const int64_t r1 = t.param<int64_t>("r1");
        const int64_t c2 = t.param<int64_t>("c2");
        const int64_t r2 = t.param<int64_t>("r2");
        const std::string ty1 = t.param<std::string>("ty1");
        const std::string ty2 = t.param<std::string>("ty2");

        std::string t1_val;
        if (ty1.empty()) {
            for (int64_t i = 0; i < c1; i++) {
                for (int64_t k = 0; k < r1; k++) {
                    t1_val += "0,";
                }
            }
        }
        std::string t2_val;
        if (ty2.empty()) {
            for (int64_t i = 0; i < c2; i++) {
                for (int64_t k = 0; k < r2; k++) {
                    t2_val += "0,";
                }
            }
        }

        const std::string enables = (ty1 == "h" || ty2 == "h") ? "enable f16;" : "";
        const std::string code =
            "\n" + enables +
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = mat" + std::to_string(c1) + "x" + std::to_string(r1) + ty1 + "(" + t1_val +
            ") * mat" + std::to_string(c2) + "x" + std::to_string(r2) + ty2 + "(" + t2_val + ");"
            "\n}"
            "\n";

        const bool pass = c1 == r2 && (ty1 == ty2 || ty1.empty() || ty2.empty());
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "mat_by_vec")
    .desc("Validates that mat * vec is only valid for CxR * C.")
    .params([](ParamsBuilder u) {
        return u.combine("ty1", tyValues())
            .combine("ty2", tyValues())
            .beginSubcases()
            .combine("c1", dimValues())
            .combine("r1", dimValues())
            .combine("v1", dimValues());
    })
    .fn([](ShaderValidationTest& t) {
        const int64_t c1 = t.param<int64_t>("c1");
        const int64_t r1 = t.param<int64_t>("r1");
        const int64_t v1 = t.param<int64_t>("v1");
        const std::string ty1 = t.param<std::string>("ty1");
        const std::string ty2 = t.param<std::string>("ty2");

        std::string t1_val;
        if (ty1.empty()) {
            for (int64_t i = 0; i < c1; i++) {
                for (int64_t k = 0; k < r1; k++) {
                    t1_val += "0,";
                }
            }
        }
        std::string t2_val;
        if (ty2.empty()) {
            for (int64_t i = 0; i < v1; i++) {
                t2_val += "0,";
            }
        }

        const std::string enables = (ty1 == "h" || ty2 == "h") ? "enable f16;" : "";
        const std::string code =
            "\n" + enables +
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = mat" + std::to_string(c1) + "x" + std::to_string(r1) + ty1 + "(" + t1_val +
            ") * vec" + std::to_string(v1) + ty2 + "(" + t2_val + ");"
            "\n}"
            "\n";

        const bool pass = c1 == v1 && (ty1 == ty2 || ty1.empty() || ty2.empty());
        t.expectCompileResult(pass, code);
    });

CTS_TEST(g, "vec_by_mat")
    .desc("Validates that vec * mat is only valid for R * CxR.")
    .params([](ParamsBuilder u) {
        return u.combine("ty1", tyValues())
            .combine("ty2", tyValues())
            .beginSubcases()
            .combine("c1", dimValues())
            .combine("r1", dimValues())
            .combine("v1", dimValues());
    })
    .fn([](ShaderValidationTest& t) {
        const int64_t c1 = t.param<int64_t>("c1");
        const int64_t r1 = t.param<int64_t>("r1");
        const int64_t v1 = t.param<int64_t>("v1");
        const std::string ty1 = t.param<std::string>("ty1");
        const std::string ty2 = t.param<std::string>("ty2");

        std::string t1_val;
        if (ty1.empty()) {
            for (int64_t i = 0; i < c1; i++) {
                for (int64_t k = 0; k < r1; k++) {
                    t1_val += "0,";
                }
            }
        }
        std::string t2_val;
        if (ty2.empty()) {
            for (int64_t i = 0; i < v1; i++) {
                t2_val += "0,";
            }
        }

        const std::string enables = (ty1 == "h" || ty2 == "h") ? "enable f16;" : "";
        const std::string code =
            "\n" + enables +
            "\n@compute @workgroup_size(1)"
            "\nfn main() {"
            "\n  let foo = vec" + std::to_string(v1) + ty2 + "(" + t2_val + ") * mat" +
            std::to_string(c1) + "x" + std::to_string(r1) + ty1 + "(" + t1_val + ");"
            "\n}"
            "\n";

        const bool pass = r1 == v1 && (ty1 == ty2 || ty1.empty() || ty2.empty());
        t.expectCompileResult(pass, code);
    });

// ---- Overflow tests ---------------------------------------------------------
static ParamsBuilder crParams(ParamsBuilder u, const std::string& key, double maxVal) {
    return u.combine(key, std::vector<Value>{Value(maxVal), Value(1.0)})
        .combine("c", dimValues())
        .combine("r", dimValues());
}

// overflow_*_scalar : lhs = matCxR<sfx>(max,max,...); rhs = scalar (max or 1).
static void runOverflowScalar(ShaderValidationTest& t,
                              const char* enables,
                              const char* sfx,
                              const char* maxSpelling) {
    const int64_t c = t.param<int64_t>("c");
    const int64_t r = t.param<int64_t>("r");
    const double rhsVal = t.param<double>("rhs");
    const bool rhsIsOne = rhsVal == 1.0;

    std::string lhs = "mat" + std::to_string(c) + "x" + std::to_string(r) + sfx + "(";
    for (int64_t i = 0; i < c; i++) {
        for (int64_t k = 0; k < r; k++) {
            lhs += std::string(maxSpelling) + ",";
        }
    }
    lhs += ")";
    const std::string rhs = rhsIsOne ? std::string("1") : std::string(maxSpelling);

    const std::string code =
        std::string("\n") + enables +
        "@compute @workgroup_size(1)"
        "\nfn main() {"
        "\n  const foo = " + lhs + " * " + rhs + ";"
        "\n}"
        "\n";
    t.expectCompileResult(rhsIsOne, code);
}

CTS_TEST(g, "overflow_scalar_f32")
    .desc("Validates that f32 scalar multiplication overflows in shader creation")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF32MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowScalar(t, "", "f", kF32Max); });

CTS_TEST(g, "overflow_scalar_f16")
    .desc("Validates that f16 scalar multiplication overflows in shader creation")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF16MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowScalar(t, "enable f16;\n", "h", kF16Max); });

CTS_TEST(g, "overflow_scalar_abstract")
    .desc("Validates that abstract scalar multiplication overflows in shader creation")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF64MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowScalar(t, "", "", kF64Max); });

// overflow_vec_* : lhs = matCxR<sfx> with row 0 = max, rest 0; rhs = vecC<sfx>(rhs).
// Pass iff rhs === 1 (f32/abstract) or rhs !== max (f16, where rhs is divided by c).
static void runOverflowVec(ShaderValidationTest& t,
                           const char* enables,
                           const char* sfx,
                           const char* maxSpelling,
                           bool f16Divide) {
    const int64_t c = t.param<int64_t>("c");
    const int64_t r = t.param<int64_t>("r");
    const double rhsVal = t.param<double>("rhs");
    const bool rhsIsOne = rhsVal == 1.0;

    std::string lhs = "mat" + std::to_string(c) + "x" + std::to_string(r) + sfx + "(";
    for (int64_t i = 0; i < c; i++) {
        for (int64_t k = 0; k < r; k++) {
            if (i == 0) {
                lhs += std::string(maxSpelling) + ",";
            } else {
                lhs += "0,";
            }
        }
    }
    lhs += ")";

    std::string rhs;
    if (f16Divide) {
        // `vec${c}h(${rhs}/${c})` — embed exact spelling of rhs and the divisor.
        const std::string rhsSpelling = rhsIsOne ? std::string("1") : std::string(maxSpelling);
        rhs = "vec" + std::to_string(c) + sfx + "(" + rhsSpelling + "/" + std::to_string(c) + ")";
    } else {
        const std::string rhsSpelling = rhsIsOne ? std::string("1") : std::string(maxSpelling);
        rhs = "vec" + std::to_string(c) + sfx + "(" + rhsSpelling + ")";
    }

    const std::string code =
        std::string("\n") + enables +
        "@compute @workgroup_size(1)"
        "\nfn main() {"
        "\n  const foo = " + lhs + " * " + rhs + ";"
        "\n}"
        "\n";

    if (f16Divide) {
        // expectCompileResult(t.params.rhs !== kValue.f16.positive.max)
        t.expectCompileResult(rhsVal != kF16MaxD, code);
    } else {
        t.expectCompileResult(rhsIsOne, code);
    }
}

CTS_TEST(g, "overflow_vec_f32")
    .desc("Validates that f32 vector multiplication overflows in shader creation. The overflow "
          "happens when multiplying the values.")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF32MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowVec(t, "", "f", kF32Max, false); });

CTS_TEST(g, "overflow_vec_f16")
    .desc("Validates that f16 vector multiplication overflows in shader creation. Overflow occurs "
          "when multiplying.")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF16MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowVec(t, "enable f16;\n", "h", kF16Max, true); });

CTS_TEST(g, "overflow_vec_abstract")
    .desc("Validates that abstract vector multiplication overflows in shader creation. Overflow "
          "occurs when multiplying.")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF64MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowVec(t, "", "", kF64Max, false); });

// overflow_vec_*_internal : lhs = matCxR<sfx>(lhs,...); rhs = vecC<sfx>(1). Pass iff lhs === 1.
static void runOverflowVecInternal(ShaderValidationTest& t,
                                   const char* enables,
                                   const char* sfx,
                                   const char* maxSpelling) {
    const int64_t c = t.param<int64_t>("c");
    const int64_t r = t.param<int64_t>("r");
    const double lhsVal = t.param<double>("lhs");
    const bool lhsIsOne = lhsVal == 1.0;
    const std::string lhsSpelling = lhsIsOne ? std::string("1") : std::string(maxSpelling);

    std::string lhs = "mat" + std::to_string(c) + "x" + std::to_string(r) + sfx + "(";
    for (int64_t i = 0; i < c; i++) {
        for (int64_t k = 0; k < r; k++) {
            lhs += lhsSpelling + ",";
        }
    }
    lhs += ")";
    const std::string rhs = "vec" + std::to_string(c) + sfx + "(1)";

    const std::string code =
        std::string("\n") + enables +
        "@compute @workgroup_size(1)"
        "\nfn main() {"
        "\n  const foo = " + lhs + " * " + rhs + ";"
        "\n}"
        "\n";
    t.expectCompileResult(lhsIsOne, code);
}

CTS_TEST(g, "overflow_vec_f32_internal")
    .desc("Validates that f32 vector multiplication overflows in shader creation. The overflow "
          "happens while summing the values.")
    .params([](ParamsBuilder u) { return crParams(u, "lhs", kF32MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowVecInternal(t, "", "f", kF32Max); });

CTS_TEST(g, "overflow_vec_f16_internal")
    .desc("Validates that f16 vector multiplication overflows in shader creation. Overflow occurs "
          "when summing")
    .params([](ParamsBuilder u) { return crParams(u, "lhs", kF16MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowVecInternal(t, "enable f16;\n", "h", kF16Max); });

CTS_TEST(g, "overflow_vec_abstract_internal")
    .desc("Validates that abstract vector multiplication overflows in shader creation. Overflow "
          "occurs when summing.")
    .params([](ParamsBuilder u) { return crParams(u, "lhs", kF64MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowVecInternal(t, "", "", kF64Max); });

// overflow_mat_* : lhs = matCxR<sfx> row0=max rest 0; rhs = matRxC<sfx> row0=rhs rest 0.
static void runOverflowMat(ShaderValidationTest& t,
                           const char* enables,
                           const char* sfx,
                           const char* maxSpelling) {
    const int64_t c = t.param<int64_t>("c");
    const int64_t r = t.param<int64_t>("r");
    const double rhsVal = t.param<double>("rhs");
    const bool rhsIsOne = rhsVal == 1.0;
    const std::string rhsSpelling = rhsIsOne ? std::string("1") : std::string(maxSpelling);

    std::string lhs = "mat" + std::to_string(c) + "x" + std::to_string(r) + sfx + "(";
    std::string rhs = "mat" + std::to_string(r) + "x" + std::to_string(c) + sfx + "(";
    for (int64_t i = 0; i < c; i++) {
        for (int64_t k = 0; k < r; k++) {
            if (i == 0) {
                lhs += std::string(maxSpelling) + ",";
                rhs += rhsSpelling + ",";
            } else {
                lhs += "0,";
                rhs += "0,";
            }
        }
    }
    rhs += ")";
    lhs += ")";

    const std::string code =
        std::string("\n") + enables +
        "@compute @workgroup_size(1)"
        "\nfn main() {"
        "\n  const foo = " + lhs + " * " + rhs + ";"
        "\n}"
        "\n";
    t.expectCompileResult(rhsIsOne, code);
}

CTS_TEST(g, "overflow_mat_f32")
    .desc("Validates that f32 matrix multiplication overflows in shader creation. Overflows when "
          "multiplying the values")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF32MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowMat(t, "", "f", kF32Max); });

CTS_TEST(g, "overflow_mat_f16")
    .desc("Validates that f16 matrix multiplication overflows in shader creation. Overflow occurs "
          "when multiplying")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF16MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowMat(t, "enable f16;\n", "h", kF16Max); });

CTS_TEST(g, "overflow_mat_abstract")
    .desc("Validates that abstract matrix multiplication overflows in shader creation. Overflow "
          "occurs when multiplying.")
    .params([](ParamsBuilder u) { return crParams(u, "rhs", kF64MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowMat(t, "", "", kF64Max); });

// overflow_mat_*_internal : lhs = matCxR<sfx>(lhs,...); rhs = matRxC<sfx>(1,...). Pass iff lhs === 1.
static void runOverflowMatInternal(ShaderValidationTest& t,
                                   const char* enables,
                                   const char* sfx,
                                   const char* maxSpelling) {
    const int64_t c = t.param<int64_t>("c");
    const int64_t r = t.param<int64_t>("r");
    const double lhsVal = t.param<double>("lhs");
    const bool lhsIsOne = lhsVal == 1.0;
    const std::string lhsSpelling = lhsIsOne ? std::string("1") : std::string(maxSpelling);

    std::string lhs = "mat" + std::to_string(c) + "x" + std::to_string(r) + sfx + "(";
    std::string rhs = "mat" + std::to_string(r) + "x" + std::to_string(c) + sfx + "(";
    for (int64_t i = 0; i < c; i++) {
        for (int64_t k = 0; k < r; k++) {
            lhs += lhsSpelling + ",";
            rhs += "1,";
        }
    }
    rhs += ")";
    lhs += ")";

    const std::string code =
        std::string("\n") + enables +
        "@compute @workgroup_size(1)"
        "\nfn main() {"
        "\n  const foo = " + lhs + " * " + rhs + ";"
        "\n}"
        "\n";
    t.expectCompileResult(lhsIsOne, code);
}

CTS_TEST(g, "overflow_mat_f32_internal")
    .desc("Validates that f32 matrix multiplication overflows in shader creation. Overflows when "
          "summing the values")
    .params([](ParamsBuilder u) { return crParams(u, "lhs", kF32MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowMatInternal(t, "", "f", kF32Max); });

CTS_TEST(g, "overflow_mat_f16_internal")
    .desc("Validates that f16 matrix multiplication overflows in shader creation. Overflow occurs "
          "when summing")
    .params([](ParamsBuilder u) { return crParams(u, "lhs", kF16MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowMatInternal(t, "enable f16;\n", "h", kF16Max); });

CTS_TEST(g, "overflow_mat_abstract_internal")
    .desc("Validates that abstract matrix multiplication overflows in shader creation. Overflow "
          "occurs when summing.")
    .params([](ParamsBuilder u) { return crParams(u, "lhs", kF64MaxD); })
    .fn([](ShaderValidationTest& t) { runOverflowMatInternal(t, "", "", kF64Max); });

}  // namespace
