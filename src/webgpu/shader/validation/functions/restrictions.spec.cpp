// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/functions/restrictions.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

const char* const kCCommonTypeDecls = R"(
struct runtime_array_struct {
  arr : array<u32>
}

struct constructible {
  a : i32,
  b : u32,
  c : f32,
  d : bool,
}

struct host_shareable {
  a : i32,
  b : u32,
  c : f32,
}

struct struct_with_array {
  a : array<constructible, 4>
}

override override_no_default : u32;
override override_default = 4u;
override override_expr = override_default + 2;

)";

// ---------------------------------------------------------------------------
// vertex_returns_position
// ---------------------------------------------------------------------------
struct VertexPosCase {
    const char* name;   // key
    const char* type;   // return type spelling
    const char* value;  // return value
    bool valid;
};

static const std::vector<VertexPosCase>& kVertexPosCases() {
    static const std::vector<VertexPosCase> cases = {
        {"bare_position", "@builtin(position) vec4f", "vec4f()", true},
        {"nested_position", "pos_struct", "pos_struct()", true},
        {"no_bare_position", "vec4f", "vec4f()", false},
        {"no_nested_position", "no_pos_struct", "no_pos_struct()", false},
    };
    return cases;
}

static std::vector<Value> caseNamesVP() {
    std::vector<Value> v;
    for (const VertexPosCase& c : kVertexPosCases()) {
        v.emplace_back(std::string(c.name));
    }
    return v;
}

static const VertexPosCase& findVP(const std::string& name) {
    for (const VertexPosCase& c : kVertexPosCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const VertexPosCase dummy{"", "", "", false};
    return dummy;
}

// ---------------------------------------------------------------------------
// function_return_types
// ---------------------------------------------------------------------------
struct RetTypeCase {
    const char* key;
    const char* name;
    const char* value;  // "" => `${name}()`
    bool valid;
};

static const std::vector<RetTypeCase>& kFunctionRetTypeCases() {
    static const std::vector<RetTypeCase> cases = {
        {"u32", "u32", "", true},
        {"i32", "i32", "", true},
        {"f32", "f32", "", true},
        {"bool", "bool", "", true},
        {"f16", "f16", "", true},
        {"vec2", "vec2u", "", true},
        {"vec3", "vec3i", "", true},
        {"vec4", "vec4f", "", true},
        {"mat2x2", "mat2x2f", "", true},
        {"mat2x3", "mat2x3f", "", true},
        {"mat2x4", "mat2x4f", "", true},
        {"mat3x2", "mat3x2f", "", true},
        {"mat3x3", "mat3x3f", "", true},
        {"mat3x4", "mat3x4f", "", true},
        {"mat4x2", "mat4x2f", "", true},
        {"mat4x3", "mat4x3f", "", true},
        {"mat4x4", "mat4x4f", "", true},
        {"array1", "array<u32, 4>", "", true},
        {"array2", "array<vec2f, 2>", "", true},
        {"array3", "array<constructible, 4>", "", true},
        {"array4", "array<mat2x2f, 4>", "", true},
        {"array5", "array<bool, 4>", "", true},
        {"struct1", "constructible", "", true},
        {"struct2", "struct_with_array", "", true},
        {"runtime_array", "array<u32>", "", false},
        {"runtime_struct", "runtime_array_struct", "", false},
        {"override_array", "array<u32, override_size>", "", false},
        {"atomic_u32", "atomic<u32>", "atomic_wg", false},
        {"atomic_struct", "atomic_struct", "", false},
        {"texture_sample", "texture_2d<f32>", "t", false},
        {"texture_depth", "texture_depth_2d", "t_depth", false},
        {"texture_multisampled", "texture_multisampled_2d<f32>", "t_multisampled", false},
        {"texture_storage", "texture_storage_2d<rgba8unorm, write>", "t_storage", false},
        {"sampler", "sampler", "s", false},
        {"sampler_comparison", "sampler_comparison", "s_depth", false},
        {"ptr", "ptr<workgroup, atomic<u32>>", "&atomic_wg", false},
    };
    return cases;
}

static std::vector<Value> caseNamesRet() {
    std::vector<Value> v;
    for (const RetTypeCase& c : kFunctionRetTypeCases()) {
        v.emplace_back(std::string(c.key));
    }
    return v;
}

static const RetTypeCase& findRet(const std::string& name) {
    for (const RetTypeCase& c : kFunctionRetTypeCases()) {
        if (name == c.key) {
            return c;
        }
    }
    static const RetTypeCase dummy{"", "", "", false};
    return dummy;
}

// ---------------------------------------------------------------------------
// function_parameter_types / function_parameter_matching
// ---------------------------------------------------------------------------
enum class ParamValid { kFalse, kTrue, kWithUnrestrictedPointerParameters };

struct ParamTypeCase {
    const char* key;
    const char* name;
    ParamValid valid;
};

static const std::vector<ParamTypeCase>& kFunctionParamTypeCases() {
    static const std::vector<ParamTypeCase> cases = {
        {"u32", "u32", ParamValid::kTrue},
        {"i32", "i32", ParamValid::kTrue},
        {"f32", "f32", ParamValid::kTrue},
        {"bool", "bool", ParamValid::kTrue},
        {"f16", "f16", ParamValid::kTrue},
        {"vec2", "vec2u", ParamValid::kTrue},
        {"vec3", "vec3i", ParamValid::kTrue},
        {"vec4", "vec4f", ParamValid::kTrue},
        {"mat2x2", "mat2x2f", ParamValid::kTrue},
        {"mat2x3", "mat2x3f", ParamValid::kTrue},
        {"mat2x4", "mat2x4f", ParamValid::kTrue},
        {"mat3x2", "mat3x2f", ParamValid::kTrue},
        {"mat3x3", "mat3x3f", ParamValid::kTrue},
        {"mat3x4", "mat3x4f", ParamValid::kTrue},
        {"mat4x2", "mat4x2f", ParamValid::kTrue},
        {"mat4x3", "mat4x3f", ParamValid::kTrue},
        {"mat4x4", "mat4x4f", ParamValid::kTrue},
        {"array1", "array<u32, 4>", ParamValid::kTrue},
        {"array2", "array<vec2f, 2>", ParamValid::kTrue},
        {"array3", "array<constructible, 4>", ParamValid::kTrue},
        {"array4", "array<mat2x2f, 4>", ParamValid::kTrue},
        {"array5", "array<bool, 4>", ParamValid::kTrue},
        {"struct1", "constructible", ParamValid::kTrue},
        {"struct2", "struct_with_array", ParamValid::kTrue},
        {"runtime_array", "array<u32>", ParamValid::kFalse},
        {"runtime_struct", "runtime_array_struct", ParamValid::kFalse},
        {"override_array", "array<u32, override_size>", ParamValid::kFalse},
        {"atomic_u32", "atomic<u32>", ParamValid::kFalse},
        {"atomic_struct", "atomic_struct", ParamValid::kFalse},
        {"texture_sample", "texture_2d<f32>", ParamValid::kTrue},
        {"texture_depth", "texture_depth_2d", ParamValid::kTrue},
        {"texture_multisampled", "texture_multisampled_2d<f32>", ParamValid::kTrue},
        {"texture_storage", "texture_storage_2d<rgba8unorm, write>", ParamValid::kTrue},
        {"sampler", "sampler", ParamValid::kTrue},
        {"sampler_comparison", "sampler_comparison", ParamValid::kTrue},
        {"ptr1", "ptr<function, u32>", ParamValid::kTrue},
        {"ptr2", "ptr<function, constructible>", ParamValid::kTrue},
        {"ptr3", "ptr<private, u32>", ParamValid::kTrue},
        {"ptr4", "ptr<private, constructible>", ParamValid::kTrue},
        {"ptr5", "ptr<storage, u32>", ParamValid::kWithUnrestrictedPointerParameters},
        {"ptr6", "ptr<storage, u32, read>", ParamValid::kWithUnrestrictedPointerParameters},
        {"ptr7", "ptr<storage, u32, read_write>", ParamValid::kWithUnrestrictedPointerParameters},
        {"ptr8", "ptr<uniform, u32>", ParamValid::kWithUnrestrictedPointerParameters},
        {"ptr9", "ptr<workgroup, u32>", ParamValid::kWithUnrestrictedPointerParameters},
        {"ptr10", "ptr<storage, host_shareable, read_write>",
         ParamValid::kWithUnrestrictedPointerParameters},
        {"ptr11", "ptr<storage, host_shareable, read>",
         ParamValid::kWithUnrestrictedPointerParameters},
        {"ptr12", "ptr<uniform, host_shareable>", ParamValid::kWithUnrestrictedPointerParameters},
        {"ptrWorkgroupAtomic", "ptr<workgroup, atomic<u32>>",
         ParamValid::kWithUnrestrictedPointerParameters},
        {"ptrWorkgroupNestedAtomic", "ptr<workgroup, array<atomic<u32>,1>>",
         ParamValid::kWithUnrestrictedPointerParameters},
        {"ptrWorkgroupOverrideNoDefault", "ptr<workgroup, array<u32, override_no_default>>",
         ParamValid::kWithUnrestrictedPointerParameters},
        {"ptrWorkgroupOverrideWithDefault", "ptr<workgroup, array<f32, override_default>>",
         ParamValid::kWithUnrestrictedPointerParameters},
        {"ptrWorkgroupOverrideExpr", "ptr<workgroup, array<vec4f, override_expr>>",
         ParamValid::kWithUnrestrictedPointerParameters},
        {"invalid_ptr1", "ptr<handle, u32>", ParamValid::kFalse},
        {"invalid_ptr2", "ptr<not_an_address_space, u32>", ParamValid::kFalse},
        {"invalid_ptr3", "ptr<storage>", ParamValid::kFalse},
        {"invalid_ptr4", "ptr<private,u32,read>", ParamValid::kFalse},
        {"invalid_ptr5", "ptr<private,u32,write>", ParamValid::kFalse},
        {"invalid_ptr6", "ptr<private,u32,read_write>", ParamValid::kFalse},
        {"invalid_ptr7", "ptr<private,clamp>", ParamValid::kFalse},
        {"invalid_ptr8", "ptr<function, texture_external>", ParamValid::kFalse},
    };
    return cases;
}

static std::vector<Value> caseNamesParamType() {
    std::vector<Value> v;
    for (const ParamTypeCase& c : kFunctionParamTypeCases()) {
        v.emplace_back(std::string(c.key));
    }
    return v;
}

static const ParamTypeCase& findParamType(const std::string& name) {
    for (const ParamTypeCase& c : kFunctionParamTypeCases()) {
        if (name == c.key) {
            return c;
        }
    }
    static const ParamTypeCase dummy{"", "", ParamValid::kFalse};
    return dummy;
}

struct ParamValueCase {
    const char* key;
    const char* value;
    std::vector<std::string> matches;
    bool needsUnrestrictedPointerParameters;
};

static const std::vector<ParamValueCase>& kFunctionParamValueCases() {
    static const std::vector<ParamValueCase> cases = {
        {"u32_literal", "0u", {"u32"}, false},
        {"i32_literal", "0i", {"i32"}, false},
        {"f32_literal", "0f", {"f32"}, false},
        {"bool_literal", "false", {"bool"}, false},
        {"abstract_int_literal", "0", {"u32", "i32", "f32", "f16"}, false},
        {"abstract_float_literal", "0.0", {"f32", "f16"}, false},
        {"vec2u_constructor", "vec2u()", {"vec2"}, false},
        {"vec2i_constructor", "vec2i()", {}, false},
        {"vec2f_constructor", "vec2f()", {}, false},
        {"vec2b_constructor", "vec2<bool>()", {}, false},
        {"vec3u_constructor", "vec3u()", {}, false},
        {"vec3i_constructor", "vec3i()", {"vec3"}, false},
        {"vec3f_constructor", "vec3f()", {}, false},
        {"vec3b_constructor", "vec3<bool>()", {}, false},
        {"vec4u_constructor", "vec4u()", {}, false},
        {"vec4i_constructor", "vec4i()", {}, false},
        {"vec4f_constructor", "vec4f()", {"vec4"}, false},
        {"vec4b_constructor", "vec4<bool>()", {}, false},
        {"vec2_abstract_int", "vec2(0,0)", {"vec2"}, false},
        {"vec2_abstract_float", "vec2(0.0,0)", {}, false},
        {"vec3_abstract_int", "vec3(0,0,0)", {"vec3"}, false},
        {"vec3_abstract_float", "vec3(0.0,0,0)", {}, false},
        {"vec4_abstract_int", "vec4(0,0,0,0)", {"vec4"}, false},
        {"vec4_abstract_float", "vec4(0.0,0,0,0)", {"vec4"}, false},
        {"mat2x2_constructor", "mat2x2f()", {"mat2x2"}, false},
        {"mat2x3_constructor", "mat2x3f()", {"mat2x3"}, false},
        {"mat2x4_constructor", "mat2x4f()", {"mat2x4"}, false},
        {"mat3x2_constructor", "mat3x2f()", {"mat3x2"}, false},
        {"mat3x3_constructor", "mat3x3f()", {"mat3x3"}, false},
        {"mat3x4_constructor", "mat3x4f()", {"mat3x4"}, false},
        {"mat4x2_constructor", "mat4x2f()", {"mat4x2"}, false},
        {"mat4x3_constructor", "mat4x3f()", {"mat4x3"}, false},
        {"mat4x4_constructor", "mat4x4f()", {"mat4x4"}, false},
        {"array1_constructor", "array<u32, 4>()", {"array1"}, false},
        {"array2_constructor", "array<vec2f, 2>()", {"array2"}, false},
        {"array3_constructor", "array<constructible, 4>()", {"array3"}, false},
        {"array4_constructor", "array<mat2x2f, 4>()", {"array4"}, false},
        {"array5_constructor", "array<bool, 4>()", {"array5"}, false},
        {"struct1_constructor", "constructible()", {"struct1"}, false},
        {"struct2_constructor", "struct_with_array()", {"struct2"}, false},
        {"g_u32", "g_u32", {"u32"}, false},
        {"g_i32", "g_i32", {"i32"}, false},
        {"g_f32", "g_f32", {"f32"}, false},
        {"g_bool", "g_bool", {"bool"}, false},
        {"g_vec2", "g_vec2", {"vec2"}, false},
        {"g_vec3", "g_vec3", {"vec3"}, false},
        {"g_vec4", "g_vec4", {"vec4"}, false},
        {"g_mat2x2", "g_mat2x2", {"mat2x2"}, false},
        {"g_mat2x3", "g_mat2x3", {"mat2x3"}, false},
        {"g_mat2x4", "g_mat2x4", {"mat2x4"}, false},
        {"g_mat3x2", "g_mat3x2", {"mat3x2"}, false},
        {"g_mat3x3", "g_mat3x3", {"mat3x3"}, false},
        {"g_mat3x4", "g_mat3x4", {"mat3x4"}, false},
        {"g_mat4x2", "g_mat4x2", {"mat4x2"}, false},
        {"g_mat4x3", "g_mat4x3", {"mat4x3"}, false},
        {"g_mat4x4", "g_mat4x4", {"mat4x4"}, false},
        {"g_array1", "g_array1", {"array1"}, false},
        {"g_array2", "g_array2", {"array2"}, false},
        {"g_array3", "g_array3", {"array3"}, false},
        {"g_array4", "g_array4", {"array4"}, false},
        {"g_array5", "g_array5", {"array5"}, false},
        {"g_constructible", "g_constructible", {"struct1"}, false},
        {"g_struct_with_array", "g_struct_with_array", {"struct2"}, false},
        {"f_u32", "f_u32", {"u32"}, false},
        {"f_i32", "f_i32", {"i32"}, false},
        {"f_f32", "f_f32", {"f32"}, false},
        {"f_bool", "f_bool", {"bool"}, false},
        {"f_vec2", "f_vec2", {"vec2"}, false},
        {"f_vec3", "f_vec3", {"vec3"}, false},
        {"f_vec4", "f_vec4", {"vec4"}, false},
        {"f_mat2x2", "f_mat2x2", {"mat2x2"}, false},
        {"f_mat2x3", "f_mat2x3", {"mat2x3"}, false},
        {"f_mat2x4", "f_mat2x4", {"mat2x4"}, false},
        {"f_mat3x2", "f_mat3x2", {"mat3x2"}, false},
        {"f_mat3x3", "f_mat3x3", {"mat3x3"}, false},
        {"f_mat3x4", "f_mat3x4", {"mat3x4"}, false},
        {"f_mat4x2", "f_mat4x2", {"mat4x2"}, false},
        {"f_mat4x3", "f_mat4x3", {"mat4x3"}, false},
        {"f_mat4x4", "f_mat4x4", {"mat4x4"}, false},
        {"f_array1", "f_array1", {"array1"}, false},
        {"f_array2", "f_array2", {"array2"}, false},
        {"f_array3", "f_array3", {"array3"}, false},
        {"f_array4", "f_array4", {"array4"}, false},
        {"f_array5", "f_array5", {"array5"}, false},
        {"f_constructible", "f_constructible", {"struct1"}, false},
        {"f_struct_with_array", "f_struct_with_array", {"struct2"}, false},
        {"g_index_u32", "g_constructible.b", {"u32"}, false},
        {"g_index_i32", "g_constructible.a", {"i32"}, false},
        {"g_index_f32", "g_constructible.c", {"f32"}, false},
        {"g_index_bool", "g_constructible.d", {"bool"}, false},
        {"f_index_u32", "f_constructible.b", {"u32"}, false},
        {"f_index_i32", "f_constructible.a", {"i32"}, false},
        {"f_index_f32", "f_constructible.c", {"f32"}, false},
        {"f_index_bool", "f_constructible.d", {"bool"}, false},
        {"g_array_index_u32", "g_struct_with_array.a[0].b", {"u32"}, false},
        {"g_array_index_i32", "g_struct_with_array.a[1].a", {"i32"}, false},
        {"g_array_index_f32", "g_struct_with_array.a[2].c", {"f32"}, false},
        {"g_array_index_bool", "g_struct_with_array.a[3].d", {"bool"}, false},
        {"f_array_index_u32", "f_struct_with_array.a[0].b", {"u32"}, false},
        {"f_array_index_i32", "f_struct_with_array.a[1].a", {"i32"}, false},
        {"f_array_index_f32", "f_struct_with_array.a[2].c", {"f32"}, false},
        {"f_array_index_bool", "f_struct_with_array.a[3].d", {"bool"}, false},
        {"texture_sample", "t", {"texture_sample"}, false},
        {"texture_depth", "t_depth", {"texture_depth"}, false},
        {"texture_multisampled", "t_multisampled", {"texture_multisampled"}, false},
        {"texture_storage", "t_storage", {"texture_storage"}, false},
        {"texture_external", "t_external", {"texture_external"}, false},
        {"sampler", "s", {"sampler"}, false},
        {"sampler_comparison", "s_depth", {"sampler_comparison"}, false},
        {"ptr1", "&f_u32", {"ptr1"}, false},
        {"ptr2", "&f_constructible", {"ptr2"}, false},
        {"ptr3", "&g_u32", {"ptr3"}, false},
        {"ptr4", "&g_constructible", {"ptr4"}, false},
        {"ptr_let1", "ptr_f_u32", {"ptr1"}, false},
        {"ptr_let2", "ptr_f_constructible", {"ptr2"}, false},
        {"ptr_let3", "ptr_g_u32", {"ptr3"}, false},
        {"ptr_let4", "ptr_g_constructible", {"ptr4"}, false},
        {"ptr_let5", "let_let_f_u32", {"ptr1"}, false},
        {"ptr5", "&f_constructible.b", {"ptr1"}, true},
        {"ptr6", "&g_constructible.b", {"ptr3"}, true},
        {"ptr7", "&f_struct_with_array.a[1].b", {"ptr1"}, true},
        {"ptr8", "&g_struct_with_array.a[2]", {"ptr4"}, true},
        {"ptr9", "&ro_host_shareable.b", {"ptr5", "ptr6"}, true},
        {"ptr10", "&rw_host_shareable", {"ptr10"}, true},
        {"ptr11", "&ro_host_shareable", {"ptr11"}, true},
        {"ptr12", "&uniform_host_shareable", {"ptr12"}, true},
        {"ptrWorkgroupOverrideNoDefault", "&wg_override_no_default",
         {"ptrWorkgroupOverrideNoDefault"}, true},
        {"ptrWorkgroupOverrideWithDefault", "&wg_override_default",
         {"ptrWorkgroupOverrideWithDefault"}, true},
        {"ptrWorkgroupOverrideExpr", "&wg_override_expr", {"ptrWorkgroupOverrideExpr"}, true},
    };
    return cases;
}

static std::vector<Value> caseNamesParamValue() {
    std::vector<Value> v;
    for (const ParamValueCase& c : kFunctionParamValueCases()) {
        v.emplace_back(std::string(c.key));
    }
    return v;
}

static const ParamValueCase& findParamValue(const std::string& name) {
    for (const ParamValueCase& c : kFunctionParamValueCases()) {
        if (name == c.key) {
            return c;
        }
    }
    static const ParamValueCase dummy{"", "", {}, false};
    return dummy;
}

static bool parameterMatches(const std::string& decl, const std::vector<std::string>& matches) {
    for (const std::string& m : matches) {
        if (decl == m) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// param_use cases (param_scope_is_function_body)
// ---------------------------------------------------------------------------
struct ParamUseCase {
    const char* name;
    const char* code;
};

static const std::vector<ParamUseCase>& kParamUseCases() {
    static const std::vector<ParamUseCase> cases = {
        {"body",
         "fn foo(param : u32) {\n    let tmp = param;\n  }"},
        {"var", "var<private> v : u32 = param;\n  fn foo(param : u32) { }"},
        {"const", "const c : u32 = param;\n  fn foo(param : u32) { }"},
        {"override", "override o : u32 = param;\n  fn foo(param : u32) { }"},
        {"function", "fn bar() { let tmp = param; }\n  fn foo(param : u32) { }"},
    };
    return cases;
}

static std::vector<Value> caseNamesParamUse() {
    std::vector<Value> v;
    for (const ParamUseCase& c : kParamUseCases()) {
        v.emplace_back(std::string(c.name));
    }
    return v;
}

static const ParamUseCase& findParamUse(const std::string& name) {
    for (const ParamUseCase& c : kParamUseCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ParamUseCase dummy{"", ""};
    return dummy;
}

// ---------------------------------------------------------------------------
// call_arg_types_match_*
// ---------------------------------------------------------------------------
const std::vector<std::string> kParamsTypes = {"u32", "i32", "f32"};

struct ArgValue {
    const char* key;
    const char* value;
    std::vector<std::string> matches;
};

static const std::vector<ArgValue>& kArgValues() {
    static const std::vector<ArgValue> values = {
        {"abstract_int", "0", {"u32", "i32", "f32"}},
        {"abstract_float", "0.0", {"f32"}},
        {"unsigned_int", "0u", {"u32"}},
        {"signed_int", "0i", {"i32"}},
        {"float", "0f", {"f32"}},
    };
    return values;
}

static std::vector<Value> argValueNames() {
    std::vector<Value> v;
    for (const ArgValue& a : kArgValues()) {
        v.emplace_back(std::string(a.key));
    }
    return v;
}

static const ArgValue& findArgValue(const std::string& name) {
    for (const ArgValue& a : kArgValues()) {
        if (name == a.key) {
            return a;
        }
    }
    static const ArgValue dummy{"", "", {}};
    return dummy;
}

static bool checkArgTypeMatch(const std::string& paramType,
                              const std::vector<std::string>& argMatches) {
    for (const std::string& m : argMatches) {
        if (m == paramType) {
            return true;
        }
    }
    return false;
}

static std::vector<Value> paramsTypeNames() {
    std::vector<Value> v;
    for (const std::string& s : kParamsTypes) {
        v.emplace_back(s);
    }
    return v;
}

// ---------------------------------------------------------------------------
// function_attributes
// ---------------------------------------------------------------------------
struct AttrCase {
    const char* name;
    const char* attr;
    bool passFunc;
    bool passParam;
    bool passRet;
};

static const std::vector<AttrCase>& kAttributes() {
    static const std::vector<AttrCase> cases = {
        {"align", "@align(5)", false, false, false},
        {"binding", "@binding(5)", false, false, false},
        {"builtin", "@builtin(position)", false, false, false},
        {"compute", "@compute", false, false, false},
        {"const", "@const", false, false, false},
        {"diagnostic", "@diagnostic(off, derivative_uniformity)", true, false, false},
        {"fragment", "@fragment", false, false, false},
        {"group", "@group(1)", false, false, false},
        {"id", "@id(1)", false, false, false},
        {"interpolate", "@interpolate(linear, center)", false, false, false},
        {"invariant", "@invariant", false, false, false},
        {"location", "@location(0)", false, false, false},
        {"must_use", "@must_use", true, false, false},
        {"size", "@size(10)", false, false, false},
        {"vertex", "@vertex", false, false, false},
        {"workgroup_size", "@workgroup_size(1)", false, false, false},
    };
    return cases;
}

static std::vector<Value> caseNamesAttr() {
    std::vector<Value> v;
    for (const AttrCase& c : kAttributes()) {
        v.emplace_back(std::string(c.name));
    }
    return v;
}

static const AttrCase& findAttr(const std::string& name) {
    for (const AttrCase& c : kAttributes()) {
        if (name == c.name) {
            return c;
        }
    }
    static const AttrCase dummy{"", "", false, false, false};
    return dummy;
}

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,functions,restrictions", "Validation tests for function restrictions");

CTS_TEST(g, "vertex_returns_position")
    .desc("Test that a vertex shader should return position")
    .params([](ParamsBuilder u) { return u.combine("case", caseNamesVP()); })
    .fn([](ShaderValidationTest& t) {
        const VertexPosCase& tc = findVP(t.param<std::string>("case"));
        const std::string code =
            std::string(
                "\nstruct pos_struct {\n  @builtin(position) pos : vec4f\n}\n\nstruct "
                "no_pos_struct {\n  @location(0) x : vec4f\n}\n\n@vertex\nfn main() -> ") +
            tc.type + " {\n  return " + tc.value + ";\n}";
        t.expectCompileResult(tc.valid, code);
    });

CTS_TEST(g, "entry_point_call_target")
    .desc("Test that an entry point cannot be the target of a function call")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {"@fragment", "@vertex", "@compute @workgroup_size(1,1,1)"})
            .combine("entry_point", {"with", "without"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string stage = t.param<std::string>("stage");
        const std::string entryPoint = t.param<std::string>("entry_point");
        const bool useAttr = entryPoint == "with";
        std::string retAttr;
        if (useAttr && stage == "@vertex") {
            retAttr = "@builtin(position)";
        }
        const bool isVertex = stage.rfind("@vertex", 0) == 0;
        const std::string ret = isVertex ? ("-> " + retAttr + " vec4f") : "";
        const std::string retValue = isVertex ? "return vec4f();" : "";
        const std::string call = isVertex ? "let tmp = bar();" : "bar();";
        const std::string stageAttr = useAttr ? stage : "";
        const std::string code = "\n" + stageAttr + "\nfn bar() " + ret + " {\n  " + retValue +
                                 "\n}\n\nfn foo() {\n  " + call + "\n}\n";
        t.expectCompileResult(!useAttr, code);
    });

CTS_TEST(g, "function_return_types")
    .desc("Test that function return types must be constructible")
    .params([](ParamsBuilder u) { return u.combine("case", caseNamesRet()); })
    .fn([](ShaderValidationTest& t) {
        const RetTypeCase& tc = findRet(t.param<std::string>("case"));
        const std::string enable = std::string(tc.name) == "f16" ? "enable f16;" : "";
        const std::string value =
            std::string(tc.value).empty() ? (std::string(tc.name) + "()") : std::string(tc.value);
        const std::string code =
            "\n" + enable + "\n\n" + kCCommonTypeDecls +
            "\nstruct atomic_struct {\n  a : atomic<u32>\n};\n\noverride override_size : u32;\n\n"
            "var<workgroup> atomic_wg : atomic<u32>;\n\n@group(0) @binding(0)\nvar t : "
            "texture_2d<f32>;\n@group(0) @binding(1)\nvar s : sampler;\n@group(0) @binding(2)\nvar "
            "s_depth : sampler_comparison;\n@group(0) @binding(3)\nvar t_storage : "
            "texture_storage_2d<rgba8unorm, write>;\n@group(0) @binding(4)\nvar t_depth : "
            "texture_depth_2d;\n@group(0) @binding(5)\nvar t_multisampled : "
            "texture_multisampled_2d<f32>;\n@group(0) @binding(6)\nvar t_external : "
            "texture_external;\n\nfn foo() -> " +
            tc.name + " {\n  return " + value + ";\n}";
        t.expectCompileResult(tc.valid, code);
    });

CTS_TEST(g, "function_parameter_types")
    .desc("Test validation of user-declared function parameter types")
    .params([](ParamsBuilder u) { return u.combine("case", caseNamesParamType()); })
    .fn([](ShaderValidationTest& t) {
        const ParamTypeCase& tc = findParamType(t.param<std::string>("case"));
        const std::string enable = std::string(tc.name) == "f16" ? "enable f16;" : "";
        const std::string code = "\n" + enable + "\n\n" + kCCommonTypeDecls + "\nfn foo(param : " +
                                 tc.name + ") {\n}";

        bool isValid = false;
        if (tc.valid == ParamValid::kTrue) {
            isValid = true;
        } else if (tc.valid == ParamValid::kWithUnrestrictedPointerParameters) {
            isValid = t.hasLanguageFeature("unrestricted_pointer_parameters");
        }
        t.expectCompileResult(isValid, code);
    });

CTS_TEST(g, "function_parameter_matching")
    .desc("Test that function parameter types match function parameter type on user-declared "
          "functions")
    .params([](ParamsBuilder u) {
        return u.combine("decl", caseNamesParamType())
            .filter([](const ParamRecord& p) {
                return findParamType(valueAs<std::string>(*findParam(p, "decl"))).valid !=
                       ParamValid::kFalse;
            })
            .beginSubcases()
            .combine("arg", caseNamesParamValue());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string declKey = t.param<std::string>("decl");
        const ParamTypeCase& param = findParamType(declKey);
        const ParamValueCase& arg = findParamValue(t.param<std::string>("arg"));
        const std::string enable = std::string(param.name) == "f16" ? "enable f16;" : "";
        const std::string code =
            "\n" + enable + "\n\n" + kCCommonTypeDecls +
            "@group(0) @binding(0)\nvar t : texture_2d<f32>;\n@group(0) @binding(1)\nvar s : "
            "sampler;\n@group(0) @binding(2)\nvar s_depth : sampler_comparison;\n@group(0) "
            "@binding(3)\nvar t_storage : texture_storage_2d<rgba8unorm, write>;\n@group(0) "
            "@binding(4)\nvar t_depth : texture_depth_2d;\n@group(0) @binding(5)\nvar "
            "t_multisampled : texture_multisampled_2d<f32>;\n@group(0) @binding(6)\nvar t_external "
            ": texture_external;\n\n@group(1) @binding(0)\nvar<storage> ro_host_shareable : "
            "host_shareable;\n@group(1) @binding(1)\nvar<storage, read_write> rw_host_shareable : "
            "host_shareable;\n@group(1) @binding(2)\nvar<uniform> uniform_host_shareable : "
            "host_shareable;\n\nfn bar(param : " +
            param.name +
            ") { }\n\nvar<private> g_u32 : u32;\nvar<private> g_i32 : i32;\nvar<private> g_f32 : "
            "f32;\nvar<private> g_bool : bool;\nvar<private> g_vec2 : vec2u;\nvar<private> g_vec3 "
            ": vec3i;\nvar<private> g_vec4 : vec4f;\nvar<private> g_mat2x2 : "
            "mat2x2f;\nvar<private> g_mat2x3 : mat2x3f;\nvar<private> g_mat2x4 : "
            "mat2x4f;\nvar<private> g_mat3x2 : mat3x2f;\nvar<private> g_mat3x3 : "
            "mat3x3f;\nvar<private> g_mat3x4 : mat3x4f;\nvar<private> g_mat4x2 : "
            "mat4x2f;\nvar<private> g_mat4x3 : mat4x3f;\nvar<private> g_mat4x4 : "
            "mat4x4f;\nvar<private> g_array1 : array<u32, 4>;\nvar<private> g_array2 : array<vec2f, "
            "2>;\nvar<private> g_array3 : array<constructible, 4>;\nvar<private> g_array4 : "
            "array<mat2x2f, 4>;\nvar<private> g_array5 : array<bool, 4>;\nvar<private> "
            "g_constructible : constructible;\nvar<private> g_struct_with_array : "
            "struct_with_array;\n\nvar<workgroup> wg_override_no_default : array<u32, "
            "override_no_default>;\nvar<workgroup> wg_override_default : array<f32, "
            "override_default>;\nvar<workgroup> wg_override_expr : array<vec4f, "
            "override_expr>;\n\nfn foo() {\n  var f_u32 : u32;\n  var f_i32 : i32;\n  var f_f32 : "
            "f32;\n  var f_bool : bool;\n  var f_vec2 : vec2u;\n  var f_vec3 : vec3i;\n  var f_vec4 "
            ": vec4f;\n  var f_mat2x2 : mat2x2f;\n  var f_mat2x3 : mat2x3f;\n  var f_mat2x4 : "
            "mat2x4f;\n  var f_mat3x2 : mat3x2f;\n  var f_mat3x3 : mat3x3f;\n  var f_mat3x4 : "
            "mat3x4f;\n  var f_mat4x2 : mat4x2f;\n  var f_mat4x3 : mat4x3f;\n  var f_mat4x4 : "
            "mat4x4f;\n  var f_array1 : array<u32, 4>;\n  var f_array2 : array<vec2f, 2>;\n  var "
            "f_array3 : array<constructible, 4>;\n  var f_array4 : array<mat2x2f, 4>;\n  var "
            "f_array5 : array<bool, 4>;\n  var f_constructible : constructible;\n  var "
            "f_struct_with_array : struct_with_array;\n  let ptr_f_u32 = &f_u32;\n  let "
            "ptr_f_constructible = &f_constructible;\n  let ptr_g_u32 = &g_u32;\n  let "
            "ptr_g_constructible = &g_constructible;\n  let let_let_f_u32 = ptr_f_u32;\n\n  bar(" +
            arg.value + ");\n}\n";

        const bool needsUnrestricted =
            (param.valid == ParamValid::kWithUnrestrictedPointerParameters) ||
            arg.needsUnrestrictedPointerParameters;

        bool isValid = parameterMatches(declKey, arg.matches);
        if (isValid && needsUnrestricted) {
            isValid = t.hasLanguageFeature("unrestricted_pointer_parameters");
        }
        t.expectCompileResult(isValid, code);
    });

CTS_TEST(g, "no_direct_recursion")
    .desc("Test that functions cannot be directly recursive")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "\nfn foo() {\n  foo();\n}");
    });

CTS_TEST(g, "no_indirect_recursion")
    .desc("Test that functions cannot be indirectly recursive")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(false, "\nfn bar() {\n  foo();\n}\nfn foo() {\n  bar();\n}");
    });

CTS_TEST(g, "param_names_must_differ")
    .desc("Test that function parameters must have different names")
    .params([](ParamsBuilder u) {
        return u.combine("p1", {"a", "b", "c"}).combine("p2", {"a", "b", "c"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string p1 = t.param<std::string>("p1");
        const std::string p2 = t.param<std::string>("p2");
        const std::string code = "fn foo(" + p1 + " : u32, " + p2 + " : f32) { }";
        t.expectCompileResult(p1 != p2, code);
    });

CTS_TEST(g, "param_scope_is_function_body")
    .desc("Test that function parameters are only in scope in the function body")
    .params([](ParamsBuilder u) { return u.combine("use", caseNamesParamUse()); })
    .fn([](ShaderValidationTest& t) {
        const std::string use = t.param<std::string>("use");
        t.expectCompileResult(use == "body", findParamUse(use).code);
    });

CTS_TEST(g, "param_number_matches_call")
    .desc("Test that function calls have an equal number of arguments as the number of parameters")
    .params([](ParamsBuilder u) {
        return u.combine("num_args", {0, 1, 2, 3, 4, 255})
            .combine("num_params", {0, 1, 2, 3, 4, 255});
    })
    .fn([](ShaderValidationTest& t) {
        const int numArgs = t.param<int>("num_args");
        const int numParams = t.param<int>("num_params");
        std::string code = "\n    fn bar(";
        for (int i = 0; i < numParams; ++i) {
            code += "p" + std::to_string(i) + " : u32,";
        }
        code += ") { }\n";
        code += "fn foo() {\nbar(";
        for (int i = 0; i < numArgs; ++i) {
            code += "0,";
        }
        code += ");\n}";
        t.expectCompileResult(numArgs == numParams, code);
    });

CTS_TEST(g, "call_arg_types_match_1_param")
    .desc("Test that the argument types match in order")
    .params([](ParamsBuilder u) {
        return u.combine("p1_type", paramsTypeNames())
            .beginSubcases()
            .combine("arg1_value", argValueNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string p1Type = t.param<std::string>("p1_type");
        const ArgValue& arg1 = findArgValue(t.param<std::string>("arg1_value"));
        const std::string code = "\nfn bar(p1 : " + p1Type + ") { }\nfn foo() {\n  bar(" +
                                 arg1.value + ");\n}";
        const bool res = checkArgTypeMatch(p1Type, arg1.matches);
        t.expectCompileResult(res, code);
    });

CTS_TEST(g, "call_arg_types_match_2_params")
    .desc("Test that the argument types match in order")
    .params([](ParamsBuilder u) {
        return u.combine("p1_type", paramsTypeNames())
            .combine("p2_type", paramsTypeNames())
            .beginSubcases()
            .combine("arg1_value", argValueNames())
            .combine("arg2_value", argValueNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string p1Type = t.param<std::string>("p1_type");
        const std::string p2Type = t.param<std::string>("p2_type");
        const ArgValue& arg1 = findArgValue(t.param<std::string>("arg1_value"));
        const ArgValue& arg2 = findArgValue(t.param<std::string>("arg2_value"));
        const std::string code = "\nfn bar(p1 : " + p1Type + ", p2 : " + p2Type +
                                 ") { }\nfn foo() {\n  bar(" + arg1.value + ", " + arg2.value +
                                 ");\n}";
        const bool res = checkArgTypeMatch(p1Type, arg1.matches) &&
                         checkArgTypeMatch(p2Type, arg2.matches);
        t.expectCompileResult(res, code);
    });

CTS_TEST(g, "call_arg_types_match_3_params")
    .desc("Test that the argument types match in order")
    .params([](ParamsBuilder u) {
        return u.combine("p1_type", paramsTypeNames())
            .combine("p2_type", paramsTypeNames())
            .combine("p3_type", paramsTypeNames())
            .beginSubcases()
            .combine("arg1_value", argValueNames())
            .combine("arg2_value", argValueNames())
            .combine("arg3_value", argValueNames());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string p1Type = t.param<std::string>("p1_type");
        const std::string p2Type = t.param<std::string>("p2_type");
        const std::string p3Type = t.param<std::string>("p3_type");
        const ArgValue& arg1 = findArgValue(t.param<std::string>("arg1_value"));
        const ArgValue& arg2 = findArgValue(t.param<std::string>("arg2_value"));
        const ArgValue& arg3 = findArgValue(t.param<std::string>("arg3_value"));
        const std::string code = "\nfn bar(p1 : " + p1Type + ", p2 : " + p2Type + ", p3 : " +
                                 p3Type + ") { }\nfn foo() {\n  bar(" + arg1.value + ",\n      " +
                                 arg2.value + ",\n      " + arg3.value + ");\n}";
        const bool res = checkArgTypeMatch(p1Type, arg1.matches) &&
                         checkArgTypeMatch(p2Type, arg2.matches) &&
                         checkArgTypeMatch(p3Type, arg3.matches);
        t.expectCompileResult(res, code);
    });

CTS_TEST(g, "param_name_can_shadow_function_name")
    .desc("Tests that a function parameter can shadow the function name")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true, "\nfn foo(foo: i32) -> i32 {\n  return foo;\n}\n");
    });

CTS_TEST(g, "param_name_can_shadow_alias")
    .desc("Tests that a function parameter can shadow an alias")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(true,
                              "\nalias foo = f32;\nfn test(foo: i32) -> i32 {\n  return foo;\n}\n");
    });

CTS_TEST(g, "param_name_can_shadow_global")
    .desc("Tests that a function parameter can shadow a global")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(
            true, "\nconst foo: f32 = 1.2f;\n\nfn test(foo: i32) -> i32 {\n  return foo;\n}\n");
    });

CTS_TEST(g, "param_comma_placement")
    .desc("Tests validation of commas in function parameter lists")
    .params([](ParamsBuilder u) {
        return u.combine("param_1", {true, false})
            .combine("param_2", {true, false})
            .combine("comma", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const bool hasP1 = t.param<bool>("param_1");
        const bool hasP2 = t.param<bool>("param_2");
        const bool hasC = t.param<bool>("comma");

        const std::string p1 = hasP1 ? "foo: i32" : "";
        const std::string p2 = hasP2 ? "bar: f32" : "";
        const std::string comma = hasC ? ", " : " ";

        const std::string code = "\nfn test(" + p1 + comma + p2 + ") {\n}\n";

        const bool success = (!hasP1 && !hasP2 && !hasC) || (hasP1 && !hasP2) ||
                             (!hasP1 && hasP2 && !hasC) || (hasP1 && hasP2 && hasC);
        t.expectCompileResult(success, code);
    });

CTS_TEST(g, "param_type_can_be_alias")
    .desc("Tests that a function parameter type can be an alias")
    .fn([](ShaderValidationTest& t) {
        t.expectCompileResult(
            true, "\nalias foo = f32;\nfn test(foo: foo) -> foo {\n  return foo;\n}\n");
    });

CTS_TEST(g, "function_name_required")
    .desc("Tests the function name is required")
    .params([](ParamsBuilder u) { return u.combine("name", {true, false}); })
    .fn([](ShaderValidationTest& t) {
        const bool hasName = t.param<bool>("name");
        const std::string name = hasName ? "name" : "";
        const std::string code = "\nfn " + name + "() -> i32 {\n  return 1;\n}\n";
        t.expectCompileResult(hasName, code);
    });

CTS_TEST(g, "param_type_required")
    .desc("Tests the parameter type is required")
    .params([](ParamsBuilder u) {
        return u.combine("ty", {true, false}).combine("colon", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const bool hasTy = t.param<bool>("ty");
        const bool hasColon = t.param<bool>("colon");
        const std::string ty = hasTy ? "i32" : "";
        const std::string colon = hasColon ? ":" : " ";
        const std::string code = "\nfn f(foo" + colon + ty + ") -> i32 {\n  return 1;\n}\n";
        t.expectCompileResult(hasTy && hasColon, code);
    });

CTS_TEST(g, "body_required")
    .desc("Tests the function body is required")
    .params([](ParamsBuilder u) { return u.combine("body", {"braces", "semi", ""}); })
    .fn([](ShaderValidationTest& t) {
        const std::string bodyParam = t.param<std::string>("body");
        const std::string body = bodyParam == "braces" ? "{}" : (bodyParam == "semi" ? ";" : "");
        const std::string code = "\nfn f() " + body + "\n\nfn other() {}\n";
        t.expectCompileResult(body == "{}", code);
    });

CTS_TEST(g, "parens_required")
    .desc("Tests that the parens for a function are required")
    .params([](ParamsBuilder u) {
        return u.combine("parens", {true, false}).combine("param", {true, false});
    })
    .fn([](ShaderValidationTest& t) {
        const bool hasParens = t.param<bool>("parens");
        const bool hasParam = t.param<bool>("param");
        std::string args = "";
        if (hasParens) {
            args += "(";
        }
        if (hasParam) {
            args += "foo: i32";
        }
        if (hasParens) {
            args += ")";
        }
        const std::string code = "\nfn f " + args + " {}\n";
        t.expectCompileResult(hasParens, code);
    });

CTS_TEST(g, "non_module_scoped_function")
    .desc("Tests that a non-module-scope function is rejected")
    .params([](ParamsBuilder u) { return u.combine("loc", {"inner", "outer"}); })
    .fn([](ShaderValidationTest& t) {
        const std::string loc = t.param<std::string>("loc");
        const std::string o = "fn a() -> i32 { return 1; }";
        std::string inner = "";
        std::string outer = "";
        if (loc == "inner") {
            inner = o;
        } else {
            outer = o;
        }
        const std::string code = "\n" + outer + "\nfn b() {\n  " + inner + "\n}\n";
        t.expectCompileResult(loc == "outer", code);
    });

CTS_TEST(g, "function_attributes")
    .desc("Tests the attributes for a function")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNamesAttr()).combine("placement", {"func", "param", "ret"});
    })
    .fn([](ShaderValidationTest& t) {
        const AttrCase& d = findAttr(t.param<std::string>("case"));
        const std::string placement = t.param<std::string>("placement");
        const bool func = placement == "func";
        const bool param = placement == "param";
        const bool ret = placement == "ret";

        const std::string code = "\n" + std::string(func ? d.attr : "") + "\nfn b(" +
                                 std::string(param ? d.attr : "") + " foo: i32) -> " +
                                 std::string(ret ? d.attr : "") + " i32{\n  return 1;\n}\n";
        // Mirrors upstream exactly, including the `placement === 'params'` typo
        // (which never matches the 'param' value, so for placement 'param' the
        // result falls through to d.pass.ret).
        bool succeed;
        if (placement == "func") {
            succeed = d.passFunc;
        } else if (placement == "params") {
            succeed = d.passParam;
        } else {
            succeed = d.passRet;
        }
        t.expectCompileResult(succeed, code);
    });

CTS_TEST(g, "must_use_requires_return")
    .desc("Tests the must_use attribute requires a return")
    .params([](ParamsBuilder u) { return u.combine("ret", {true, false}); })
    .fn([](ShaderValidationTest& t) {
        const bool hasRet = t.param<bool>("ret");
        std::string ret = "";
        std::string retStmt = "";
        if (hasRet) {
            ret = "-> i32";
            retStmt = "return 1;";
        }
        const std::string code = "\n@must_use\nfn b() " + ret + " {\n  " + retStmt + "\n}\n";
        t.expectCompileResult(hasRet, code);
    });

CTS_TEST(g, "overload")
    .desc("Tests that user functions can not overload ")
    .params([](ParamsBuilder u) { return u.combine("overload", {true, false}); })
    .fn([](ShaderValidationTest& t) {
        const bool overload = t.param<bool>("overload");
        std::string code = "fn a(f: i32) {}\n";
        if (overload) {
            code += "fn a(f: u32) {}";
        }
        t.expectCompileResult(overload == false, code);
    });

}  // namespace
