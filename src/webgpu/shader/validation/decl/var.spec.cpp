// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/var.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for host-shareable types and var declarations. All tests are
// compile-only (expectCompileResult). The address-space / access-mode info table
// (kAddressSpaceInfo, kAccessModeInfo) and the util.ts helpers (getVarDeclShader,
// explicitSpaceExpander, accessModeExpander, supportsRead/supportsWrite) are
// ported inline here.

#include <algorithm>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,decl,var",
    "Validation tests for host-shareable types.");

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

// Object key order preserved (storage, uniform, private, workgroup, function, handle).
static const std::vector<AddressSpaceInfo>& kAddressSpaceInfo() {
    static const std::vector<AddressSpaceInfo> info = {
        {"storage", true, Requirement::kMust, {"read", "read_write"}, Requirement::kMay, true},
        {"uniform", true, Requirement::kMust, {"read"}, Requirement::kNever, true},
        {"private", false, Requirement::kMust, {"read_write"}, Requirement::kNever, true},
        {"workgroup", false, Requirement::kMust, {"read_write"}, Requirement::kNever, true},
        {"function", false, Requirement::kMay, {"read_write"}, Requirement::kNever, false},
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

// effectiveAccessMode: accessMode || info.accessModes[0]
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

static bool supportsRead(const std::string& addressSpace, const std::string& accessMode) {
    const AddressSpaceInfo& info = addressSpaceInfo(addressSpace);
    const std::string mode = effectiveAccessMode(info, accessMode);
    return modesIncludes(info, mode) && accessModeInfo(mode).read;
}

static bool supportsWrite(const std::string& addressSpace, const std::string& accessMode) {
    const AddressSpaceInfo& info = addressSpaceInfo(addressSpace);
    const std::string mode = effectiveAccessMode(info, accessMode);
    return modesIncludes(info, mode) && accessModeInfo(mode).write;
}

// ---------------------------------------------------------------------------
// kTypes (object key order preserved).
// ---------------------------------------------------------------------------
struct TypeInfo {
    const char* name;
    bool isHostShareable;
    bool isConstructible;
    bool isFixedFootprint;
    bool requiresF16;
};

static const std::vector<TypeInfo>& kTypes() {
    static const std::vector<TypeInfo> types = {
        {"bool", false, true, true, false},
        {"i32", true, true, true, false},
        {"u32", true, true, true, false},
        {"f32", true, true, true, false},
        {"f16", true, true, true, true},
        {"vec2<bool>", false, true, true, false},
        {"vec3i", true, true, true, false},
        {"vec4u", true, true, true, false},
        {"vec2f", true, true, true, false},
        {"vec3h", true, true, true, true},
        {"mat2x2f", true, true, true, false},
        {"mat3x4h", true, true, true, true},
        {"atomic<i32>", true, false, true, false},
        {"atomic<u32>", true, false, true, false},
        {"array<vec4<bool>>", false, false, false, false},
        {"array<vec4<bool>, 4>", false, true, true, false},
        {"array<vec4u>", true, false, false, false},
        {"array<vec4u, 4>", true, true, true, false},
        {"array<vec4u, array_size_const>", true, true, true, false},
        {"array<vec4u, array_size_override>", false, false, true, false},
        {"S_u32", true, true, true, false},
        {"S_bool", false, true, true, false},
        {"S_S_bool", false, true, true, false},
        {"S_array_vec4u", true, false, false, false},
        {"S_array_vec4u_4", true, true, true, false},
        {"S_array_bool_4", false, true, true, false},
        {"ptr<function, u32>", false, false, false, false},
        {"sampler", false, false, false, false},
        {"texture_2d<f32>", false, false, false, false},
    };
    return types;
}

static std::vector<Value> typeNames() {
    std::vector<Value> values;
    for (const TypeInfo& ti : kTypes()) {
        values.emplace_back(std::string(ti.name));
    }
    return values;
}

static const TypeInfo& findType(const std::string& name) {
    for (const TypeInfo& ti : kTypes()) {
        if (name == ti.name) {
            return ti;
        }
    }
    static const TypeInfo dummy{"", false, false, false, false};
    return dummy;
}

static bool containsStr(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

// Common struct/alias declaration preamble used by module/function scope tests.
static std::string typePreamble(const std::string& type, bool requiresF16) {
    return std::string(requiresF16 ? "enable f16;" : "") +
           "\n    const array_size_const = 4;"
           "\n    override array_size_override = 4;\n"
           "\n    struct S_u32 { a : u32 }"
           "\n    struct S_bool { a : bool }"
           "\n    struct S_S_bool { a : S_bool }"
           "\n    struct S_array_vec4u { a : array<u32> }"
           "\n    struct S_array_vec4u_4 { a : array<vec4u, 4> }"
           "\n    struct S_array_bool_4 { a : array<bool, 4> }\n"
           "\n    alias MyType = " + type + ";\n"
           "\n    ";
}

CTS_TEST(g, "module_scope_types")
    .desc("Test that only types that are allowed for a given address space are accepted.")
    .params([](ParamsBuilder u) {
        return u.combine("type", typeNames())
            .combine("kind", {"comment", "handle", "private", "storage_ro", "storage_rw",
                              "uniform", "workgroup"})
            .combine("via_alias", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const std::string kind = t.param<std::string>("kind");
        const bool viaAlias = t.param<bool>("via_alias");
        const TypeInfo& type = findType(typeName);
        if (type.requiresF16) {
            t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_ShaderF16, "shader-f16");
        }
        const bool isAtomic = containsStr(typeName, "atomic");

        std::string decl = "<>";
        bool shouldPass = false;
        if (kind == "comment") {
            decl = "// ";
            shouldPass = true;
        } else if (kind == "handle") {
            decl = "@group(0) @binding(0) var foo : ";
            shouldPass = containsStr(typeName, "texture") || containsStr(typeName, "sampler");
        } else if (kind == "private") {
            decl = "var<private> foo : ";
            shouldPass = type.isConstructible;
        } else if (kind == "storage_ro") {
            decl = "@group(0) @binding(0) var<storage, read> foo : ";
            shouldPass = type.isHostShareable && !isAtomic;
        } else if (kind == "storage_rw") {
            decl = "@group(0) @binding(0) var<storage, read_write> foo : ";
            shouldPass = type.isHostShareable;
        } else if (kind == "uniform") {
            decl = "@group(0) @binding(0) var<uniform> foo : ";
            shouldPass = type.isHostShareable && type.isConstructible;
        } else if (kind == "workgroup") {
            decl = "var<workgroup> foo : ";
            shouldPass = type.isFixedFootprint;
        }

        const std::string wgsl =
            typePreamble(typeName, type.requiresF16) + decl + " " +
            (viaAlias ? "MyType" : typeName) + ";\n    ";
        t.expectCompileResult(shouldPass, wgsl);
    });

CTS_TEST(g, "function_scope_types")
    .desc("Test that only types that are allowed for a given address space are accepted.")
    .params([](ParamsBuilder u) {
        return u.combine("type", typeNames())
            .combine("kind", {"comment", "var"})
            .combine("via_alias", {Value(false), Value(true)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const std::string kind = t.param<std::string>("kind");
        const bool viaAlias = t.param<bool>("via_alias");
        const TypeInfo& type = findType(typeName);
        if (type.requiresF16) {
            t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_ShaderF16, "shader-f16");
        }

        std::string decl = "<>";
        bool shouldPass = false;
        if (kind == "comment") {
            decl = "// ";
            shouldPass = true;
        } else if (kind == "var") {
            decl = "var foo : ";
            shouldPass = type.isConstructible;
        }

        const std::string wgsl =
            typePreamble(typeName, type.requiresF16) + "fn foo() {\n      " + decl +
            " " + (viaAlias ? "MyType" : typeName) + ";\n    }";
        t.expectCompileResult(shouldPass, wgsl);
    });

CTS_TEST(g, "module_scope_initializers")
    .desc("Test that initializers are only supported on address spaces that allow them.")
    .params([](ParamsBuilder u) {
        return u.combine("initializer", {Value(false), Value(true)})
            .combine("kind", {"private", "storage_ro", "storage_rw", "uniform", "workgroup"});
    })
    .fn([](ShaderValidationTest& t) {
        const bool initializer = t.param<bool>("initializer");
        const std::string kind = t.param<std::string>("kind");
        std::string decl = "<>";
        if (kind == "private") {
            decl = "var<private> foo : ";
        } else if (kind == "storage_ro") {
            decl = "@group(0) @binding(0) var<storage, read> foo : ";
        } else if (kind == "storage_rw") {
            decl = "@group(0) @binding(0) var<storage, read_write> foo : ";
        } else if (kind == "uniform") {
            decl = "@group(0) @binding(0) var<uniform> foo : ";
        } else if (kind == "workgroup") {
            decl = "var<workgroup> foo : ";
        }
        const std::string wgsl = decl + " u32" + (initializer ? " = 42u" : "") + ";";
        t.expectCompileResult(kind == "private" || !initializer, wgsl);
    });

CTS_TEST(g, "handle_initializer")
    .desc("Test that initializers are not allowed for handle types")
    .params([](ParamsBuilder u) {
        return u.combine("initializer", {Value(false), Value(true)})
            .combine("type", {"sampler", "texture_2d<f32>"});
    })
    .fn([](ShaderValidationTest& t) {
        const bool initializer = t.param<bool>("initializer");
        const std::string type = t.param<std::string>("type");
        const std::string wgsl =
            "\n    @group(0) @binding(0) var foo : " + type +
            ";\n    @group(0) @binding(1) var bar : " + type +
            (initializer ? " = foo" : "") + ";";
        t.expectCompileResult(!initializer, wgsl);
    });

// kInitializers (object key order preserved): name -> validity for private.
struct InitCase {
    const char* expr;
    bool valid;
};
static const std::vector<InitCase>& kInitializers() {
    static const std::vector<InitCase> cases = {
        {"u32()", true},
        {"42u", true},
        {"u32(sqrt(42.0))", true},
        {"user_func()", false},
        {"my_const_42u", true},
        {"my_override_42u", true},
        {"another_private_var", false},
        {"vec4u(1, 2, 3, 4)[my_const_42u / 20]", true},
        {"vec4u(1, 2, 3, 4)[my_override_42u / 20]", true},
        {"vec4u(1, 2, 3, 4)[another_private_var / 20]", false},
    };
    return cases;
}

static std::vector<Value> initializerNames() {
    std::vector<Value> values;
    for (const InitCase& c : kInitializers()) {
        values.emplace_back(std::string(c.expr));
    }
    return values;
}

CTS_TEST(g, "initializer_kind")
    .desc("Test that initializers must be const-expression or override-expression for the private "
          "address space.")
    .params([](ParamsBuilder u) {
        return u.combine("initializer", initializerNames())
            .combine("addrspace", {"private", "function"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string initializer = t.param<std::string>("initializer");
        const std::string addrspace = t.param<std::string>("addrspace");
        bool initValid = false;
        for (const InitCase& c : kInitializers()) {
            if (initializer == c.expr) {
                initValid = c.valid;
                break;
            }
        }
        std::string wgsl =
            "\n    const my_const_42u = 42u;"
            "\n    override my_override_42u : u32;"
            "\n    var<private> another_private_var = 42u;\n"
            "\n    fn user_func() -> u32 {"
            "\n      return 42u;"
            "\n    }\n    ";
        if (addrspace == "private") {
            wgsl += "\n      var<private> foo : u32 = " + initializer + ";";
        } else {
            wgsl += "\n      fn foo() {\n        var bar : u32 = " + initializer + ";\n      }";
        }
        t.expectCompileResult(addrspace == "function" || initValid, wgsl);
    });

CTS_TEST(g, "function_addrspace_at_module_scope")
    .desc("Test that the function address space is not allowed at module scope.")
    .params([](ParamsBuilder u) {
        return u.combine("addrspace", {"private", "function"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addrspace = t.param<std::string>("addrspace");
        t.expectCompileResult(addrspace == "private",
                              "var<" + addrspace + "> foo : i32;");
    });

// kResourceDecls (object key order preserved).
struct ResourceDecl {
    const char* name;
    const char* decl;
};
static const std::vector<ResourceDecl>& kResourceDecls() {
    static const std::vector<ResourceDecl> cases = {
        {"uniform", "var<uniform> buffer : vec4f;"},
        {"storage", "var<storage> buffer : vec4f;"},
        {"texture", "var t : texture_2d<f32>;"},
        {"sampler", "var s : sampler;"},
    };
    return cases;
}

static std::vector<Value> resourceDeclNames() {
    std::vector<Value> values;
    for (const ResourceDecl& c : kResourceDecls()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "binding_point_on_resources")
    .desc("Test that resource variables must have both @group and @binding attributes.")
    .params([](ParamsBuilder u) {
        return u.combine("decl", resourceDeclNames())
            .combine("group", {"", "@group(0)"})
            .combine("binding", {"", "@binding(0)"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string declName = t.param<std::string>("decl");
        const std::string group = t.param<std::string>("group");
        const std::string binding = t.param<std::string>("binding");
        std::string decl;
        for (const ResourceDecl& c : kResourceDecls()) {
            if (declName == c.name) {
                decl = c.decl;
                break;
            }
        }
        const bool shouldPass = !group.empty() && !binding.empty();
        const std::string wgsl = group + " " + binding + " " + decl;
        t.expectCompileResult(shouldPass, wgsl);
    });

CTS_TEST(g, "binding_point_on_non_resources")
    .desc("Test that non-resource variables cannot have either @group or @binding attributes.")
    .params([](ParamsBuilder u) {
        return u.combine("addrspace", {"private", "workgroup"})
            .combine("group", {"", "@group(0)"})
            .combine("binding", {"", "@binding(0)"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string addrspace = t.param<std::string>("addrspace");
        const std::string group = t.param<std::string>("group");
        const std::string binding = t.param<std::string>("binding");
        const bool shouldPass = group.empty() && binding.empty();
        const std::string wgsl =
            group + " " + binding + " var<" + addrspace + "> foo : i32;";
        t.expectCompileResult(shouldPass, wgsl);
    });

CTS_TEST(g, "binding_point_on_function_var")
    .desc("Test that function variables cannot have either @group or @binding attributes.")
    .params([](ParamsBuilder u) {
        return u.combine("group", {"", "@group(0)"})
            .combine("binding", {"", "@binding(0)"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string group = t.param<std::string>("group");
        const std::string binding = t.param<std::string>("binding");
        const bool shouldPass = group.empty() && binding.empty();
        const std::string wgsl =
            "\n    fn foo() {\n      " + group + " " + binding + " var bar : i32;\n    }";
        t.expectCompileResult(shouldPass, wgsl);
    });

CTS_TEST(g, "binding_collisions")
    .desc("Test that binding points can collide iff they are not used by the same entry point.")
    .params([](ParamsBuilder u) {
        return u.combine("a_group", {Value(0), Value(1)})
            .combine("b_group", {Value(0), Value(1)})
            .combine("a_binding", {Value(0), Value(1)})
            .combine("b_binding", {Value(0), Value(1)})
            .combine("b_use", {"same", "different"});
    })
    .fn([](ShaderValidationTest& t) {
        const int64_t a_group = t.param<int64_t>("a_group");
        const int64_t b_group = t.param<int64_t>("b_group");
        const int64_t a_binding = t.param<int64_t>("a_binding");
        const int64_t b_binding = t.param<int64_t>("b_binding");
        const std::string b_use = t.param<std::string>("b_use");

        const std::string mid =
            b_use == "same"
                ? std::string("")
                : std::string("\n      }\n\n    @fragment\n    fn main2() {");

        const std::string wgsl =
            "\n    @group(" + std::to_string(a_group) + ") @binding(" +
            std::to_string(a_binding) +
            ") var<uniform> a : vec4f;\n    @group(" + std::to_string(b_group) + ") @binding(" +
            std::to_string(b_binding) +
            ") var<uniform> b : vec4f;\n\n    @fragment\n    fn main1() {\n      _ = a;\n      " +
            mid + "\n      _ = b;\n    }";

        const bool collision = a_group == b_group && a_binding == b_binding;
        const bool shouldFail = collision && b_use == "same";
        t.expectCompileResult(!shouldFail, wgsl);
    });

CTS_TEST(g, "binding_collision_unused_helper")
    .desc("Test that binding points can collide in an unused helper function.")
    .fn([](ShaderValidationTest& t) {
        const std::string wgsl =
            "\n    @group(0) @binding(0) var<uniform> a : vec4f;"
            "\n    @group(0) @binding(0) var<uniform> b : vec4f;\n"
            "\n    fn foo() {"
            "\n      _ = a;"
            "\n      _ = b;"
            "\n    }";
        t.expectCompileResult(true, wgsl);
    });

CTS_TEST(g, "address_space_access_mode")
    .desc("Test that only storage accepts an access mode")
    .params([](ParamsBuilder u) {
        return u.combine("address_space",
                         {"private", "storage", "uniform", "function", "workgroup"})
            .combine("access_mode", {"", "read", "write", "read_write"})
            .combine("trailing_comma", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string address_space = t.param<std::string>("address_space");
        const std::string access_mode = t.param<std::string>("access_mode");
        const bool trailing_comma = t.param<bool>("trailing_comma");

        std::string fdecl;
        std::string mdecl;
        bool shouldPass = access_mode.empty();
        std::string suffix;
        if (access_mode.empty()) {
            suffix += trailing_comma ? "," : "";
        } else {
            suffix += "," + access_mode;
            suffix += trailing_comma ? "," : "";
        }
        if (address_space == "private") {
            mdecl = "var<private" + suffix + "> x : u32;";
        } else if (address_space == "storage") {
            mdecl = "@group(0) @binding(0) var<storage" + suffix + "> x : u32;";
            shouldPass = access_mode != "write";
        } else if (address_space == "uniform") {
            mdecl = "@group(0) @binding(0) var<uniform" + suffix + "> x : u32;";
        } else if (address_space == "workgroup") {
            mdecl = "var<workgroup" + suffix + "> x : u32;";
        } else if (address_space == "function") {
            fdecl = "var<function" + suffix + "> x : u32;";
        }
        const std::string code =
            mdecl + "\n    fn foo() {\n      " + fdecl + "\n    }";
        t.expectCompileResult(shouldPass, code);
    });

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

CTS_TEST(g, "explicit_access_mode")
    .desc("Validate uses of an explicit access mode on a var declaration")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", nonHandleAddressSpaces())
            .combine("explicitSpace", {Value(true), Value(false)})
            .filter([](const ParamRecord& p) {
                const Value* asVal = findParam(p, "addressSpace");
                const Value* esVal = findParam(p, "explicitSpace");
                const std::string space =
                    asVal != nullptr ? valueAs<std::string>(*asVal) : std::string();
                const bool explicitSpace = esVal != nullptr && valueAs<bool>(*esVal);
                return addressSpaceInfo(space).spell != Requirement::kMust || explicitSpace;
            })
            .combine("explicitAccess", {Value(true)})
            .combine("accessMode", {"read", "write", "read_write"})
            .combine("stage", {"compute"});
    })
    .fn([](ShaderValidationTest& t) {
        VarDeclParams p;
        p.addressSpace = t.param<std::string>("addressSpace");
        p.explicitSpace = t.param<bool>("explicitSpace");
        p.explicitAccess = t.param<bool>("explicitAccess");
        p.accessMode = t.param<std::string>("accessMode");
        p.stage = t.param<std::string>("stage");
        const std::string prog = getVarDeclShader(p);
        const AddressSpaceInfo& info = addressSpaceInfo(p.addressSpace);
        const bool ok = p.explicitSpace && info.spellAccessMode != Requirement::kNever &&
                        modesIncludes(info, p.accessMode);
        t.expectCompileResult(ok, prog);
    });

CTS_TEST(g, "implicit_access_mode")
    .desc("Validate an implicit access mode on a var declaration")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", nonHandleAddressSpaces())
            .expand("explicitSpace", explicitSpaceExpander)
            .combine("explicitAccess", {Value(false)})
            .combine("accessMode", {""})
            .combine("stage", {"compute"});
    })
    .fn([](ShaderValidationTest& t) {
        VarDeclParams p;
        p.addressSpace = t.param<std::string>("addressSpace");
        p.explicitSpace = t.param<bool>("explicitSpace");
        p.explicitAccess = t.param<bool>("explicitAccess");
        p.accessMode = t.param<std::string>("accessMode");
        p.stage = t.param<std::string>("stage");
        const std::string prog = getVarDeclShader(p);
        // 7.3 var Declarations: "The access mode always has a default value,.."
        t.expectCompileResult(true, prog);
    });

CTS_TEST(g, "read_access")
    .desc("A variable can be read from when the access mode permits")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", nonHandleAddressSpaces())
            .expand("explicitSpace", explicitSpaceExpander)
            .combine("explicitAccess", {Value(false), Value(true)})
            .expand("accessMode", accessModeExpander)
            .combine("stage", {"compute"});
    })
    .fn([](ShaderValidationTest& t) {
        VarDeclParams p;
        p.addressSpace = t.param<std::string>("addressSpace");
        p.explicitSpace = t.param<bool>("explicitSpace");
        p.explicitAccess = t.param<bool>("explicitAccess");
        p.accessMode = t.param<std::string>("accessMode");
        p.stage = t.param<std::string>("stage");
        const std::string prog = getVarDeclShader(p, "let copy = x;");
        t.expectCompileResult(supportsRead(p.addressSpace, p.accessMode), prog);
    });

CTS_TEST(g, "write_access")
    .desc("A variable can be written to when the access mode permits")
    .params([](ParamsBuilder u) {
        return u.combine("addressSpace", nonHandleAddressSpaces())
            .expand("explicitSpace", explicitSpaceExpander)
            .combine("explicitAccess", {Value(false), Value(true)})
            .expand("accessMode", accessModeExpander)
            .combine("stage", {"compute"});
    })
    .fn([](ShaderValidationTest& t) {
        VarDeclParams p;
        p.addressSpace = t.param<std::string>("addressSpace");
        p.explicitSpace = t.param<bool>("explicitSpace");
        p.explicitAccess = t.param<bool>("explicitAccess");
        p.accessMode = t.param<std::string>("accessMode");
        p.stage = t.param<std::string>("stage");
        const std::string prog = getVarDeclShader(p, "x = 0;");
        t.expectCompileResult(supportsWrite(p.addressSpace, p.accessMode), prog);
    });

// kTestTypes (order preserved).
static const std::vector<std::string>& kTestTypes() {
    static const std::vector<std::string> types = {
        "f32",         "i32",         "u32",         "bool",
        "vec2<f32>",   "vec2<i32>",   "vec2<u32>",   "vec2<bool>",
        "vec3<f32>",   "vec3<i32>",   "vec3<u32>",   "vec3<bool>",
        "vec4<f32>",   "vec4<i32>",   "vec4<u32>",   "vec4<bool>",
        "mat2x2<f32>", "mat2x3<f32>", "mat2x4<f32>", "mat3x2<f32>",
        "mat3x3<f32>", "mat3x4<f32>", "mat4x2<f32>", "mat4x3<f32>",
        "mat4x4<f32>", "array<f32, 12>", "array<i32, 12>", "array<u32, 12>",
        "array<bool, 12>",
    };
    return types;
}

static std::vector<Value> testTypeValues() {
    std::vector<Value> values;
    for (const std::string& s : kTestTypes()) {
        values.emplace_back(s);
    }
    return values;
}

CTS_TEST(g, "initializer_type")
    .desc("\n  If present, the initializer's type must match the store type of the variable.\n  "
          "Testing scalars, vectors, and matrices of every dimension and type.\n  TODO: add test "
          "for: structs - arrays of vectors and matrices - arrays of different length\n")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("lhsType", testTypeValues())
            .combine("rhsType", testTypeValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string lhsType = t.param<std::string>("lhsType");
        const std::string rhsType = t.param<std::string>("rhsType");
        const std::string code =
            "\n      @fragment\n      fn main() {\n        var a : " + lhsType + " = " + rhsType +
            "();\n      }\n    ";
        t.expectCompileResult(lhsType == rhsType, code);
    });

CTS_TEST(g, "var_access_mode_bad_other_template_contents")
    .desc("A variable declaration with explicit access mode with varying other template list "
          "contents")
    .params([](ParamsBuilder u) {
        return u.combine("accessMode", {"read", "read_write"})
            .combine("prefix", {"storage,", "", ","})
            .combine("suffix", {",storage", ",read", ",", ""});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string accessMode = t.param<std::string>("accessMode");
        const std::string prefix = t.param<std::string>("prefix");
        const std::string suffix = t.param<std::string>("suffix");
        const std::string prog =
            "@group(0) @binding(0)\n                  var<" + prefix + accessMode + suffix +
            "> x: i32;";
        const bool ok = prefix == "storage," && (suffix.empty() || suffix == ",");
        t.expectCompileResult(ok, prog);
    });

CTS_TEST(g, "var_access_mode_bad_template_delim")
    .desc("A variable declaration has explicit access mode with varying template list delimiters")
    .params([](ParamsBuilder u) {
        return u.combine("accessMode", {"read", "read_write"})
            .combine("prefix", {"", "<", ">", ","})
            .combine("suffix", {"", "<", ">", ","});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string accessMode = t.param<std::string>("accessMode");
        const std::string prefix = t.param<std::string>("prefix");
        const std::string suffix = t.param<std::string>("suffix");
        const std::string prog =
            "@group(0) @binding(0)\n                  var " + prefix + "storage," + accessMode +
            suffix + " x: i32;";
        const bool ok = prefix == "<" && suffix == ">";
        t.expectCompileResult(ok, prog);
    });

CTS_TEST(g, "shader_stage")
    .desc("Test the limitations of address space and shader stage")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {"compute", "vertex", "fragment"})
            .combine("kind", {"handle_ro", "handle_wo", "handle_rw", "function", "private",
                              "storage_ro", "storage_rw", "uniform", "workgroup"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string kind = t.param<std::string>("kind");
        if (!t.hasLanguageFeature("readonly_and_readwrite_storage_textures") &&
            kind == "handle_rw") {
            t.skip("Unsupported language feature");
        }
        std::string mdecl;
        std::string fdecl;
        bool expect = true;
        if (kind == "handle_ro") {
            mdecl = "@group(0) @binding(0) var v : sampler;";
        } else if (kind == "handle_wo") {
            mdecl = "@group(0) @binding(0) var v : texture_storage_2d<r32uint, write>;";
            expect = stage != "vertex";
        } else if (kind == "handle_rw") {
            mdecl = "@group(0) @binding(0) var v : texture_storage_2d<r32uint, read_write>;";
            expect = stage != "vertex";
        } else if (kind == "function") {
            fdecl = "var v : u32;";
        } else if (kind == "private") {
            mdecl = "var<private> v : i32;";
        } else if (kind == "storage_ro") {
            mdecl = "@group(0) @binding(0) var<storage> v : u32;";
        } else if (kind == "storage_rw") {
            mdecl = "@group(0) @binding(0) var<storage, read_write> v : u32;";
            expect = stage != "vertex";
        } else if (kind == "uniform") {
            mdecl = "@group(0) @binding(0) var<uniform> v : u32;";
        } else if (kind == "workgroup") {
            mdecl = "var<workgroup> v : u32;";
            expect = stage == "compute";
        }
        std::string func;
        if (stage == "compute") {
            func = "@compute @workgroup_size(1)\n        fn main() {\n          " + fdecl +
                   "\n          _ = v;\n        }";
        } else if (stage == "vertex") {
            func = "@vertex\n        fn main() -> @builtin(position) vec4f {\n          " + fdecl +
                   "\n          _ = v;\n          return vec4f();\n        }";
        } else {
            func = "@fragment\n        fn main() {\n          " + fdecl +
                   "\n          _ = v;\n        }";
        }
        const std::string code = "\n    " + mdecl + "\n    " + func;
        t.expectCompileResult(expect, code);
    });

} // namespace
