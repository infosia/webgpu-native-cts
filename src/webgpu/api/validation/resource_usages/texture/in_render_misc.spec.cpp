// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/resource_usages/texture/in_render_misc.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

constexpr uint32_t kTextureSize = 16;
constexpr uint32_t kTextureLayers = 3;

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,resource_usages,texture,in_render_misc",
    R"(
Texture Usages Validation Tests on All Kinds of WebGPU Subresource Usage Scopes.
)");

WGPUStringView sv(const char* text) {
    return WGPUStringView{text, WGPU_STRLEN};
}

std::vector<Value> textureBindingTypes() {
    return {
        std::string("sampled-texture"),
        std::string("writeonly-storage-texture"),
        std::string("readonly-storage-texture"),
        std::string("readwrite-storage-texture"),
    };
}

std::vector<Value> copyPassAndTextureBindingTypes() {
    std::vector<Value> values = {std::string("copy-src"), std::string("copy-dst"), std::string("color-attachment")};
    std::vector<Value> bindings = textureBindingTypes();
    values.insert(values.end(), bindings.begin(), bindings.end());
    return values;
}

std::vector<Value> textureUsageValuesWithZero() {
    std::vector<Value> values = {0};
    for (WGPUTextureUsage usage : kTextureUsages) {
        values.emplace_back(static_cast<uint64_t>(usage));
    }
    return values;
}

bool isReadOnlyTextureBindingType(const std::string& type) {
    return type == "sampled-texture" || type == "readonly-storage-texture";
}

bool isStorageTextureType(const std::string& type) {
    return type == "writeonly-storage-texture" || type == "readonly-storage-texture" ||
           type == "readwrite-storage-texture";
}

void skipIfStorageTextureUnsupported(AllFeaturesMaxLimitsGpuTest& t, const std::string& type) {
    if (isStorageTextureType(type) &&
        !t.isTextureFormatUsableWithStorageAccessMode(WGPUTextureFormat_R32Float,
                                                       type == "readonly-storage-texture" ? WGPUStorageTextureAccess_ReadOnly :
                                                       type == "readwrite-storage-texture" ? WGPUStorageTextureAccess_ReadWrite :
                                                                                             WGPUStorageTextureAccess_WriteOnly)) {
        t.skip("r32float does not support requested storage access");
    }
}

WGPUTextureAspect aspectFor(const std::string& aspect) {
    if (aspect == "depth-only") return WGPUTextureAspect_DepthOnly;
    if (aspect == "stencil-only") return WGPUTextureAspect_StencilOnly;
    std::abort();
}

WGPUTextureSampleType sampleTypeFor(const std::string& sampleType) {
    if (sampleType == "unfilterable-float") return WGPUTextureSampleType_UnfilterableFloat;
    if (sampleType == "depth") return WGPUTextureSampleType_Depth;
    if (sampleType == "uint") return WGPUTextureSampleType_Uint;
    std::abort();
}

WGPUStorageTextureAccess storageAccessFor(const std::string& type) {
    if (type == "writeonly-storage-texture") return WGPUStorageTextureAccess_WriteOnly;
    if (type == "readonly-storage-texture") return WGPUStorageTextureAccess_ReadOnly;
    if (type == "readwrite-storage-texture") return WGPUStorageTextureAccess_ReadWrite;
    std::abort();
}

WGPUTexture createTexture(AllFeaturesMaxLimitsGpuTest& t,
                          WGPUTextureFormat format,
                          WGPUTextureUsage usage,
                          uint32_t layers = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kTextureSize, kTextureSize, layers};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = usage;
    return t.createTextureTracked(desc);
}

WGPUTextureView createView(AllFeaturesMaxLimitsGpuTest& t,
                           WGPUTexture texture,
                           WGPUTextureViewDimension dimension = WGPUTextureViewDimension_2DArray,
                           uint32_t baseLayer = 0,
                           uint32_t layerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED,
                           WGPUTextureAspect aspect = WGPUTextureAspect_All,
                           WGPUTextureUsage usage = WGPUTextureUsage_None) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.dimension = dimension;
    desc.baseArrayLayer = baseLayer;
    desc.arrayLayerCount = layerCount;
    desc.baseMipLevel = 0;
    desc.mipLevelCount = 1;
    desc.aspect = aspect;
    desc.usage = usage;
    return t.createViewTracked(texture, desc);
}

WGPURenderPassColorAttachment colorAttachment(WGPUTextureView view) {
    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    attachment.view = view;
    attachment.loadOp = WGPULoadOp_Load;
    attachment.storeOp = WGPUStoreOp_Store;
    return attachment;
}

WGPUBindGroupLayout createBindGroupLayoutForTest(AllFeaturesMaxLimitsGpuTest& t,
                                                 const std::string& usage,
                                                 const std::string& sampleType,
                                                 WGPUShaderStage visibility = WGPUShaderStage_Fragment) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = 0;
    entry.visibility = visibility;
    if (usage == "sampled-texture") {
        entry.texture.sampleType = sampleTypeFor(sampleType);
        entry.texture.viewDimension = WGPUTextureViewDimension_2DArray;
    } else {
        entry.storageTexture.access = storageAccessFor(usage);
        entry.storageTexture.format = WGPUTextureFormat_R32Float;
        entry.storageTexture.viewDimension = WGPUTextureViewDimension_2DArray;
    }
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBindGroup createBindGroupForTest(AllFeaturesMaxLimitsGpuTest& t,
                                     WGPUTextureView view,
                                     const std::string& usage,
                                     const std::string& sampleType,
                                     WGPUShaderStage visibility = WGPUShaderStage_Fragment) {
    WGPUBindGroupLayout layout = createBindGroupLayoutForTest(t, usage, sampleType, visibility);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.textureView = view;
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupTracked(desc);
}

WGPUPipelineLayout pipelineLayout(AllFeaturesMaxLimitsGpuTest& t, const std::vector<WGPUBindGroupLayout>& layouts) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.empty() ? nullptr : layouts.data();
    return t.createPipelineLayoutTracked(desc);
}

WGPUComputePipeline createComputePipeline(AllFeaturesMaxLimitsGpuTest& t,
                                          WGPUPipelineLayout layout,
                                          std::string_view wgsl) {
    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

WGPURenderPipeline createRenderPipeline(AllFeaturesMaxLimitsGpuTest& t,
                                        WGPUPipelineLayout layout,
                                        std::string_view fragmentCode) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(
        "@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(); }");
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentCode);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_R32Float;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = sv("main");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = sv("main");
    desc.fragment = &fragment;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    return t.createRenderPipelineTracked(desc);
}

WGPURenderPassEncoder beginRenderPass(WGPUCommandEncoder encoder,
                                      const std::vector<WGPURenderPassColorAttachment>& colors,
                                      const WGPURenderPassDepthStencilAttachment* ds = nullptr) {
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = colors.size();
    desc.colorAttachments = colors.empty() ? nullptr : colors.data();
    desc.depthStencilAttachment = ds;
    return wgpuCommandEncoderBeginRenderPass(encoder, &desc);
}

CTS_TEST(testGroup, "subresources,set_bind_group_on_same_index_color_texture")
    .desc(R"(
  Test that when one color texture subresource is bound to different bind groups, whether the bind
  groups are reset by another compatible ones or not, its list of internal usages within one usage
  scope can only be a compatible usage list.)")
    .params([](ParamsBuilder u) {
        return u.combine("useDifferentTextureAsTexture2", {true, false})
            .combine("baseLayer2", {0, 1})
            .combine("view1Binding", textureBindingTypes())
            .combine("view2Binding", textureBindingTypes());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool useDifferentTexture = t.param<bool>("useDifferentTextureAsTexture2");
        const uint32_t baseLayer2 = static_cast<uint32_t>(t.param<int>("baseLayer2"));
        const std::string view1Binding = t.param<std::string>("view1Binding");
        const std::string view2Binding = t.param<std::string>("view2Binding");
        skipIfStorageTextureUnsupported(t, view1Binding);
        skipIfStorageTextureUnsupported(t, view2Binding);

        WGPUTexture texture0 = createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding, kTextureLayers);
        WGPUTextureView textureView0 = createView(t, texture0, WGPUTextureViewDimension_2DArray, 0, 1);
        WGPUBindGroup bg0 = createBindGroupForTest(t, textureView0, view1Binding, "unfilterable-float");
        WGPUBindGroup bg1 = createBindGroupForTest(t, textureView0, view2Binding, "unfilterable-float");
        WGPUTexture texture2 = useDifferentTexture ? createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding, kTextureLayers) : texture0;
        WGPUTextureView textureView2 = createView(t, texture2, WGPUTextureViewDimension_2DArray, baseLayer2, kTextureLayers - baseLayer2);
        WGPUBindGroup validBg2 = createBindGroupForTest(t, textureView2, view2Binding, "unfilterable-float");

        WGPUTexture unused = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_RenderAttachment);
        WGPURenderPassColorAttachment ca = colorAttachment(createView(t, unused, WGPUTextureViewDimension_2D, 0, 1));
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, bg1, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, validBg2, 0, nullptr);
        wgpuRenderPassEncoderEnd(pass);

        const bool noConflict = (isReadOnlyTextureBindingType(view1Binding) && isReadOnlyTextureBindingType(view2Binding)) ||
                                view1Binding == view2Binding;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !noConflict);
    });

CTS_TEST(testGroup, "subresources,set_bind_group_on_same_index_depth_stencil_texture")
    .desc(R"(
  Test that when one depth stencil texture subresource is bound to different bind groups, whether
  the bind groups are reset by another compatible ones or not, its list of internal usages within
  one usage scope can only be a compatible usage list.)")
    .params([](ParamsBuilder u) {
        return u.combine("bindAspect", {std::string("depth-only"), std::string("stencil-only")})
            .combine("depthStencilReadOnly", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string bindAspect = t.param<std::string>("bindAspect");
        const bool readOnly = t.param<bool>("depthStencilReadOnly");
        WGPUTexture dsTexture = createTexture(t, WGPUTextureFormat_Depth24PlusStencil8,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment);
        WGPUBindGroup conflicted = createBindGroupForTest(t,
            createView(t, dsTexture, WGPUTextureViewDimension_2DArray, 0, WGPU_ARRAY_LAYER_COUNT_UNDEFINED, aspectFor(bindAspect)),
            "sampled-texture", bindAspect == "depth-only" ? "depth" : "uint");
        WGPUTexture colorTex = createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding);
        WGPUBindGroup validBg = createBindGroupForTest(t, createView(t, colorTex), "sampled-texture", "unfilterable-float");
        WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        ds.view = createView(t, dsTexture, WGPUTextureViewDimension_2D, 0, 1);
        ds.depthReadOnly = readOnly;
        ds.stencilReadOnly = readOnly;
        if (!readOnly) {
            ds.depthLoadOp = WGPULoadOp_Load;
            ds.depthStoreOp = WGPUStoreOp_Store;
            ds.stencilLoadOp = WGPULoadOp_Load;
            ds.stencilStoreOp = WGPUStoreOp_Store;
        }
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        std::vector<WGPURenderPassColorAttachment> empty;
        WGPURenderPassEncoder pass = beginRenderPass(encoder, empty, &ds);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, conflicted, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, validBg, 0, nullptr);
        wgpuRenderPassEncoderEnd(pass);
        t.expectValidationError([&] { t.finishTracked(encoder); }, !readOnly);
    });

std::string shaderForUsage(const std::string& usage, bool render) {
    if (render) {
        if (usage == "sampled-texture") return "@group(0) @binding(0) var texture0 : texture_2d_array<f32>;\n@fragment fn main() -> @location(0) vec4<f32> { return textureLoad(texture0, vec2<i32>(), 0, 0); }";
        if (usage == "readonly-storage-texture") return "@group(0) @binding(0) var texture0 : texture_storage_2d_array<r32float, read>;\n@fragment fn main() -> @location(0) vec4<f32> { return textureLoad(texture0, vec2<i32>(), 0); }";
        if (usage == "writeonly-storage-texture") return "@group(0) @binding(0) var texture0 : texture_storage_2d_array<r32float, write>;\n@fragment fn main() -> @location(0) vec4<f32> { textureStore(texture0, vec2<i32>(), 0, vec4<f32>(1,0,0,1)); return vec4<f32>(); }";
        return "@group(0) @binding(0) var texture0 : texture_storage_2d_array<r32float, read_write>;\n@fragment fn main() -> @location(0) vec4<f32> { let c = textureLoad(texture0, vec2<i32>(), 0); textureStore(texture0, vec2<i32>(), 0, c); return c; }";
    }
    if (usage == "sampled-texture") return "@group(0) @binding(0) var texture0 : texture_2d_array<f32>;\n@group(1) @binding(0) var writableStorage : texture_storage_2d_array<r32float, write>;\n@compute @workgroup_size(1) fn main() { let v = textureLoad(texture0, vec2<i32>(), 0, 0); textureStore(writableStorage, vec2<i32>(), 0, v); }";
    if (usage == "readonly-storage-texture") return "@group(0) @binding(0) var texture0 : texture_storage_2d_array<r32float, read>;\n@group(1) @binding(0) var writableStorage : texture_storage_2d_array<r32float, write>;\n@compute @workgroup_size(1) fn main() { let v = textureLoad(texture0, vec2<i32>(), 0); textureStore(writableStorage, vec2<i32>(), 0, v); }";
    if (usage == "writeonly-storage-texture") return "@group(0) @binding(0) var texture0 : texture_storage_2d_array<r32float, write>;\n@group(1) @binding(0) var writableStorage : texture_storage_2d_array<r32float, write>;\n@compute @workgroup_size(1) fn main() { textureStore(texture0, vec2<i32>(), 0, vec4<f32>()); textureStore(writableStorage, vec2<i32>(), 0, vec4<f32>()); }";
    return "@group(0) @binding(0) var texture0 : texture_storage_2d_array<r32float, read_write>;\n@group(1) @binding(0) var writableStorage : texture_storage_2d_array<r32float, write>;\n@compute @workgroup_size(1) fn main() { let c = textureLoad(texture0, vec2<i32>(), 0); textureStore(texture0, vec2<i32>(), 0, c); textureStore(writableStorage, vec2<i32>(), 0, c); }";
}

CTS_TEST(testGroup, "subresources,set_unused_bind_group")
    .desc(R"(
  Test that when one texture subresource is bound to different bind groups and the bind groups are
  used in the same render or compute pass encoder, its list of internal usages within one usage
  scope can only be a compatible usage list.)")
    .params([](ParamsBuilder u) {
        return u.combine("inRenderPass", {true, false})
            .combine("textureUsage0", textureBindingTypes())
            .combine("textureUsage1", textureBindingTypes());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool inRenderPass = t.param<bool>("inRenderPass");
        const std::string usage0 = t.param<std::string>("textureUsage0");
        const std::string usage1 = t.param<std::string>("textureUsage1");
        skipIfStorageTextureUnsupported(t, usage0);
        skipIfStorageTextureUnsupported(t, usage1);

        WGPUTexture texture0 = createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding, kTextureLayers);
        WGPUTextureView view0 = createView(t, texture0, WGPUTextureViewDimension_2DArray, 0, 1);
        WGPUShaderStage visibility = inRenderPass ? WGPUShaderStage_Fragment : WGPUShaderStage_Compute;
        WGPUBindGroup bg0 = createBindGroupForTest(t, view0, usage0, "unfilterable-float", visibility);
        WGPUBindGroup bg1 = createBindGroupForTest(t, view0, usage1, "unfilterable-float", visibility);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (inRenderPass) {
            WGPUBindGroupLayout layout = createBindGroupLayoutForTest(t, usage0, "unfilterable-float", visibility);
            WGPURenderPipeline pipeline = createRenderPipeline(t, pipelineLayout(t, {layout}), shaderForUsage(usage0, true));
            WGPUTexture color = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_RenderAttachment);
            WGPURenderPassColorAttachment ca = colorAttachment(createView(t, color, WGPUTextureViewDimension_2D, 0, 1));
            std::vector<WGPURenderPassColorAttachment> colors = {ca};
            WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            wgpuRenderPassEncoderSetBindGroup(pass, 1, bg1, 0, nullptr);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        } else {
            WGPUBindGroupLayout layout0 = createBindGroupLayoutForTest(t, usage0, "unfilterable-float", visibility);
            WGPUBindGroupLayout layout1 = createBindGroupLayoutForTest(t, "writeonly-storage-texture", "unfilterable-float", visibility);
            WGPUComputePipeline pipeline = createComputePipeline(t, pipelineLayout(t, {layout0, layout1}), shaderForUsage(usage0, false));
            WGPUTexture writable = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_StorageBinding);
            WGPUBindGroup writableBg = createBindGroupForTest(t, createView(t, writable), "writeonly-storage-texture", "unfilterable-float", visibility);
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            wgpuComputePassEncoderSetBindGroup(pass, 1, writableBg, 0, nullptr);
            wgpuComputePassEncoderSetBindGroup(pass, 2, bg1, 0, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, pipeline);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
        }
        const bool success = !inRenderPass ||
            (isReadOnlyTextureBindingType(usage0) && isReadOnlyTextureBindingType(usage1)) ||
            usage0 == usage1;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
    });

void useTextureOnCommandEncoder(AllFeaturesMaxLimitsGpuTest& t,
                                WGPUTexture texture,
                                const std::string& usage,
                                WGPUCommandEncoder encoder) {
    if (usage == "copy-src") {
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.size = 256;
        bd.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bd);
        t.copyTextureToBuffer(encoder, texture, buffer, 256, WGPUExtent3D{1, 1, 1});
    } else if (usage == "copy-dst") {
        WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
        bd.size = 256;
        bd.usage = WGPUBufferUsage_CopySrc;
        WGPUBuffer buffer = t.createBufferTracked(bd);
        t.copyBufferToTexture(encoder, buffer, 256, texture, WGPUExtent3D{1, 1, 1});
    } else if (usage == "color-attachment") {
        WGPURenderPassColorAttachment ca = colorAttachment(createView(t, texture, WGPUTextureViewDimension_2D, 0, 1));
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
        wgpuRenderPassEncoderEnd(pass);
    } else {
        WGPUTexture color = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_RenderAttachment);
        WGPURenderPassColorAttachment ca = colorAttachment(createView(t, color, WGPUTextureViewDimension_2D, 0, 1));
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
        WGPUBindGroup bg = createBindGroupForTest(t, createView(t, texture), usage, "unfilterable-float");
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        wgpuRenderPassEncoderEnd(pass);
    }
}

CTS_TEST(testGroup, "subresources,texture_usages_in_copy_and_render_pass")
    .desc(R"(
  Test that using one texture subresource in a render pass encoder and a copy command is always
  allowed as WebGPU SPEC (chapter 3.4.5) defines that out of any pass encoder, each command always
  belongs to one usage scope.)")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", copyPassAndTextureBindingTypes())
            .combine("usage1", copyPassAndTextureBindingTypes())
            .filter([](const ParamRecord& p) {
                const std::string u0 = valueAs<std::string>(*findParam(p, "usage0"));
                const std::string u1 = valueAs<std::string>(*findParam(p, "usage1"));
                return u0 == "copy-src" || u0 == "copy-dst" || u1 == "copy-src" || u1 == "copy-dst";
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string usage0 = t.param<std::string>("usage0");
        const std::string usage1 = t.param<std::string>("usage1");
        skipIfStorageTextureUnsupported(t, usage0);
        skipIfStorageTextureUnsupported(t, usage1);
        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_TextureBinding |
            WGPUTextureUsage_StorageBinding | WGPUTextureUsage_RenderAttachment);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        useTextureOnCommandEncoder(t, texture, usage0, encoder);
        useTextureOnCommandEncoder(t, texture, usage1, encoder);
        t.expectValidationError([&] { t.finishTracked(encoder); }, false);
    });

CTS_TEST(testGroup, "subresources,texture_view_usages")
    .desc(R"(
  Test that the usages of the texture view are used to validate compatibility in command encoding
  instead of the usages of the base texture.)")
    .params([](ParamsBuilder u) {
        std::vector<Value> bindingTypes = {std::string("color-attachment")};
        std::vector<Value> bindings = textureBindingTypes();
        bindingTypes.insert(bindingTypes.end(), bindings.begin(), bindings.end());
        return u.combine("bindingType", bindingTypes)
            .combine("viewUsage", textureUsageValuesWithZero())
            .filter([](const ParamRecord& p) {
                return static_cast<WGPUTextureUsage>(valueAs<uint64_t>(*findParam(p, "viewUsage"))) !=
                       WGPUTextureUsage_TransientAttachment;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string bindingType = t.param<std::string>("bindingType");
        const WGPUTextureUsage viewUsage = static_cast<WGPUTextureUsage>(t.param<uint64_t>("viewUsage"));
        skipIfStorageTextureUnsupported(t, bindingType);
        WGPUTextureUsage usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding;
        if (bindingType == "color-attachment") usage |= WGPUTextureUsage_RenderAttachment;
        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float, usage);
        if (bindingType == "color-attachment") {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassColorAttachment ca = colorAttachment(createView(t, texture, WGPUTextureViewDimension_2D, 0, 1, WGPUTextureAspect_All, viewUsage));
            std::vector<WGPURenderPassColorAttachment> colors = {ca};
            WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
            wgpuRenderPassEncoderEnd(pass);
            const bool success = viewUsage == WGPUTextureUsage_None ||
                ((viewUsage & WGPUTextureUsage_RenderAttachment) != 0);
            t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
        } else {
            bool success = true;
            if (viewUsage != WGPUTextureUsage_None) {
                if (bindingType == "sampled-texture") success = (viewUsage & WGPUTextureUsage_TextureBinding) != 0;
                else success = (viewUsage & WGPUTextureUsage_StorageBinding) != 0;
            }
            t.expectValidationError([&] {
                WGPUTextureView view = createView(t, texture, WGPUTextureViewDimension_2DArray, 0, WGPU_ARRAY_LAYER_COUNT_UNDEFINED, WGPUTextureAspect_All, viewUsage);
                createBindGroupForTest(t, view, bindingType, "unfilterable-float");
            }, !success);
        }
    });

}  // namespace
