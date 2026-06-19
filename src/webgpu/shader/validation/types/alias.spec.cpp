// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/alias.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,types,alias",
    "Validation tests for type aliases");

CTS_TEST(g, "no_direct_recursion")
    .desc("Test that direct recursion of type aliases is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "T"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "alias T = " + target + ";";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion")
    .desc("Test that indirect recursion of type aliases is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "S"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nalias S = T;\nalias T = " + target + ";\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_vector_element")
    .desc("Test that indirect recursion of type aliases via vector element types is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "V"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nalias V = vec4<T>;\nalias T = " + target + ";\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_matrix_element")
    .desc("Test that indirect recursion of type aliases via matrix element types is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"f32", "M"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nalias M = mat4x4<T>;\nalias T = " + target + ";\n";
        t.expectCompileResult(target == "f32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_array_element")
    .desc("Test that indirect recursion of type aliases via array element types is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "A"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nalias A = array<T, 4>;\nalias T = " + target + ";\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_array_size")
    .desc("Test that indirect recursion of type aliases via array size expressions is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "A"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nalias A = array<i32, T(1)>;\nalias T = " + target + ";\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_atomic")
    .desc("Test that indirect recursion of type aliases via atomic types is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "A"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nalias A = atomic<T>;\nalias T = " + target + ";\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_ptr_store_type")
    .desc("Test that indirect recursion of type aliases via pointer store types is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "P"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nalias P = ptr<function, T>;\nalias T = " + target + ";\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_struct_member")
    .desc("Test that indirect recursion of type aliases via struct members is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "S"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nstruct S {\n  a : T\n}\nalias T = " + target + ";\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_struct_attribute")
    .desc("Test that indirect recursion of type aliases via struct members is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "S"})
            .combine("attribute", {"align", "location", "size"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string attribute = t.param<std::string>("attribute");
        const std::string wgsl = "\nstruct S {\n  @" + attribute + "(T(4)) a : f32\n}\nalias T = " +
                                 target + ";\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

// Mirrors upstream kTypes.
static std::vector<Value> kTypes() {
    return {
        std::string("bool"),
        std::string("i32"),
        std::string("u32"),
        std::string("f32"),
        std::string("f16"),
        std::string("vec2<i32>"),
        std::string("vec3<u32>"),
        std::string("vec4<f32>"),
        std::string("mat2x2<f32>"),
        std::string("mat2x3<f32>"),
        std::string("mat2x4<f32>"),
        std::string("mat3x2<f32>"),
        std::string("mat3x3<f32>"),
        std::string("mat3x4<f32>"),
        std::string("mat4x2<f32>"),
        std::string("mat4x3<f32>"),
        std::string("mat4x4<f32>"),
        std::string("array<u32>"),
        std::string("array<i32, 4>"),
        std::string("array<vec2<u32>, 8>"),
        std::string("S"),
        std::string("T"),
        std::string("atomic<u32>"),
        std::string("atomic<i32>"),
        std::string("ptr<function, u32>"),
        std::string("ptr<private, i32>"),
        std::string("ptr<workgroup, f32>"),
        std::string("ptr<uniform, vec2f>"),
        std::string("ptr<storage, vec2u>"),
        std::string("ptr<storage, vec3i, read>"),
        std::string("ptr<storage, vec4f, read_write>"),
        std::string("sampler"),
        std::string("sampler_comparison"),
        std::string("texture_1d<f32>"),
        std::string("texture_2d<u32>"),
        std::string("texture_2d_array<i32>"),
        std::string("texture_3d<f32>"),
        std::string("texture_cube<i32>"),
        std::string("texture_cube_array<u32>"),
        std::string("texture_multisampled_2d<f32>"),
        std::string("texture_depth_multisampled_2d"),
        std::string("texture_external"),
        std::string("texture_storage_1d<rgba8snorm, write>"),
        std::string("texture_storage_1d<r32uint, write>"),
        std::string("texture_storage_1d<r32sint, read_write>"),
        std::string("texture_storage_1d<r32float, read>"),
        std::string("texture_storage_2d<rgba16uint, write>"),
        std::string("texture_storage_2d_array<rgba32float, write>"),
        std::string("texture_storage_3d<bgra8unorm, write>"),
        std::string("texture_depth_2d"),
        std::string("texture_depth_2d_array"),
        std::string("texture_depth_cube"),
        std::string("texture_depth_cube_array"),

        // Pre-declared aliases (spot check)
        std::string("vec2f"),
        std::string("vec3u"),
        std::string("vec4i"),
        std::string("mat2x2f"),

        // User-defined aliases
        std::string("anotherAlias"),
        std::string("random_alias"),
    };
}

static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

CTS_TEST(g, "any_type")
    .desc("Test that any type can be aliased")
    .params([](ParamsBuilder u) {
        return u.combine("type", kTypes());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ty = t.param<std::string>("type");
        t.skipIf(contains(ty, "texture_storage") && contains(ty, "read") &&
                     !t.hasLanguageFeature("readonly_and_readwrite_storage_textures"),
                 "Missing language feature");
        const std::string enable = ty == "f16" ? std::string("enable f16;") : std::string();
        const std::string code = "\n    " + enable +
                                 "\n    struct S { x : u32 }\n    struct T { y : S }\n    alias "
                                 "anotherAlias = u32;\n    alias random_alias = i32;\n    alias "
                                 "myType = " +
                                 ty + ";";
        t.expectCompileResult(true, code);
    });

struct MatchCase {
    const char* name;
    const char* code;
};

// Mirrors upstream kMatchCases (object key order preserved).
static const std::vector<MatchCase>& kMatchCases() {
    static const std::vector<MatchCase> cases = {
        {"function_param",
         "\n    fn foo(x : u32) { }\n    fn bar() {\n      var x : alias_alias_u32;\n      "
         "foo(x);\n    }"},
        {"constructor", "var<private> v : u32 = alias_u32(1);"},
        {"template_param", "var<private> v : vec2<alias_u32> = vec2<u32>();"},
        {"predeclared_alias", "var<private> v : vec2<alias_alias_u32> = vec2u();"},
        {"struct_element",
         "\n    struct S { x : alias_u32 }\n    const c_u32 = 0u;\n    const c = S(c_u32);"},
    };
    return cases;
}

static const MatchCase& findMatchCase(const std::string& name) {
    for (const MatchCase& c : kMatchCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const MatchCase dummy{"", ""};
    return dummy;
}

static std::vector<Value> matchCaseNames() {
    std::vector<Value> values;
    for (const MatchCase& c : kMatchCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "match_non_alias")
    .desc("Test that type checking succeeds using aliased and unaliased type")
    .params([](ParamsBuilder u) {
        return u.combine("case", matchCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const MatchCase& c = findMatchCase(t.param<std::string>("case"));
        const std::string code = "\n    alias alias_u32 = u32;\n    alias alias_alias_u32 = "
                                 "alias_u32;\n    " +
                                 std::string(c.code);
        t.expectCompileResult(true, code);
    });

}  // namespace
