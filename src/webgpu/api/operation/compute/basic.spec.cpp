// Ported from gpuweb/cts src/webgpu/api/operation/compute/basic.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,compute,basic",
    "Basic compute operation tests.");

constexpr std::string_view kMemcpyShader = R"(
struct Data { value : u32 };
@group(0) @binding(0) var<storage, read> src : Data;
@group(0) @binding(1) var<storage, read_write> dst : Data;
@compute @workgroup_size(1) fn main() {
  dst.value = src.value;
  return;
}
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

WGPUBuffer createStorageDestinationBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = 4;
    desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
    return t.createBufferTracked(desc);
}

WGPUBindGroupLayout createMemcpyBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries;

    entries[0] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    entries[1] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entries[1].buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

WGPUPipelineLayout createPipelineLayout(AllFeaturesMaxLimitsGpuTest& t, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    return t.createPipelineLayoutTracked(desc);
}

WGPUComputePipeline createMemcpyPipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(kMemcpyShader);

    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = stringView("main");
    return t.createComputePipelineTracked(desc);
}

WGPUBindGroup createMemcpyBindGroup(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    WGPUBuffer src,
    WGPUBuffer dst) {
    std::array<WGPUBindGroupEntry, 2> entries;

    entries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].buffer = src;
    entries[0].offset = 0;
    entries[0].size = 4;

    entries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[1].binding = 1;
    entries[1].buffer = dst;
    entries[1].offset = 0;
    entries[1].size = 4;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

CTS_TEST(g, "memcpy")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t data = 0x01020304;
        WGPUBuffer src = t.makeBufferWithContents(&data, sizeof(data), WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage);
        WGPUBuffer dst = createStorageDestinationBuffer(t);

        WGPUBindGroupLayout bindGroupLayout = createMemcpyBindGroupLayout(t);
        WGPUPipelineLayout pipelineLayout = createPipelineLayout(t, bindGroupLayout);
        WGPUComputePipeline pipeline = createMemcpyPipeline(t, pipelineLayout);
        WGPUBindGroup bindGroup = createMemcpyBindGroup(t, bindGroupLayout, src, dst);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        submit(t, encoder);

        t.expectGPUBufferValuesEqual(dst, &data, sizeof(data));
    });

CTS_TEST(g, "large_dispatch")
    .desc("Test reasonably-sized large dispatches (see also: stress tests).")
    .params([](ParamsBuilder u) {
        // Reasonably-sized powers of two, and some stranger larger sizes.
        return u.combine("dispatchSize", {256, 2048, 315, 628, 2179, "maximum"})
            // Test some reasonable workgroup sizes.
            .beginSubcases()
            // 0 == x axis; 1 == y axis; 2 == z axis.
            .combine("largeDimension", {0, 1, 2})
            .expand("workgroupSize", [](const ParamRecord&) {
                return std::vector<Value>{1, 2, 8, 32, "maximum"};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // The output storage buffer is filled with this value.
        const uint32_t val = 0x01020304;
        const uint32_t badVal = 0xbaadf00d;

        const WGPULimits limits = t.getLimits();
        const std::array<uint32_t, 3> kMaxComputeWorkgroupSize = {
            limits.maxComputeWorkgroupSizeX,
            limits.maxComputeWorkgroupSizeY,
            limits.maxComputeWorkgroupSizeZ,
        };

        const size_t largeDimension = static_cast<size_t>(t.param<int64_t>("largeDimension"));
        const uint32_t wgSize = t.paramIsString("workgroupSize")
                                    ? kMaxComputeWorkgroupSize[largeDimension]
                                    : static_cast<uint32_t>(t.param<int64_t>("workgroupSize"));
        const uint32_t dispatchSize = t.paramIsString("dispatchSize")
                                          ? limits.maxComputeWorkgroupsPerDimension
                                          : static_cast<uint32_t>(t.param<int64_t>("dispatchSize"));
        const uint64_t bufferLength = static_cast<uint64_t>(dispatchSize) * wgSize;
        const uint64_t bufferByteSize = sizeof(uint32_t) * bufferLength;
        WGPUBufferDescriptor dstDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        dstDesc.size = bufferByteSize;
        dstDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
        WGPUBuffer dst = t.createBufferTracked(dstDesc);

        // Only use one large dimension and workgroup size in the dispatch
        // call to keep the size of the test reasonable.
        std::array<uint32_t, 3> dims = {1, 1, 1};
        dims[largeDimension] = dispatchSize;
        std::array<uint32_t, 3> wgSizes = {1, 1, 1};
        wgSizes[largeDimension] = wgSize;

        const std::string wgsl =
            "struct OutputBuffer {\n"
            "  value : array<u32>\n"
            "};\n"
            "\n"
            "@group(0) @binding(0) var<storage, read_write> dst : OutputBuffer;\n"
            "\n"
            "@compute @workgroup_size(" + std::to_string(wgSizes[0]) + ", " + std::to_string(wgSizes[1]) + ", " +
            std::to_string(wgSizes[2]) + ")\n"
            "fn main(\n"
            "  @builtin(global_invocation_id) GlobalInvocationID : vec3<u32>\n"
            ") {\n"
            "  var xExtent : u32 = " + std::to_string(dims[0]) + "u * " + std::to_string(wgSizes[0]) + "u;\n"
            "  var yExtent : u32 = " + std::to_string(dims[1]) + "u * " + std::to_string(wgSizes[1]) + "u;\n"
            "  var zExtent : u32 = " + std::to_string(dims[2]) + "u * " + std::to_string(wgSizes[2]) + "u;\n"
            "  var index : u32 = (\n"
            "    GlobalInvocationID.z * xExtent * yExtent +\n"
            "    GlobalInvocationID.y * xExtent +\n"
            "    GlobalInvocationID.x);\n"
            "  var val : u32 = " + std::to_string(val) + "u;\n"
            "  // Trivial error checking in the indexing and invocation.\n"
            "  if (GlobalInvocationID.x > xExtent ||\n"
            "      GlobalInvocationID.y > yExtent ||\n"
            "      GlobalInvocationID.z > zExtent) {\n"
            "    val = " + std::to_string(badVal) + "u;\n"
            "  }\n"
            "  dst.value[index] = val;\n"
            "}\n";

        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = nullptr;  // 'auto'
        pipelineDesc.compute.module = t.createShaderModuleTracked(wgsl);
        pipelineDesc.compute.entryPoint = stringView("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

        WGPUBindGroupLayout layout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = dst;
        entry.offset = 0;
        entry.size = bufferByteSize;
        WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.layout = layout;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries = &entry;
        WGPUBindGroup bg = t.createBindGroupTracked(bindGroupDesc);
        wgpuBindGroupLayoutRelease(layout);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = t.beginComputePassTracked(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, dims[0], dims[1], dims[2]);
        wgpuComputePassEncoderEnd(pass);
        submit(t, encoder);

        // checkElementsEqualGenerated(a, _i => val) over the whole buffer, read in place from the
        // mapped staging buffer (no extra host copy).
        t.expectGPUBufferValuesPassCheck(
            dst,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                const size_t count = len / sizeof(uint32_t);
                size_t failures = 0;
                size_t firstFailure = 0;
                uint32_t firstActual = 0;
                for (size_t i = 0; i < count; ++i) {
                    uint32_t value = 0;
                    std::memcpy(&value, actual + i * sizeof(uint32_t), sizeof(value));
                    if (value != val) {
                        if (failures == 0) {
                            firstFailure = i;
                            firstActual = value;
                        }
                        ++failures;
                    }
                }
                if (failures == 0) {
                    return std::nullopt;
                }
                std::ostringstream message;
                message << failures << " of " << count << " elements differ from 0x" << std::hex << val
                        << "; first at index " << std::dec << firstFailure << ": got 0x" << std::hex
                        << firstActual;
                return message.str();
            },
            0,
            static_cast<size_t>(bufferByteSize));

        wgpuBufferDestroy(dst);
    });

} // namespace
