// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/bitwise.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the bitwise binary expression operations (i32 / u32 / abstract-int).

#include <cstdint>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,bitwise",
    "Execution Tests for the bitwise binary expression operations");

InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// Param builders.
ParamsBuilder typedParams(ParamsBuilder u) {
    return u.combine("type", {"i32", "u32", "abstract-int"})
        .combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder compoundParams(ParamsBuilder u) {
    return u.combine("type", {"i32", "u32"})
        .combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

enum class Kind { I32, U32, AI };
Kind kindOf(const std::string& s) {
    if (s == "i32") {
        return Kind::I32;
    }
    if (s == "u32") {
        return Kind::U32;
    }
    return Kind::AI;
}

Scalar makeScalar(Kind k, uint64_t bits) {
    switch (k) {
        case Kind::I32:
            return i32Bits(static_cast<uint32_t>(bits));
        case Kind::U32:
            return u32Bits(static_cast<uint32_t>(bits));
        case Kind::AI:
            return abstractInt64(static_cast<int64_t>(bits));
    }
    return u32Bits(0);
}

ScalarKind scalarKindOf(Kind k) {
    switch (k) {
        case Kind::I32:
            return ScalarKind::I32;
        case Kind::U32:
            return ScalarKind::U32;
        case Kind::AI:
            return ScalarKind::AbstractInt;
    }
    return ScalarKind::U32;
}

// Parses a binary digit string ("0"/"1") into a uint64_t (verbatim upstream literals; avoids any
// hand transcription error).
uint64_t bin(const char* s) {
    uint64_t v = 0;
    for (const char* p = s; *p; ++p) {
        v = (v << 1) | static_cast<uint64_t>(*p == '1' ? 1 : 0);
    }
    return v;
}

// Static input pairs (a, b) per (op, size), copied verbatim from upstream binary literals. The
// expected result is computed by applying the op (== upstream's hand-listed expected).
struct InputPair {
    uint64_t a;
    uint64_t b;
};

enum class Op { Or, And, Xor };

std::vector<InputPair> staticInputs(Op op, int size) {
    if (op == Op::Or) {
        if (size == 32) {
            return {
                {bin("00000000000000000000000000000000"), bin("00000000000000000000000000000000")},
                {bin("11111111111111111111111111111111"), bin("00000000000000000000000000000000")},
                {bin("00000000000000000000000000000000"), bin("11111111111111111111111111111111")},
                {bin("11111111111111111111111111111111"), bin("11111111111111111111111111111111")},
                {bin("10100100010010100100010010100100"), bin("00000000000000000000000000000000")},
                {bin("00000000000000000000000000000000"), bin("10100100010010100100010010100100")},
                {bin("01010010001001010010001001010010"), bin("10100100010010100100010010100100")},
            };
        }
        return {
            {bin("0000000000000000000000000000000000000000000000000000000000000000"),
             bin("0000000000000000000000000000000000000000000000000000000000000000")},
            {bin("1111111111111111111111111111111111111111111111111111111111111111"),
             bin("0000000000000000000000000000000000000000000000000000000000000000")},
            {bin("0000000000000000000000000000000000000000000000000000000000000000"),
             bin("1111111111111111111111111111111111111111111111111111111111111111")},
            {bin("1111111111111111111111111111111111111111111111111111111111111111"),
             bin("1111111111111111111111111111111111111111111111111111111111111111")},
            {bin("1010010001001010010001001010010010100100010010100100010010100100"),
             bin("0000000000000000000000000000000000000000000000000000000000000000")},
            {bin("0000000000000000000000000000000000000000000000000000000000000000"),
             bin("1010010001001010010001001010010010100100010010100100010010100100")},
            {bin("0101001000100101001000100101001010100100010010100100010010100100"),
             bin("1010010001001010010001001010010010100100010010100100010010100100")},
        };
    }
    if (op == Op::And) {
        if (size == 32) {
            return {
                {bin("00000000000000000000000000000000"), bin("00000000000000000000000000000000")},
                {bin("11111111111111111111111111111111"), bin("00000000000000000000000000000000")},
                {bin("00000000000000000000000000000000"), bin("11111111111111111111111111111111")},
                {bin("11111111111111111111111111111111"), bin("11111111111111111111111111111111")},
                {bin("10100100010010100100010010100100"), bin("00000000000000000000000000000000")},
                {bin("10100100010010100100010010100100"), bin("11111111111111111111111111111111")},
                {bin("00000000000000000000000000000000"), bin("10100100010010100100010010100100")},
                {bin("11111111111111111111111111111111"), bin("10100100010010100100010010100100")},
                {bin("01010010001001010010001001010010"), bin("01011011101101011011101101011011")},
            };
        }
        return {
            {bin("0000000000000000000000000000000000000000000000000000000000000000"),
             bin("0000000000000000000000000000000000000000000000000000000000000000")},
            {bin("1111111111111111111111111111111111111111111111111111111111111111"),
             bin("0000000000000000000000000000000000000000000000000000000000000000")},
            {bin("0000000000000000000000000000000000000000000000000000000000000000"),
             bin("1111111111111111111111111111111111111111111111111111111111111111")},
            {bin("1111111111111111111111111111111111111111111111111111111111111111"),
             bin("1111111111111111111111111111111111111111111111111111111111111111")},
            {bin("1010010001001010010001001010010010100100010010100100010010100100"),
             bin("0000000000000000000000000000000000000000000000000000000000000000")},
            {bin("1010010001001010010001001010010010100100010010100100010010100100"),
             bin("1111111111111111111111111111111111111111111111111111111111111111")},
            {bin("0000000000000000000000000000000000000000000000000000000000000000"),
             bin("1010010001001010010001001010010010100100010010100100010010100100")},
            {bin("1111111111111111111111111111111111111111111111111111111111111111"),
             bin("1010010001001010010001001010010010100100010010100100010010100100")},
            {bin("0101001000100101001000100101001001010010001001010010001001010010"),
             bin("0101101110110101101110110101101101011011101101011011101101011011")},
        };
    }
    // Xor.
    if (size == 32) {
        return {
            {bin("00000000000000000000000000000000"), bin("00000000000000000000000000000000")},
            {bin("11111111111111111111111111111111"), bin("00000000000000000000000000000000")},
            {bin("00000000000000000000000000000000"), bin("11111111111111111111111111111111")},
            {bin("11111111111111111111111111111111"), bin("11111111111111111111111111111111")},
            {bin("10100100010010100100010010100100"), bin("00000000000000000000000000000000")},
            {bin("10100100010010100100010010100100"), bin("11111111111111111111111111111111")},
            {bin("00000000000000000000000000000000"), bin("10100100010010100100010010100100")},
            {bin("11111111111111111111111111111111"), bin("10100100010010100100010010100100")},
            {bin("01010010001001010010001001010010"), bin("01011011101101011011101101011011")},
        };
    }
    return {
        {bin("0000000000000000000000000000000000000000000000000000000000000000"),
         bin("0000000000000000000000000000000000000000000000000000000000000000")},
        {bin("1111111111111111111111111111111111111111111111111111111111111111"),
         bin("0000000000000000000000000000000000000000000000000000000000000000")},
        {bin("0000000000000000000000000000000000000000000000000000000000000000"),
         bin("1111111111111111111111111111111111111111111111111111111111111111")},
        {bin("1111111111111111111111111111111111111111111111111111111111111111"),
         bin("1111111111111111111111111111111111111111111111111111111111111111")},
        {bin("1010010001001010010001001010010010100100010010100100010010100100"),
         bin("0000000000000000000000000000000000000000000000000000000000000000")},
        {bin("1010010001001010010001001010010010100100010010100100010010100100"),
         bin("1111111111111111111111111111111111111111111111111111111111111111")},
        {bin("0000000000000000000000000000000000000000000000000000000000000000"),
         bin("1010010001001010010001001010010010100100010010100100010010100100")},
        {bin("1111111111111111111111111111111111111111111111111111111111111111"),
         bin("1010010001001010010001001010010010100100010010100100010010100100")},
        {bin("0101001000100101001000100101001001010010001001010010001001010010"),
         bin("0101101110110101101110110101101101011011101101011011101101011011")},
    };
}

uint64_t apply(Op op, uint64_t a, uint64_t b) {
    switch (op) {
        case Op::Or:
            return a | b;
        case Op::And:
            return a & b;
        case Op::Xor:
            return a ^ b;
    }
    return 0;
}

std::vector<Case> makeBitwiseCases(Op op, Kind kind) {
    const int size = kind == Kind::AI ? 64 : 32;
    const uint64_t mask = size == 64 ? 0xFFFFFFFFFFFFFFFFull : 0xFFFFFFFFull;

    std::vector<Case> cases;
    auto add = [&](uint64_t a, uint64_t b, uint64_t e) {
        cases.push_back(
            {{CaseValue(makeScalar(kind, a)), CaseValue(makeScalar(kind, b))},
             CaseValue(makeScalar(kind, e))});
    };
    for (const InputPair& p : staticInputs(op, size)) {
        add(p.a, p.b, apply(op, p.a, p.b) & mask);
    }
    // Single-bit permutations.
    for (int i = 0; i < size; ++i) {
        const uint64_t lhs = (uint64_t(1) << i) & mask;
        for (int j = 0; j < size; ++j) {
            uint64_t rhs;
            if (op == Op::Or) {
                rhs = (uint64_t(1) << j) & mask;
            } else {
                rhs = mask ^ ((uint64_t(1) << j) & mask);
            }
            add(lhs, rhs, apply(op, lhs, rhs) & mask);
        }
    }
    return cases;
}

void runBitwise(AllFeaturesMaxLimitsGpuTest& t, Op op, const std::string& wgslOp) {
    const Kind kind = kindOf(t.param<std::string>("type"));
    const InputSource src = cfgInputSource(t);
    if (kind == Kind::AI && src != InputSource::Const) {
        t.skip("abstract-int only supports const input source");
    }
    const ExprType ty = scalarType(scalarKindOf(kind));
    auto cases = makeBitwiseCases(op, kind);
    run(t, binaryOp(wgslOp), {ty, ty}, ty, src, cfgVectorize(t), cases);
}

void runBitwiseCompound(AllFeaturesMaxLimitsGpuTest& t, Op op, const std::string& wgslOp) {
    const Kind kind = kindOf(t.param<std::string>("type"));
    const ExprType ty = scalarType(scalarKindOf(kind));
    auto cases = makeBitwiseCases(op, kind);
    runCompound(t, wgslOp, {ty, ty}, ty, cfgInputSource(t), cfgVectorize(t), cases);
}

CTS_TEST(testGroup, "bitwise_or").params(typedParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runBitwise(t, Op::Or, "|");
});
CTS_TEST(testGroup, "bitwise_or_compound").params(compoundParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runBitwiseCompound(t, Op::Or, "|=");
});
CTS_TEST(testGroup, "bitwise_and").params(typedParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runBitwise(t, Op::And, "&");
});
CTS_TEST(testGroup, "bitwise_and_compound").params(compoundParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runBitwiseCompound(t, Op::And, "&=");
});
CTS_TEST(testGroup, "bitwise_exclusive_or").params(typedParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runBitwise(t, Op::Xor, "^");
});
CTS_TEST(testGroup, "bitwise_exclusive_or_compound").params(compoundParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    runBitwiseCompound(t, Op::Xor, "^=");
});

} // namespace
