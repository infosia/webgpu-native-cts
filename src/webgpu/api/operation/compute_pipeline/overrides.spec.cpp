// Ported from gpuweb/cts src/webgpu/api/operation/compute_pipeline/overrides.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports basic for isAsync=false (1 case); async + other 6 tests deferred.

#include <array>
#include <cstdint>
#include <string_view>
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
//   1 case (isAsync=false); dispatches 1 workgroup and verifies 11 u32 output values.
//   8 constants are pipeline-set; c4, c7, c10 use their WGSL defaults (4, 7, 10).
//   Expected output: buf.data = {0,1,2,3,4,5,6,7,8,9,10}.
CTS_TEST(g, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("isAsync", {false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
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
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

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
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
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

} // namespace
