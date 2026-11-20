// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/operation_context_helper.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Shared by the memory_sync buffer and texture tests: the operation boundaries / contexts tables
// (kOperationBoundaries, kOperationContexts, kBoundaryInfo) and the OperationContextHelper state
// machine that moves between the queue, command-encoder, pass and render-bundle contexts.
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/webgpu.h"

namespace cts::memory_sync {

// Context a particular operation is permitted in. Sorted such that the first is the most
// top-level context and the last is the most nested (inside a render bundle, in a render pass).
enum class OpContext : int {
    Queue = 0,
    CommandEncoder = 1,
    ComputePassEncoder = 2,
    RenderPassEncoder = 3,
    RenderBundleEncoder = 4,
};

inline constexpr std::array<const char*, 5> kOperationContexts = {
    "queue",                 // Operation occurs on the GPUQueue object.
    "command-encoder",       // Operation may be encoded in a GPUCommandEncoder.
    "compute-pass-encoder",  // Operation may be encoded in a GPUComputePassEncoder.
    "render-pass-encoder",   // Operation may be encoded in a GPURenderPassEncoder.
    "render-bundle-encoder", // Operation may be encoded in a GPURenderBundleEncoder.
};

inline OpContext parseContext(std::string_view name) {
    for (size_t i = 0; i < kOperationContexts.size(); ++i) {
        if (name == kOperationContexts[i]) {
            return static_cast<OpContext>(static_cast<int>(i));
        }
    }
    std::abort();
}

// Boundary between the first operation and the second operation.
inline constexpr std::array<const char*, 7> kOperationBoundaries = {
    "queue-op",        // Operations are performed in different queue operations (submit, writeTexture).
    "command-buffer",  // Operations are in different command buffers.
    "pass",            // Operations are in different passes.
    "execute-bundles", // Operations are in different executeBundles(...) calls.
    "render-bundle",   // Operations are in different render bundles.
    "dispatch",        // Operations are in different dispatches.
    "draw",            // Operations are in different draws.
};

struct ContextPair {
    const char* first;
    const char* second;
};

// Port of kBoundaryInfo: boundary => the context pairs the boundary can separate.
inline std::vector<ContextPair> boundaryContexts(std::string_view boundary) {
    constexpr const char* kCe = "command-encoder";
    constexpr const char* kCp = "compute-pass-encoder";
    constexpr const char* kRp = "render-pass-encoder";
    constexpr const char* kRb = "render-bundle-encoder";

    if (boundary == "queue-op") {
        std::vector<ContextPair> pairs;
        for (const char* a : kOperationContexts) {
            for (const char* b : kOperationContexts) {
                pairs.push_back(ContextPair{a, b});
            }
        }
        return pairs;
    }
    if (boundary == "command-buffer") {
        const std::array<const char*, 4> nonQueue = {kCe, kCp, kRp, kRb};
        std::vector<ContextPair> pairs;
        for (const char* a : nonQueue) {
            for (const char* b : nonQueue) {
                pairs.push_back(ContextPair{a, b});
            }
        }
        return pairs;
    }
    if (boundary == "pass") {
        return {
            {kCp, kCp}, {kCp, kRp}, {kRp, kCp}, {kRp, kRp},
            {kRb, kRp}, {kRp, kRb}, {kRb, kRb},
        };
    }
    if (boundary == "execute-bundles") {
        return {{kRb, kRb}};
    }
    if (boundary == "render-bundle") {
        return {{kRb, kRp}, {kRp, kRb}, {kRb, kRb}};
    }
    if (boundary == "dispatch") {
        return {{kCp, kCp}};
    }
    if (boundary == "draw") {
        return {{kRp, kRp}, {kRb, kRp}, {kRp, kRb}};
    }
    std::abort();
}

inline bool isRenderContext(std::string_view context) {
    return context == "render-pass-encoder" || context == "render-bundle-encoder";
}

// GPURenderPassEncoder | GPURenderBundleEncoder: forwards each call to whichever is set.
struct RenderEncoder {
    WGPURenderPassEncoder pass = nullptr;
    WGPURenderBundleEncoder bundle = nullptr;

    void setPipeline(WGPURenderPipeline pipeline) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderSetPipeline(bundle, pipeline);
        } else {
            std::abort();
        }
    }
    void setBindGroup(uint32_t index, WGPUBindGroup group) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetBindGroup(pass, index, group, 0, nullptr);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderSetBindGroup(bundle, index, group, 0, nullptr);
        } else {
            std::abort();
        }
    }
    void setVertexBuffer(uint32_t slot, WGPUBuffer buffer) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetVertexBuffer(pass, slot, buffer, 0, WGPU_WHOLE_SIZE);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderSetVertexBuffer(bundle, slot, buffer, 0, WGPU_WHOLE_SIZE);
        } else {
            std::abort();
        }
    }
    void setIndexBufferUint32(WGPUBuffer buffer) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderSetIndexBuffer(pass, buffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderSetIndexBuffer(bundle, buffer, WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);
        } else {
            std::abort();
        }
    }
    void draw(uint32_t vertexCount) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderDraw(pass, vertexCount, 1, 0, 0);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderDraw(bundle, vertexCount, 1, 0, 0);
        } else {
            std::abort();
        }
    }
    void drawIndexed(uint32_t indexCount) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderDrawIndexed(pass, indexCount, 1, 0, 0, 0);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderDrawIndexed(bundle, indexCount, 1, 0, 0, 0);
        } else {
            std::abort();
        }
    }
    void drawIndirect(WGPUBuffer indirectBuffer) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderDrawIndirect(pass, indirectBuffer, 0);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderDrawIndirect(bundle, indirectBuffer, 0);
        } else {
            std::abort();
        }
    }
    void drawIndexedIndirect(WGPUBuffer indirectBuffer) const {
        if (pass != nullptr) {
            wgpuRenderPassEncoderDrawIndexedIndirect(pass, indirectBuffer, 0);
        } else if (bundle != nullptr) {
            wgpuRenderBundleEncoderDrawIndexedIndirect(bundle, indirectBuffer, 0);
        } else {
            std::abort();
        }
    }
};

// Port of OperationContextHelper. Every encoder / command buffer / bundle it creates is tracked
// by the fixture, so nothing here outlives the case.
class OperationContextHelper {
  public:
    explicit OperationContextHelper(GpuTest& t) : t_(t) {}
    virtual ~OperationContextHelper() = default;

    // Set based on the current context.
    WGPUCommandEncoder commandEncoder = nullptr;
    WGPUComputePassEncoder computePassEncoder = nullptr;
    WGPURenderPassEncoder renderPassEncoder = nullptr;
    WGPURenderBundleEncoder renderBundleEncoder = nullptr;

    static constexpr uint32_t kTextureWidth = 4;
    static constexpr uint32_t kTextureHeight = 4;
    static constexpr WGPUTextureFormat kTextureFormat = WGPUTextureFormat_RGBA8Unorm;

    static WGPUExtent3D textureSize() {
        return WGPUExtent3D{kTextureWidth, kTextureHeight, 1};
    }

    // The renderer for an op encoded in `context` (render pass or render bundle).
    RenderEncoder rendererFor(OpContext context) const {
        RenderEncoder renderer;
        if (context == OpContext::RenderBundleEncoder) {
            renderer.bundle = renderBundleEncoder;
        } else {
            renderer.pass = renderPassEncoder;
        }
        return renderer;
    }

    // Ensure that all encoded commands are finished and submitted.
    void ensureSubmit() {
        ensureContext(OpContext::Queue);
        flushCommandBuffers();
    }

    void ensureContext(OpContext context) {
        // Find the common ancestor so we can transition currentContext -> context.
        const int ancestorIndex = std::min(static_cast<int>(context), static_cast<int>(currentContext_));
        const OpContext ancestor = static_cast<OpContext>(ancestorIndex);

        // Pop the context until we're at the common ancestor.
        while (currentContext_ != ancestor) {
            // About to pop the render pass encoder: execute any outstanding render bundles.
            if (currentContext_ == OpContext::RenderPassEncoder) {
                flushRenderBundles();
            }
            popContext();
        }

        if (currentContext_ == context) {
            return;
        }

        switch (context) {
            case OpContext::Queue:
                std::abort(); // unreachable
            case OpContext::CommandEncoder:
                if (currentContext_ != OpContext::Queue) {
                    std::abort();
                }
                commandEncoder = t_.createCommandEncoderTracked();
                break;
            case OpContext::ComputePassEncoder: {
                if (currentContext_ == OpContext::Queue) {
                    commandEncoder = t_.createCommandEncoderTracked();
                } else if (currentContext_ != OpContext::CommandEncoder) {
                    std::abort();
                }
                WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
                computePassEncoder = t_.beginComputePassTracked(commandEncoder, &passDesc);
                break;
            }
            case OpContext::RenderPassEncoder:
                if (currentContext_ == OpContext::Queue) {
                    commandEncoder = t_.createCommandEncoderTracked();
                } else if (currentContext_ != OpContext::CommandEncoder) {
                    std::abort();
                }
                beginDummyRenderPass();
                break;
            case OpContext::RenderBundleEncoder: {
                if (currentContext_ == OpContext::Queue) {
                    commandEncoder = t_.createCommandEncoderTracked();
                }
                if (currentContext_ == OpContext::Queue || currentContext_ == OpContext::CommandEncoder) {
                    beginDummyRenderPass();
                } else if (currentContext_ != OpContext::RenderPassEncoder) {
                    std::abort();
                }
                WGPUTextureFormat colorFormat = kTextureFormat;
                WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                desc.colorFormatCount = 1;
                desc.colorFormats = &colorFormat;
                desc.sampleCount = 1;
                renderBundleEncoder = t_.createRenderBundleEncoderTracked(desc);
                break;
            }
        }
        currentContext_ = context;
    }

    void ensureBoundary(std::string_view boundary) {
        if (boundary == "command-buffer") {
            ensureContext(OpContext::Queue);
            return;
        }
        if (boundary == "queue-op") {
            ensureContext(OpContext::Queue);
            // Submit any command buffers so the next one is in a separate submit.
            flushCommandBuffers();
            return;
        }
        if (boundary == "dispatch") {
            // Nothing to do to separate dispatches.
            if (currentContext_ != OpContext::ComputePassEncoder) {
                std::abort();
            }
            return;
        }
        if (boundary == "draw") {
            // Nothing to do to separate draws.
            if (currentContext_ != OpContext::RenderPassEncoder &&
                currentContext_ != OpContext::RenderBundleEncoder) {
                std::abort();
            }
            return;
        }
        if (boundary == "pass") {
            ensureContext(OpContext::CommandEncoder);
            return;
        }
        if (boundary == "render-bundle") {
            ensureContext(OpContext::RenderPassEncoder);
            return;
        }
        if (boundary == "execute-bundles") {
            ensureContext(OpContext::RenderPassEncoder);
            // Execute any render bundles so the next one is in a separate executeBundles.
            flushRenderBundles();
            return;
        }
        std::abort();
    }

  protected:
    GpuTest& t_;
    // We start at the queue context which is top-level.
    OpContext currentContext_ = OpContext::Queue;

  private:
    // Upstream makeDummyAttachment: 4x4 rgba8unorm, loadOp 'load', storeOp 'store'.
    void beginDummyRenderPass() {
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = textureSize();
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = kTextureFormat;
        texDesc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture texture = t_.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t_.createViewTracked(texture, viewDesc);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Load;
        colorAttachment.storeOp = WGPUStoreOp_Store;

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        renderPassEncoder = t_.beginRenderPassTracked(commandEncoder, &passDesc);
    }

    void popContext() {
        switch (currentContext_) {
            case OpContext::Queue:
                std::abort(); // unreachable
            case OpContext::CommandEncoder: {
                WGPUCommandBuffer commandBuffer = t_.finishTracked(commandEncoder);
                commandEncoder = nullptr;
                currentContext_ = OpContext::Queue;
                commandBuffers_.push_back(commandBuffer);
                break;
            }
            case OpContext::ComputePassEncoder:
                wgpuComputePassEncoderEnd(computePassEncoder);
                computePassEncoder = nullptr;
                currentContext_ = OpContext::CommandEncoder;
                break;
            case OpContext::RenderPassEncoder:
                wgpuRenderPassEncoderEnd(renderPassEncoder);
                renderPassEncoder = nullptr;
                currentContext_ = OpContext::CommandEncoder;
                break;
            case OpContext::RenderBundleEncoder: {
                WGPURenderBundle bundle = t_.finishRenderBundleTracked(renderBundleEncoder);
                renderBundleEncoder = nullptr;
                currentContext_ = OpContext::RenderPassEncoder;
                renderBundles_.push_back(bundle);
                break;
            }
        }
    }

    void flushRenderBundles() {
        if (renderPassEncoder == nullptr) {
            std::abort();
        }
        if (!renderBundles_.empty()) {
            wgpuRenderPassEncoderExecuteBundles(renderPassEncoder, renderBundles_.size(), renderBundles_.data());
            renderBundles_.clear();
        }
    }

    void flushCommandBuffers() {
        if (!commandBuffers_.empty()) {
            wgpuQueueSubmit(t_.queue(), commandBuffers_.size(), commandBuffers_.data());
            commandBuffers_.clear();
        }
    }

    std::vector<WGPUCommandBuffer> commandBuffers_;
    std::vector<WGPURenderBundle> renderBundles_;
};

} // namespace cts::memory_sync
