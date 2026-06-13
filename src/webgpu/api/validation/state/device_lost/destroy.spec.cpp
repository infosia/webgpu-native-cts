// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/state/device_lost/destroy.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "cts/test.h"
#include "cts/webgpu.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

struct LostState {
    bool fired = false;
    WGPUDeviceLostReason reason = WGPUDeviceLostReason_Unknown;
};

void onDeviceLost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView, void* userdata1, void*) {
    auto* state = static_cast<LostState*>(userdata1);
    state->fired = true;
    state->reason = reason;
}

struct HandleTracker {
    std::vector<WGPUBuffer> buffers;
    std::vector<WGPUSampler> samplers;
    std::vector<WGPUTexture> textures;
    std::vector<WGPUTextureView> textureViews;
    std::vector<WGPUShaderModule> shaderModules;
    std::vector<WGPUBindGroupLayout> bindGroupLayouts;
    std::vector<WGPUBindGroup> bindGroups;
    std::vector<WGPUPipelineLayout> pipelineLayouts;
    std::vector<WGPUComputePipeline> computePipelines;
    std::vector<WGPURenderPipeline> renderPipelines;
    std::vector<WGPUCommandEncoder> commandEncoders;
    std::vector<WGPUCommandBuffer> commandBuffers;
    std::vector<WGPUQuerySet> querySets;
    std::vector<WGPURenderBundleEncoder> renderBundleEncoders;
    std::vector<WGPURenderBundle> renderBundles;

    ~HandleTracker() {
        for (WGPURenderBundle h : renderBundles) if (h != nullptr) wgpuRenderBundleRelease(h);
        for (WGPURenderBundleEncoder h : renderBundleEncoders) if (h != nullptr) wgpuRenderBundleEncoderRelease(h);
        for (WGPUQuerySet h : querySets) if (h != nullptr) wgpuQuerySetRelease(h);
        for (WGPUCommandBuffer h : commandBuffers) if (h != nullptr) wgpuCommandBufferRelease(h);
        for (WGPUCommandEncoder h : commandEncoders) if (h != nullptr) wgpuCommandEncoderRelease(h);
        for (WGPURenderPipeline h : renderPipelines) if (h != nullptr) wgpuRenderPipelineRelease(h);
        for (WGPUComputePipeline h : computePipelines) if (h != nullptr) wgpuComputePipelineRelease(h);
        for (WGPUPipelineLayout h : pipelineLayouts) if (h != nullptr) wgpuPipelineLayoutRelease(h);
        for (WGPUBindGroup h : bindGroups) if (h != nullptr) wgpuBindGroupRelease(h);
        for (WGPUBindGroupLayout h : bindGroupLayouts) if (h != nullptr) wgpuBindGroupLayoutRelease(h);
        for (WGPUShaderModule h : shaderModules) if (h != nullptr) wgpuShaderModuleRelease(h);
        for (WGPUTextureView h : textureViews) if (h != nullptr) wgpuTextureViewRelease(h);
        for (WGPUTexture h : textures) if (h != nullptr) wgpuTextureRelease(h);
        for (WGPUSampler h : samplers) if (h != nullptr) wgpuSamplerRelease(h);
        for (WGPUBuffer h : buffers) if (h != nullptr) wgpuBufferRelease(h);
    }
};

struct OwnedDeviceContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    LostState lost;
    HandleTracker handles;

    OwnedDeviceContext() = default;
    OwnedDeviceContext(const OwnedDeviceContext&) = delete;
    OwnedDeviceContext& operator=(const OwnedDeviceContext&) = delete;

    ~OwnedDeviceContext() {
        if (queue != nullptr) wgpuQueueRelease(queue);
        if (device != nullptr) wgpuDeviceRelease(device);
        if (adapter != nullptr) wgpuAdapterRelease(adapter);
        if (instance != nullptr) wgpuInstanceRelease(instance);
    }
};

std::vector<WGPUFeatureName> supportedFeatures(WGPUAdapter adapter) {
    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(adapter, &features);
    std::vector<WGPUFeatureName> result(features.featureCount);
    for (size_t i = 0; i < features.featureCount; ++i) {
        result[i] = features.features[i];
    }
    wgpuSupportedFeaturesFreeMembers(features);
    return result;
}

void createOwnedDevice(Fixture& t, OwnedDeviceContext& ctx) {
    ctx.instance = createInstance();
    if (ctx.instance == nullptr) {
        t.fail("device_lost/destroy: failed to create WGPUInstance");
    }

    AdapterResult adapter = requestAdapterSync(ctx.instance, nullptr);
    if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
        t.fail("device_lost/destroy: failed to request adapter: " + adapter.message);
    }
    ctx.adapter = adapter.adapter;

    WGPULimits limits = WGPU_LIMITS_INIT;
    const bool gotLimits = wgpuAdapterGetLimits(ctx.adapter, &limits) == WGPUStatus_Success;
    std::vector<WGPUFeatureName> features = supportedFeatures(ctx.adapter);

    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    if (gotLimits) {
        desc.requiredLimits = &limits;
    }
    desc.requiredFeatureCount = features.size();
    desc.requiredFeatures = features.empty() ? nullptr : features.data();
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    desc.deviceLostCallbackInfo.callback = onDeviceLost;
    desc.deviceLostCallbackInfo.userdata1 = &ctx.lost;

    DeviceResult device = requestDeviceSync(ctx.instance, ctx.adapter, &desc);
    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        t.fail("device_lost/destroy: failed to request device: " + device.message);
    }
    ctx.device = device.device;
    ctx.queue = wgpuDeviceGetQueue(ctx.device);
}

void awaitLostIfRequested(Fixture& t, OwnedDeviceContext& ctx, bool awaitLost) {
    if (!awaitLost) {
        return;
    }
    if (!processEventsUntil(ctx.instance, [&ctx] { return ctx.lost.fired; })) {
        t.fail("device lost callback timed out");
    }
    t.expect(ctx.lost.reason == WGPUDeviceLostReason_Destroyed, "device lost reason is destroyed");
}

void expectValidationErrorOnDevice(
    Fixture& t,
    OwnedDeviceContext& ctx,
    const std::function<void()>& body,
    bool shouldError) {
    wgpuDevicePushErrorScope(ctx.device, WGPUErrorFilter_Validation);
    body();
    ScopeResult result = popErrorScopeSync(ctx.instance, ctx.device);
    if (result.status != WGPUPopErrorScopeStatus_Success) {
        t.fail("popErrorScope failed: " + result.message);
    }
    const bool hadError = result.type != WGPUErrorType_NoError;
    if (shouldError && !hadError) {
        t.fail("expected validation error, got none");
    }
    if (!shouldError && hadError) {
        t.fail("unexpected validation error: " + result.message);
    }
}

void executeAfterDestroy(
    Fixture& t,
    bool awaitLost,
    const std::function<void(OwnedDeviceContext&)>& setup,
    const std::function<void(OwnedDeviceContext&)>& body) {
    OwnedDeviceContext ctx;
    createOwnedDevice(t, ctx);
    setup(ctx);
    expectValidationErrorOnDevice(t, ctx, [&] { body(ctx); }, false);
    wgpuDeviceDestroy(ctx.device);
    awaitLostIfRequested(t, ctx, awaitLost);
    expectValidationErrorOnDevice(t, ctx, [&] { body(ctx); }, false);
}

void executeAfterDestroy(Fixture& t, bool awaitLost, const std::function<void(OwnedDeviceContext&)>& body) {
    executeAfterDestroy(t, awaitLost, [](OwnedDeviceContext&) {}, body);
}

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

std::vector<Value> boolValues() {
    return {true, false};
}

std::vector<Value> stringValues(std::initializer_list<const char*> values) {
    std::vector<Value> result;
    for (const char* value : values) {
        result.emplace_back(std::string(value));
    }
    return result;
}

std::vector<Value> formatValues(std::span<const WGPUTextureFormat> formats) {
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

std::vector<Value> bindingEntryValues() {
    std::vector<Value> values;
    for (std::string_view key : allBindingEntries(false)) {
        values.emplace_back(std::string(key));
    }
    return values;
}

std::vector<Value> renderableFormatValues() {
    return formatValues(kColorRenderableTextureFormats);
}

std::vector<Value> queryTypeValues() {
    return stringValues({"occlusion", "timestamp"});
}

std::vector<Value> canvasTypeValues() {
    return stringValues({"onscreen", "offscreen"});
}

std::vector<Value> canvasContextValues() {
    return stringValues({"2d", "webgpu"});
}

WGPUBufferUsage bufferUsageType(std::string_view usageType) {
    if (usageType == "MAP_READ") return WGPUBufferUsage_MapRead;
    if (usageType == "MAP_WRITE") return WGPUBufferUsage_MapWrite;
    if (usageType == "COPY_SRC") return WGPUBufferUsage_CopySrc;
    if (usageType == "COPY_DST") return WGPUBufferUsage_CopyDst;
    if (usageType == "INDEX") return WGPUBufferUsage_Index;
    if (usageType == "VERTEX") return WGPUBufferUsage_Vertex;
    if (usageType == "UNIFORM") return WGPUBufferUsage_Uniform;
    if (usageType == "STORAGE") return WGPUBufferUsage_Storage;
    if (usageType == "INDIRECT") return WGPUBufferUsage_Indirect;
    if (usageType == "QUERY_RESOLVE") return WGPUBufferUsage_QueryResolve;
    std::abort();
}

WGPUBufferUsage bufferUsageCopy(std::string_view usageCopy) {
    if (usageCopy == "COPY_NONE") return WGPUBufferUsage_None;
    if (usageCopy == "COPY_SRC") return WGPUBufferUsage_CopySrc;
    if (usageCopy == "COPY_DST") return WGPUBufferUsage_CopyDst;
    std::abort();
}

WGPUTextureUsage textureUsageType(std::string_view usageType) {
    if (usageType == "sampled") return WGPUTextureUsage_TextureBinding;
    if (usageType == "storage") return WGPUTextureUsage_StorageBinding;
    if (usageType == "render") return WGPUTextureUsage_RenderAttachment;
    std::abort();
}

WGPUTextureUsage textureUsageCopy(std::string_view usageCopy) {
    if (usageCopy == "COPY_NONE") return WGPUTextureUsage_None;
    if (usageCopy == "COPY_SRC") return WGPUTextureUsage_CopySrc;
    if (usageCopy == "COPY_DST") return WGPUTextureUsage_CopyDst;
    std::abort();
}

bool isPossiblyColorRenderable(WGPUTextureFormat format) {
    for (WGPUTextureFormat listed : kColorRenderableTextureFormats) {
        if (listed == format) {
            return true;
        }
    }
    return false;
}

bool isPossiblyStorageReadable(WGPUTextureFormat format) {
    for (WGPUTextureFormat listed : kStorageTextureFormats) {
        if (listed == format) {
            return true;
        }
    }
    for (WGPUTextureFormat listed : kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly) {
        if (listed == format) {
            return true;
        }
    }
    for (WGPUTextureFormat listed : kTextureFormatsTier2EnablesStorageReadWrite) {
        if (listed == format) {
            return true;
        }
    }
    return false;
}

bool deviceHasFeature(WGPUDevice device, WGPUFeatureName feature) {
    return feature == WGPUFeatureName_Force32 || wgpuDeviceHasFeature(device, feature) != 0;
}

void skipIfFormatNotSupported(Fixture& t, WGPUDevice device, WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    if (!deviceHasFeature(device, info.requiredFeature)) {
        t.skip(std::string(textureFormatIdentifier(format)) + " feature not supported");
    }
}

void skipIfRenderAttachmentUnsupported(Fixture& t, WGPUDevice device, WGPUTextureFormat format) {
    skipIfFormatNotSupported(t, device, format);
    if (!isPossiblyColorRenderable(format)) {
        t.skip(std::string(textureFormatIdentifier(format)) + " is not renderable");
    }
}

WGPUQueryType parseQueryType(std::string_view type) {
    if (type == "occlusion") return WGPUQueryType_Occlusion;
    if (type == "timestamp") return WGPUQueryType_Timestamp;
    std::abort();
}

void skipIfQueryTypeUnsupported(Fixture& t, WGPUDevice device, std::string_view type) {
    if (type == "timestamp" && !wgpuDeviceHasFeature(device, WGPUFeatureName_TimestampQuery)) {
        t.skip("timestamp-query feature not supported");
    }
}

WGPUBuffer createBuffer(OwnedDeviceContext& ctx, uint64_t size, WGPUBufferUsage usage, bool mappedAtCreation = false) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    desc.mappedAtCreation = mappedAtCreation ? WGPU_TRUE : WGPU_FALSE;
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(ctx.device, &desc);
    if (buffer != nullptr) ctx.handles.buffers.push_back(buffer);
    return buffer;
}

WGPUTexture createTexture(
    OwnedDeviceContext& ctx,
    WGPUTextureFormat format,
    WGPUTextureUsage usage,
    uint32_t width,
    uint32_t height,
    uint32_t sampleCount = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{width, height, 1};
    desc.format = format;
    desc.usage = usage;
    desc.sampleCount = sampleCount;
    WGPUTexture texture = wgpuDeviceCreateTexture(ctx.device, &desc);
    if (texture != nullptr) ctx.handles.textures.push_back(texture);
    return texture;
}

WGPUTextureView createView(OwnedDeviceContext& ctx, WGPUTexture texture, WGPUTextureFormat format) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.format = format;
    WGPUTextureView view = wgpuTextureCreateView(texture, &desc);
    if (view != nullptr) ctx.handles.textureViews.push_back(view);
    return view;
}

WGPUSampler createSampler(OwnedDeviceContext& ctx, bool nonFiltering = false, bool comparison = false) {
    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    if (nonFiltering) {
        desc.minFilter = WGPUFilterMode_Nearest;
        desc.magFilter = WGPUFilterMode_Nearest;
    }
    if (comparison) {
        desc.compare = WGPUCompareFunction_Less;
    }
    WGPUSampler sampler = wgpuDeviceCreateSampler(ctx.device, &desc);
    if (sampler != nullptr) ctx.handles.samplers.push_back(sampler);
    return sampler;
}

WGPUShaderModule createShaderModule(OwnedDeviceContext& ctx, std::string_view wgsl) {
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = WGPUStringView{wgsl.data(), wgsl.size()};
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(ctx.device, &desc);
    if (module != nullptr) ctx.handles.shaderModules.push_back(module);
    return module;
}

WGPUBindGroupLayout createBindGroupLayoutForEntry(OwnedDeviceContext& ctx, std::string_view key) {
    WGPUBindGroupLayoutEntry entry = bglEntryFromKey(key);
    entry.binding = 0;
    entry.visibility = validStagesForEntryKey(key);
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(ctx.device, &desc);
    if (layout != nullptr) ctx.handles.bindGroupLayouts.push_back(layout);
    return layout;
}

WGPUPipelineLayout createPipelineLayout(OwnedDeviceContext& ctx, WGPUBindGroupLayout bindGroupLayout) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 1;
    desc.bindGroupLayouts = &bindGroupLayout;
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(ctx.device, &desc);
    if (layout != nullptr) ctx.handles.pipelineLayouts.push_back(layout);
    return layout;
}

const char* noOpShaderCode(std::string_view stage) {
    if (stage == "VERTEX") {
        return "@vertex fn main() -> @builtin(position) vec4f { return vec4f(); }";
    }
    if (stage == "FRAGMENT") {
        return "@fragment fn main() -> @location(0) vec4f { return vec4f(); }";
    }
    if (stage == "COMPUTE") {
        return "@compute @workgroup_size(1) fn main() {}";
    }
    std::abort();
}

WGPUComputePipeline createComputePipeline(OwnedDeviceContext& ctx, WGPUShaderModule module, const char* entryPoint) {
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.compute.module = module;
    desc.compute.entryPoint = sv(entryPoint);
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(ctx.device, &desc);
    if (pipeline != nullptr) ctx.handles.computePipelines.push_back(pipeline);
    return pipeline;
}

WGPURenderPipeline createRenderPipeline(
    OwnedDeviceContext& ctx,
    WGPUShaderModule vertexModule,
    WGPUShaderModule fragmentModule,
    const char* fragmentEntryPoint) {
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_None;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = sv(fragmentEntryPoint);
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = sv("main");
    desc.fragment = &fragment;
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(ctx.device, &desc);
    if (pipeline != nullptr) ctx.handles.renderPipelines.push_back(pipeline);
    return pipeline;
}

WGPUCommandEncoder createCommandEncoder(OwnedDeviceContext& ctx) {
    WGPUCommandEncoderDescriptor desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device, &desc);
    if (encoder != nullptr) ctx.handles.commandEncoders.push_back(encoder);
    return encoder;
}

WGPUCommandBuffer finishCommandEncoder(OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
    WGPUCommandBufferDescriptor desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, &desc);
    if (commands != nullptr) ctx.handles.commandBuffers.push_back(commands);
    return commands;
}

WGPUQuerySet createQuerySet(OwnedDeviceContext& ctx, WGPUQueryType type, uint32_t count) {
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.type = type;
    desc.count = count;
    WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(ctx.device, &desc);
    if (querySet != nullptr) ctx.handles.querySets.push_back(querySet);
    return querySet;
}

WGPURenderBundleEncoder createRenderBundleEncoder(OwnedDeviceContext& ctx, WGPUTextureFormat format) {
    WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    desc.colorFormatCount = 1;
    desc.colorFormats = &format;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(ctx.device, &desc);
    if (encoder != nullptr) ctx.handles.renderBundleEncoders.push_back(encoder);
    return encoder;
}

WGPUTextureView createRenderAttachment(OwnedDeviceContext& ctx) {
    WGPUTexture texture = createTexture(
        ctx,
        WGPUTextureFormat_RGBA8Unorm,
        WGPUTextureUsage_RenderAttachment,
        1,
        1);
    return createView(ctx, texture, WGPUTextureFormat_RGBA8Unorm);
}

void withRenderPass(OwnedDeviceContext& ctx, WGPUCommandEncoder encoder, const std::function<void(WGPURenderPassEncoder)>& body) {
    WGPUTextureView view = createRenderAttachment(ctx);
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &desc);
    body(pass);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

void executeCommandsAfterDestroy(
    Fixture& t,
    std::string_view stage,
    bool awaitLost,
    const std::function<void(OwnedDeviceContext&)>& setup,
    const std::function<void(OwnedDeviceContext&, WGPUCommandEncoder)>& encode) {
    OwnedDeviceContext ctx;
    createOwnedDevice(t, ctx);
    setup(ctx);

    {
        WGPUCommandEncoder encoder = createCommandEncoder(ctx);
        encode(ctx, encoder);
        WGPUCommandBuffer commands = finishCommandEncoder(ctx, encoder);
        wgpuQueueSubmit(ctx.queue, 1, &commands);
    }

    if (stage == "finish") {
        WGPUCommandEncoder encoder = createCommandEncoder(ctx);
        encode(ctx, encoder);
        wgpuDeviceDestroy(ctx.device);
        awaitLostIfRequested(t, ctx, awaitLost);
        expectValidationErrorOnDevice(t, ctx, [&] { finishCommandEncoder(ctx, encoder); }, false);
        return;
    }

    if (stage == "submit") {
        WGPUCommandEncoder encoder = createCommandEncoder(ctx);
        encode(ctx, encoder);
        WGPUCommandBuffer commands = finishCommandEncoder(ctx, encoder);
        wgpuDeviceDestroy(ctx.device);
        awaitLostIfRequested(t, ctx, awaitLost);
        expectValidationErrorOnDevice(t, ctx, [&] { wgpuQueueSubmit(ctx.queue, 1, &commands); }, false);
        return;
    }

    std::abort();
}

WGPUBindGroupEntry bindGroupEntryForResource(OwnedDeviceContext& ctx, std::string_view resourceType) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;

    if (resourceType == "uniformBuf") {
        WGPUBuffer buffer = createBuffer(ctx, 16, WGPUBufferUsage_Uniform);
        entry.buffer = buffer;
        entry.size = 16;
        return entry;
    }
    if (resourceType == "storageBuf") {
        WGPUBuffer buffer = createBuffer(ctx, 16, WGPUBufferUsage_Storage);
        entry.buffer = buffer;
        entry.size = 16;
        return entry;
    }
    if (resourceType == "filtSamp") {
        entry.sampler = createSampler(ctx);
        return entry;
    }
    if (resourceType == "nonFiltSamp") {
        entry.sampler = createSampler(ctx, true);
        return entry;
    }
    if (resourceType == "compareSamp") {
        entry.sampler = createSampler(ctx, false, true);
        return entry;
    }
    if (resourceType == "sampledTex" || resourceType == "sampledTexMS") {
        const uint32_t sampleCount = resourceType == "sampledTexMS" ? 4 : 1;
        WGPUTexture texture = createTexture(
            ctx,
            WGPUTextureFormat_RGBA8Unorm,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment,
            1,
            1,
            sampleCount);
        entry.textureView = createView(ctx, texture, WGPUTextureFormat_RGBA8Unorm);
        return entry;
    }
    if (resourceType == "readonlyStorageTex" || resourceType == "writeonlyStorageTex" ||
        resourceType == "readwriteStorageTex") {
        WGPUTexture texture = createTexture(ctx, WGPUTextureFormat_R32Float, WGPUTextureUsage_StorageBinding, 1, 1);
        entry.textureView = createView(ctx, texture, WGPUTextureFormat_R32Float);
        return entry;
    }
    std::abort();
}

bool resourceMatchesEntry(std::string_view resourceType, std::string_view entryKey) {
    if (entryKey == "buffer_uniform") return resourceType == "uniformBuf";
    if (entryKey == "buffer_storage" || entryKey == "buffer_read-only-storage") return resourceType == "storageBuf";
    if (entryKey == "sampler_filtering") return resourceType == "filtSamp" || resourceType == "nonFiltSamp";
    if (entryKey == "sampler_non-filtering") return resourceType == "nonFiltSamp";
    if (entryKey == "sampler_comparison") return resourceType == "compareSamp";
    if (entryKey == "texture_ms-false") return resourceType == "sampledTex";
    if (entryKey == "texture_ms-true") return resourceType == "sampledTexMS";
    if (entryKey == "storageTexture_read-only") return resourceType == "readonlyStorageTex";
    if (entryKey == "storageTexture_write-only") return resourceType == "writeonlyStorageTex";
    if (entryKey == "storageTexture_read-write") return resourceType == "readwriteStorageTex";
    std::abort();
}

TestGroup<Fixture> g = MakeTestGroup<Fixture>(
    "api,validation,state,device_lost,destroy",
    R"(
Tests for device lost induced via destroy.
  - Tests that prior to device destruction, valid APIs do not generate errors (control case).
  - After device destruction, runs the same APIs. No expected observable results, so test crash or future failures are the only current failure indicators.
)");

CTS_TEST(g, "createBuffer")
    .desc("Tests creating buffers on destroyed device. Tests valid combinations of various usages and mappedAtCreation.")
    .params([](ParamsBuilder u) {
        return u.combine("usageType", stringValues({"MAP_READ", "MAP_WRITE", "COPY_SRC", "COPY_DST", "INDEX", "VERTEX", "UNIFORM", "STORAGE", "INDIRECT", "QUERY_RESOLVE"}))
            .combine("usageCopy", stringValues({"COPY_NONE", "COPY_SRC", "COPY_DST"}))
            .combine("awaitLost", boolValues())
            .filter([](const ParamRecord& p) {
                const std::string usageType = valueAs<std::string>(*findParam(p, "usageType"));
                const std::string usageCopy = valueAs<std::string>(*findParam(p, "usageCopy"));
                if (usageType == "COPY_SRC" || usageType == "COPY_DST") return false;
                if (usageType == "MAP_READ") return usageCopy == "COPY_NONE" || usageCopy == "COPY_DST";
                if (usageType == "MAP_WRITE") return usageCopy == "COPY_NONE" || usageCopy == "COPY_SRC";
                return true;
            })
            .combine("mappedAtCreation", boolValues());
    })
    .fn([](Fixture& t) {
        const std::string usageType = t.param<std::string>("usageType");
        const std::string usageCopy = t.param<std::string>("usageCopy");
        const bool awaitLost = t.param<bool>("awaitLost");
        const bool mappedAtCreation = t.param<bool>("mappedAtCreation");
        executeAfterDestroy(t, awaitLost, [&](OwnedDeviceContext& ctx) {
            createBuffer(ctx, 16, bufferUsageType(usageType) | bufferUsageCopy(usageCopy), mappedAtCreation);
        });
    });

CTS_TEST(g, "createTexture,2d,uncompressed_format")
    .desc("Tests creating 2d uncompressed textures on destroyed device.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kRegularTextureFormats))
            .combine("usageType", stringValues({"sampled", "storage", "render"}))
            .combine("usageCopy", stringValues({"COPY_NONE", "COPY_SRC", "COPY_DST"}))
            .combine("awaitLost", boolValues())
            .filter([](const ParamRecord& p) {
                const WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                const std::string usageType = valueAs<std::string>(*findParam(p, "usageType"));
                return !((!isPossiblyColorRenderable(format) && usageType == "render") ||
                         (!isPossiblyStorageReadable(format) && usageType == "storage"));
            });
    })
    .fn([](Fixture& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string usageType = t.param<std::string>("usageType");
        const std::string usageCopy = t.param<std::string>("usageCopy");
        const bool awaitLost = t.param<bool>("awaitLost");
        executeAfterDestroy(t, awaitLost, [&](OwnedDeviceContext& ctx) {
            skipIfFormatNotSupported(t, ctx.device, format);
            const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
            createTexture(ctx, format, textureUsageType(usageType) | textureUsageCopy(usageCopy), info.blockWidth, info.blockHeight);
        });
    });

CTS_TEST(g, "createTexture,2d,compressed_format")
    .desc("Tests creating 2d compressed textures on destroyed device.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kCompressedTextureFormats))
            .combine("usageType", stringValues({"sampled", "storage", "render"}))
            .combine("usageCopy", stringValues({"COPY_NONE", "COPY_SRC", "COPY_DST"}))
            .combine("awaitLost", boolValues())
            .filter([](const ParamRecord& p) {
                const WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                const std::string usageType = valueAs<std::string>(*findParam(p, "usageType"));
                return !((!isPossiblyColorRenderable(format) && usageType == "render") ||
                         (!isPossiblyStorageReadable(format) && usageType == "storage"));
            });
    })
    .fn([](Fixture& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string usageType = t.param<std::string>("usageType");
        const std::string usageCopy = t.param<std::string>("usageCopy");
        const bool awaitLost = t.param<bool>("awaitLost");
        executeAfterDestroy(t, awaitLost, [&](OwnedDeviceContext& ctx) {
            skipIfFormatNotSupported(t, ctx.device, format);
            const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
            createTexture(ctx, format, textureUsageType(usageType) | textureUsageCopy(usageCopy), info.blockWidth, info.blockHeight);
        });
    });

CTS_TEST(g, "createView,2d,uncompressed_format")
    .desc("Tests creating texture views on 2d uncompressed textures from destroyed device.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kRegularTextureFormats))
            .combine("usageType", stringValues({"sampled", "storage", "render"}))
            .combine("usageCopy", stringValues({"COPY_NONE", "COPY_SRC", "COPY_DST"}))
            .combine("awaitLost", boolValues())
            .filter([](const ParamRecord& p) {
                const WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                const std::string usageType = valueAs<std::string>(*findParam(p, "usageType"));
                return !((!isPossiblyColorRenderable(format) && usageType == "render") ||
                         (!isPossiblyStorageReadable(format) && usageType == "storage"));
            });
    })
    .fn([](Fixture& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string usageType = t.param<std::string>("usageType");
        const std::string usageCopy = t.param<std::string>("usageCopy");
        const bool awaitLost = t.param<bool>("awaitLost");
        executeAfterDestroy(t, awaitLost, [&](OwnedDeviceContext& ctx) {
            skipIfFormatNotSupported(t, ctx.device, format);
            const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
            WGPUTexture texture = createTexture(ctx, format, textureUsageType(usageType) | textureUsageCopy(usageCopy), info.blockWidth, info.blockHeight);
            createView(ctx, texture, format);
        }, [&](OwnedDeviceContext& ctx) {
            createView(ctx, ctx.handles.textures.front(), format);
        });
    });

CTS_TEST(g, "createView,2d,compressed_format")
    .desc("Tests creating texture views on 2d compressed textures from destroyed device.")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues(kCompressedTextureFormats))
            .combine("usageType", stringValues({"sampled", "storage", "render"}))
            .combine("usageCopy", stringValues({"COPY_NONE", "COPY_SRC", "COPY_DST"}))
            .combine("awaitLost", boolValues())
            .filter([](const ParamRecord& p) {
                const WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                const std::string usageType = valueAs<std::string>(*findParam(p, "usageType"));
                return !((!isPossiblyColorRenderable(format) && usageType == "render") ||
                         (!isPossiblyStorageReadable(format) && usageType == "storage"));
            });
    })
    .fn([](Fixture& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string usageType = t.param<std::string>("usageType");
        const std::string usageCopy = t.param<std::string>("usageCopy");
        const bool awaitLost = t.param<bool>("awaitLost");
        executeAfterDestroy(t, awaitLost, [&](OwnedDeviceContext& ctx) {
            skipIfFormatNotSupported(t, ctx.device, format);
            const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
            WGPUTexture texture = createTexture(ctx, format, textureUsageType(usageType) | textureUsageCopy(usageCopy), info.blockWidth, info.blockHeight);
            createView(ctx, texture, format);
        }, [&](OwnedDeviceContext& ctx) {
            createView(ctx, ctx.handles.textures.front(), format);
        });
    });

CTS_TEST(g, "createSampler")
    .desc("Tests creating samplers on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            createSampler(ctx);
        });
    });

CTS_TEST(g, "createBindGroupLayout")
    .desc("Tests creating bind group layouts on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("entry", bindingEntryValues()).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const std::string entry = t.param<std::string>("entry");
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            createBindGroupLayoutForEntry(ctx, entry);
        });
    });

CTS_TEST(g, "createBindGroup")
    .desc("A destroyed device should not be able to create any valid bind groups.")
    .params([](ParamsBuilder u) {
        return u.combine("resourceType", stringValues({"uniformBuf", "storageBuf", "filtSamp", "nonFiltSamp", "compareSamp", "sampledTex", "sampledTexMS", "readonlyStorageTex", "writeonlyStorageTex", "readwriteStorageTex"}))
            .combine("entry", bindingEntryValues())
            .filter([](const ParamRecord& p) {
                return resourceMatchesEntry(
                    valueAs<std::string>(*findParam(p, "resourceType")),
                    valueAs<std::string>(*findParam(p, "entry")));
            })
            .combine("awaitLost", boolValues());
    })
    .fn([](Fixture& t) {
        const std::string resourceType = t.param<std::string>("resourceType");
        const std::string entry = t.param<std::string>("entry");
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            WGPUBindGroupLayout layout = createBindGroupLayoutForEntry(ctx, entry);
            WGPUBindGroupEntry bgEntry = bindGroupEntryForResource(ctx, resourceType);
            WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            desc.layout = layout;
            desc.entryCount = 1;
            desc.entries = &bgEntry;
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &desc);
            if (bindGroup != nullptr) ctx.handles.bindGroups.push_back(bindGroup);
        }, [&](OwnedDeviceContext& ctx) {
            WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            bgEntry.binding = 0;
            if (resourceType == "uniformBuf" || resourceType == "storageBuf") {
                bgEntry.buffer = ctx.handles.buffers.front();
                bgEntry.size = 16;
            } else if (resourceType == "filtSamp" || resourceType == "nonFiltSamp" || resourceType == "compareSamp") {
                bgEntry.sampler = ctx.handles.samplers.front();
            } else {
                bgEntry.textureView = ctx.handles.textureViews.front();
            }
            WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            desc.layout = ctx.handles.bindGroupLayouts.front();
            desc.entryCount = 1;
            desc.entries = &bgEntry;
            WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device, &desc);
            if (bindGroup != nullptr) ctx.handles.bindGroups.push_back(bindGroup);
        });
    });

CTS_TEST(g, "createPipelineLayout")
    .desc("Tests creating pipeline layouts on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("entry", bindingEntryValues()).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const std::string entry = t.param<std::string>("entry");
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            WGPUBindGroupLayout layout = createBindGroupLayoutForEntry(ctx, entry);
            createPipelineLayout(ctx, layout);
        }, [](OwnedDeviceContext& ctx) {
            createPipelineLayout(ctx, ctx.handles.bindGroupLayouts.front());
        });
    });

CTS_TEST(g, "createShaderModule")
    .desc("Tests creating shader modules on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"VERTEX", "FRAGMENT", "COMPUTE"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const std::string stage = t.param<std::string>("stage");
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            createShaderModule(ctx, noOpShaderCode(stage));
        });
    });

CTS_TEST(g, "createComputePipeline")
    .desc("Tests creating compute pipeline on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            WGPUShaderModule module = createShaderModule(ctx, noOpShaderCode("COMPUTE"));
            createComputePipeline(ctx, module, "main");
        }, [](OwnedDeviceContext& ctx) {
            createComputePipeline(ctx, ctx.handles.shaderModules.front(), "main");
        });
    });

CTS_TEST(g, "createRenderPipeline")
    .desc("Tests creating render pipeline on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            WGPUShaderModule vertex = createShaderModule(ctx, noOpShaderCode("VERTEX"));
            WGPUShaderModule fragment = createShaderModule(ctx, noOpShaderCode("FRAGMENT"));
            createRenderPipeline(ctx, vertex, fragment, "main");
        }, [](OwnedDeviceContext& ctx) {
            createRenderPipeline(ctx, ctx.handles.shaderModules[0], ctx.handles.shaderModules[1], "main");
        });
    });

CTS_TEST(g, "createComputePipelineAsync")
    .desc("Tests creating a pipeline asynchronously while destroying the device and on a destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("valid", boolValues()).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const bool valid = t.param<bool>("valid");
        OwnedDeviceContext ctx;
        createOwnedDevice(t, ctx);
        WGPUShaderModule module = createShaderModule(ctx, noOpShaderCode("COMPUTE"));
        // Native has no portable async pipeline wrapper here; use the same sync create path.
        expectValidationErrorOnDevice(t, ctx, [&] {
            createComputePipeline(ctx, module, valid ? "main" : "does_not_exist");
        }, !valid);
        wgpuDeviceDestroy(ctx.device);
        awaitLostIfRequested(t, ctx, t.param<bool>("awaitLost"));
        expectValidationErrorOnDevice(t, ctx, [&] {
            createComputePipeline(ctx, module, valid ? "main" : "does_not_exist");
        }, false);
    });

CTS_TEST(g, "createRenderPipelineAsync")
    .desc("Tests creating a pipeline asynchronously while destroying the device and on a destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("valid", boolValues()).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const bool valid = t.param<bool>("valid");
        OwnedDeviceContext ctx;
        createOwnedDevice(t, ctx);
        WGPUShaderModule vertex = createShaderModule(ctx, noOpShaderCode("VERTEX"));
        WGPUShaderModule fragment = createShaderModule(ctx, noOpShaderCode("FRAGMENT"));
        // Native has no portable async pipeline wrapper here; use the same sync create path.
        expectValidationErrorOnDevice(t, ctx, [&] {
            createRenderPipeline(ctx, vertex, fragment, valid ? "main" : "does_not_exist");
        }, !valid);
        wgpuDeviceDestroy(ctx.device);
        awaitLostIfRequested(t, ctx, t.param<bool>("awaitLost"));
        expectValidationErrorOnDevice(t, ctx, [&] {
            createRenderPipeline(ctx, vertex, fragment, valid ? "main" : "does_not_exist");
        }, false);
    });

CTS_TEST(g, "createCommandEncoder")
    .desc("Tests creating command encoders on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            createCommandEncoder(ctx);
        });
    });

CTS_TEST(g, "createRenderBundleEncoder")
    .desc("Tests creating render bundle encoders on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("format", renderableFormatValues()).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            skipIfRenderAttachmentUnsupported(t, ctx.device, format);
            createRenderBundleEncoder(ctx, format);
        });
    });

CTS_TEST(g, "createQuerySet")
    .desc("Tests creating query sets on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("type", queryTypeValues()).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const std::string type = t.param<std::string>("type");
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            skipIfQueryTypeUnsupported(t, ctx.device, type);
            createQuerySet(ctx, parseQueryType(type), 4);
        });
    });

CTS_TEST(g, "importExternalTexture")
    .desc("Tests import external texture on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("sourceType", stringValues({"VideoElement", "VideoFrame"})).combine("awaitLost", boolValues()); })
    .unimplemented();

CTS_TEST(g, "command,copyBufferToBuffer")
    .desc("Tests copyBufferToBuffer command with various uncompressed formats on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            createBuffer(ctx, 16, WGPUBufferUsage_CopySrc);
            createBuffer(ctx, 16, WGPUBufferUsage_CopyDst);
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            wgpuCommandEncoderCopyBufferToBuffer(encoder, ctx.handles.buffers[0], 0, ctx.handles.buffers[1], 0, 16);
        });
    });

CTS_TEST(g, "command,copyBufferToTexture")
    .desc("Tests copyBufferToTexture command on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            createBuffer(ctx, 16, WGPUBufferUsage_CopySrc);
            createTexture(ctx, WGPUTextureFormat_RGBA32Uint, WGPUTextureUsage_CopyDst, 1, 1);
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            WGPUTexelCopyBufferInfo src = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            src.buffer = ctx.handles.buffers[0];
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = ctx.handles.textures[0];
            WGPUExtent3D size{1, 1, 1};
            wgpuCommandEncoderCopyBufferToTexture(encoder, &src, &dst, &size);
        });
    });

CTS_TEST(g, "command,copyTextureToBuffer")
    .desc("Tests copyTextureToBuffer command on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            createTexture(ctx, WGPUTextureFormat_RGBA32Uint, WGPUTextureUsage_CopySrc, 1, 1);
            createBuffer(ctx, 16, WGPUBufferUsage_CopyDst);
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = ctx.handles.textures[0];
            WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            dst.buffer = ctx.handles.buffers[0];
            WGPUExtent3D size{1, 1, 1};
            wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &size);
        });
    });

CTS_TEST(g, "command,copyTextureToTexture")
    .desc("Tests copyTextureToTexture command on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            createTexture(ctx, WGPUTextureFormat_RGBA32Uint, WGPUTextureUsage_CopySrc, 1, 1);
            createTexture(ctx, WGPUTextureFormat_RGBA32Uint, WGPUTextureUsage_CopyDst, 1, 1);
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture = ctx.handles.textures[0];
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = ctx.handles.textures[1];
            WGPUExtent3D size{1, 1, 1};
            wgpuCommandEncoderCopyTextureToTexture(encoder, &src, &dst, &size);
        });
    });

CTS_TEST(g, "command,clearBuffer")
    .desc("Tests encoding and finishing a clearBuffer command on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            createBuffer(ctx, 16, WGPUBufferUsage_CopyDst);
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            wgpuCommandEncoderClearBuffer(encoder, ctx.handles.buffers[0], 0, 16);
        });
    });

CTS_TEST(g, "command,resolveQuerySet")
    .desc("Tests encoding and finishing a resolveQuerySet command on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            createQuerySet(ctx, WGPUQueryType_Occlusion, 2);
            createBuffer(ctx, 16, WGPUBufferUsage_QueryResolve);
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            wgpuCommandEncoderResolveQuerySet(encoder, ctx.handles.querySets[0], 0, 1, ctx.handles.buffers[0], 0);
        });
    });

CTS_TEST(g, "command,computePass,dispatch")
    .desc("Tests encoding and dispatching a simple valid compute pass on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            WGPUShaderModule module = createShaderModule(ctx, noOpShaderCode("COMPUTE"));
            createComputePipeline(ctx, module, "main");
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            WGPUComputePassDescriptor desc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &desc);
            wgpuComputePassEncoderSetPipeline(pass, ctx.handles.computePipelines[0]);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
            wgpuComputePassEncoderRelease(pass);
        });
    });

CTS_TEST(g, "command,renderPass,draw")
    .desc("Tests encoding and finishing a simple valid render pass on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            WGPUShaderModule vertex = createShaderModule(ctx, noOpShaderCode("VERTEX"));
            WGPUShaderModule fragment = createShaderModule(ctx, noOpShaderCode("FRAGMENT"));
            createRenderPipeline(ctx, vertex, fragment, "main");
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            withRenderPass(ctx, encoder, [&](WGPURenderPassEncoder pass) {
                wgpuRenderPassEncoderSetPipeline(pass, ctx.handles.renderPipelines[0]);
                wgpuRenderPassEncoderDraw(pass, 0, 1, 0, 0);
            });
        });
    });

CTS_TEST(g, "command,renderPass,renderBundle")
    .desc("Tests encoding and drawing a render pass including a render bundle on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("stage", stringValues({"finish", "submit"})).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        executeCommandsAfterDestroy(t, t.param<std::string>("stage"), t.param<bool>("awaitLost"), [](OwnedDeviceContext& ctx) {
            WGPUShaderModule vertex = createShaderModule(ctx, noOpShaderCode("VERTEX"));
            WGPUShaderModule fragment = createShaderModule(ctx, noOpShaderCode("FRAGMENT"));
            WGPURenderPipeline pipeline = createRenderPipeline(ctx, vertex, fragment, "main");
            WGPURenderBundleEncoder bundleEncoder = createRenderBundleEncoder(ctx, WGPUTextureFormat_RGBA8Unorm);
            wgpuRenderBundleEncoderSetPipeline(bundleEncoder, pipeline);
            wgpuRenderBundleEncoderDraw(bundleEncoder, 0, 1, 0, 0);
            WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEncoder, nullptr);
            if (bundle != nullptr) ctx.handles.renderBundles.push_back(bundle);
        }, [](OwnedDeviceContext& ctx, WGPUCommandEncoder encoder) {
            withRenderPass(ctx, encoder, [&](WGPURenderPassEncoder pass) {
                WGPURenderBundle bundle = ctx.handles.renderBundles[0];
                wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
                wgpuRenderPassEncoderDraw(pass, 0, 1, 0, 0);
            });
        });
    });

CTS_TEST(g, "queue,writeBuffer")
    .desc("Tests writeBuffer on queue on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("numElements", {4, 8, 16}).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const uint64_t numElements = t.param<uint64_t>("numElements");
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            WGPUBuffer buffer = createBuffer(ctx, numElements, WGPUBufferUsage_CopyDst);
            std::vector<uint8_t> data(static_cast<size_t>(numElements));
            wgpuQueueWriteBuffer(ctx.queue, buffer, 0, data.data(), data.size());
        }, [&](OwnedDeviceContext& ctx) {
            std::vector<uint8_t> data(static_cast<size_t>(numElements));
            wgpuQueueWriteBuffer(ctx.queue, ctx.handles.buffers.front(), 0, data.data(), data.size());
        });
    });

CTS_TEST(g, "queue,writeTexture,2d,uncompressed_format")
    .desc("Tests writeTexture on queue on destroyed device with uncompressed formats.")
    .params([](ParamsBuilder u) { return u.combine("format", formatValues(kRegularTextureFormats)).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            skipIfFormatNotSupported(t, ctx.device, format);
            const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
            const uint32_t bytesPerBlock = info.bytesPerBlock == 0 ? 4 : info.bytesPerBlock;
            WGPUTexture texture = createTexture(ctx, format, WGPUTextureUsage_CopyDst, info.blockWidth, info.blockHeight);
            std::vector<uint8_t> data(bytesPerBlock);
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = texture;
            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            WGPUExtent3D size{info.blockWidth, info.blockHeight, 1};
            wgpuQueueWriteTexture(ctx.queue, &dst, data.data(), data.size(), &layout, &size);
        }, [&](OwnedDeviceContext& ctx) {
            const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
            const uint32_t bytesPerBlock = info.bytesPerBlock == 0 ? 4 : info.bytesPerBlock;
            std::vector<uint8_t> data(bytesPerBlock);
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = ctx.handles.textures.front();
            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            WGPUExtent3D size{info.blockWidth, info.blockHeight, 1};
            wgpuQueueWriteTexture(ctx.queue, &dst, data.data(), data.size(), &layout, &size);
        });
    });

CTS_TEST(g, "queue,writeTexture,2d,compressed_format")
    .desc("Tests writeTexture on queue on destroyed device with compressed formats.")
    .params([](ParamsBuilder u) { return u.combine("format", formatValues(kCompressedTextureFormats)).combine("awaitLost", boolValues()); })
    .fn([](Fixture& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        executeAfterDestroy(t, t.param<bool>("awaitLost"), [&](OwnedDeviceContext& ctx) {
            skipIfFormatNotSupported(t, ctx.device, format);
            const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
            WGPUTexture texture = createTexture(ctx, format, WGPUTextureUsage_CopyDst, info.blockWidth, info.blockHeight);
            std::vector<uint8_t> data(info.bytesPerBlock);
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = texture;
            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            WGPUExtent3D size{info.blockWidth, info.blockHeight, 1};
            wgpuQueueWriteTexture(ctx.queue, &dst, data.data(), data.size(), &layout, &size);
        }, [&](OwnedDeviceContext& ctx) {
            const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
            std::vector<uint8_t> data(info.bytesPerBlock);
            WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            dst.texture = ctx.handles.textures.front();
            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            WGPUExtent3D size{info.blockWidth, info.blockHeight, 1};
            wgpuQueueWriteTexture(ctx.queue, &dst, data.data(), data.size(), &layout, &size);
        });
    });

CTS_TEST(g, "queue,copyExternalImageToTexture,canvas")
    .desc("Tests copyExternalImageToTexture from canvas on queue on destroyed device.")
    .params([](ParamsBuilder u) {
        return u.combine("canvasType", canvasTypeValues())
            .combine("contextType", canvasContextValues())
            .combine("awaitLost", boolValues());
    })
    .unimplemented();

CTS_TEST(g, "queue,copyExternalImageToTexture,imageBitmap")
    .desc("Tests copyExternalImageToTexture from canvas on queue on destroyed device.")
    .params([](ParamsBuilder u) { return u.combine("awaitLost", boolValues()); })
    .unimplemented();

} // namespace
