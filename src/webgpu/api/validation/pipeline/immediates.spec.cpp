// Ported from gpuweb/cts src/webgpu/api/validation/pipeline/immediates.spec.ts
// Note: isAsync=true sub-cases use the same synchronous validation path (harness has no async
// pipeline-creation wrapper). Validation behaviour is identical for sync/async.
// Note: supportsImmediateData() — in native we check maxImmediateSize != 0 &&
//   maxImmediateSize != WGPU_LIMIT_U32_UNDEFINED (same signal: limit is advertised by the device).
// Note: The expandWithParams generator in upstream conditionally yields render-only sub-cases
//   when pipelineType == 'render'. In C++ this is handled by splitting into per-pipelineType
//   combineWithParams tables and doing the same skip/error logic inside fn().

#include <cstdint>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,pipeline,immediates",
    "Pipeline creation validation tests for immediate data size mismatches.");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// Generate WGSL code for a compute stage with immediate data of `size` bytes.
// If size == 0, no immediate data struct is declared.
static std::string makeComputeShaderCode(uint32_t size) {
    if (size == 0) {
        return "@compute @workgroup_size(1) fn main_compute() {}";
    }
    const uint32_t numFields = size / 4;
    std::string fields;
    for (uint32_t i = 0; i < numFields; ++i) {
        if (i > 0) {
            fields += ", ";
        }
        fields += "m" + std::to_string(i) + ": u32";
    }
    return "struct Immediates { " + fields + " }\n"
           "var<immediate> data: Immediates;\n"
           "fn use_data() { _ = data.m0; }\n"
           "@compute @workgroup_size(1) fn main_compute() { use_data(); }";
}

// Generate WGSL code for a vertex stage with immediate data of `size` bytes.
static std::string makeVertexShaderCode(uint32_t size) {
    if (size == 0) {
        return "@vertex fn main_vertex() -> @builtin(position) vec4<f32> { "
               "return vec4<f32>(0.0, 0.0, 0.0, 1.0); }";
    }
    const uint32_t numFields = size / 4;
    std::string fields;
    for (uint32_t i = 0; i < numFields; ++i) {
        if (i > 0) {
            fields += ", ";
        }
        fields += "m" + std::to_string(i) + ": u32";
    }
    return "struct Immediates { " + fields + " }\n"
           "var<immediate> data: Immediates;\n"
           "@vertex fn main_vertex() -> @builtin(position) vec4<f32> { "
           "_ = data.m0; return vec4<f32>(0.0, 0.0, 0.0, 1.0); }";
}

// Generate WGSL code for a fragment stage with immediate data of `size` bytes.
static std::string makeFragmentShaderCode(uint32_t size) {
    if (size == 0) {
        return "@fragment fn main_fragment() -> @location(0) vec4<f32> { "
               "return vec4<f32>(0.0, 1.0, 0.0, 1.0); }";
    }
    const uint32_t numFields = size / 4;
    std::string fields;
    for (uint32_t i = 0; i < numFields; ++i) {
        if (i > 0) {
            fields += ", ";
        }
        fields += "m" + std::to_string(i) + ": u32";
    }
    return "struct Immediates { " + fields + " }\n"
           "var<immediate> data: Immediates;\n"
           "@fragment fn main_fragment() -> @location(0) vec4<f32> { "
           "_ = data.m0; return vec4<f32>(0.0, 1.0, 0.0, 1.0); }";
}

// ---------------------------------------------------------------------------
// test: pipeline_creation_immediate_size_mismatch
// ---------------------------------------------------------------------------

// Sub-case descriptor: stageASize/stageBSize are encoded as int (>=0 for a concrete
// byte count) or via the magic sentinel values below.
static constexpr int kSizeSentinelMax          = -1; // resolves to maxImmediateSize
static constexpr int kSizeSentinelExceedLimits = -2; // resolves to maxImmediateSize + 4

// layoutSize is encoded as int (>=0) or as -3 meaning 'auto'.
// Using -3 to avoid collision with the stage size sentinels above (different field, but clearer).
static constexpr int kLayoutSizeAuto = -3;

struct SubcaseEntry {
    int stageASize;  // concrete byte count, kSizeSentinelMax, or kSizeSentinelExceedLimits
    int stageBSize;  // same; unused by compute (treated as 0)
    int layoutSize;  // concrete byte count or kLayoutSizeAuto
};

// Subcases shared by both compute and render pipelineType.
static const SubcaseEntry kCommonSubcases[] = {
    // Equal: stageASize == layoutSize -> success
    { 16, 16, 16 },
    // Shader smaller than layout -> success
    { 12, 12, 16 },
    // Shader larger by a small diff -> error
    { 20, 20, 16 },
    // Shader larger -> error
    { 32, 32, 16 },
    // StageA at maxImmediateSize, auto layout -> success
    { kSizeSentinelMax, 0, kLayoutSizeAuto },
    // StageA exceeds maxImmediateSize, auto layout -> error
    { kSizeSentinelExceedLimits, 0, kLayoutSizeAuto },
};

// Additional sub-cases only valid for render (stageB = fragment).
static const SubcaseEntry kRenderOnlySubcases[] = {
    // StageB (fragment) at maxImmediateSize, auto layout -> success
    { 0, kSizeSentinelMax, kLayoutSizeAuto },
    // Both stages at maxImmediateSize, auto layout -> success
    { kSizeSentinelMax, kSizeSentinelMax, kLayoutSizeAuto },
    // StageB exceeds maxImmediateSize, auto layout -> error
    { 0, kSizeSentinelExceedLimits, kLayoutSizeAuto },
};

CTS_TEST(g, "pipeline_creation_immediate_size_mismatch")
    .desc(
        "Validate that creating a compute or render pipeline fails if the shader uses\n"
        "immediate data larger than the immediateSize specified in the pipeline layout,\n"
        "or larger than maxImmediateSize if layout is 'auto'.\n"
        "Also validates that using less or equal size is allowed.\n"
        "\n"
        "For compute pipelines, stageASize is the compute stage size (stageBSize is unused).\n"
        "For render pipelines, stageASize is the vertex stage size and stageBSize is the\n"
        "fragment stage size.")
    .params([](ParamsBuilder u) {
        // Outer: pipelineType x isAsync x subcaseIndex
        // The subcaseIndex encodes both the SubcaseEntry index and whether it is from the
        // render-only table (encoded as 100 + index).
        return u
            .combine("pipelineType", {Value(std::string("compute")), Value(std::string("render"))})
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                // Common subcases: indices 0..5
                ParamRecord{{"subcaseIdx", Value(0)}},
                ParamRecord{{"subcaseIdx", Value(1)}},
                ParamRecord{{"subcaseIdx", Value(2)}},
                ParamRecord{{"subcaseIdx", Value(3)}},
                ParamRecord{{"subcaseIdx", Value(4)}},
                ParamRecord{{"subcaseIdx", Value(5)}},
                // Render-only subcases: encoded as 100 + index (0..2)
                ParamRecord{{"subcaseIdx", Value(100)}},
                ParamRecord{{"subcaseIdx", Value(101)}},
                ParamRecord{{"subcaseIdx", Value(102)}},
            })
            .filter([](const ParamRecord& params) {
                // Render-only subcases are only valid when pipelineType == "render"
                const int idx = valueAs<int>(*findParam(params, "subcaseIdx"));
                const std::string pipelineType =
                    valueAs<std::string>(*findParam(params, "pipelineType"));
                if (idx >= 100 && pipelineType != "render") {
                    return false;
                }
                return true;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t maxImmediateSize = t.getLimits().maxImmediateSize;
        // Skip if the device doesn't support immediate data.
        if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
            t.skip("Immediate data not supported (maxImmediateSize is 0 or undefined)");
        }

        const std::string pipelineType = t.param<std::string>("pipelineType");
        const int subcaseIdx           = t.param<int>("subcaseIdx");
        // isAsync: the harness has no async pipeline-creation wrapper; both paths are identical.
        (void)t.param<bool>("isAsync");

        // Resolve the SubcaseEntry.
        const SubcaseEntry* entry = nullptr;
        if (subcaseIdx >= 100) {
            entry = &kRenderOnlySubcases[subcaseIdx - 100];
        } else {
            entry = &kCommonSubcases[subcaseIdx];
        }

        // Resolve stageASize / stageBSize to concrete byte counts.
        auto resolveSize = [&](int sizeDescriptor) -> uint32_t {
            if (sizeDescriptor == kSizeSentinelMax) {
                return maxImmediateSize;
            }
            if (sizeDescriptor == kSizeSentinelExceedLimits) {
                return maxImmediateSize + 4;
            }
            return static_cast<uint32_t>(sizeDescriptor);
        };

        const uint32_t resolvedStageASize = resolveSize(entry->stageASize);
        const uint32_t resolvedStageBSize = resolveSize(entry->stageBSize);

        // Ensure the test's fixed sizes fit within the device limit (mirrors upstream assert).
        if (entry->stageASize != kSizeSentinelExceedLimits) {
            if (resolvedStageASize > maxImmediateSize) {
                t.fail("stageASize (" + std::to_string(resolvedStageASize)
                       + ") must be <= maxImmediateSize (" + std::to_string(maxImmediateSize) + ")");
            }
        }
        if (entry->stageBSize != kSizeSentinelExceedLimits) {
            if (resolvedStageBSize > maxImmediateSize) {
                t.fail("stageBSize (" + std::to_string(resolvedStageBSize)
                       + ") must be <= maxImmediateSize (" + std::to_string(maxImmediateSize) + ")");
            }
        }

        // Build the pipeline layout (or use auto = null layout).
        WGPUPipelineLayout layout    = nullptr;
        uint32_t           validSize = 0;

        if (entry->layoutSize == kLayoutSizeAuto) {
            layout    = nullptr;  // auto layout in the C API is represented by null
            validSize = maxImmediateSize;
        } else {
            WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            plDesc.bindGroupLayoutCount = 0;
            plDesc.bindGroupLayouts     = nullptr;
            plDesc.immediateSize        = static_cast<uint32_t>(entry->layoutSize);
            layout = t.createPipelineLayoutTracked(plDesc);
            validSize = static_cast<uint32_t>(entry->layoutSize);
        }

        const bool stageAExceedsLimit = resolvedStageASize > validSize;
        const bool stageBExceedsLimit = resolvedStageBSize > validSize;
        const bool shouldError        = stageAExceedsLimit || stageBExceedsLimit;

        if (pipelineType == "compute") {
            // Build compute pipeline descriptor.
            const std::string computeCode = makeComputeShaderCode(resolvedStageASize);
            WGPUShaderModule computeModule = t.createShaderModuleTracked(computeCode);

            WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            desc.layout                        = layout;
            desc.compute.module                = computeModule;
            desc.compute.entryPoint            = sv("main_compute");

            t.expectValidationError([&] {
                t.createComputePipelineTracked(desc);
            }, shouldError);
        } else {
            // Build render pipeline descriptor.
            const std::string vertexCode   = makeVertexShaderCode(resolvedStageASize);
            const std::string fragmentCode = makeFragmentShaderCode(resolvedStageBSize);

            WGPUShaderModule vertexModule   = t.createShaderModuleTracked(vertexCode);
            WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentCode);

            WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
            colorTarget.format               = WGPUTextureFormat_RGBA8Unorm;

            WGPUFragmentState fragment   = WGPU_FRAGMENT_STATE_INIT;
            fragment.module              = fragmentModule;
            fragment.entryPoint          = sv("main_fragment");
            fragment.targetCount         = 1;
            fragment.targets             = &colorTarget;

            WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
            desc.layout                       = layout;
            desc.vertex.module                = vertexModule;
            desc.vertex.entryPoint            = sv("main_vertex");
            desc.fragment                     = &fragment;

            t.expectValidationError([&] {
                t.createRenderPipelineTracked(desc);
            }, shouldError);
        }
    });

} // namespace
