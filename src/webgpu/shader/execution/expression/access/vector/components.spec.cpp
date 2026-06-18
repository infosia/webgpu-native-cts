// Ported from gpuweb/cts src/webgpu/shader/execution/expression/access/vector/components.spec.ts
// @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for vector component-selection expressions (v.xyzw / v.rgba swizzles). The
// operand is a vecN of a concrete (i32/u32/f32/f16/bool) or abstract (abstract-int/abstract-float)
// element type; the selected components form a scalar (single index) or a vecM (multiple indices)
// result. The full set of 1..N-wide component-index permutations is enumerated as subcases (upstream
// indices(n) generator). Value-EXACT: each result element is the real stored element, compared
// bit-for-bit (no tolerance). f16 element type is skipped when the 'shader-f16' feature is absent.
// The abstract_scalar variant is const-input-source only.

#include <cstdint>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::access;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,access,vector,components",
    "Execution Tests for vector component selection expressions");

// Returns the full permutation of component indices used to component-select a vector of width 'n'.
// Each swizzle is encoded as a string of single decimal digits (e.g. {0,1} -> "01"); width <= 4 so
// every index is a single digit. Mirrors upstream indices(n): for width 1..n-1, every n^width tuple.
std::vector<std::string> indices(int n) {
    std::vector<std::string> out;
    for (int width = 1; width < n; ++width) {
        std::vector<std::string> level = {""};
        for (int pos = 0; pos < width; ++pos) {
            std::vector<std::string> next;
            for (const std::string& s : level) {
                for (int j = 0; j < width; ++j) {
                    next.push_back(s + static_cast<char>('0' + j));
                }
            }
            level = std::move(next);
        }
        for (const std::string& s : level) {
            out.push_back(s);
        }
    }
    return out;
}

// Decodes a swizzle string of single decimal digits into the index vector.
std::vector<int> decodeIndices(const std::string& s) {
    std::vector<int> out;
    out.reserve(s.size());
    for (char c : s) {
        out.push_back(c - '0');
    }
    return out;
}

ParamsBuilder concreteParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("elementType", {"i32", "u32", "f32", "f16", "bool"})
        .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                           Value(static_cast<int64_t>(4))})
        .combine("components", {"rgba", "xyzw"})
        .beginSubcases()
        .expand("indices", [](const ParamRecord& p) {
            const int width = static_cast<int>(valueAs<int64_t>(*findParam(p, "width")));
            std::vector<Value> out;
            for (const std::string& s : indices(width)) {
                out.push_back(Value(s));
            }
            return out;
        });
}

ParamsBuilder abstractParams(ParamsBuilder u) {
    return u.combine("elementType", {"abstract-int", "abstract-float"})
        .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                           Value(static_cast<int64_t>(4))})
        .combine("components", {"rgba", "xyzw"})
        .beginSubcases()
        .expand("indices", [](const ParamRecord& p) {
            const int width = static_cast<int>(valueAs<int64_t>(*findParam(p, "width")));
            std::vector<Value> out;
            for (const std::string& s : indices(width)) {
                out.push_back(Value(s));
            }
            return out;
        });
}

CTS_TEST(testGroup, "concrete_scalar")
    .desc("Test vector component selection of concrete vectors")
    .params(concreteParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind elemKind = elementKind(t.param<std::string>("elementType"));
        if (elemKind == ScalarKind::F16) {
            skipIfNoF16(t);
        }
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const std::string components = t.param<std::string>("components");
        const std::vector<int> idx = decodeIndices(t.param<std::string>("indices"));

        // elementValues(i): bool -> i&1; else (i+1)*10.
        auto elementValues = [&](int i) {
            return elemKind == ScalarKind::Bool ? (i & 1) : (i + 1) * 10;
        };

        std::vector<Scalar> elements;
        elements.reserve(static_cast<size_t>(width));
        for (int i = 0; i < width; ++i) {
            elements.push_back(makeConcrete(elemKind, elementValues(i)));
        }

        // Build the result value (scalar when 1 index, vecM otherwise).
        ExprType resultType;
        CaseValue expected;
        if (idx.size() == 1) {
            resultType = scalarType(elemKind);
            expected = CaseValue(makeConcrete(elemKind, elementValues(idx[0])));
        } else {
            resultType = vecType(static_cast<int>(idx.size()), elemKind);
            std::vector<Scalar> rels;
            rels.reserve(idx.size());
            for (int i : idx) {
                rels.push_back(makeConcrete(elemKind, elementValues(i)));
            }
            expected = CaseValue::vec(rels);
        }

        // Swizzle string: map each index to the component letter.
        std::string swizzle;
        for (int i : idx) {
            swizzle += components[static_cast<size_t>(i)];
        }
        ExpressionBuilder builder = [swizzle](const std::vector<std::string>& ops) {
            return ops[0] + "." + swizzle;
        };

        run(t, builder, {vecType(width, elemKind)}, resultType,
            inputSourceFromParam(t.param<std::string>("inputSource")), 0,
            {Case{{CaseValue::vec(elements)}, expected}});
    });

CTS_TEST(testGroup, "abstract_scalar")
    .desc("Test vector component selection of abstract numeric vectors")
    .params(abstractParams)
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind elemKind = elementKind(t.param<std::string>("elementType"));
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const std::string components = t.param<std::string>("components");
        const std::vector<int> idx = decodeIndices(t.param<std::string>("indices"));

        std::vector<Scalar> elements;
        elements.reserve(static_cast<size_t>(width));
        for (int i = 0; i < width; ++i) {
            elements.push_back(makeAbstractScaled(elemKind, i + 1)); // (i+1) * 0x100000000
        }

        // Result is f32 (scalar) or vecM<f32>: each selected element / 0x100000000 == index+1.
        ExprType resultType;
        CaseValue expected;
        if (idx.size() == 1) {
            resultType = scalarType(ScalarKind::F32);
            expected = CaseValue(f32FromInt(idx[0] + 1));
        } else {
            resultType = vecType(static_cast<int>(idx.size()), ScalarKind::F32);
            std::vector<Scalar> rels;
            rels.reserve(idx.size());
            for (int i : idx) {
                rels.push_back(f32FromInt(i + 1));
            }
            expected = CaseValue::vec(rels);
        }

        std::string swizzle;
        for (int i : idx) {
            swizzle += components[static_cast<size_t>(i)];
        }
        ExpressionBuilder builder = [swizzle](const std::vector<std::string>& ops) {
            return ops[0] + "." + swizzle + " / 0x100000000";
        };

        run(t, builder, {vecType(width, elemKind)}, resultType, InputSource::Const, 0,
            {Case{{CaseValue::vec(elements)}, expected}});
    });

} // namespace
