// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/reverseBits.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'reverseBits' builtin function.
//
// The input table is ported verbatim from upstream: Zero, single-set-bit (1<<k for
// k=0..31), all-ones prefixes ((1<<n)-1 for n=2..32), and the 30 "random after leading
// 1" patterns. Upstream lists the expected value as the explicit bit-reversal of each
// input; we reproduce it here with reverse32(), which is exactly the builtin's defined
// behaviour (bit at position k of the result equals bit at position 31-k of e), so every
// expected value is bit-identical to the upstream literal.

#include <cstdint>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,reverseBits",
    "Execution tests for the 'reverseBits' builtin function.");

ParamsBuilder inputSourceVectorizeParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}

int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

uint32_t reverse32(uint32_t e) {
    uint32_t r = 0;
    for (int k = 0; k < 32; ++k) {
        r |= ((e >> k) & 1u) << (31 - k);
    }
    return r;
}

// The 30 "random after leading 1" inputs, ported verbatim (identical to upstream's
// countOneBits/reverseBits random table).
const uint32_t kRandomInputs[] = {
    0x00000006u, 0x0000000Du, 0x0000001Du, 0x00000039u, 0x0000006Fu, 0x000000FFu,
    0x000001EFu, 0x000003FFu, 0x000007F1u, 0x00000EDDu, 0x00001B7Fu, 0x00003FDFu,
    0x00005E75u, 0x0000DEF7u, 0x0001FFF3u, 0x0003FFBFu, 0x0007F7FFu, 0x000FFFFFu,
    0x001F57BFu, 0x003EFFF7u, 0x007FF42Fu, 0x00FFF3FBu, 0x01FDFBFFu, 0x03AFBDFBu,
    0x07FBFFFFu, 0x0F01B7BFu, 0x1EBDFFFFu, 0x36FE7FBDu, 0x57F7F7DFu, 0xE27ADBAFu,
};

template <typename MakeIn, typename MakeExp>
void appendCases(std::vector<Case>& cases, MakeIn makeIn, MakeExp makeExp) {
    auto C = [&](uint32_t in) {
        cases.push_back({{makeIn(in)}, makeExp(reverse32(in))});
    };
    // Zero
    C(0x00000000u);
    // One + 0's after leading 1 (single set bit at each position)
    for (int b = 0; b < 32; ++b) {
        C(1u << b);
    }
    // 1's after leading 1
    for (int n = 2; n <= 32; ++n) {
        C(n == 32 ? 0xFFFFFFFFu : ((1u << n) - 1u));
    }
    // random after leading 1
    for (uint32_t in : kRandomInputs) {
        C(in);
    }
}

CTS_TEST(testGroup, "u32")
    .params(inputSourceVectorizeParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<Case> cases;
        appendCases(
            cases, [](uint32_t v) { return u32Bits(v); }, [](uint32_t v) { return u32Bits(v); });
        run(t, builtin("reverseBits"), {scalarType(ScalarKind::U32)}, scalarType(ScalarKind::U32),
            cfgInputSource(t), cfgVectorize(t), cases);
    });

CTS_TEST(testGroup, "i32")
    .params(inputSourceVectorizeParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<Case> cases;
        appendCases(
            cases, [](uint32_t v) { return i32Bits(v); }, [](uint32_t v) { return i32Bits(v); });
        run(t, builtin("reverseBits"), {scalarType(ScalarKind::I32)}, scalarType(ScalarKind::I32),
            cfgInputSource(t), cfgVectorize(t), cases);
    });

} // namespace
