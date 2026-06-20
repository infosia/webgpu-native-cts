// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/u32_arithmetic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the u32 arithmetic binary expression operations.

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
    "shader,execution,expression,binary,u32_arithmetic",
    "Execution Tests for the u32 arithmetic binary expression operations");

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

std::optional<uint32_t> u32_add(uint32_t x, uint32_t y) { return x + y; }
std::optional<uint32_t> u32_subtract(uint32_t x, uint32_t y) { return x - y; }
std::optional<uint32_t> u32_multiply(uint32_t x, uint32_t y) { return x * y; }
std::optional<uint32_t> u32_divide_non_const(uint32_t x, uint32_t y) {
    if (y == 0u) {
        return x;
    }
    return x / y;
}
std::optional<uint32_t> u32_divide_const(uint32_t x, uint32_t y) {
    if (y == 0u) {
        return std::nullopt;
    }
    return x / y;
}
std::optional<uint32_t> u32_remainder_non_const(uint32_t x, uint32_t y) {
    if (y == 0u) {
        return 0u;
    }
    return x % y;
}
std::optional<uint32_t> u32_remainder_const(uint32_t x, uint32_t y) {
    if (y == 0u) {
        return std::nullopt;
    }
    return x % y;
}

const ExprType U32 = scalarType(ScalarKind::U32);

void runScalarScalar(AllFeaturesMaxLimitsGpuTest& t, const std::string& op, const U32Op& f) {
    auto cases = generateBinaryToU32Cases(sparseU32Range(), sparseU32Range(), f);
    run(t, binaryOp(op), {U32, U32}, U32, cfgInputSource(t), cfgVectorize(t), cases);
}
void runCompoundScalar(AllFeaturesMaxLimitsGpuTest& t, const std::string& op, const U32Op& f) {
    auto cases = generateBinaryToU32Cases(sparseU32Range(), sparseU32Range(), f);
    runCompound(t, op, {U32, U32}, U32, cfgInputSource(t), cfgVectorize(t), cases);
}

CTS_TEST(testGroup, "addition").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarScalar(t, "+", u32_add);
});
CTS_TEST(testGroup, "addition_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCompoundScalar(t, "+=", u32_add);
});
CTS_TEST(testGroup, "subtraction").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarScalar(t, "-", u32_subtract);
});
CTS_TEST(testGroup, "subtraction_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCompoundScalar(t, "-=", u32_subtract);
});
CTS_TEST(testGroup, "multiplication").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarScalar(t, "*", u32_multiply);
});
CTS_TEST(testGroup, "multiplication_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runCompoundScalar(t, "*=", u32_multiply);
});
CTS_TEST(testGroup, "division").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    runScalarScalar(t, "/", isConst ? U32Op(u32_divide_const) : U32Op(u32_divide_non_const));
});
CTS_TEST(testGroup, "division_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    runCompoundScalar(t, "/=", isConst ? U32Op(u32_divide_const) : U32Op(u32_divide_non_const));
});
CTS_TEST(testGroup, "remainder").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    runScalarScalar(t, "%", isConst ? U32Op(u32_remainder_const) : U32Op(u32_remainder_non_const));
});
CTS_TEST(testGroup, "remainder_compound").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    runCompoundScalar(t, "%=", isConst ? U32Op(u32_remainder_const) : U32Op(u32_remainder_non_const));
});

CTS_TEST(testGroup, "addition_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateU32VectorBinaryToVectorCases(sparseU32Range(), vectorU32Range(n), u32_add);
    run(t, binaryOp("+"), {U32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "addition_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(), u32_add);
    run(t, binaryOp("+"), {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "addition_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(), u32_add);
    runCompound(t, "+=", {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "subtraction_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateU32VectorBinaryToVectorCases(sparseU32Range(), vectorU32Range(n), u32_subtract);
    run(t, binaryOp("-"), {U32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "subtraction_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(), u32_subtract);
    run(t, binaryOp("-"), {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "subtraction_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(), u32_subtract);
    runCompound(t, "-=", {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "multiplication_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateU32VectorBinaryToVectorCases(sparseU32Range(), vectorU32Range(n), u32_multiply);
    run(t, binaryOp("*"), {U32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "multiplication_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(), u32_multiply);
    run(t, binaryOp("*"), {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "multiplication_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(), u32_multiply);
    runCompound(t, "*=", {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "division_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateU32VectorBinaryToVectorCases(sparseU32Range(), vectorU32Range(n),
                                                      isConst ? U32Op(u32_divide_const) : U32Op(u32_divide_non_const));
    run(t, binaryOp("/"), {U32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "division_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(),
                                                      isConst ? U32Op(u32_divide_const) : U32Op(u32_divide_non_const));
    run(t, binaryOp("/"), {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "division_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(),
                                                      isConst ? U32Op(u32_divide_const) : U32Op(u32_divide_non_const));
    runCompound(t, "/=", {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "remainder_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = rhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateU32VectorBinaryToVectorCases(sparseU32Range(), vectorU32Range(n),
                                                      isConst ? U32Op(u32_remainder_const) : U32Op(u32_remainder_non_const));
    run(t, binaryOp("%"), {U32, vt}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "remainder_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(),
                                                      isConst ? U32Op(u32_remainder_const) : U32Op(u32_remainder_non_const));
    run(t, binaryOp("%"), {vt, U32}, vt, cfgInputSource(t), 0, cases);
});
CTS_TEST(testGroup, "remainder_vector_scalar_compound").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const int n = lhsVec(t);
    const bool isConst = cfgInputSource(t) == InputSource::Const;
    ExprType vt = vecType(n, ScalarKind::U32);
    auto cases = generateVectorU32BinaryToVectorCases(vectorU32Range(n), sparseU32Range(),
                                                      isConst ? U32Op(u32_remainder_const) : U32Op(u32_remainder_non_const));
    runCompound(t, "%=", {vt, U32}, vt, cfgInputSource(t), 0, cases);
});

} // namespace
