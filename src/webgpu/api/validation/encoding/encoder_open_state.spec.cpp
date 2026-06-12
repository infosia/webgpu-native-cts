// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/encoding/encoder_open_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,encoder_open_state",
    "Validation tests to all commands of GPUCommandEncoder, GPUComputePassEncoder, and "
    "GPURenderPassEncoder when the encoder is not finished.");

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

WGPUTextureView createRenderView(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{4, 4, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, viewDesc);
}

WGPURenderPassEncoder beginRenderPass(WGPUCommandEncoder encoder, WGPUTextureView view, WGPUQuerySet querySet = nullptr) {
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &color;
    desc.occlusionQuerySet = querySet;
    return wgpuCommandEncoderBeginRenderPass(encoder, &desc);
}

WGPURenderPipeline createRenderPipeline(AllFeaturesMaxLimitsGpuTest& t) {
    constexpr std::string_view code =
        "@vertex fn vs_main() -> @builtin(position) vec4<f32> { return vec4<f32>(); }\n"
        "@fragment fn fs_main() {}";
    WGPUShaderModule module = t.createShaderModuleTracked(code);

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_None;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("fs_main");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs_main");
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

WGPUComputePipeline createComputePipeline(AllFeaturesMaxLimitsGpuTest& t) {
    constexpr std::string_view code = "@compute @workgroup_size(1) fn main() {}";
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

WGPUBindGroup createBindGroupForTest(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    WGPUSampler sampler = t.createSamplerTracked(samplerDesc);

    WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Fragment;
    layoutEntry.sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    WGPUBindGroupLayout layout = t.createBindGroupLayoutTracked(layoutDesc);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.sampler = sampler;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, WGPUBufferUsage usage, uint64_t size = 32) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUQuerySet createOcclusionQuerySet(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.type = WGPUQueryType_Occlusion;
    desc.count = 1;
    return wgpuDeviceCreateQuerySet(t.device(), &desc);
}

std::vector<Value> encoderCommands() {
    return {
        std::string("beginComputePass"),
        std::string("beginRenderPass"),
        std::string("clearBuffer"),
        std::string("copyBufferToBuffer"),
        std::string("copyBufferToTexture"),
        std::string("copyTextureToBuffer"),
        std::string("copyTextureToTexture"),
        std::string("insertDebugMarker"),
        std::string("popDebugGroup"),
        std::string("pushDebugGroup"),
        std::string("resolveQuerySet"),
    };
}

std::vector<Value> renderPassCommands() {
    return {
        std::string("draw"),
        std::string("drawIndexed"),
        std::string("drawIndexedIndirect"),
        std::string("drawIndirect"),
        std::string("multiDrawIndexedIndirect"),
        std::string("multiDrawIndirect"),
        std::string("setIndexBuffer"),
        std::string("setBindGroup"),
        std::string("setVertexBuffer"),
        std::string("setPipeline"),
        std::string("setViewport"),
        std::string("setScissorRect"),
        std::string("setBlendConstant"),
        std::string("setStencilReference"),
        std::string("setImmediates"),
        std::string("beginOcclusionQuery"),
        std::string("endOcclusionQuery"),
        std::string("executeBundles"),
        std::string("pushDebugGroup"),
        std::string("popDebugGroup"),
        std::string("insertDebugMarker"),
    };
}

std::vector<Value> renderBundleCommands() {
    return {
        std::string("draw"),
        std::string("drawIndexed"),
        std::string("drawIndexedIndirect"),
        std::string("drawIndirect"),
        std::string("setPipeline"),
        std::string("setBindGroup"),
        std::string("setIndexBuffer"),
        std::string("setVertexBuffer"),
        std::string("setImmediates"),
        std::string("pushDebugGroup"),
        std::string("popDebugGroup"),
        std::string("insertDebugMarker"),
    };
}

std::vector<Value> computePassCommands() {
    return {
        std::string("setBindGroup"),
        std::string("setPipeline"),
        std::string("setImmediates"),
        std::string("dispatchWorkgroups"),
        std::string("dispatchWorkgroupsIndirect"),
        std::string("pushDebugGroup"),
        std::string("popDebugGroup"),
        std::string("insertDebugMarker"),
    };
}

bool isUnavailableCommand(const std::string& command) {
    return command == "setImmediates" || command == "multiDrawIndirect" || command == "multiDrawIndexedIndirect";
}

void skipUnavailableCommand(AllFeaturesMaxLimitsGpuTest& t, const std::string& command) {
    if (command == "setImmediates") {
        t.skip("setImmediates native entry points are not exported by any backend at this revision");
    }
    if (command == "multiDrawIndirect" || command == "multiDrawIndexedIndirect") {
        t.skip("chromium-experimental-multi-draw-indirect is not exposed in native headers");
    }
}

void encodeCommandEncoderCommand(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder, const std::string& command) {
    WGPUBuffer srcBuffer = createBuffer(t, WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Indirect);
    WGPUBuffer dstBuffer = createBuffer(t, WGPUBufferUsage_CopyDst | WGPUBufferUsage_QueryResolve);
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{4, 4, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
    WGPUTexture srcTexture = t.createTextureTracked(texDesc);
    WGPUTexture dstTexture = t.createTextureTracked(texDesc);
    WGPUQuerySet querySet = createOcclusionQuerySet(t);

    if (command == "beginComputePass") {
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        if (pass != nullptr) {
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
        }
    } else if (command == "beginRenderPass") {
        WGPURenderPassEncoder pass = beginRenderPass(encoder, createRenderView(t));
        if (pass != nullptr) {
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        }
    } else if (command == "clearBuffer") {
        wgpuCommandEncoderClearBuffer(encoder, dstBuffer, 0, 16);
    } else if (command == "copyBufferToBuffer") {
        wgpuCommandEncoderCopyBufferToBuffer(encoder, srcBuffer, 0, dstBuffer, 0, 0);
    } else if (command == "copyBufferToTexture") {
        t.copyBufferToTexture(encoder, srcBuffer, 256, dstTexture, WGPUExtent3D{1, 1, 1});
    } else if (command == "copyTextureToBuffer") {
        t.copyTextureToBuffer(encoder, srcTexture, dstBuffer, 256, WGPUExtent3D{1, 1, 1});
    } else if (command == "copyTextureToTexture") {
        t.copyTextureToTexture(encoder, srcTexture, dstTexture, WGPUExtent3D{1, 1, 1});
    } else if (command == "insertDebugMarker") {
        wgpuCommandEncoderInsertDebugMarker(encoder, sv("marker"));
    } else if (command == "popDebugGroup") {
        wgpuCommandEncoderPushDebugGroup(encoder, sv("group"));
        wgpuCommandEncoderPopDebugGroup(encoder);
    } else if (command == "pushDebugGroup") {
        wgpuCommandEncoderPushDebugGroup(encoder, sv("group"));
        wgpuCommandEncoderPopDebugGroup(encoder);
    } else if (command == "resolveQuerySet") {
        wgpuCommandEncoderResolveQuerySet(encoder, querySet, 0, 1, dstBuffer, 0);
    }

    if (querySet != nullptr) {
        wgpuQuerySetRelease(querySet);
    }
}

void encodeRenderPassCommand(AllFeaturesMaxLimitsGpuTest& t, WGPURenderPassEncoder pass, const std::string& command) {
    WGPURenderPipeline pipeline = createRenderPipeline(t);
    WGPUBindGroup bindGroup = createBindGroupForTest(t);
    WGPUBuffer indexBuffer = createBuffer(t, WGPUBufferUsage_Index, 64);
    WGPUBuffer vertexBuffer = createBuffer(t, WGPUBufferUsage_Vertex, 64);
    WGPUBuffer indirectBuffer = createBuffer(t, WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst, 64);
    const uint32_t indirectDraw[5] = {1, 1, 0, 0, 0};
    t.queueWriteBuffer(indirectBuffer, 0, indirectDraw, sizeof(indirectDraw));

    if (command == "draw" || command == "drawIndirect") {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    }
    if (command == "drawIndexed" || command == "drawIndexedIndirect") {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0, 16);
    }
    if (command == "endOcclusionQuery") {
        wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
    }
    if (command == "popDebugGroup") {
        wgpuRenderPassEncoderPushDebugGroup(pass, sv("group"));
    }

    if (command == "draw") {
        wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
    } else if (command == "drawIndexed") {
        wgpuRenderPassEncoderDrawIndexed(pass, 1, 1, 0, 0, 0);
    } else if (command == "drawIndexedIndirect") {
        wgpuRenderPassEncoderDrawIndexedIndirect(pass, indirectBuffer, 0);
    } else if (command == "drawIndirect") {
        wgpuRenderPassEncoderDrawIndirect(pass, indirectBuffer, 0);
    } else if (command == "setIndexBuffer") {
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0, 16);
    } else if (command == "setBindGroup") {
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    } else if (command == "setVertexBuffer") {
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, 16);
    } else if (command == "setPipeline") {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    } else if (command == "setViewport") {
        wgpuRenderPassEncoderSetViewport(pass, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f);
    } else if (command == "setScissorRect") {
        wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, 1, 1);
    } else if (command == "setBlendConstant") {
        WGPUColor color = WGPUColor{1.0, 1.0, 1.0, 1.0};
        wgpuRenderPassEncoderSetBlendConstant(pass, &color);
    } else if (command == "setStencilReference") {
        wgpuRenderPassEncoderSetStencilReference(pass, 0);
    } else if (command == "beginOcclusionQuery") {
        wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
        wgpuRenderPassEncoderEndOcclusionQuery(pass);
    } else if (command == "endOcclusionQuery") {
        wgpuRenderPassEncoderEndOcclusionQuery(pass);
    } else if (command == "executeBundles") {
        wgpuRenderPassEncoderExecuteBundles(pass, 0, nullptr);
    } else if (command == "pushDebugGroup") {
        wgpuRenderPassEncoderPushDebugGroup(pass, sv("group"));
        wgpuRenderPassEncoderPopDebugGroup(pass);
    } else if (command == "popDebugGroup") {
        wgpuRenderPassEncoderPopDebugGroup(pass);
    } else if (command == "insertDebugMarker") {
        wgpuRenderPassEncoderInsertDebugMarker(pass, sv("marker"));
    }
}

void encodeRenderBundleCommand(AllFeaturesMaxLimitsGpuTest& t, WGPURenderBundleEncoder bundle, const std::string& command) {
    WGPURenderPipeline pipeline = createRenderPipeline(t);
    WGPUBindGroup bindGroup = createBindGroupForTest(t);
    WGPUBuffer indexBuffer = createBuffer(t, WGPUBufferUsage_Index, 64);
    WGPUBuffer vertexBuffer = createBuffer(t, WGPUBufferUsage_Vertex, 64);
    WGPUBuffer indirectBuffer = createBuffer(t, WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst, 64);
    const uint32_t indirectDraw[5] = {1, 1, 0, 0, 0};
    t.queueWriteBuffer(indirectBuffer, 0, indirectDraw, sizeof(indirectDraw));

    if (command == "draw" || command == "drawIndirect") {
        wgpuRenderBundleEncoderSetPipeline(bundle, pipeline);
    }
    if (command == "drawIndexed" || command == "drawIndexedIndirect") {
        wgpuRenderBundleEncoderSetPipeline(bundle, pipeline);
        wgpuRenderBundleEncoderSetIndexBuffer(bundle, indexBuffer, WGPUIndexFormat_Uint32, 0, 16);
    }
    if (command == "popDebugGroup") {
        wgpuRenderBundleEncoderPushDebugGroup(bundle, sv("group"));
    }

    if (command == "draw") {
        wgpuRenderBundleEncoderDraw(bundle, 1, 1, 0, 0);
    } else if (command == "drawIndexed") {
        wgpuRenderBundleEncoderDrawIndexed(bundle, 1, 1, 0, 0, 0);
    } else if (command == "drawIndexedIndirect") {
        wgpuRenderBundleEncoderDrawIndexedIndirect(bundle, indirectBuffer, 0);
    } else if (command == "drawIndirect") {
        wgpuRenderBundleEncoderDrawIndirect(bundle, indirectBuffer, 0);
    } else if (command == "setPipeline") {
        wgpuRenderBundleEncoderSetPipeline(bundle, pipeline);
    } else if (command == "setBindGroup") {
        wgpuRenderBundleEncoderSetBindGroup(bundle, 0, bindGroup, 0, nullptr);
    } else if (command == "setIndexBuffer") {
        wgpuRenderBundleEncoderSetIndexBuffer(bundle, indexBuffer, WGPUIndexFormat_Uint32, 0, 16);
    } else if (command == "setVertexBuffer") {
        wgpuRenderBundleEncoderSetVertexBuffer(bundle, 0, vertexBuffer, 0, 16);
    } else if (command == "pushDebugGroup") {
        wgpuRenderBundleEncoderPushDebugGroup(bundle, sv("group"));
        wgpuRenderBundleEncoderPopDebugGroup(bundle);
    } else if (command == "popDebugGroup") {
        wgpuRenderBundleEncoderPopDebugGroup(bundle);
    } else if (command == "insertDebugMarker") {
        wgpuRenderBundleEncoderInsertDebugMarker(bundle, sv("marker"));
    }
}

void encodeComputePassCommand(AllFeaturesMaxLimitsGpuTest& t, WGPUComputePassEncoder pass, const std::string& command) {
    WGPUComputePipeline pipeline = createComputePipeline(t);
    WGPUBuffer buffer = createBuffer(t, WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst, 16);
    const uint32_t indirect[3] = {1, 1, 1};
    t.queueWriteBuffer(buffer, 0, indirect, sizeof(indirect));
    if (command == "dispatchWorkgroups" || command == "dispatchWorkgroupsIndirect") {
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
    }
    if (command == "popDebugGroup") {
        wgpuComputePassEncoderPushDebugGroup(pass, sv("group"));
    }

    if (command == "setBindGroup") {
        wgpuComputePassEncoderSetBindGroup(pass, 0, nullptr, 0, nullptr);
    } else if (command == "setPipeline") {
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
    } else if (command == "dispatchWorkgroups") {
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    } else if (command == "dispatchWorkgroupsIndirect") {
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, buffer, 0);
    } else if (command == "pushDebugGroup") {
        wgpuComputePassEncoderPushDebugGroup(pass, sv("group"));
        wgpuComputePassEncoderPopDebugGroup(pass);
    } else if (command == "popDebugGroup") {
        wgpuComputePassEncoderPopDebugGroup(pass);
    } else if (command == "insertDebugMarker") {
        wgpuComputePassEncoderInsertDebugMarker(pass, sv("marker"));
    }
}

CTS_TEST(testGroup, "non_pass_commands")
    .desc("Test that functions of GPUCommandEncoder generate a validation error if the encoder is already finished.")
    .params([](ParamsBuilder u) {
        return u.combine("command", encoderCommands())
                .beginSubcases()
                .combine("finishBeforeCommand", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string command = t.param<std::string>("command");
        const bool finishBeforeCommand = t.param<bool>("finishBeforeCommand");
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (finishBeforeCommand) {
            t.finishTracked(encoder);
        }

        t.expectValidationError([&] {
            encodeCommandEncoderCommand(t, encoder, command);
        }, finishBeforeCommand);

        if (!finishBeforeCommand) {
            t.expectValidationError([&] {
                t.finishTracked(encoder);
            }, false);
        }
    });

CTS_TEST(testGroup, "render_pass_commands")
    .desc("Test that functions of GPURenderPassEncoder generate a validation error if the encoder or the pass is already finished.")
    .params([](ParamsBuilder u) {
        return u.combine("command", renderPassCommands())
                .beginSubcases()
                .combine("finishBeforeCommand", {std::string("no"), std::string("pass"), std::string("encoder")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string command = t.param<std::string>("command");
        const std::string finishBeforeCommand = t.param<std::string>("finishBeforeCommand");
        if (isUnavailableCommand(command)) {
            skipUnavailableCommand(t, command);
        }

        WGPUQuerySet querySet = createOcclusionQuerySet(t);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPass(encoder, createRenderView(t), querySet);

        if (finishBeforeCommand != "no") {
            wgpuRenderPassEncoderEnd(pass);
        }
        if (finishBeforeCommand == "encoder") {
            t.finishTracked(encoder);
        }

        if (finishBeforeCommand == "no") {
            t.expectValidationError([&] {
                encodeRenderPassCommand(t, pass, command);
            }, false);
            wgpuRenderPassEncoderEnd(pass);
            t.expectValidationError([&] { t.finishTracked(encoder); }, false);
        } else if (finishBeforeCommand == "pass") {
            t.expectValidationError([&] {
                encodeRenderPassCommand(t, pass, command);
            }, false);
            t.expectValidationError([&] { t.finishTracked(encoder); }, true);
        } else {
            t.expectValidationError([&] {
                encodeRenderPassCommand(t, pass, command);
            }, true);
        }
        wgpuRenderPassEncoderRelease(pass);
        wgpuQuerySetRelease(querySet);
    });

CTS_TEST(testGroup, "render_bundle_commands")
    .desc("Test that functions of GPURenderBundleEncoder generate a validation error if the encoder or the pass is already finished.")
    .params([](ParamsBuilder u) {
        return u.combine("command", renderBundleCommands())
                .beginSubcases()
                .combine("finishBeforeCommand", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string command = t.param<std::string>("command");
        const bool finishBeforeCommand = t.param<bool>("finishBeforeCommand");
        if (isUnavailableCommand(command)) {
            skipUnavailableCommand(t, command);
        }

        WGPUTextureFormat colorFormat = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        desc.colorFormatCount = 1;
        desc.colorFormats = &colorFormat;
        WGPURenderBundleEncoder bundle = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
        if (finishBeforeCommand) {
            WGPURenderBundle finished = wgpuRenderBundleEncoderFinish(bundle, nullptr);
            if (finished != nullptr) {
                wgpuRenderBundleRelease(finished);
            }
        }

        t.expectValidationError([&] {
            encodeRenderBundleCommand(t, bundle, command);
        }, finishBeforeCommand);

        if (!finishBeforeCommand) {
            t.expectValidationError([&] {
                WGPURenderBundle finished = wgpuRenderBundleEncoderFinish(bundle, nullptr);
                if (finished != nullptr) {
                    wgpuRenderBundleRelease(finished);
                }
            }, false);
        }
        wgpuRenderBundleEncoderRelease(bundle);
    });

CTS_TEST(testGroup, "compute_pass_commands")
    .desc("Test that functions of GPUComputePassEncoder generate a validation error if the encoder or the pass is already finished.")
    .params([](ParamsBuilder u) {
        return u.combine("command", computePassCommands())
                .beginSubcases()
                .combine("finishBeforeCommand", {std::string("no"), std::string("pass"), std::string("encoder")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string command = t.param<std::string>("command");
        const std::string finishBeforeCommand = t.param<std::string>("finishBeforeCommand");
        if (isUnavailableCommand(command)) {
            skipUnavailableCommand(t, command);
        }

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        if (finishBeforeCommand != "no") {
            wgpuComputePassEncoderEnd(pass);
        }
        if (finishBeforeCommand == "encoder") {
            t.finishTracked(encoder);
        }

        if (finishBeforeCommand == "no") {
            t.expectValidationError([&] {
                encodeComputePassCommand(t, pass, command);
            }, false);
            wgpuComputePassEncoderEnd(pass);
            t.expectValidationError([&] { t.finishTracked(encoder); }, false);
        } else if (finishBeforeCommand == "pass") {
            t.expectValidationError([&] {
                encodeComputePassCommand(t, pass, command);
            }, false);
            t.expectValidationError([&] { t.finishTracked(encoder); }, true);
        } else {
            t.expectValidationError([&] {
                encodeComputePassCommand(t, pass, command);
            }, true);
        }
        wgpuComputePassEncoderRelease(pass);
    });

} // namespace
