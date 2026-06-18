// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/arrayLength.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for arrayLength(&storage_runtime_array): the result is the
// runtime element count derived from the bound buffer size, floored to whole
// elements. The length buffer is zero-initialized so a silent no-write surfaces
// as a mismatch (the expected lengths are non-zero in every case).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

uint32_t alignUp(uint32_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

struct TestType {
    const char* type;
    uint32_t stride;
};

// List of array element types to test (matches upstream kTestTypes).
const std::vector<TestType>& kTestTypes() {
    static const std::vector<TestType> types = {
        {"u32", 4},
        {"i32", 4},
        {"f32", 4},
        {"f16", 2},
        {"vec2<u32>", 8},
        {"vec2<i32>", 8},
        {"vec2<f32>", 8},
        {"vec2<f16>", 4},
        {"vec3<u32>", 16},
        {"vec3<i32>", 16},
        {"vec3<f32>", 16},
        {"vec3<f16>", 8},
        {"vec4<u32>", 16},
        {"vec4<i32>", 16},
        {"vec4<f32>", 16},
        {"vec4<f16>", 8},
        {"mat2x2<f32>", 16},
        {"mat2x3<f32>", 32},
        {"mat2x4<f32>", 32},
        {"mat3x2<f32>", 24},
        {"mat3x3<f32>", 48},
        {"mat3x4<f32>", 48},
        {"mat4x2<f32>", 32},
        {"mat4x3<f32>", 64},
        {"mat4x4<f32>", 64},
        {"mat2x2<f16>", 8},
        {"mat2x3<f16>", 16},
        {"mat2x4<f16>", 16},
        {"mat3x2<f16>", 12},
        {"mat3x3<f16>", 24},
        {"mat3x4<f16>", 24},
        {"mat4x2<f16>", 16},
        {"mat4x3<f16>", 32},
        {"mat4x4<f16>", 32},
        {"atomic<u32>", 4},
        {"atomic<i32>", 4},
        {"array<u32,4>", 16},
        {"array<i32,4>", 16},
        {"array<f32,4>", 16},
        {"array<f16,4>", 8},
        {"ElemStruct", 4},
        {"ElemStruct_ImplicitPadding", 16},
        {"ElemStruct_ExplicitPadding", 32},
    };
    return types;
}

const char* kWgslStructures =
    "\n"
    "struct ElemStruct { a : u32 }\n"
    "struct ElemStruct_ImplicitPadding { a : vec3<u32> }\n"
    "struct ElemStruct_ExplicitPadding { @align(32) a : u32 }\n";

bool typeRequiresF16(const std::string& testType) {
    return testType.find("f16") != std::string::npos;
}

std::string shaderHeader(const std::string& testType) {
    return typeRequiresF16(testType) ? "enable f16;\n\n" : "";
}

// Build combineWithParams records for kTestTypes (type + stride).
std::vector<ParamRecord> testTypeRecords() {
    std::vector<ParamRecord> records;
    for (const TestType& tt : kTestTypes()) {
        ParamRecord record;
        record.emplace_back("type", Value(std::string(tt.type)));
        record.emplace_back("stride", Value(static_cast<int64_t>(tt.stride)));
        records.push_back(std::move(record));
    }
    return records;
}

// Run a shader and check that the computed array length is correct.
void runShaderTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& wgsl,
    uint32_t stride,
    uint32_t offset,
    uint64_t bufferSize,
    uint64_t bindingSize,
    uint64_t bindingOffset) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout = nullptr;
    pipeDesc.compute.module = shaderModule;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = bufferSize;
    bufDesc.usage = WGPUBufferUsage_Storage;
    WGPUBuffer buffer = t.createBufferTracked(bufDesc);

    // Length output buffer, zero-initialized (WebGPU guarantees zero-init).
    WGPUBufferDescriptor lenDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    lenDesc.size = 4;
    lenDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer lengthBuffer = t.createBufferTracked(lenDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entries[2];
    entries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[0].binding = 0;
    entries[0].buffer = buffer;
    entries[0].offset = bindingOffset;
    entries[0].size = bindingSize;
    entries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    entries[1].binding = 1;
    entries[1].buffer = lengthBuffer;
    entries[1].offset = 0;
    entries[1].size = 4;
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = 2;
    bgDesc.entries = entries;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    const uint32_t length = static_cast<uint32_t>((bindingSize - offset) / stride);
    t.expectGPUBufferValuesEqual(lengthBuffer, &length, sizeof(length));
}

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,arrayLength",
    "Execution tests for the 'arrayLength' builtin function.");

CTS_TEST(testGroup, "single_element")
    .desc("Test arrayLength() with a binding just large enough for a single element.")
    .params([](ParamsBuilder u) { return u.combineWithParams(testTypeRecords()); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string type = t.param<std::string>("type");
        const uint32_t stride = static_cast<uint32_t>(t.param<int64_t>("stride"));
        if (typeRequiresF16(type) && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        const std::string wgsl =
            shaderHeader(type) + kWgslStructures +
            "\n"
            "      @group(0) @binding(0) var<storage, read_write> buffer : array<" + type +
            ">;\n"
            "      @group(0) @binding(1) var<storage, read_write> length : u32;\n"
            "      @compute @workgroup_size(1)\n"
            "      fn main() {\n"
            "        length = arrayLength(&buffer);\n"
            "      }\n";
        // Ensure that binding size is multiple of 4.
        const uint32_t bufferSize = alignUp(stride, 4);
        runShaderTest(t, wgsl, stride, 0, bufferSize, bufferSize, 0);
    });

CTS_TEST(testGroup, "multiple_elements")
    .desc("Test arrayLength() with a binding large enough for multiple elements.")
    .params([](ParamsBuilder u) {
        return u.combine("buffer_size", {Value(static_cast<int64_t>(640)),
                                         Value(static_cast<int64_t>(1004)),
                                         Value(static_cast<int64_t>(1048576))})
            .combineWithParams(testTypeRecords());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string type = t.param<std::string>("type");
        const uint32_t stride = static_cast<uint32_t>(t.param<int64_t>("stride"));
        const uint64_t bufferSize = static_cast<uint64_t>(t.param<int64_t>("buffer_size"));
        if (typeRequiresF16(type) && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        const std::string wgsl =
            shaderHeader(type) + kWgslStructures +
            "\n"
            "      @group(0) @binding(0) var<storage, read_write> buffer : array<" + type +
            ">;\n"
            "      @group(0) @binding(1) var<storage, read_write> length : u32;\n"
            "      @compute @workgroup_size(1)\n"
            "      fn main() {\n"
            "        length = arrayLength(&buffer);\n"
            "      }\n";
        runShaderTest(t, wgsl, stride, 0, bufferSize, bufferSize, 0);
    });

CTS_TEST(testGroup, "struct_member")
    .desc("Test arrayLength() with an array that is inside a structure.")
    .params([](ParamsBuilder u) {
        return u.combine("member_offset", {Value(static_cast<int64_t>(0)),
                                           Value(static_cast<int64_t>(4)),
                                           Value(static_cast<int64_t>(20))})
            .combineWithParams(testTypeRecords());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string type = t.param<std::string>("type");
        const uint32_t stride = static_cast<uint32_t>(t.param<int64_t>("stride"));
        const int64_t memberOffsetParam = t.param<int64_t>("member_offset");
        if (typeRequiresF16(type) && !wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        const uint32_t memberOffset = alignUp(static_cast<uint32_t>(memberOffsetParam), stride);
        const std::string paddingDecl =
            memberOffsetParam > 0
                ? ("@size(" + std::to_string(memberOffset) + ") padding : u32,")
                : "";
        const std::string wgsl =
            shaderHeader(type) + kWgslStructures +
            "\n"
            "      alias ArrayType = array<" + type +
            ">;\n"
            "      struct Struct {\n"
            "        " + paddingDecl +
            "\n"
            "        arr : ArrayType,\n"
            "      }\n"
            "      @group(0) @binding(0) var<storage, read_write> buffer : Struct;\n"
            "      @group(0) @binding(1) var<storage, read_write> length : u32;\n"
            "      @compute @workgroup_size(1)\n"
            "      fn main() {\n"
            "        length = arrayLength(&buffer.arr);\n"
            "      }\n";
        const uint64_t bufferSize = 1048576;
        runShaderTest(t, wgsl, stride, memberOffset, bufferSize, bufferSize, 0);
    });

CTS_TEST(testGroup, "binding_subregion")
    .desc("Test arrayLength() with a binding that starts at a non-zero offset and does not fill the "
          "entire buffer.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string wgsl =
            "\n"
            "      @group(0) @binding(0) var<storage, read_write> buffer : array<vec3<f32>>;\n"
            "      @group(0) @binding(1) var<storage, read_write> length : u32;\n"
            "      @compute @workgroup_size(1)\n"
            "      fn main() {\n"
            "        length = arrayLength(&buffer);\n"
            "      }\n";
        const uint32_t stride = 16;
        const uint64_t bufferSize = 1024;
        const uint64_t bindingSize = 640;
        const uint64_t bindingOffset = 256;
        runShaderTest(t, wgsl, stride, 0, bufferSize, bindingSize, bindingOffset);
    });

CTS_TEST(testGroup, "read_only")
    .desc("Test arrayLength() with a read-only storage buffer.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string wgsl =
            "\n"
            "      @group(0) @binding(0) var<storage, read> buffer : array<vec3<f32>>;\n"
            "      @group(0) @binding(1) var<storage, read_write> length : u32;\n"
            "      @compute @workgroup_size(1)\n"
            "      fn main() {\n"
            "        length = arrayLength(&buffer);\n"
            "      }\n";
        const uint32_t stride = 16;
        const uint64_t bufferSize = 1024;
        runShaderTest(t, wgsl, stride, 0, bufferSize, bufferSize, 0);
    });

} // namespace
