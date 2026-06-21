// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/parse/literal.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting note: upstream uses JS `Set` union/dedup (`new Set([...a, ...b])`).
// This port reproduces the same ordered-dedup semantics via `unionSets` (first
// occurrence wins, insertion order preserved), so the expanded `val` case list
// (and its order) matches upstream exactly. Membership in the "valid" set decides
// the expected compile result.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,parse,literal",
    "Validation tests for literals");

// Ordered set helpers (insertion order, first occurrence wins).
static bool contains(const std::vector<std::string>& set, const std::string& v) {
    for (const std::string& s : set) {
        if (s == v) {
            return true;
        }
    }
    return false;
}

static void appendUnique(std::vector<std::string>& out, const std::vector<std::string>& src) {
    for (const std::string& s : src) {
        if (!contains(out, s)) {
            out.push_back(s);
        }
    }
}

static std::vector<std::string> unionSets(std::initializer_list<std::vector<std::string>> sets) {
    std::vector<std::string> out;
    for (const std::vector<std::string>& s : sets) {
        appendUnique(out, s);
    }
    return out;
}

static std::vector<Value> toValues(const std::vector<std::string>& src) {
    std::vector<Value> values;
    for (const std::string& s : src) {
        values.emplace_back(s);
    }
    return values;
}

static bool hasH(const std::string& s) {
    return s.find('h') != std::string::npos;
}

// ---------------------------------------------------------------------------
// bools
// ---------------------------------------------------------------------------
CTS_TEST(g, "bools")
    .desc("Test that valid bools are accepted.")
    .params([](ParamsBuilder u) {
        return u.combine("val", {"true", "false"}).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string code = "var test = " + t.param<std::string>("val") + ";";
        t.expectCompileResult(true, t.wrapInEntryPoint(code));
    });

// ---------------------------------------------------------------------------
// Shared integer sets (mirrors upstream module-scope const Sets).
// ---------------------------------------------------------------------------
static const std::vector<std::string>& kAbstractIntNonNegative() {
    static const std::vector<std::string> v = {"0x123", "123", "0", "0x3f", "2147483647"};
    return v;
}
static const std::vector<std::string>& kAbstractIntNegative() {
    static const std::vector<std::string> v = {"-0x123", "-123", "-0x3f", "-2147483647",
                                               "-2147483648"};
    return v;
}
static const std::vector<std::string>& kI32() {
    static const std::vector<std::string> v = {"94i", "2147483647i", "-2147483647i",
                                               "i32(-2147483648)"};
    return v;
}
static const std::vector<std::string>& kU32() {
    static const std::vector<std::string> v = {"42u", "0u", "4294967295u"};
    return v;
}

// ---------------------------------------------------------------------------
// abstract_int
// ---------------------------------------------------------------------------
CTS_TEST(g, "abstract_int")
    .desc("Test that valid integers are accepted, and invalid integers are rejected.")
    .params([](ParamsBuilder u) {
        const std::vector<std::string> valid =
            unionSets({kAbstractIntNonNegative(), kAbstractIntNegative(), kI32(), kU32()});
        const std::vector<std::string> invalid = {
            "0123", "2147483648i", "-2147483649i", "4294967295", "4294967295i", "4294967296u",
            "-1u"};
        return u.combine("val", toValues(unionSets({valid, invalid}))).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::vector<std::string> valid =
            unionSets({kAbstractIntNonNegative(), kAbstractIntNegative(), kI32(), kU32()});
        const std::string val = t.param<std::string>("val");
        const std::string code = "var test = " + val + ";";
        t.expectCompileResult(contains(valid, val), t.wrapInEntryPoint(code));
    });

// ---------------------------------------------------------------------------
// i32
// ---------------------------------------------------------------------------
CTS_TEST(g, "i32")
    .desc("Test that valid signed integers are accepted, and invalid signed integers are rejected.")
    .params([](ParamsBuilder u) {
        const std::vector<std::string> validI32 =
            unionSets({kAbstractIntNonNegative(), kAbstractIntNegative(), kI32()});
        const std::vector<std::string> invalidI32 =
            unionSets({kU32(),
                       {"2147483648", "2147483648i", "-2147483649", "-2147483649i", "1.0", "1.0f",
                        "1.0h"}});
        return u.combine("val", toValues(unionSets({validI32, invalidI32}))).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::vector<std::string> validI32 =
            unionSets({kAbstractIntNonNegative(), kAbstractIntNegative(), kI32()});
        const std::string val = t.param<std::string>("val");
        const std::string code = "var test: i32 = " + val + ";";
        std::vector<std::string> ext;
        if (hasH(val)) {
            ext.push_back("f16");
        }
        t.expectCompileResult(contains(validI32, val), t.wrapInEntryPoint(code, ext));
    });

// ---------------------------------------------------------------------------
// u32
// ---------------------------------------------------------------------------
CTS_TEST(g, "u32")
    .desc("Test that valid unsigned integers are accepted, and invalid unsigned integers are "
          "rejected.")
    .params([](ParamsBuilder u) {
        const std::vector<std::string> validU32 =
            unionSets({kAbstractIntNonNegative(), kU32(), {"4294967295"}});
        const std::vector<std::string> invalidU32 =
            unionSets({kAbstractIntNegative(), kI32(),
                       {"4294967296", "4294967296u", "-1", "1.0", "1.0f", "1.0h"}});
        return u.combine("val", toValues(unionSets({validU32, invalidU32}))).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::vector<std::string> validU32 =
            unionSets({kAbstractIntNonNegative(), kU32(), {"4294967295"}});
        const std::string val = t.param<std::string>("val");
        const std::string code = "var test: u32 = " + val + ";";
        std::vector<std::string> ext;
        if (hasH(val)) {
            ext.push_back("f16");
        }
        t.expectCompileResult(contains(validU32, val), t.wrapInEntryPoint(code, ext));
    });

// ---------------------------------------------------------------------------
// Shared float sets.
// ---------------------------------------------------------------------------
static const std::vector<std::string>& kF32() {
    static const std::vector<std::string> v = {
        "0f",   "0.0f",   "12.223f", "12.f",  ".12f",
        "2.4e+4f", "2.4e-2f", "2.e+4f", "1e-4f", "0x1P+4f"};
    return v;
}
static const std::vector<std::string>& kF16() {
    static const std::vector<std::string> v = {
        "0h",     "1h",      ".1h",      "1.1e2h",   "1.1E+2h",
        "2.4e-2h", "0xep2h",  "0xEp-2h", "0x3p+2h", "0x3.2p+2h"};
    return v;
}
static const std::vector<std::string>& kAbstractFloat() {
    static const std::vector<std::string> v = {
        "0.0",    ".0",      "12.",      "00012.",   ".12",
        "1.2e2",  "1.2E2",   "1.2e+2",   "2.4e-2",   ".1e-2",
        "0x.3",   "0X.3",    "0xa.fp+2", "0xa.fP+2", "0xE.fp+2",
        "0X1.fp-4"};
    return v;
}

// ---------------------------------------------------------------------------
// abstract_float
// ---------------------------------------------------------------------------
CTS_TEST(g, "abstract_float")
    .desc("Test that valid floats are accepted, and invalid floats are rejected")
    .params([](ParamsBuilder u) {
        const std::vector<std::string> valid = unionSets({kF32(), kF16(), kAbstractFloat()});
        const std::vector<std::string> invalid = {
            ".f",     ".e-2",     "1.e&2f", "1.ef",  "1.e+f",
            "0x.p2",  "0x1p",     "0x1p^",  "1.0e+999999999999f", "0x1.0p+999999999999f",
            "0x1.00000001pf0"};
        const std::vector<std::string> invalidF16 = {
            "1.1eh",  "1.1e!2h", "1.1e+h", "1.0e+999999h", "0x1.0p+999999h", "0xf.h", "0x3h"};
        return u.combine("val", toValues(unionSets({valid, invalid, invalidF16})))
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::vector<std::string> validFloats = unionSets({kF32(), kF16(), kAbstractFloat()});
        const std::vector<std::string> invalidF16 = {
            "1.1eh",  "1.1e!2h", "1.1e+h", "1.0e+999999h", "0x1.0p+999999h", "0xf.h", "0x3h"};
        const std::string val = t.param<std::string>("val");
        const std::string code = "var test = " + val + ";";
        std::vector<std::string> ext;
        if (contains(kF16(), val) || contains(invalidF16, val)) {
            ext.push_back("f16");
        }
        t.expectCompileResult(contains(validFloats, val), t.wrapInEntryPoint(code, ext));
    });

// ---------------------------------------------------------------------------
// f32
// ---------------------------------------------------------------------------
CTS_TEST(g, "f32")
    .desc("Test that valid floats are accepted, and invalid floats are rejected")
    .params([](ParamsBuilder u) {
        const std::vector<std::string> validF32 =
            unionSets({kF32(), kAbstractFloat(), {"1", "-1"}});
        const std::vector<std::string> invalidF32 = unionSets(
            {kF16(),
             {"1u",    "1i",     "1h",     ".f",     ".e-2",   "1.e&2f", "1.ef",   "1.e+f",
              "0x.p2", "0x1p",   "0x1p^",  "1.0e+999999999999f", "0x1.0p+999999999999f",
              "0x1.00000001pf0"}});
        return u.combine("val", toValues(unionSets({validF32, invalidF32}))).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::vector<std::string> validF32 =
            unionSets({kF32(), kAbstractFloat(), {"1", "-1"}});
        const std::string val = t.param<std::string>("val");
        const std::string code = "var test: f32 = " + val + ";";
        std::vector<std::string> ext;
        if (contains(kF16(), val)) {
            ext.push_back("f16");
        }
        t.expectCompileResult(contains(validF32, val), t.wrapInEntryPoint(code, ext));
    });

// ---------------------------------------------------------------------------
// f16
// ---------------------------------------------------------------------------
CTS_TEST(g, "f16")
    .desc("\nTest that valid half floats are accepted, and invalid half floats are rejected\n")
    .params([](ParamsBuilder u) {
        const std::vector<std::string> validF16 =
            unionSets({kF16(), kAbstractFloat(), {"1", "-1"}});
        const std::vector<std::string> invalidF16 = unionSets(
            {kF32(),
             {"1i",    "1u",     "1f",     "1.1eh",  "1.1e!2h", "1.1e+h", "1.0e+999999h",
              "0x1.0p+999999h"}});
        return u.combine("val", toValues(unionSets({validF16, invalidF16}))).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::vector<std::string> validF16 =
            unionSets({kF16(), kAbstractFloat(), {"1", "-1"}});
        const std::string val = t.param<std::string>("val");
        const std::string code = "var test: f16 = " + val + ";";
        std::vector<std::string> ext = {"f16"};
        t.expectCompileResult(contains(validF16, val), t.wrapInEntryPoint(code, ext));
    });

}  // namespace
