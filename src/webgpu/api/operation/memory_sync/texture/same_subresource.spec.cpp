// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/texture/same_subresource.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - ../operation_context_helper.ts and ./texture_sync_test.ts are ported in
//     ../operation_context_helper.h and ./texture_sync_test.h; TextureSyncTestHelper (defined in the
//     upstream spec file) is ported here.
//   - Query spelling: the {op, in} object params (read/write/first/second) are strings holding
//     upstream's JSON, e.g. read="{\"op\":\"t2b-copy\",\"in\":\"command-encoder\"}" (see
//     texture_sync_test.h). Names, order and counts match upstream.
//   - skipIfNeedStorageTexturesAndNoStorageTextures returns early outside compatibility mode
//     upstream; the harness runs core devices, so it is a no-op.
//   - The texel data written by write-texture / t2t-copy / b2t-copy is upstream's
//     kTexelRepresentationInfo['rgba8unorm'].pack(encode(data)); every component is 0 or 1, so
//     each byte is the component times 255.
//   - The sample/storage pipelines do not depend on the case (the storage pipeline depends only on
//     the stored value), so they and their explicit bind group layouts are created once per device.
//   - expectSingleColor(rgba8unorm) is a copy into a fresh (zero-initialized) buffer whose texels
//     are compared to the expected bytes.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/api/operation/memory_sync/operation_context_helper.h"
#include "webgpu/api/operation/memory_sync/texture/texture_sync_test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;
using namespace cts::memory_sync;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,memory_sync,texture,same_subresource",
    R"(
Memory Synchronization Tests for Texture: read before write, read after write, and write after write to the same subresource.

- TODO: Test synchronization between multiple queues.
- TODO: Test depth/stencil attachments.
- TODO: Use non-solid-color texture contents [2]
)");

WGPUStringView sv(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

constexpr std::string_view kFullscreenQuadWGSL = R"(
  struct VertexOutput {
    @builtin(position) Position : vec4<f32>
  };

  @vertex fn vert_main(@builtin(vertex_index) VertexIndex : u32) -> VertexOutput {
    var pos = array<vec2<f32>, 6>(
        vec2<f32>( 1.0,  1.0),
        vec2<f32>( 1.0, -1.0),
        vec2<f32>(-1.0, -1.0),
        vec2<f32>( 1.0,  1.0),
        vec2<f32>(-1.0, -1.0),
        vec2<f32>(-1.0,  1.0));

    var output : VertexOutput;
    output.Position = vec4<f32>(pos[VertexIndex], 0.0, 1.0);
    return output;
  }
)";

constexpr std::string_view kSampleFragmentWGSL = R"(
                @group(0) @binding(0) var inputTex: texture_2d<f32>;
                @group(0) @binding(1) var outputTex: texture_storage_2d<rgba8unorm, write>;

                @fragment fn frag_main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) vec4<f32> {
                  let coord = vec2<i32>(fragCoord.xy);
                  textureStore(outputTex, coord, textureLoad(inputTex, coord, 0));
                  return vec4<f32>();
                }
)";

constexpr std::string_view kSampleComputeWGSL = R"(
                @group(0) @binding(0) var inputTex: texture_2d<f32>;
                @group(0) @binding(1) var outputTex: texture_storage_2d<rgba8unorm, write>;

                @compute @workgroup_size(8, 8)
                fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
                  if (any(gid.xy >= vec2<u32>(textureDimensions(inputTex)))) {
                    return;
                  }
                  let coord = vec2<i32>(gid.xy);
                  textureStore(outputTex, coord, textureLoad(inputTex, coord, 0));
                }
)";

std::string storageFragmentWGSL(const std::string& storedValue) {
    return std::string(kFullscreenQuadWGSL) + R"(

                @group(0) @binding(0) var outputTex: texture_storage_2d<rgba8unorm, write>;

                @fragment fn frag_main(@builtin(position) fragCoord: vec4<f32>) -> @location(0) vec4<f32> {
                  textureStore(outputTex, vec2<i32>(fragCoord.xy), )" +
           storedValue + R"();
                  return vec4<f32>();
                }
)";
}

std::string storageComputeWGSL(const std::string& storedValue) {
    return R"(
                @group(0) @binding(0) var outputTex: texture_storage_2d<rgba8unorm, write>;

                @compute @workgroup_size(8, 8)
                fn main(@builtin(global_invocation_id) gid : vec3<u32>) {
                  if (any(gid.xy >= vec2<u32>(textureDimensions(outputTex)))) {
                    return;
                  }
                  let coord = vec2<i32>(gid.xy);
                  textureStore(outputTex, coord, )" +
           storedValue + R"();
                }
)";
}

// PerTexelComponent<number> for rgba8unorm.
struct TexelValue {
    double R = 0;
    double G = 0;
    double B = 0;
    double A = 0;
};

// rep.pack(rep.encode(data)) for rgba8unorm.
std::array<uint8_t, 4> encodeRGBA8Unorm(const TexelValue& data) {
    auto encode = [](double v) {
        const double clamped = v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v);
        return static_cast<uint8_t>(clamped * 255.0 + 0.5);
    };
    return {{encode(data.R), encode(data.G), encode(data.B), encode(data.A)}};
}

// `vec4<f32>(${[R, G, B, A].map(x => x.toFixed(5)).join(', ')})`
std::string storedValueWGSL(const TexelValue& data) {
    std::array<char, 128> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "vec4<f32>(%.5f, %.5f, %.5f, %.5f)", data.R, data.G, data.B, data.A);
    return std::string(buffer.data());
}

// ---------------------------------------------------------------------------
// Per-device cache of the sample / storage pipelines and their explicit bind group layouts.
// ---------------------------------------------------------------------------

struct TextureSyncPipeline {
    WGPUBindGroupLayout bindGroupLayout = nullptr;
    WGPURenderPipeline renderPipeline = nullptr;
    WGPUComputePipeline computePipeline = nullptr;
};

struct TextureSyncPipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, std::map<std::string, TextureSyncPipeline>> byDevice;

    ~TextureSyncPipelineCache() override {
        for (auto& [device, pipelines] : byDevice) {
            (void)device;
            for (auto& [key, entry] : pipelines) {
                (void)key;
                if (entry.renderPipeline != nullptr) {
                    wgpuRenderPipelineRelease(entry.renderPipeline);
                }
                if (entry.computePipeline != nullptr) {
                    wgpuComputePipelineRelease(entry.computePipeline);
                }
                if (entry.bindGroupLayout != nullptr) {
                    wgpuBindGroupLayoutRelease(entry.bindGroupLayout);
                }
            }
        }
    }
};

WGPUShaderModule createShaderModuleUntracked(WGPUDevice device, std::string_view wgsl) {
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = sv(wgsl);
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;
    return wgpuDeviceCreateShaderModule(device, &desc);
}

// withSampledInput: binding 0 = texture_2d<f32> (unfilterable-float), binding 1 = write-only
// rgba8unorm storage texture ('sample' read op). Otherwise binding 0 = the storage texture
// ('storage' write op).
WGPUBindGroupLayout createUntrackedBindGroupLayout(WGPUDevice device, WGPUShaderStage visibility, bool withSampledInput) {
    std::array<WGPUBindGroupLayoutEntry, 2> entries = {{
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
        WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
    }};
    uint32_t count = 0;
    if (withSampledInput) {
        WGPUBindGroupLayoutEntry& sampled = entries[count++];
        sampled.binding = 0;
        sampled.visibility = visibility;
        sampled.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        sampled.texture.viewDimension = WGPUTextureViewDimension_2D;
        sampled.texture.multisampled = false;
    }
    WGPUBindGroupLayoutEntry& storage = entries[count++];
    storage.binding = withSampledInput ? 1 : 0;
    storage.visibility = visibility;
    storage.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
    storage.storageTexture.format = OperationContextHelper::kTextureFormat;
    storage.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = count;
    desc.entries = entries.data();
    return wgpuDeviceCreateBindGroupLayout(device, &desc);
}

// Upstream builds the pipeline (and bind group layout) inline for each op; they depend only on
// (op, render-or-compute, stored value), so they are cached per device under that key.
const TextureSyncPipeline& textureSyncPipeline(GpuTest& t,
                                               bool isRender,
                                               bool withSampledInput,
                                               const std::string& storedValue) {
    const std::string key = std::string(isRender ? "render:" : "compute:") +
                            (withSampledInput ? "sample" : "storage:" + storedValue);
    auto& pipelines = deviceScoped<TextureSyncPipelineCache>().byDevice[t.device()];
    auto it = pipelines.find(key);
    if (it != pipelines.end()) {
        return it->second;
    }

    WGPUDevice device = t.device();
    TextureSyncPipeline entry;
    // getVisibilityForContext: FRAGMENT for render-pass/render-bundle contexts, else COMPUTE.
    entry.bindGroupLayout = createUntrackedBindGroupLayout(
        device, isRender ? WGPUShaderStage_Fragment : WGPUShaderStage_Compute, withSampledInput);

    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    layoutDesc.bindGroupLayoutCount = 1;
    layoutDesc.bindGroupLayouts = &entry.bindGroupLayout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device, &layoutDesc);

    if (isRender) {
        const std::string code = withSampledInput
                                     ? std::string(kFullscreenQuadWGSL) + "\n" + std::string(kSampleFragmentWGSL)
                                     : storageFragmentWGSL(storedValue);
        WGPUShaderModule module = createShaderModuleUntracked(device, code);

        // Unused attachment since we can't use textureStore in the vertex shader. Set writeMask
        // to zero.
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = OperationContextHelper::kTextureFormat;
        target.writeMask = WGPUColorWriteMask_None;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = module;
        fragment.entryPoint = sv("frag_main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = layout;
        desc.vertex.module = module;
        desc.vertex.entryPoint = sv("vert_main");
        desc.fragment = &fragment;
        entry.renderPipeline = wgpuDeviceCreateRenderPipeline(device, &desc);
        wgpuShaderModuleRelease(module);
    } else {
        const std::string code = withSampledInput ? std::string(kSampleComputeWGSL) : storageComputeWGSL(storedValue);
        WGPUShaderModule module = createShaderModuleUntracked(device, code);
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = layout;
        desc.compute.module = module;
        desc.compute.entryPoint = sv("main");
        entry.computePipeline = wgpuDeviceCreateComputePipeline(device, &desc);
        wgpuShaderModuleRelease(module);
    }
    wgpuPipelineLayoutRelease(layout);
    return pipelines.emplace(key, entry).first->second;
}

// ---------------------------------------------------------------------------
// TextureSyncTestHelper
// ---------------------------------------------------------------------------

class TextureSyncTestHelper : public OperationContextHelper {
  public:
    TextureSyncTestHelper(GpuTest& t, WGPUTextureUsage usage) : OperationContextHelper(t) {
        texture_ = createTexture(usage);
    }

    // Upstream returns early outside compatibility mode; core devices never skip here.
    static void skipIfNeedStorageTexturesAndNoStorageTextures(GpuTest& /*t*/) {}

    // Perform a read operation on the test texture. Returns a texture copy holding the contents.
    WGPUTexture performReadOp(const OpAndContext& read) {
        const std::string& op = read.op;
        const OpContext context = parseContext(read.in);
        ensureContext(context);

        if (op == "t2t-copy") {
            WGPUTexture texture = createTexture(WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
            requireCommandEncoder();
            t_.copyTextureToTexture(commandEncoder, texture_, texture, textureSize());
            return texture;
        }
        if (op == "t2b-copy") {
            const TextureCopyLayout layout = getTextureCopyLayout(kTextureFormat, WGPUTextureDimension_2D, textureSize());
            WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            bufferDesc.size = layout.byteLength;
            bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
            WGPUBuffer buffer = t_.createBufferTracked(bufferDesc);

            WGPUTexture texture = createTexture(WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);
            requireCommandEncoder();
            t_.copyTextureToBuffer(commandEncoder, texture_, buffer, layout.bytesPerRow, textureSize());
            t_.copyBufferToTexture(commandEncoder, buffer, layout.bytesPerRow, texture, textureSize());
            return texture;
        }
        if (op == "sample") {
            WGPUTexture texture = createTexture(WGPUTextureUsage_CopySrc | WGPUTextureUsage_StorageBinding);
            const bool isRender = isRenderContext(read.in);
            const TextureSyncPipeline& pipeline = textureSyncPipeline(t_, isRender, true, "");

            std::array<WGPUBindGroupEntry, 2> entries = {{
                WGPU_BIND_GROUP_ENTRY_INIT,
                WGPU_BIND_GROUP_ENTRY_INIT,
            }};
            entries[0].binding = 0;
            entries[0].textureView = createView(texture_);
            entries[1].binding = 1;
            entries[1].textureView = createView(texture);
            WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bindGroupDesc.layout = pipeline.bindGroupLayout;
            bindGroupDesc.entryCount = entries.size();
            bindGroupDesc.entries = entries.data();
            WGPUBindGroup bindGroup = t_.createBindGroupTracked(bindGroupDesc);

            encodeDrawOrDispatch(context, pipeline, bindGroup);
            return texture;
        }
        // b2t-copy, attachment-resolve, attachment-store are not read ops.
        std::abort();
    }

    void performWriteOp(const OpAndContext& write, const TexelValue& data) {
        const std::string& op = write.op;
        const OpContext context = parseContext(write.in);
        ensureContext(context);

        if (op == "attachment-store") {
            requireCommandEncoder();
            WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            attachment.view = createView(texture_);
            // [2] Use non-solid-color texture values
            attachment.clearValue = WGPUColor{data.R, data.G, data.B, data.A};
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &attachment;
            renderPassEncoder = t_.beginRenderPassTracked(commandEncoder, &passDesc);
            currentContext_ = OpContext::RenderPassEncoder;
            return;
        }
        if (op == "write-texture") {
            // [2] Use non-solid-color texture values
            const std::vector<uint8_t> fullTexelData = solidTexelData(data, 4 * kTextureWidth);
            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.offset = 0;
            layout.bytesPerRow = 4 * kTextureWidth;
            t_.queueWriteTexture(texture_, textureSize(), layout, fullTexelData.data(), fullTexelData.size());
            return;
        }
        if (op == "t2t-copy") {
            WGPUTexture texture = createTexture(WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst);

            // [2] Use non-solid-color texture values
            const std::vector<uint8_t> fullTexelData = solidTexelData(data, 4 * kTextureWidth);
            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.offset = 0;
            layout.bytesPerRow = 4 * kTextureWidth;
            t_.queueWriteTexture(texture, textureSize(), layout, fullTexelData.data(), fullTexelData.size());

            requireCommandEncoder();
            t_.copyTextureToTexture(commandEncoder, texture, texture_, textureSize());
            return;
        }
        if (op == "b2t-copy") {
            // [2] Use non-solid-color texture values. bytesPerRow = align(texelByteLength, 256).
            constexpr uint32_t kBytesPerRow = 256;
            const std::vector<uint8_t> fullTexelData = solidTexelData(data, kBytesPerRow);

            WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            bufferDesc.size = fullTexelData.size();
            bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
            WGPUBuffer buffer = t_.createBufferTracked(bufferDesc);
            t_.queueWriteBuffer(buffer, 0, fullTexelData.data(), fullTexelData.size());

            requireCommandEncoder();
            t_.copyBufferToTexture(commandEncoder, buffer, kBytesPerRow, texture_, textureSize());
            return;
        }
        if (op == "attachment-resolve") {
            requireCommandEncoder();
            WGPUTextureDescriptor targetDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            targetDesc.format = kTextureFormat;
            targetDesc.size = textureSize();
            targetDesc.usage = WGPUTextureUsage_RenderAttachment;
            targetDesc.sampleCount = 4;
            targetDesc.mipLevelCount = 1;
            targetDesc.dimension = WGPUTextureDimension_2D;
            WGPUTexture renderTarget = t_.createTextureTracked(targetDesc);

            WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            attachment.view = createView(renderTarget);
            attachment.resolveTarget = createView(texture_);
            // [2] Use non-solid-color texture values
            attachment.clearValue = WGPUColor{data.R, data.G, data.B, data.A};
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Discard;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &attachment;
            renderPassEncoder = t_.beginRenderPassTracked(commandEncoder, &passDesc);
            currentContext_ = OpContext::RenderPassEncoder;
            return;
        }
        if (op == "storage") {
            const bool isRender = isRenderContext(write.in);
            // [2] Use non-solid-color texture values
            const TextureSyncPipeline& pipeline = textureSyncPipeline(t_, isRender, false, storedValueWGSL(data));

            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding = 0;
            entry.textureView = createView(texture_);
            WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bindGroupDesc.layout = pipeline.bindGroupLayout;
            bindGroupDesc.entryCount = 1;
            bindGroupDesc.entries = &entry;
            WGPUBindGroup bindGroup = t_.createBindGroupTracked(bindGroupDesc);

            encodeDrawOrDispatch(context, pipeline, bindGroup);
            return;
        }
        // t2b-copy and sample are not write ops.
        std::abort();
    }

  private:
    WGPUTexture createTexture(WGPUTextureUsage usage) {
        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = textureSize();
        desc.format = kTextureFormat;
        desc.usage = usage;
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        desc.dimension = WGPUTextureDimension_2D;
        return t_.createTextureTracked(desc);
    }

    WGPUTextureView createView(WGPUTexture texture) {
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        return t_.createViewTracked(texture, viewDesc);
    }

    void requireCommandEncoder() const {
        if (commandEncoder == nullptr) {
            std::abort();
        }
    }

    // A kTextureWidth x kTextureHeight block of `data` texels, rows `bytesPerRow` apart.
    static std::vector<uint8_t> solidTexelData(const TexelValue& data, uint32_t bytesPerRow) {
        const std::array<uint8_t, 4> texel = encodeRGBA8Unorm(data);
        std::vector<uint8_t> bytes(static_cast<size_t>(bytesPerRow) * kTextureHeight, 0);
        for (uint32_t y = 0; y < kTextureHeight; ++y) {
            for (uint32_t x = 0; x < kTextureWidth; ++x) {
                std::memcpy(bytes.data() + static_cast<size_t>(y) * bytesPerRow + x * texel.size(), texel.data(),
                            texel.size());
            }
        }
        return bytes;
    }

    void encodeDrawOrDispatch(OpContext context, const TextureSyncPipeline& pipeline, WGPUBindGroup bindGroup) {
        switch (context) {
            case OpContext::RenderBundleEncoder:
                if (renderBundleEncoder == nullptr) {
                    std::abort();
                }
                wgpuRenderBundleEncoderSetPipeline(renderBundleEncoder, pipeline.renderPipeline);
                wgpuRenderBundleEncoderSetBindGroup(renderBundleEncoder, 0, bindGroup, 0, nullptr);
                wgpuRenderBundleEncoderDraw(renderBundleEncoder, 6, 1, 0, 0);
                return;
            case OpContext::RenderPassEncoder:
                if (renderPassEncoder == nullptr) {
                    std::abort();
                }
                wgpuRenderPassEncoderSetPipeline(renderPassEncoder, pipeline.renderPipeline);
                wgpuRenderPassEncoderSetBindGroup(renderPassEncoder, 0, bindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(renderPassEncoder, 6, 1, 0, 0);
                return;
            case OpContext::ComputePassEncoder:
                if (computePassEncoder == nullptr) {
                    std::abort();
                }
                wgpuComputePassEncoderSetPipeline(computePassEncoder, pipeline.computePipeline);
                wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, bindGroup, 0, nullptr);
                wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, (kTextureWidth + 7) / 8,
                                                         (kTextureHeight + 7) / 8, 1);
                return;
            default:
                std::abort();
        }
    }

    WGPUTexture texture_ = nullptr;
};

// Upstream t.expectSingleColor(texture, 'rgba8unorm', {size: [4, 4, 1], exp}).
void expectSingleColor(GpuTest& t, WGPUTexture texture, const TexelValue& expectedTexel) {
    const WGPUExtent3D size = OperationContextHelper::textureSize();
    const TextureCopyLayout layout =
        getTextureCopyLayout(OperationContextHelper::kTextureFormat, WGPUTextureDimension_2D, size);

    WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufferDesc.size = layout.byteLength;
    bufferDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, layout.bytesPerRow, size);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    const std::array<uint8_t, 4> expected = encodeRGBA8Unorm(expectedTexel);
    const uint32_t bytesPerRow = layout.bytesPerRow;
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [size, bytesPerRow, expected](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (uint32_t y = 0; y < size.height; ++y) {
                for (uint32_t x = 0; x < size.width; ++x) {
                    const size_t offset = static_cast<size_t>(y) * bytesPerRow + static_cast<size_t>(x) * 4;
                    if (offset + 4 > len) {
                        return std::string("readback buffer too small");
                    }
                    if (std::memcmp(actual + offset, expected.data(), 4) != 0) {
                        std::ostringstream message;
                        message << "texel (" << x << "," << y << ") expected rgba8unorm {"
                                << static_cast<int>(expected[0]) << "," << static_cast<int>(expected[1]) << ","
                                << static_cast<int>(expected[2]) << "," << static_cast<int>(expected[3])
                                << "}, got {" << static_cast<int>(actual[offset]) << ","
                                << static_cast<int>(actual[offset + 1]) << "," << static_cast<int>(actual[offset + 2])
                                << "," << static_cast<int>(actual[offset + 3]) << "}";
                        return message.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(layout.byteLength));
}

CTS_TEST(g, "rw")
    .desc(R"(
    Perform a 'read' operations on a texture subresource, followed by a 'write' operation.
    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).
    Test that the results are synchronized.
    The read should not see the contents written by the subsequent write.)")
    .params([](ParamsBuilder u) { return u.combineWithParams(textureReadWriteCaseRecords()); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        const OpAndContext read = parseOpAndContext(t.param<std::string>("read"));
        const OpAndContext write = parseOpAndContext(t.param<std::string>("write"));

        TextureSyncTestHelper::skipIfNeedStorageTexturesAndNoStorageTextures(t);
        TextureSyncTestHelper helper(
            t, WGPUTextureUsage_CopyDst | textureOpInfo(read.op).readUsage | textureOpInfo(write.op).writeUsage);
        // [2] Use non-solid-color texture value.
        const TexelValue texelValue1{0, 1, 0, 1};
        const TexelValue texelValue2{1, 0, 0, 1};

        // Initialize the texture with something.
        helper.performWriteOp(OpAndContext{"write-texture", "queue"}, texelValue1);
        WGPUTexture readbackTexture = helper.performReadOp(read);
        helper.ensureBoundary(boundary);
        helper.performWriteOp(write, texelValue2);
        helper.ensureSubmit();

        // Contents should be the first value written, not the second.
        expectSingleColor(t, readbackTexture, texelValue1);
    });

CTS_TEST(g, "wr")
    .desc(R"(
    Perform a 'write' operation on a texture subresource, followed by a 'read' operation.
    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).
    Test that the results are synchronized.
    The read should see exactly the contents written by the previous write.

    - TODO: Use non-solid-color texture contents [2])")
    .params([](ParamsBuilder u) { return u.combineWithParams(textureWriteReadCaseRecords()); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        const OpAndContext write = parseOpAndContext(t.param<std::string>("write"));
        const OpAndContext read = parseOpAndContext(t.param<std::string>("read"));

        TextureSyncTestHelper::skipIfNeedStorageTexturesAndNoStorageTextures(t);
        TextureSyncTestHelper helper(t, textureOpInfo(read.op).readUsage | textureOpInfo(write.op).writeUsage);
        // [2] Use non-solid-color texture value.
        const TexelValue texelValue{0, 1, 0, 1};

        helper.performWriteOp(write, texelValue);
        helper.ensureBoundary(boundary);
        WGPUTexture readbackTexture = helper.performReadOp(read);
        helper.ensureSubmit();

        // Contents should be exactly the values written.
        expectSingleColor(t, readbackTexture, texelValue);
    });

CTS_TEST(g, "ww")
    .desc(R"(
    Perform a 'first' write operation on a texture subresource, followed by a 'second' write operation.
    Operations are separated by a 'boundary' (pass, encoder, queue-op, etc.).
    Test that the results are synchronized.
    The second write should overwrite the contents of the first.)")
    .params([](ParamsBuilder u) { return u.combineWithParams(textureWriteWriteCaseRecords()); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string boundary = t.param<std::string>("boundary");
        const OpAndContext first = parseOpAndContext(t.param<std::string>("first"));
        const OpAndContext second = parseOpAndContext(t.param<std::string>("second"));

        TextureSyncTestHelper::skipIfNeedStorageTexturesAndNoStorageTextures(t);
        TextureSyncTestHelper helper(
            t, WGPUTextureUsage_CopySrc | textureOpInfo(first.op).writeUsage | textureOpInfo(second.op).writeUsage);
        // [2] Use non-solid-color texture value.
        const TexelValue texelValue1{1, 0, 0, 1};
        const TexelValue texelValue2{0, 1, 0, 1};

        helper.performWriteOp(first, texelValue1);
        helper.ensureBoundary(boundary);
        helper.performWriteOp(second, texelValue2);
        helper.ensureSubmit();

        // Read back the contents so we can test the result.
        WGPUTexture readbackTexture = helper.performReadOp(OpAndContext{"t2t-copy", "command-encoder"});
        helper.ensureSubmit();

        // Contents should be the second value written.
        expectSingleColor(t, readbackTexture, texelValue2);
    });

CTS_TEST(g, "rw,single_pass,load_store")
    .desc(R"(
    TODO: Test memory synchronization when loading from a texture subresource in a single pass and storing to it.)")
    .unimplemented();

CTS_TEST(g, "rw,single_pass,load_resolve")
    .desc(R"(
    TODO: Test memory synchronization when loading from a texture subresource in a single pass and resolving to it.)")
    .unimplemented();

} // namespace
