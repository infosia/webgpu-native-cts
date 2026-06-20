// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/select.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for the 'select' builtin function. The 'short_circuit' test (which relies on the
// flow-control harness) and the abstract-float component are deferred to Stage B; all exact
// components (bool / f32 / f16 / abstract-int / i32 / u32, with integer-valued float operands which
// are bit-exact) are ported.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,select",
    "Execution tests for the 'select' builtin function.");

// Builds a component scalar for value 'v' of the given component code.
// 'b'=bool (parity), 'f'=f32, 'h'=f16, 'ai'=abstract-int, 'i'=i32, 'u'=u32.
Scalar componentScalar(const std::string& comp, int v) {
    if (comp == "b") {
        return boolean((v & 1) == 1);
    }
    if (comp == "i") {
        return i32(v);
    }
    if (comp == "u") {
        return u32(static_cast<uint32_t>(v));
    }
    if (comp == "f") {
        return f32Bits(access::f32BitsOfInt(v));
    }
    if (comp == "h") {
        return f16Bits(access::f16BitsOfDouble(static_cast<double>(v)));
    }
    // ai
    return abstractInt64(static_cast<int64_t>(v));
}

ScalarKind componentKind(const std::string& comp) {
    if (comp == "b") {
        return ScalarKind::Bool;
    }
    if (comp == "i") {
        return ScalarKind::I32;
    }
    if (comp == "u") {
        return ScalarKind::U32;
    }
    if (comp == "f") {
        return ScalarKind::F32;
    }
    if (comp == "h") {
        return ScalarKind::F16;
    }
    return ScalarKind::AbstractInt;
}

bool maybeSkip(AllFeaturesMaxLimitsGpuTest& t, const std::string& comp, InputSource src) {
    if (comp == "af" && src != InputSource::Const) {
        t.skip("abstract-float select only for const");
    }
    if (comp == "ai" && src != InputSource::Const) {
        t.skip("abstract-int select only for const");
    }
    if (comp == "h" && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
    if (comp == "af") {
        // abstract-float result materialization is Stage B.
        t.skip("abstract-float component deferred to Stage B");
    }
    return false;
}

ParamsBuilder selectParams(ParamsBuilder u, bool includeScalarOverload) {
    auto b = u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
                 .combine("component", {"b", "af", "f", "h", "ai", "i", "u"});
    if (includeScalarOverload) {
        return b.combine("overload", {"scalar", "vec2", "vec3", "vec4"});
    }
    return b.combine("overload", {"vec2", "vec3", "vec4"});
}

int overloadWidth(const std::string& o) {
    if (o == "scalar") {
        return 1;
    }
    if (o == "vec2") {
        return 2;
    }
    if (o == "vec3") {
        return 3;
    }
    return 4;
}

// g.test('scalar'): scalar-cond select. f/t are scalar or vecN of componentType; cond is scalar bool.
CTS_TEST(testGroup, "scalar")
    .params([](ParamsBuilder u) { return selectParams(u, true); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string comp = t.param<std::string>("component");
        const std::string overload = t.param<std::string>("overload");
        const InputSource src = inputSourceFromParam(t.param<std::string>("inputSource"));
        maybeSkip(t, comp, src);

        const int width = overloadWidth(overload);
        const ScalarKind kind = componentKind(comp);
        // scalars[0..7] from values [0,1,2,3,5,6,7,8].
        const int vals[8] = {0, 1, 2, 3, 5, 6, 7, 8};
        Scalar sc[8];
        for (int i = 0; i < 8; ++i) {
            sc[i] = componentScalar(comp, vals[i]);
        }
        auto mkVal = [&](int base) -> CaseValue {
            if (width == 1) {
                return CaseValue(sc[base]);
            }
            std::vector<Scalar> els;
            for (int i = 0; i < width; ++i) {
                els.push_back(sc[base + i]);
            }
            return CaseValue::vec(els);
        };
        // a = scalars[0..width-1]; b = scalars[4..4+width-1].
        const CaseValue a = mkVal(0);
        const CaseValue b = mkVal(4);
        std::vector<Case> cases = {
            {{a, b, CaseValue(boolean(false))}, a},
            {{a, b, CaseValue(boolean(true))}, b},
        };

        const ExprType ty = width == 1 ? scalarType(kind) : vecType(width, kind);
        run(t, builtin("select"), {ty, ty, scalarType(ScalarKind::Bool)}, ty, src, 0, cases);
    });

// g.test('vector'): vecN-cond select. f/t/cond are vecN.
CTS_TEST(testGroup, "vector")
    .params([](ParamsBuilder u) { return selectParams(u, false); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string comp = t.param<std::string>("component");
        const std::string overload = t.param<std::string>("overload");
        const InputSource src = inputSourceFromParam(t.param<std::string>("inputSource"));
        maybeSkip(t, comp, src);

        const int width = overloadWidth(overload);
        const ScalarKind kind = componentKind(comp);
        const int vals[8] = {0, 1, 2, 3, 5, 6, 7, 8};
        Scalar sc[8];
        for (int i = 0; i < 8; ++i) {
            sc[i] = componentScalar(comp, vals[i]);
        }
        std::vector<Scalar> aEls, bEls;
        for (int i = 0; i < width; ++i) {
            aEls.push_back(sc[i]);
            bEls.push_back(sc[4 + i]);
        }
        const CaseValue a = CaseValue::vec(aEls);
        const CaseValue b = CaseValue::vec(bEls);

        std::vector<Case> cases;
        const int n = 1 << width;
        for (int bits = 0; bits < n; ++bits) {
            std::vector<Scalar> cond, expected;
            for (int i = 0; i < width; ++i) {
                const bool ci = (bits >> i) & 1;
                cond.push_back(boolean(ci));
                expected.push_back(ci ? bEls[i] : aEls[i]);
            }
            cases.push_back({{a, b, CaseValue::vec(cond)}, CaseValue::vec(expected)});
        }

        const ExprType ty = vecType(width, kind);
        const ExprType boolTy = vecType(width, ScalarKind::Bool);
        run(t, builtin("select"), {ty, ty, boolTy}, ty, src, 0, cases);
    });

// NOTE: g.test('short_circuit') is DEFERRED to Stage B (it relies on the flow-control harness).

} // namespace
