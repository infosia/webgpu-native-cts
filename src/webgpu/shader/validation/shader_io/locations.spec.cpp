// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/locations.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// All seven g.test use the compile-only path (t.expectCompileResult); none
// validate at pipeline creation. The upstream file does NOT parameterize over
// device limits (no maxInterStageShaderVariables/Components in params), so no
// build-time-limit-to-runtime-skip rewrite is needed.

#include <set>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,locations",
    "Validation tests for entry point user-defined IO");

// Mirrors upstream util.ts generateShader().
static std::string generateShader(const std::string& attribute,
                                  const std::string& type,
                                  const std::string& stage,
                                  const std::string& io,
                                  bool use_struct) {
    std::string code;

    if (use_struct) {
        code += "struct S {\n";
        code += "  " + attribute + " value : " + type + ",\n";
        if (stage == "vertex" && io == "out" &&
            attribute.find("builtin(position)") == std::string::npos) {
            code += "  @builtin(position) position : vec4<f32>,\n";
        }
        code += "};\n\n";
    }

    if (stage != "") {
        code += "@" + stage;
        if (stage == "compute") {
            code += " @workgroup_size(1)";
        }
    }

    std::string param;
    std::string retType;
    std::string retVal;
    if (io == "in") {
        if (use_struct) {
            param = "in : S";
        } else {
            param = attribute + " value : " + type;
        }
        if (stage == "vertex") {
            retType = "-> @builtin(position) vec4<f32>";
            retVal = "return vec4<f32>();";
        }
    } else if (io == "out") {
        if (use_struct) {
            retType = "-> S";
            retVal = "return S();";
        } else {
            retType = "-> " + attribute + " " + type;
            retVal = "return " + type + "();";
        }
    }

    code += "\n    fn main(" + param + ") " + retType + " {\n      " + retVal + "\n    }\n  ";
    return code;
}

static const std::vector<std::string>& kValidLocationTypes() {
    static const std::vector<std::string> v = {
        "f16",       "f32",       "i32",       "u32",
        "vec2<f32>", "vec2<i32>", "vec2<u32>", "vec3<f32>", "vec3<i32>", "vec3<u32>",
        "vec4<f32>", "vec4<i32>", "vec4<u32>",
        "vec2h",     "vec2f",     "vec2i",     "vec2u",
        "vec3h",     "vec3f",     "vec3i",     "vec3u",
        "vec4h",     "vec4f",     "vec4i",     "vec4u",
        "MyAlias",
    };
    return v;
}

static const std::vector<std::string>& kInvalidLocationTypes() {
    static const std::vector<std::string> v = {
        "bool",       "vec2<bool>", "vec3<bool>", "vec4<bool>",
        "mat2x2<f32>", "mat2x3<f32>", "mat2x4<f32>",
        "mat3x2<f32>", "mat3x3<f32>", "mat3x4<f32>",
        "mat4x2<f32>", "mat4x3<f32>", "mat4x4<f32>",
        "mat2x2f", "mat2x3f", "mat2x4f", "mat3x2f", "mat3x3f", "mat3x4f",
        "mat4x2f", "mat4x3f", "mat4x4f",
        "mat2x2h", "mat2x3h", "mat2x4h", "mat3x2h", "mat3x3h", "mat3x4h",
        "mat4x2h", "mat4x3h", "mat4x4h",
        "array<f32, 12>", "array<i32, 12>", "array<u32, 12>", "array<bool, 12>",
        "atomic<i32>", "atomic<u32>",
        "MyStruct",
        "texture_1d<i32>", "texture_2d<f32>", "texture_2d_array<i32>", "texture_3d<f32>",
        "texture_cube<u32>", "texture_cube_array<i32>", "texture_multisampled_2d<i32>",
        "texture_external",
        "texture_storage_1d<rgba8unorm, write>", "texture_storage_2d<rg32float, write>",
        "texture_storage_2d_array<r32float, write>", "texture_storage_3d<r32float, write>",
        "texture_depth_2d", "texture_depth_2d_array", "texture_depth_cube",
        "texture_depth_cube_array", "texture_depth_multisampled_2d",
        "sampler", "sampler_comparison",
    };
    return v;
}

static bool isValidLocationType(const std::string& type) {
    for (const std::string& v : kValidLocationTypes()) {
        if (v == type) {
            return true;
        }
    }
    return false;
}

CTS_TEST(g, "stage_inout")
    .desc("Test validation of user-defined IO stage and in/out usage")
    .params([](ParamsBuilder u) {
        return u.combine("use_struct", {Value(true), Value(false)})
                .combine("target_stage", {std::string("vertex"), std::string("fragment"),
                                          std::string("compute")})
                .combine("target_io", {std::string("in"), std::string("out")})
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const bool useStruct = t.param<bool>("use_struct");
        const std::string targetStage = t.param<std::string>("target_stage");
        const std::string targetIo = t.param<std::string>("target_io");

        const std::string code =
            generateShader("@location(0)", "f32", targetStage, targetIo, useStruct);

        const bool expectation =
            targetStage == "fragment" ||
            (targetStage == "vertex" && (targetIo == "in" || useStruct));
        t.expectCompileResult(expectation, code);
    });

CTS_TEST(g, "type")
    .desc("Test validation of user-defined IO types")
    .params([](ParamsBuilder u) {
        // new Set([...valid, ...invalid]) — union, insertion order valid-then-invalid,
        // duplicates collapsed (the two lists are disjoint here).
        std::vector<Value> types;
        for (const std::string& s : kValidLocationTypes()) {
            types.emplace_back(s);
        }
        for (const std::string& s : kInvalidLocationTypes()) {
            types.emplace_back(s);
        }
        return u.combine("use_struct", {Value(true), Value(false)})
                .combine("type", types)
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const bool useStruct = t.param<bool>("use_struct");
        const std::string type = t.param<std::string>("type");

        std::string code;

        const bool endsWithH = !type.empty() && type.back() == 'h';
        const bool isVecOrMat = type.rfind("mat", 0) == 0 || type.rfind("vec", 0) == 0;
        if (type == "f16" || (isVecOrMat && endsWithH)) {
            code += "enable f16;\n";
        }

        if (type == "MyStruct") {
            code +=
                "struct MyStruct {\n"
                "                value : f32,\n"
                "              }\n"
                "              ";
        }
        if (type == "MyAlias") {
            code += "alias MyAlias = i32;\n";
        }

        code += generateShader("@location(0) @interpolate(flat, either)", type, "fragment", "in",
                               useStruct);

        t.expectCompileResult(isValidLocationType(type), code);
    });

CTS_TEST(g, "nesting")
    .desc("Test validation of nested user-defined IO")
    .params([](ParamsBuilder u) {
        return u.combine("target_stage",
                         {std::string("vertex"), std::string("fragment"), std::string("")})
                .combine("target_io", {std::string("in"), std::string("out")})
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string targetStage = t.param<std::string>("target_stage");
        const std::string targetIo = t.param<std::string>("target_io");

        std::string code =
            "struct Inner {\n"
            "               @location(0) value : f32,\n"
            "             }\n"
            "             struct Outer {\n"
            "               inner : Inner,\n"
            "             }\n"
            "             ";

        code += generateShader("", "Outer", targetStage, targetIo, /*use_struct=*/false);

        t.expectCompileResult(targetStage == "", code);
    });

CTS_TEST(g, "duplicates")
    .desc("Test that duplicated user-defined IO attributes are validated.")
    .params([](ParamsBuilder u) {
        return u.combine("first", {std::string("p1"), std::string("s1a"), std::string("s2a"),
                                   std::string("ra")})
                .combine("second", {std::string("p2"), std::string("s1b"), std::string("s2b"),
                                    std::string("rb")})
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string first = t.param<std::string>("first");
        const std::string second = t.param<std::string>("second");

        const std::string p1 = first == "p1" ? "0" : "1";
        const std::string p2 = second == "p2" ? "0" : "2";
        const std::string s1a = first == "s1a" ? "0" : "3";
        const std::string s1b = second == "s1b" ? "0" : "4";
        const std::string s2a = first == "s2a" ? "0" : "5";
        const std::string s2b = second == "s2b" ? "0" : "6";
        const std::string ra = first == "ra" ? "0" : "1";
        const std::string rb = second == "rb" ? "0" : "2";
        const std::string code =
            "\n    struct S1 {"
            "\n      @location(" + s1a + ") a : f32,"
            "\n      @location(" + s1b + ") b : f32,"
            "\n    };"
            "\n    struct S2 {"
            "\n      @location(" + s2a + ") a : f32,"
            "\n      @location(" + s2b + ") b : f32,"
            "\n    };"
            "\n    struct R {"
            "\n      @location(" + ra + ") a : f32,"
            "\n      @location(" + rb + ") b : f32,"
            "\n    };"
            "\n    @fragment"
            "\n    fn main(@location(" + p1 + ") p1 : f32,"
            "\n            @location(" + p2 + ") p2 : f32,"
            "\n            s1 : S1,"
            "\n            s2 : S2,"
            "\n            ) -> R {"
            "\n      return R();"
            "\n    }"
            "\n    ";

        const bool firstIsRet = first == "ra";
        const bool secondIsRet = second == "rb";
        const bool expectation = firstIsRet != secondIsRet;
        t.expectCompileResult(expectation, code);
    });

// Mirrors upstream kValidationTests (object key order preserved).
struct ValidationCase {
    const char* name;
    const char* src;
    bool pass;
};

static const std::vector<ValidationCase>& kValidationTests() {
    static const std::vector<ValidationCase> v = {
        {"zero", "@location(0)", true},
        {"one", "@location(1)", true},
        {"extra_comma", "@location(1,)", true},
        {"i32", "@location(1i)", true},
        {"u32", "@location(1u)", true},
        {"hex", "@location(0x1)", true},
        {"const_expr", "@location(a + b)", true},
        {"max", "@location(2147483647)", true},
        {"newline", "@\nlocation(1)", true},
        {"comment", "@/* comment */location(1)", true},
        {"misspelling", "@mlocation(1)", false},
        {"no_parens", "@location", false},
        {"empty_params", "@location()", false},
        {"missing_left_paren", "@location 1)", false},
        {"missing_right_paren", "@location(1", false},
        {"extra_params", "@location(1, 2)", false},
        {"f32", "@location(1f)", false},
        {"f32_literal", "@location(1.0)", false},
        {"negative", "@location(-1)", false},
        {"override_expr", "@location(z + y)", false},
        {"vec", "@location(vec2(1,1))", false},
        {"duplicate", "@location(0) @location(0)", false},
    };
    return v;
}

CTS_TEST(g, "validation")
    .desc("Test validation of location")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const ValidationCase& c : kValidationTests()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("attr", names);
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("attr");
        const ValidationCase* found = nullptr;
        for (const ValidationCase& c : kValidationTests()) {
            if (name == c.name) {
                found = &c;
                break;
            }
        }
        const std::string src = found != nullptr ? found->src : "";
        const std::string code =
            "\nconst a = 5;"
            "\nconst b = 6;"
            "\noverride z = 7;"
            "\noverride y = 8;"
            "\n"
            "\n@vertex fn main("
            "\n  " + src + " res: f32"
            "\n) -> @builtin(position) vec4f {"
            "\n  return vec4f(0);"
            "\n}";
        t.expectCompileResult(found != nullptr && found->pass, code);
    });

CTS_TEST(g, "location_fp16")
    .desc("Test validation of location with fp16")
    .params([](ParamsBuilder u) {
        return u.combine("ext", {std::string(""), std::string("h")});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string ext = t.param<std::string>("ext");
        const std::string code =
            "\n"
            "\n@vertex fn main("
            "\n  @location(1" + ext + ") res: f32"
            "\n) -> @builtin(position) vec4f {"
            "\n  return vec4f();"
            "\n}";
        t.expectCompileResult(ext == "", code);
    });

// Mirrors upstream kOutOfOrderCases. Optional fields use empty-string + a
// "present" flag (params/returnType/decls/returnValue distinguish "" vs unset).
struct OutOfOrderCase {
    const char* name;
    const char* params;       // empty => not present
    bool hasParams;
    const char* returnType;   // empty => not present
    bool hasReturnType;
    const char* decls;        // empty => not present
    bool hasDecls;
    const char* returnValue;  // empty => not present
    bool hasReturnValue;
    bool valid;
};

static const std::vector<OutOfOrderCase>& kOutOfOrderCases() {
    static const std::vector<OutOfOrderCase> v = {
        {"reverse_params",
         "@location(2) p1 : f32, @location(1) p2 : f32, @location(0) p3 : f32", true,
         "", false, "", false, "", false, true},
        {"no_zero_params",
         "@location(2) p1 : f32, @location(1) p2 : f32", true,
         "", false, "", false, "", false, true},
        {"reverse_overlap",
         "@location(2) p1 : f32, @location(1) p2 : f32, @location(1) p3 : f32", true,
         "", false, "", false, "", false, false},
        {"struct",
         "p1 : S", true,
         "", false,
         "struct S {\n      @location(1) x : f32,\n      @location(0) y : f32,\n    }", true,
         "", false, true},
        {"struct_override",
         "@location(0) p1 : S", true,
         "", false,
         "struct S {\n      @location(1) x : f32,\n      @location(0) y : f32,\n    }", true,
         "", false, false},
        {"struct_random",
         "p1 : S, p2 : T", true,
         "", false,
         "struct S {\n      @location(16) x : f32,\n      @location(4) y : f32,\n    }\n    "
         "struct T {\n      @location(13) x : f32,\n      @location(7) y : f32,\n    }", true,
         "", false, true},
        {"struct_random_overlap",
         "p1 : S, p2 : T", true,
         "", false,
         "struct S {\n      @location(16) x : f32,\n      @location(4) y : f32,\n    }\n    "
         "struct T {\n      @location(13) x : f32,\n      @location(4) y : f32,\n    }", true,
         "", false, false},
        {"mixed_locations1",
         "@location(12) p1 : f32, p2 : S", true,
         "", false,
         "struct S {\n      @location(2) x : f32,\n    }", true,
         "", false, true},
        {"mixed_locations2",
         "p1 : S, @location(2) p2 : f32", true,
         "", false,
         "struct S {\n      @location(12) x : f32,\n    }", true,
         "", false, true},
        {"mixed_overlap",
         "p1 : S, @location(12) p2 : f32", true,
         "", false,
         "struct S {\n      @location(12) x : f32,\n    }", true,
         "", false, false},
        {"with_param_builtin",
         "p : S", true,
         "", false,
         "struct S {\n      @location(12) x : f32,\n      @builtin(position) pos : vec4f,\n"
         "      @location(0) y : f32,\n    }", true,
         "", false, true},
        {"non_zero_return",
         "", false,
         "@location(1) vec4f", true,
         "", false,
         "vec4f()", true, true},
        {"reverse_return",
         "", false,
         "S", true,
         "struct S {\n      @location(2) x : f32,\n      @location(1) y : f32,\n"
         "      @location(0) z : f32,\n    }", true,
         "S()", true, true},
        {"gap_return",
         "", false,
         "S", true,
         "struct S {\n      @location(13) x : f32,\n      @location(7) y : f32,\n"
         "      @location(2) z : f32,\n    }", true,
         "S()", true, true},
        {"with_return_builtin",
         "", false,
         "S", true,
         "struct S {\n      @location(11) x : f32,\n      @builtin(frag_depth) d : f32,\n"
         "      @location(10) y : f32,\n    }", true,
         "S()", true, true},
    };
    return v;
}

CTS_TEST(g, "out_of_order")
    .desc("Test validation of out of order locations")
    .params([](ParamsBuilder u) {
        std::vector<Value> names;
        for (const OutOfOrderCase& c : kOutOfOrderCases()) {
            names.emplace_back(std::string(c.name));
        }
        return u.combine("case", names);
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("case");
        const OutOfOrderCase* tc = nullptr;
        for (const OutOfOrderCase& c : kOutOfOrderCases()) {
            if (name == c.name) {
                tc = &c;
                break;
            }
        }
        const std::string decls = (tc != nullptr && tc->hasDecls) ? tc->decls : "";
        const std::string params = (tc != nullptr && tc->hasParams) ? tc->params : "";
        const std::string returnType =
            (tc != nullptr && tc->hasReturnType) ? std::string("-> ") + tc->returnType : "";
        const std::string returnValue =
            (tc != nullptr && tc->hasReturnValue) ? std::string("return ") + tc->returnValue + ";"
                                                  : "";
        const std::string code =
            "\n" + decls +
            "\n"
            "\n@fragment"
            "\nfn main(" + params + ") " + returnType + " {"
            "\n  " + returnValue +
            "\n}"
            "\n";
        t.expectCompileResult(tc != nullptr && tc->valid, code);
    });

} // namespace
