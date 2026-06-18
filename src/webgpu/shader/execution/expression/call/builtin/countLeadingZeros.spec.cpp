// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/countLeadingZeros.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'countLeadingZeros' builtin function (clz).

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,countLeadingZeros",
    "Execution tests for the 'countLeadingZeros' builtin function.");

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
        // Zero
        C(0x00000000u, 32);
        // One
        C(0x00000001u, 31);
        // 0's after leading 1
        C(0x00000002u, 30);
        C(0x00000004u, 29);
        C(0x00000008u, 28);
        C(0x00000010u, 27);
        C(0x00000020u, 26);
        C(0x00000040u, 25);
        C(0x00000080u, 24);
        C(0x00000100u, 23);
        C(0x00000200u, 22);
        C(0x00000400u, 21);
        C(0x00000800u, 20);
        C(0x00001000u, 19);
        C(0x00002000u, 18);
        C(0x00004000u, 17);
        C(0x00008000u, 16);
        C(0x00010000u, 15);
        C(0x00020000u, 14);
        C(0x00040000u, 13);
        C(0x00080000u, 12);
        C(0x00100000u, 11);
        C(0x00200000u, 10);
        C(0x00400000u, 9);
        C(0x00800000u, 8);
        C(0x01000000u, 7);
        C(0x02000000u, 6);
        C(0x04000000u, 5);
        C(0x08000000u, 4);
        C(0x10000000u, 3);
        C(0x20000000u, 2);
        C(0x40000000u, 1);
        C(0x80000000u, 0);
        // 1's after leading 1
        C(0x00000003u, 30);
        C(0x00000007u, 29);
        C(0x0000000Fu, 28);
        C(0x0000001Fu, 27);
        C(0x0000003Fu, 26);
        C(0x0000007Fu, 25);
        C(0x000000FFu, 24);
        C(0x000001FFu, 23);
        C(0x000003FFu, 22);
        C(0x000007FFu, 21);
        C(0x00000FFFu, 20);
        C(0x00001FFFu, 19);
        C(0x00003FFFu, 18);
        C(0x00007FFFu, 17);
        C(0x0000FFFFu, 16);
        C(0x0001FFFFu, 15);
        C(0x0003FFFFu, 14);
        C(0x0007FFFFu, 13);
        C(0x000FFFFFu, 12);
        C(0x001FFFFFu, 11);
        C(0x003FFFFFu, 10);
        C(0x007FFFFFu, 9);
        C(0x00FFFFFFu, 8);
        C(0x01FFFFFFu, 7);
        C(0x03FFFFFFu, 6);
        C(0x07FFFFFFu, 5);
        C(0x0FFFFFFFu, 4);
        C(0x1FFFFFFFu, 3);
        C(0x3FFFFFFFu, 2);
        C(0x7FFFFFFFu, 1);
        C(0xFFFFFFFFu, 0);
        // random after leading 1
        C(0x00000006u, 29);
        C(0x0000000Du, 28);
        C(0x0000001Du, 27);
        C(0x00000039u, 26);
        C(0x0000006Fu, 25);
        C(0x000000FFu, 24);
        C(0x000001EFu, 23);
        C(0x000003FFu, 22);
        C(0x000007F1u, 21);
        C(0x00000EDDu, 20);
        C(0x00001B7Fu, 19);
        C(0x00003FDFu, 18);
        C(0x00005E75u, 17);
        C(0x0000DEF7u, 16);
        C(0x0001FFF3u, 15);
        C(0x0003FFBFu, 14);
        C(0x0007F7FFu, 13);
        C(0x000FFFFFu, 12);
        C(0x001F57BFu, 11);
        C(0x003EFFF7u, 10);
        C(0x007FF42Fu, 9);
        C(0x00FFF3FBu, 8);
        C(0x01FDFBFFu, 7);
        C(0x03AFBDFBu, 6);
        C(0x07FBFFFFu, 5);
        C(0x0F01B7BFu, 4);
        C(0x1EBDFFFFu, 3);
        C(0x36FE7FBDu, 2);
        C(0x57F7F7DFu, 1);
        C(0xE27ADBAFu, 0);
        run(t, builtin("countLeadingZeros"), {scalarType(ScalarKind::U32)},
            scalarType(ScalarKind::U32), cfgInputSource(t), cfgVectorize(t), cases);
    });

CTS_TEST(testGroup, "i32")
    .params(inputSourceVectorizeParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<Case> cases;
        auto C = [&](uint32_t in, int32_t exp) { cases.push_back({{i32Bits(in)}, i32(exp)}); };
        // Zero
        C(0x00000000u, 32);
        // One
        C(0x00000001u, 31);
        // 0's after leading 1
        C(0x00000002u, 30);
        C(0x00000004u, 29);
        C(0x00000008u, 28);
        C(0x00000010u, 27);
        C(0x00000020u, 26);
        C(0x00000040u, 25);
        C(0x00000080u, 24);
        C(0x00000100u, 23);
        C(0x00000200u, 22);
        C(0x00000400u, 21);
        C(0x00000800u, 20);
        C(0x00001000u, 19);
        C(0x00002000u, 18);
        C(0x00004000u, 17);
        C(0x00008000u, 16);
        C(0x00010000u, 15);
        C(0x00020000u, 14);
        C(0x00040000u, 13);
        C(0x00080000u, 12);
        C(0x00100000u, 11);
        C(0x00200000u, 10);
        C(0x00400000u, 9);
        C(0x00800000u, 8);
        C(0x01000000u, 7);
        C(0x02000000u, 6);
        C(0x04000000u, 5);
        C(0x08000000u, 4);
        C(0x10000000u, 3);
        C(0x20000000u, 2);
        C(0x40000000u, 1);
        C(0x80000000u, 0);
        // 1's after leading 1
        C(0x00000003u, 30);
        C(0x00000007u, 29);
        C(0x0000000Fu, 28);
        C(0x0000001Fu, 27);
        C(0x0000003Fu, 26);
        C(0x0000007Fu, 25);
        C(0x000000FFu, 24);
        C(0x000001FFu, 23);
        C(0x000003FFu, 22);
        C(0x000007FFu, 21);
        C(0x00000FFFu, 20);
        C(0x00001FFFu, 19);
        C(0x00003FFFu, 18);
        C(0x00007FFFu, 17);
        C(0x0000FFFFu, 16);
        C(0x0001FFFFu, 15);
        C(0x0003FFFFu, 14);
        C(0x0007FFFFu, 13);
        C(0x000FFFFFu, 12);
        C(0x001FFFFFu, 11);
        C(0x003FFFFFu, 10);
        C(0x007FFFFFu, 9);
        C(0x00FFFFFFu, 8);
        C(0x01FFFFFFu, 7);
        C(0x03FFFFFFu, 6);
        C(0x07FFFFFFu, 5);
        C(0x0FFFFFFFu, 4);
        C(0x1FFFFFFFu, 3);
        C(0x3FFFFFFFu, 2);
        C(0x7FFFFFFFu, 1);
        C(0xFFFFFFFFu, 0);
        // random after leading 1
        C(0x00000006u, 29);
        C(0x0000000Du, 28);
        C(0x0000001Du, 27);
        C(0x00000039u, 26);
        C(0x0000006Fu, 25);
        C(0x000000FFu, 24);
        C(0x000001EFu, 23);
        C(0x000003FFu, 22);
        C(0x000007F1u, 21);
        C(0x00000EDDu, 20);
        C(0x00001B7Fu, 19);
        C(0x00003FDFu, 18);
        C(0x00005E75u, 17);
        C(0x0000DEF7u, 16);
        C(0x0001FFF3u, 15);
        C(0x0003FFBFu, 14);
        C(0x0007F7FFu, 13);
        C(0x000FFFFFu, 12);
        C(0x001F57BFu, 11);
        C(0x003EFFF7u, 10);
        C(0x007FF42Fu, 9);
        C(0x00FFF3FBu, 8);
        C(0x01FDFBFFu, 7);
        C(0x03AFBDFBu, 6);
        C(0x07FBFFFFu, 5);
        C(0x0F01B7BFu, 4);
        C(0x1EBDFFFFu, 3);
        C(0x36FE7FBDu, 2);
        C(0x57F7F7DFu, 1);
        C(0xE27ADBAFu, 0);
        run(t, builtin("countLeadingZeros"), {scalarType(ScalarKind::I32)},
            scalarType(ScalarKind::I32), cfgInputSource(t), cfgVectorize(t), cases);
    });

} // namespace
