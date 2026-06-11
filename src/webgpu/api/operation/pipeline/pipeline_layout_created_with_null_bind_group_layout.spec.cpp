// Ported from gpuweb/cts src/webgpu/api/operation/pipeline/pipeline_layout_created_with_null_bind_group_layout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,pipeline,pipeline_layout_created_with_null_bind_group_layout",
    "Tests for the creation of pipeline layouts with null bind group layouts.");

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// emptyBindGroupLayoutType ['Null', 'Undefined', 'Empty'].
// 'Null' and 'Undefined' both become a null WGPUBindGroupLayout handle in the C
// API (there is no distinct JS `undefined` versus `null` at the C boundary);
// 'Empty' becomes a bind group layout created with no entries.
enum class EmptyType { Null, Undefined, Empty };

EmptyType parseEmptyType(std::string_view s) {
    if (s == "Null") { return EmptyType::Null; }
    if (s == "Undefined") { return EmptyType::Undefined; }
    if (s == "Empty") { return EmptyType::Empty; }
    std::abort();
}

WGPUBindGroupLayout createEmptyBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 0;
    desc.entries = nullptr;
    return t.createBindGroupLayoutTracked(desc);
}

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// -----------------------------------------------------------------------------
// rendering
// -----------------------------------------------------------------------------

CTS_TEST(g, "pipeline_layout_with_null_bind_group_layout,rendering")
    .desc(
        "Tests that using a render pipeline created with a pipeline layout that has null bind "
        "group layout works correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("emptyBindGroupLayoutType", {"Null", "Undefined", "Empty"})
            .combine("emptyBindGroupLayoutIndex", {0, 1, 2, 3});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const EmptyType emptyType = parseEmptyType(t.param<std::string>("emptyBindGroupLayoutType"));
        const int emptyIndex = t.param<int>("emptyBindGroupLayoutIndex");

        const std::array<std::array<float, 4>, 4> colors = {{
            {0.2f, 0.0f, 0.0f, 0.2f},
            {0.0f, 0.2f, 0.0f, 0.2f},
            {0.0f, 0.0f, 0.2f, 0.2f},
            {0.4f, 0.0f, 0.0f, 0.2f},
        }};
        std::array<float, 4> outputColor = {0.0f, 0.0f, 0.0f, 0.0f};

        std::string declarations;
        std::string statement = "return vec4(0.0, 0.0, 0.0, 0.0)";

        std::vector<WGPUBindGroupLayout> bindGroupLayouts;
        std::vector<WGPUBindGroup> bindGroups;

        for (int bindGroupIndex = 0; bindGroupIndex < 4; ++bindGroupIndex) {
            // The non-empty bind group layout: a single FRAGMENT-visible uniform buffer.
            WGPUBindGroupLayoutEntry bglEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            bglEntry.binding = 0;
            bglEntry.visibility = WGPUShaderStage_Fragment;
            bglEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            bglEntry.buffer.type = WGPUBufferBindingType_Uniform;
            bglEntry.buffer.minBindingSize = 16;

            WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            bglDesc.entryCount = 1;
            bglDesc.entries = &bglEntry;
            WGPUBindGroupLayout bindGroupLayout = t.createBindGroupLayoutTracked(bglDesc);

            const std::array<float, 4>& color = colors[bindGroupIndex];
            WGPUBuffer buffer = t.makeBufferWithContents(
                color.data(), color.size() * sizeof(float), WGPUBufferUsage_Uniform);

            // Still create and set the bind group when the corresponding bind group layout in the
            // pipeline is null. The output color should not be affected by the buffer in this bind
            // group.
            WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            bgEntry.binding = 0;
            bgEntry.buffer = buffer;
            bgEntry.offset = 0;
            bgEntry.size = color.size() * sizeof(float);

            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout = bindGroupLayout;
            bgDesc.entryCount = 1;
            bgDesc.entries = &bgEntry;
            bindGroups.push_back(t.createBindGroupTracked(bgDesc));

            // Set null, undefined or empty bind group layout in bindGroupLayouts which is used in
            // the creation of pipeline layout.
            if (bindGroupIndex == emptyIndex) {
                switch (emptyType) {
                    case EmptyType::Null:
                        bindGroupLayouts.push_back(nullptr);
                        break;
                    case EmptyType::Undefined:
                        bindGroupLayouts.push_back(nullptr);
                        break;
                    case EmptyType::Empty:
                        bindGroupLayouts.push_back(createEmptyBindGroupLayout(t));
                        break;
                }
                continue;
            }

            // Set the uniform buffers used in the shader.
            bindGroupLayouts.push_back(bindGroupLayout);
            {
                std::ostringstream decl;
                decl << "@group(" << bindGroupIndex << ") @binding(0) var<uniform> input"
                     << bindGroupIndex << " : vec4f;\n";
                declarations += decl.str();
            }
            statement += " + input" + std::to_string(bindGroupIndex);

            // Compute the expected output color.
            for (size_t i = 0; i < color.size(); ++i) {
                outputColor[i] += color[i];
            }
        }

        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = bindGroupLayouts.size();
        plDesc.bindGroupLayouts = bindGroupLayouts.data();
        WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(plDesc);

        const WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        std::string code =
            std::string(declarations) +
            "\n@vertex\n"
            "fn vert_main() -> @builtin(position) vec4f {\n"
            "    return vec4f(0.0, 0.0, 0.0, 1.0);\n"
            "}\n"
            "@fragment\n"
            "fn frag_main() -> @location(0) vec4f {\n"
            "    " + statement + ";\n"
            "}\n";
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = format;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = shaderModule;
        fragment.entryPoint = stringView("frag_main");
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;

        WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        rpDesc.layout = pipelineLayout;
        rpDesc.vertex.module = shaderModule;
        rpDesc.vertex.entryPoint = stringView("vert_main");
        rpDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        rpDesc.multisample.count = 1;
        rpDesc.fragment = &fragment;
        WGPURenderPipeline renderPipeline = t.createRenderPipelineTracked(rpDesc);

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        texDesc.size = WGPUExtent3D{1, 1, 1};
        texDesc.format = format;
        WGPUTexture renderTarget = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(renderTarget, viewDesc);

        WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Load; // texture is lazily zero-initialized.
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(commandEncoder, &passDesc);
        for (size_t i = 0; i < bindGroups.size(); ++i) {
            wgpuRenderPassEncoderSetBindGroup(pass, static_cast<uint32_t>(i), bindGroups[i], 0, nullptr);
        }
        wgpuRenderPassEncoderSetPipeline(pass, renderPipeline);
        wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBuffer commandBuffer = t.finishTracked(commandEncoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // Read back the single rgba8unorm pixel and compare against the expected color.
        const uint32_t bytesPerRow =
            static_cast<uint32_t>(alignTo(4, kBytesPerRowAlignment));
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = bytesPerRow;
        bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readback = t.createBufferTracked(bufDesc);

        WGPUCommandEncoder copyEncoder = t.createCommandEncoderTracked();
        t.copyTextureToBuffer(copyEncoder, renderTarget, readback, bytesPerRow, WGPUExtent3D{1, 1, 1});
        WGPUCommandBuffer copyBuffer = t.finishTracked(copyEncoder);
        wgpuQueueSubmit(t.queue(), 1, &copyBuffer);

        // Expected rgba8unorm bytes from the float output color.
        std::array<uint8_t, 4> expected{};
        for (size_t i = 0; i < 4; ++i) {
            float v = outputColor[i];
            if (v < 0.0f) { v = 0.0f; }
            if (v > 1.0f) { v = 1.0f; }
            expected[i] = static_cast<uint8_t>(std::lround(v * 255.0f));
        }

        t.expectGPUBufferValuesPassCheck(
            readback,
            [expected](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < 4) {
                    return std::string("readback too small for rgba8unorm pixel");
                }
                // Allow a tolerance of 1 ULP for unorm quantization differences.
                for (size_t ch = 0; ch < 4; ++ch) {
                    const int got = static_cast<int>(actual[ch]);
                    const int exp = static_cast<int>(expected[ch]);
                    if (std::abs(got - exp) > 1) {
                        std::ostringstream msg;
                        msg << "rgba8unorm mismatch channel " << ch << ": expected " << exp
                            << ", got " << got;
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0,
            4);
    });

// -----------------------------------------------------------------------------
// compute
// -----------------------------------------------------------------------------

CTS_TEST(g, "pipeline_layout_with_null_bind_group_layout,compute")
    .desc(
        "Tests that using a compute pipeline created with a pipeline layout that has null bind "
        "group layout works correctly.")
    .params([](ParamsBuilder u) {
        return u.combine("emptyBindGroupLayoutType", {"Null", "Undefined", "Empty"})
            .combine("emptyBindGroupLayoutIndex", {0, 1, 2, 3});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const EmptyType emptyType = parseEmptyType(t.param<std::string>("emptyBindGroupLayoutType"));
        const int emptyIndex = t.param<int>("emptyBindGroupLayoutIndex");

        std::string declarations;
        std::string statement = "output = 0u ";

        WGPUBufferDescriptor outDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        outDesc.size = 4;
        outDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage;
        WGPUBuffer outputBuffer = t.createBufferTracked(outDesc);

        uint32_t expectedValue = 0;

        std::vector<WGPUBindGroupLayout> bindGroupLayouts;
        std::vector<WGPUBindGroup> bindGroups;
        bool outputDeclared = false;

        for (int bindGroupIndex = 0; bindGroupIndex < 4; ++bindGroupIndex) {
            const uint32_t inputValue = static_cast<uint32_t>(bindGroupIndex + 1);
            WGPUBuffer inputBuffer = t.makeBufferWithContents(
                &inputValue, sizeof(inputValue), WGPUBufferUsage_Uniform);

            std::vector<WGPUBindGroupLayoutEntry> bglEntries;
            std::vector<WGPUBindGroupEntry> bgEntries;

            WGPUBindGroupLayoutEntry uniformEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            uniformEntry.binding = 0;
            uniformEntry.visibility = WGPUShaderStage_Compute;
            uniformEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            uniformEntry.buffer.type = WGPUBufferBindingType_Uniform;
            uniformEntry.buffer.minBindingSize = 4;
            bglEntries.push_back(uniformEntry);

            WGPUBindGroupEntry uniformBgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            uniformBgEntry.binding = 0;
            uniformBgEntry.buffer = inputBuffer;
            uniformBgEntry.offset = 0;
            uniformBgEntry.size = sizeof(inputValue);
            bgEntries.push_back(uniformBgEntry);

            // Set null, undefined or empty bind group layout in bindGroupLayouts which is used in
            // the creation of pipeline layout.
            if (bindGroupIndex == emptyIndex) {
                switch (emptyType) {
                    case EmptyType::Null:
                        bindGroupLayouts.push_back(nullptr);
                        break;
                    case EmptyType::Undefined:
                        bindGroupLayouts.push_back(nullptr);
                        break;
                    case EmptyType::Empty:
                        bindGroupLayouts.push_back(createEmptyBindGroupLayout(t));
                        break;
                }

                // Still create and set the bind group when the corresponding bind group layout in
                // the compute pipeline is null. The value in the output buffer should not be
                // affected by the buffer in this bind group.
                WGPUBindGroupLayoutDescriptor nullCaseBglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
                nullCaseBglDesc.entryCount = bglEntries.size();
                nullCaseBglDesc.entries = bglEntries.data();
                WGPUBindGroupLayout nullCaseBgl = t.createBindGroupLayoutTracked(nullCaseBglDesc);

                WGPUBindGroupDescriptor nullCaseBgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
                nullCaseBgDesc.layout = nullCaseBgl;
                nullCaseBgDesc.entryCount = bgEntries.size();
                nullCaseBgDesc.entries = bgEntries.data();
                bindGroups.push_back(t.createBindGroupTracked(nullCaseBgDesc));
                continue;
            }

            {
                std::ostringstream decl;
                decl << "@group(" << bindGroupIndex << ") @binding(0) var<uniform> input"
                     << bindGroupIndex << " : u32;\n";
                declarations += decl.str();
            }
            statement += " + input" + std::to_string(bindGroupIndex);

            // Set the output storage buffer.
            if (!outputDeclared) {
                WGPUBindGroupLayoutEntry storageEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                storageEntry.binding = 1;
                storageEntry.visibility = WGPUShaderStage_Compute;
                storageEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
                storageEntry.buffer.type = WGPUBufferBindingType_Storage;
                storageEntry.buffer.minBindingSize = 4;
                bglEntries.push_back(storageEntry);

                WGPUBindGroupEntry storageBgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
                storageBgEntry.binding = 1;
                storageBgEntry.buffer = outputBuffer;
                storageBgEntry.offset = 0;
                storageBgEntry.size = 4;
                bgEntries.push_back(storageBgEntry);

                {
                    std::ostringstream decl;
                    decl << "@group(" << bindGroupIndex
                         << ") @binding(1) var<storage, read_write> output : u32;\n";
                    declarations += decl.str();
                }
                outputDeclared = true;
            }

            // Set the input uniform buffers.
            WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            bglDesc.entryCount = bglEntries.size();
            bglDesc.entries = bglEntries.data();
            WGPUBindGroupLayout bindGroupLayout = t.createBindGroupLayoutTracked(bglDesc);
            bindGroupLayouts.push_back(bindGroupLayout);

            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout = bindGroupLayout;
            bgDesc.entryCount = bgEntries.size();
            bgDesc.entries = bgEntries.data();
            bindGroups.push_back(t.createBindGroupTracked(bgDesc));

            // Compute the expected output value in the output storage buffer.
            expectedValue += static_cast<uint32_t>(bindGroupIndex + 1);
        }

        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = bindGroupLayouts.size();
        plDesc.bindGroupLayouts = bindGroupLayouts.data();
        WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(plDesc);

        std::string code =
            std::string(declarations) +
            "\n@compute @workgroup_size(1)\n"
            "fn main() {\n"
            "  " + statement + ";\n"
            "}\n";
        WGPUShaderModule module = t.createShaderModuleTracked(code);

        WGPUComputePipelineDescriptor cpDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        cpDesc.layout = pipelineLayout;
        cpDesc.compute.module = module;
        cpDesc.compute.entryPoint = stringView("main");
        WGPUComputePipeline computePipeline = t.createComputePipelineTracked(cpDesc);

        WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(commandEncoder, &passDesc);
        for (size_t i = 0; i < bindGroups.size(); ++i) {
            wgpuComputePassEncoderSetBindGroup(pass, static_cast<uint32_t>(i), bindGroups[i], 0, nullptr);
        }
        wgpuComputePassEncoderSetPipeline(pass, computePipeline);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);

        WGPUCommandBuffer commandBuffer = t.finishTracked(commandEncoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        t.expectGPUBufferValuesEqual(outputBuffer, &expectedValue, sizeof(expectedValue));
    });

} // namespace
