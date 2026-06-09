// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/inter_stage.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,inter_stage",
    "Interface matching between vertex and fragment shader validation for createRenderPipeline.");

// Returns a WGPUStringView from a null-terminated C string.
WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}


// Build the vertex shader WGSL for a given list of output declarations.
// Each element has "__" replaced with "v<i>" (matching JS getVarName(i)).
static std::string buildVertexShaderCode(const std::vector<std::string>& outputs) {
    std::string members;
    for (int i = 0; i < static_cast<int>(outputs.size()); ++i) {
        std::string decl = outputs[i];
        // Replace first occurrence of "__" with "v<i>"
        std::string varName = "v" + std::to_string(i);
        std::string::size_type pos = decl.find("__");
        if (pos != std::string::npos) {
            decl.replace(pos, 2, varName);
        }
        members += "            " + decl + ",\n";
    }
    return
        "        struct A {\n"
        + members +
        "            @builtin(position) pos: vec4<f32>,\n"
        "        }\n"
        "        @vertex fn main() -> A {\n"
        "            var vertexOut: A;\n"
        "            vertexOut.pos = vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
        "            return vertexOut;\n"
        "        }\n";
}

// Build the fragment shader WGSL for a given list of input declarations.
// Each element has "__" replaced with "v<i>" (matching JS getVarName(i)).
// If hasBuiltinPosition is true, add @builtin(position) pos: vec4<f32>.
// preamble is prepended before the struct (for `enable` directives, etc.).
static std::string buildFragmentShaderCode(
    const std::vector<std::string>& inputs,
    bool hasBuiltinPosition = false,
    const std::string& preamble = "")
{
    std::string members;
    for (int i = 0; i < static_cast<int>(inputs.size()); ++i) {
        std::string decl = inputs[i];
        // Replace first occurrence of "__" with "v<i>"
        std::string varName = "v" + std::to_string(i);
        std::string::size_type pos = decl.find("__");
        if (pos != std::string::npos) {
            decl.replace(pos, 2, varName);
        }
        members += "            " + decl + ",\n";
    }
    if (hasBuiltinPosition) {
        members += "            @builtin(position) pos: vec4<f32>\n";
    }
    return
        preamble
        + "        struct B {\n"
        + members +
        "        }\n"
        "        @fragment fn main(fragmentIn: B) -> @location(0) vec4<f32> {\n"
        "            return vec4<f32>(1.0, 1.0, 1.0, 1.0);\n"
        "        }\n";
}

// Helper: create a render pipeline using vertex and fragment WGSL, with optional topology.
static void doCreateRenderPipelineTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool success,
    const std::string& vertWGSL,
    const std::string& fragWGSL,
    WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_Undefined)
{
    WGPUShaderModule vertModule = t.createShaderModuleTracked(vertWGSL);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(fragWGSL);

    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 0;
    plDesc.bindGroupLayouts = nullptr;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(plDesc);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragModule;
    fragment.entryPoint = sv("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertModule;
    desc.vertex.entryPoint = sv("main");
    desc.fragment = &fragment;
    if (topology != WGPUPrimitiveTopology_Undefined) {
        desc.primitive.topology = topology;
    }

    t.expectValidationError([&] {
        t.createRenderPipelineTracked(desc);
    }, !success);
}

// ---------------------------------------------------------------------------
// Struct to hold location,mismatch test case data
// ---------------------------------------------------------------------------
struct LocationMismatchCase {
    std::vector<std::string> outputs;
    std::vector<std::string> inputs;
    bool success;
};

static const LocationMismatchCase kLocationMismatchCases[] = {
    { {"@location(0) __: f32"}, {"@location(0) __: f32"}, true },
    { {"@location(0) __: f32"}, {"@location(1) __: f32"}, false },
    { {"@location(1) __: f32"}, {"@location(0) __: f32"}, false },
    { {"@location(0) __: f32", "@location(1) __: f32"}, {"@location(1) __: f32", "@location(0) __: f32"}, true },
    { {"@location(1) __: f32", "@location(0) __: f32"}, {"@location(0) __: f32", "@location(1) __: f32"}, true },
};

// ---------------------------------------------------------------------------
// test: location,mismatch
// Tests that missing declaration at the same location should fail validation.
// ---------------------------------------------------------------------------
CTS_TEST(g, "location,mismatch")
    .desc("Tests that missing declaration at the same location should fail validation.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combine("caseIndex", {Value(0), Value(1), Value(2), Value(3), Value(4)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int caseIndex = t.param<int>("caseIndex");
        // isAsync is ignored — harness has no async pipeline-creation wrapper.

        const LocationMismatchCase& tc = kLocationMismatchCases[caseIndex];
        const std::string vertWGSL = buildVertexShaderCode(tc.outputs);
        const std::string fragWGSL = buildFragmentShaderCode(tc.inputs);
        doCreateRenderPipelineTest(t, tc.success, vertWGSL, fragWGSL);
    });

// ---------------------------------------------------------------------------
// test: location,superset
// Tests that validation should succeed when vertex output is superset of fragment input.
// ---------------------------------------------------------------------------
CTS_TEST(g, "location,superset")
    .desc("Tests that validation should succeed when vertex output is superset of fragment input.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // isAsync ignored — harness has no async pipeline-creation wrapper.
        const std::string vertWGSL = buildVertexShaderCode(
            {"@location(0) __: f32", "@location(1) __: f32"});
        const std::string fragWGSL = buildFragmentShaderCode(
            {"@location(1) __: f32"});
        doCreateRenderPipelineTest(t, true, vertWGSL, fragWGSL);
    });

// ---------------------------------------------------------------------------
// test: location,subset
// Tests that validation should fail when vertex output is a subset of fragment input.
// ---------------------------------------------------------------------------
CTS_TEST(g, "location,subset")
    .desc("Tests that validation should fail when vertex output is a subset of fragment input.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // isAsync ignored — harness has no async pipeline-creation wrapper.
        const std::string vertWGSL = buildVertexShaderCode(
            {"@location(0) __: f32"});
        const std::string fragWGSL = buildFragmentShaderCode(
            {"@location(0) __: f32", "@location(1) __: f32"});
        doCreateRenderPipelineTest(t, false, vertWGSL, fragWGSL);
    });

// ---------------------------------------------------------------------------
// Struct for type test cases
// ---------------------------------------------------------------------------
struct TypeCase {
    const char* output;
    const char* input;
};

static const TypeCase kTypeCases[] = {
    { "f32",        "f32"        },  // same -> success
    { "i32",        "f32"        },  // mismatch
    { "u32",        "f32"        },  // mismatch
    { "u32",        "i32"        },  // mismatch
    { "i32",        "u32"        },  // mismatch
    { "vec2<f32>",  "vec2<f32>"  },  // same -> success
    { "vec3<f32>",  "vec2<f32>"  },  // mismatch
    { "vec2<f32>",  "vec3<f32>"  },  // mismatch
    { "vec2<f32>",  "f32"        },  // mismatch
    { "f32",        "vec2<f32>"  },  // mismatch
};

// ---------------------------------------------------------------------------
// test: type
// Tests that validation should fail when type of vertex output and fragment
// input at the same location doesn't match.
// ---------------------------------------------------------------------------
CTS_TEST(g, "type")
    .desc(
        "Tests that validation should fail when type of vertex output and fragment input at the "
        "same location doesn't match.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combine("caseIndex", {Value(0), Value(1), Value(2), Value(3), Value(4),
                                   Value(5), Value(6), Value(7), Value(8), Value(9)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int caseIndex = t.param<int>("caseIndex");
        // isAsync ignored — harness has no async pipeline-creation wrapper.

        const TypeCase& tc = kTypeCases[caseIndex];
        const bool success = (std::string(tc.output) == std::string(tc.input));

        // Use @interpolate(flat, either) so integer types are valid inter-stage variables.
        const std::string outputDecl =
            std::string("@location(0) @interpolate(flat, either) __: ") + tc.output;
        const std::string inputDecl =
            std::string("@location(0) @interpolate(flat, either) __: ") + tc.input;

        const std::string vertWGSL = buildVertexShaderCode({outputDecl});
        const std::string fragWGSL = buildFragmentShaderCode({inputDecl});
        doCreateRenderPipelineTest(t, success, vertWGSL, fragWGSL);
    });

// ---------------------------------------------------------------------------
// Struct for interpolation_type test cases
// ---------------------------------------------------------------------------
struct InterpolationTypeCase {
    const char* output;      // attribute annotation on vertex output (may be "")
    const char* input;       // attribute annotation on fragment input (may be "")
    // _success: if set, overrides the default "output == input" rule
    // If not set (-1), use (output == input) as the success condition.
    int successOverride;     // -1 = use default, 0 = false, 1 = true
    // _compat_success: ignored since we never run in compatibility mode.
};

// Note: for interpolation tests, the WGSL variable type is f32.
// success = _success if present, else (output == input).
// _compat_success is irrelevant for the C++ harness.
static const InterpolationTypeCase kInterpolationTypeCases[] = {
    // { output, input, successOverride }
    { "",                          "",                           -1 }, // same -> success (both default)
    { "",                          "@interpolate(perspective)",  1  }, // _success: true
    { "",                          "@interpolate(perspective, center)", 1 }, // _success: true
    { "@interpolate(perspective)", "",                           1  }, // _success: true
    { "",                          "@interpolate(linear)",       -1 }, // no _success -> output==input? "" != "@interpolate(linear)" -> false
    { "@interpolate(perspective)", "@interpolate(perspective)",  -1 }, // same -> success
    { "@interpolate(linear)",      "@interpolate(perspective)",  -1 }, // different -> fail
    { "@interpolate(flat, either)","@interpolate(perspective)",  -1 }, // different -> fail
    { "@interpolate(linear)",      "@interpolate(flat, either)", -1 }, // different -> fail
    { "@interpolate(linear, center)", "@interpolate(linear, center)", -1 }, // same -> success (_compat_success=false but we ignore compat)
};

// ---------------------------------------------------------------------------
// test: interpolation_type
// Tests that validation should fail when interpolation type of vertex output
// and fragment input at the same location doesn't match.
// ---------------------------------------------------------------------------
CTS_TEST(g, "interpolation_type")
    .desc(
        "Tests that validation should fail when interpolation type of vertex output and fragment "
        "input at the same location doesn't match.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combine("caseIndex", {Value(0), Value(1), Value(2), Value(3), Value(4),
                                   Value(5), Value(6), Value(7), Value(8), Value(9)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int caseIndex = t.param<int>("caseIndex");
        // isAsync ignored — harness has no async pipeline-creation wrapper.

        const InterpolationTypeCase& tc = kInterpolationTypeCases[caseIndex];
        const std::string outputStr(tc.output);
        const std::string inputStr(tc.input);

        // Skip if the interpolation annotation involves 'linear' or 'sample'
        // (these are disallowed in compatibility mode). Since C++ harness is never
        // in compatibility mode, we do NOT skip — port them unconditionally.

        bool success;
        if (tc.successOverride == -1) {
            success = (outputStr == inputStr);
        } else {
            success = (tc.successOverride != 0);
        }

        // Build output/input declarations:
        // "@location(0) <output> __: f32"
        std::string outputDecl = "@location(0)";
        if (!outputStr.empty()) { outputDecl += " " + outputStr; }
        outputDecl += " __: f32";

        std::string inputDecl = "@location(0)";
        if (!inputStr.empty()) { inputDecl += " " + inputStr; }
        inputDecl += " __: f32";

        const std::string vertWGSL = buildVertexShaderCode({outputDecl});
        const std::string fragWGSL = buildFragmentShaderCode({inputDecl});
        doCreateRenderPipelineTest(t, success, vertWGSL, fragWGSL);
    });

// ---------------------------------------------------------------------------
// Struct for interpolation_sampling test cases
// ---------------------------------------------------------------------------
struct InterpolationSamplingCase {
    const char* output;
    const char* input;
    int successOverride;  // -1 = use default (output==input), 0 = false, 1 = true
    // _compat_success: ignored (C++ harness never runs in compatibility mode)
};

static const InterpolationSamplingCase kInterpolationSamplingCases[] = {
    { "@interpolate(perspective)",         "@interpolate(perspective)",         -1 }, // same -> success
    { "@interpolate(perspective)",         "@interpolate(perspective, center)",  1 }, // _success: true
    { "@interpolate(linear, center)",      "@interpolate(linear)",               1 }, // _success: true (_compat_success=false, ignored)
    { "@interpolate(flat, either)",        "@interpolate(flat, either)",        -1 }, // same -> success
    { "@interpolate(perspective)",         "@interpolate(perspective, sample)", -1 }, // different -> fail
    { "@interpolate(perspective, center)", "@interpolate(perspective, sample)", -1 }, // different -> fail
    { "@interpolate(perspective, center)", "@interpolate(perspective, centroid)",-1 }, // different -> fail
    { "@interpolate(perspective, centroid)","@interpolate(perspective)",        -1 }, // different -> fail
};

// ---------------------------------------------------------------------------
// test: interpolation_sampling
// Tests that validation should fail when interpolation sampling of vertex output
// and fragment input at the same location doesn't match.
// ---------------------------------------------------------------------------
CTS_TEST(g, "interpolation_sampling")
    .desc(
        "Tests that validation should fail when interpolation sampling of vertex output and "
        "fragment input at the same location doesn't match.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combine("caseIndex", {Value(0), Value(1), Value(2), Value(3),
                                   Value(4), Value(5), Value(6), Value(7)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int caseIndex = t.param<int>("caseIndex");
        // isAsync ignored — harness has no async pipeline-creation wrapper.

        const InterpolationSamplingCase& tc = kInterpolationSamplingCases[caseIndex];
        const std::string outputStr(tc.output);
        const std::string inputStr(tc.input);

        // Skip if 'linear' or 'sample' is present:
        // The upstream spec calls skipIfDisallowedInterpolationParameter only in compat mode.
        // C++ harness is never in compatibility mode, so no skip needed.

        bool success;
        if (tc.successOverride == -1) {
            success = (outputStr == inputStr);
        } else {
            success = (tc.successOverride != 0);
        }

        // Build declarations: "@location(0) <annot> __: f32"
        std::string outputDecl = "@location(0) " + outputStr + " __: f32";
        std::string inputDecl  = "@location(0) " + inputStr  + " __: f32";

        const std::string vertWGSL = buildVertexShaderCode({outputDecl});
        const std::string fragWGSL = buildFragmentShaderCode({inputDecl});
        doCreateRenderPipelineTest(t, success, vertWGSL, fragWGSL);
    });

// ---------------------------------------------------------------------------
// test: max_shader_variable_location
// Tests that validation should fail when there is a location of user-defined
// output/input variable >= device.limits.maxInterStageShaderVariables.
// ---------------------------------------------------------------------------
CTS_TEST(g, "max_shader_variable_location")
    .desc(
        "Tests that validation should fail when there is location of user-defined output/input "
        "variable >= device.limits.maxInterStageShaderVariables.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            // locationDelta: location = maxInterStageShaderVariables + locationDelta
            .combine("locationDelta", {Value(0), Value(-1), Value(-2)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int locationDelta = t.param<int>("locationDelta");
        // isAsync ignored — harness has no async pipeline-creation wrapper.

        const WGPULimits limits = t.getLimits();
        const uint32_t maxInterStageShaderVariables = limits.maxInterStageShaderVariables;
        // location = maxInterStageShaderVariables + locationDelta
        const int location = static_cast<int>(maxInterStageShaderVariables) + locationDelta;

        const std::string locStr = std::to_string(location);
        const std::string outputDecl = "@location(" + locStr + ") __: f32";
        const std::string inputDecl  = "@location(" + locStr + ") __: f32";

        const std::string vertWGSL = buildVertexShaderCode({outputDecl});
        const std::string fragWGSL = buildFragmentShaderCode({inputDecl});

        // success = location < maxInterStageShaderVariables (i.e. locationDelta < 0)
        const bool success = (location < static_cast<int>(maxInterStageShaderVariables));
        doCreateRenderPipelineTest(t, success, vertWGSL, fragWGSL);
    });

// ---------------------------------------------------------------------------
// Struct for max_variables_count,output test case data
// ---------------------------------------------------------------------------
struct MaxOutputCase {
    int numVariablesDelta;
    const char* topology; // "triangle-list" or "point-list"
    bool success;
};

static const MaxOutputCase kMaxOutputCases[] = {
    { 0,  "triangle-list", true  },
    { 1,  "triangle-list", false },
    { 0,  "point-list",    false },
    { -1, "point-list",    true  },
};

// ---------------------------------------------------------------------------
// test: max_variables_count,output
// Tests that validation should fail when all user-defined outputs >
// max vertex shader output variables.
// ---------------------------------------------------------------------------
CTS_TEST(g, "max_variables_count,output")
    .desc(
        "Tests that validation should fail when all user-defined outputs > max vertex shader "
        "output variables.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combine("caseIndex", {Value(0), Value(1), Value(2), Value(3)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int caseIndex = t.param<int>("caseIndex");
        // isAsync ignored — harness has no async pipeline-creation wrapper.

        const MaxOutputCase& tc = kMaxOutputCases[caseIndex];

        const WGPULimits limits = t.getLimits();
        const uint32_t maxInterStageShaderVariables = limits.maxInterStageShaderVariables;
        const int numVec4 = static_cast<int>(maxInterStageShaderVariables) + tc.numVariablesDelta;

        // Build outputs and inputs: range(numVec4, i => `@location(${i}) vout${i}: vec4<f32>`)
        std::vector<std::string> outputs;
        std::vector<std::string> inputs;
        for (int i = 0; i < numVec4; ++i) {
            // Note: these use explicit names (not "__"), so no substitution needed.
            outputs.push_back("@location(" + std::to_string(i) + ") vout" + std::to_string(i) + ": vec4<f32>");
            inputs.push_back("@location(" + std::to_string(i) + ") fin" + std::to_string(i) + ": vec4<f32>");
        }

        // Build WGSL — these already have explicit names, so buildVertexShaderCode/__
        // substitution is a no-op (no "__" in the strings). We can reuse the builders.
        const std::string vertWGSL = buildVertexShaderCode(outputs);
        const std::string fragWGSL = buildFragmentShaderCode(inputs);

        WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_Undefined;
        if (std::string(tc.topology) == "triangle-list") {
            topology = WGPUPrimitiveTopology_TriangleList;
        } else if (std::string(tc.topology) == "point-list") {
            topology = WGPUPrimitiveTopology_PointList;
        }

        doCreateRenderPipelineTest(t, tc.success, vertWGSL, fragWGSL, topology);
    });

// ---------------------------------------------------------------------------
// test: max_variables_count,input
// Tests that validation should fail when all user-defined inputs >
// max vertex shader output variables (with varying proportions of builtins and
// user-defined variables).
// ---------------------------------------------------------------------------
CTS_TEST(g, "max_variables_count,input")
    .desc(
        "Tests that validation should fail when all user-defined inputs > max vertex shader "
        "output variables (with varying proportions of builtins and user-defined variables).")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .combine("builtins", {Value(std::string("none")), Value(std::string("compat")), Value(std::string("all"))})
            .beginSubcases()
            .combine("overLimit", {Value(false), Value(true)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string builtins = t.param<std::string>("builtins");
        const bool overLimit = t.param<bool>("overLimit");
        // isAsync ignored — harness has no async pipeline-creation wrapper.

        // C++ harness is never in compatibility mode:
        // upstream: if (builtins === 'all') t.skipIf(t.isCompatibility) — omitted.

        // Build the list of builtin input declarations.
        // Note: these use explicit names, not "__", so buildFragmentShaderCode won't modify them.
        std::vector<std::string> inputs;

        // Preamble for the fragment shader: add `enable` directives for optional features
        // that contribute builtins, mirroring the JS's hasFeature guard in getFragmentStateWithInputs.
        std::string fragPreamble;
        const bool hasPrimitiveIndex = (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_PrimitiveIndex) != 0);
        const bool hasSubgroups = (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups) != 0);
        if (hasPrimitiveIndex) {
            fragPreamble += "enable primitive_index;\n";
        }
        if (hasSubgroups) {
            fragPreamble += "enable subgroups;\n";
        }

        if (builtins == "all") {
            // Gate: 'all' includes sample_mask and sample_index, which require
            // non-compatibility mode (already guaranteed). Also gate optional features.
            inputs.push_back("@builtin(sample_mask) sample_mask_in: u32");
            inputs.push_back("@builtin(sample_index) sample_index_in: u32");
            if (hasPrimitiveIndex) {
                inputs.push_back("@builtin(primitive_index) primitive_index_in: u32");
            }
            if (hasSubgroups) {
                inputs.push_back("@builtin(subgroup_invocation_id) subgroup_invocation_id_in: u32");
                inputs.push_back("@builtin(subgroup_size) subgroup_size_in: u32");
            }
            // fallthrough: also add 'compat' builtins
            inputs.push_back("@builtin(front_facing) front_facing_in: bool");
        } else if (builtins == "compat") {
            inputs.push_back("@builtin(front_facing) front_facing_in: bool");
        }
        // builtins == "none": no builtins added

        const WGPULimits limits = t.getLimits();
        const uint32_t maxInterStageShaderVariables = limits.maxInterStageShaderVariables;

        // numVec4 = maxInterStageShaderVariables - inputs.length + (overLimit ? 1 : 0)
        const int numVec4 =
            static_cast<int>(maxInterStageShaderVariables)
            - static_cast<int>(inputs.size())
            + (overLimit ? 1 : 0);

        // Vertex outputs: range(numVec4, i => `@location(${i}) vout${i}: vec4<f32>`)
        std::vector<std::string> outputs;
        for (int i = 0; i < numVec4; ++i) {
            outputs.push_back("@location(" + std::to_string(i) + ") vout" + std::to_string(i) + ": vec4<f32>");
        }

        // Append user-defined fragment inputs
        for (int i = 0; i < numVec4; ++i) {
            inputs.push_back("@location(" + std::to_string(i) + ") fin" + std::to_string(i) + ": vec4<f32>");
        }

        // Fragment shader uses hasBuiltinPosition=true (upstream always passes true for this test).
        // This adds `@builtin(position) pos: vec4<f32>` to struct B.
        const bool hasBuiltinPosition = true;

        const std::string vertWGSL = buildVertexShaderCode(outputs);
        const std::string fragWGSL = buildFragmentShaderCode(inputs, hasBuiltinPosition, fragPreamble);

        const bool success = !overLimit;
        doCreateRenderPipelineTest(t, success, vertWGSL, fragWGSL);
    });

} // namespace
