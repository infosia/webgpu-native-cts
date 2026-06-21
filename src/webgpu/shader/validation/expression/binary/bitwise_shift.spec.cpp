// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/binary/bitwise_shift.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kAllScalarsAndVectors is a Type table keyed by Type.toString() (vectors
// stringify as `vecN<elementType>`); the lhs/rhs params carry those key strings
// and the Type is reconstructed via typeByName(). The shift-case lists
// (kLeftShiftCases/kRightShiftCases) are combined directly as parameter records
// (lhs/rhs/pass columns). The `vectorize` param is `undefined` (Value::undef())
// or 2/3/4. kInvalidTypes is keyed by a scalar `type` name. See binary_types.h.
//
// Binary-literal lhs values precomputed (JS interprets binary literals as
// unsigned; `signed()` reinterprets to a signed i32). See the inline comments.

#include <string>
#include <variant>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/expression/binary/binary_types.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;
namespace bt = cts::shader_validation::binary;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,binary,bitwise_shift",
    "Validation tests for the bitwise shift binary expression operations");

// vectorize(v, size): vecN(v) if size != undefined, else v.
static std::string vectorize(const std::string& v, const Value* size) {
    if (size != nullptr && !std::holds_alternative<Value::Undefined>(size->data())) {
        const int64_t s = valueAs<int64_t>(*size);
        return "vec" + std::to_string(s) + "(" + v + ")";
    }
    return v;
}

// ---- scalar_vector ----------------------------------------------------------
CTS_TEST(g, "scalar_vector")
    .desc("Validates that scalar and vector expressions are only accepted when the LHS is an "
          "integer and the RHS is abstract or unsigned.")
    .params([](ParamsBuilder u) {
        return u.combine("lhs", bt::typeNames(bt::kAllScalarsAndVectors()))
            .combine("rhs", bt::typeNamesNoVec34(bt::kAllScalarsAndVectors()))
            .combine("compound_assignment", {Value(false), Value(true)})
            .beginSubcases()
            .combine("op", {"<<", ">>"});
    })
    .fn([](ShaderValidationTest& t) {
        const bt::Type lhs = bt::typeByName(t.param<std::string>("lhs"));
        const bt::Type rhs = bt::typeByName(t.param<std::string>("rhs"));
        const bt::Type lhsElement = bt::scalarTypeOf(lhs);
        const bt::Type rhsElement = bt::scalarTypeOf(rhs);
        const bool hasF16 =
            lhsElement.kind == bt::ScalarKind::F16 || rhsElement.kind == bt::ScalarKind::F16;
        const std::string op = t.param<std::string>("op");
        const bool compound = t.param<bool>("compound_assignment");

        std::string code;
        if (compound) {
            code = std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nfn f() {\n  var foo = " +
                   bt::createWgsl(lhs, 0) + ";\n  foo " + op + "= " + bt::createWgsl(rhs, 0) +
                   ";\n}\n";
        } else {
            code = std::string("\n") + (hasF16 ? "enable f16;" : "") + "\nconst lhs = " +
                   bt::createWgsl(lhs, 0) + ";\nconst rhs = " + bt::createWgsl(rhs, 0) +
                   ";\nconst foo = lhs " + op + " rhs;\n";
        }

        // LHS must be an integer (abstractInt/i32/u32); RHS must be abstractInt/u32.
        const bool lhs_valid = lhsElement.kind == bt::ScalarKind::AbstractInt ||
                               lhsElement.kind == bt::ScalarKind::I32 ||
                               lhsElement.kind == bt::ScalarKind::U32;
        const bool rhs_valid = rhsElement.kind == bt::ScalarKind::AbstractInt ||
                               rhsElement.kind == bt::ScalarKind::U32;
        const bool valid =
            lhs_valid && rhs_valid && bt::numElementsOf(lhs) == bt::numElementsOf(rhs);
        t.expectCompileResult(valid, code);
    });

// ---- invalid_types ----------------------------------------------------------
struct InvalidType {
    const char* name;
    const char* expr;
};

static const std::vector<InvalidType>& kInvalidTypes() {
    static const std::vector<InvalidType> v = {
        {"mat2x2f", "m"}, {"array", "arr"},  {"ptr", "(&u)"}, {"atomic", "a"},
        {"texture", "t"}, {"sampler", "s"},  {"struct", "str"},
    };
    return v;
}

static std::vector<Value> invalidTypeNames() {
    std::vector<Value> values;
    for (const InvalidType& it : kInvalidTypes()) {
        values.emplace_back(std::string(it.name));
    }
    return values;
}

static std::string invalidControl(const std::string& name, const std::string& e) {
    if (name == "mat2x2f") return "u32(" + e + "[0][0])";
    if (name == "array") return e + "[0]";
    if (name == "ptr") return "*" + e;
    if (name == "atomic") return "atomicLoad(&" + e + ")";
    if (name == "texture") return "u32(textureLoad(" + e + ", vec2(), 0).x)";
    if (name == "sampler") return "u32(textureSampleLevel(t, " + e + ", vec2(), 0).x)";
    if (name == "struct") return e + ".u";
    return e;
}

static const InvalidType& findInvalidType(const std::string& name) {
    for (const InvalidType& it : kInvalidTypes()) {
        if (name == it.name) {
            return it;
        }
    }
    static const InvalidType dummy{"", ""};
    return dummy;
}

CTS_TEST(g, "invalid_types")
    .desc("Validates that expressions are never accepted for non-scalar and non-vector types.")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"<<", ">>"})
            .combine("type", invalidTypeNames())
            .combine("control", {Value(true), Value(false)})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const InvalidType& it = findInvalidType(t.param<std::string>("type"));
        const bool control = t.param<bool>("control");
        const std::string op = t.param<std::string>("op");
        const std::string expr = control ? invalidControl(it.name, it.expr) : std::string(it.expr);
        const std::string code =
            "\n@group(0) @binding(0) var t : texture_2d<f32>;"
            "\n@group(0) @binding(1) var s : sampler;"
            "\n@group(0) @binding(2) var<storage, read_write> a : atomic<u32>;"
            "\n\nstruct S { u : u32 }"
            "\n\nvar<private> u : u32;"
            "\nvar<private> m : mat2x2f;"
            "\nvar<private> arr : array<u32, 4>;"
            "\nvar<private> str : S;"
            "\n\n@compute @workgroup_size(1)\nfn main() {\n  let foo = " +
            expr + " " + op + " " + expr + ";\n}\n";
        t.expectCompileResult(control, code);
    });

// ---- shift_left_concrete / shift_right_concrete -----------------------------
struct ShiftCase {
    const char* lhs;
    const char* rhs;
    bool pass;
};

// kLeftShiftCases. Binary literals precomputed:
//   0b0100..(30 zeros) = 1073741824, 0b0111..(31 ones) = 2147483647,
//   0b0000..1 = 1, 0b1100.. = 3221225472, 0b1111..(32 ones) = 4294967295,
//   signed(0b1100..) = -1073741824, signed(0b1111..) = -1,
//   0b0100..(62 zeros, 64-bit) = 4611686018427387904.
static const std::vector<ShiftCase>& kLeftShiftCases() {
    static const std::vector<ShiftCase> v = {
        {"0u", "31u", true},
        {"0u", "32u", false},
        {"0u", "33u", false},
        {"0u", "1000u", false},
        {"0u", "0xFFFFFFFFu", false},

        {"0i", "31u", true},
        {"0i", "32u", false},
        {"0i", "33u", false},
        {"0i", "1000u", false},
        {"0i", "0xFFFFFFFFu", false},

        // Signed overflow (sign change)
        {"1073741824i", "1u", false},
        {"2147483647i", "1u", false},
        {"1i", "31u", false},
        // Same cases should pass if lhs is unsigned
        {"1073741824u", "1u", true},
        {"2147483647u", "1u", true},
        {"1u", "31u", true},

        // Unsigned overflow
        {"3221225472u", "1u", false},
        {"4294967295u", "1u", false},
        {"4294967295u", "31u", false},
        // Same cases should pass if lhs is signed
        {"-1073741824i", "1u", true},
        {"-1i", "1u", true},
        {"-1i", "31u", true},

        // Shift by negative is an error
        {"1", "-1", false},
        {"1i", "-1", false},
        {"1u", "-1", false},

        // Signed overflow (sign change) for abstract
        {"1", "63", false},
        {"2", "62", false},
        {"4611686018427387904", "1u", false},

        // Negative operand overflow for abstract
        {"-1", "63", true},
        {"-1", "64", false},
    };
    return v;
}

// kRightShiftCases.
static const std::vector<ShiftCase>& kRightShiftCases() {
    static const std::vector<ShiftCase> v = {
        {"0u", "31u", true},
        {"0u", "32u", false},
        {"0u", "33u", false},
        {"0u", "1000u", false},
        {"0u", "0xFFFFFFFFu", false},

        {"0i", "31u", true},
        {"0i", "32u", false},
        {"0i", "33u", false},
        {"0i", "1000u", false},
        {"0i", "0xFFFFFFFFu", false},

        // Shift by negative is an error
        {"1", "-1", false},
        {"1i", "-1", false},
        {"1u", "-1", false},

        // Abstract shifts are permitted to underflow
        {"1", "64", true},
        {"-1", "64", true},
    };
    return v;
}

// The `case` param is keyed by its index (the upstream record is a struct; we
// reconstruct lhs/rhs/pass by index). Subcase query carries `case=N`.
static std::vector<ParamRecord> shiftCaseRecords(const std::vector<ShiftCase>& cases) {
    std::vector<ParamRecord> recs;
    for (size_t i = 0; i < cases.size(); ++i) {
        ParamRecord r;
        r.emplace_back("case", Value(static_cast<int64_t>(i)));
        recs.push_back(std::move(r));
    }
    return recs;
}

static void shiftConcreteBody(ShaderValidationTest& t, const std::vector<ShiftCase>& cases,
                              const char* opStr) {
    const int64_t idx = t.param<int64_t>("case");
    const ShiftCase& c = cases[static_cast<size_t>(idx)];
    const Value* vec = findParam(t.params(), "vectorize");
    const std::string code = std::string("\n@compute @workgroup_size(1)\nfn main() {\n    const r = ") +
                             vectorize(c.lhs, vec) + " " + opStr + " " + vectorize(c.rhs, vec) +
                             ";\n}\n    ";
    t.expectCompileResult(c.pass, code);
}

CTS_TEST(g, "shift_left_concrete")
    .desc("Tests validation of binary left shift (including abstract, despite the test name)")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(shiftCaseRecords(kLeftShiftCases()))
            .combine("vectorize", {Value::undef(), Value(2), Value(3), Value(4)});
    })
    .fn([](ShaderValidationTest& t) { shiftConcreteBody(t, kLeftShiftCases(), "<<"); });

CTS_TEST(g, "shift_right_concrete")
    .desc("Tests validation of binary right shift (including abstract, despite the test name)")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(shiftCaseRecords(kRightShiftCases()))
            .combine("vectorize", {Value::undef(), Value(2), Value(3), Value(4)});
    })
    .fn([](ShaderValidationTest& t) { shiftConcreteBody(t, kRightShiftCases(), ">>"); });

// ---- shift_left_abstract / shift_right_abstract -----------------------------
CTS_TEST(g, "shift_left_abstract")
    .desc("Validates that the result when the LHS is abstract is also abstract")
    .fn([](ShaderValidationTest& t) {
        const std::string wgsl =
            "\n    const lhs = 0xfffff0000; // too large for 32 bits"
            "\n    const res = lhs << 4u;"
            "\n    const_assert res == 0xfffff00000;";
        t.expectCompileResult(true, wgsl);
    });

CTS_TEST(g, "shift_right_abstract")
    .desc("Validates that the result when the LHS is abstract is also abstract")
    .fn([](ShaderValidationTest& t) {
        const std::string wgsl =
            "\n    const lhs = 0xfffff0000; // too large for 32 bits"
            "\n    const res = lhs >> 1u;"
            "\n    const_assert res == 0x7ffff8000;";
        t.expectCompileResult(true, wgsl);
    });

// ---- partial_eval_errors ----------------------------------------------------
CTS_TEST(g, "partial_eval_errors")
    .desc("Tests partial evaluation errors for left and right shift")
    .params([](ParamsBuilder u) {
        return u.combine("op", {"<<", ">>"})
            .combine("type", {"i32", "u32"})
            .combine("lhs", {"const", "var"})
            .combine("vectorize", {Value::undef(), Value(2), Value(3), Value(4)})
            .beginSubcases()
            .combine("stage", {"shader", "pipeline"})
            .combine("value", {Value(31), Value(32), Value(33), Value(64)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string op = t.param<std::string>("op");
        const std::string type = t.param<std::string>("type");
        const std::string lhsKind = t.param<std::string>("lhs");
        const std::string stage = t.param<std::string>("stage");
        const int64_t value = t.param<int64_t>("value");
        const Value* vec = findParam(t.params(), "vectorize");

        std::string rhs = "o";
        if (stage == "shader") {
            rhs = std::to_string(value) + "u";  // u32.create(value).wgsl()
        }

        std::string vecType = type;
        if (vec != nullptr && !std::holds_alternative<Value::Undefined>(vec->data())) {
            vecType = "vec" + std::to_string(valueAs<int64_t>(*vec)) + "<" + type + ">";
        }

        const std::string wgsl = "\noverride o = 0u;\nfn foo() -> " + vecType + " {\n  " + lhsKind +
                                 " v : " + vecType + " = " + vectorize("0", vec) + ";\n  return v " +
                                 op + " " + vectorize(rhs, vec) + ";\n}";

        const bool expect = value < 32;
        if (stage == "shader") {
            t.expectCompileResult(expect, wgsl);
        } else {
            ShaderValidationTest::PipelineArgs args;
            args.expectedResult = expect;
            args.code = wgsl;
            args.constants["o"] = static_cast<double>(value);
            args.reference = {"o", "foo()"};
            t.expectPipelineResult(args);
        }
    });

}  // namespace
