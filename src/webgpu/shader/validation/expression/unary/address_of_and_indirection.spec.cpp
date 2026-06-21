// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/expression/unary/address_of_and_indirection.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// kDerefTypes is an object keyed by derefType name; each entry carries a `wgsl`
// snippet and a `requires_pointer_composite_access` flag, reconstructed via a
// local lookup helper. kInvalidCases is an object keyed by case name; the case's
// body snippet is reconstructed likewise. The `.combine(...).filter(...)` chains
// preserve upstream order and predicates exactly.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,expression,unary,address_of_and_indirection",
    "Validation tests for unary address-of and indirection (dereference)");

// kAddressSpaces / kAccessModes / kStorageTypes / kCompositeTypes (order preserved).
static const std::vector<std::string> kAddressSpaces = {"function", "private", "workgroup",
                                                        "uniform", "storage"};
static const std::vector<std::string> kAccessModes = {"read", "read_write"};
static const std::vector<std::string> kStorageTypes = {"bool", "u32", "i32", "f32", "f16"};
static const std::vector<std::string> kCompositeTypes = {"array", "struct", "vec", "mat"};

static std::vector<Value> toValues(const std::vector<std::string>& v) {
    std::vector<Value> out;
    for (const std::string& s : v) {
        out.emplace_back(s);
    }
    return out;
}

// kDerefTypes (object key order preserved).
struct DerefType {
    const char* name;
    const char* wgsl;
    bool requires_pointer_composite_access;
};

static const std::vector<DerefType>& kDerefTypes() {
    static const std::vector<DerefType> v = {
        {"deref_address_of_identifier", "(*(&a))", false},
        {"deref_pointer", "(*p)", false},
        {"address_of_identifier", "(&a)", true},
        {"pointer", "p", true},
    };
    return v;
}

static std::vector<Value> derefTypeNames() {
    std::vector<Value> values;
    for (const DerefType& d : kDerefTypes()) {
        values.emplace_back(std::string(d.name));
    }
    return values;
}

static const DerefType& findDerefType(const std::string& name) {
    for (const DerefType& d : kDerefTypes()) {
        if (name == d.name) {
            return d;
        }
    }
    static const DerefType dummy{"", "", false};
    return dummy;
}

static std::string filterStr(const ParamRecord& p, const char* key) {
    const Value* v = findParam(p, key);
    return v != nullptr ? valueAs<std::string>(*v) : std::string();
}

CTS_TEST(g, "basic")
    .desc(
        "Validates address-of (&) every supported variable type, ensuring the type is correct by "
        "assigning to an explicitly typed pointer. Also validates dereferencing the reference, "
        "ensuring the type is correct by assigning to an explicitly typed variable.")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", toValues(kAddressSpaces))
            .combine("accessMode", toValues(kAccessModes))
            .combine("storageType", toValues(kStorageTypes))
            .combine("derefType", derefTypeNames())
            .filter([](const ParamRecord& p) {
                if (filterStr(p, "storageType") == "bool") {
                    const std::string as = filterStr(p, "addressSpace");
                    return as == "function" || as == "private";
                }
                return true;
            })
            .filter([](const ParamRecord& p) {
                // This test does not test composite access.
                return !findDerefType(filterStr(p, "derefType")).requires_pointer_composite_access;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("addressSpace");
        const std::string accessMode = t.param<std::string>("accessMode");
        const std::string storageType = t.param<std::string>("storageType");
        const DerefType& deref = findDerefType(t.param<std::string>("derefType"));

        const bool isLocal = addressSpace == "function";
        const std::string commaAccessMode =
            addressSpace == "storage" ? (", " + accessMode) : std::string();

        std::string varDecl;
        if (addressSpace == "uniform" || addressSpace == "storage") {
            varDecl += "@group(0) @binding(0) ";
        }
        varDecl += "var<" + addressSpace + commaAccessMode + "> a : VarType;";

        const std::string wgsl =
            "\n      " + (storageType == "f16" ? std::string("enable f16;") : std::string("")) +
            "\n"
            "\n      alias VarType = " + storageType + ";"
            "\n      alias PtrType = ptr<" + addressSpace + ", VarType " + commaAccessMode + ">;"
            "\n"
            "\n      " + (isLocal ? std::string("") : varDecl) +
            "\n"
            "\n      fn foo() {"
            "\n        " + (isLocal ? varDecl : std::string("")) +
            "\n        let p : PtrType = &a;"
            "\n        var deref : VarType = " + deref.wgsl + ";"
            "\n      }"
            "\n    ";

        t.expectCompileResult(true, wgsl);
    });

CTS_TEST(g, "composite")
    .desc(
        "Validates address-of (&) every supported variable type for composite types, ensuring the "
        "type is correct by assigning to an explicitly typed pointer. Also validates dereferencing "
        "the reference followed by member/index access, ensuring the type is correct by assigning "
        "to an explicitly typed variable.")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", toValues(kAddressSpaces))
            .combine("compositeType", toValues(kCompositeTypes))
            .combine("storageType", toValues(kStorageTypes))
            .beginSubcases()
            .combine("derefType", derefTypeNames())
            .combine("accessMode", toValues(kAccessModes))
            .filter([](const ParamRecord& p) {
                if (filterStr(p, "storageType") == "bool") {
                    const std::string as = filterStr(p, "addressSpace");
                    return as == "function" || as == "private";
                }
                return true;
            })
            .filter([](const ParamRecord& p) {
                if (filterStr(p, "compositeType") == "mat") {
                    const std::string st = filterStr(p, "storageType");
                    return st == "f32" || st == "f16";
                }
                return true;
            });
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("addressSpace");
        const std::string compositeType = t.param<std::string>("compositeType");
        const std::string storageType = t.param<std::string>("storageType");
        const DerefType& deref = findDerefType(t.param<std::string>("derefType"));
        const std::string accessMode = t.param<std::string>("accessMode");

        const bool isLocal = addressSpace == "function";
        const std::string commaAccessMode =
            addressSpace == "storage" ? (", " + accessMode) : std::string();

        std::string varDecl;
        if (addressSpace == "uniform" || addressSpace == "storage") {
            varDecl += "@group(0) @binding(0) ";
        }
        varDecl += "var<" + addressSpace + commaAccessMode + "> a : VarType;";

        std::string wgsl =
            "\n          " + (storageType == "f16" ? std::string("enable f16;") : std::string(""));

        const std::string ptrTypeLine =
            "\n          alias PtrType = ptr<" + addressSpace + ", VarType " + commaAccessMode + ">;";
        const std::string varDeclLine = "\n          " + (isLocal ? std::string("") : varDecl);
        const std::string localVarDeclLine =
            "\n            " + (isLocal ? varDecl : std::string(""));

        if (compositeType == "array") {
            wgsl +=
                "\n          struct S { @align(16) member : " + storageType + " }"
                "\n          alias VarType = array<S, 10>;" + ptrTypeLine + varDeclLine +
                "\n"
                "\n          fn foo() {" + localVarDeclLine +
                "\n            let p : PtrType = &a;"
                "\n            var deref : " + storageType + " = " + deref.wgsl + "[0].member;"
                "\n          }";
        } else if (compositeType == "struct") {
            wgsl +=
                "\n          struct S { member : " + storageType + " }"
                "\n          alias VarType = S;" + ptrTypeLine + varDeclLine +
                "\n"
                "\n          fn foo() {" + localVarDeclLine +
                "\n            let p : PtrType = &a;"
                "\n            var deref : " + storageType + " = " + deref.wgsl + ".member;"
                "\n          }";
        } else if (compositeType == "vec") {
            wgsl +=
                "\n          alias VarType = vec3<" + storageType + ">;" + ptrTypeLine + varDeclLine +
                "\n"
                "\n          fn foo() {" + localVarDeclLine +
                "\n            let p : PtrType = &a;"
                "\n            var deref_member : " + storageType + " = " + deref.wgsl + ".x;"
                "\n            var deref_index : " + storageType + " = " + deref.wgsl + "[0];"
                "\n          }";
        } else if (compositeType == "mat") {
            wgsl +=
                "\n          alias VarType = mat2x3<" + storageType + ">;" + ptrTypeLine +
                varDeclLine +
                "\n"
                "\n          fn foo() {" + localVarDeclLine +
                "\n            let p : PtrType = &a;"
                "\n            var deref_vec : vec3<" + storageType + "> = " + deref.wgsl + "[0];"
                "\n            var deref_elem : " + storageType + " = " + deref.wgsl + "[0][0];"
                "\n          }";
        }

        bool shouldPass = true;
        if (deref.requires_pointer_composite_access &&
            !t.hasLanguageFeature("pointer_composite_access")) {
            shouldPass = false;
        }

        t.expectCompileResult(shouldPass, wgsl);
    });

// kInvalidCases (object key order preserved).
struct InvalidCase {
    const char* name;
    const char* body;
};

static const std::vector<InvalidCase>& kInvalidCases() {
    static const std::vector<InvalidCase> v = {
        {"address_of_let", "\n    let a = 1;\n    let p = &a;"},
        {"address_of_texture", "\n    let p = &t;"},
        {"address_of_sampler", "\n    let p = &s;"},
        {"address_of_function", "\n    let p = &func;"},
        {"address_of_vector_elem_via_member", "\n    var a : vec3<f32>();\n    let p = &a.x;"},
        {"address_of_vector_elem_via_index", "\n    var a : vec3<f32>();\n    let p = &a[0];"},
        {"address_of_matrix_elem", "\n    var a : mat2x3<f32>();\n    let p = &a[0][0];"},
        {"deref_non_pointer", "\n    var a = 1;\n    let p = *a;\n  "},
    };
    return v;
}

static std::vector<Value> invalidCaseNames() {
    std::vector<Value> values;
    for (const InvalidCase& c : kInvalidCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const InvalidCase& findInvalidCase(const std::string& name) {
    for (const InvalidCase& c : kInvalidCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const InvalidCase dummy{"", ""};
    return dummy;
}

CTS_TEST(g, "invalid")
    .desc("Test invalid cases of unary address-of and dereference")
    .params([](ParamsBuilder u) {
        return u.combine("case", invalidCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const InvalidCase& c = findInvalidCase(t.param<std::string>("case"));
        const std::string wgsl =
            "\n      @group(0) @binding(0) var s : sampler;"
            "\n      @group(0) @binding(1) var t : texture_2d<f32>;"
            "\n      fn func() {}"
            "\n      fn main() {"
            "\n        " + std::string(c.body) +
            "\n      }"
            "\n    ";
        t.expectCompileResult(false, wgsl);
    });

}  // namespace
