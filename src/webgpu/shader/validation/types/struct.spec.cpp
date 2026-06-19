// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/types/struct.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Validation tests for struct types. All tests are compile-only
// (expectCompileResult).

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,types,struct",
    "Validation tests for struct types");

CTS_TEST(g, "no_direct_recursion")
    .desc("Test that direct recursion of structures is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "S"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nstruct S {\n  a : " + target + "\n}";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion")
    .desc("Test that indirect recursion of structures is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "S"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl =
            "\nstruct S {\n  a : T\n}\nstruct T {\n  a : " + target + "\n}";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_array_element")
    .desc("Test that indirect recursion of structures via array element types is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "S"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl = "\nstruct S {\n  a : array<" + target + ", 4>\n}\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_array_size")
    .desc("Test that indirect recursion of structures via array size expressions is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"S1", "S2"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl =
            "\nstruct S1 {\n  a : i32,\n}\nstruct S2 {\n  a : i32,\n  b : array<i32, " + target +
            "().a + 1>,\n}\n";
        t.expectCompileResult(target == "S1", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_struct_attribute")
    .desc("Test that indirect recursion of structures via struct members is rejected")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"S1", "S2"})
            .combine("attribute", {"align", "location", "size"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string attribute = t.param<std::string>("attribute");
        const std::string wgsl =
            "\nstruct S1 {\n  a : i32\n}\nstruct S2 {\n  @" + attribute + "(" + target +
            "(4).a) a : i32\n}\n";
        t.expectCompileResult(target == "S1", wgsl);
    });

CTS_TEST(g, "no_indirect_recursion_via_struct_member_nested_in_alias")
    .desc("Test that indirect recursion of structures via struct members is rejected when the "
          "member type is an alias that contains the structure")
    .params([](ParamsBuilder u) {
        return u.combine("target", {"i32", "A"});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string target = t.param<std::string>("target");
        const std::string wgsl =
            "\nalias A = array<S2, 4>;\nstruct S1 {\n  a : " + target +
            "\n}\nstruct S2 {\n  a : S1\n}\n";
        t.expectCompileResult(target == "i32", wgsl);
    });

// Mirrors upstream kStructureCases (object key order preserved).
struct StructureCase {
    const char* name;
    const char* code;
    bool valid;
    bool f16;
};

static const std::vector<StructureCase>& kStructureCases() {
    static const std::vector<StructureCase> cases = {
        {"bool", "struct S { x : bool }", true, false},
        {"u32", "struct S { x : u32 }", true, false},
        {"i32", "struct S { x : i32 }", true, false},
        {"f32", "struct S { x : f32 }", true, false},
        {"f16", "struct S { x : f16 }", true, true},

        {"vec2u", "struct S { x : vec2u }", true, false},
        {"vec3i", "struct S { x : vec3i }", true, false},
        {"vec4f", "struct S { x : vec4f }", true, false},
        {"vec4h", "struct S { x : vec4h }", true, true},

        {"mat2x2f", "struct S { x : mat2x2f }", true, false},
        {"mat3x4h", "struct S { x : mat3x4h }", true, true},

        {"atomic_u32", "struct S { x : atomic<u32> }", true, false},
        {"atomic_i32", "struct S { x : atomic<i32> }", true, false},

        {"array_u32_4", "struct S { x : array<u32, 4> }", true, false},
        {"array_u32", "struct S { x : array<u32> }", true, false},
        {"array_u32_not_last", "struct S { x : array<u32>, y : u32 }", false, false},
        {"array_u32_override", "override o : u32;\n    struct S { x : array<u32, o> }", false,
         false},

        {"structure", "struct S { x : u32 }\n    struct T { x : S }", true, false},
        {"structure_structure_rta", "struct S { x : array<u32> }\n    struct T { x : S }", false,
         false},

        {"pointer", "struct S { x : ptr<function, u32> }", false, false},

        {"texture", "struct S { x : texture_2d<f32> }", false, false},
        {"sampler", "struct S { x : sampler }", false, false},
        {"sampler_comparison", "struct S { x : sampler_comparison }", false, false},

        {"many_members",
         "struct S {\n      m1 : u32,\n      m2 : i32,\n      m3 : vec4f,\n      m4 : array<u32, "
         "8>,\n      m5 : array<f32>\n    }",
         true, false},

        {"trailing_comma", "struct S { x : u32, }", true, false},

        {"empty", "struct S { }", false, false},

        {"name_collision1", "struct S { x : u32 }\n    struct S { x : u32 }", false, false},
        {"name_collision2", "fn S() { }\n    struct S { x : u32 }", false, false},
        {"name_collision3", "struct S { x : u32 }\n    alias S = u32;", false, false},
        {"member_collision", "struct S { x : u32, x : u32 }", false, false},
        {"no_name", "struct { x : u32 }", false, false},
        {"missing_l_brace", "struct S x : u32 }", false, false},
        {"missing_r_brace", "struct S { x : u32", false, false},
        {"bad_name", "struct 123 { x : u32 }", false, false},
        {"bad_delimiter", "struct S { x : u32; y : u32 }", false, false},
        {"missing_delimiter", "struct S { x : u32 y : u32 }", false, false},
        {"bad_member_decl", "struct S { x u32 }", false, false},
    };
    return cases;
}

static std::vector<Value> structureCaseNames() {
    std::vector<Value> values;
    for (const StructureCase& c : kStructureCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const StructureCase& findStructureCase(const std::string& name) {
    for (const StructureCase& c : kStructureCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const StructureCase dummy{"", "", false, false};
    return dummy;
}

CTS_TEST(g, "structures")
    .desc("Validation tests for structures")
    .params([](ParamsBuilder u) {
        return u.combine("case", structureCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const StructureCase& c = findStructureCase(t.param<std::string>("case"));
        const std::string code =
            (c.f16 ? std::string("enable f16;") : std::string()) + "\n    " + c.code;
        t.expectCompileResult(c.valid, code);
    });

} // namespace
