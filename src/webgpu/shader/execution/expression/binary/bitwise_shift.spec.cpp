// Ported from gpuweb/cts src/webgpu/shader/execution/expression/binary/bitwise_shift.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for the bitwise shift binary expression operations.

#include <cstdint>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,binary,bitwise_shift",
    "Execution Tests for the bitwise shift binary expression operations");

// Parses a binary digit string into a uint64_t (verbatim upstream literals; avoids transcription
// error for the long 64-bit shift input patterns).
uint64_t bin(const char* s) {
    uint64_t v = 0;
    for (const char* p = s; *p; ++p) {
        v = (v << 1) | static_cast<uint64_t>(*p == '1' ? 1 : 0);
    }
    return v;
}

InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

ParamsBuilder abstractParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}
ParamsBuilder concreteParams(ParamsBuilder u) {
    return u.combine("type", {"i32", "u32"})
        .combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

// --- const-eval validity (isValidConstShiftLeft / isValidConstShiftRight) ---

// e1 is a 32-bit value of the given signedness, e2 the shift amount.
bool isValidConstShiftLeft32(uint32_t e1, int e2, bool isSigned) {
    if (e2 == 0) {
        return true;
    }
    const int bitwidth = 32;
    if (e2 >= bitwidth) {
        return false;
    }
    if (!isSigned) {
        const int must_be_zero_msb = e2;
        const uint32_t mask = ~0u << (bitwidth - must_be_zero_msb);
        if ((e1 & mask) != 0u) {
            return false;
        }
    } else {
        const int64_t value = static_cast<int64_t>(static_cast<int32_t>(e1));
        const int64_t must_match_msb = static_cast<int64_t>(e2) + 1;
        // mask = ~0 << (bitwidth - must_match_msb), in 64-bit two's complement.
        const uint64_t shamt = static_cast<uint64_t>(static_cast<int64_t>(bitwidth) - must_match_msb);
        const uint64_t mask = static_cast<uint64_t>(-1) << shamt;
        const uint64_t v = static_cast<uint64_t>(value);
        if ((v & mask) != 0ull && (v & mask) != mask) {
            return false;
        }
    }
    return true;
}

bool isValidConstShiftRight32(int e2) {
    if (e2 == 0) {
        return true;
    }
    if (e2 >= 32) {
        return false;
    }
    return true;
}

// abstract (64-bit) left shift validity.
bool isValidConstShiftLeftAbstract(int64_t e1, int e2) {
    if (e2 == 0) {
        return true;
    }
    const int bitwidth = 64;
    if (e2 >= bitwidth) {
        return false;
    }
    const int64_t must_match_msb = static_cast<int64_t>(e2) + 1;
    const uint64_t shamt = static_cast<uint64_t>(static_cast<int64_t>(bitwidth) - must_match_msb);
    const uint64_t mask = static_cast<uint64_t>(-1) << shamt;
    const uint64_t v = static_cast<uint64_t>(e1);
    if ((v & mask) != 0ull && (v & mask) != mask) {
        return false;
    }
    return true;
}

// --- case builders ---

Scalar concreteScalar(bool isSigned, uint32_t bits) {
    return isSigned ? i32Bits(bits) : u32Bits(bits);
}

void addShiftLeftConcreteGen(std::vector<Case>& cases, uint32_t e1, bool isConst, bool isSigned) {
    for (int e2 = 0; e2 < 64; ++e2) {
        if (isConst && !isValidConstShiftLeft32(e1, e2, isSigned)) {
            continue;
        }
        const uint32_t expected = e1 << (e2 % 32);
        cases.push_back({{CaseValue(concreteScalar(isSigned, e1)), CaseValue(u32(static_cast<uint32_t>(e2)))},
                         CaseValue(concreteScalar(isSigned, expected))});
    }
}

void addShiftRightConcreteGen(std::vector<Case>& cases, uint32_t e1, bool isConst, bool isSigned) {
    for (int e2 = 0; e2 < 64; ++e2) {
        if (isConst && !isValidConstShiftRight32(e2)) {
            continue;
        }
        // JS '>>>' / '>>' mask the shift count to 5 bits (e2 & 31), matching WGSL runtime shift.
        const int sh = e2 & 31;
        uint32_t expected;
        if (!isSigned) {
            expected = e1 >> sh;  // logical
        } else {
            const int32_t sv = static_cast<int32_t>(e1);
            expected = static_cast<uint32_t>(sv >> sh);  // arithmetic
        }
        cases.push_back({{CaseValue(concreteScalar(isSigned, e1)), CaseValue(u32(static_cast<uint32_t>(e2)))},
                         CaseValue(concreteScalar(isSigned, expected))});
    }
}

std::vector<Case> makeShiftLeftConcreteCases(bool isConst, bool isSigned) {
    std::vector<Case> cases;
    auto addStatic = [&](uint32_t a, uint32_t shift, uint32_t e) {
        cases.push_back({{CaseValue(concreteScalar(isSigned, a)), CaseValue(u32(shift))},
                         CaseValue(concreteScalar(isSigned, e))});
    };
    addStatic(0x00000001u, 1, 0x00000002u);
    addStatic(0x00000003u, 1, 0x00000006u);
    const bool add_unsigned_overflow = !isConst || !isSigned;
    const bool add_signed_overflow = !isConst || isSigned;
    if (add_unsigned_overflow) {
        addStatic(0x40000000u, 1, 0x80000000u);
        addStatic(0x7FFFFFFFu, 1, 0xFFFFFFFEu);
        addStatic(0x00000001u, 31, 0x80000000u);
    }
    if (add_signed_overflow) {
        addStatic(0xC0000000u, 1, 0x80000000u);
        addStatic(0xFFFFFFFFu, 1, 0xFFFFFFFEu);
        addStatic(0xFFFFFFFFu, 31, 0x80000000u);
    }
    const uint32_t inputs[] = {0x00000000u, 0x00000001u, 0x00000002u, 0x00000003u, 0x80000000u,
                               0x40000000u, 0xC0000000u, 0x10208455u, 0xEFDF7BAAu};
    for (uint32_t in : inputs) {
        addShiftLeftConcreteGen(cases, in, isConst, isSigned);
    }
    return cases;
}

std::vector<Case> makeShiftRightConcreteCases(bool isConst, bool isSigned) {
    std::vector<Case> cases;
    auto addStatic = [&](uint32_t a, uint32_t shift, uint32_t e) {
        cases.push_back({{CaseValue(concreteScalar(isSigned, a)), CaseValue(u32(shift))},
                         CaseValue(concreteScalar(isSigned, e))});
    };
    addStatic(0x00000001u, 1, 0x00000000u);
    addStatic(0x00000003u, 1, 0x00000001u);
    addStatic(0x40000000u, 1, 0x20000000u);
    addStatic(0x60000000u, 1, 0x30000000u);
    if (!isSigned) {
        addStatic(0x80000000u, 1, 0x40000000u);
        addStatic(0xC0000000u, 1, 0x60000000u);
    } else {
        addStatic(0x80000000u, 1, 0xC0000000u);
        addStatic(0xC0000000u, 1, 0xE0000000u);
    }
    const uint32_t inputs[] = {0x00000000u, 0x00000001u, 0x00000002u, 0x00000003u, 0x80000000u,
                               0x40000000u, 0xC0000000u, 0x10208455u, 0xEFDF7BAAu};
    for (uint32_t in : inputs) {
        addShiftRightConcreteGen(cases, in, isConst, isSigned);
    }
    return cases;
}

// abstract (64-bit) cases.
void addShiftLeftAbstractGen(std::vector<Case>& cases, int64_t e1) {
    for (int e2 = 0; e2 < 64; ++e2) {
        if (!isValidConstShiftLeftAbstract(e1, e2)) {
            continue;
        }
        const int64_t expected = static_cast<int64_t>(static_cast<uint64_t>(e1) << e2);
        cases.push_back({{CaseValue(abstractInt64(e1)), CaseValue(u32(static_cast<uint32_t>(e2)))},
                         CaseValue(abstractInt64(expected))});
    }
}

void addShiftRightAbstractGen(std::vector<Case>& cases, int64_t e1) {
    for (int e2 = 0; e2 < 64; ++e2) {
        const int64_t expected = e1 >> e2;  // arithmetic (C++ impl-defined but arithmetic on common targets)
        cases.push_back({{CaseValue(abstractInt64(e1)), CaseValue(u32(static_cast<uint32_t>(e2)))},
                         CaseValue(abstractInt64(expected))});
    }
    for (int e2 = 64; e2 < 1025; e2 *= 2) {
        const int64_t expected = e1 < 0 ? -1 : 0;
        cases.push_back({{CaseValue(abstractInt64(e1)), CaseValue(u32(static_cast<uint32_t>(e2)))},
                         CaseValue(abstractInt64(expected))});
    }
}

std::vector<Case> makeShiftLeftAbstractCases() {
    std::vector<Case> cases;
    auto addStatic = [&](int64_t a, uint32_t shift, int64_t e) {
        cases.push_back({{CaseValue(abstractInt64(a)), CaseValue(u32(shift))},
                         CaseValue(abstractInt64(e))});
    };
    addStatic(0x1, 1, 0x2);
    addStatic(0x3, 1, 0x6);
    addStatic(0, 0, 0);
    addStatic(0, 1, 0);
    addStatic(0, 16, 0);
    addStatic(0, 32, 0);
    addStatic(0, 64, 0);
    addStatic(0, 65, 0);
    addStatic(0, 128, 0);
    addStatic(0, 256, 0);
    const int64_t inputs[] = {
        static_cast<int64_t>(bin("0000000000000000000000000000000000000000000000000000000000000000")),
        static_cast<int64_t>(bin("0000000000000000000000000000000000000000000000000000000000000001")),
        static_cast<int64_t>(bin("0000000000000000000000000000000000000000000000000000000000000010")),
        static_cast<int64_t>(bin("0000000000000000000000000000000000000000000000000000000000000011")),
        static_cast<int64_t>(bin("1000000000000000000000000000000000000000000000000000000000000000")),
        static_cast<int64_t>(bin("0100000000000000000000000000000000000000000000000000000000000000")),
        static_cast<int64_t>(bin("1100000000000000000000000000000000000000000000000000000000000000")),
        static_cast<int64_t>(bin("0001000000100000100001000101010100010000001000001000010001010101")),
        static_cast<int64_t>(bin("1110111111011111011110111010101011101111110111110111101110101010")),
    };
    for (int64_t in : inputs) {
        addShiftLeftAbstractGen(cases, in);
    }
    return cases;
}

std::vector<Case> makeShiftRightAbstractCases() {
    std::vector<Case> cases;
    auto addStatic = [&](int64_t a, uint32_t shift, int64_t e) {
        cases.push_back({{CaseValue(abstractInt64(a)), CaseValue(u32(shift))},
                         CaseValue(abstractInt64(e))});
    };
    addStatic(0x1, 1, 0x0);
    addStatic(0x3, 1, 0x1);
    addStatic(0x4000000000000000LL, 1, 0x2000000000000000LL);
    addStatic(0x6000000000000000LL, 1, 0x3000000000000000LL);
    addStatic(static_cast<int64_t>(0x8000000000000000ull), 1,
              static_cast<int64_t>(0xC000000000000000ull));
    addStatic(static_cast<int64_t>(0xC000000000000000ull), 1,
              static_cast<int64_t>(0xE000000000000000ull));
    const int64_t inputs[] = {
        static_cast<int64_t>(bin("0000000000000000000000000000000000000000000000000000000000000000")),
        static_cast<int64_t>(bin("0000000000000000000000000000000000000000000000000000000000000001")),
        static_cast<int64_t>(bin("0000000000000000000000000000000000000000000000000000000000000010")),
        static_cast<int64_t>(bin("0000000000000000000000000000000000000000000000000000000000000011")),
        static_cast<int64_t>(bin("1000000000000000000000000000000000000000000000000000000000000000")),
        static_cast<int64_t>(bin("0100000000000000000000000000000000000000000000000000000000000000")),
        static_cast<int64_t>(bin("1100000000000000000000000000000000000000000000000000000000000000")),
        static_cast<int64_t>(bin("0001000000100000100001000101010100010000001000001000010001010101")),
        static_cast<int64_t>(bin("1110111111011111011110111010101011101111110111110111101110101010")),
    };
    for (int64_t in : inputs) {
        addShiftRightAbstractGen(cases, in);
    }
    return cases;
}

const ExprType U32 = scalarType(ScalarKind::U32);
const ExprType AI = scalarType(ScalarKind::AbstractInt);

CTS_TEST(testGroup, "shift_left_abstract").params(abstractParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = makeShiftLeftAbstractCases();
    run(t, binaryOp("<<"), {AI, U32}, AI, InputSource::Const, cfgVectorize(t), cases);
});
CTS_TEST(testGroup, "shift_left_concrete").params(concreteParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isSigned = t.param<std::string>("type") == "i32";
    const InputSource src = cfgInputSource(t);
    auto cases = makeShiftLeftConcreteCases(src == InputSource::Const, isSigned);
    const ExprType ty = scalarType(isSigned ? ScalarKind::I32 : ScalarKind::U32);
    run(t, binaryOp("<<"), {ty, U32}, ty, src, cfgVectorize(t), cases);
});
CTS_TEST(testGroup, "shift_left_concrete_compound").params(concreteParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isSigned = t.param<std::string>("type") == "i32";
    const InputSource src = cfgInputSource(t);
    auto cases = makeShiftLeftConcreteCases(src == InputSource::Const, isSigned);
    const ExprType ty = scalarType(isSigned ? ScalarKind::I32 : ScalarKind::U32);
    runCompound(t, "<<=", {ty, U32}, ty, src, cfgVectorize(t), cases);
});
CTS_TEST(testGroup, "shift_right_abstract").params(abstractParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    auto cases = makeShiftRightAbstractCases();
    run(t, binaryOp(">>"), {AI, U32}, AI, InputSource::Const, cfgVectorize(t), cases);
});
CTS_TEST(testGroup, "shift_right_concrete").params(concreteParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isSigned = t.param<std::string>("type") == "i32";
    const InputSource src = cfgInputSource(t);
    auto cases = makeShiftRightConcreteCases(src == InputSource::Const, isSigned);
    const ExprType ty = scalarType(isSigned ? ScalarKind::I32 : ScalarKind::U32);
    run(t, binaryOp(">>"), {ty, U32}, ty, src, cfgVectorize(t), cases);
});
CTS_TEST(testGroup, "shift_right_concrete_compound").params(concreteParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const bool isSigned = t.param<std::string>("type") == "i32";
    const InputSource src = cfgInputSource(t);
    auto cases = makeShiftRightConcreteCases(src == InputSource::Const, isSigned);
    const ExprType ty = scalarType(isSigned ? ScalarKind::I32 : ScalarKind::U32);
    runCompound(t, ">>=", {ty, U32}, ty, src, cfgVectorize(t), cases);
});

} // namespace
