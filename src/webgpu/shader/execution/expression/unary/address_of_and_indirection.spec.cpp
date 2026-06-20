// Ported from gpuweb/cts src/webgpu/shader/execution/expression/unary/address_of_and_indirection.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution Tests for unary address-of and indirection (dereference). Each case round-trips a value
// through a predeclared helper function that takes the address of a local and dereferences it. The
// values are EXACT (pure copy, no arithmetic), so no FP interval is needed. f16 source variants are
// skipped at runtime when the shader-f16 feature is absent (no Metal oracle), mirroring upstream.

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "common/webgpu/backend.h"
#include "webgpu/shader/execution/expression/expression.h"
#include "webgpu/shader/execution/expression/floating_point.h"

using namespace cts;
using namespace cts::expression;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,unary,address_of_and_indirection",
    "Execution Tests for unary address-of and indirection (dereference)");

// All the ways to deref an expression (kDerefCases). 'requiresPCA' = requires pointer_composite_access.
struct DerefCase {
    const char* name;
    const char* wgsl;
    bool requiresPCA;
};
const DerefCase kDerefCases[] = {
    {"deref_address_of_identifier", "(*(&a))", false},
    {"deref_pointer", "(*p)", false},
    {"address_of_identifier", "(&a)", true},
    {"pointer", "p", true},
};

const DerefCase& derefCaseByName(const std::string& name) {
    for (const DerefCase& c : kDerefCases) {
        if (name == c.name) {
            return c;
        }
    }
    return kDerefCases[0];
}

ScalarKind scalarKindOf(const std::string& name) {
    if (name == "bool") {
        return ScalarKind::Bool;
    }
    if (name == "u32") {
        return ScalarKind::U32;
    }
    if (name == "i32") {
        return ScalarKind::I32;
    }
    if (name == "f32") {
        return ScalarKind::F32;
    }
    return ScalarKind::F16;
}

InputSource cfgInputSource(const Fixture& t) {
    return inputSourceFromParam(t.param<std::string>("inputSource"));
}
int cfgVectorize(const Fixture& t) {
    return t.paramIsUndefined("vectorize") ? 0 : static_cast<int>(t.param<int64_t>("vectorize"));
}

// allButConstInputSource: every input source except 'const'.
ParamsBuilder allButConstParams(ParamsBuilder u) {
    return u.combine("inputSource", {"uniform", "storage_r", "storage_rw"});
}

// f16 (binary16) bit pattern of a finite double (round-to-nearest-even). Mirrors the constructor
// port's helper; used to build f16 scalars whose value == the f16-quantized input (exact data move).
uint16_t f16BitsOfDoubleLocal(double value) {
    if (value == 0.0) {
        return std::signbit(value) ? 0x8000u : 0x0000u;
    }
    const uint32_t sign = value < 0.0 ? 0x8000u : 0x0000u;
    double mag = value < 0.0 ? -value : value;
    int e = 0;
    while (mag >= 2.0) {
        mag /= 2.0;
        ++e;
    }
    while (mag < 1.0) {
        mag *= 2.0;
        --e;
    }
    if (e > 15) {
        // Overflow -> infinity for this exact-move test (input range stays within f16 normal range).
        return static_cast<uint16_t>(sign | 0x7C00u);
    }
    if (e < -14) {
        // Subnormal/underflow: round (mag * 2^e) into the 2^-24 subnormal grid.
        const double subScaled = (mag * std::ldexp(1.0, e)) / std::ldexp(1.0, -24);
        uint32_t mant = static_cast<uint32_t>(subScaled);
        const double frac = subScaled - static_cast<double>(mant);
        if (frac > 0.5 || (frac == 0.5 && (mant & 1u) != 0)) {
            ++mant;
        }
        return static_cast<uint16_t>(sign | (mant & 0x3FFu));
    }
    const double scaled = (mag - 1.0) * 1024.0;
    uint32_t mant = static_cast<uint32_t>(scaled);
    const double frac = scaled - static_cast<double>(mant);
    if (frac > 0.5 || (frac == 0.5 && (mant & 1u) != 0)) {
        ++mant;
        if (mant == 1024u) {
            mant = 0;
            ++e;
        }
    }
    const uint32_t biasedExp = static_cast<uint32_t>(e + 15);
    return static_cast<uint16_t>(sign | (biasedExp << 10) | (mant & 0x3FFu));
}

// ty.create(e): build a scalar of 'k' from an f32-range double. JS TypedArray coercion semantics:
// i32/u32 truncate toward zero (mod 2^32); f32/f16 quantize; bool is e != 0. Input == expected
// (exact data movement). f16 must produce a genuine f16 scalar (16-bit pattern), NOT an f32 scalar,
// or the readback (16-bit f16) will mismatch the (32-bit f32) expected value.
Scalar createScalar(ScalarKind k, double e) {
    switch (k) {
        case ScalarKind::Bool:
            return boolean(e != 0.0);
        case ScalarKind::I32: {
            // ToInt32: truncate toward zero, take low 32 bits as signed.
            const int64_t t = static_cast<int64_t>(std::trunc(e));
            return i32(static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(t))));
        }
        case ScalarKind::U32: {
            const int64_t t = static_cast<int64_t>(std::trunc(e));
            return u32(static_cast<uint32_t>(static_cast<uint64_t>(t)));
        }
        case ScalarKind::F16:
            return f16Bits(f16BitsOfDoubleLocal(e));
        case ScalarKind::F32:
        default: {
            float f = static_cast<float>(fp::quantize(fp::FPKind::F32, e));
            uint32_t bits;
            std::memcpy(&bits, &f, 4);
            return f32Bits(bits);
        }
    }
}

// Build the round-trip cases over sparseScalarF32Range() (input == expected).
std::vector<Case> derefCases(ScalarKind k) {
    std::vector<Case> cases;
    for (double e : fp::sparseScalarF32Range()) {
        const Scalar s = createScalar(k, e);
        cases.push_back({{CaseValue(s)}, CaseValue(s)});
    }
    return cases;
}

// The element type spelling and vectorized type spelling.
std::string elemTypeName(ScalarKind k) {
    switch (k) {
        case ScalarKind::Bool:
            return "bool";
        case ScalarKind::U32:
            return "u32";
        case ScalarKind::I32:
            return "i32";
        case ScalarKind::F32:
            return "f32";
        default:
            return "f16";
    }
}
std::string typeName(ScalarKind k, int vectorize) {
    const std::string e = elemTypeName(k);
    return vectorize == 0 ? e : ("vec" + std::to_string(vectorize) + "<" + e + ">");
}

// The expression builder calls the predeclared helper.
ExpressionBuilder derefBuilder() {
    return [](const std::vector<std::string>& ops) {
        return "get_dereferenced_value(" + ops[0] + ")";
    };
}

bool maybeSkipF16(AllFeaturesMaxLimitsGpuTest& t, ScalarKind k) {
    if (k == ScalarKind::F16 && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
        t.skip("shader-f16 feature not available");
        return true;
    }
    return false;
}

// Mirrors upstream t.hasLanguageFeature('pointer_composite_access') via a one-shot probe instance
// (language features are an instance-level property; the probe answer matches the harness instance).
// Guarded for Dawn: only Dawn's lib exports wgpuInstanceHasWGSLLanguageFeature; on the non-Dawn
// backends the feature cannot be queried, so it is treated as unsupported (the PCA cases skip,
// matching upstream on implementations without the feature). The Dawn oracle is authoritative here.
bool hasPCA() {
#if defined(CTS_BACKEND_DAWN)
    static const bool supported = [] {
        WGPUInstance probe = createInstance();
        if (probe == nullptr) {
            return false;
        }
        const bool has = wgpuInstanceHasWGSLLanguageFeature(
                             probe, WGPUWGSLLanguageFeatureName_PointerCompositeAccess) != 0u;
        wgpuInstanceRelease(probe);
        return has;
    }();
    return supported;
#else
    return false;
#endif
}

ParamsBuilder scalarAndDerefParams(ParamsBuilder u) {
    return u.combine("scalarType", {"bool", "u32", "i32", "f32", "f16"})
        .combine("derefType", {"deref_address_of_identifier", "deref_pointer",
                               "address_of_identifier", "pointer"});
}
ParamsBuilder vectorizeAll(ParamsBuilder u) {
    return u.combine("vectorize", {Value(), Value(static_cast<int64_t>(2)),
                                   Value(static_cast<int64_t>(3)), Value(static_cast<int64_t>(4))});
}

} // namespace

// Expression: *e -- pointer dereference. The 'deref' test FILTERS out the cases requiring
// pointer_composite_access (they cannot be expressed as plain *e), so only the non-PCA derefTypes.
CTS_TEST(testGroup, "deref")
    .params([](ParamsBuilder u) {
        return allButConstParams(vectorizeAll(u))
            .combine("scalarType", {"bool", "u32", "i32", "f32", "f16"})
            .combine("derefType", {"deref_address_of_identifier", "deref_pointer",
                                   "address_of_identifier", "pointer"})
            .filter([](const ParamRecord& p) {
                const Value* v = findParam(p, "derefType");
                return v != nullptr && !derefCaseByName(valueAs<std::string>(*v)).requiresPCA;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ScalarKind k = scalarKindOf(t.param<std::string>("scalarType"));
        if (maybeSkipF16(t, k)) {
            return;
        }
        const int vec = cfgVectorize(t);
        const std::string type = typeName(k, vec);
        const DerefCase& dc = derefCaseByName(t.param<std::string>("derefType"));
        const std::string predeclaration =
            "fn get_dereferenced_value(value: " + type + ") -> " + type + " {\n"
            "  var a = value;\n"
            "  let p = &a;\n"
            "  return " + std::string(dc.wgsl) + ";\n"
            "}";
        const ExprType ty = vec == 0 ? scalarType(k) : vecType(vec, k);
        runWithPredeclaration(t, derefBuilder(), predeclaration, {ty}, ty, cfgInputSource(t), vec,
                              derefCases(k));
    });

// Expression: (*e)[index] -- deref as lhs of index accessor.
CTS_TEST(testGroup, "deref_index")
    .params([](ParamsBuilder u) { return scalarAndDerefParams(allButConstParams(vectorizeAll(u))); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const DerefCase& dc = derefCaseByName(t.param<std::string>("derefType"));
        if (dc.requiresPCA && !hasPCA()) {
            t.skip("pointer_composite_access not supported");
            return;
        }
        const ScalarKind k = scalarKindOf(t.param<std::string>("scalarType"));
        if (maybeSkipF16(t, k)) {
            return;
        }
        const int vec = cfgVectorize(t);
        const std::string type = typeName(k, vec);
        const std::string predeclaration =
            "fn get_dereferenced_value(value: " + type + ") -> " + type + " {\n"
            "  var a = array<" + type + ", 1>(value);\n"
            "  let p = &a;\n"
            "  return " + std::string(dc.wgsl) + "[0];\n"
            "}";
        const ExprType ty = vec == 0 ? scalarType(k) : vecType(vec, k);
        runWithPredeclaration(t, derefBuilder(), predeclaration, {ty}, ty, cfgInputSource(t), vec,
                              derefCases(k));
    });

// Expression: (*e).member -- deref as lhs of member accessor.
CTS_TEST(testGroup, "deref_member")
    .params([](ParamsBuilder u) { return scalarAndDerefParams(allButConstParams(vectorizeAll(u))); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const DerefCase& dc = derefCaseByName(t.param<std::string>("derefType"));
        if (dc.requiresPCA && !hasPCA()) {
            t.skip("pointer_composite_access not supported");
            return;
        }
        const ScalarKind k = scalarKindOf(t.param<std::string>("scalarType"));
        if (maybeSkipF16(t, k)) {
            return;
        }
        const int vec = cfgVectorize(t);
        const std::string type = typeName(k, vec);
        const std::string predeclaration =
            "struct S {\n  m : " + type + "\n}\n"
            "fn get_dereferenced_value(value: " + type + ") -> " + type + " {\n"
            "  var a = S(value);\n"
            "  let p = &a;\n"
            "  return " + std::string(dc.wgsl) + ".m;\n"
            "}";
        const ExprType ty = vec == 0 ? scalarType(k) : vecType(vec, k);
        runWithPredeclaration(t, derefBuilder(), predeclaration, {ty}, ty, cfgInputSource(t), vec,
                              derefCases(k));
    });

// Expression: (*e).swizzle -- deref as lhs of swizzle. vectorize is 2/3/4 only (no scalar).
CTS_TEST(testGroup, "deref_swizzle")
    .params([](ParamsBuilder u) {
        return allButConstParams(u)
            .combine("vectorize", {Value(static_cast<int64_t>(2)), Value(static_cast<int64_t>(3)),
                                   Value(static_cast<int64_t>(4))})
            .combine("scalarType", {"bool", "u32", "i32", "f32", "f16"})
            .combine("derefType", {"deref_address_of_identifier", "deref_pointer",
                                   "address_of_identifier", "pointer"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const DerefCase& dc = derefCaseByName(t.param<std::string>("derefType"));
        if (dc.requiresPCA && !hasPCA()) {
            t.skip("pointer_composite_access not supported");
            return;
        }
        const ScalarKind k = scalarKindOf(t.param<std::string>("scalarType"));
        if (maybeSkipF16(t, k)) {
            return;
        }
        const int vec = static_cast<int>(t.param<int64_t>("vectorize"));
        const std::string type = "vec" + std::to_string(vec) + "<" + elemTypeName(k) + ">";
        const std::string swizzle = std::string("xyzw").substr(0, static_cast<size_t>(vec));
        const std::string predeclaration =
            "fn get_dereferenced_value(value: " + type + ") -> " + type + " {\n"
            "  var a = value;\n"
            "  let p = &a;\n"
            "  return " + std::string(dc.wgsl) + "." + swizzle + ";\n"
            "}";
        const ExprType ty = vecType(vec, k);
        runWithPredeclaration(t, derefBuilder(), predeclaration, {ty}, ty, cfgInputSource(t), vec,
                              derefCases(k));
    });
