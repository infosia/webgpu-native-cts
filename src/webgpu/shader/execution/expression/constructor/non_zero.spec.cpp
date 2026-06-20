// Ported from gpuweb/cts src/webgpu/shader/execution/expression/constructor/non_zero.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for value constructors from components. The concrete constructors are exact; the
// abstract_* g.tests use the '* 0x100000000 ... / 0x100000000' literal trick to force abstract
// const-eval into a concrete result; the 'structure' g.test uses module-scope struct predeclaration
// (runWithPredeclaration). See the abstract/structure section below for the member_types encoding.

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

// An ExpressionBuilder that ignores its operands and emits a fixed expression string (used by the
// abstract_* const-eval tests, whose expressions embed literal values directly).
ExpressionBuilder fixedExprNonZero(const std::string& expr) {
    return [expr](const std::vector<std::string>&) { return expr; };
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

// ---------------------------------------------------------------------------
// Abstract-numeric and structure constructors.
//
// The abstract_* g.tests force abstract const-eval via the literal trick: each value is emitted as
// `value * 0x100000000` (with a `.0` suffix for abstract-float), and the whole expression is divided
// by `0x100000000` (or multiplied by `1.0 / 0x100000000` for the matrix variants). The result type
// is CONCRETE, so a fixed (operand-ignoring) ExpressionBuilder with empty params + InputSource::Const
// is used, and the expected is the concrete value the trick recovers exactly.
//
// NOTE: upstream's `member_types` param (structure test) and `kConcreteTypesForAbstractType` lists are
// arrays of type names; Value cannot hold an array, so `member_types` is encoded as a comma-delimited
// string (e.g. "i32,u32"). This changes only the spelling of the member_types query component; the
// case/subcase structure is identical.
// ---------------------------------------------------------------------------

// kAbstractValueScale = 0x100000000 = 2^32.
const int64_t kAbstractValueScale = 4294967296LL;

// Concrete-type metadata: the element scalar kind, the component width (1=scalar, 2/3/4=vec), and
// whether the concrete type is f16 (for the shader-f16 skip).
struct ConcreteTypeInfo {
    ScalarKind kind = ScalarKind::F32;
    int width = 1;     // vector width (1 for scalars)
    bool isMatrix = false;
    int cols = 0;      // matrix columns
    int rows = 0;      // matrix rows
    bool isF16 = false;
};

// Parse a concrete type name (f32/f16/i32/u32, vecNf/vecNh/vecNi/vecNu, matCxRf/matCxRh).
ConcreteTypeInfo parseConcreteType(const std::string& name) {
    ConcreteTypeInfo info;
    auto kindFromSuffix = [](char c) -> ScalarKind {
        switch (c) {
            case 'f':
                return ScalarKind::F32;
            case 'h':
                return ScalarKind::F16;
            case 'i':
                return ScalarKind::I32;
            case 'u':
                return ScalarKind::U32;
            default:
                return ScalarKind::F32;
        }
    };
    if (name.rfind("mat", 0) == 0) {
        // matCxR{f|h}
        info.isMatrix = true;
        info.cols = name[3] - '0';
        info.rows = name[5] - '0';
        const char suffix = name.back();
        info.kind = kindFromSuffix(suffix);
        info.width = info.rows;
        info.isF16 = info.kind == ScalarKind::F16;
        return info;
    }
    if (name.rfind("vec", 0) == 0) {
        info.width = name[3] - '0';
        const char suffix = name.back();
        info.kind = kindFromSuffix(suffix);
        info.isF16 = info.kind == ScalarKind::F16;
        return info;
    }
    // scalar
    info.kind = scalarKind(name);
    info.width = 1;
    info.isF16 = info.kind == ScalarKind::F16;
    return info;
}

// The ExprType for a parsed concrete type.
ExprType concreteExprType(const ConcreteTypeInfo& info) {
    if (info.isMatrix) {
        return matType(info.cols, info.rows, info.kind);
    }
    if (info.width > 1) {
        return vecType(info.width, info.kind);
    }
    return scalarType(info.kind);
}

void maybeSkipF16Concrete(AllFeaturesMaxLimitsGpuTest& t, bool isF16) {
    if (isF16 && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
    }
}

// The concrete types tested for each abstract type (kConcreteTypesForAbstractType).
std::vector<Value> concreteTypesForAbstractType(const std::string& abstractType) {
    if (abstractType == "abstract-float") {
        return {Value("f32"), Value("f16")};
    }
    if (abstractType == "abstract-int") {
        return {Value("f32"), Value("f16"), Value("i32"), Value("u32")};
    }
    if (abstractType == "vec3<abstract-int>") {
        return {Value("vec3f"), Value("vec3h"), Value("vec3i"), Value("vec3u")};
    }
    if (abstractType == "vec4<abstract-float>") {
        return {Value("vec4f"), Value("vec4h")};
    }
    if (abstractType == "mat2x3<abstract-float>") {
        return {Value("mat2x3f"), Value("mat2x3h")};
    }
    return {};
}

// Emit an abstract literal for `value`: `value * 2^32`, with a `.0` suffix for abstract-float.
std::string abstractLiteral(int value, bool abstractFloat) {
    std::string s = std::to_string(static_cast<int64_t>(value) * kAbstractValueScale);
    if (abstractFloat) {
        s += ".0";
    }
    return s;
}

// ---- abstract_vector_splat ----
CTS_TEST(testGroup, "abstract_vector_splat")
    .params([](ParamsBuilder u) {
        return u.combine("abstract_type", {"abstract-int", "abstract-float"})
            .expand("concrete_type",
                    [](const ParamRecord& p) {
                        const Value* v = findParam(p, "abstract_type");
                        return v != nullptr ? concreteTypesForAbstractType(valueAs<std::string>(*v))
                                            : std::vector<Value>{};
                    })
            .combine("value", {Value(static_cast<int64_t>(1)), Value(static_cast<int64_t>(2)),
                               Value(static_cast<int64_t>(5)), Value(static_cast<int64_t>(100))})
            .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string abstractType = t.param<std::string>("abstract_type");
        const ConcreteTypeInfo elemInfo = parseConcreteType(t.param<std::string>("concrete_type"));
        maybeSkipF16Concrete(t, elemInfo.isF16);
        const bool abstractFloat = abstractType == "abstract-float";
        const int value = static_cast<int>(t.param<int64_t>("value"));
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const ExprType vt = vecType(width, elemInfo.kind);

        const std::string expr = "vec" + std::to_string(width) + "(" +
                                 abstractLiteral(value, abstractFloat) + ") / 0x100000000";
        std::vector<Scalar> els(static_cast<size_t>(width), scalarOfInt(elemInfo.kind, value));
        run(t, fixedExprNonZero(expr), {}, vt, InputSource::Const, 0,
            {Case({}, CaseValue::vec(els))});
    });

// ---- abstract_vector_elements ----
CTS_TEST(testGroup, "abstract_vector_elements")
    .params([](ParamsBuilder u) {
        return u.combine("abstract_type", {"abstract-int", "abstract-float"})
            .expand("concrete_type",
                    [](const ParamRecord& p) {
                        const Value* v = findParam(p, "abstract_type");
                        return v != nullptr ? concreteTypesForAbstractType(valueAs<std::string>(*v))
                                            : std::vector<Value>{};
                    })
            .combine("width", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                               Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string abstractType = t.param<std::string>("abstract_type");
        const ConcreteTypeInfo elemInfo = parseConcreteType(t.param<std::string>("concrete_type"));
        maybeSkipF16Concrete(t, elemInfo.isF16);
        const bool abstractFloat = abstractType == "abstract-float";
        const int width = static_cast<int>(t.param<int64_t>("width"));
        const ExprType vt = vecType(width, elemInfo.kind);

        std::string args;
        std::vector<Scalar> els;
        for (int i = 0; i < width; ++i) {
            const int v = (i + 1) * 10;
            if (i > 0) {
                args += ", ";
            }
            args += abstractLiteral(v, abstractFloat);
            els.push_back(scalarOfInt(elemInfo.kind, v));
        }
        const std::string expr = "vec" + std::to_string(width) + "(" + args + ") / 0x100000000";
        run(t, fixedExprNonZero(expr), {}, vt, InputSource::Const, 0,
            {Case({}, CaseValue::vec(els))});
    });

// ---- abstract_vector_mix ----
CTS_TEST(testGroup, "abstract_vector_mix")
    .params([](ParamsBuilder u) {
        return u.combine("abstract_type", {"abstract-int", "abstract-float"})
            .expand("concrete_type",
                    [](const ParamRecord& p) {
                        const Value* v = findParam(p, "abstract_type");
                        return v != nullptr ? concreteTypesForAbstractType(valueAs<std::string>(*v))
                                            : std::vector<Value>{};
                    })
            .combine("signature", {"2s", "s2", "2ss", "s2s", "ss2", "22", "3s", "s3"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string abstractType = t.param<std::string>("abstract_type");
        const ConcreteTypeInfo elemInfo = parseConcreteType(t.param<std::string>("concrete_type"));
        maybeSkipF16Concrete(t, elemInfo.isF16);
        const bool abstractFloat = abstractType == "abstract-float";
        const std::string sig = t.param<std::string>("signature");

        int width = 0;
        std::vector<Scalar> els;
        auto nextValue = [&]() -> std::string {
            const int v = (width + 1) * 10;
            ++width;
            els.push_back(scalarOfInt(elemInfo.kind, v));
            return abstractLiteral(v, abstractFloat);
        };
        std::vector<std::string> args;
        for (char c : sig) {
            if (c == '2') {
                const std::string a = nextValue();
                const std::string b = nextValue();
                args.push_back("vec2(" + a + ", " + b + ")");
            } else if (c == '3') {
                const std::string a = nextValue();
                const std::string b = nextValue();
                const std::string cc = nextValue();
                args.push_back("vec3(" + a + ", " + b + ", " + cc + ")");
            } else if (c == 's') {
                args.push_back(nextValue());
            }
        }
        std::string joined;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                joined += ", ";
            }
            joined += args[i];
        }
        const ExprType vt = vecType(width, elemInfo.kind);
        const std::string expr = "vec" + std::to_string(width) + "(" + joined + ") / 0x100000000";
        run(t, fixedExprNonZero(expr), {}, vt, InputSource::Const, 0,
            {Case({}, CaseValue::vec(els))});
    });

// ---- abstract_matrix_elements ----
CTS_TEST(testGroup, "abstract_matrix_elements")
    .params([](ParamsBuilder u) {
        return u.combine("concrete_type", {"f32", "f16"})
            .combine("columns", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                 Value(static_cast<int64_t>(4))})
            .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                              Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("concrete_type"));
        maybeSkipF16(t, k);
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));
        const ExprType mt = matType(cols, rows, k);

        std::string args;
        std::vector<Scalar> els;
        bool first = true;
        for (int c = 0; c < cols; ++c) {
            for (int r = 0; r < rows; ++r) {
                const int v = (c + 1) * 10 + (r + 1);
                if (!first) {
                    args += ", ";
                }
                first = false;
                args += std::to_string(static_cast<int64_t>(v) * kAbstractValueScale) + ".0";
                els.push_back(scalarOfInt(k, v));
            }
        }
        const std::string expr = "mat" + std::to_string(cols) + "x" + std::to_string(rows) + "(" +
                                 args + ") * (1.0 / 0x100000000)";
        run(t, fixedExprNonZero(expr), {}, mt, InputSource::Const, 0,
            {Case({}, CaseValue::vec(els))});
    });

// ---- abstract_matrix_column_vectors ----
CTS_TEST(testGroup, "abstract_matrix_column_vectors")
    .params([](ParamsBuilder u) {
        return u.combine("concrete_type", {"f32", "f16"})
            .combine("columns", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                 Value(static_cast<int64_t>(4))})
            .combine("rows", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                              Value(static_cast<int64_t>(4))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKind(t.param<std::string>("concrete_type"));
        maybeSkipF16(t, k);
        const int cols = static_cast<int>(t.param<int64_t>("columns"));
        const int rows = static_cast<int>(t.param<int64_t>("rows"));
        const ExprType mt = matType(cols, rows, k);

        std::vector<std::string> columnVectors;
        std::vector<Scalar> els;
        for (int c = 0; c < cols; ++c) {
            std::string columnElements;
            for (int r = 0; r < rows; ++r) {
                const int v = (c + 1) * 10 + (r + 1);
                if (r > 0) {
                    columnElements += ", ";
                }
                // abstract-int literals (no .0 suffix), matching upstream.
                columnElements += std::to_string(static_cast<int64_t>(v) * kAbstractValueScale);
                els.push_back(scalarOfInt(k, v));
            }
            columnVectors.push_back("vec" + std::to_string(rows) + "(" + columnElements + ")");
        }
        std::string joined;
        for (size_t i = 0; i < columnVectors.size(); ++i) {
            if (i > 0) {
                joined += ", ";
            }
            joined += columnVectors[i];
        }
        const std::string expr = "mat" + std::to_string(cols) + "x" + std::to_string(rows) + "(" +
                                 joined + ") * (1.0 / 0x100000000)";
        run(t, fixedExprNonZero(expr), {}, mt, InputSource::Const, 0,
            {Case({}, CaseValue::vec(els))});
    });

// ---- abstract_array_elements ----
CTS_TEST(testGroup, "abstract_array_elements")
    .params([](ParamsBuilder u) {
        return u.combine("abstract_type", {"abstract-int", "abstract-float", "vec3<abstract-int>",
                                           "vec4<abstract-float>", "mat2x3<abstract-float>"})
            .expand("concrete_type",
                    [](const ParamRecord& p) {
                        const Value* v = findParam(p, "abstract_type");
                        return v != nullptr ? concreteTypesForAbstractType(valueAs<std::string>(*v))
                                            : std::vector<Value>{};
                    })
            .combine("length", {Value(static_cast<int64_t>(1)), Value(static_cast<int64_t>(5)),
                                Value(static_cast<int64_t>(10))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string abstractType = t.param<std::string>("abstract_type");
        const ConcreteTypeInfo elemInfo = parseConcreteType(t.param<std::string>("concrete_type"));
        maybeSkipF16Concrete(t, elemInfo.isF16);
        const int count = static_cast<int>(t.param<int64_t>("length"));
        const ExprType element = concreteExprType(elemInfo);
        const ExprType at = arrayType(count, element);

        int i = 0;
        auto nextValue = [&]() -> int { return ++i * 10; };
        std::vector<std::string> args;
        std::vector<Scalar> expected;
        for (int idx = 0; idx < count; ++idx) {
            if (abstractType == "abstract-int") {
                const int v = nextValue();
                args.push_back(std::to_string(v));
                expected.push_back(scalarOfInt(elemInfo.kind, v));
            } else if (abstractType == "abstract-float") {
                const int v = nextValue();
                args.push_back(std::to_string(v) + ".0");
                expected.push_back(scalarOfInt(elemInfo.kind, v));
            } else if (abstractType == "vec3<abstract-int>") {
                const int x = nextValue();
                const int y = nextValue();
                const int z = nextValue();
                args.push_back("vec3(" + std::to_string(x) + ", " + std::to_string(y) + ", " +
                               std::to_string(z) + ")");
                expected.push_back(scalarOfInt(elemInfo.kind, x));
                expected.push_back(scalarOfInt(elemInfo.kind, y));
                expected.push_back(scalarOfInt(elemInfo.kind, z));
            } else if (abstractType == "vec4<abstract-float>") {
                const int x = nextValue();
                const int y = nextValue();
                const int z = nextValue();
                const int w = nextValue();
                args.push_back("vec4(" + std::to_string(x) + ".0, " + std::to_string(y) + ".0, " +
                               std::to_string(z) + ".0, " + std::to_string(w) + ".0)");
                expected.push_back(scalarOfInt(elemInfo.kind, x));
                expected.push_back(scalarOfInt(elemInfo.kind, y));
                expected.push_back(scalarOfInt(elemInfo.kind, z));
                expected.push_back(scalarOfInt(elemInfo.kind, w));
            } else { // mat2x3<abstract-float>
                const int e00 = nextValue();
                const int e01 = nextValue();
                const int e02 = nextValue();
                const int e10 = nextValue();
                const int e11 = nextValue();
                const int e12 = nextValue();
                args.push_back("mat2x3(vec3(" + std::to_string(e00) + ".0, " + std::to_string(e01) +
                               ".0, " + std::to_string(e02) + ".0), vec3(" + std::to_string(e10) +
                               ".0, " + std::to_string(e11) + ".0, " + std::to_string(e12) +
                               ".0))");
                expected.push_back(scalarOfInt(elemInfo.kind, e00));
                expected.push_back(scalarOfInt(elemInfo.kind, e01));
                expected.push_back(scalarOfInt(elemInfo.kind, e02));
                expected.push_back(scalarOfInt(elemInfo.kind, e10));
                expected.push_back(scalarOfInt(elemInfo.kind, e11));
                expected.push_back(scalarOfInt(elemInfo.kind, e12));
            }
        }
        std::string joined;
        for (size_t j = 0; j < args.size(); ++j) {
            if (j > 0) {
                joined += ", ";
            }
            joined += args[j];
        }
        const std::string expr = "array(" + joined + ")";
        run(t, fixedExprNonZero(expr), {}, at, InputSource::Const, 0,
            {Case({}, CaseValue::composite(expected))});
    });

// ---- structure ----
// Parse a struct member type name into an ExprType (scalar/vec/mat).
ExprType structMemberType(const std::string& name) {
    if (name == "vec3f") {
        return vecType(3, ScalarKind::F32);
    }
    if (name == "vec4i") {
        return vecType(4, ScalarKind::I32);
    }
    if (name == "vec2i") {
        return vecType(2, ScalarKind::I32);
    }
    if (name == "mat3x2f") {
        return matType(3, 2, ScalarKind::F32);
    }
    return scalarType(scalarKind(name));
}

// Split "a,b,c" into {"a","b","c"}.
std::vector<std::string> splitTypes(const std::string& csv) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= csv.size()) {
        const size_t comma = csv.find(',', start);
        if (comma == std::string::npos) {
            out.push_back(csv.substr(start));
            break;
        }
        out.push_back(csv.substr(start, comma - start));
        start = comma + 1;
    }
    return out;
}

bool typesContainF16(const std::vector<std::string>& types) {
    for (const std::string& ty : types) {
        if (ty == "f16") {
            return true;
        }
    }
    return false;
}

// elementType.create(i): splat the integer `i` across all slots of the member's concrete type.
CaseValue memberCreateValue(const ExprType& ty, int value) {
    if (ty.form == TypeForm::Matrix) {
        std::vector<Scalar> els(static_cast<size_t>(ty.cols * ty.width),
                                scalarOfInt(ty.scalarKind(), value));
        return CaseValue::vec(els);
    }
    if (ty.form == TypeForm::ScalarVec && ty.width > 1) {
        std::vector<Scalar> els(static_cast<size_t>(ty.width), scalarOfInt(ty.scalarKind(), value));
        return CaseValue::vec(els);
    }
    return CaseValue(scalarOfInt(ty.scalarKind(), value));
}

CTS_TEST(testGroup, "structure")
    .params([](ParamsBuilder u) {
        return u.combine("member_types", {"bool", "u32", "vec3f", "i32,u32",
                                          "i32,f16,vec4i,mat3x2f", "bool,u32,f16,vec3f,vec2i",
                                          "i32,u32,f32,f16,vec3f,vec4i"})
            .combine("nested", {false, true})
            .beginSubcases()
            .expand("member_index", [](const ParamRecord& p) {
                const Value* v = findParam(p, "member_types");
                std::vector<Value> idx;
                if (v != nullptr) {
                    const size_t n = splitTypes(valueAs<std::string>(*v)).size();
                    for (size_t i = 0; i < n; ++i) {
                        idx.push_back(Value(static_cast<int64_t>(i)));
                    }
                }
                return idx;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::vector<std::string> types = splitTypes(t.param<std::string>("member_types"));
        const bool nested = t.param<bool>("nested");
        const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
        if (typesContainF16(types) && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        const ExprType memberType = structMemberType(types[static_cast<size_t>(memberIndex)]);

        // Build the param types and per-member input values (member i's value is i).
        std::vector<ExprType> params;
        std::vector<CaseValue> inputs;
        for (size_t i = 0; i < types.size(); ++i) {
            const ExprType mt = structMemberType(types[i]);
            params.push_back(mt);
            inputs.push_back(memberCreateValue(mt, static_cast<int>(i)));
        }
        const CaseValue expected = memberCreateValue(memberType, memberIndex);

        // Struct decls placed at module scope (WGSL is order-independent).
        std::string members;
        for (size_t i = 0; i < types.size(); ++i) {
            members += "  member_" + std::to_string(i) + " : " + types[i] + ",\n";
        }
        const std::string predeclaration =
            "struct MyStruct {\n" + members + "};\n" +
            "struct OuterStruct {\n  pre : i32,\n  inner : MyStruct,\n  post : i32,\n};";

        // Builder joins the operands into the struct constructor + member access.
        const std::string idx = std::to_string(memberIndex);
        ExpressionBuilder builder = [nested, idx](const std::vector<std::string>& ops) {
            std::string joined;
            for (size_t i = 0; i < ops.size(); ++i) {
                if (i > 0) {
                    joined += ", ";
                }
                joined += ops[i];
            }
            return nested ? ("OuterStruct(10, MyStruct(" + joined + "), 20).inner.member_" + idx)
                          : ("MyStruct(" + joined + ").member_" + idx);
        };

        runWithPredeclaration(t, builder, predeclaration, params, memberType, InputSource::Const, 0,
                              {Case(inputs, expected)});
    });

} // namespace
