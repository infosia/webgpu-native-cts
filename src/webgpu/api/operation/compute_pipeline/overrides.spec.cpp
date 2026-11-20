// Ported from gpuweb/cts src/webgpu/api/operation/compute_pipeline/overrides.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports all 7 upstream tests: basic, numeric_id, computed, precision, workgroup_size,
// shared_shader_module, multi_entry_points.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,compute_pipeline,overrides",
    "Compute pipeline pipeline-overridable constants tests.");

// Shader with 11 override constants of mixed types (bool, f32, i32, u32).
// c0,c1 = bool; c2,c3,c4 = f32; c5,c6,c7 = i32; c8,c9,c10 = u32.
// c4=4.0, c7=7, c10=10u are WGSL defaults (not supplied via pipeline constants).
constexpr std::string_view kOverridesShader = R"(
override c0: bool;          override c1: bool = false;
override c2: f32;           override c3: f32 = 0.0;     override c4: f32 = 4.0;
override c5: i32;           override c6: i32 = 0;       override c7: i32 = 7;
override c8: u32;           override c9: u32 = 0u;      override c10: u32 = 10u;
struct Buf { data : array<u32, 11> }
@group(0) @binding(0) var<storage, read_write> buf : Buf;
@compute @workgroup_size(1) fn main() {
  buf.data[0]=u32(c0); buf.data[1]=u32(c1); buf.data[2]=u32(c2); buf.data[3]=u32(c3);
  buf.data[4]=u32(c4); buf.data[5]=u32(c5); buf.data[6]=u32(c6); buf.data[7]=u32(c7);
  buf.data[8]=u32(c8); buf.data[9]=u32(c9); buf.data[10]=u32(c10);
}
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Build a WGPUConstantEntry with the given WGSL identifier key and double value.
WGPUConstantEntry makeConstantEntry(std::string_view key, double value) {
    WGPUConstantEntry entry = WGPU_CONSTANT_ENTRY_INIT;
    entry.key   = stringView(key);
    entry.value = value;
    return entry;
}

// basic:
//   2 cases (isAsync=true/false); each dispatches 1 workgroup and verifies 11 u32 output values.
//   8 constants are pipeline-set; c4, c7, c10 use their WGSL defaults (4, 7, 10).
//   Expected output: buf.data = {0,1,2,3,4,5,6,7,8,9,10}.
CTS_TEST(g, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        // Storage buffer: 11 u32 = 44 bytes, zero-initialized.
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 11 * sizeof(uint32_t);
        bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer dst = t.createBufferTracked(bufDesc);

        // Build the 8 pipeline-supplied constant entries.
        // c4 (4.0), c7 (7), c10 (10u) are omitted — they use their WGSL defaults.
        // bool values are supplied as 1.0 (true) or 0.0 (false).
        // Keep this vector alive until createComputePipeline returns.
        std::vector<WGPUConstantEntry> constants;
        constants.reserve(8);
        constants.push_back(makeConstantEntry("c0", 0.0)); // false → u32(0)
        constants.push_back(makeConstantEntry("c1", 1.0)); // true  → u32(1)
        constants.push_back(makeConstantEntry("c2", 2.0));
        constants.push_back(makeConstantEntry("c3", 3.0));
        constants.push_back(makeConstantEntry("c5", 5.0));
        constants.push_back(makeConstantEntry("c6", 6.0));
        constants.push_back(makeConstantEntry("c8", 8.0));
        constants.push_back(makeConstantEntry("c9", 9.0));

        // Compute pipeline: layout:auto, the override shader, constants applied.
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(kOverridesShader);

        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        // layout:auto — pipeDesc.layout is null after INIT, which selects automatic layout.
        pipeDesc.compute.module        = shaderModule;
        pipeDesc.compute.entryPoint    = stringView("main");
        pipeDesc.compute.constantCount = constants.size();
        pipeDesc.compute.constants     = constants.data();
        WGPUComputePipeline pipeline = t.createComputePipelineSyncOrAsync(isAsync, pipeDesc);

        // Bind group: auto layout at group 0, binding 0 → dst buffer.
        WGPUBindGroupLayout bindGroupLayout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding = 0;
        bgEntry.buffer  = dst;
        bgEntry.offset  = 0;
        bgEntry.size    = 11 * sizeof(uint32_t);

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bindGroupLayout;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &bgEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        wgpuBindGroupLayoutRelease(bindGroupLayout);

        // Compute pass: set pipeline, bind group, dispatch 1 workgroup, end.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // Verify: expected buf.data = {0,1,2,3,4,5,6,7,8,9,10}.
        const std::array<uint32_t, 11> expected = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
        t.expectGPUBufferValuesEqual(dst, expected.data(), expected.size() * sizeof(uint32_t));
    });

// ---------------------------------------------------------------------------------------------
// Shared helpers for the tests after `basic` (port of upstream F.expectShaderOutputWithConstants).
// ---------------------------------------------------------------------------------------------

// A pipeline-constant record: key (WGSL name or decimal numeric id) -> value.
using ConstantList = std::vector<std::pair<std::string, double>>;

// Converts `constants` to WGPUConstantEntry values. The returned entries point into `constants`,
// which must stay alive until the pipeline is created.
std::vector<WGPUConstantEntry> toConstantEntries(const ConstantList& constants) {
    std::vector<WGPUConstantEntry> entries;
    entries.reserve(constants.size());
    for (const auto& [key, value] : constants) {
        entries.push_back(makeConstantEntry(key, value));
    }
    return entries;
}

// Builds a layout:auto compute pipeline descriptor for `module`/`entryPoint` with `entries`.
WGPUComputePipelineDescriptor makeComputeDescriptor(WGPUShaderModule module,
                                                    std::string_view entryPoint,
                                                    const std::vector<WGPUConstantEntry>& entries) {
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    // layout:auto — desc.layout is null after INIT.
    desc.compute.module        = module;
    desc.compute.entryPoint    = stringView(entryPoint);
    desc.compute.constantCount = entries.size();
    desc.compute.constants     = entries.empty() ? nullptr : entries.data();
    return desc;
}

// Storage + CopySrc buffer of `size` bytes (zero-initialized by WebGPU).
WGPUBuffer createOutputBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size  = size;
    desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
    return t.createBufferTracked(desc);
}

// Bind group for group 0 of `pipeline` with `buffer` bound at binding 0 over [0, size).
WGPUBindGroup createOutputBindGroup(AllFeaturesMaxLimitsGpuTest& t,
                                    WGPUComputePipeline pipeline,
                                    WGPUBuffer buffer,
                                    uint64_t size) {
    WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer  = buffer;
    entry.offset  = 0;
    entry.size    = size;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout     = layout;
    desc.entryCount = 1;
    desc.entries    = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(desc);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

// One (pipeline, bind group) dispatch of a single workgroup.
struct Dispatch {
    WGPUComputePipeline pipeline;
    WGPUBindGroup bindGroup;
};

// Encodes every dispatch in order into one compute pass and submits it.
void encodeAndSubmit(AllFeaturesMaxLimitsGpuTest& t, const std::vector<Dispatch>& dispatches) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);
    for (const Dispatch& dispatch : dispatches) {
        wgpuComputePassEncoderSetPipeline(pass, dispatch.pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, dispatch.bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    }
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

// Port of upstream F.expectShaderOutputWithConstants: creates a layout:auto pipeline from `code`
// (entry point "main") with `constants`, dispatches one workgroup writing into a zeroed buffer of
// `expectedSize` bytes, and expects the buffer to equal `expected`.
void expectShaderOutputWithConstants(AllFeaturesMaxLimitsGpuTest& t,
                                     bool isAsync,
                                     const void* expected,
                                     size_t expectedSize,
                                     const ConstantList& constants,
                                     std::string_view code) {
    WGPUBuffer dst = createOutputBuffer(t, expectedSize);

    WGPUShaderModule module = t.createShaderModuleTracked(code);
    const std::vector<WGPUConstantEntry> entries = toConstantEntries(constants);
    const WGPUComputePipelineDescriptor desc = makeComputeDescriptor(module, "main", entries);
    WGPUComputePipeline pipeline = t.createComputePipelineSyncOrAsync(isAsync, desc);

    WGPUBindGroup bindGroup = createOutputBindGroup(t, pipeline, dst, expectedSize);
    encodeAndSubmit(t, {Dispatch{pipeline, bindGroup}});

    t.expectGPUBufferValuesEqual(dst, expected, expectedSize);
}

CTS_TEST(g, "numeric_id")
    .desc("Test that correct values are used as output to the storage buffer for constants specified "
          "with numeric id instead of their names.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const std::array<uint32_t, 3> expected = {1, 2, 3};
        expectShaderOutputWithConstants(
            t, isAsync, expected.data(), expected.size() * sizeof(uint32_t),
            {
                {"1001", 1.0},
                {"1", 2.0},
                // 1003 is using default value
            },
            R"(
        @id(1001) override c1: u32;            // some big numeric id
        @id(1) override c2: u32 = 0u;          // id == 1 might collide with some generated constant id
        @id(1003) override c3: u32 = 3u;       // default

        struct Buf {
            data : array<u32, 3>
        }

        @group(0) @binding(0) var<storage, read_write> buf : Buf;

        @compute @workgroup_size(1) fn main() {
            buf.data[0] = c1;
            buf.data[1] = c2;
            buf.data[2] = c3;
        }
      )");
    });

CTS_TEST(g, "computed")
    .desc("Test that computed overrides work correctly")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUShaderModule module = t.createShaderModuleTracked(R"(
      override c0: f32 = 0.;
      override c1: f32 = 0.;
      override c2: f32 = c0 * c1;

      struct Buf {
          data : array<u32, 3>,
      }

      @group(0) @binding(0) var<storage, read_write> buf : Buf;

      @compute @workgroup_size(1) fn main() {
          buf.data[0] = u32(c0);
          buf.data[1] = u32(c1);
          buf.data[2] = u32(c2);
      }
    )");

        const std::array<uint32_t, 3> expected = {2, 4, 8};
        constexpr uint64_t kSize = 3 * sizeof(uint32_t);
        WGPUBuffer buffer = createOutputBuffer(t, kSize);

        const ConstantList constants = {{"c0", 2.0}, {"c1", 4.0}};
        const std::vector<WGPUConstantEntry> entries = toConstantEntries(constants);
        const WGPUComputePipelineDescriptor desc = makeComputeDescriptor(module, "main", entries);
        // Upstream always uses createComputePipelineAsync here.
        WGPUComputePipeline pipeline = t.createComputePipelineSyncOrAsync(true, desc);

        WGPUBindGroup bindGroup = createOutputBindGroup(t, pipeline, buffer, kSize);
        encodeAndSubmit(t, {Dispatch{pipeline, bindGroup}});

        t.expectGPUBufferValuesEqual(buffer, expected.data(), kSize);
    });

CTS_TEST(g, "precision")
    .desc("Test that float number precision is preserved for constants as they are used for compute "
          "shader output of the storage buffer.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const double c1 = 3.14159;
        const double c2 = 3.141592653589793;
        // These values get rounded to f32 (as upstream's Float32Array does), so the values coming
        // out of the shader are not exactly the doubles above.
        const std::array<float, 2> expected = {static_cast<float>(c1), static_cast<float>(c2)};
        expectShaderOutputWithConstants(
            t, isAsync, expected.data(), expected.size() * sizeof(float),
            {
                {"c1", c1},
                {"c2", c2},
            },
            R"(
        override c1: f32;
        override c2: f32;

        struct Buf {
            data : array<f32, 2>
        }

        @group(0) @binding(0) var<storage, read_write> buf : Buf;

        @compute @workgroup_size(1) fn main() {
            buf.data[0] = c1;
            buf.data[1] = c2;
        }
      )");
    });

CTS_TEST(g, "workgroup_size")
    .desc("Test that constants can be used as workgroup size correctly, the compute shader should "
          "write the max local invocation id to the storage buffer which is equal to the workgroup "
          "size dimension given by the constant.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false})
            .combine("type", {"u32", "i32"})
            .combine("size", {3, 16, 64})
            .combine("v", {"x", "y", "z"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        const std::string type = t.param<std::string>("type");
        const uint32_t size = static_cast<uint32_t>(t.param<int>("size"));
        const std::string v = t.param<std::string>("v");

        std::string workgroupSizeStr = "1, 1, d";
        if (v == "x") {
            workgroupSizeStr = "d";
        } else if (v == "y") {
            workgroupSizeStr = "1, d";
        }

        const std::string code =
            "\n        override d: " + type + ";\n"
            "\n"
            "        struct Buf {\n"
            "            data : array<u32, 1>\n"
            "        }\n"
            "\n"
            "        @group(0) @binding(0) var<storage, read_write> buf : Buf;\n"
            "\n"
            "        @compute @workgroup_size(" + workgroupSizeStr + ") fn main(\n"
            "            @builtin(local_invocation_id) local_invocation_id : vec3<u32>\n"
            "        ) {\n"
            "            if (local_invocation_id." + v + " >= u32(d - 1)) {\n"
            "                buf.data[0] = local_invocation_id." + v + " + 1;\n"
            "            }\n"
            "        }\n";

        const std::array<uint32_t, 1> expected = {size};
        expectShaderOutputWithConstants(t, isAsync, expected.data(), sizeof(uint32_t),
                                        {{"d", static_cast<double>(size)}}, code);
    });

CTS_TEST(g, "shared_shader_module")
    .desc("Test that when the same shader module is shared by different pipelines, the correct "
          "constant values are used as output to the storage buffer. The constant value should not "
          "affect other pipeline sharing the same shader module.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        WGPUShaderModule module = t.createShaderModuleTracked(R"(
      override a: u32;

      struct Buf {
          data : array<u32, 1>
      }

      @group(0) @binding(0) var<storage, read_write> buf : Buf;

      @compute @workgroup_size(1) fn main() {
          buf.data[0] = a;
      })");

        const std::array<uint32_t, 2> expects = {1, 2};
        const std::array<ConstantList, 2> constants = {
            ConstantList{{"a", 1.0}},
            ConstantList{{"a", 2.0}},
        };

        std::vector<Dispatch> dispatches;
        std::array<WGPUBuffer, 2> buffers = {};
        for (size_t i = 0; i < buffers.size(); ++i) {
            buffers[i] = createOutputBuffer(t, sizeof(uint32_t));
        }
        std::array<WGPUComputePipeline, 2> pipelines = {};
        for (size_t i = 0; i < pipelines.size(); ++i) {
            const std::vector<WGPUConstantEntry> entries = toConstantEntries(constants[i]);
            const WGPUComputePipelineDescriptor desc = makeComputeDescriptor(module, "main", entries);
            pipelines[i] = t.createComputePipelineSyncOrAsync(isAsync, desc);
        }
        for (size_t i = 0; i < pipelines.size(); ++i) {
            dispatches.push_back(
                Dispatch{pipelines[i], createOutputBindGroup(t, pipelines[i], buffers[i], sizeof(uint32_t))});
        }
        encodeAndSubmit(t, dispatches);

        for (size_t i = 0; i < buffers.size(); ++i) {
            t.expectGPUBufferValuesEqual(buffers[i], &expects[i], sizeof(uint32_t));
        }
    });

CTS_TEST(g, "multi_entry_points")
    .desc("Test that constants used for different entry points are used correctly as output to the "
          "storage buffer. They should have no impact for pipeline using entry points that doesn't "
          "reference them.")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isAsync = t.param<bool>("isAsync");
        WGPUShaderModule module = t.createShaderModuleTracked(R"(
    override c1: u32;
    override c2: u32;
    override c3: u32;

    struct Buf {
        data : array<u32, 1>
    }

    @group(0) @binding(0) var<storage, read_write> buf : Buf;

    @compute @workgroup_size(1) fn main1() {
        buf.data[0] = c1;
    }

    @compute @workgroup_size(1) fn main2() {
        buf.data[0] = c2;
    }

    @compute @workgroup_size(c3) fn main3() {
        buf.data[0] = 3u;
    })");

        const std::array<uint32_t, 4> expects = {1, 2, 3, 4};
        const std::array<std::string_view, 4> entryPoints = {"main1", "main2", "main3", "main1"};
        const std::array<ConstantList, 4> constants = {
            ConstantList{{"c1", 1.0}},
            ConstantList{{"c2", 2.0}},
            // c3 is used as workgroup size
            ConstantList{{"c3", 1.0}},
            // assign a different value to c1
            ConstantList{{"c1", 4.0}},
        };

        std::array<WGPUBuffer, 4> buffers = {};
        for (size_t i = 0; i < buffers.size(); ++i) {
            buffers[i] = createOutputBuffer(t, sizeof(uint32_t));
        }
        std::array<WGPUComputePipeline, 4> pipelines = {};
        for (size_t i = 0; i < pipelines.size(); ++i) {
            const std::vector<WGPUConstantEntry> entries = toConstantEntries(constants[i]);
            const WGPUComputePipelineDescriptor desc =
                makeComputeDescriptor(module, entryPoints[i], entries);
            pipelines[i] = t.createComputePipelineSyncOrAsync(isAsync, desc);
        }
        std::vector<Dispatch> dispatches;
        for (size_t i = 0; i < pipelines.size(); ++i) {
            dispatches.push_back(
                Dispatch{pipelines[i], createOutputBindGroup(t, pipelines[i], buffers[i], sizeof(uint32_t))});
        }
        encodeAndSubmit(t, dispatches);

        for (size_t i = 0; i < buffers.size(); ++i) {
            t.expectGPUBufferValuesEqual(buffers[i], &expects[i], sizeof(uint32_t));
        }
    });

} // namespace
