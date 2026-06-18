// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/firstLeadingBit.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'firstLeadingBit' builtin function.

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,firstLeadingBit",
    "Execution tests for the 'firstLeadingBit' builtin function.");

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
        auto C = [&](uint32_t in, int64_t exp) {
            cases.push_back({{u32Bits(in)}, u32(static_cast<uint32_t>(exp))});
        };
        C(0x00000000u, -1);
        C(0x00000001u, 0);
        C(0x00000002u, 1);
        C(0x00000004u, 2);
        C(0x00000008u, 3);
        C(0x00000010u, 4);
        C(0x00000020u, 5);
        C(0x00000040u, 6);
        C(0x00000080u, 7);
        C(0x00000100u, 8);
        C(0x00000200u, 9);
        C(0x00000400u, 10);
        C(0x00000800u, 11);
        C(0x00001000u, 12);
        C(0x00002000u, 13);
        C(0x00004000u, 14);
        C(0x00008000u, 15);
        C(0x00010000u, 16);
        C(0x00020000u, 17);
        C(0x00040000u, 18);
        C(0x00080000u, 19);
        C(0x00100000u, 20);
        C(0x00200000u, 21);
        C(0x00400000u, 22);
        C(0x00800000u, 23);
        C(0x01000000u, 24);
        C(0x02000000u, 25);
        C(0x04000000u, 26);
        C(0x08000000u, 27);
        C(0x10000000u, 28);
        C(0x20000000u, 29);
        C(0x40000000u, 30);
        C(0x80000000u, 31);
        C(0x00000003u, 1);
        C(0x00000007u, 2);
        C(0x0000000Fu, 3);
        C(0x0000001Fu, 4);
        C(0x0000003Fu, 5);
        C(0x0000007Fu, 6);
        C(0x000000FFu, 7);
        C(0x000001FFu, 8);
        C(0x000003FFu, 9);
        C(0x000007FFu, 10);
        C(0x00000FFFu, 11);
        C(0x00001FFFu, 12);
        C(0x00003FFFu, 13);
        C(0x00007FFFu, 14);
        C(0x0000FFFFu, 15);
        C(0x0001FFFFu, 16);
        C(0x0003FFFFu, 17);
        C(0x0007FFFFu, 18);
        C(0x000FFFFFu, 19);
        C(0x001FFFFFu, 20);
        C(0x003FFFFFu, 21);
        C(0x007FFFFFu, 22);
        C(0x00FFFFFFu, 23);
        C(0x01FFFFFFu, 24);
        C(0x03FFFFFFu, 25);
        C(0x07FFFFFFu, 26);
        C(0x0FFFFFFFu, 27);
        C(0x1FFFFFFFu, 28);
        C(0x3FFFFFFFu, 29);
        C(0x7FFFFFFFu, 30);
        C(0xFFFFFFFFu, 31);
        C(0x00000006u, 2);
        C(0x0000000Du, 3);
        C(0x0000001Du, 4);
        C(0x00000039u, 5);
        C(0x0000006Fu, 6);
        C(0x000000FFu, 7);
        C(0x000001EFu, 8);
        C(0x000003FFu, 9);
        C(0x000007F1u, 10);
        C(0x00000EDDu, 11);
        C(0x00001B7Fu, 12);
        C(0x00003FDFu, 13);
        C(0x00005E75u, 14);
        C(0x0000DEF7u, 15);
        C(0x0001FFF3u, 16);
        C(0x0003FFBFu, 17);
        C(0x0007F7FFu, 18);
        C(0x000FFFFFu, 19);
        C(0x001F57BFu, 20);
        C(0x003EFFF7u, 21);
        C(0x007FF42Fu, 22);
        C(0x00FFF3FBu, 23);
        C(0x01FDFBFFu, 24);
        C(0x03AFBDFBu, 25);
        C(0x07FBFFFFu, 26);
        C(0x0F01B7BFu, 27);
        C(0x1EBDFFFFu, 28);
        C(0x36FE7FBDu, 29);
        C(0x57F7F7DFu, 30);
        C(0xE27ADBAFu, 31);
        run(t, builtin("firstLeadingBit"), {scalarType(ScalarKind::U32)}, scalarType(ScalarKind::U32),
            cfgInputSource(t), cfgVectorize(t), cases);
    });


CTS_TEST(testGroup, "i32")
    .params(inputSourceVectorizeParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<Case> cases;
        auto C = [&](uint32_t in, int64_t exp) {
            cases.push_back({{i32Bits(in)}, i32(static_cast<int32_t>(exp))});
        };
        C(0x00000000u, -1);
        C(0xFFFFFFFFu, -1);
        C(0x00000001u, 0);
        C(0x00000002u, 1);
        C(0x00000004u, 2);
        C(0x00000008u, 3);
        C(0x00000010u, 4);
        C(0x00000020u, 5);
        C(0x00000040u, 6);
        C(0x00000080u, 7);
        C(0x00000100u, 8);
        C(0x00000200u, 9);
        C(0x00000400u, 10);
        C(0x00000800u, 11);
        C(0x00001000u, 12);
        C(0x00002000u, 13);
        C(0x00004000u, 14);
        C(0x00008000u, 15);
        C(0x00010000u, 16);
        C(0x00020000u, 17);
        C(0x00040000u, 18);
        C(0x00080000u, 19);
        C(0x00100000u, 20);
        C(0x00200000u, 21);
        C(0x00400000u, 22);
        C(0x00800000u, 23);
        C(0x01000000u, 24);
        C(0x02000000u, 25);
        C(0x04000000u, 26);
        C(0x08000000u, 27);
        C(0x10000000u, 28);
        C(0x20000000u, 29);
        C(0x40000000u, 30);
        C(0xFFFFFFFEu, 0);
        C(0xFFFFFFFCu, 1);
        C(0xFFFFFFF8u, 2);
        C(0xFFFFFFF0u, 3);
        C(0xFFFFFFE0u, 4);
        C(0xFFFFFFC0u, 5);
        C(0xFFFFFF80u, 6);
        C(0xFFFFFF00u, 7);
        C(0xFFFFFE00u, 8);
        C(0xFFFFFC00u, 9);
        C(0xFFFFF800u, 10);
        C(0xFFFFF000u, 11);
        C(0xFFFFE000u, 12);
        C(0xFFFFC000u, 13);
        C(0xFFFF8000u, 14);
        C(0xFFFF0000u, 15);
        C(0xFFFE0000u, 16);
        C(0xFFFC0000u, 17);
        C(0xFFF80000u, 18);
        C(0xFFF00000u, 19);
        C(0xFFE00000u, 20);
        C(0xFFC00000u, 21);
        C(0xFF800000u, 22);
        C(0xFF000000u, 23);
        C(0xFE000000u, 24);
        C(0xFC000000u, 25);
        C(0xF8000000u, 26);
        C(0xF0000000u, 27);
        C(0xE0000000u, 28);
        C(0xC0000000u, 29);
        C(0x80000000u, 30);
        C(0x00000003u, 1);
        C(0x00000007u, 2);
        C(0x0000000Fu, 3);
        C(0x0000001Fu, 4);
        C(0x0000003Fu, 5);
        C(0x0000007Fu, 6);
        C(0x000000FFu, 7);
        C(0x000001FFu, 8);
        C(0x000003FFu, 9);
        C(0x000007FFu, 10);
        C(0x00000FFFu, 11);
        C(0x00001FFFu, 12);
        C(0x00003FFFu, 13);
        C(0x00007FFFu, 14);
        C(0x0000FFFFu, 15);
        C(0x0001FFFFu, 16);
        C(0x0003FFFFu, 17);
        C(0x0007FFFFu, 18);
        C(0x000FFFFFu, 19);
        C(0x001FFFFFu, 20);
        C(0x003FFFFFu, 21);
        C(0x007FFFFFu, 22);
        C(0x00FFFFFFu, 23);
        C(0x01FFFFFFu, 24);
        C(0x03FFFFFFu, 25);
        C(0x07FFFFFFu, 26);
        C(0x0FFFFFFFu, 27);
        C(0x1FFFFFFFu, 28);
        C(0x3FFFFFFFu, 29);
        C(0x7FFFFFFFu, 30);
        C(0xFFFFFFFDu, 1);
        C(0xFFFFFFFBu, 2);
        C(0xFFFFFFF7u, 3);
        C(0xFFFFFFEFu, 4);
        C(0xFFFFFFDFu, 5);
        C(0xFFFFFFBFu, 6);
        C(0xFFFFFF7Fu, 7);
        C(0xFFFFFEFFu, 8);
        C(0xFFFFFDFFu, 9);
        C(0xFFFFFBFFu, 10);
        C(0xFFFFF7FFu, 11);
        C(0xFFFFEFFFu, 12);
        C(0xFFFFDFFFu, 13);
        C(0xFFFFBFFFu, 14);
        C(0xFFFF7FFFu, 15);
        C(0xFFFEFFFFu, 16);
        C(0xFFFDFFFFu, 17);
        C(0xFFFBFFFFu, 18);
        C(0xFFF7FFFFu, 19);
        C(0xFFEFFFFFu, 20);
        C(0xFFDFFFFFu, 21);
        C(0xFFBFFFFFu, 22);
        C(0xFF7FFFFFu, 23);
        C(0xFEFFFFFFu, 24);
        C(0xFDFFFFFFu, 25);
        C(0xFBFFFFFFu, 26);
        C(0xF7FFFFFFu, 27);
        C(0xEFFFFFFFu, 28);
        C(0xDFFFFFFFu, 29);
        C(0xBFFFFFFFu, 30);
        C(0x00000006u, 2);
        C(0x0000000Du, 3);
        C(0x0000001Du, 4);
        C(0x00000039u, 5);
        C(0x0000006Fu, 6);
        C(0x000000FFu, 7);
        C(0x000001EFu, 8);
        C(0x000003FFu, 9);
        C(0x000007F1u, 10);
        C(0x00000EDDu, 11);
        C(0x00001B7Fu, 12);
        C(0x00003FDFu, 13);
        C(0x00005E75u, 14);
        C(0x0000DEF7u, 15);
        C(0x0001FFF3u, 16);
        C(0x0003FFBFu, 17);
        C(0x0007F7FFu, 18);
        C(0x000FFFFFu, 19);
        C(0x001F57BFu, 20);
        C(0x003EFFF7u, 21);
        C(0x007FF42Fu, 22);
        C(0x00FFF3FBu, 23);
        C(0x01FDFBFFu, 24);
        C(0x03AFBDFBu, 25);
        C(0x07FBFFFFu, 26);
        C(0x0F01B7BFu, 27);
        C(0x1EBDFFFFu, 28);
        C(0x36FE7FBDu, 29);
        C(0x57F7F7DFu, 30);
        C(0xFFFFFFFAu, 2);
        C(0xFFFFFFF6u, 3);
        C(0xFFFFFFEDu, 4);
        C(0xFFFFFFDDu, 5);
        C(0xFFFFFFB9u, 6);
        C(0xFFFFFF6Fu, 7);
        C(0xFFFFFEFFu, 8);
        C(0xFFFFFDEFu, 9);
        C(0xFFFFFBFFu, 10);
        C(0xFFFFF7F1u, 11);
        C(0xFFFFEEDDu, 12);
        C(0xFFFFDB7Fu, 13);
        C(0xFFFFBFDFu, 14);
        C(0xFFFF5E75u, 15);
        C(0xFFFEDEF7u, 16);
        C(0xFFFDFFF3u, 17);
        C(0xFFFBFFBFu, 18);
        C(0xFFF7F7FFu, 19);
        C(0xFFEFFFFFu, 20);
        C(0xFFDF57BFu, 21);
        C(0xFFBEFFF7u, 22);
        C(0xFF7FF42Fu, 23);
        C(0xFEFFF3FBu, 24);
        C(0xFDFDFBFFu, 25);
        C(0xFBAFBDFBu, 26);
        C(0xF7FBFFFFu, 27);
        C(0xEF01B7BFu, 28);
        C(0xDEBDFFFFu, 29);
        C(0xB6FE7FBDu, 30);
        run(t, builtin("firstLeadingBit"), {scalarType(ScalarKind::I32)}, scalarType(ScalarKind::I32),
            cfgInputSource(t), cfgVectorize(t), cases);
    });


} // namespace
