// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/extension/subgroup_size_control.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - Upstream beforeAllSubcases selects a device with 'subgroup-size-control'. There
//     is no portable WGPUFeatureName for subgroup-size-control in the standard
//     webgpu-headers enum (only WGPUFeatureName_Subgroups), so the gate is enforced
//     via the `enable subgroups;` auto-skip in the helpers: every shader here enables
//     subgroups, and on a device without subgroups the case is skipped. This keeps the
//     port portable across all three backends (-Werror clean) while preserving the
//     feature-gated skip.
//   - subgroup_size_override_* and workgroup_size_x_* use the pipeline path
//     (expectPipelineResult) with override constants, and loop over the adapter's
//     valid subgroup sizes obtained from WGPUAdapterInfo.subgroupMin/MaxSize.

#include <string>
#include <vector>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::UniqueFeaturesAndLimitsShaderValidationTest;
using SVT = UniqueFeaturesAndLimitsShaderValidationTest;

namespace {

TestGroup<SVT> g = MakeTestGroup<SVT>(
    "shader,validation,extension,subgroup_size_control",
    "Validation tests for the subgroup_size_control extension");

static bool isPowerOfTwo(int64_t n) {
    return n > 0 && (n & (n - 1)) == 0;
}

// Mirrors upstream selectDeviceOrSkipTestCase({requiredFeatures:['subgroup-size-control']}).
// There is no stable WGPUFeatureName for subgroup-size-control in the standard
// webgpu-headers enum, so probe behaviorally: if `enable subgroups; enable
// subgroup_size_control;` does not compile, the device lacks the feature -> skip.
static void skipIfSubgroupSizeControlUnsupported(SVT& t) {
    if (!t.wgslExtensionUsable("enable subgroups;\nenable subgroup_size_control;")) {
        t.skip("device does not support the subgroup-size-control feature");
    }
}

// Mirrors upstream getValidSubgroupSizes(device): all power-of-two sizes in
// [subgroupMinSize, subgroupMaxSize] for which a @subgroup_size pipeline builds.
static std::vector<uint32_t> getValidSubgroupSizes(SVT& t) {
    const SVT::SubgroupSizeRange range = t.getSubgroupSizeRange();
    std::vector<uint32_t> sizes;
    if (range.minSize == 0 || range.maxSize == 0) {
        return sizes;
    }
    for (uint32_t s = range.minSize; s <= range.maxSize; s *= 2) {
        const std::string wgsl =
            "\nenable subgroups;"
            "\nenable subgroup_size_control;\n"
            "\n@compute @workgroup_size(" + std::to_string(s) + ", 1, 1) @subgroup_size(" +
            std::to_string(s) + ")"
            "\nfn main(@builtin(local_invocation_index) lid : u32) {"
            "\n}";
        if (!t.computePipelineCausesError(wgsl)) {
            sizes.push_back(s);
        }
    }
    return sizes;
}

// ---------------------------------------------------------------------------
// enable_subgroup_size_control_requires_subgroups
// ---------------------------------------------------------------------------
CTS_TEST(g, "enable_subgroup_size_control_requires_subgroups")
    .desc(R"(Checks that enabling the WGSL extension subgroup_size_control without also enabling the
     subgroups extension is a compilation error.)")
    .params([](ParamsBuilder u) {
        return u.combine("enableSubgroups", {Value(false), Value(true)});
    })
    .fn([](SVT& t) {
        skipIfSubgroupSizeControlUnsupported(t);
        const bool enableSubgroups = t.param<bool>("enableSubgroups");
        const std::string subgroupsLine = enableSubgroups ? "enable subgroups;" : "";
        const std::string code =
            "\n        " + subgroupsLine +
            "\n        enable subgroup_size_control;"
            "\n        @compute @workgroup_size(1)"
            "\n        fn main() {}"
            "\n      ";
        t.expectCompileResult(enableSubgroups, code);
    });

// ---------------------------------------------------------------------------
// use_subgroup_size_attribute_requires_subgroup_size_control_extension_enabled
// ---------------------------------------------------------------------------
CTS_TEST(g, "use_subgroup_size_attribute_requires_subgroup_size_control_extension_enabled")
    .desc(R"(Checks that the @subgroup_size attribute is only allowed with the WGSL extension
     subgroup_size_control enabled in the shader and the WebGPU extension subgroup-size-control
     supported on the device.)")
    .params([](ParamsBuilder u) {
        return u.combine("enableExtension", {Value(false), Value(true)});
    })
    .fn([](SVT& t) {
        skipIfSubgroupSizeControlUnsupported(t);
        const bool enableExtension = t.param<bool>("enableExtension");
        const int kSubgroupSize = 4;
        const std::string extLine = enableExtension ? "enable subgroup_size_control;" : "";
        const std::string code =
            "\n        enable subgroups;"
            "\n        " + extLine +
            "\n        @compute @workgroup_size(" + std::to_string(kSubgroupSize) +
            ") @subgroup_size(" + std::to_string(kSubgroupSize) + ")"
            "\n        fn main() {}"
            "\n      ";
        t.expectCompileResult(enableExtension, code);
    });

// ---------------------------------------------------------------------------
// subgroup_size_attribute_only_valid_in_compute_stage
// ---------------------------------------------------------------------------
CTS_TEST(g, "subgroup_size_attribute_only_valid_in_compute_stage")
    .desc(R"(Checks that the @subgroup_size attribute is only valid on a compute shader entry point.
     Applying it to a vertex or fragment entry point must be a compilation error.)")
    .params([](ParamsBuilder u) {
        return u.combine("stage", {Value(std::string("compute")),
                                   Value(std::string("vertex")),
                                   Value(std::string("fragment"))});
    })
    .fn([](SVT& t) {
        skipIfSubgroupSizeControlUnsupported(t);
        const std::string stage = t.param<std::string>("stage");
        const int kSubgroupSize = 4;
        const std::string sz = std::to_string(kSubgroupSize);

        std::string code;
        if (stage == "compute") {
            code =
                "\n    enable subgroups;"
                "\n    enable subgroup_size_control;"
                "\n    @compute @workgroup_size(" + sz + ") @subgroup_size(" + sz + ")"
                "\n    fn main() {}"
                "\n  ";
        } else if (stage == "vertex") {
            code =
                "\n    enable subgroups;"
                "\n    enable subgroup_size_control;"
                "\n    @vertex @subgroup_size(" + sz + ")"
                "\n    fn main() -> @builtin(position) vec4f {"
                "\n      return vec4f(0);"
                "\n    }"
                "\n  ";
        } else {
            code =
                "\n    enable subgroups;"
                "\n    enable subgroup_size_control;"
                "\n    @fragment @subgroup_size(" + sz + ")"
                "\n    fn main() -> @location(0) vec4f {"
                "\n      return vec4f(0);"
                "\n    }"
                "\n  ";
        }
        t.expectCompileResult(stage == "compute", code);
    });

// ---------------------------------------------------------------------------
// subgroup_size_value_must_be_const_or_override_i32_u32
// ---------------------------------------------------------------------------
struct ValueCase {
    const char* name;
    const char* expr;
    const char* decl;
    bool pass;
};

static const std::vector<ValueCase>& kSubgroupSizeValueCases() {
    static const std::vector<ValueCase> v = {
        {"literal_abstract_int", "4", "", true},
        {"literal_u32", "4u", "", true},
        {"literal_i32", "4i", "", true},
        {"const_i32", "k_i32", "const k_i32: i32 = 4;", true},
        {"const_u32", "k_u32", "const k_u32: u32 = 4;", true},
        {"const_expr_abstract", "2 + 2", "", true},
        {"const_expr_named", "k + 1", "const k = 3;", true},
        {"override_i32", "o_i32", "override o_i32: i32 = 4;", true},
        {"override_u32", "o_u32", "override o_u32: u32 = 4;", true},
        {"override_expr", "o + 1", "override o: u32 = 3;", true},
        {"literal_f32", "4.0f", "", false},
        {"literal_abstract_float", "4.0", "", false},
        {"literal_bool", "true", "", false},
        {"const_f32", "k_f32", "const k_f32: f32 = 4.0;", false},
        {"const_bool", "k_bool", "const k_bool: bool = true;", false},
        {"override_f32", "o_f32", "override o_f32: f32 = 4.0;", false},
        {"let_u32", "r", "fn dummy() -> u32 { let r: u32 = 4; return r; }", false},
        {"var_u32", "v", "var<private> v: u32 = 4;", false},
    };
    return v;
}

static const ValueCase& findValueCase(const std::string& name) {
    for (const ValueCase& c : kSubgroupSizeValueCases()) {
        if (name == c.name) {
            return c;
        }
    }
    static const ValueCase dummy{"", "", "", false};
    return dummy;
}

static std::vector<Value> valueCaseNames() {
    std::vector<Value> values;
    for (const ValueCase& c : kSubgroupSizeValueCases()) {
        values.emplace_back(std::string(c.name));
    }
    return values;
}

CTS_TEST(g, "subgroup_size_value_must_be_const_or_override_i32_u32")
    .desc(R"(Checks that the value of @subgroup_size must be a constant expression or an override
     expression that resolves to an i32 or a u32.)")
    .params([](ParamsBuilder u) {
        return u.combine("case", valueCaseNames());
    })
    .fn([](SVT& t) {
        skipIfSubgroupSizeControlUnsupported(t);
        const ValueCase& c = findValueCase(t.param<std::string>("case"));
        const std::string code =
            "\n        enable subgroups;"
            "\n        enable subgroup_size_control;"
            "\n        " + std::string(c.decl) +
            "\n        @compute @workgroup_size(4) @subgroup_size(" + std::string(c.expr) + ")"
            "\n        fn main() {}"
            "\n      ";
        t.expectCompileResult(c.pass, code);
    });

// ---------------------------------------------------------------------------
// subgroup_size_constant_value_must_be_power_of_2
// ---------------------------------------------------------------------------
CTS_TEST(g, "subgroup_size_constant_value_must_be_power_of_2")
    .desc(R"(Checks that when @subgroup_size is a constant expression, it is a shader creation error if
    the value is not a power of 2.)")
    .params([](ParamsBuilder u) {
        return u.combine("size", {Value(0), Value(1), Value(2), Value(3), Value(4), Value(5),
                                  Value(8), Value(9), Value(15), Value(16), Value(32), Value(33),
                                  Value(48), Value(64), Value(65), Value(100), Value(128), Value(256)});
    })
    .fn([](SVT& t) {
        skipIfSubgroupSizeControlUnsupported(t);
        const int64_t size = t.param<int64_t>("size");
        const std::string sz = std::to_string(size);
        const std::string code =
            "\n        enable subgroups;"
            "\n        enable subgroup_size_control;"
            "\n        @compute @workgroup_size(" + sz + ") @subgroup_size(" + sz + ")"
            "\n        fn main() {}"
            "\n      ";
        t.expectCompileResult(isPowerOfTwo(size), code);
    });

// ---------------------------------------------------------------------------
// subgroup_size_override_must_be_power_of_2_at_pipeline_creation
// ---------------------------------------------------------------------------
CTS_TEST(g, "subgroup_size_override_must_be_power_of_2_at_pipeline_creation")
    .desc(R"(Checks that when @subgroup_size is an override expression, it is a pipeline creation error
     if the override value resolves to a value that is not a power of 2.)")
    .params([](ParamsBuilder u) {
        return u.combine("size", {Value(3), Value(5), Value(7), Value(15), Value(31),
                                  Value(63), Value(127)});
    })
    .fn([](SVT& t) {
        skipIfSubgroupSizeControlUnsupported(t);
        const int64_t size = t.param<int64_t>("size");
        SVT::PipelineArgs args;
        args.expectedResult = false;
        args.code =
            "\n        enable subgroups;"
            "\n        enable subgroup_size_control;"
            "\n        override S: u32;"
            "\n        @workgroup_size(S) @subgroup_size(S)";
        args.constants["S"] = static_cast<double>(size);
        args.addWorkgroupSize = false;
        t.expectPipelineResult(args);
    });

// ---------------------------------------------------------------------------
// subgroup_size_override_valid_values_no_error
// ---------------------------------------------------------------------------
CTS_TEST(g, "subgroup_size_override_valid_values_no_error")
    .desc(R"(Checks that when @subgroup_size is an override expression and the override value resolves
     to a valid subgroup size (a power of 2 between subgroupMinSize and subgroupMaxSize), pipeline
     creation succeeds without error.)")
    .fn([](SVT& t) {
        skipIfSubgroupSizeControlUnsupported(t);
        const std::vector<uint32_t> validSizes = getValidSubgroupSizes(t);
        if (validSizes.empty()) {
            t.skip("device reports no valid subgroup sizes (subgroups unsupported)");
        }
        for (uint32_t subgroupSize : validSizes) {
            SVT::PipelineArgs args;
            args.expectedResult = true;
            args.code =
                "\n          enable subgroups;"
                "\n          enable subgroup_size_control;"
                "\n          override S: u32;"
                "\n          @workgroup_size(S) @subgroup_size(S)";
            args.constants["S"] = static_cast<double>(subgroupSize);
            args.addWorkgroupSize = false;
            t.expectPipelineResult(args);
        }
    });

// ---------------------------------------------------------------------------
// workgroup_size_x_must_be_multiple_of_subgroup_size_at_pipeline_creation
// ---------------------------------------------------------------------------
CTS_TEST(g, "workgroup_size_x_must_be_multiple_of_subgroup_size_at_pipeline_creation")
    .desc(R"(Checks that a pipeline-creation error results if the x-dimension of the entry point's
     workgroup_size is not a multiple of the subgroup_size value. Tests all combinations of
     constant and override expressions for both workgroup_size and subgroup_size.)")
    .params([](ParamsBuilder u) {
        return u
            .combine("workgroupSizeIsOverride", {Value(false), Value(true)})
            .combine("subgroupSizeIsOverride", {Value(false), Value(true)})
            .combine("offset", {Value(0), Value(1), Value(-1)})
            .combine("multiplier", {Value(1), Value(2), Value(3)});
    })
    .fn([](SVT& t) {
        skipIfSubgroupSizeControlUnsupported(t);
        const bool workgroupSizeIsOverride = t.param<bool>("workgroupSizeIsOverride");
        const bool subgroupSizeIsOverride = t.param<bool>("subgroupSizeIsOverride");
        const int64_t offset = t.param<int64_t>("offset");
        const int64_t multiplier = t.param<int64_t>("multiplier");

        const std::vector<uint32_t> validSizes = getValidSubgroupSizes(t);
        if (validSizes.empty()) {
            t.skip("device reports no valid subgroup sizes (subgroups unsupported)");
        }

        for (uint32_t subgroupSize : validSizes) {
            const int64_t workgroupSizeX =
                static_cast<int64_t>(subgroupSize) * multiplier + offset;
            if (workgroupSizeX <= 0) {
                continue;
            }
            const bool isMultiple = workgroupSizeX % static_cast<int64_t>(subgroupSize) == 0;

            const std::string sgStr = std::to_string(subgroupSize);
            const std::string wgStr = std::to_string(workgroupSizeX);
            const std::string wgExpr = workgroupSizeIsOverride ? "override_W" : "const_W";
            const std::string sgExpr = subgroupSizeIsOverride ? "override_S" : "const_S";

            SVT::PipelineArgs args;
            args.expectedResult = isMultiple;
            args.code =
                "\n            enable subgroups;"
                "\n            enable subgroup_size_control;"
                "\n            const const_S = " + sgStr + "u;"
                "\n            const const_W = " + wgStr + "u;"
                "\n            override override_S: u32;"
                "\n            override override_W: u32;"
                "\n            @workgroup_size(" + wgExpr + ")"
                "\n            @subgroup_size(" + sgExpr + ")"
                "\n          ";
            args.constants["override_S"] = static_cast<double>(subgroupSize);
            args.constants["override_W"] = static_cast<double>(workgroupSizeX);
            args.addWorkgroupSize = false;
            t.expectPipelineResult(args);
        }
    });

} // namespace
