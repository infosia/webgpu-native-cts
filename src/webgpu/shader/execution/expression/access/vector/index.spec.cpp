// Ported from gpuweb/cts src/webgpu/shader/execution/expression/access/vector/index.spec.ts
// @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for vector indexing expressions (v[i]). The operand is a vecN of a concrete
// (i32/u32/f32/f16/bool) or abstract (abstract-int/abstract-float) element type; the index is an
// i32/u32 scalar; the result is the indexed scalar element. Value-EXACT: the expected result is the
// real stored element, compared bit-for-bit (no tolerance). f16 element type is skipped when the
// 'shader-f16' feature is absent. The abstract_scalar variant is const-input-source only.

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::access;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,access,vector,index",
    "Execution Tests for vector indexing expressions");

// Builds the index scalar (i32 or u32) for value i.
Scalar makeIndex(ScalarKind kind, int i) {
    return kind == ScalarKind::U32 ? u32(static_cast<uint32_t>(i)) : i32(i);
}

ParamsBuilder concreteParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("elementType", {"i32", "u32", "f32", "f16", "bool"})
        .combine("indexType", {"i32", "u32"})
        .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                           Value(static_cast<int64_t>(4))});
}

ParamsBuilder abstractParams(ParamsBuilder u) {
    return u.combine("elementType", {"abstract-int", "abstract-float"})
        .combine("indexType", {"i32", "u32"})
        .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                           Value(static_cast<int64_t>(4))});
}

// Expression builder for v[i]: ops[0][ops[1]].
ExpressionBuilder indexBuilder() {
    return [](const std::vector<std::string>& ops) { return ops[0] + "[" + ops[1] + "]"; };
}

// Expression builder for the abstract variant: (v[i]) / 0x100000000.
ExpressionBuilder abstractIndexBuilder() {
    return [](const std::vector<std::string>& ops) {
        return ops[0] + "[" + ops[1] + "] / 0x100000000";
    };
}

CTS_TEST(testGroup, "concrete_scalar")
    .desc("Test indexing of concrete vectors")
    .params(concreteParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string elementTypeName = t.param<std::string>("elementType");
        const ScalarKind elemKind = elementKind(elementTypeName);
        if (elemKind == ScalarKind::F16) {
            skipIfNoF16(t);
        }
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const int width = static_cast<int>(t.param<int64_t>("width"));

        std::vector<Scalar> elements;
        elements.reserve(static_cast<size_t>(width));
        for (int i = 0; i < width; ++i) {
            const int v = elemKind == ScalarKind::Bool ? (i & 1) : (i + 1) * 10;
            elements.push_back(makeConcrete(elemKind, v));
        }
        const CaseValue vector = CaseValue::vec(elements);

        std::vector<Case> cases;
        cases.reserve(static_cast<size_t>(width));
        for (int i = 0; i < width; ++i) {
            cases.push_back({{vector, CaseValue(makeIndex(idxKind, i))}, CaseValue(elements[static_cast<size_t>(i)])});
        }

        run(t, indexBuilder(), {vecType(width, elemKind), scalarType(idxKind)},
            scalarType(elemKind), inputSourceFromParam(t.param<std::string>("inputSource")), 0,
            cases);
    });

CTS_TEST(testGroup, "abstract_scalar")
    .desc("Test indexing of abstract numeric vectors")
    .params(abstractParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind elemKind = elementKind(t.param<std::string>("elementType"));
        const ScalarKind idxKind = elementKind(t.param<std::string>("indexType"));
        const int width = static_cast<int>(t.param<int64_t>("width"));

        std::vector<Scalar> elements;
        elements.reserve(static_cast<size_t>(width));
        for (int i = 0; i < width; ++i) {
            elements.push_back(makeAbstractScaled(elemKind, i + 1)); // (i+1) * 0x100000000
        }
        const CaseValue vector = CaseValue::vec(elements);

        std::vector<Case> cases;
        cases.reserve(static_cast<size_t>(width));
        for (int i = 0; i < width; ++i) {
            cases.push_back({{vector, CaseValue(makeIndex(idxKind, i))},
                             CaseValue(f32FromInt(i + 1))});
        }

        run(t, abstractIndexBuilder(), {vecType(width, elemKind), scalarType(idxKind)},
            scalarType(ScalarKind::F32), InputSource::Const, 0, cases);
    });

} // namespace
