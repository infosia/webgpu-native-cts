// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/all.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'all' builtin function.

#include <cstdint>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,all",
    "Execution tests for the 'all' builtin function.");

CTS_TEST(testGroup, "bool")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("overload", {"scalar", "vec2", "vec3", "vec4"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string overload = t.param<std::string>("overload");
        const int width = overload == "scalar" ? 1 : (overload == "vec2" ? 2 : (overload == "vec3" ? 3 : 4));
        const InputSource src = inputSourceFromParam(t.param<std::string>("inputSource"));

        std::vector<Case> cases;
        const int n = 1 << width;
        for (int bits = 0; bits < n; ++bits) {
            std::vector<Scalar> in;
            bool allTrue = true;
            for (int i = 0; i < width; ++i) {
                const bool b = (bits >> i) & 1;
                in.push_back(boolean(b));
                allTrue = allTrue && b;
            }
            CaseValue inputValue = width == 1 ? CaseValue(in[0]) : CaseValue::vec(in);
            cases.push_back({{inputValue}, CaseValue(boolean(allTrue))});
        }

        const ExprType paramTy = width == 1 ? scalarType(ScalarKind::Bool)
                                            : vecType(width, ScalarKind::Bool);
        run(t, builtin("all"), {paramTy}, scalarType(ScalarKind::Bool), src, 0, cases);
    });

} // namespace
