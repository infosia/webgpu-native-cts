// Ported from gpuweb/cts src/webgpu/shader/execution/memory_model/adjacent.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/shader/execution/memory_model/memory_model_setup.h"

using namespace cts;

namespace {

// Algorithm: with N invocations, N is even:
//     srcBuffer: An array of random scalar values. Avoids unsupported values like infinity and NaN.
//     resultBuffer: A result array
//     pattern: 0|1|2|3
//       Pattern 0: Identity: invocation i: dst[i] = src[i]
//       Pattern 1: Try to prevent write coalescing.
//          Even elements stay in place.
//          Reverse order of odd elements.
//          invocation 2k:   dst[2k] = src[2k]
//          invocation 2k+1: dst[2k+1] = src[N - (2k+1)]
//       Pattern 2: Try to prevent write coalescing.
//          Reverse order of even elements.
//          Odd elements stay in place.
//          invocation 2k:   dst[2k] = src[N - 2 - 2k]
//          invocation 2k+1: dst[2k+1] = src[2k+1]
//       Pattern 3: Reverse elements: dst[i] = src[N-1-i]
//     addressSpace: workgroup|storage
//          Where dst is allocated.

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,memory_model,adjacent",
    "Tests writes from different invocations to adjacent scalars do not interfere.\n"
    "This is especially interesting when the scalar type is narrower than 32-bits.");

// For simplicity, make the entire source (and destination) array fit
// in workgroup memory.
// We can count on up to 16384 bytes in workgroup memory.
static constexpr uint32_t kNumValues = 4096; // Assumed even
static constexpr uint32_t kWorkgroupSize = 128; // Use 1-dimensional workgroups.

/**
 * Returns an integer for the bit pattern of a random finite f16 value.
 * Consumes values from prng.
 *
 * A finite f16 value is one where the 5-bit exponent field is not all 1s.
 * The exponent bits are 0x7c00.
 */
static uint16_t randomFiniteF16(memory_model::PRNG& prng) {
    static constexpr uint16_t kExponentBits = 0x7c00u;
    // With any reasonable random number stream, the average number
    // of trips around this loop is < 1 + 1/32 because there are 5 exponent bits.
    uint16_t candidate = 0;
    do {
        candidate = static_cast<uint16_t>(prng.randomU32() & 0xffffu);
        // Non-finite f16 values have all 1 bits in the exponent.
    } while ((candidate & kExponentBits) == kExponentBits);
    return candidate;
}

/**
 * Fills array arr (length kNumValues) with random finite f16 values.
 * Consumes values from prng.
 */
static void fillWithRandomFiniteF16(memory_model::PRNG& prng, std::vector<uint16_t>& arr) {
    for (size_t i = 0; i < arr.size(); i++) {
        arr[i] = randomFiniteF16(prng);
    }
}

/**
 * Returns the WGSL destination index expression based on pattern.
 *
 * i must be the WGSL string for the source index.
 * pattern is the indexing pattern (0..3).
 */
static std::string getDstIndexExpression(const std::string& i, int pattern) {
    switch (pattern) {
        case 0:
            return i;
        case 1:
            // Even elements map to themselves.
            // Odd elements map to the reversed order of odd elements.
            return "select(" + std::to_string(kNumValues) + " - " + i + ", " +
                   i + ", (" + i + " & 1) == 0)";
        case 2:
            // Even elements map to the reversed order of even elements.
            // Since N is even, element 0 should get index N-2. (!)
            // Odd elements map to themselves.
            return "select(" + i + ", " + std::to_string(kNumValues) + " - 2 - " + i +
                   ", (" + i + " & 1) == 0)";
        case 3:
            return std::to_string(kNumValues) + " - 1 -" + i;
        default:
            return i;
    }
}

/**
 * Computes the reference (correct) result for the given source array and indexing pattern.
 *
 * pattern: the indexing pattern (0..3)
 * src: the source array (kNumValues elements)
 * dst: the array to fill with values transferred from src (kNumValues elements)
 */
static void computeReference(int pattern, const std::vector<uint16_t>& src, std::vector<uint16_t>& dst) {
    const uint32_t n = static_cast<uint32_t>(src.size());
    for (uint32_t i = 0; i < n; i++) {
        const bool isEven = (i & 1u) == 0u;
        switch (pattern) {
            case 0:
                dst[i] = src[i];
                break;
            case 1:
                if (isEven) {
                    dst[i] = src[i];
                } else {
                    dst[n - i] = src[i];
                }
                break;
            case 2:
                if (isEven) {
                    dst[kNumValues - 2u - i] = src[i];
                } else {
                    dst[i] = src[i];
                }
                break;
            case 3:
                dst[n - 1u - i] = src[i];
                break;
            default:
                dst[i] = src[i];
                break;
        }
    }
}

/**
 * Returns the source text for a shader that copies elements from a source
 * buffer to a destination buffer, while remapping indices according to the
 * specified pattern.
 *
 * addressSpace: "workgroup" or "storage"
 * pattern: 0..3
 */
static std::string makeShaderText(const std::string& addressSpace, int pattern) {
    // When the destination buffer is in 'storage', then write directly to it.
    // Otherwise, destination is in workgroup memory, and we need to name the
    // output buffer differently.
    const std::string dstBuf = (addressSpace == "storage") ? "dst" : "dstBuf";

    std::string shader;

    shader +=
        "enable f16;\n"
        "@group(0) @binding(0) var<storage> src: array<f16>;\n"
        "@group(0) @binding(1) var<storage,read_write> " + dstBuf + ": array<f16>;\n";

    if (addressSpace == "workgroup") {
        shader += "var<workgroup> dst: array<f16," + std::to_string(kNumValues) + ">;\n";
    }

    const std::string dstIndexExpr = getDstIndexExpression("srcIndex", pattern);
    shader +=
        "@compute @workgroup_size(" + std::to_string(kWorkgroupSize) + ")\n"
        "fn adjacent_writes(@builtin(global_invocation_id) gid: vec3u) {\n"
        "    let srcIndex = gid.x;\n"
        "    let dstIndex = " + dstIndexExpr + ";\n"
        "    dst[dstIndex] = src[srcIndex];\n";

    if (addressSpace == "workgroup") {
        // Copy to the output buffer.
        // The barrier is not necessary here, but it should prevent
        // the compiler from being clever and optimizing away the
        // intermediate write to workgroup memory.
        shader +=
            "    workgroupBarrier();\n"
            "    " + dstBuf + "[dstIndex] = dst[dstIndex];\n";
    }

    shader += "}\n";

    return shader;
}

/**
 * Runs the test on the GPU, generating random source data and
 * checking the results against the expected permutation of that data.
 */
static void runTest(AllFeaturesMaxLimitsGpuTest& t, const std::string& addressSpace, int pattern) {
    const uint32_t seed = (static_cast<uint32_t>(pattern) + 1u) * static_cast<uint32_t>(addressSpace.length());
    memory_model::PRNG prng(seed);

    static constexpr uint32_t kBytesPerScalar = 2; // f16 is 2 bytes wide.
    static constexpr uint64_t kBufByteSize = static_cast<uint64_t>(kNumValues) * kBytesPerScalar;

    // Allocate source and expected on host.
    std::vector<uint16_t> hostSrc(kNumValues, 0u);
    std::vector<uint16_t> expected(kNumValues, 0u);

    // Fill host source with random finite f16 values.
    fillWithRandomFiniteF16(prng, hostSrc);
    // Compute the expected (reference) result on the CPU.
    computeReference(pattern, hostSrc, expected);

    // Create GPU source buffer: STORAGE (read-only in shader), COPY_DST.
    WGPUBufferDescriptor srcDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    srcDesc.size = kBufByteSize;
    srcDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
    WGPUBuffer srcBuf = t.createBufferTracked(srcDesc);

    // Create GPU destination buffer: STORAGE (read_write in shader), COPY_SRC.
    WGPUBufferDescriptor dstDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    dstDesc.size = kBufByteSize;
    dstDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
    WGPUBuffer dstBuf = t.createBufferTracked(dstDesc);

    // Upload source data.
    t.queueWriteBuffer(srcBuf, 0, hostSrc.data(), hostSrc.size() * sizeof(uint16_t));

    const std::string shaderText = makeShaderText(addressSpace, pattern);
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(shaderText);

    constexpr std::string_view kEntryPointSV = "adjacent_writes";
    WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr; // auto layout
    pipelineDesc.compute.module = shaderModule;
    pipelineDesc.compute.entryPoint = WGPUStringView{kEntryPointSV.data(), kEntryPointSV.size()};
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

    // Get auto-layout bind group layout and release after use.
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    std::array<WGPUBindGroupEntry, 2> bindGroupEntries;
    bindGroupEntries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
    bindGroupEntries[0].binding = 0;
    bindGroupEntries[0].buffer = srcBuf;
    bindGroupEntries[0].offset = 0;
    bindGroupEntries[0].size = kBufByteSize;

    bindGroupEntries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
    bindGroupEntries[1].binding = 1;
    bindGroupEntries[1].buffer = dstBuf;
    bindGroupEntries[1].offset = 0;
    bindGroupEntries[1].size = kBufByteSize;

    WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bindGroupDesc.layout = bgl;
    bindGroupDesc.entryCount = bindGroupEntries.size();
    bindGroupDesc.entries = bindGroupEntries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, kNumValues / kWorkgroupSize, 1, 1);
    wgpuComputePassEncoderEnd(pass);

    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    t.expectGPUBufferValuesEqual(dstBuf, expected.data(), expected.size() * sizeof(uint16_t));
}

CTS_TEST(g, "f16")
    .desc("Check that writes by different invocations to adjacent f16 values in an array do not "
          "interfere with each other.")
    .params([](ParamsBuilder u) {
        return u
            .combine("addressSpace", {"workgroup", "storage"})
            .combine("pattern", {0, 1, 2, 3});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }
        const std::string addressSpace = t.param<std::string>("addressSpace");
        const int pattern = t.param<int>("pattern");
        runTest(t, addressSpace, pattern);
    });

} // namespace
