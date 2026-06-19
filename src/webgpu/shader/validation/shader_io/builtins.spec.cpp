// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_io/builtins.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - generateShader() is ported verbatim from shader_io/util.ts (string-equivalent).
//   - isCompatibility is false in this harness (Dawn runs non-compat), so the
//     compatibility-mode skips for sample_index/sample_mask never fire; the
//     missing-@builtin(position) skip IS replicated.
//   - The kBuiltins `requires` field gates two stabilized WGSL language features
//     ('subgroup_id', 'linear_indexing') that the enabler's hasLanguageFeature does
//     NOT know (and which would fail()). Upstream gates the expected compile result
//     on actual feature support, so we probe support behaviorally with
//     compilesWithoutError() on a canonical valid shader per requires-builtin. When
//     unsupported, the matching entry drops out of the expectation predicate exactly
//     as upstream's `t.hasLanguageFeature(x.requires)` would.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::ShaderValidationTest;

namespace {

TestGroup<ShaderValidationTest> g = MakeTestGroup<ShaderValidationTest>(
    "shader,validation,shader_io,builtins",
    "Validation tests for entry point built-in variables");

// ---------------------------------------------------------------------------
// generateShader (ported from util.ts)
// ---------------------------------------------------------------------------
static std::string generateShader(const std::string& attribute,
                                  const std::string& type,
                                  const std::string& stage,
                                  const std::string& io,
                                  bool use_struct,
                                  const std::string& enableExt) {
    std::string code;

    if (!enableExt.empty()) {
        code += "enable " + enableExt + ";\n";
    }

    if (use_struct) {
        // Generate a struct that wraps the entry point IO variable.
        code += "struct S {\n";
        code += "  " + attribute + " value : " + type + ",\n";
        if (stage == "vertex" && io == "out" &&
            attribute.find("builtin(position)") == std::string::npos) {
            // Add position builtin for vertex outputs.
            code += "  @builtin(position) position : vec4<f32>,\n";
        }
        code += "};\n\n";
    }

    if (!stage.empty()) {
        // Generate the entry point attributes.
        code += "@" + stage;
        if (stage == "compute") {
            code += " @workgroup_size(1)";
        }
    }

    // Generate the entry point parameter and return type.
    std::string param;
    std::string retType;
    std::string retVal;
    if (io == "in") {
        if (use_struct) {
            param = "in : S";
        } else {
            param = attribute + " value : " + type;
        }
        // Vertex shaders must always return `@builtin(position)`.
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
// kBuiltins table (mirrors upstream)
// ---------------------------------------------------------------------------
struct Builtin {
    const char* name;
    const char* stage;  // vertex | fragment | compute
    const char* io;     // in | out
    const char* type;
    const char* enableExt;  // "" if none
    const char* requiresFeature;   // "" | "subgroup_id" | "linear_indexing"
};

static const std::vector<Builtin>& kBuiltins() {
    static const std::vector<Builtin> v = {
        {"vertex_index", "vertex", "in", "u32", "", ""},
        {"instance_index", "vertex", "in", "u32", "", ""},
        {"position", "vertex", "out", "vec4<f32>", "", ""},
        {"position", "fragment", "in", "vec4<f32>", "", ""},
        {"front_facing", "fragment", "in", "bool", "", ""},
        {"frag_depth", "fragment", "out", "f32", "", ""},
        {"local_invocation_id", "compute", "in", "vec3<u32>", "", ""},
        {"local_invocation_index", "compute", "in", "u32", "", ""},
        {"global_invocation_id", "compute", "in", "vec3<u32>", "", ""},
        {"workgroup_id", "compute", "in", "vec3<u32>", "", ""},
        {"num_workgroups", "compute", "in", "vec3<u32>", "", ""},
        {"sample_index", "fragment", "in", "u32", "", ""},
        {"sample_mask", "fragment", "in", "u32", "", ""},
        {"sample_mask", "fragment", "out", "u32", "", ""},
        {"subgroup_invocation_id", "compute", "in", "u32", "subgroups", ""},
        {"subgroup_size", "compute", "in", "u32", "subgroups", ""},
        {"subgroup_invocation_id", "fragment", "in", "u32", "subgroups", ""},
        {"subgroup_size", "fragment", "in", "u32", "subgroups", ""},
        {"clip_distances", "vertex", "out", "array<f32,1>", "clip_distances", ""},
        {"clip_distances", "vertex", "out", "array<f32,2>", "clip_distances", ""},
        {"clip_distances", "vertex", "out", "array<f32,3>", "clip_distances", ""},
        {"clip_distances", "vertex", "out", "array<f32,4>", "clip_distances", ""},
        {"clip_distances", "vertex", "out", "array<f32,5>", "clip_distances", ""},
        {"clip_distances", "vertex", "out", "array<f32,6>", "clip_distances", ""},
        {"clip_distances", "vertex", "out", "array<f32,7>", "clip_distances", ""},
        {"clip_distances", "vertex", "out", "array<f32,8>", "clip_distances", ""},
        {"primitive_id", "fragment", "in", "u32", "chromium_experimental_primitive_id", ""},
        {"subgroup_id", "compute", "in", "u32", "subgroups", "subgroup_id"},
        {"num_subgroups", "compute", "in", "u32", "subgroups", "subgroup_id"},
        {"workgroup_index", "compute", "in", "u32", "", "linear_indexing"},
        {"global_invocation_index", "compute", "in", "u32", "", "linear_indexing"},
    };
    return v;
}

// Mirrors upstream `combineWithParams(kBuiltins)`: each builtin entry becomes a
// case with its 6 fields as params.
static std::vector<ParamRecord> builtinParamRecords() {
    std::vector<ParamRecord> records;
    for (const Builtin& b : kBuiltins()) {
        ParamRecord r;
        r.emplace_back("name", Value(std::string(b.name)));
        r.emplace_back("stage", Value(std::string(b.stage)));
        r.emplace_back("io", Value(std::string(b.io)));
        r.emplace_back("type", Value(std::string(b.type)));
        // Omit `enable`/`requires` keys when absent, mirroring upstream's TS objects
        // (optional properties) so the generated query identity matches.
        if (b.enableExt[0] != '\0') {
            r.emplace_back("enable", Value(std::string(b.enableExt)));
        }
        if (b.requiresFeature[0] != '\0') {
            r.emplace_back("requires", Value(std::string(b.requiresFeature)));
        }
        records.push_back(r);
    }
    return records;
}

// Probe whether a `requires`-gated WGSL feature is supported by trial-compiling a
// canonical valid shader using the relevant builtin.
static bool requiresSupported(ShaderValidationTest& t, const std::string& requiresFeature) {
    if (requiresFeature.empty()) {
        return true;
    }
    if (requiresFeature == "subgroup_id") {
        return t.compilesWithoutError(
            "enable subgroups;\n@compute @workgroup_size(1) fn main("
            "@builtin(subgroup_id) sid : u32) { _ = sid; }");
    }
    if (requiresFeature == "linear_indexing") {
        return t.compilesWithoutError(
            "@compute @workgroup_size(1) fn main("
            "@builtin(workgroup_index) wi : u32) { _ = wi; }");
    }
    return false;
}

// List of types to test against (kTestTypes).
static const std::vector<std::string>& kTestTypes() {
    static const std::vector<std::string> v = {
        "bool", "u32", "i32", "f32",
        "vec2<bool>", "vec2<u32>", "vec2<i32>", "vec2<f32>",
        "vec3<bool>", "vec3<u32>", "vec3<i32>", "vec3<f32>",
        "vec4<bool>", "vec4<u32>", "vec4<i32>", "vec4<f32>",
        "mat2x2<f32>", "mat2x3<f32>", "mat2x4<f32>",
        "mat3x2<f32>", "mat3x3<f32>", "mat3x4<f32>",
        "mat4x2<f32>", "mat4x3<f32>", "mat4x4<f32>",
        "atomic<u32>", "atomic<i32>",
        "array<bool,4>", "array<u32,4>", "array<i32,4>",
        "array<f32,1>", "array<f32,2>", "array<f32,3>", "array<f32,4>",
        "array<f32,5>", "array<f32,6>", "array<f32,7>", "array<f32,8>",
        "array<f32,9>", "MyStruct",
    };
    return v;
}

static std::vector<Value> testTypeValues() {
    std::vector<Value> values;
    for (const std::string& s : kTestTypes()) {
        values.emplace_back(s);
    }
    return values;
}

static std::vector<Value> stageValues() {
    return {Value(std::string("")), Value(std::string("vertex")),
            Value(std::string("fragment")), Value(std::string("compute"))};
}

static std::vector<Value> ioValues() {
    return {Value(std::string("in")), Value(std::string("out"))};
}

static std::vector<Value> useStructValues() {
    return {Value(true), Value(false)};
}

// ---------------------------------------------------------------------------
// stage_inout
// ---------------------------------------------------------------------------
CTS_TEST(g, "stage_inout")
    .desc("Test that each @builtin attribute is validated against the required stage and in/out usage for that built-in variable.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(builtinParamRecords())
            .combine("use_struct", useStructValues())
            .combine("target_stage", stageValues())
            .combine("target_io", ioValues())
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("name");
        const std::string type = t.param<std::string>("type");
        const bool useStruct = t.param<bool>("use_struct");
        const std::string targetStage = t.param<std::string>("target_stage");
        const std::string targetIo = t.param<std::string>("target_io");
        const std::string enableExt =
            t.hasParam("enable") ? t.param<std::string>("enable") : std::string("");

        // beforeAllSubcases skips. isCompatibility == false here, so the
        // sample_index/sample_mask compat skip never fires.
        if (name != "position" && targetStage == "vertex" && targetIo == "out" && !useStruct) {
            t.skip("missing @builtin(position) in the vertex output when the vertex output is not a struct");
        }

        // The `chromium_experimental_primitive_id` WGSL extension token is gated by
        // the same device feature as the auto-skip, but the token itself may be
        // unrecognized by the backend's WGSL front-end (e.g. it was renamed to
        // `primitive_index`). Skip when the token does not parse, mirroring the
        // feature-absent skip at the WGSL-token level (not a pass).
        if (enableExt == "chromium_experimental_primitive_id" &&
            !t.wgslExtensionUsable("enable chromium_experimental_primitive_id;")) {
            t.skip("WGSL extension token `chromium_experimental_primitive_id` not usable on this backend");
        }

        const std::string code =
            generateShader("@builtin(" + name + ")", type, targetStage, targetIo, useStruct,
                           enableExt);

        // Expect to pass iff the built-in table contains a matching entry.
        bool expectation = false;
        for (const Builtin& x : kBuiltins()) {
            const bool stageMatch =
                (x.stage == targetStage) || (useStruct && targetStage == "");
            const bool ioMatch = (x.io == targetIo) || (targetStage == "");
            const bool requiresOk =
                (x.requiresFeature[0] == '\0') || requiresSupported(t, x.requiresFeature);
            if (x.name == name && stageMatch && ioMatch && x.type == type && requiresOk) {
                expectation = true;
                break;
            }
        }
        t.expectCompileResult(expectation, code);
    });

// ---------------------------------------------------------------------------
// type
// ---------------------------------------------------------------------------
CTS_TEST(g, "type")
    .desc("Test that each @builtin attribute is validated against the required type of that built-in variable.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(builtinParamRecords())
            .combine("use_struct", useStructValues())
            .beginSubcases()
            .combine("target_type", testTypeValues());
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("name");
        const std::string stage = t.param<std::string>("stage");
        const std::string io = t.param<std::string>("io");
        const std::string type = t.param<std::string>("type");
        const bool useStruct = t.param<bool>("use_struct");
        const std::string targetType = t.param<std::string>("target_type");
        const std::string enableExt =
            t.hasParam("enable") ? t.param<std::string>("enable") : std::string("");

        // beforeAllSubcases: isCompatibility == false; position skip replicated.
        if (name != "position" && stage == "vertex" && io == "out" && !useStruct) {
            t.skip("missing @builtin(position) in the vertex output");
        }

        // See stage_inout: skip when the chromium_experimental_primitive_id WGSL
        // extension token is unrecognized by the backend front-end.
        if (enableExt == "chromium_experimental_primitive_id" &&
            !t.wgslExtensionUsable("enable chromium_experimental_primitive_id;")) {
            t.skip("WGSL extension token `chromium_experimental_primitive_id` not usable on this backend");
        }

        std::string code;
        if (targetType == "MyStruct") {
            code += "struct MyStruct {\n";
            code += "  value : " + type + "\n";
            code += "};\n\n";
        }

        code += generateShader("@builtin(" + name + ")", targetType, stage, io, useStruct,
                               enableExt);

        bool expectation = false;
        for (const Builtin& x : kBuiltins()) {
            const bool requiresOk =
                (x.requiresFeature[0] == '\0') || requiresSupported(t, x.requiresFeature);
            if (x.name == name && x.stage == stage && x.io == io && x.type == targetType &&
                requiresOk) {
                expectation = true;
                break;
            }
        }
        t.expectCompileResult(expectation, code);
    });

// ---------------------------------------------------------------------------
// nesting
// ---------------------------------------------------------------------------
CTS_TEST(g, "nesting")
    .desc("Test validation of nested built-in variables")
    .params([](ParamsBuilder u) {
        return u.combine("target_stage",
                         {Value(std::string("fragment")), Value(std::string(""))})
            .combine("target_io", ioValues())
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string targetStage = t.param<std::string>("target_stage");
        const std::string targetIo = t.param<std::string>("target_io");
        std::string code =
            "\n    struct Inner {"
            "\n      @builtin(frag_depth) value : f32"
            "\n    };"
            "\n    struct Outer {"
            "\n      inner : Inner"
            "\n    };";
        code += generateShader("", "Outer", targetStage, targetIo, false, "");
        // Expect to pass only if the struct is not used for entry point IO.
        t.expectCompileResult(targetStage == "", code);
    });

// ---------------------------------------------------------------------------
// duplicates
// ---------------------------------------------------------------------------
CTS_TEST(g, "duplicates")
    .desc("Test that duplicated built-in variables are validated.")
    .params([](ParamsBuilder u) {
        return u.combine("first",
                         {Value(std::string("p1")), Value(std::string("s1a")),
                          Value(std::string("s2a")), Value(std::string("ra"))})
            .combine("second",
                     {Value(std::string("p2")), Value(std::string("s1b")),
                      Value(std::string("s2b")), Value(std::string("rb"))})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        // beforeAllSubcases: skipIf(isCompatibility) — false here, never fires.
        const std::string first = t.param<std::string>("first");
        const std::string second = t.param<std::string>("second");
        const std::string sm = "@builtin(sample_mask)";
        const std::string p1 = first == "p1" ? sm : "@location(1) @interpolate(flat, either)";
        const std::string p2 = second == "p2" ? sm : "@location(2) @interpolate(flat, either)";
        const std::string s1a = first == "s1a" ? sm : "@location(3) @interpolate(flat, either)";
        const std::string s1b = second == "s1b" ? sm : "@location(4) @interpolate(flat, either)";
        const std::string s2a = first == "s2a" ? sm : "@location(5) @interpolate(flat, either)";
        const std::string s2b = second == "s2b" ? sm : "@location(6) @interpolate(flat, either)";
        const std::string ra = first == "ra" ? sm : "@location(1) @interpolate(flat, either)";
        const std::string rb = second == "rb" ? sm : "@location(2) @interpolate(flat, either)";
        const std::string code =
            "\n    struct S1 {"
            "\n      " + s1a + " a : u32,"
            "\n      " + s1b + " b : u32,"
            "\n    };"
            "\n    struct S2 {"
            "\n      " + s2a + " a : u32,"
            "\n      " + s2b + " b : u32,"
            "\n    };"
            "\n    struct R {"
            "\n      " + ra + " a : u32,"
            "\n      " + rb + " b : u32,"
            "\n    };"
            "\n    @fragment"
            "\n    fn main(" + p1 + " p1 : u32,"
            "\n            " + p2 + " p2 : u32,"
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

// ---------------------------------------------------------------------------
// missing_vertex_position
// ---------------------------------------------------------------------------
CTS_TEST(g, "missing_vertex_position")
    .desc("Test that vertex shaders are required to output @builtin(position).")
    .params([](ParamsBuilder u) {
        return u.combine("use_struct", useStructValues())
            .combine("attribute",
                     {Value(std::string("@builtin(position)")), Value(std::string("@location(0)"))})
            .beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const bool useStruct = t.param<bool>("use_struct");
        const std::string attribute = t.param<std::string>("attribute");
        const std::string ret = useStruct ? std::string("S") : (attribute + " vec4<f32>");
        const std::string retVal = useStruct ? std::string("S") : std::string("vec4<f32>");
        const std::string code =
            "\n    struct S {"
            "\n      " + attribute + " value : vec4<f32>"
            "\n    };"
            "\n"
            "\n    @vertex"
            "\n    fn main() -> " + ret + " {"
            "\n      return " + retVal + "();"
            "\n    }"
            "\n    ";
        // Expect to pass only when using @builtin(position).
        t.expectCompileResult(attribute == "@builtin(position)", code);
    });

// ---------------------------------------------------------------------------
// reuse_builtin_name
// ---------------------------------------------------------------------------
CTS_TEST(g, "reuse_builtin_name")
    .desc("Test that a builtin name can be used in different contexts")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(builtinParamRecords())
            .combine("use",
                     {Value(std::string("alias")), Value(std::string("struct")),
                      Value(std::string("function")), Value(std::string("module-var")),
                      Value(std::string("function-var"))});
    })
    .fn([](ShaderValidationTest& t) {
        const std::string name = t.param<std::string>("name");
        const std::string use = t.param<std::string>("use");
        std::string code;
        if (use == "alias") {
            code += "alias " + name + " = i32;";
        } else if (use == "struct") {
            code += "struct " + name + " { i: f32, }";
        } else if (use == "function") {
            code += "fn " + name + "() {}";
        } else if (use == "module-var") {
            code += "const " + name + " = 1;";
        } else if (use == "function-var") {
            code += "fn test() { let " + name + " = 1; }";
        }
        t.expectCompileResult(true, code);
    });

// ---------------------------------------------------------------------------
// parse
// ---------------------------------------------------------------------------
struct ParseCase {
    const char* name;
    const char* src;
    bool pass;
};

static const std::vector<ParseCase>& kParseTests() {
    static const std::vector<ParseCase> v = {
        {"pos", "@builtin(position)", true},
        {"trailing_comma", "@builtin(position,)", true},
        {"newline_in_attr", "@ \n builtin(position)", true},
        {"whitespace_in_attr", "@/* comment */builtin/* comment */\n\n(\t/*comment*/position/*comment*/)", true},
        {"invalid_name", "@abuiltin(position)", false},
        {"no_params", "@builtin", false},
        {"missing_param", "@builtin()", false},
        {"missing_parens", "@builtin position", false},
        {"missing_lparen", "@builtin position)", false},
        {"missing_rparen", "@builtin(position", false},
        {"multiple_params", "@builtin(position, frag_depth)", false},
        {"ident_param", "@builtin(identifier)", false},
        {"number_param", "@builtin(2)", false},
        {"duplicate", "@builtin(position) @builtin(position)", false},
    };
    return v;
}

static std::vector<Value> parseCaseNames() {
    std::vector<Value> values;
    for (const ParseCase& c : kParseTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

static const ParseCase& findParseCase(const std::string& name) {
    for (const ParseCase& c : kParseTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ParseCase dummy{"", "", false};
    return dummy;
}

CTS_TEST(g, "parse")
    .desc("Test that @builtin is parsed correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("builtin", parseCaseNames());
    })
    .fn([](ShaderValidationTest& t) {
        const ParseCase& curr = findParseCase(t.param<std::string>("builtin"));
        const std::string code =
            "\n@vertex"
            "\nfn main() -> " + std::string(curr.src) + " vec4<f32> {"
            "\n  return vec4<f32>(.4, .2, .3, .1);"
            "\n}";
        t.expectCompileResult(curr.pass, code);
    });

// ---------------------------------------------------------------------------
// placement
// ---------------------------------------------------------------------------
struct PlacementScope {
    const char* name;
    bool allowed;
};

static const std::vector<PlacementScope>& kPlacementScopes() {
    static const std::vector<PlacementScope> v = {
        {"private-var", false},
        {"storage-var", false},
        {"struct-member", true},
        {"non-ep-param", false},
        {"non-ep-ret", false},
        {"fn-decl", false},
        {"fn-var", false},
        {"while-stmt", false},
        {"undefined", true},
    };
    return v;
}

static std::vector<Value> placementScopeNames() {
    std::vector<Value> values;
    for (const PlacementScope& s : kPlacementScopes()) {
        values.emplace_back(std::string(s.name));
    }
    return values;
}

static bool placementAllowed(const std::string& name) {
    for (const PlacementScope& s : kPlacementScopes()) {
        if (name == s.name) {
            return s.allowed;
        }
    }
    return false;
}

CTS_TEST(g, "placement")
    .desc("Tests the locations @builtin is allowed to appear")
    .params([](ParamsBuilder u) {
        return u.combine("scope", placementScopeNames()).beginSubcases();
    })
    .fn([](ShaderValidationTest& t) {
        const std::string scope = t.param<std::string>("scope");
        const std::string attr = "@builtin(vertex_index)";
        auto at = [&](const char* s) { return scope == s ? attr : std::string(""); };
        const std::string code =
            "\n      " + at("private-var") +
            "\n      var<private> priv_var : u32;"
            "\n"
            "\n      " + at("storage-var") +
            "\n      @group(0) @binding(0)"
            "\n      var<storage> stor_var : u32;"
            "\n"
            "\n      struct A {"
            "\n        " + at("struct-member") +
            "\n        a : u32,"
            "\n      }"
            "\n"
            "\n      fn v(" + at("non-ep-param") + " i : u32) ->"
            "\n            " + at("non-ep-ret") + " u32 { return 1; }"
            "\n"
            "\n      @vertex"
            "\n      " + at("fn-decl") +
            "\n      fn f("
            "\n        @location(0) b : u32,"
            "\n      ) -> @builtin(position) vec4f {"
            "\n        " + at("fn-var") +
            "\n        var<function> func_v : u32;"
            "\n"
            "\n        " + at("while-stmt") +
            "\n        while false {}"
            "\n"
            "\n        return vec4(1, 1, 1, 1);"
            "\n      }"
            "\n    ";
        // scope === undefined || attribute[scope]. Note the upstream `attribute` map
        // has no 'undefined' key (returns undefined -> falsy), but the leading
        // `scope === undefined` short-circuits to true for that case.
        t.expectCompileResult(scope == "undefined" || placementAllowed(scope), code);
    });

} // namespace
