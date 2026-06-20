// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/i32_arithmetic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the i32 arithmetic binary expression operations.

#include <cstdint>
#include <optional>
#include <vector>

#include "webgpu/shader/execution/expression/binary/binary_ops_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::binary_ops;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,i32_arithmetic",
    "Execution Tests for the i32 arithmetic binary expression operations");

ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder vectorizeRhsParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize_rhs", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                   Value(static_cast<int64_t>(4))});
}
ParamsBuilder vectorizeLhsParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize_lhs", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                   Value(static_cast<int64_t>(4))});
}
InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}
int rhsVec(const Fixture& t) { return static_cast<int>(t.param<int64_t>("vectorize_rhs")); }
int lhsVec(const Fixture& t) { return static_cast<int>(t.param<int64_t>("vectorize_lhs")); }

// Op functions (i32_arithmetic.cache.ts).
std::optional<int32_t> i32_add(int32_t x, int32_t y) {
    return static_cast<int32_t>(static_cast<uint32_t>(x) + static_cast<uint32_t>(y));
}
std::optional<int32_t> i32_subtract(int32_t x, int32_t y) {
    return static_cast<int32_t>(static_cast<uint32_t>(x) - static_cast<uint32_t>(y));
}
std::optional<int32_t> i32_multiply(int32_t x, int32_t y) {
    // Math.imul: 32-bit signed multiply (low 32 bits).
    return static_cast<int32_t>(static_cast<uint32_t>(x) * static_cast<uint32_t>(y));
}
std::optional<int32_t> i32_divide_non_const(int32_t x, int32_t y) {
    if (y == 0) {
        return x;
    }
    if (x == -2147483648 && y == -1) {
        return x;
    }
    return x / y;
}
std::optional<int32_t> i32_divide_const(int32_t x, int32_t y) {
    if (y == 0) {
        return std::nullopt;
    }
    if (x == -2147483648 && y == -1) {
        return std::nullopt;
    }
    return x / y;
}
std::optional<int32_t> i32_remainder_non_const(int32_t x, int32_t y) {
    if (y == 0) {
        return 0;
    }
    if (x == -2147483648 && y == -1) {
        return 0;
    }
    return x % y;
}
std::optional<int32_t> i32_remainder_const(int32_t x, int32_t y) {
    if (y == 0) {
        return std::nullopt;
    }
    if (x == -2147483648 && y == -1) {
        return std::nullopt;
    }
    return x % y;
}

const ExprType I32 = scalarType(ScalarKind::I32);

void runScalarScalar(AllFeaturesMaxLimitsGpuTest& t, const std::string& op, const I32Op& f) {
    auto cases = generateBinaryToI32Cases(sparseI32Range(), sparseI32Range(), f);
    run(t, binaryOp(op), {I32, I32}, I32, cfgInputSource(t), cfgVectorize(t), cases);
}
void runCompoundScalar(AllFeaturesMaxLimitsGpuTest& t, const std::string& op, const I32Op& f) {
    auto cases = generateBinaryToI32Cases(sparseI32Range(), sparseI32Range(), f);
    runCompound(t, op, {I32, I32}, I32, cfgInputSource(t), cfgVectorize(t), cases);
}

CTS_TEST(testGroup, "addition").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarScalar(t, "+", i32_add);
});
CTS_TEST(testGroup, "addition_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCompoundScalar(t, "+=", i32_add);
});
CTS_TEST(testGroup, "subtraction").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarScalar(t, "-", i32_subtract);
});
CTS_TEST(testGroup, "subtraction_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCompoundScalar(t, "-=", i32_subtract);
});
CTS_TEST(testGroup, "multiplication").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarScalar(t, "*", i32_multiply);
});
CTS_TEST(testGroup, "multiplication_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCompoundScalar(t, "*=", i32_multiply);
});
CTS_TEST(testGroup, "division").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    runScalarScalar(t, "/", isConst ? I32Op(i32_divide_const) : I32Op(i32_divide_non_const));
});
CTS_TEST(testGroup, "division_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    runCompoundScalar(t, "/=", isConst ? I32Op(i32_divide_const) : I32Op(i32_divide_non_const));
});
CTS_TEST(testGroup, "remainder").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    runScalarScalar(t, "%", isConst ? I32Op(i32_remainder_const) : I32Op(i32_remainder_non_const));
});
CTS_TEST(testGroup, "remainder_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    runCompoundScalar(t, "%=", isConst ? I32Op(i32_remainder_const) : I32Op(i32_remainder_non_const));
});

// scalar-vector / vector-scalar.
CTS_TEST(testGroup, "addition_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateI32VectorBinaryToVectorCases(sparseI32Range(), vectorI32Range(n), i32_add);
    run(t, binaryOp("+"), {I32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "addition_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(), i32_add);
    run(t, binaryOp("+"), {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "addition_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(), i32_add);
    runCompound(t, "+=", {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "subtraction_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateI32VectorBinaryToVectorCases(sparseI32Range(), vectorI32Range(n), i32_subtract);
    run(t, binaryOp("-"), {I32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "subtraction_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(), i32_subtract);
    run(t, binaryOp("-"), {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "subtraction_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(), i32_subtract);
    runCompound(t, "-=", {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "multiplication_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateI32VectorBinaryToVectorCases(sparseI32Range(), vectorI32Range(n), i32_multiply);
    run(t, binaryOp("*"), {I32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "multiplication_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(), i32_multiply);
    run(t, binaryOp("*"), {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "multiplication_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(), i32_multiply);
    runCompound(t, "*=", {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "division_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateI32VectorBinaryToVectorCases(sparseI32Range(), vectorI32Range(n),
                                                      isConst ? I32Op(i32_divide_const) : I32Op(i32_divide_non_const));
    run(t, binaryOp("/"), {I32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "division_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(),
                                                      isConst ? I32Op(i32_divide_const) : I32Op(i32_divide_non_const));
    run(t, binaryOp("/"), {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "division_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(),
                                                      isConst ? I32Op(i32_divide_const) : I32Op(i32_divide_non_const));
    runCompound(t, "/=", {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "remainder_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateI32VectorBinaryToVectorCases(sparseI32Range(), vectorI32Range(n),
                                                      isConst ? I32Op(i32_remainder_const) : I32Op(i32_remainder_non_const));
    run(t, binaryOp("%"), {I32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "remainder_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(),
                                                      isConst ? I32Op(i32_remainder_const) : I32Op(i32_remainder_non_const));
    run(t, binaryOp("%"), {vt, I32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "remainder_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::I32);
    auto cases = generateVectorI32BinaryToVectorCases(vectorI32Range(n), sparseI32Range(),
                                                      isConst ? I32Op(i32_remainder_const) : I32Op(i32_remainder_non_const));
    runCompound(t, "%=", {vt, I32}, vt, cfgInputSource(t), 0, cases);
});

} // namespace
