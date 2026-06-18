// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/countOneBits.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'countOneBits' builtin function (population count).

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,countOneBits",
    "Execution tests for the 'countOneBits' builtin function.");

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

CTS_TEST(testGroup, "u32")
    .params(inputSourceVectorizeParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<Case> cases;
        auto C = [&](uint32_t in, uint32_t exp) { cases.push_back({{u32Bits(in)}, u32(exp)}); };
        // Zero / single-bit
        C(0x00000000u, 0);
        for (int b = 0; b < 32; ++b) {
            C(1u << b, 1);
        }
        // 1's after leading 1
        for (int n = 2; n <= 32; ++n) {
            C(n == 32 ? 0xFFFFFFFFu : ((1u << n) - 1u), static_cast<uint32_t>(n));
        }
        // random after leading 1
        C(0x00000006u, 2);
        C(0x0000000Du, 3);
        C(0x0000001Du, 4);
        C(0x00000039u, 4);
        C(0x0000006Fu, 6);
        C(0x000000FFu, 8);
        C(0x000001EFu, 8);
        C(0x000003FFu, 10);
        C(0x000007F1u, 8);
        C(0x00000EDDu, 9);
        C(0x00001B7Fu, 11);
        C(0x00003FDFu, 13);
        C(0x00005E75u, 10);
        C(0x0000DEF7u, 13);
        C(0x0001FFF3u, 15);
        C(0x0003FFBFu, 17);
        C(0x0007F7FFu, 18);
        C(0x000FFFFFu, 20);
        C(0x001F57BFu, 17);
        C(0x003EFFF7u, 20);
        C(0x007FF42Fu, 17);
        C(0x00FFF3FBu, 21);
        C(0x01FDFBFFu, 23);
        C(0x03AFBDFBu, 21);
        C(0x07FBFFFFu, 26);
        C(0x0F01B7BFu, 18);
        C(0x1EBDFFFFu, 26);
        C(0x36FE7FBDu, 24);
        C(0x57F7F7DFu, 26);
        C(0xE27ADBAFu, 21);
        run(t, builtin("countOneBits"), {scalarType(ScalarKind::U32)}, scalarType(ScalarKind::U32),
            cfgInputSource(t), cfgVectorize(t), cases);
    });

CTS_TEST(testGroup, "i32")
    .params(inputSourceVectorizeParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<Case> cases;
        auto C = [&](uint32_t in, int32_t exp) { cases.push_back({{i32Bits(in)}, i32(exp)}); };
        C(0x00000000u, 0);
        for (int b = 0; b < 32; ++b) {
            C(1u << b, 1);
        }
        for (int n = 2; n <= 32; ++n) {
            C(n == 32 ? 0xFFFFFFFFu : ((1u << n) - 1u), n);
        }
        C(0x00000006u, 2);
        C(0x0000000Du, 3);
        C(0x0000001Du, 4);
        C(0x00000039u, 4);
        C(0x0000006Fu, 6);
        C(0x000000FFu, 8);
        C(0x000001EFu, 8);
        C(0x000003FFu, 10);
        C(0x000007F1u, 8);
        C(0x00000EDDu, 9);
        C(0x00001B7Fu, 11);
        C(0x00003FDFu, 13);
        C(0x00005E75u, 10);
        C(0x0000DEF7u, 13);
        C(0x0001FFF3u, 15);
        C(0x0003FFBFu, 17);
        C(0x0007F7FFu, 18);
        C(0x000FFFFFu, 20);
        C(0x001F57BFu, 17);
        C(0x003EFFF7u, 20);
        C(0x007FF42Fu, 17);
        C(0x00FFF3FBu, 21);
        C(0x01FDFBFFu, 23);
        C(0x03AFBDFBu, 21);
        C(0x07FBFFFFu, 26);
        C(0x0F01B7BFu, 18);
        C(0x1EBDFFFFu, 26);
        C(0x36FE7FBDu, 24);
        C(0x57F7F7DFu, 26);
        C(0xE27ADBAFu, 21);
        run(t, builtin("countOneBits"), {scalarType(ScalarKind::I32)}, scalarType(ScalarKind::I32),
            cfgInputSource(t), cfgVectorize(t), cases);
    });

} // namespace
