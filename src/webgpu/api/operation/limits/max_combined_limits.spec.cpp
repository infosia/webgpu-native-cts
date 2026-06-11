// Ported from gpuweb/cts src/webgpu/api/operation/limits/max_combined_limits.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 The webgpu-native-cts Authors, BSD-3-Clause.

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,limits,max_combined_limits",
    "Test that with the limits set to their maximum we can actually use "
    "the maximum number of storage buffers, storage textures, and fragment outputs "
    "at the same time.\n\n"
    "In particular, OpenGL ES 3.1 has GL_MAX_COMBINED_SHADER_OUTPUT_RESOURCES which "
    "the spec says is the combination of storage textures, storage buffers, and "
    "fragment shader outputs. This test checks that the whatever values the WebGPU "
    "implementation allows, all of them are useable.");

WGPUStringView stringView(const std::string& value) {
    return WGPUStringView{value.data(), value.size()};
}

// Mirrors getColorRenderByteCost() from format_info.ts for the formats used here.
uint32_t getColorRenderByteCost(WGPUTextureFormat format) {
    switch (format) {
        case WGPUTextureFormat_R8Uint:
            return 1;
        case WGPUTextureFormat_RGBA8Uint:
            return 4;
        case WGPUTextureFormat_RGBA32Uint:
            return 16;
        default:
            std::abort();
    }
}

WGPUTextureFormat parseFormat(const std::string& format) {
    if (format == "r8uint") {
        return WGPUTextureFormat_R8Uint;
    }
    if (format == "rgba8uint") {
        return WGPUTextureFormat_RGBA8Uint;
    }
    if (format == "rgba32uint") {
        return WGPUTextureFormat_RGBA32Uint;
    }
    std::abort();
}

constexpr uint32_t kWidth = 4;
constexpr uint32_t kHeight = 4;

// fillExpected() from upstream: per-pixel (x+i, y+i, i, 1+i) for the four channels.
std::array<uint32_t, kWidth * kHeight * 4> makeExpectedRGBA32Uint(uint32_t i) {
    std::array<uint32_t, kWidth * kHeight * 4> expected{};
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            const uint32_t off = (y * kWidth + x) * 4;
            expected[off + 0] = x + i;
            expected[off + 1] = y + i;
            expected[off + 2] = i;
            expected[off + 3] = 1 + i;
        }
    }
    return expected;
}

std::array<uint8_t, kWidth * kHeight * 4> makeExpectedRGBA8Uint(uint32_t i) {
    std::array<uint8_t, kWidth * kHeight * 4> expected{};
    for (uint32_t y = 0; y < kHeight; ++y) {
        for (uint32_t x = 0; x < kWidth; ++x) {
            const uint32_t off = (y * kWidth + x) * 4;
            expected[off + 0] = static_cast<uint8_t>(x + i);
            expected[off + 1] = static_cast<uint8_t>(y + i);
            expected[off + 2] = static_cast<uint8_t>(i);
            expected[off + 3] = static_cast<uint8_t>(1 + i);
        }
    }
    return expected;
}

std::array<uint8_t, kWidth * kHeight> makeExpectedR8Uint(uint32_t i) {
    const auto temp = makeExpectedRGBA8Uint(i);
    std::array<uint8_t, kWidth * kHeight> expected{};
    for (uint32_t j = 0; j < expected.size(); ++j) {
        expected[j] = temp[j * 4];
    }
    return expected;
}

// Build a row-padded (bytesPerRow == 256) expected buffer for the texture readback
// comparison, mirroring upstream's TexelView.fromTextureDataByReference exact match.
std::vector<uint8_t> padRows(const uint8_t* tight, uint32_t bytesPerRowTight) {
    std::vector<uint8_t> padded(static_cast<size_t>(kBytesPerRowAlignment) * kHeight, 0);
    for (uint32_t y = 0; y < kHeight; ++y) {
        std::memcpy(
            padded.data() + static_cast<size_t>(y) * kBytesPerRowAlignment,
            tight + static_cast<size_t>(y) * bytesPerRowTight,
            bytesPerRowTight);
    }
    return padded;
}

CTS_TEST(g, "max_storage_buffer_texture_frag_outputs")
    .desc("Use the maximum number of storage buffer, storage texture, and fragment stage outputs")
    .params([](ParamsBuilder u) {
        return u.combine("format", {std::string("r8uint"), std::string("rgba8uint"), std::string("rgba32uint")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string formatName = t.param<std::string>("format");
        const WGPUTextureFormat format = parseFormat(formatName);

        const WGPULimits limits = t.getLimits();
        const WGPUCompatibilityModeLimits compatibilityLimits = t.getCompatibilityModeLimits();

        const uint32_t numColorAttachments = std::min(
            limits.maxColorAttachments,
            static_cast<uint32_t>(limits.maxColorAttachmentBytesPerSample / getColorRenderByteCost(format)));

        // The per-fragment-stage storage fields live in the compatibility-mode limits
        // chained struct (the JS `?? maxStorageBuffersPerShaderStage` fallback covers
        // limit sets where they are not available, signalled by WGPU_LIMIT_U32_UNDEFINED).
        uint32_t numStorageBuffers = compatibilityLimits.maxStorageBuffersInFragmentStage;
        if (numStorageBuffers == WGPU_LIMIT_U32_UNDEFINED) {
            numStorageBuffers = limits.maxStorageBuffersPerShaderStage;
        }
        uint32_t numStorageTextures = compatibilityLimits.maxStorageTexturesInFragmentStage;
        if (numStorageTextures == WGPU_LIMIT_U32_UNDEFINED) {
            numStorageTextures = limits.maxStorageTexturesPerShaderStage;
        }

        // Build the shader source dynamically (matches upstream string interpolation).
        std::string code;
        for (uint32_t i = 0; i < numStorageBuffers; ++i) {
            code += "@group(0) @binding(" + std::to_string(i) +
                    ") var<storage, read_write> sb" + std::to_string(i) + ": array<vec4u>;\n";
        }
        code += "\n";
        for (uint32_t i = 0; i < numStorageTextures; ++i) {
            code += "@group(1) @binding(" + std::to_string(i) +
                    ") var st" + std::to_string(i) + ": texture_storage_2d<rgba32uint, write>;\n";
        }
        code += "\nstruct FragOut {\n";
        for (uint32_t i = 0; i < numColorAttachments; ++i) {
            code += "  @location(" + std::to_string(i) + ") f" + std::to_string(i) + ": vec4u,\n";
        }
        code += "};\n\n";
        code += "@vertex fn vs(@builtin(vertex_index) vNdx: u32) -> @builtin(position) vec4f {\n";
        code += "  let pos = array(\n";
        code += "    vec2f(-1,  3),\n";
        code += "    vec2f( 3, -1),\n";
        code += "    vec2f(-1, -1),\n";
        code += "  );\n";
        code += "  return vec4f(pos[vNdx], 0, 1);\n";
        code += "}\n\n";
        code += "@fragment fn fs(@builtin(position) position: vec4f) -> FragOut {\n";
        code += "  let p = vec4u(position);\n";
        code += "  let ndx = p.y * " + std::to_string(kWidth) + " + p.x;\n\n";
        for (uint32_t i = 0; i < numStorageBuffers; ++i) {
            code += "  sb" + std::to_string(i) + "[ndx] = p + " + std::to_string(i) + ";\n";
        }
        code += "\n";
        for (uint32_t i = 0; i < numStorageTextures; ++i) {
            code += "  textureStore(st" + std::to_string(i) + ", p.xy, p + " + std::to_string(i) + " * 2);\n";
        }
        code += "\n  var fragOut: FragOut;\n";
        for (uint32_t i = 0; i < numColorAttachments; ++i) {
            code += "  fragOut.f" + std::to_string(i) + " = vec4u(p + " + std::to_string(i) + " * 3);\n";
        }
        code += "  return fragOut;\n";
        code += "}\n";

        WGPUShaderModule module = t.createShaderModuleTracked(code);

        // Render pipeline (auto layout), fragment writes to numColorAttachments targets,
        // depth-stencil attachment is just extra output (its contents are not checked).
        std::vector<WGPUColorTargetState> targets(numColorAttachments);
        for (uint32_t i = 0; i < numColorAttachments; ++i) {
            targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
            targets[i].format = format;
            targets[i].writeMask = WGPUColorWriteMask_All;
        }

        const std::string fsEntry = "fs";
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module;
        fragment.entryPoint = stringView(fsEntry);
        fragment.targetCount = numColorAttachments;
        fragment.targets = targets.data();

        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
        depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
        depthStencil.depthCompare = WGPUCompareFunction_Less;

        const std::string vsEntry = "vs";
        WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = nullptr;  // 'auto'
        pipeDesc.vertex.module = module;
        pipeDesc.vertex.entryPoint = stringView(vsEntry);
        pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeDesc.multisample.count = 1;
        pipeDesc.fragment = &fragment;
        pipeDesc.depthStencil = &depthStencil;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

        const uint32_t kSize = kWidth * kHeight * 4 * 4;  // bytes for an rgba32uint storage buffer

        std::vector<WGPUBuffer> storageBuffers(numStorageBuffers);
        for (uint32_t i = 0; i < numStorageBuffers; ++i) {
            WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            bufDesc.size = kSize;
            bufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
            storageBuffers[i] = t.createBufferTracked(bufDesc);
        }

        std::vector<WGPUTexture> storageTextures(numStorageTextures);
        for (uint32_t i = 0; i < numStorageTextures; ++i) {
            WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            texDesc.format = WGPUTextureFormat_RGBA32Uint;
            texDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
            texDesc.usage = WGPUTextureUsage_StorageBinding | WGPUTextureUsage_CopySrc;
            storageTextures[i] = t.createTextureTracked(texDesc);
        }

        std::vector<WGPUTexture> colorTargets(numColorAttachments);
        for (uint32_t i = 0; i < numColorAttachments; ++i) {
            WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            texDesc.format = format;
            texDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
            texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
            colorTargets[i] = t.createTextureTracked(texDesc);
        }

        // Bind group 0: storage buffers.
        std::vector<WGPUBindGroupEntry> bg0Entries(numStorageBuffers);
        for (uint32_t i = 0; i < numStorageBuffers; ++i) {
            bg0Entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            bg0Entries[i].binding = i;
            bg0Entries[i].buffer = storageBuffers[i];
            bg0Entries[i].offset = 0;
            bg0Entries[i].size = kSize;
        }
        WGPUBindGroupDescriptor bg0Desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bg0Desc.layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        bg0Desc.entryCount = bg0Entries.size();
        bg0Desc.entries = bg0Entries.data();
        WGPUBindGroup bindGroup0 = t.createBindGroupTracked(bg0Desc);

        // Bind group 1: storage texture views.
        WGPUTextureViewDescriptor stViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        std::vector<WGPUTextureView> stViews(numStorageTextures);
        std::vector<WGPUBindGroupEntry> bg1Entries(numStorageTextures);
        for (uint32_t i = 0; i < numStorageTextures; ++i) {
            stViews[i] = t.createViewTracked(storageTextures[i], stViewDesc);
            bg1Entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            bg1Entries[i].binding = i;
            bg1Entries[i].textureView = stViews[i];
        }
        WGPUBindGroupDescriptor bg1Desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bg1Desc.layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 1);
        bg1Desc.entryCount = bg1Entries.size();
        bg1Desc.entries = bg1Entries.data();
        WGPUBindGroup bindGroup1 = t.createBindGroupTracked(bg1Desc);

        // Render pass: color attachments + depth-stencil (contents unchecked).
        WGPUTextureViewDescriptor targetViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        std::vector<WGPUTextureView> targetViews(numColorAttachments);
        std::vector<WGPURenderPassColorAttachment> colorAttachments(numColorAttachments);
        for (uint32_t i = 0; i < numColorAttachments; ++i) {
            targetViews[i] = t.createViewTracked(colorTargets[i], targetViewDesc);
            colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachments[i].view = targetViews[i];
            colorAttachments[i].loadOp = WGPULoadOp_Clear;
            colorAttachments[i].storeOp = WGPUStoreOp_Store;
        }

        WGPUTextureDescriptor dsTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dsTexDesc.format = WGPUTextureFormat_Depth24PlusStencil8;
        dsTexDesc.usage = WGPUTextureUsage_RenderAttachment;
        dsTexDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
        WGPUTexture dsTexture = t.createTextureTracked(dsTexDesc);
        WGPUTextureViewDescriptor dsViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView dsView = t.createViewTracked(dsTexture, dsViewDesc);

        WGPURenderPassDepthStencilAttachment dsAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        dsAttachment.view = dsView;
        dsAttachment.depthClearValue = 1.0f;
        dsAttachment.depthLoadOp = WGPULoadOp_Clear;
        dsAttachment.depthStoreOp = WGPUStoreOp_Store;
        dsAttachment.stencilClearValue = 0;
        dsAttachment.stencilLoadOp = WGPULoadOp_Clear;
        dsAttachment.stencilStoreOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = colorAttachments.size();
        passDesc.colorAttachments = colorAttachments.data();
        passDesc.depthStencilAttachment = &dsAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup0, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, bindGroup1, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // Verify storage buffers (rgba32uint contents).
        for (uint32_t i = 0; i < numStorageBuffers; ++i) {
            const auto expected = makeExpectedRGBA32Uint(i);
            t.expectGPUBufferValuesEqual(storageBuffers[i], expected.data(), kSize);
        }

        // Verify storage textures (rgba32uint, value == p + i*2).
        for (uint32_t i = 0; i < numStorageTextures; ++i) {
            const auto expected32 = makeExpectedRGBA32Uint(i * 2);
            const uint32_t bytesPerRowTight = kWidth * 16;
            const std::vector<uint8_t> padded =
                padRows(reinterpret_cast<const uint8_t*>(expected32.data()), bytesPerRowTight);

            WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            readbackDesc.size = padded.size();
            readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
            WGPUBuffer readback = t.createBufferTracked(readbackDesc);

            WGPUCommandEncoder copyEnc = t.createCommandEncoderTracked();
            t.copyTextureToBuffer(
                copyEnc, storageTextures[i], readback, kBytesPerRowAlignment, WGPUExtent3D{kWidth, kHeight, 1});
            WGPUCommandBuffer copyCb = t.finishTracked(copyEnc);
            wgpuQueueSubmit(t.queue(), 1, &copyCb);

            TexelCopyBufferLayout layout{0, kBytesPerRowAlignment, kHeight};
            t.expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
                padded.data(),
                padded.size(),
                readback,
                WGPUTextureFormat_RGBA32Uint,
                WGPUExtent3D{kWidth, kHeight, 1},
                layout);
        }

        // Verify color targets (value == p + i*3, interpreted in the target format).
        for (uint32_t i = 0; i < numColorAttachments; ++i) {
            std::vector<uint8_t> tight;
            uint32_t bytesPerRowTight = 0;
            switch (format) {
                case WGPUTextureFormat_R8Uint: {
                    const auto e = makeExpectedR8Uint(i * 3);
                    tight.assign(e.begin(), e.end());
                    bytesPerRowTight = kWidth;
                    break;
                }
                case WGPUTextureFormat_RGBA8Uint: {
                    const auto e = makeExpectedRGBA8Uint(i * 3);
                    tight.assign(e.begin(), e.end());
                    bytesPerRowTight = kWidth * 4;
                    break;
                }
                case WGPUTextureFormat_RGBA32Uint: {
                    const auto e = makeExpectedRGBA32Uint(i * 3);
                    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(e.data());
                    tight.assign(bytes, bytes + sizeof(e));
                    bytesPerRowTight = kWidth * 16;
                    break;
                }
                default:
                    std::abort();
            }

            const std::vector<uint8_t> padded = padRows(tight.data(), bytesPerRowTight);

            WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            readbackDesc.size = padded.size();
            readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
            WGPUBuffer readback = t.createBufferTracked(readbackDesc);

            WGPUCommandEncoder copyEnc = t.createCommandEncoderTracked();
            t.copyTextureToBuffer(
                copyEnc, colorTargets[i], readback, kBytesPerRowAlignment, WGPUExtent3D{kWidth, kHeight, 1});
            WGPUCommandBuffer copyCb = t.finishTracked(copyEnc);
            wgpuQueueSubmit(t.queue(), 1, &copyCb);

            TexelCopyBufferLayout layout{0, kBytesPerRowAlignment, kHeight};
            t.expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
                padded.data(),
                padded.size(),
                readback,
                format,
                WGPUExtent3D{kWidth, kHeight, 1},
                layout);
        }
    });

} // namespace
