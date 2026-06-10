// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/buffers/createBindGroup.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 Kota Iguchi, BSD-3-Clause.
//
// Notes:
//   - The upstream fixture is AllFeaturesMaxLimitsGPUTest; we use AllFeaturesMaxLimitsGpuTest.
//   - The upstream uses .paramsSubcasesOnly(...) which maps to .params([](ParamsBuilder u){
//       return u.beginSubcases()...}).
//   - Upstream offset/size params include `undefined`; we represent those with Value::undef()
//     and check paramIsUndefined() in the test body. The semantics are:
//       offset undefined → effective offset 0 when binding the whole-buffer resource
//       size   undefined → effective size  = whole buffer extent relative to offset
//   - When bindBufferResource=true the buffer itself is the resource (offset/size are always
//     undefined due to the filter). When false, we pass an explicit WGPUBindGroupEntry with
//     the given offset and size (using WGPU_WHOLE_SIZE when size is undefined).
//   - The bind-group entry (WGPUBindGroupEntry) is declared before the descriptor that
//     consumes it so its lifetime covers the createBindGroupTracked call (lifetime rule).
//   - wgpuComputePipelineGetBindGroupLayout is a getter → must call wgpuBindGroupLayoutRelease
//     after use (harness does not track it).

#include <cstdint>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,buffers,createBindGroup",
    "\nBuffer tests in createBindGroup.\n");

// ---------------------------------------------------------------------------
// The compute shader clears all elements of the bound storage buffer to 0.
// arrayLength(&buffer) reflects the binding size so only the bound range is
// cleared, matching upstream behaviour.
// ---------------------------------------------------------------------------
constexpr std::string_view kClearShader = R"(
@group(0) @binding(0) var<storage, read_write> buffer : array<u32>;

@compute @workgroup_size(1) fn main() {
  for (var i = 0u; i < arrayLength(&buffer); i = i + 1u) {
    buffer[i] = 0;
  }
  return;
}
)";

// ---------------------------------------------------------------------------
// test: buffer_binding_resource
// ---------------------------------------------------------------------------
CTS_TEST(g, "buffer_binding_resource")
    .desc(
        "Validate the correctness of the buffer binding resource by filling the buffer with\n"
        "    testable data, clearing buffer in shader, and verifying the content of the whole buffer:\n"
        "  - covers the whole buffer\n"
        "  - covers the beginning of the buffer\n"
        "  - covers the end of the buffer\n"
        "  - covers neither the beginning nor the end of the buffer")
    .params([](ParamsBuilder u) {
        // Upstream uses .paramsSubcasesOnly → all params become subcases.
        return u.beginSubcases()
            .combine("bindBufferResource", {false, true})
            .combine("offset", {Value(0), Value(256), Value::undef()})
            .combine("size", {Value(4), Value(8), Value::undef()})
            .combine("extraBufferSize", {Value(0), Value(8)})
            // offset and size don't matter if bindBufferResource is true;
            // keep only the (undefined, undefined) combination for that branch.
            .filter([](const ParamRecord& p) {
                const Value* bbr = findParam(p, "bindBufferResource");
                if (bbr == nullptr) return true;
                bool bindBuffer = valueAs<bool>(*bbr);
                if (!bindBuffer) return true;
                // bindBufferResource=true: require both offset and size to be undefined.
                const Value* off = findParam(p, "offset");
                const Value* sz  = findParam(p, "size");
                bool offUndef = (off == nullptr || std::holds_alternative<Value::Undefined>(off->data()));
                bool szUndef  = (sz  == nullptr || std::holds_alternative<Value::Undefined>(sz->data()));
                return offUndef && szUndef;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool bindBufferResource = t.param<bool>("bindBufferResource");

        // Resolve offset: undefined → 0
        const bool offsetUndef = t.paramIsUndefined("offset");
        const uint64_t offset  = offsetUndef ? 0u : static_cast<uint64_t>(t.param<int>("offset"));

        // Resolve size: undefined → use 16 (4 u32s, the upstream default for unspecified size)
        const bool sizeUndef        = t.paramIsUndefined("size");
        const uint64_t sizeOrDefault = sizeUndef ? 16u : static_cast<uint64_t>(t.param<int>("size"));

        const uint64_t extraBufferSize = static_cast<uint64_t>(t.param<int>("extraBufferSize"));

        // bufferSize = (offset ?? 0) + (size ?? 16) + extraBufferSize
        const uint64_t bufferSize = offset + sizeOrDefault + extraBufferSize;

        // Fill buffer with bytes: bufferData[i] = i + 1  (zero-fill rule: output buffer
        // is NOT pre-filled with expected; initial data is intentionally non-zero).
        std::vector<uint8_t> bufferData(static_cast<size_t>(bufferSize));
        for (size_t i = 0; i < bufferData.size(); ++i) {
            bufferData[i] = static_cast<uint8_t>(i + 1);
        }

        // Upload initial data into a STORAGE|COPY_SRC|COPY_DST buffer.
        WGPUBuffer buffer = t.makeBufferWithContents(
            bufferData.data(),
            bufferData.size(),
            WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage);

        // Create compute pipeline with auto layout.
        // kEntryPointSV points at a string literal (static duration), safe to embed in desc.
        constexpr std::string_view kEntryPointSV = "main";
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(kClearShader);
        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout             = nullptr; // auto layout
        pipeDesc.compute.module     = shaderModule;
        pipeDesc.compute.entryPoint = WGPUStringView{kEntryPointSV.data(), kEntryPointSV.size()};
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

        // Obtain bind group layout from pipeline (getter — must release manually).
        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        // Declare the bind-group entry before the descriptor so it outlives
        // createBindGroupTracked (lifetime rule).
        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding = 0;
        bgEntry.buffer  = buffer;

        if (bindBufferResource) {
            // Bind the whole buffer: offset=0, size=WGPU_WHOLE_SIZE.
            bgEntry.offset = 0;
            bgEntry.size   = WGPU_WHOLE_SIZE;
        } else {
            // Bind specified sub-range.
            bgEntry.offset = offset;
            // If size was undefined, bind the remainder of the buffer from offset.
            bgEntry.size   = sizeUndef ? WGPU_WHOLE_SIZE : sizeOrDefault;
        }

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &bgEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        // Release the getter result now that we no longer need it.
        wgpuBindGroupLayoutRelease(bgl);

        // Record and submit the compute dispatch.
        {
            WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &cpDesc);
            wgpuComputePassEncoderSetPipeline(pass, pipeline);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
        }

        // Compute the expected cleared region.
        //   expectOffset = bindBufferResource ? 0 : offset ?? 0
        //   expectSize   = bindBufferResource ? bufferSize
        //                                     : size ?? (bufferSize - expectOffset)
        const uint64_t expectOffset = bindBufferResource ? 0u : offset;
        const uint64_t expectSize   = bindBufferResource
            ? bufferSize
            : (sizeUndef ? bufferSize - expectOffset : sizeOrDefault);

        // Apply the expected clear into our bufferData reference copy, mirroring upstream.
        for (uint64_t i = 0; i < expectSize; ++i) {
            bufferData[static_cast<size_t>(expectOffset + i)] = 0;
        }

        // Verify: the harness maps the buffer and compares bytes.
        t.expectGPUBufferValuesEqual(buffer, bufferData.data(), bufferData.size());
    });

} // namespace
