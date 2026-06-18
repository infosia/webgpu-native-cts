// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/firstTrailingBit.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'firstTrailingBit' builtin function.

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,firstTrailingBit",
    "Execution tests for the 'firstTrailingBit' builtin function.");

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
        C(0x80000000u, 31);
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
        C(0xFFFFFFFFu, 0);
        C(0xFFFFFFFEu, 1);
        C(0xFFFFFFFCu, 2);
        C(0xFFFFFFF8u, 3);
        C(0xFFFFFFF0u, 4);
        C(0xFFFFFFE0u, 5);
        C(0xFFFFFFC0u, 6);
        C(0xFFFFFF80u, 7);
        C(0xFFFFFF00u, 8);
        C(0xFFFFFE00u, 9);
        C(0xFFFFFC00u, 10);
        C(0xFFFFF800u, 11);
        C(0xFFFFF000u, 12);
        C(0xFFFFE000u, 13);
        C(0xFFFFC000u, 14);
        C(0xFFFF8000u, 15);
        C(0xFFFF0000u, 16);
        C(0xFFFE0000u, 17);
        C(0xFFFC0000u, 18);
        C(0xFFF80000u, 19);
        C(0xFFF00000u, 20);
        C(0xFFE00000u, 21);
        C(0xFFC00000u, 22);
        C(0xFF800000u, 23);
        C(0xFF000000u, 24);
        C(0xFE000000u, 25);
        C(0xFC000000u, 26);
        C(0xF8000000u, 27);
        C(0xF0000000u, 28);
        C(0xE0000000u, 29);
        C(0xC0000000u, 30);
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
        run(t, builtin("firstTrailingBit"), {scalarType(ScalarKind::U32)}, scalarType(ScalarKind::U32),
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
        C(0x80000000u, 31);
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
        C(0xFFFFFFFFu, 0);
        C(0xFFFFFFFEu, 1);
        C(0xFFFFFFFCu, 2);
        C(0xFFFFFFF8u, 3);
        C(0xFFFFFFF0u, 4);
        C(0xFFFFFFE0u, 5);
        C(0xFFFFFFC0u, 6);
        C(0xFFFFFF80u, 7);
        C(0xFFFFFF00u, 8);
        C(0xFFFFFE00u, 9);
        C(0xFFFFFC00u, 10);
        C(0xFFFFF800u, 11);
        C(0xFFFFF000u, 12);
        C(0xFFFFE000u, 13);
        C(0xFFFFC000u, 14);
        C(0xFFFF8000u, 15);
        C(0xFFFF0000u, 16);
        C(0xFFFE0000u, 17);
        C(0xFFFC0000u, 18);
        C(0xFFF80000u, 19);
        C(0xFFF00000u, 20);
        C(0xFFE00000u, 21);
        C(0xFFC00000u, 22);
        C(0xFF800000u, 23);
        C(0xFF000000u, 24);
        C(0xFE000000u, 25);
        C(0xFC000000u, 26);
        C(0xF8000000u, 27);
        C(0xF0000000u, 28);
        C(0xE0000000u, 29);
        C(0xC0000000u, 30);
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
        run(t, builtin("firstTrailingBit"), {scalarType(ScalarKind::I32)}, scalarType(ScalarKind::I32),
            cfgInputSource(t), cfgVectorize(t), cases);
    });


} // namespace
