// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/functions/alias_analysis.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

// ---------------------------------------------------------------------------
// kUses: a use of a pointer/reference. `gen` substitutes the "%REF%" token with
// the reference expression. `isWrite` marks whether the use mutates the memory.
// (Mirrors upstream kUses, key order preserved.)
// ---------------------------------------------------------------------------
struct Use {
    const char* name;
    bool isWrite;
    const char* gen;  // template with %REF% placeholder
};

static const std::vector<Use>& kUses() {
    static const std::vector<Use> uses = {
        {"no_access", false, "{ let p = &*&%REF%; }"},
        {"assign", true, "%REF% = 42;"},
        {"compound_assign_lhs", true, "%REF% += 1;"},
        {"compound_assign_rhs", false, "{ var tmp : i32; tmp += %REF%; }"},
        {"increment", true, "%REF%++;"},
        {"binary_lhs", false, "_ = %REF% + 1;"},
        {"binary_rhs", false, "_ = 1 + %REF%;"},
        {"unary_minus", false, "_ = -%REF%;"},
        {"bitcast", false, "_ = bitcast<f32>(%REF%);"},
        {"convert", false, "_ = f32(%REF%);"},
        {"builtin_arg", false, "_ = abs(%REF%);"},
        {"index_access", false, "{ var arr : array<i32, 4>; _ = arr[%REF%]; }"},
        {"let_init", false, "{ let tmp = %REF%; }"},
        {"var_init", false, "{ var tmp = %REF%; }"},
        {"return", false, "{ return %REF%; }"},
        {"switch_cond", false, "switch(%REF%) { default { break; } }"},
    };
    return uses;
}

static std::vector<Value> useNames() {
    std::vector<Value> values;
    for (const Use& u : kUses()) {
        values.emplace_back(std::string(u.name));
    }
    return values;
}

static const Use& findUse(const std::string& name) {
    for (const Use& u : kUses()) {
        if (name == u.name) {
            return u;
        }
    }
    static const Use dummy{"", false, ""};
    return dummy;
}

static std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static std::string genUse(const std::string& useName, const std::string& ref) {
    return replaceAll(findUse(useName).gen, "%REF%", ref);
}

// shouldPass: expect fail if the pointers are aliased and at least one of the
// accesses is a write; if either access is "no access" then expect pass.
static bool shouldPass2(bool aliased, const std::string& a, const std::string& b) {
    const bool anyWrite = findUse(a).isWrite || findUse(b).isWrite;
    return !aliased || !anyWrite || a == "no_access" || b == "no_access";
}

// ptr() — mirrors upstream ptr(addressSpace, type).
static std::string ptr(const std::string& addressSpace, const std::string& type) {
    if (addressSpace == "function") {
        return "ptr<function, " + type + ">";
    } else if (addressSpace == "private") {
        return "ptr<private, " + type + ">";
    } else if (addressSpace == "storage") {
        return "ptr<storage, " + type + ", read_write>";
    } else if (addressSpace == "uniform") {
        return "ptr<uniform, " + type + ">";
    } else {  // workgroup
        return "ptr<workgroup, " + type + ">";
    }
}

// declareModuleScopeVar — mirrors upstream declareModuleScopeVar.
static std::string declareModuleScopeVar(const std::string& name,
                                         const std::string& addressSpace,
                                         const std::string& type) {
    const std::string binding = (name == "x") ? "0" : "1";
    if (addressSpace == "private") {
        return "var<private> " + name + " : " + type + ";";
    } else if (addressSpace == "storage") {
        return "@binding(" + binding + ") @group(0) var<storage, read_write> " + name + " : " +
               type + ";";
    } else if (addressSpace == "uniform") {
        return "@binding(" + binding + ") @group(0) var<uniform> " + name + " : " + type + ";";
    } else {  // workgroup
        return "var<workgroup> " + name + " : " + type + ";";
    }
}

static std::string maybeDeclareModuleScopeVar(const std::string& name,
                                             const std::string& addressSpace,
                                             const std::string& type) {
    if (addressSpace == "function") {
        return "";
    }
    return declareModuleScopeVar(name, addressSpace, type);
}

static std::string maybeDeclareFunctionScopeVar(const std::string& name,
                                               const std::string& addressSpace,
                                               const std::string& type) {
    if (addressSpace == "function") {
        return "var " + name + " : " + type + ";";
    }
    return "";
}

// Returns true if a pointer of the given address space requires the
// 'unrestricted_pointer_parameters' language feature.
static bool requiresUnrestrictedPointerParameters(const std::string& addressSpace) {
    return addressSpace != "function" && addressSpace != "private";
}

// Mirrors upstream t.skipIfLanguageFeatureNotSupported(name).
static void skipIfLanguageFeatureNotSupported(ShaderValidationTest& t, const char* feature) {
    if (!t.hasLanguageFeature(feature)) {
        t.skip(std::string("Language feature not supported: ") + feature);
    }
}

const std::vector<std::string> kWritableAddressSpaces = {"private", "function", "storage",
                                                        "workgroup"};

static std::vector<Value> writableAddressSpaces() {
    std::vector<Value> v;
    for (const std::string& s : kWritableAddressSpaces) {
        v.emplace_back(s);
    }
    return v;
}

// ---------------------------------------------------------------------------
// Atomic builtins (mirrors upstream kAtomicBuiltins).
// ---------------------------------------------------------------------------
const std::vector<std::string> kAtomicBuiltins = {
    "atomicLoad",     "atomicStore", "atomicAdd", "atomicSub",
    "atomicMax",      "atomicMin",   "atomicAnd", "atomicOr",
    "atomicXor",      "atomicExchange", "atomicCompareExchangeWeak"};

static std::vector<Value> atomicBuiltins() {
    std::vector<Value> v;
    for (const std::string& s : kAtomicBuiltins) {
        v.emplace_back(s);
    }
    return v;
}

static bool atomicIsWrite(const std::string& builtin) {
    return builtin != "atomicLoad";
}

static std::string callAtomicBuiltin(const std::string& builtin, const std::string& ptrExpr) {
    if (builtin == "atomicLoad") {
        return "i += " + builtin + "(" + ptrExpr + ")";
    } else if (builtin == "atomicStore") {
        return builtin + "(" + ptrExpr + ", 42)";
    } else if (builtin == "atomicCompareExchangeWeak") {
        return builtin + "(" + ptrExpr + ", 10, 42)";
    } else {
        // atomicAdd/Sub/Max/Min/And/Or/Xor/Exchange
        return "i += " + builtin + "(" + ptrExpr + ", 42)";
    }
}

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,functions,alias_analysis", "Validation tests for function alias analysis");

CTS_TEST(g, "two_pointers")
    .desc("Test aliasing of two pointers passed to a function.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", writableAddressSpaces())
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", useNames())
            .combine("b_use", useNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const std::string bUse = t.param<std::string>("b_use");

        if (requiresUnrestrictedPointerParameters(addressSpace)) {
            skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");
        }

        const std::string code =
            "\n" + maybeDeclareModuleScopeVar("x", addressSpace, "i32") +
            "\n" + maybeDeclareModuleScopeVar("y", addressSpace, "i32") +
            "\n\nfn callee(pa : " + ptr(addressSpace, "i32") +
            ",\n          pb : " + ptr(addressSpace, "i32") + ") -> i32 {\n  " +
            genUse(aUse, "*pa") + "\n  " + genUse(bUse, "*pb") +
            "\n  return 0;\n}\n\nfn caller() {\n  " +
            maybeDeclareFunctionScopeVar("x", addressSpace, "i32") + "\n  " +
            maybeDeclareFunctionScopeVar("y", addressSpace, "i32") + "\n  callee(&x, " +
            (aliased ? "&x" : "&y") + ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, bUse), code);
    });

CTS_TEST(g, "two_pointers_to_array_elements")
    .desc("Test aliasing of two array element pointers passed to a function.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", writableAddressSpaces())
            .combine("index", {0, 1})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", useNames())
            .combine("b_use", useNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const int index = t.param<int>("index");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const std::string bUse = t.param<std::string>("b_use");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        const std::string code =
            "\n" + maybeDeclareModuleScopeVar("x", addressSpace, "array<i32, 4>") +
            "\n" + maybeDeclareModuleScopeVar("y", addressSpace, "array<i32, 4>") +
            "\n\nfn callee(pa : " + ptr(addressSpace, "i32") +
            ",\n          pb : " + ptr(addressSpace, "i32") + ") -> i32 {\n  " +
            genUse(aUse, "*pa") + "\n  " + genUse(bUse, "*pb") +
            "\n  return 0;\n}\n\nfn caller() {\n  " +
            maybeDeclareFunctionScopeVar("x", addressSpace, "array<i32, 4>") + "\n  " +
            maybeDeclareFunctionScopeVar("y", addressSpace, "array<i32, 4>") + "\n  callee(&x[" +
            std::to_string(index) + "], " + (aliased ? "&x[0]" : "&y[0]") + ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, bUse), code);
    });

CTS_TEST(g, "two_pointers_to_array_elements_indirect")
    .desc(
        "Test aliasing of two array pointers passed to a function, which indexes those arrays and "
        "then\npasses the element pointers to another function.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", writableAddressSpaces())
            .combine("index", {0, 1})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", useNames())
            .combine("b_use", useNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const int index = t.param<int>("index");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const std::string bUse = t.param<std::string>("b_use");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        const std::string code =
            "\n" + maybeDeclareModuleScopeVar("x", addressSpace, "array<i32, 4>") +
            "\n" + maybeDeclareModuleScopeVar("y", addressSpace, "array<i32, 4>") +
            "\n\nfn callee(pa : " + ptr(addressSpace, "i32") +
            ",\n          pb : " + ptr(addressSpace, "i32") + ") -> i32 {\n  " +
            genUse(aUse, "*pa") + "\n  " + genUse(bUse, "*pb") +
            "\n  return 0;\n}\n\nfn index(pa : " + ptr(addressSpace, "array<i32, 4>") +
            ",\n         pb : " + ptr(addressSpace, "array<i32, 4>") +
            ") -> i32 {\n  return callee(&(*pa)[" + std::to_string(index) +
            "], &(*pb)[0]);\n}\n\nfn caller() {\n  " +
            maybeDeclareFunctionScopeVar("x", addressSpace, "array<i32, 4>") + "\n  " +
            maybeDeclareFunctionScopeVar("y", addressSpace, "array<i32, 4>") + "\n  index(&x, " +
            (aliased ? "&x" : "&y") + ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, bUse), code);
    });

CTS_TEST(g, "two_pointers_to_struct_members")
    .desc("Test aliasing of two struct member pointers passed to a function.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", writableAddressSpaces())
            .combine("member", {"a", "b"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", useNames())
            .combine("b_use", useNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const std::string member = t.param<std::string>("member");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const std::string bUse = t.param<std::string>("b_use");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        const std::string code =
            "\nstruct S {\n  a : i32,\n  b : i32,\n}\n\n" +
            maybeDeclareModuleScopeVar("x", addressSpace, "S") + "\n" +
            maybeDeclareModuleScopeVar("y", addressSpace, "S") +
            "\n\nfn callee(pa : " + ptr(addressSpace, "i32") +
            ",\n          pb : " + ptr(addressSpace, "i32") + ") -> i32 {\n  " +
            genUse(aUse, "*pa") + "\n  " + genUse(bUse, "*pb") +
            "\n  return 0;\n}\n\nfn caller() {\n  " +
            maybeDeclareFunctionScopeVar("x", addressSpace, "S") + "\n  " +
            maybeDeclareFunctionScopeVar("y", addressSpace, "S") + "\n  callee(&x." + member +
            ", " + (aliased ? "&x.a" : "&y.a") + ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, bUse), code);
    });

CTS_TEST(g, "two_pointers_to_struct_members_indirect")
    .desc(
        "Test aliasing of two structure pointers passed to a function, which accesses members of "
        "those\nstructures and then passes the member pointers to another function.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", writableAddressSpaces())
            .combine("member", {"a", "b"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", useNames())
            .combine("b_use", useNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const std::string member = t.param<std::string>("member");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const std::string bUse = t.param<std::string>("b_use");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        const std::string code =
            "\nstruct S {\n  a : i32,\n  b : i32,\n}\n\n" +
            maybeDeclareModuleScopeVar("x", addressSpace, "S") + "\n" +
            maybeDeclareModuleScopeVar("y", addressSpace, "S") +
            "\n\nfn callee(pa : " + ptr(addressSpace, "i32") +
            ",\n          pb : " + ptr(addressSpace, "i32") + ") -> i32 {\n  " +
            genUse(aUse, "*pa") + "\n  " + genUse(bUse, "*pb") +
            "\n  return 0;\n}\n\nfn access(pa : " + ptr(addressSpace, "S") +
            ",\n          pb : " + ptr(addressSpace, "S") + ") -> i32 {\n  return callee(&(*pa)." +
            member + ", &(*pb).a);\n}\n\nfn caller() {\n  " +
            maybeDeclareFunctionScopeVar("x", addressSpace, "S") + "\n  " +
            maybeDeclareFunctionScopeVar("y", addressSpace, "S") + "\n  access(&x, " +
            (aliased ? "&x" : "&y") + ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, bUse), code);
    });

CTS_TEST(g, "one_pointer_one_module_scope")
    .desc("Test aliasing of a pointer with a direct access to a module-scope variable.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"private", "storage", "workgroup"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", useNames())
            .combine("b_use", useNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const std::string bUse = t.param<std::string>("b_use");

        if (requiresUnrestrictedPointerParameters(addressSpace)) {
            skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");
        }

        const std::string code =
            "\n" + declareModuleScopeVar("x", addressSpace, "i32") + "\n" +
            declareModuleScopeVar("y", addressSpace, "i32") + "\n\nfn callee(pb : " +
            ptr(addressSpace, "i32") + ") -> i32 {\n  " + genUse(aUse, "x") + "\n  " +
            genUse(bUse, "*pb") + "\n  return 0;\n}\n\nfn caller() {\n  callee(" +
            (aliased ? "&x" : "&y") + ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, bUse), code);
    });

CTS_TEST(g, "subcalls")
    .desc("Test aliasing of two pointers passed to a function, and then passed to other functions.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"private", "storage", "workgroup"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", {"no_access", "assign", "binary_lhs"})
            .combine("b_use", {"no_access", "assign", "binary_lhs"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const std::string bUse = t.param<std::string>("b_use");

        if (requiresUnrestrictedPointerParameters(addressSpace)) {
            skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");
        }
        const std::string ptrI32 = ptr(addressSpace, "i32");
        const std::string code =
            "\n" + declareModuleScopeVar("x", addressSpace, "i32") + "\n" +
            declareModuleScopeVar("y", addressSpace, "i32") +
            "\n\nfn subcall_no_access(p : " + ptrI32 +
            ") {\n  let pp = &*p;\n}\n\nfn subcall_binary_lhs(p : " + ptrI32 +
            ") -> i32 {\n  return *p + 1;\n}\n\nfn subcall_assign(p : " + ptrI32 +
            ") {\n  *p = 42;\n}\n\nfn callee(pa : " + ptrI32 + ", pb : " + ptrI32 +
            ") -> i32 {\n  let new_pa = &*pa;\n  let new_pb = &*pb;\n  subcall_" + aUse +
            "(new_pa);\n  subcall_" + bUse + "(new_pb);\n  return 0;\n}\n\nfn caller() {\n  callee(&x, " +
            (aliased ? "&x" : "&y") + ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, bUse), code);
    });

CTS_TEST(g, "member_accessors")
    .desc("Test aliasing of two pointers passed to a function and used with member accessors.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"private", "storage", "workgroup"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", {"no_access", "assign", "binary_lhs"})
            .combine("b_use", {"no_access", "assign", "binary_lhs"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const std::string bUse = t.param<std::string>("b_use");

        if (requiresUnrestrictedPointerParameters(addressSpace)) {
            skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");
        }

        const std::string ptrS = ptr(addressSpace, "S");
        const std::string code =
            "\nstruct S { a : i32 }\n\n" + declareModuleScopeVar("x", addressSpace, "S") + "\n" +
            declareModuleScopeVar("y", addressSpace, "S") + "\n\nfn callee(pa : " + ptrS +
            ", pb : " + ptrS + ") -> i32 {\n  " + genUse(aUse, "(*pa).a") + "\n  " +
            genUse(bUse, "(*pb).a") + "\n  return 0;\n}\n\nfn caller() {\n  callee(&x, " +
            (aliased ? "&x" : "&y") + ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, bUse), code);
    });

CTS_TEST(g, "swizzles")
    .desc("Test aliasing of two pointers passed to a function and used with swizzles.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"private", "storage", "workgroup"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("a_use", {"no_access", "compound_assign_lhs"})
            .combine("deref", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");
        const std::string aUse = t.param<std::string>("a_use");
        const bool deref = t.param<bool>("deref");

        if (requiresUnrestrictedPointerParameters(addressSpace)) {
            skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");
        }
        if (deref == false) {
            skipIfLanguageFeatureNotSupported(t, "pointer_composite_access");
        }

        const std::string ptrVec = ptr(addressSpace, "vec4i");
        const std::string code =
            "\n" + declareModuleScopeVar("x", addressSpace, "vec4i") + "\n" +
            declareModuleScopeVar("y", addressSpace, "vec4i") + "\n\nfn callee(pa : " + ptrVec +
            ", pb : " + ptrVec + ") -> i32 {\n  " + genUse(aUse, "(*pa)") + "\n  let value = " +
            (deref ? std::string("(*pb)") : std::string("pb")) +
            ".wzyx;\n  return 0;\n}\n\nfn caller() {\n  callee(&x, " + (aliased ? "&x" : "&y") +
            ");\n}\n";
        t.expectCompileResult(shouldPass2(aliased, aUse, "let_init"), code);
    });

CTS_TEST(g, "same_pointer_read_and_write")
    .desc("Test that we can read from and write to the same pointer.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"private", "storage", "workgroup"}).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");

        if (requiresUnrestrictedPointerParameters(addressSpace)) {
            skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");
        }

        const std::string code =
            "\n" + declareModuleScopeVar("v", addressSpace, "i32") + "\n\nfn callee(p : " +
            ptr(addressSpace, "i32") + ") {\n  *p = *p + 1;\n}\n\nfn caller() {\n  callee(&v);\n}\n";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "aliasing_inside_function")
    .desc("Test that we can alias pointers inside a function.")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", {"private", "storage", "workgroup"}).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("address_space");
        const std::string code =
            "\n" + declareModuleScopeVar("v", addressSpace, "i32") +
            "\n\nfn foo() {\n  var v : i32;\n  let p1 = &v;\n  let p2 = &v;\n  *p1 = 42;\n  *p2 = "
            "42;\n}\n";
        t.expectCompileResult(true, code);
    });

CTS_TEST(g, "two_atomic_pointers")
    .desc("Test aliasing of two atomic pointers passed to a function.")
    .params([](ParamsBuilder u) {
        return u.combine("builtin_a", atomicBuiltins())
            .combine("builtin_b", {"atomicLoad", "atomicStore"})
            .combine("address_space", {"storage", "workgroup"})
            .combine("aliased", {true, false})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string builtinA = t.param<std::string>("builtin_a");
        const std::string builtinB = t.param<std::string>("builtin_b");
        const std::string addressSpace = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        const std::string ptrAtomic = ptr(addressSpace, "atomic<i32>");
        const std::string code =
            "\n" + declareModuleScopeVar("x", addressSpace, "atomic<i32>") + "\n" +
            declareModuleScopeVar("y", addressSpace, "atomic<i32>") + "\n\nfn callee(pa : " +
            ptrAtomic + ", pb : " + ptrAtomic + ") {\n  var i : i32;\n  " +
            callAtomicBuiltin(builtinA, "pa") + ";\n  " + callAtomicBuiltin(builtinB, "pb") +
            ";\n}\n\nfn caller() {\n  callee(&x, &" + (aliased ? "x" : "y") + ");\n}\n";
        const bool shouldFail = aliased && (atomicIsWrite(builtinA) || atomicIsWrite(builtinB));
        t.expectCompileResult(!shouldFail, code);
    });

CTS_TEST(g, "two_atomic_pointers_to_array_elements")
    .desc("Test aliasing of two atomic array element pointers passed to a function.")
    .params([](ParamsBuilder u) {
        return u.combine("builtin_a", atomicBuiltins())
            .combine("builtin_b", {"atomicLoad", "atomicStore"})
            .combine("address_space", {"storage", "workgroup"})
            .combine("index", {0, 1})
            .combine("aliased", {true, false})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string builtinA = t.param<std::string>("builtin_a");
        const std::string builtinB = t.param<std::string>("builtin_b");
        const std::string addressSpace = t.param<std::string>("address_space");
        const int index = t.param<int>("index");
        const bool aliased = t.param<bool>("aliased");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        const std::string ptrAtomic = ptr(addressSpace, "atomic<i32>");
        const std::string code =
            "\n" + declareModuleScopeVar("x", addressSpace, "array<atomic<i32>, 32>") + "\n" +
            declareModuleScopeVar("y", addressSpace, "array<atomic<i32>, 32>") +
            "\n\nfn callee(pa : " + ptrAtomic + ", pb : " + ptrAtomic +
            ") {\n  var i : i32;\n  " + callAtomicBuiltin(builtinA, "pa") + ";\n  " +
            callAtomicBuiltin(builtinB, "pb") + ";\n}\n\nfn caller() {\n  callee(&x[" +
            std::to_string(index) + "], &" + (aliased ? "x" : "y") + "[0]);\n}\n";
        const bool shouldFail = aliased && (atomicIsWrite(builtinA) || atomicIsWrite(builtinB));
        t.expectCompileResult(!shouldFail, code);
    });

CTS_TEST(g, "two_atomic_pointers_to_struct_members")
    .desc("Test aliasing of two struct member atomic pointers passed to a function.")
    .params([](ParamsBuilder u) {
        return u.combine("builtin_a", atomicBuiltins())
            .combine("builtin_b", {"atomicLoad", "atomicStore"})
            .combine("address_space", {"storage", "workgroup"})
            .combine("member", {"a", "b"})
            .combine("aliased", {true, false})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string builtinA = t.param<std::string>("builtin_a");
        const std::string builtinB = t.param<std::string>("builtin_b");
        const std::string addressSpace = t.param<std::string>("address_space");
        const std::string member = t.param<std::string>("member");
        const bool aliased = t.param<bool>("aliased");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        const std::string ptrAtomic = ptr(addressSpace, "atomic<i32>");
        const std::string code =
            "\nstruct S {\n  a : atomic<i32>,\n  b : atomic<i32>,\n}\n\n" +
            declareModuleScopeVar("x", addressSpace, "S") + "\n" +
            declareModuleScopeVar("y", addressSpace, "S") + "\n\nfn callee(pa : " + ptrAtomic +
            ", pb : " + ptrAtomic + ") {\n  var i : i32;\n  " + callAtomicBuiltin(builtinA, "pa") +
            ";\n  " + callAtomicBuiltin(builtinB, "pb") + ";\n}\n\nfn caller() {\n  callee(&x." +
            member + ", &" + (aliased ? "x" : "y") + ".a);\n}\n";
        const bool shouldFail = aliased && (atomicIsWrite(builtinA) || atomicIsWrite(builtinB));
        t.expectCompileResult(!shouldFail, code);
    });

CTS_TEST(g, "one_atomic_pointer_one_module_scope")
    .desc("Test aliasing of an atomic pointer with a direct access to a module-scope variable.")
    .params([](ParamsBuilder u) {
        return u.combine("builtin_a", atomicBuiltins())
            .combine("builtin_b", {"atomicLoad", "atomicStore"})
            .combine("address_space", {"storage", "workgroup"})
            .combine("aliased", {true, false})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string builtinA = t.param<std::string>("builtin_a");
        const std::string builtinB = t.param<std::string>("builtin_b");
        const std::string addressSpace = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        const std::string ptrAtomic = ptr(addressSpace, "atomic<i32>");
        const std::string code =
            "\n" + declareModuleScopeVar("x", addressSpace, "atomic<i32>") + "\n" +
            declareModuleScopeVar("y", addressSpace, "atomic<i32>") + "\n\nfn callee(p : " +
            ptrAtomic + ") {\n  var i : i32;\n  " + callAtomicBuiltin(builtinA, "p") + ";\n  " +
            callAtomicBuiltin(builtinB, aliased ? "&x" : "&y") + ";\n}\n\nfn caller() {\n  callee(&x);\n}\n";
        const bool shouldFail = aliased && (atomicIsWrite(builtinA) || atomicIsWrite(builtinB));
        t.expectCompileResult(!shouldFail, code);
    });

CTS_TEST(g, "workgroup_uniform_load")
    .desc("Test aliasing via workgroupUniformLoad.")
    .params([](ParamsBuilder u) {
        return u.combine("use", {"load", "store", "workgroupUniformLoad"})
            .combine("aliased", {true, false})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string use = t.param<std::string>("use");
        const bool aliased = t.param<bool>("aliased");

        skipIfLanguageFeatureNotSupported(t, "unrestricted_pointer_parameters");

        std::string useExpr;
        if (use == "load") {
            useExpr = "v = *pa";
        } else if (use == "store") {
            useExpr = "*pa = 1";
        } else {  // workgroupUniformLoad
            useExpr = "v = workgroupUniformLoad(pa)";
        }

        const std::string code =
            "\nvar<workgroup> x : i32;\nvar<workgroup> y : i32;\n\nfn callee(pa : "
            "ptr<workgroup, i32>, pb : ptr<workgroup, i32>) -> i32 {\n  var v : i32;\n  " +
            useExpr + ";\n  return v + workgroupUniformLoad(pb);\n}\n\nfn caller() {\n  callee(&x, &" +
            (aliased ? "x" : "y") + ");\n}\n";
        const bool shouldFail = aliased && use == "store";
        t.expectCompileResult(!shouldFail, code);
    });

}  // namespace
