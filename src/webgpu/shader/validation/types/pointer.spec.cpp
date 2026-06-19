// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/pointer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for pointer types. All tests are compile-only
// (expectCompileResult).
//
// Porting notes:
//   - The address-space / access-mode info tables (kAddressSpaceInfo,
//     kAccessModeInfo from shader/types.ts) and the decl/util.ts helpers
//     (declareEntryPoint, declareVarX, getVarDeclShader, pointerType,
//     explicitSpaceExpander, accessModeExpander, supportsWrite) are ported inline,
//     matching decl/var.spec.cpp. The address-space table preserves the upstream
//     OBJECT KEY order (storage, uniform, private, function, workgroup, handle) so
//     query/case identities match keysOf() ordering.
//   - This spec does not use the pipeline path or hasLanguageFeature.

#include <algorithm>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,types,pointer",
    "Test pointer type validation");

// ---------------------------------------------------------------------------
// Ported from shader/types.ts: kAccessModeInfo / kAddressSpaceInfo.
// ---------------------------------------------------------------------------
enum class Requirement { kMust, kMay, kNever };

struct AccessModeInfo {
    bool read;
    bool write;
};
static AccessModeInfo accessModeInfo(const std::string& mode) {
    if (mode == "read") {
        return {true, false};
    }
    if (mode == "write") {
        return {false, true};
    }
    // read_write
    return {true, true};
}

struct AddressSpaceInfo {
    const char* name;
    bool binding;
    Requirement spell;
    std::vector<std::string> accessModes; // first is the default
    Requirement spellAccessMode;
    bool moduleScope; // true => 'module', false => 'function'
};

// Object key order preserved (storage, uniform, private, function, workgroup, handle).
static const std::vector<AddressSpaceInfo>& kAddressSpaceInfo() {
    static const std::vector<AddressSpaceInfo> info = {
        {"storage", true, Requirement::kMust, {"read", "read_write"}, Requirement::kMay, true},
        {"uniform", true, Requirement::kMust, {"read"}, Requirement::kNever, true},
        {"private", false, Requirement::kMust, {"read_write"}, Requirement::kNever, true},
        {"function", false, Requirement::kMay, {"read_write"}, Requirement::kNever, false},
        {"workgroup", false, Requirement::kMust, {"read_write"}, Requirement::kNever, true},
        {"handle", true, Requirement::kNever, {}, Requirement::kNever, true},
    };
    return info;
}

static const AddressSpaceInfo& addressSpaceInfo(const std::string& space) {
    for (const AddressSpaceInfo& i : kAddressSpaceInfo()) {
        if (space == i.name) {
            return i;
        }
    }
    static const AddressSpaceInfo dummy{"", false, Requirement::kNever, {}, Requirement::kNever,
                                        true};
    return dummy;
}

// ---------------------------------------------------------------------------
// Ported from decl/util.ts.
// ---------------------------------------------------------------------------
static std::string declareEntryPoint(const std::string& stage, const std::string& body) {
    if (stage == "vertex") {
        return "@vertex\nfn main() -> @builtin(position) vec4f {\n  " + body +
               "\n  return vec4f();\n}";
    }
    if (stage == "fragment") {
        return "@fragment\nfn main() {\n  " + body + "\n}";
    }
    // compute
    return "@compute @workgroup_size(1)\nfn main() {\n  " + body + "\n}";
}

// declareVarX(addressSpace, accessMode) — '' values mean omit.
static std::string declareVarX(const std::string& addressSpace, const std::string& accessMode) {
    std::string out;
    if (!addressSpace.empty() && addressSpaceInfo(addressSpace).binding) {
        out += "@group(0) @binding(0) ";
    }
    out += "var";
    std::vector<std::string> templateParts;
    if (!addressSpace.empty()) {
        templateParts.push_back(addressSpace);
    }
    if (!accessMode.empty()) {
        templateParts.push_back(accessMode);
    }
    if (!templateParts.empty()) {
        std::string joined;
        for (size_t i = 0; i < templateParts.size(); ++i) {
            if (i > 0) {
                joined += ",";
            }
            joined += templateParts[i];
        }
        out += "<" + joined + ">";
    }
    out += " x: i32;";
    return out;
}

struct VarDeclParams {
    std::string addressSpace;
    bool explicitSpace;
    std::string accessMode; // may be ""
    bool explicitAccess;
    std::string stage;
};

static std::string getVarDeclShader(const VarDeclParams& p, const std::string& additionalBody = "") {
    const AddressSpaceInfo& info = addressSpaceInfo(p.addressSpace);
    const std::string decl =
        declareVarX(p.explicitSpace ? p.addressSpace : "", p.explicitAccess ? p.accessMode : "");
    if (info.moduleScope) {
        return decl + "\n" + declareEntryPoint(p.stage, additionalBody);
    }
    // function scope
    return declareEntryPoint(p.stage, decl + "\n" + additionalBody);
}

// pointerType: ptr<space,storeType[,accessMode]> ; space = explicitSpace ? addressSpace : function.
static std::string pointerType(const std::string& addressSpace,
                               bool explicitSpace,
                               const std::string& accessMode,
                               const std::string& ptrStoreType) {
    const std::string space = explicitSpace ? addressSpace : "function";
    const std::string modePart = accessMode.empty() ? std::string() : ("," + accessMode);
    return "ptr<" + space + "," + ptrStoreType + modePart + ">";
}

static std::string effectiveAccessMode(const AddressSpaceInfo& info, const std::string& accessMode) {
    if (!accessMode.empty()) {
        return accessMode;
    }
    return info.accessModes.empty() ? std::string() : info.accessModes[0];
}

static bool modesIncludes(const AddressSpaceInfo& info, const std::string& mode) {
    return std::find(info.accessModes.begin(), info.accessModes.end(), mode) !=
           info.accessModes.end();
}

static bool supportsWrite(const std::string& addressSpace, const std::string& accessMode) {
    const AddressSpaceInfo& info = addressSpaceInfo(addressSpace);
    const std::string mode = effectiveAccessMode(info, accessMode);
    return modesIncludes(info, mode) && accessModeInfo(mode).write;
}

// Address spaces that can hold an i32 variable (kAddressSpaceInfo minus 'handle').
static std::vector<Value> nonHandleAddressSpaces() {
    std::vector<Value> values;
    for (const AddressSpaceInfo& i : kAddressSpaceInfo()) {
        if (std::string(i.name) != "handle") {
            values.emplace_back(std::string(i.name));
        }
    }
    return values;
}

// explicitSpaceExpander: spell==must => [true]; else [true,false].
static std::vector<Value> explicitSpaceExpander(const ParamRecord& p) {
    const Value* asVal = findParam(p, "addressSpace");
    const std::string space = asVal != nullptr ? valueAs<std::string>(*asVal) : std::string();
    if (addressSpaceInfo(space).spell == Requirement::kMust) {
        return {Value(true)};
    }
    return {Value(true), Value(false)};
}

// accessModeExpander: explicitAccess && spellAccessMode!=never => info.accessModes; else [''].
static std::vector<Value> accessModeExpander(const ParamRecord& p) {
    const Value* asVal = findParam(p, "addressSpace");
    const Value* eaVal = findParam(p, "explicitAccess");
    const std::string space = asVal != nullptr ? valueAs<std::string>(*asVal) : std::string();
    const bool explicitAccess = eaVal != nullptr && valueAs<bool>(*eaVal);
    const AddressSpaceInfo& info = addressSpaceInfo(space);
    if (explicitAccess && info.spellAccessMode != Requirement::kNever) {
        std::vector<Value> modes;
        for (const std::string& m : info.accessModes) {
            modes.emplace_back(m);
        }
        return modes;
    }
    return {Value(std::string(""))};
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

CTS_TEST(g, "missing_type")
    .desc("Test that pointer types require an element type")
    .params([](ParamsBuilder u) {
        return u.combine("aspace", {"function", "private", "workgroup", "storage", "uniform"})
            .combine("comma", {"", ","});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string aspace = t.param<std::string>("aspace");
        const std::string comma = t.param<std::string>("comma");
        const std::string code = "alias T = ptr<" + aspace + comma + ">;";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "address_space")
    .desc("Test address spaces in pointer type parameterization")
    .params([](ParamsBuilder u) {
        return u.combine("aspace", {"function", "private", "workgroup", "storage", "uniform",
                                    "handle", "bad_aspace"})
            .combine("comma", {"", ","});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string aspace = t.param<std::string>("aspace");
        const std::string comma = t.param<std::string>("comma");
        const std::string code = "alias T = ptr<" + aspace + ", u32" + comma + ">;";
        const bool success = aspace != "handle" && aspace != "bad_aspace";
        t.expectCompileResult(success, code);
    });

CTS_TEST(g, "access_mode")
    .desc("Test access mode in pointer type parameterization")
    .params([](ParamsBuilder u) {
        return u.combine("aspace", {"function", "private", "storage", "uniform", "workgroup"})
            .combine("access", {"read", "write", "read_write"})
            .combine("comma", {"", ","});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string aspace = t.param<std::string>("aspace");
        const std::string access = t.param<std::string>("access");
        const std::string comma = t.param<std::string>("comma");
        // Default access mode is tested above.
        const std::string code =
            "alias T = ptr<" + aspace + ", u32, " + access + comma + ">;";
        const bool success = aspace == "storage" && access != "write";
        t.expectCompileResult(success, code);
    });

// Mirrors upstream kTypeCases (object key order preserved).
struct TypeCase {
    const char* name;
    const char* type;
    bool storable;
    bool f16;
    const char* aspace; // "" => default 'storage'
};

static const std::vector<TypeCase>& kTypeCases() {
    static const std::vector<TypeCase> cases = {
        // Scalars
        {"bool", "bool", true, false, "function"},
        {"u32", "u32", true, false, ""},
        {"i32", "i32", true, false, ""},
        {"f32", "f32", true, false, ""},
        {"f16", "f16", true, true, ""},

        // Vectors
        {"vec2u", "vec2u", true, false, ""},
        {"vec3i", "vec3i", true, false, ""},
        {"vec4f", "vec4f", true, false, ""},
        {"vec2_bool", "vec2<bool>", true, false, "workgroup"},
        {"vec3h", "vec3h", true, true, ""},

        // Matrices
        {"mat2x2f", "mat2x2f", true, false, ""},
        {"mat3x4h", "mat3x4h", true, true, ""},

        // Atomics
        {"atomic_u32", "atomic<u32>", true, false, ""},
        {"atomic_i32", "atomic<i32>", true, false, ""},

        // Arrays
        {"array_sized_u32", "array<u32, 4>", true, false, ""},
        {"array_sized_vec4f", "array<vec4f, 16>", true, false, ""},
        {"array_sized_S", "array<S, 2>", true, false, ""},
        {"array_runtime_u32", "array<u32>", true, false, ""},
        {"array_runtime_S", "array<S>", true, false, ""},
        {"array_runtime_atomic_u32", "array<atomic<u32>>", true, false, ""},
        {"array_override_u32", "array<u32, o>", true, false, "workgroup"},

        // Structs
        {"struct_S", "S", true, false, ""},
        {"struct_T", "T", true, false, ""},

        // Pointers
        {"ptr_function_u32", "ptr<function, u32>", false, false, ""},
        {"ptr_workgroup_bool", "ptr<workgroup, bool>", false, false, ""},

        // Sampler (while storable, can only be in the handle address space)
        {"sampler", "sampler", false, false, ""},

        // Texture (while storable, can only be in the handle address space)
        {"texture_2d", "texture_2d<f32>", false, false, ""},

        // Alias
        {"alias", "u32_alias", true, false, ""},

        // Reference
        {"reference", "ref<function, u32>", false, false, "function"},
    };
    return cases;
}

static std::vector<Value> typeCaseNames() {
    std::vector<Value> values;
    for (const TypeCase& c : kTypeCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const TypeCase& findTypeCase(const std::string& name) {
    for (const TypeCase& c : kTypeCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const TypeCase dummy{"", "", false, false, ""};
    return dummy;
}

CTS_TEST(g, "type")
    .desc("Tests that pointee type must be storable")
    .params([](ParamsBuilder u) {
        return u.combine("case", typeCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const TypeCase& testcase = findTypeCase(t.param<std::string>("case"));
        const std::string typeStr = testcase.type;
        const std::string aspace =
            std::string(testcase.aspace).empty() ? std::string("storage") : testcase.aspace;
        const std::string access =
            typeStr.find("atomic") != std::string::npos ? std::string(", read_write")
                                                        : std::string();
        const std::string code =
            (testcase.f16 ? std::string("enable f16;") : std::string()) +
            "\n    override o : u32;\n    struct S { x : u32 }\n    struct T { s : array<S> }\n    "
            "alias u32_alias = u32;\n    alias Type = ptr<" +
            aspace + ", " + typeStr + access + ">;";
        t.expectCompileResult(testcase.storable, code);
    });

CTS_TEST(g, "let_ptr_explicit_type_matches_var")
    .desc("Let-declared pointer with explicit type initialized from var with same address space "
          "and access mode")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", nonHandleAddressSpaces())
            .expand("explicitSpace", explicitSpaceExpander)
            .combine("explicitAccess", {Value(false), Value(true)})
            .expand("accessMode", accessModeExpander)
            .combine("stage", {"compute"})
            .combine("ptrStoreType", {"i32", "u32"});
    })
    .fn([](ShaderValidationTest& t) {
        VarDeclParams p;
        p.addressSpace = t.param<std::string>("addressSpace");
        p.explicitSpace = t.param<bool>("explicitSpace");
        p.explicitAccess = t.param<bool>("explicitAccess");
        p.accessMode = t.param<std::string>("accessMode");
        p.stage = t.param<std::string>("stage");
        const std::string ptrStoreType = t.param<std::string>("ptrStoreType");
        const std::string ptrTy = pointerType(p.addressSpace, p.explicitSpace, p.accessMode,
                                               ptrStoreType);
        const std::string prog = getVarDeclShader(p, "let p: " + ptrTy + " = &x;");
        const bool ok = ptrStoreType == "i32"; // The store type matches the variable's store type.
        t.expectCompileResult(ok, prog);
    });

CTS_TEST(g, "let_ptr_reads")
    .desc("Validate reading via ptr when permitted by access mode")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", nonHandleAddressSpaces())
            .expand("explicitSpace", explicitSpaceExpander)
            .combine("explicitAccess", {Value(false), Value(true)})
            .expand("accessMode", accessModeExpander)
            .combine("stage", {"compute"})
            .combine("inferPtrType", {Value(false), Value(true)})
            .combine("ptrStoreType", {"i32"});
    })
    .fn([](ShaderValidationTest& t) {
        VarDeclParams p;
        p.addressSpace = t.param<std::string>("addressSpace");
        p.explicitSpace = t.param<bool>("explicitSpace");
        p.explicitAccess = t.param<bool>("explicitAccess");
        p.accessMode = t.param<std::string>("accessMode");
        p.stage = t.param<std::string>("stage");
        const bool inferPtrType = t.param<bool>("inferPtrType");
        const std::string ptrStoreType = t.param<std::string>("ptrStoreType");
        const std::string typePart =
            inferPtrType ? (": " + pointerType(p.addressSpace, p.explicitSpace, p.accessMode,
                                                ptrStoreType))
                         : std::string();
        const std::string prog = getVarDeclShader(p, "let p" + typePart + " = &x; let read = *p;");
        const bool ok = true; // We can always read.
        t.expectCompileResult(ok, prog);
    });

CTS_TEST(g, "let_ptr_writes")
    .desc("Validate writing via ptr when permitted by access mode")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", nonHandleAddressSpaces())
            .expand("explicitSpace", explicitSpaceExpander)
            .combine("explicitAccess", {Value(false), Value(true)})
            .expand("accessMode", accessModeExpander)
            .combine("stage", {"compute"})
            .combine("inferPtrType", {Value(false), Value(true)})
            .combine("ptrStoreType", {"i32"});
    })
    .fn([](ShaderValidationTest& t) {
        VarDeclParams p;
        p.addressSpace = t.param<std::string>("addressSpace");
        p.explicitSpace = t.param<bool>("explicitSpace");
        p.explicitAccess = t.param<bool>("explicitAccess");
        p.accessMode = t.param<std::string>("accessMode");
        p.stage = t.param<std::string>("stage");
        const bool inferPtrType = t.param<bool>("inferPtrType");
        const std::string ptrStoreType = t.param<std::string>("ptrStoreType");
        const std::string typePart =
            inferPtrType ? (": " + pointerType(p.addressSpace, p.explicitSpace, p.accessMode,
                                                ptrStoreType))
                         : std::string();
        const std::string prog = getVarDeclShader(p, "let p" + typePart + " = &x; *p = 42;");
        const bool ok = supportsWrite(p.addressSpace, p.accessMode);
        t.expectCompileResult(ok, prog);
    });

CTS_TEST(g, "ptr_handle_space_invalid")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "alias p = ptr<handle,u32>;");
    });

CTS_TEST(g, "ptr_bad_store_type")
    .params([](ParamsBuilder u) {
        return u.combine("storeType", {"undeclared", "clamp", "1"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string storeType = t.param<std::string>("storeType");
        t.expectCompileResult(false, "alias p = ptr<private," + storeType + ">;");
    });

CTS_TEST(g, "ptr_address_space_never_uses_access_mode")
    .params([](ParamsBuilder u) {
        // keysOf(kAddressSpaceInfo).filter(spellAccessMode === 'never').
        std::vector<Value> spaces;
        for (const AddressSpaceInfo& i : kAddressSpaceInfo()) {
            if (i.spellAccessMode == Requirement::kNever) {
                spaces.emplace_back(std::string(i.name));
            }
        }
        return u.combine("addressSpace", spaces)
            .combine("accessMode", {"read", "write", "read_write"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addressSpace = t.param<std::string>("addressSpace");
        const std::string accessMode = t.param<std::string>("accessMode");
        const std::string prog =
            "alias pty = ptr<" + addressSpace + ",u32,;" + accessMode + ">;";
        t.expectCompileResult(false, prog);
    });

// Mirrors upstream kStoreTypeNotInstantiable (object key order preserved).
struct StoreTypeCase {
    const char* name;
    const char* code;
};

static const std::vector<StoreTypeCase>& kStoreTypeNotInstantiable() {
    static const std::vector<StoreTypeCase> cases = {
        {"ptr", "alias p = ptr<storage,ptr<private,i32>>;"},
        {"privateAtomic", "alias p = ptr<private,atomic<u32>>;"},
        {"functionAtomic", "alias p = ptr<function,atomic<u32>>;"},
        {"uniformAtomic", "alias p = ptr<uniform,atomic<u32>>;"},
        {"workgroupRTArray", "alias p = ptr<workgroup,array<i32>>;"},
        {"uniformRTArray", "alias p = ptr<uniform,array<i32>>;"},
        {"privateRTArray", "alias p = ptr<private,array<i32>>;"},
        {"functionRTArray", "alias p = ptr<function,array<i32>>;"},
        {"RTArrayNotLast", "struct S { a: array<i32>, b: i32 } alias p = ptr<storage,S>;"},
        {"nestedRTArray",
         "struct S { a: array<i32>, b: i32 } struct { s: S } alias p = ptr<storage,T>;"},
    };
    return cases;
}

CTS_TEST(g, "ptr_not_instantiable")
    .desc("Validate that ptr type must correspond to a variable that could be declared somewhere; "
          "test bad cases")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const StoreTypeCase& c : kStoreTypeNotInstantiable()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("case", names);
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("case");
        for (const StoreTypeCase& c : kStoreTypeNotInstantiable()) {
            if (name == c.name) {
                t.expectCompileResult(false, c.code);
                return;
            }
        }
    });

} // namespace
