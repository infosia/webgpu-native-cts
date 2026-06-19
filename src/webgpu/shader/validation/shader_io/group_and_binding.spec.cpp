// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/group_and_binding.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - All tests are compile-only (expectCompileResult); upstream performs no
//     pipeline creation here. The resource declaration emitters, kResourceKinds*
//     selections and declareEntrypoint are mirrored from util.ts.

#include <functional>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,group_and_binding",
    "Validation tests for group and binding");

// ---------------------------------------------------------------------------
// Resource declaration emitters (mirror util.ts).
// An emitter produces a `var` declaration string for the given name, optional
// group and optional binding. -1 means "undefined" (omit the attribute).
// ---------------------------------------------------------------------------
using ResourceDeclarationEmitter = std::function<std::string(const std::string&, int, int)>;

static std::string groupAndBinding(int group, int binding) {
    std::string g_attr = group >= 0 ? "@group(" + std::to_string(group) + ")" : "/* no group */";
    std::string b_attr = binding >= 0 ? "@binding(" + std::to_string(binding) + ")" : "/* no binding */";
    return g_attr + " " + b_attr;
}

static ResourceDeclarationEmitter basicEmitter(const std::string& type) {
    return [type](const std::string& name, int group, int binding) {
        return groupAndBinding(group, binding) + " var " + name + " : " + type + ";\n";
    };
}

struct ResourceEntry {
    const char* kind;
    ResourceDeclarationEmitter emitter;
};

static const std::vector<ResourceEntry>& kResourceEmitters() {
    static const std::vector<ResourceEntry> entries = {
        {"texture_1d", basicEmitter("texture_1d<i32>")},
        {"texture_2d", basicEmitter("texture_2d<i32>")},
        {"texture_2d_array", basicEmitter("texture_2d_array<f32>")},
        {"texture_3d", basicEmitter("texture_3d<i32>")},
        {"texture_cube", basicEmitter("texture_cube<u32>")},
        {"texture_cube_array", basicEmitter("texture_cube_array<u32>")},
        {"texture_multisampled_2d", basicEmitter("texture_multisampled_2d<i32>")},
        {"texture_external", basicEmitter("texture_external")},
        {"texture_storage_1d", basicEmitter("texture_storage_1d<rgba8unorm, write>")},
        {"texture_storage_2d", basicEmitter("texture_storage_2d<rgba8sint, write>")},
        {"texture_storage_2d_array", basicEmitter("texture_storage_2d_array<r32uint, write>")},
        {"texture_storage_3d", basicEmitter("texture_storage_3d<rgba32uint, write>")},
        {"texture_depth_2d", basicEmitter("texture_depth_2d")},
        {"texture_depth_2d_array", basicEmitter("texture_depth_2d_array")},
        {"texture_depth_cube", basicEmitter("texture_depth_cube")},
        {"texture_depth_cube_array", basicEmitter("texture_depth_cube_array")},
        {"texture_depth_multisampled_2d", basicEmitter("texture_depth_multisampled_2d")},
        {"sampler", basicEmitter("sampler")},
        {"sampler_comparison", basicEmitter("sampler_comparison")},
        {"uniform",
         [](const std::string& name, int group, int binding) {
             return groupAndBinding(group, binding) + " var<uniform> " + name +
                    " : array<vec4<f32>, 16>;\n";
         }},
        {"storage",
         [](const std::string& name, int group, int binding) {
             return groupAndBinding(group, binding) + " var<storage> " + name +
                    " : array<vec4<f32>, 16>;\n";
         }},
    };
    return entries;
}

static const ResourceDeclarationEmitter& findEmitter(const std::string& kind) {
    for (const ResourceEntry& e : kResourceEmitters()) {
        if (kind == e.kind) {
            return e.emitter;
        }
    }
    static const ResourceDeclarationEmitter dummy =
        [](const std::string&, int, int) { return std::string(); };
    return dummy;
}

static const std::vector<std::string> kResourceKindsAll = {
    "texture_1d", "texture_2d", "texture_2d_array", "texture_3d", "texture_cube",
    "texture_cube_array", "texture_multisampled_2d", "texture_external",
    "texture_storage_1d", "texture_storage_2d", "texture_storage_2d_array",
    "texture_storage_3d", "texture_depth_2d", "texture_depth_2d_array",
    "texture_depth_cube", "texture_depth_cube_array", "texture_depth_multisampled_2d",
    "sampler", "sampler_comparison", "uniform", "storage",
};

static const std::vector<std::string> kResourceKindsA = {
    "storage", "texture_2d", "texture_external", "uniform"};

static const std::vector<std::string> kResourceKindsB = {
    "texture_3d", "texture_storage_1d", "uniform"};

static std::vector<Value> toValues(const std::vector<std::string>& strs) {
    std::vector<Value> values;
    for (const std::string& s : strs) {
        values.emplace_back(s);
    }
    return values;
}

// declareEntrypoint (mirror util.ts).
static std::string declareEntrypoint(const std::string& name,
                                     const std::string& stage,
                                     const std::string& body) {
    if (stage == "vertex") {
        return "@vertex\nfn " + name + "() -> @builtin(position) vec4f {\n  " + body +
               "\n  return vec4f();\n}";
    }
    if (stage == "fragment") {
        return "@fragment\nfn " + name + "() {\n  " + body + "\n}";
    }
    // compute
    return "@compute @workgroup_size(1)\nfn " + name + "() {\n  " + body + "\n}";
}

CTS_TEST(g, "binding_attributes")
    .desc("Test that both @group and @binding attributes must both be declared.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {std::string("vertex"), std::string("fragment"), std::string("compute")})
                .combine("has_group", {Value(true), Value(false)})
                .combine("has_binding", {Value(true), Value(false)})
                .combine("resource", toValues(kResourceKindsAll))
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const ResourceDeclarationEmitter& emitter = findEmitter(t.param<std::string>("resource"));
        const bool has_group = t.param<bool>("has_group");
        const bool has_binding = t.param<bool>("has_binding");
        const std::string code = emitter("R", has_group ? 0 : -1, has_binding ? 0 : -1);
        const bool expect = has_group && has_binding;
        t.expectCompileResult(expect, code);
    });

CTS_TEST(g, "private_module_scope")
    .desc("Test validation of group and binding on private resources")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@group(1) @binding(1)"
            "\nvar<private> a: i32;"
            "\n"
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  _ = a;"
            "\n}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "private_function_scope")
    .desc("Test validation of group and binding on function-scope private resources")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  @group(1) @binding(1)"
            "\n  var<private> a: i32;"
            "\n}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "function_scope")
    .desc("Test validation of group and binding on function-scope private resources")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  @group(1) @binding(1)"
            "\n  var a: i32;"
            "\n}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "function_scope_texture")
    .desc("Test validation of group and binding on function-scope private resources")
    .fn([](ShaderValidationTest& t) {
        const std::string code =
            "\n@workgroup_size(1, 1, 1)"
            "\n@compute fn main() {"
            "\n  @group(1) @binding(1)"
            "\n  var a: texture_2d<f32>;"
            "\n}";
        t.expectCompileResult(false, code);
    });

CTS_TEST(g, "single_entry_point")
    .desc("Test that two different resource variables in a shader must not have the same group and binding values, when considered as a pair.")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {std::string("vertex"), std::string("fragment"), std::string("compute")})
                .combine("a_kind", toValues(kResourceKindsA))
                .combine("b_kind", toValues(kResourceKindsB))
                .combine("usage", {std::string("direct"), std::string("transitive")})
                .filter([](const ParamRecord& p) {
                    return !(valueAs<std::string>(*findParam(p, "stage")) == "vertex" &&
                             valueAs<std::string>(*findParam(p, "b_kind")) == "texture_storage_1d");
                })
                .beginSubcases()
                .combine("a_group", {Value(0), Value(3)})
                .combine("b_group", {Value(0), Value(3)})
                .combine("a_binding", {Value(0), Value(3)})
                .combine("b_binding", {Value(0), Value(3)});
    })
    .fn([](ShaderValidationTest& t) {
        const ResourceDeclarationEmitter& resourceA = findEmitter(t.param<std::string>("a_kind"));
        const ResourceDeclarationEmitter& resourceB = findEmitter(t.param<std::string>("b_kind"));
        const std::string stage = t.param<std::string>("stage");
        const std::string usage = t.param<std::string>("usage");
        const int a_group = static_cast<int>(t.param<int64_t>("a_group"));
        const int b_group = static_cast<int>(t.param<int64_t>("b_group"));
        const int a_binding = static_cast<int>(t.param<int64_t>("a_binding"));
        const int b_binding = static_cast<int>(t.param<int64_t>("b_binding"));

        const std::string resources =
            "\n" + resourceA("resource_a", a_group, a_binding) +
            "\n" + resourceB("resource_b", b_group, b_binding) + "\n";
        const bool expect = a_group != b_group || a_binding != b_binding;

        std::string code;
        if (usage == "direct") {
            code = "\n" + resources + "\n" +
                   declareEntrypoint("main", stage, "_ = resource_a; _ = resource_b;") + "\n";
        } else {
            code = "\n" + resources + "\n"
                   "fn use_a() { _ = resource_a; }\n"
                   "fn use_b() { _ = resource_b; }\n" +
                   declareEntrypoint("main", stage, "use_a(); use_b();") + "\n";
        }
        t.expectCompileResult(expect, code);
    });

CTS_TEST(g, "different_entry_points")
    .desc("Test that resources may use the same binding points if exclusively accessed by different entry points.")
    .params([](ParamsBuilder u) {
        return u.combine("a_stage", {std::string("vertex"), std::string("fragment"), std::string("compute")})
                .combine("b_stage", {std::string("vertex"), std::string("fragment"), std::string("compute")})
                .combine("a_kind", toValues(kResourceKindsA))
                .combine("b_kind", toValues(kResourceKindsB))
                .combine("usage", {std::string("direct"), std::string("transitive")})
                .filter([](const ParamRecord& p) {
                    return !(valueAs<std::string>(*findParam(p, "b_stage")) == "vertex" &&
                             valueAs<std::string>(*findParam(p, "b_kind")) == "texture_storage_1d");
                })
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const ResourceDeclarationEmitter& resourceA = findEmitter(t.param<std::string>("a_kind"));
        const ResourceDeclarationEmitter& resourceB = findEmitter(t.param<std::string>("b_kind"));
        const std::string a_stage = t.param<std::string>("a_stage");
        const std::string b_stage = t.param<std::string>("b_stage");
        const std::string usage = t.param<std::string>("usage");

        const std::string resources =
            "\n" + resourceA("resource_a", 0, 0) +
            "\n" + resourceB("resource_b", 0, 0) + "\n";
        const bool expect = true;  // Binding reuse between different entry points is fine.

        std::string code;
        if (usage == "direct") {
            code = "\n" + resources + "\n" +
                   declareEntrypoint("main_a", a_stage, "_ = resource_a;") + "\n" +
                   declareEntrypoint("main_b", b_stage, "_ = resource_b;") + "\n";
        } else {
            code = "\n" + resources + "\n"
                   "fn use_a() { _ = resource_a; }\n"
                   "fn use_b() { _ = resource_b; }\n" +
                   declareEntrypoint("main_a", a_stage, "use_a();") + "\n" +
                   declareEntrypoint("main_b", b_stage, "use_b();") + "\n";
        }
        t.expectCompileResult(expect, code);
    });

} // namespace
