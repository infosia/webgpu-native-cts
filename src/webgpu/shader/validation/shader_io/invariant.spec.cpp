// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/invariant.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - valid_only_with_vertex_position_builtin combines over the kBuiltins table
//     (mirrored here from builtins.spec.ts). It feeds name/stage/io/type/enable to
//     generateShader (the `enable` directive drives the enabler's feature auto-skip
//     for f16/subgroups/clip_distances/chromium_experimental_primitive_id) and
//     expects success iff name === "position". The `requires` field of kBuiltins is
//     irrelevant to this test (only used by builtins.spec matching), so it is omitted.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,invariant",
    "Validation tests for the invariant attribute");

// ---------------------------------------------------------------------------
// generateShader (mirrors util.ts generateShader).
// ---------------------------------------------------------------------------
static bool contains(const std::string& s, const std::string& sub) {
    return s.find(sub) != std::string::npos;
}

static std::string generateShader(const std::string& attribute,
                                  const std::string& type,
                                  const std::string& stage,
                                  const std::string& io,
                                  bool use_struct,
                                  const std::string& enable) {
    std::string code;

    if (!enable.empty()) {
        code += "enable " + enable + ";\n";
    }

    if (use_struct) {
        code += "struct S {\n";
        code += "  " + attribute + " value : " + type + ",\n";
        if (stage == "vertex" && io == "out" && !contains(attribute, "builtin(position)")) {
            code += "  @builtin(position) position : vec4<f32>,\n";
        }
        code += "};\n\n";
    }

    if (!stage.empty()) {
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

// ---------------------------------------------------------------------------
// kBuiltins (mirrors builtins.spec.ts kBuiltins; only the fields used here).
// ---------------------------------------------------------------------------
struct Builtin {
    const char* name;
    const char* stage;
    const char* io;
    const char* type;
    const char* enable;  // "" if none
};

static const std::vector<Builtin>& kBuiltins() {
    static const std::vector<Builtin> builtins = {
        {"vertex_index", "vertex", "in", "u32", ""},
        {"instance_index", "vertex", "in", "u32", ""},
        {"position", "vertex", "out", "vec4<f32>", ""},
        {"position", "fragment", "in", "vec4<f32>", ""},
        {"front_facing", "fragment", "in", "bool", ""},
        {"frag_depth", "fragment", "out", "f32", ""},
        {"local_invocation_id", "compute", "in", "vec3<u32>", ""},
        {"local_invocation_index", "compute", "in", "u32", ""},
        {"global_invocation_id", "compute", "in", "vec3<u32>", ""},
        {"workgroup_id", "compute", "in", "vec3<u32>", ""},
        {"num_workgroups", "compute", "in", "vec3<u32>", ""},
        {"sample_index", "fragment", "in", "u32", ""},
        {"sample_mask", "fragment", "in", "u32", ""},
        {"sample_mask", "fragment", "out", "u32", ""},
        {"subgroup_invocation_id", "compute", "in", "u32", "subgroups"},
        {"subgroup_size", "compute", "in", "u32", "subgroups"},
        {"subgroup_invocation_id", "fragment", "in", "u32", "subgroups"},
        {"subgroup_size", "fragment", "in", "u32", "subgroups"},
        {"clip_distances", "vertex", "out", "array<f32,1>", "clip_distances"},
        {"clip_distances", "vertex", "out", "array<f32,2>", "clip_distances"},
        {"clip_distances", "vertex", "out", "array<f32,3>", "clip_distances"},
        {"clip_distances", "vertex", "out", "array<f32,4>", "clip_distances"},
        {"clip_distances", "vertex", "out", "array<f32,5>", "clip_distances"},
        {"clip_distances", "vertex", "out", "array<f32,6>", "clip_distances"},
        {"clip_distances", "vertex", "out", "array<f32,7>", "clip_distances"},
        {"clip_distances", "vertex", "out", "array<f32,8>", "clip_distances"},
        {"primitive_id", "fragment", "in", "u32", "chromium_experimental_primitive_id"},
        {"subgroup_id", "compute", "in", "u32", "subgroups"},
        {"num_subgroups", "compute", "in", "u32", "subgroups"},
        {"workgroup_index", "compute", "in", "u32", ""},
        {"global_invocation_index", "compute", "in", "u32", ""},
    };
    return builtins;
}

static std::vector<ParamRecord> kBuiltinsParams() {
    std::vector<ParamRecord> params;
    for (const Builtin& b : kBuiltins()) {
        params.push_back(ParamRecord{
            {"name", std::string(b.name)},
            {"stage", std::string(b.stage)},
            {"io", std::string(b.io)},
            {"type", std::string(b.type)},
            {"enable", std::string(b.enable)},
        });
    }
    return params;
}

// ---------------------------------------------------------------------------
// kTests for the parsing test.
// ---------------------------------------------------------------------------
struct AttrCase {
    const char* name;
    const char* src;
    bool pass;
};

static const std::vector<AttrCase>& kTests() {
    static const std::vector<AttrCase> cases = {
        {"invariant", "@invariant", true},
        {"comment", "@/* comment */invariant", true},
        {"split_line", "@\ninvariant", true},
        {"empty_parens", "@invariant()", false},
        {"value", "@invariant(0)", false},
        {"missing_right_paren", "@invariant(", false},
        {"missing_left_paren", "@invariant)", false},
        {"duplicate", "@invariant @invariant", false},
    };
    return cases;
}

static std::vector<Value> attrNames() {
    std::vector<Value> values;
    for (const AttrCase& c : kTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const AttrCase& findCase(const std::string& name) {
    for (const AttrCase& c : kTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const AttrCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "parsing")
    .desc("Test parsing of the invariant attribute")
    .params([](ParamsBuilder u) {
        return u.combine("attr", attrNames());
    })
    .fn([](ShaderValidationTest& t) {
        const AttrCase& curr = findCase(t.param<std::string>("attr"));
        const std::string code =
            "\n    struct VertexOut {"
            "\n      @builtin(position) " + std::string(curr.src) + " position : vec4<f32>"
            "\n    };"
            "\n    @vertex"
            "\n    fn main() -> VertexOut {"
            "\n      return VertexOut();"
            "\n    }"
            "\n    ";
        t.expectCompileResult(curr.pass, code);
    });

CTS_TEST(g, "valid_only_with_vertex_position_builtin")
    .desc("Test that the invariant attribute is only accepted with the vertex position builtin")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(kBuiltinsParams())
                .combine("use_struct", {Value(true), Value(false)})
                .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("name");
        const std::string type = t.param<std::string>("type");
        const std::string stage = t.param<std::string>("stage");
        const std::string io = t.param<std::string>("io");
        const bool use_struct = t.param<bool>("use_struct");
        const std::string enable = t.param<std::string>("enable");

        const std::string code = generateShader(
            "@builtin(" + name + ") @invariant", type, stage, io, use_struct, enable);

        t.expectCompileResult(name == "position", code);
    });

CTS_TEST(g, "valid_only_with_position_in_non_entry_point_struct")
    .desc("Test that the invariant attribute requires position, regardless if the struct is used in an entry point")
    .params([](ParamsBuilder u) {
        return u.combine("has_position", {Value(true), Value(false)});
    })
    .fn([](ShaderValidationTest& t) {
        const bool has_position = t.param<bool>("has_position");
        std::string code = "struct A {\n";
        code += "  @invariant";
        if (has_position) {
            code += " @builtin(position)";
        }
        code += "  a : vec4f,\n";
        code += "}\n";
        code += "@group(0) @binding(0) var<storage> a: A;\n";
        code += "@compute @workgroup_size(1) fn main() { let b = a; }";

        t.expectCompileResult(has_position, code);
    });

CTS_TEST(g, "not_valid_on_user_defined_io")
    .desc("Test that the invariant attribute is not accepted on user-defined IO attributes.")
    .params([](ParamsBuilder u) {
        return u.combine("use_invariant", {Value(true), Value(false)}).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const bool use_invariant = t.param<bool>("use_invariant");
        const std::string invariant = use_invariant ? "@invariant" : "";
        const std::string code =
            "\n    struct VertexOut {"
            "\n      @location(0) " + invariant + " loc0 : vec4<f32>,"
            "\n      @builtin(position) position : vec4<f32>,"
            "\n    };"
            "\n    @vertex"
            "\n    fn main() -> VertexOut {"
            "\n      return VertexOut();"
            "\n    }"
            "\n    ";
        t.expectCompileResult(!use_invariant, code);
    });

} // namespace
