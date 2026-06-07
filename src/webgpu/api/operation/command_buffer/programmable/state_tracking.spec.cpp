// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/programmable/state_tracking.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports bind_group_indices for compute pass / storage; render-pass + uniform + other tests deferred.

#include <array>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,programmable,state_tracking",
    "Tests for programmable state tracking in command buffers.");

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Build the WGSL shader string for the given group indices {a, b, out}.
// Each permutation bakes different @group(N) annotations into the source.
std::string buildShader(uint32_t groupA, uint32_t groupB, uint32_t groupOut) {
    std::ostringstream ss;
    ss << "struct I32 { value : i32 };\n"
       << "@group(" << groupA   << ") @binding(0) var<storage, read>       a      : I32;\n"
       << "@group(" << groupB   << ") @binding(0) var<storage, read>       b      : I32;\n"
       << "@group(" << groupOut << ") @binding(0) var<storage, read_write> result : I32;\n"
       << "@compute @workgroup_size(1) fn main() { result.value = a.value - b.value; }\n";
    return ss.str();
}

// Create a bind-group layout with a single buffer binding at binding 0, compute-visible.
WGPUBindGroupLayout createSingleBufferBGL(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBufferBindingType type) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = WGPUShaderStage_Compute;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = type;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

// Create a bind group binding a single buffer (binding 0, full size).
WGPUBindGroup createSingleBufferBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUBuffer buffer,
    uint64_t size) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer;
    entry.offset = 0;
    entry.size = size;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

// Returns the 6 permutations of {a, b, out} group indices drawn from {0, 1, 2}.
std::vector<Value> groupIndexPermutations() {
    return {
        Value("0,1,2"),
        Value("1,2,0"),
        Value("2,0,1"),
        Value("0,2,1"),
        Value("2,1,0"),
        Value("1,0,2"),
    };
}

// Parse "a,b,out" string into the three group indices.
void parseGroupIndices(const std::string& s, uint32_t& a, uint32_t& b, uint32_t& out) {
    // Format: "X,Y,Z" where X, Y, Z are single digits 0-2.
    a   = static_cast<uint32_t>(s[0] - '0');
    b   = static_cast<uint32_t>(s[2] - '0');
    out = static_cast<uint32_t>(s[4] - '0');
}

CTS_TEST(g, "bind_group_indices")
    .desc("Bind groups can be assigned to any group index; compute shader reads a - b == 1.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("groupIndices", groupIndexPermutations());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string groupIndicesStr = t.param<std::string>("groupIndices");
        uint32_t groupA = 0, groupB = 0, groupOut = 0;
        parseGroupIndices(groupIndicesStr, groupA, groupB, groupOut);

        // Build and compile the shader with the baked group indices.
        const std::string shaderSrc = buildShader(groupA, groupB, groupOut);
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(shaderSrc);

        // Create bind-group layouts: read-only for a and b, read_write for out.
        WGPUBindGroupLayout bglRead    = createSingleBufferBGL(t, WGPUBufferBindingType_ReadOnlyStorage);
        WGPUBindGroupLayout bglStorage = createSingleBufferBGL(t, WGPUBufferBindingType_Storage);

        // Build an explicit pipeline layout with 3 BGL slots.
        // Each slot is placed at the group index that the shader uses.
        std::array<WGPUBindGroupLayout, 3> bgls{};
        bgls[groupA]   = bglRead;
        bgls[groupB]   = bglRead;
        bgls[groupOut] = bglStorage;

        WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.bindGroupLayoutCount = bgls.size();
        layoutDesc.bindGroupLayouts = bgls.data();
        WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(layoutDesc);

        // Create the compute pipeline.
        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.compute.module = shaderModule;
        pipelineDesc.compute.entryPoint = stringView("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

        // Buffers: bufA = i32(3), bufB = i32(2), bufOut = i32(0).
        // All buffers need STORAGE | COPY_SRC | COPY_DST.
        const WGPUBufferUsage kBufUsage =
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;

        const int32_t valA   = 3;
        const int32_t valB   = 2;
        const int32_t valOut = 0;
        WGPUBuffer bufA   = t.makeBufferWithContents(&valA,   sizeof(valA),   kBufUsage);
        WGPUBuffer bufB   = t.makeBufferWithContents(&valB,   sizeof(valB),   kBufUsage);
        WGPUBuffer bufOut = t.makeBufferWithContents(&valOut, sizeof(valOut), kBufUsage);

        // Create bind groups: bgA and bgB use bglRead, bgOut uses bglStorage.
        WGPUBindGroup bgA   = createSingleBufferBindGroup(t, bglRead,    bufA,   sizeof(int32_t));
        WGPUBindGroup bgB   = createSingleBufferBindGroup(t, bglRead,    bufB,   sizeof(int32_t));
        WGPUBindGroup bgOut = createSingleBufferBindGroup(t, bglStorage, bufOut, sizeof(int32_t));

        // Encode the compute pass: set pipeline, set all bind groups, dispatch.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, groupA,   bgA,   0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, groupB,   bgB,   0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, groupOut, bgOut, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // Verify: a.value - b.value == 3 - 2 == 1.
        const int32_t expected = 1;
        t.expectGPUBufferValuesEqual(bufOut, &expected, sizeof(expected));
    });

} // namespace
