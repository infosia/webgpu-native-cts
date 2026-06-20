// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/ai_arithmetic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the abstract int arithmetic binary expression operations (const-eval only).

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
    "shader,execution,expression,binary,ai_arithmetic",
    "Execution Tests for the abstract int arithmetic binary expression operations");

// abstract-int is const-only (onlyConstInputSource).
ParamsBuilder vectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder vectorizeRhsParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize_rhs", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                   Value(static_cast<int64_t>(4))});
}
ParamsBuilder vectorizeLhsParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize_lhs", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                   Value(static_cast<int64_t>(4))});
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}
int rhsVec(const Fixture& t) { return static_cast<int>(t.param<int64_t>("vectorize_rhs")); }
int lhsVec(const Fixture& t) { return static_cast<int>(t.param<int64_t>("vectorize_lhs")); }

// Op functions (ai_arithmetic.cache.ts). Return nullopt to drop OOB / invalid cases.
std::optional<int64_t> ai_add(int64_t x, int64_t y) {
    int64_t r;
    if (i64AddOverflow(x, y, r)) {
        return std::nullopt;
    }
    return r;
}
std::optional<int64_t> ai_div(int64_t x, int64_t y) {
    if (y == 0) {
        return std::nullopt;
    }
    if (x == INT64_MIN && y == -1) {
        return std::nullopt;
    }
    return x / y;
}
std::optional<int64_t> ai_mul(int64_t x, int64_t y) {
    int64_t r;
    if (i64MulOverflow(x, y, r)) {
        return std::nullopt;
    }
    return r;
}
std::optional<int64_t> ai_rem(int64_t x, int64_t y) {
    if (y == 0) {
        return std::nullopt;
    }
    if (x == INT64_MIN && y == -1) {
        return std::nullopt;
    }
    return x % y;
}
std::optional<int64_t> ai_sub(int64_t x, int64_t y) {
    int64_t r;
    if (i64SubOverflow(x, y, r)) {
        return std::nullopt;
    }
    return r;
}

const ExprType AI = scalarType(ScalarKind::AbstractInt);

void runScalar(AllFeaturesMaxLimitsGpuTest& t, const std::string& op, const I64Op& f) {
    auto cases = generateBinaryToI64Cases(sparseI64Range(), sparseI64Range(), f);
    run(t, binaryOp(op), {AI, AI}, AI, InputSource::Const, cfgVectorize(t), cases);
}
void runScalarVector(AllFeaturesMaxLimitsGpuTest& t, const std::string& op, const I64Op& f) {
    const int n = rhsVec(t);
    ExprType vt = vecType(n, ScalarKind::AbstractInt);
    auto cases = generateI64VectorBinaryToVectorCases(sparseI64Range(), vectorI64Range(n), f);
    run(t, binaryOp(op), {AI, vt}, vt, InputSource::Const, 0, cases);
}
void runVectorScalar(AllFeaturesMaxLimitsGpuTest& t, const std::string& op, const I64Op& f) {
    const int n = lhsVec(t);
    ExprType vt = vecType(n, ScalarKind::AbstractInt);
    auto cases = generateVectorI64BinaryToVectorCases(vectorI64Range(n), sparseI64Range(), f);
    run(t, binaryOp(op), {vt, AI}, vt, InputSource::Const, 0, cases);
}

CTS_TEST(testGroup, "addition").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalar(t, "+", ai_add);
});
CTS_TEST(testGroup, "addition_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarVector(t, "+", ai_add);
});
CTS_TEST(testGroup, "addition_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runVectorScalar(t, "+", ai_add);
});
CTS_TEST(testGroup, "division").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalar(t, "/", ai_div);
});
CTS_TEST(testGroup, "division_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarVector(t, "/", ai_div);
});
CTS_TEST(testGroup, "division_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runVectorScalar(t, "/", ai_div);
});
CTS_TEST(testGroup, "multiplication").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalar(t, "*", ai_mul);
});
CTS_TEST(testGroup, "multiplication_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarVector(t, "*", ai_mul);
});
CTS_TEST(testGroup, "multiplication_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runVectorScalar(t, "*", ai_mul);
});
CTS_TEST(testGroup, "remainder").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalar(t, "%", ai_rem);
});
CTS_TEST(testGroup, "remainder_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarVector(t, "%", ai_rem);
});
CTS_TEST(testGroup, "remainder_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runVectorScalar(t, "%", ai_rem);
});
CTS_TEST(testGroup, "subtraction").params(vectorizeParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalar(t, "-", ai_sub);
});
CTS_TEST(testGroup, "subtraction_scalar_vector").params(vectorizeRhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runScalarVector(t, "-", ai_sub);
});
CTS_TEST(testGroup, "subtraction_vector_scalar").params(vectorizeLhsParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runVectorScalar(t, "-", ai_sub);
});

} // namespace
