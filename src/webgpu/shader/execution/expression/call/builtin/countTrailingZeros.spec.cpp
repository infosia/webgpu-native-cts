// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/countTrailingZeros.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'countTrailingZeros' builtin function (number of consecutive
// 0 bits starting from the least significant bit; also known as "ctz").

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,countTrailingZeros",
    "Execution tests for the 'countTrailingZeros' builtin function.");

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
        // High bit
        C(0x80000000u, 31);
        // 0's before trailing 1
        for (int b = 0; b <= 30; ++b) {
            C(1u << b, static_cast<uint32_t>(b));
        }
        // 1's before trailing 1
        for (int n = 0; n <= 30; ++n) {
            C(0xFFFFFFFFu << n, static_cast<uint32_t>(n));
        }
        // random before trailing 1
        C(0xF03FDE8Fu, 0);
        C(0xDEFE5CF2u, 1);
        C(0xF76FF43Cu, 2);
        C(0xD377F4E8u, 3);
        C(0xD7DF1FB0u, 4);
        C(0xFDF7EBE0u, 5);
        C(0xF9EF9EC0u, 6);
        C(0xCEDF7E80u, 7);
        C(0xEF7EEB00u, 8);
        C(0xFDEFFE00u, 9);
        C(0x9F776C00u, 10);
        C(0xFFB7B800u, 11);
        C(0xFB5BB000u, 12);
        C(0x3D43A000u, 13);
        C(0xFBC6C000u, 14);
        C(0xBF5F8000u, 15);
        C(0xDDEB0000u, 16);
        C(0x74DA0000u, 17);
        C(0xE72C0000u, 18);
        C(0xF9D80000u, 19);
        C(0x34900000u, 20);
        C(0xFA600000u, 21);
        C(0x02C00000u, 22);
        C(0xE7800000u, 23);
        C(0x2D000000u, 24);
        C(0xDA000000u, 25);
        C(0xD4000000u, 26);
        C(0xB8000000u, 27);
        C(0x70000000u, 28);
        C(0xA0000000u, 29);
        run(t, builtin("countTrailingZeros"), {scalarType(ScalarKind::U32)},
            scalarType(ScalarKind::U32), cfgInputSource(t), cfgVectorize(t), cases);
    });

CTS_TEST(testGroup, "i32")
    .params(inputSourceVectorizeParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::vector<Case> cases;
        auto C = [&](uint32_t in, int32_t exp) { cases.push_back({{i32Bits(in)}, i32(exp)}); };
        // Zero
        C(0x00000000u, 32);
        // High bit
        C(0x80000000u, 31);
        // 0's before trailing 1
        for (int b = 0; b <= 30; ++b) {
            C(1u << b, b);
        }
        // 1's before trailing 1
        for (int n = 0; n <= 30; ++n) {
            C(0xFFFFFFFFu << n, n);
        }
        // random before trailing 1
        C(0xF03FDE8Fu, 0);
        C(0xDEFE5CF2u, 1);
        C(0xF76FF43Cu, 2);
        C(0xD377F4E8u, 3);
        C(0xD7DF1FB0u, 4);
        C(0xFDF7EBE0u, 5);
        C(0xF9EF9EC0u, 6);
        C(0xCEDF7E80u, 7);
        C(0xEF7EEB00u, 8);
        C(0xFDEFFE00u, 9);
        C(0x9F776C00u, 10);
        C(0xFFB7B800u, 11);
        C(0xFB5BB000u, 12);
        C(0x3D43A000u, 13);
        C(0xFBC6C000u, 14);
        C(0xBF5F8000u, 15);
        C(0xDDEB0000u, 16);
        C(0x74DA0000u, 17);
        C(0xE72C0000u, 18);
        C(0xF9D80000u, 19);
        C(0x34900000u, 20);
        C(0xFA600000u, 21);
        C(0x02C00000u, 22);
        C(0xE7800000u, 23);
        C(0x2D000000u, 24);
        C(0xDA000000u, 25);
        C(0xD4000000u, 26);
        C(0xB8000000u, 27);
        C(0x70000000u, 28);
        C(0xA0000000u, 29);
        run(t, builtin("countTrailingZeros"), {scalarType(ScalarKind::I32)},
            scalarType(ScalarKind::I32), cfgInputSource(t), cfgVectorize(t), cases);
    });

} // namespace
