// Ported from gpuweb/cts src/webgpu/shader/execution/expression/constructor/non_zero.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for value constructors from components. The concrete (non-abstract, non-struct)
// constructors are ported here (all exact); the abstract_* g.tests and the 'structure' g.test are
// deferred to Stage B (abstract literal '/0x100000000' const-eval and struct predeclaration).

#include <cstdint>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/constructor/constructor_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::constructor;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,constructor,non_zero",
    "Execution Tests for value constructors from components");

void maybeSkipF16(AllFeaturesMaxLimitsGpuTest& t, ScalarKind k) {
    if (k == ScalarKind::F16 && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}

ExpressionBuilder fixedFn(const std::string& fn) {
    return [fn](const std::vector<std::string>& ops) {
        std::string out = fn + "(";
        for (size_t i = 0; i < ops.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += ops[i];
        }
        return out + ")";
    };
}

// Scalar for the (min|max|int) value of a concrete type. min/max use the largest finite values.
Scalar valueScalar(ScalarKind k, const std::string& v) {
    if (v == "min" || v == "max") {
        const bool isMin = v == "min";
        switch (k) {
            case ScalarKind::Bool:
                return boolean(!isMin);
            case ScalarKind::I32:
                return i32(isMin ? -2147483648 : 2147483647);
            case ScalarKind::U32:
                return u32(isMin ? 0u : 0xFFFFFFFFu);
            case ScalarKind::F32:
                return f32Bits(isMin ? 0xFF7FFFFFu : 0x7F7FFFFFu);
            case ScalarKind::F16:
                return f16Bits(isMin ? 0xFBFFu : 0x7BFFu);
            default:
                return u32(0);
        }
    }
    return scalarOfInt(k, std::stoi(v));
}

const char* widthSpelling(int w) {
    return w == 2 ? "2" : (w == 3 ? "3" : "4");
}

// ---- scalar_identity ----
CTS_TEST(testGroup, "scalar_identity")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("type", {"bool", "i32", "u32", "f32", "f16"})
            .combine("value", {"min", "max", "1", "2", "5", "100"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        const std::string value = t.param<std::string>("value");
        if (k == ScalarKind::Bool && value != "min" && value != "max") {
            t.skip("bool only tests min/max");
        }
        maybeSkipF16(t, k);
        const ExprType ty = scalarType(k);
        const Scalar s = valueScalar(k, value);
        run(t, fixedFn(wgslTypeName(ty)), {ty}, ty, cfgInputSource(t), 0,
            {Case({CaseValue(s)}, CaseValue(s))});
    });

// ---- vector_identity ----
CTS_TEST(testGroup, "vector_identity")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("type", {"bool", "i32", "u32", "f32", "f16"})
            .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))})
            .combine("infer_type", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        maybeSkipF16(t, k);
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const bool infer = t.param<bool>("infer_type");
        const ExprType vt = vecType(width, k);
        std::vector<Scalar> els;
        for (int i = 0; i < width; ++i) {
            els.push_back(scalarOfInt(k, k == ScalarKind::Bool ? (i & 1) : (i + 1) * 10));
        }
        const std::string fn = infer ? ("vec" + std::string(widthSpelling(width))) : wgslTypeName(vt);
        run(t, fixedFn(fn), {vt}, vt, cfgInputSource(t), 0,
            {Case({CaseValue::vec(els)}, CaseValue::vec(els))});
    });

// ---- concrete_vector_splat ----
CTS_TEST(testGroup, "concrete_vector_splat")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("type", {"bool", "i32", "u32", "f32", "f16"})
            .combine("value", {"min", "max", "1", "2", "5", "100"})
            .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))})
            .combine("infer_type", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        const std::string value = t.param<std::string>("value");
        if (k == ScalarKind::Bool && value != "min" && value != "max") {
            t.skip("bool only tests min/max");
        }
        maybeSkipF16(t, k);
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const bool infer = t.param<bool>("infer_type");
        const ExprType vt = vecType(width, k);
        const Scalar s = valueScalar(k, value);
        std::vector<Scalar> exp(width, s);
        const std::string fn = infer ? ("vec" + std::string(widthSpelling(width))) : wgslTypeName(vt);
        run(t, fixedFn(fn), {scalarType(k)}, vt, cfgInputSource(t), 0,
            {Case({CaseValue(s)}, CaseValue::vec(exp))});
    });

// ---- concrete_vector_elements ----
CTS_TEST(testGroup, "concrete_vector_elements")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("type", {"bool", "i32", "u32", "f32", "f16"})
            .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))})
            .combine("infer_type", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        maybeSkipF16(t, k);
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const bool infer = t.param<bool>("infer_type");
        const ExprType vt = vecType(width, k);
        std::vector<Scalar> els;
        std::vector<ExprType> params;
        for (int i = 0; i < width; ++i) {
            els.push_back(scalarOfInt(k, k == ScalarKind::Bool ? (i & 1) : (i + 1) * 10));
            params.push_back(scalarType(k));
        }
        std::vector<CaseValue> inputs;
        for (const Scalar& s : els) {
            inputs.push_back(CaseValue(s));
        }
        const std::string fn = infer ? ("vec" + std::string(widthSpelling(width))) : wgslTypeName(vt);
        run(t, fixedFn(fn), params, vt, cfgInputSource(t), 0, {Case(inputs, CaseValue::vec(els))});
    });

// ---- concrete_vector_mix ----
CTS_TEST(testGroup, "concrete_vector_mix")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("type", {"bool", "i32", "u32", "f32", "f16"})
            .combine("signature", {"2s", "s2", "2ss", "s2s", "ss2", "22", "3s", "s3"})
            .combine("infer_type", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("type"));
        maybeSkipF16(t, k);
        const std::string sig = t.param<std::string>("signature");
        const bool infer = t.param<bool>("infer_type");

        int width = 0;
        std::vector<int> elementVals;
        auto nextValue = [&]() -> int {
            const int v = (k == ScalarKind::Bool) ? (width & 1) : (width + 1) * 10;
            elementVals.push_back(v);
            ++width;
            return v;
        };
        std::vector<ExprType> params;
        std::vector<CaseValue> inputs;
        for (char c : sig) {
            if (c == '2') {
                std::vector<Scalar> els = {scalarOfInt(k, nextValue()), scalarOfInt(k, nextValue())};
                params.push_back(vecType(2, k));
                inputs.push_back(CaseValue::vec(els));
            } else if (c == '3') {
                std::vector<Scalar> els = {scalarOfInt(k, nextValue()), scalarOfInt(k, nextValue()),
                                           scalarOfInt(k, nextValue())};
                params.push_back(vecType(3, k));
                inputs.push_back(CaseValue::vec(els));
            } else if (c == 's') {
                params.push_back(scalarType(k));
                inputs.push_back(CaseValue(scalarOfInt(k, nextValue())));
            }
        }
        std::vector<Scalar> expected;
        for (int v : elementVals) {
            expected.push_back(scalarOfInt(k, v));
        }
        const ExprType vt = vecType(width, k);
        const std::string fn = infer ? ("vec" + std::string(widthSpelling(width))) : wgslTypeName(vt);
        run(t, fixedFn(fn), params, vt, cfgInputSource(t), 0,
            {Case(inputs, CaseValue::vec(expected))});
    });

// ---- matrix_identity / concrete_matrix_elements / concrete_matrix_column_vectors ----
ParamsBuilder matrixParams(ParamsBuilder u) {
    return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
        .combine("type", {"f32", "f16"})
        .combine("columns", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                             Value(static_cast<int64_t>(4))})
        .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                          Value(static_cast<int64_t>(4))})
        .combine("infer_type", {false, true});
}

CTS_TEST(testGroup, "matrix_identity").params(matrixParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const ScalarKind k = scalarKind(t.param<std::string>("type"));
    maybeSkipF16(t, k);
    const int cols = static_cast<int>(t.param<int64_t>("columns"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const bool infer = t.param<bool>("infer_type");
    const ExprType mt = matType(cols, rows, k);
    std::vector<Scalar> els;
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            els.push_back(scalarOfInt(k, (c + 1) * 10 + (r + 1)));
        }
    }
    const std::string fn = infer ? ("mat" + std::to_string(cols) + "x" + std::to_string(rows))
                                  : wgslTypeName(mt);
    run(t, fixedFn(fn), {mt}, mt, cfgInputSource(t), 0,
        {Case({CaseValue::vec(els)}, CaseValue::vec(els))});
});

CTS_TEST(testGroup, "concrete_matrix_elements").params(matrixParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const ScalarKind k = scalarKind(t.param<std::string>("type"));
    maybeSkipF16(t, k);
    const int cols = static_cast<int>(t.param<int64_t>("columns"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const bool infer = t.param<bool>("infer_type");
    const ExprType mt = matType(cols, rows, k);
    std::vector<Scalar> els;
    std::vector<ExprType> params;
    std::vector<CaseValue> inputs;
    for (int c = 0; c < cols; ++c) {
        for (int r = 0; r < rows; ++r) {
            Scalar s = scalarOfInt(k, (c + 1) * 10 + (r + 1));
            els.push_back(s);
            params.push_back(scalarType(k));
            inputs.push_back(CaseValue(s));
        }
    }
    const std::string fn = infer ? ("mat" + std::to_string(cols) + "x" + std::to_string(rows))
                                  : wgslTypeName(mt);
    run(t, fixedFn(fn), params, mt, cfgInputSource(t), 0, {Case(inputs, CaseValue::vec(els))});
});

CTS_TEST(testGroup, "concrete_matrix_column_vectors").params(matrixParams).fn([](AllFeaturesMaxLimitsGpuTest& t) {
    const ScalarKind k = scalarKind(t.param<std::string>("type"));
    maybeSkipF16(t, k);
    const int cols = static_cast<int>(t.param<int64_t>("columns"));
    const int rows = static_cast<int>(t.param<int64_t>("rows"));
    const bool infer = t.param<bool>("infer_type");
    const ExprType mt = matType(cols, rows, k);
    std::vector<Scalar> els;
    std::vector<ExprType> params;
    std::vector<CaseValue> inputs;
    for (int c = 0; c < cols; ++c) {
        std::vector<Scalar> col;
        for (int r = 0; r < rows; ++r) {
            Scalar s = scalarOfInt(k, (c + 1) * 10 + (r + 1));
            els.push_back(s);
            col.push_back(s);
        }
        params.push_back(vecType(rows, k));
        inputs.push_back(CaseValue::vec(col));
    }
    const std::string fn = infer ? ("mat" + std::to_string(cols) + "x" + std::to_string(rows))
                                  : wgslTypeName(mt);
    run(t, fixedFn(fn), params, mt, cfgInputSource(t), 0, {Case(inputs, CaseValue::vec(els))});
});

// ---- concrete_array_elements ----
CTS_TEST(testGroup, "concrete_array_elements")
    .params([](ParamsBuilder u) {
        return u.combine("inputSource", {"const", "uniform", "storage_r", "storage_rw"})
            .combine("type", {"bool", "i32", "u32", "f32", "f16", "vec3f", "vec4i"})
            .combine("length", {Value(static_cast<int64_t>(1)), Value(static_cast<int64_t>(5)),
                                Value(static_cast<int64_t>(10))})
            .combine("infer_type", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const int length = static_cast<int>(t.param<int64_t>("length"));
        const bool infer = t.param<bool>("infer_type");

        ExprType element;
        int compWidth = 1;
        ScalarKind compKind;
        if (typeName == "vec3f") {
            element = vecType(3, ScalarKind::F32);
            compWidth = 3;
            compKind = ScalarKind::F32;
        } else if (typeName == "vec4i") {
            element = vecType(4, ScalarKind::I32);
            compWidth = 4;
            compKind = ScalarKind::I32;
        } else {
            compKind = scalarKind(typeName);
            element = scalarType(compKind);
        }
        maybeSkipF16(t, element.scalarKind());
        const ExprType at = arrayType(length, element);

        std::vector<Scalar> expected;
        std::vector<ExprType> params;
        std::vector<CaseValue> inputs;
        for (int i = 0; i < length; ++i) {
            const int v = (i + 1) * 10;
            // elementType.create(v): scalar -> v; vector -> splat v across components.
            for (int c = 0; c < compWidth; ++c) {
                expected.push_back(scalarOfInt(compKind, v));
            }
            params.push_back(element);
            if (compWidth == 1) {
                inputs.push_back(CaseValue(scalarOfInt(compKind, v)));
            } else {
                std::vector<Scalar> els(compWidth, scalarOfInt(compKind, v));
                inputs.push_back(CaseValue::vec(els));
            }
        }
        const std::string fn = infer ? "array" : wgslTypeName(at);
        run(t, fixedFn(fn), params, at, cfgInputSource(t), 0,
            {Case(inputs, CaseValue::composite(expected))});
    });

// NOTE: abstract_* g.tests (splat/elements/mix/matrix/array) and g.test('structure') are DEFERRED to
// Stage B (abstract '/0x100000000' const-eval literal handling and struct predeclaration).

} // namespace
