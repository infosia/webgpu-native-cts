// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/extension/dual_source_blending.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - use_blend_src_requires_extension_enabled: like clip_distances, upstream gates
//     the device on requireExtension. On our all-features context the spec-faithful
//     success condition is (enableExtension && device-has-dual-source-blending);
//     requireExtension is preserved for query identity.
//   - The other four tests always `enable dual_source_blending;`; the auto-skip in
//     expectCompileResult skips the case when the device lacks dual-source-blending.
//   - blend_src_same_type additionally `enable f16;` for h-typed cases; that enable
//     triggers the f16 auto-skip when the device lacks shader-f16.

#include <set>
#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::UniqueFeaturesAndLimitsShaderValidationTest;

namespace {

TestGroup<UniqueFeaturesAndLimitsShaderValidationTest> g =
    MakeTestGroup<UniqueFeaturesAndLimitsShaderValidationTest>(
        "shader,validation,extension,dual_source_blending",
        "Validation tests for the dual_source_blending extension");

// ---------------------------------------------------------------------------
// use_blend_src_requires_extension_enabled
// ---------------------------------------------------------------------------
CTS_TEST(g, "use_blend_src_requires_extension_enabled")
    .desc(R"(Checks that the blend_src attribute is only allowed with the WGSL extension
     dual_source_blending enabled in shader and the WebGPU extension dual-source-blending supported
     on the device.)")
    .params([](ParamsBuilder u) {
        return u.combine("requireExtension", {Value(true), Value(false)})
                .combine("enableExtension", {Value(true), Value(false)});
    })
    .fn([](UniqueFeaturesAndLimitsShaderValidationTest& t) {
        const bool requireExtension = t.param<bool>("requireExtension");
        (void)requireExtension; // preserved for query identity; device is all-features
        const bool enableExtension = t.param<bool>("enableExtension");

        const bool deviceHas = t.deviceHasFeature(WGPUFeatureName_DualSourceBlending);
        const bool expectedSuccess = enableExtension && deviceHas;

        const std::string enableLine = enableExtension ? "enable dual_source_blending;" : "";
        const std::string code =
            "\n        " + enableLine +
            "\n        struct FragOut {"
            "\n          @location(0) @blend_src(0) color : vec4f,"
            "\n          @location(0) @blend_src(1) blend : vec4f,"
            "\n        }"
            "\n        @fragment fn main() -> FragOut {"
            "\n          var output : FragOut;"
            "\n          output.color = vec4f(1.0, 0.0, 0.0, 1.0);"
            "\n          output.blend = vec4f(0.0, 1.0, 0.0, 1.0);"
            "\n          return output;"
            "\n        }"
            "\n    ";
        t.expectCompileResult(expectedSuccess, code);
    });

// ---------------------------------------------------------------------------
// blend_src_syntax_validation
// ---------------------------------------------------------------------------
struct SyntaxCase {
    const char* name;
    const char* src;
    bool add_blend_src_0;
    bool add_blend_src_1;
    bool pass;
};

static const std::vector<SyntaxCase>& kSyntaxValidationTests() {
    static const std::vector<SyntaxCase> v = {
        {"zero", "@blend_src(0)", false, true, true},
        {"one", "@blend_src(1)", true, false, true},
        {"invalid", "@blend_src(2)", true, true, false},
        {"extra_comma", "@blend_src(1,)", true, false, true},
        {"i32", "@blend_src(1i)", true, false, true},
        {"u32", "@blend_src(1u)", true, false, true},
        {"hex", "@blend_src(0x1)", true, false, true},
        {"valid_const_expr", "@blend_src(a + b)", true, false, true},
        {"invalid_const_expr", "@blend_src(b + c)", true, true, false},
        {"max", "@blend_src(2147483647)", true, true, false},
        {"newline", "@\nblend_src(1)", true, false, true},
        {"comment", "@/* comment */blend_src(1)", true, false, true},
        {"misspelling", "@mblend_src(1)", true, true, false},
        {"no_parens", "@blend_src", true, true, false},
        {"no_parens_no_blend_src_0", "@blend_src", false, true, false},
        {"empty_params", "@blend_src()", true, true, false},
        {"empty_params_no_blend_src_0", "@blend_src()", false, true, false},
        {"missing_left_paren", "@blend_src 1)", true, false, false},
        {"missing_right_paren", "@blend_src(1", true, false, false},
        {"extra_params", "@blend_src(1, 2)", true, true, false},
        {"f32", "@blend_src(1f)", true, false, false},
        {"f32_literal", "@blend_src(1.0)", true, false, false},
        {"negative", "@blend_src(-1)", true, true, false},
        {"override_expr", "@blend_src(z + y)", true, false, false},
        {"vec", "@blend_src(vec2(1,1))", true, true, false},
        {"duplicate", "@blend_src(1) @blend_src(1)", true, false, false},
    };
    return v;
}

static const SyntaxCase& findSyntaxCase(const std::string& name) {
    for (const SyntaxCase& c : kSyntaxValidationTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const SyntaxCase dummy{"", "", false, false, false};
    return dummy;
}

static std::vector<Value> syntaxCaseNames() {
    std::vector<Value> values;
    for (const SyntaxCase& c : kSyntaxValidationTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "blend_src_syntax_validation")
    .desc("Syntax validation tests of blend_src.")
    .params([](ParamsBuilder u) {
        return u.combine("attr", syntaxCaseNames());
    })
    .fn([](UniqueFeaturesAndLimitsShaderValidationTest& t) {
        t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_DualSourceBlending, "dual-source-blending");
        const SyntaxCase& c = findSyntaxCase(t.param<std::string>("attr"));

        const std::string blendSrc0 =
            c.add_blend_src_0 ? "@location(0) @blend_src(0) color0 : vec4f," : "";
        const std::string blendSrc1 =
            c.add_blend_src_1 ? "@location(0) @blend_src(1) color1 : vec4f," : "";
        const std::string assign0 =
            c.add_blend_src_0 ? "output.color0 = output.blend;" : "";
        const std::string assign1 =
            c.add_blend_src_1 ? "output.color1 = output.blend;" : "";

        const std::string code =
            "\nenable dual_source_blending;\n"
            "\nconst a = 0;"
            "\nconst b = 1;"
            "\nconst c = 1;"
            "\noverride z = 0;"
            "\noverride y = 1;\n"
            "\nstruct FragOut {"
            "\n  @location(0) " + std::string(c.src) + " blend : vec4f,"
            "\n  " + blendSrc0 +
            "\n  " + blendSrc1 +
            "\n}\n"
            "\n@fragment fn main() -> FragOut {"
            "\n  var output : FragOut;"
            "\n  output.blend = vec4f(1.0, 0.0, 0.0, 1.0);"
            "\n  " + assign0 +
            "\n  " + assign1 +
            "\n  return output;"
            "\n}";
        t.expectCompileResult(c.pass, code);
    });

// ---------------------------------------------------------------------------
// blend_src_stage_input_output
// ---------------------------------------------------------------------------
struct StageIOCase {
    const char* name;
    const char* shader;
    bool pass;
};

static const std::vector<StageIOCase>& kStageIOValidationTests() {
    static const std::vector<StageIOCase> v = {
        {"vertex_input",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    @vertex fn main(vertexInput : BlendSrcStruct) -> @builtin(position) vec4f {"
         "\n      return vertexInput.color + vertexInput.blend;"
         "\n    }"
         "\n    ",
         false},
        {"vertex_output",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n      @builtin(position) myPosition: vec4f,"
         "\n    }"
         "\n    @vertex fn main() -> BlendSrcStruct {"
         "\n      var vertexOutput : BlendSrcStruct;"
         "\n      vertexOutput.myPosition = vec4f(0.0, 0.0, 0.0, 1.0);"
         "\n      return vertexOutput;"
         "\n    }"
         "\n    ",
         false},
        {"fragment_input",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    @fragment fn main(fragmentInput : BlendSrcStruct) -> @location(0) vec4f {"
         "\n      return fragmentInput.color + fragmentInput.blend;"
         "\n    }"
         "\n    ",
         false},
        {"fragment_output",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    @fragment fn main() -> BlendSrcStruct {"
         "\n      var fragmentOutput : BlendSrcStruct;"
         "\n      fragmentOutput.color = vec4f(0.0, 1.0, 0.0, 1.0);"
         "\n      fragmentOutput.blend = fragmentOutput.color;"
         "\n      return fragmentOutput;"
         "\n    }"
         "\n    ",
         true},
    };
    return v;
}

static const StageIOCase& findStageIOCase(const std::string& name) {
    for (const StageIOCase& c : kStageIOValidationTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const StageIOCase dummy{"", "", false};
    return dummy;
}

static std::vector<Value> stageIOCaseNames() {
    std::vector<Value> values;
    for (const StageIOCase& c : kStageIOValidationTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "blend_src_stage_input_output")
    .desc(R"(Test that the struct with blend_src cannot be used in the input of the fragment stage, the
  input of the vertex stage, or the output of the vertex stage. blend_src can be used as a part of
  the output of the fragment stage.)")
    .params([](ParamsBuilder u) {
        return u.combine("attr", stageIOCaseNames());
    })
    .fn([](UniqueFeaturesAndLimitsShaderValidationTest& t) {
        t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_DualSourceBlending, "dual-source-blending");
        const StageIOCase& c = findStageIOCase(t.param<std::string>("attr"));
        const std::string code =
            "\nenable dual_source_blending;\n\n" + std::string(c.shader) + "\n";
        t.expectCompileResult(c.pass, code);
    });

// ---------------------------------------------------------------------------
// blend_src_usage
// ---------------------------------------------------------------------------
struct UsageCase {
    const char* name;
    const char* code;
    bool pass;
    bool use_default_main_function;
};

static const std::vector<UsageCase>& kUsageValidationTests() {
    static const std::vector<UsageCase> v = {
        {"const", "@blend_src(0) const color = 1.2;", false, true},
        {"override", "@blend_src(0) @id(0) override color : f32;", false, true},
        {"let",
         "\n    @fragment fn main() -> vec4f {"
         "\n      let @blend_src(0) color = vec4f();"
         "\n      return color;"
         "\n    }"
         "\n    ",
         false, false},
        {"var_private", "@blend_src(0) var<private> color : vec4f;", false, true},
        {"var_function",
         "\n    @fragment fn main() -> vec4f {"
         "\n      var @blend_src(0) color : vec4f;"
         "\n      color = vec4f();"
         "\n      return color;"
         "\n    }"
         "\n    ",
         false, false},
        {"function_declaration",
         "\n    @blend_src(0) fn fun() {}"
         "\n    ",
         false, true},
        {"non_entrypoint_function_input_non_struct",
         "\n    fn fun(@blend_src(0) color : vec4f) -> vec4f {"
         "\n      return color;"
         "\n    }"
         "\n    ",
         false, true},
        {"non_entrypoint_function_output_non_struct",
         "\n    fn fun() -> @blend_src(0) vec4f {"
         "\n      return vec4f();"
         "\n    }"
         "\n    ",
         false, true},
        {"entrypoint_input_non_struct",
         "\n    @fragment fn main(@location(0) @blend_src(0) color : vec4f) -> @location(0) vec4f {"
         "\n      return color;"
         "\n    }"
         "\n    ",
         false, false},
        {"entrypoint_output_non_struct",
         "\n    @fragment fn main() -> @location(0) @blend_src(0) vec4f {"
         "\n      return vec4f();"
         "\n    }"
         "\n    ",
         false, false},
        {"struct_member_only_blend_src_0",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_only_blend_src_1",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_no_location_blend_src_0",
         "\n    struct BlendSrcStruct {"
         "\n      @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_no_location_blend_src_1",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_duplicate_blend_src_0",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(0) blend : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_duplicate_blend_src_1",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(1) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_has_non_zero_location_blend_src_0",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color1 : vec4f,"
         "\n      @location(1) @blend_src(0) color2 : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_has_non_zero_location_blend_src_1",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend1 : vec4f,"
         "\n      @location(1) @blend_src(1) blend2 : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_non_zero_location_blend_src_0_blend_src_1",
         "\n    struct BlendSrcStruct {"
         "\n      @location(1) @blend_src(0) color : vec4f,"
         "\n      @location(1) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_has_non_zero_location_no_blend_src",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n      @location(1) color2 : vec4f,"
         "\n    }"
         "\n    ",
         false, true},
        {"struct_member_no_location_no_blend_src",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n      depth : f32,"
         "\n    }"
         "\n    ",
         true, true},
        {"struct_member_blend_src_and_builtin",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n      @builtin(frag_depth) depth : f32,"
         "\n    }"
         "\n    ",
         true, true},
        {"struct_member_location_0_blend_src_0_blend_src_1",
         "\n    struct BlendSrcStruct {"
         "\n      @location(0) @blend_src(0) color : vec4f,"
         "\n      @location(0) @blend_src(1) blend : vec4f,"
         "\n    }"
         "\n    ",
         true, true},
    };
    return v;
}

static const UsageCase& findUsageCase(const std::string& name) {
    for (const UsageCase& c : kUsageValidationTests()) {
        if (name == c.name) {
            return c;
        }
    }
    static const UsageCase dummy{"", "", false, false};
    return dummy;
}

static std::vector<Value> usageCaseNames() {
    std::vector<Value> values;
    for (const UsageCase& c : kUsageValidationTests()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "blend_src_usage")
    .desc(R"(Test that blend_src can only be used on a member of a structure, and must be used together with
    the location attribute. In addition, if blend_src is used on a member of a structure, there must
    be exactly 2 members that have location attribute in the structure: one is @location(0)
    @blend_src(0) and another is @location(0) @blend_src(1).)")
    .params([](ParamsBuilder u) {
        return u.combine("attr", usageCaseNames());
    })
    .fn([](UniqueFeaturesAndLimitsShaderValidationTest& t) {
        t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_DualSourceBlending, "dual-source-blending");
        const UsageCase& c = findUsageCase(t.param<std::string>("attr"));
        const std::string defaultMain = c.use_default_main_function
            ? "@fragment fn main() -> @location(0) vec4f {\n  return vec4f();\n}"
            : "";
        const std::string code =
            "\nenable dual_source_blending;\n\n" + std::string(c.code) + "\n\n" + defaultMain + "\n";
        t.expectCompileResult(c.pass, code);
    });

// ---------------------------------------------------------------------------
// blend_src_same_type
// ---------------------------------------------------------------------------
static const std::vector<std::string>& kValidLocationTypes() {
    static const std::vector<std::string> v = {
        "f16", "f32", "i32", "u32",
        "vec2h", "vec2f", "vec2i", "vec2u",
        "vec3h", "vec3f", "vec3i", "vec3u",
        "vec4h", "vec4f", "vec4i", "vec4u",
    };
    return v;
}

static bool isF16Type(const std::string& ty) {
    static const std::set<std::string> f16{"f16", "vec2h", "vec3h", "vec4h"};
    return f16.count(ty) != 0;
}

static std::vector<Value> validLocationTypeValues() {
    std::vector<Value> values;
    for (const std::string& s : kValidLocationTypes()) {
        values.emplace_back(s);
    }
    return values;
}

CTS_TEST(g, "blend_src_same_type")
    .desc("Test that the struct member with @blend_src(0) and @blend_src(1) must have same type.")
    .params([](ParamsBuilder u) {
        return u.combine("blendSrc0Type", validLocationTypeValues())
                .combine("blendSrc1Type", validLocationTypeValues());
    })
    .fn([](UniqueFeaturesAndLimitsShaderValidationTest& t) {
        t.skipIfDeviceDoesNotHaveFeature(WGPUFeatureName_DualSourceBlending, "dual-source-blending");
        const std::string blendSrc0Type = t.param<std::string>("blendSrc0Type");
        const std::string blendSrc1Type = t.param<std::string>("blendSrc1Type");

        const bool needF16 = isF16Type(blendSrc0Type) || isF16Type(blendSrc1Type);
        // f16 enable below triggers the shader-f16 auto-skip in expectCompileResult
        // when the device lacks the feature (mirrors upstream's requiredFeatures push).
        const std::string f16Line = needF16 ? "enable f16;" : "";

        const std::string code =
            "\nenable dual_source_blending;\n\n" + f16Line + "\n\n"
            "struct BlendSrcOutput {"
            "\n  @location(0) @blend_src(0) color : " + blendSrc0Type + ","
            "\n  @location(0) @blend_src(1) blend : " + blendSrc1Type + ","
            "\n}\n\n"
            "@fragment fn main() -> BlendSrcOutput {"
            "\n  var output : BlendSrcOutput;"
            "\n  output.color = " + blendSrc0Type + "();"
            "\n  output.blend = " + blendSrc1Type + "();"
            "\n  return output;"
            "\n}\n";

        const bool success = blendSrc0Type == blendSrc1Type;
        t.expectCompileResult(success, code);
    });

} // namespace
