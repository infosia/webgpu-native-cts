// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/resource_usages/texture/in_pass_encoder.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

using namespace cts;

namespace {

constexpr uint32_t kSize = 32;
constexpr uint32_t kBaseLevel = 1;
constexpr uint32_t kTotalLevels = 6;
constexpr uint32_t kBaseLayer = 1;
constexpr uint32_t kTotalLayers = 6;
constexpr uint32_t kSliceCount = 2;

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,resource_usages,texture,in_pass_encoder",
    R"(
Texture Usages Validation Tests in Render Pass and Compute Pass.
)");

WGPUStringView sv(const char* text) {
    return WGPUStringView{text, WGPU_STRLEN};
}

std::vector<Value> textureBindingTypes() {
    return {
        std::string("sampled-texture"),
        std::string("multisampled-texture"),
        std::string("writeonly-storage-texture"),
        std::string("readonly-storage-texture"),
        std::string("readwrite-storage-texture"),
    };
}

std::vector<Value> conflictingPairs() {
    return {
        std::string("sampled-texture/writeonly-storage-texture"),
        std::string("sampled-texture/readwrite-storage-texture"),
        std::string("readonly-storage-texture/writeonly-storage-texture"),
        std::string("readonly-storage-texture/readwrite-storage-texture"),
        std::string("writeonly-storage-texture/readwrite-storage-texture"),
    };
}

std::string pairFirst(const std::string& pair) {
    return pair.substr(0, pair.find('/'));
}

std::string pairSecond(const std::string& pair) {
    return pair.substr(pair.find('/') + 1);
}

bool isReadOnly(const std::string& type) {
    return type == "sampled-texture" || type == "multisampled-texture" || type == "readonly-storage-texture";
}

bool isStorage(const std::string& type) {
    return type == "writeonly-storage-texture" || type == "readonly-storage-texture" || type == "readwrite-storage-texture";
}

bool hasDepth(WGPUTextureFormat format);
bool hasStencil(WGPUTextureFormat format);

void skipIfStorageUnsupported(AllFeaturesMaxLimitsGpuTest& t, const std::string& type) {
    if (!isStorage(type)) return;
    const WGPUStorageTextureAccess access = type == "readonly-storage-texture" ? WGPUStorageTextureAccess_ReadOnly :
        type == "readwrite-storage-texture" ? WGPUStorageTextureAccess_ReadWrite : WGPUStorageTextureAccess_WriteOnly;
    if (!t.isTextureFormatUsableWithStorageAccessMode(WGPUTextureFormat_R32Float, access)) {
        t.skip("r32float does not support requested storage access");
    }
}

WGPUTextureAspect aspectFor(const std::string& aspect) {
    if (aspect == "all") return WGPUTextureAspect_All;
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

std::string sampleTypeNameForAspect(WGPUTextureFormat format, const std::string& aspect) {
    if (aspect == "stencil-only" || (aspect == "all" && hasStencil(format) && !hasDepth(format))) {
        return "uint";
    }
    return "depth";
}

WGPUTextureAspect viewAspectForBinding(WGPUTextureFormat format, const std::string& aspect) {
    if (aspect == "all" && hasStencil(format) && !hasDepth(format)) return WGPUTextureAspect_StencilOnly;
    if (aspect == "all" && hasDepth(format) && !hasStencil(format)) return WGPUTextureAspect_DepthOnly;
    return aspectFor(aspect);
}

WGPUStorageTextureAccess storageAccessFor(const std::string& type) {
    if (type == "writeonly-storage-texture") return WGPUStorageTextureAccess_WriteOnly;
    if (type == "readonly-storage-texture") return WGPUStorageTextureAccess_ReadOnly;
    if (type == "readwrite-storage-texture") return WGPUStorageTextureAccess_ReadWrite;
    std::abort();
}

WGPUTextureUsage usageForType(const std::string& type) {
    if (type == "render-target") return WGPUTextureUsage_RenderAttachment;
    if (type == "sampled-texture" || type == "multisampled-texture") return WGPUTextureUsage_TextureBinding;
    return WGPUTextureUsage_StorageBinding;
}

WGPUTexture createTexture(AllFeaturesMaxLimitsGpuTest& t,
                          WGPUTextureFormat format = WGPUTextureFormat_R32Float,
                          WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
                          uint32_t layers = 1,
                          uint32_t levels = 1,
                          uint32_t sampleCount = 1,
                          uint32_t size = kSize) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{size, size, layers};
    desc.mipLevelCount = levels;
    desc.sampleCount = sampleCount;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = usage;
    return t.createTextureTracked(desc);
}

WGPUTextureView createView(AllFeaturesMaxLimitsGpuTest& t,
                           WGPUTexture texture,
                           WGPUTextureViewDimension dimension = WGPUTextureViewDimension_2D,
                           uint32_t baseLevel = 0,
                           uint32_t levelCount = 1,
                           uint32_t baseLayer = 0,
                           uint32_t layerCount = 1,
                           WGPUTextureAspect aspect = WGPUTextureAspect_All) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.dimension = dimension;
    desc.baseMipLevel = baseLevel;
    desc.mipLevelCount = levelCount;
    desc.baseArrayLayer = baseLayer;
    desc.arrayLayerCount = layerCount;
    desc.aspect = aspect;
    return t.createViewTracked(texture, desc);
}

WGPURenderPassColorAttachment colorAttachment(WGPUTextureView view) {
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{0.0, 1.0, 0.0, 1.0};
    return color;
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

WGPUBindGroupLayout createBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t,
                                          uint32_t binding,
                                          const std::string& type,
                                          WGPUTextureViewDimension dimension,
                                          WGPUShaderStage visibility = WGPUShaderStage_Compute | WGPUShaderStage_Fragment,
                                          std::string_view sampleType = "unfilterable-float") {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = visibility;
    if (type == "sampled-texture" || type == "multisampled-texture") {
        entry.texture.sampleType = sampleTypeFor(std::string(sampleType));
        entry.texture.viewDimension = dimension;
        entry.texture.multisampled = type == "multisampled-texture";
    } else {
        entry.storageTexture.access = storageAccessFor(type);
        entry.storageTexture.format = WGPUTextureFormat_R32Float;
        entry.storageTexture.viewDimension = dimension;
    }
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = 1;
    desc.entries = &entry;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBindGroup createBindGroup(AllFeaturesMaxLimitsGpuTest& t,
                              uint32_t binding,
                              WGPUTextureView view,
                              const std::string& type,
                              WGPUTextureViewDimension dimension,
                              WGPUShaderStage visibility = WGPUShaderStage_Compute | WGPUShaderStage_Fragment,
                              std::string_view sampleType = "unfilterable-float") {
    WGPUBindGroupLayout layout = createBindGroupLayout(t, binding, type, dimension, visibility, sampleType);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
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

WGPUComputePipeline computePipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout = nullptr, std::string_view code = "") {
    std::string wgsl = code.empty() ? "@compute @workgroup_size(1) fn main() {}" : std::string(code);
    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

WGPURenderPipeline renderPipeline(AllFeaturesMaxLimitsGpuTest& t, WGPUPipelineLayout layout = nullptr, uint32_t writeMask = WGPUColorWriteMask_All) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(
        "@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(); }");
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(
        "@fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(); }");
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_R32Float;
    target.writeMask = writeMask;
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

struct ScopeObjects {
    WGPUBindGroupLayout layout0;
    WGPUBindGroupLayout layout1;
    WGPUBindGroup bindGroup0;
    WGPUBindGroup bindGroup1;
    WGPUCommandEncoder encoder;
    WGPUComputePassEncoder computePass;
    WGPURenderPassEncoder renderPass;
};

ScopeObjects testValidationScope(AllFeaturesMaxLimitsGpuTest& t, bool compute, const std::string& usage1, const std::string& usage2) {
    WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float,
        WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding);
    WGPUTextureView view = createView(t, texture);
    ScopeObjects o{};
    o.layout0 = createBindGroupLayout(t, 0, usage1, WGPUTextureViewDimension_2D);
    o.layout1 = createBindGroupLayout(t, 0, usage2, WGPUTextureViewDimension_2D);
    o.bindGroup0 = createBindGroup(t, 0, view, usage1, WGPUTextureViewDimension_2D);
    o.bindGroup1 = createBindGroup(t, 0, view, usage2, WGPUTextureViewDimension_2D);
    o.encoder = t.createCommandEncoderTracked();
    if (compute) {
        o.computePass = wgpuCommandEncoderBeginComputePass(o.encoder, nullptr);
    } else {
        WGPUTexture color = createTexture(t);
        WGPURenderPassColorAttachment ca = colorAttachment(createView(t, color));
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        o.renderPass = beginRenderPass(o.encoder, colors);
    }
    return o;
}

std::string computeBindingDeclaration(const std::string& usage, uint32_t group) {
    const std::string prefix = "@group(" + std::to_string(group) + ") @binding(0) var tex" +
        std::to_string(group);
    if (usage == "sampled-texture") {
        return prefix + " : texture_2d<f32>;\n";
    }
    if (usage == "readonly-storage-texture") {
        return prefix + " : texture_storage_2d<r32float, read>;\n";
    }
    if (usage == "writeonly-storage-texture") {
        return prefix + " : texture_storage_2d<r32float, write>;\n";
    }
    if (usage == "readwrite-storage-texture") {
        return prefix + " : texture_storage_2d<r32float, read_write>;\n";
    }
    std::abort();
}

std::string computeBindingUse(const std::string& usage, uint32_t group) {
    const std::string name = "tex" + std::to_string(group);
    if (usage == "sampled-texture") {
        return "let value" + std::to_string(group) + " = textureLoad(" + name + ", vec2<i32>(0), 0);\n";
    }
    if (usage == "readonly-storage-texture") {
        return "let value" + std::to_string(group) + " = textureLoad(" + name + ", vec2<i32>(0));\n";
    }
    if (usage == "writeonly-storage-texture") {
        return "textureStore(" + name + ", vec2<i32>(0), vec4<f32>(1.0, 0.0, 0.0, 1.0));\n";
    }
    if (usage == "readwrite-storage-texture") {
        return "let value" + std::to_string(group) + " = textureLoad(" + name + ", vec2<i32>(0));\n" +
            "textureStore(" + name + ", vec2<i32>(0), value" + std::to_string(group) + ");\n";
    }
    std::abort();
}

WGPUComputePipeline computePipelineUsingPair(AllFeaturesMaxLimitsGpuTest& t,
                                             WGPUPipelineLayout layout,
                                             const std::string& usage0,
                                             const std::string& usage1) {
    const std::string wgsl = computeBindingDeclaration(usage0, 0) +
        computeBindingDeclaration(usage1, 1) +
        "@compute @workgroup_size(1) fn main() {\n" +
        computeBindingUse(usage0, 0) +
        computeBindingUse(usage1, 1) +
        "}\n";
    return computePipeline(t, layout, wgsl);
}

std::vector<ParamRecord> colorUsageCombos(bool compute) {
    return {
        ParamRecord{{"_usageOK", true}, {"type0", std::string("sampled-texture")}, {"type1", std::string("sampled-texture")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("sampled-texture")}, {"type1", std::string("writeonly-storage-texture")}},
        ParamRecord{{"_usageOK", true}, {"type0", std::string("sampled-texture")}, {"type1", std::string("readonly-storage-texture")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("sampled-texture")}, {"type1", std::string("readwrite-storage-texture")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("sampled-texture")}, {"type1", std::string("render-target")}},
        ParamRecord{{"_usageOK", !compute}, {"type0", std::string("writeonly-storage-texture")}, {"type1", std::string("writeonly-storage-texture")}},
        ParamRecord{{"_usageOK", true}, {"type0", std::string("readonly-storage-texture")}, {"type1", std::string("readonly-storage-texture")}},
        ParamRecord{{"_usageOK", !compute}, {"type0", std::string("readwrite-storage-texture")}, {"type1", std::string("readwrite-storage-texture")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("readonly-storage-texture")}, {"type1", std::string("writeonly-storage-texture")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("readonly-storage-texture")}, {"type1", std::string("readwrite-storage-texture")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("writeonly-storage-texture")}, {"type1", std::string("readwrite-storage-texture")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("readonly-storage-texture")}, {"type1", std::string("render-target")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("writeonly-storage-texture")}, {"type1", std::string("render-target")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("readwrite-storage-texture")}, {"type1", std::string("render-target")}},
        ParamRecord{{"_usageOK", false}, {"type0", std::string("render-target")}, {"type1", std::string("render-target")}},
    };
}

std::vector<ParamRecord> colorUsageComputeCombos() {
    std::vector<ParamRecord> records;
    for (bool compute : {false, true}) {
        std::vector<ParamRecord> usageRecords = colorUsageCombos(compute);
        for (ParamRecord& record : usageRecords) {
            ParamRecord withCompute{{"compute", compute}};
            withCompute.insert(withCompute.end(), record.begin(), record.end());
            records.push_back(std::move(withCompute));
        }
    }
    return records;
}

std::vector<ParamRecord> subresourceCombos() {
    return {
        ParamRecord{{"levelCount0", 1}, {"layerCount0", 1}, {"baseLevel1", static_cast<int>(kBaseLevel)}, {"levelCount1", 1}, {"baseLayer1", static_cast<int>(kBaseLayer)}, {"layerCount1", 1}, {"_resourceSuccess", false}},
        ParamRecord{{"levelCount0", 1}, {"layerCount0", 1}, {"baseLevel1", static_cast<int>(kBaseLevel + 1)}, {"levelCount1", 1}, {"baseLayer1", static_cast<int>(kBaseLayer)}, {"layerCount1", 1}, {"_resourceSuccess", true}},
        ParamRecord{{"levelCount0", 1}, {"layerCount0", 1}, {"baseLevel1", static_cast<int>(kBaseLevel)}, {"levelCount1", 1}, {"baseLayer1", static_cast<int>(kBaseLayer + 1)}, {"layerCount1", 1}, {"_resourceSuccess", true}},
        ParamRecord{{"levelCount0", 1}, {"layerCount0", 1}, {"baseLevel1", 0}, {"levelCount1", static_cast<int>(kTotalLevels)}, {"baseLayer1", static_cast<int>(kBaseLayer)}, {"layerCount1", 1}, {"_resourceSuccess", false}},
        ParamRecord{{"levelCount0", 1}, {"layerCount0", 1}, {"baseLevel1", static_cast<int>(kBaseLevel)}, {"levelCount1", 1}, {"baseLayer1", 0}, {"layerCount1", static_cast<int>(kTotalLayers)}, {"_resourceSuccess", false}},
        ParamRecord{{"levelCount0", 1}, {"layerCount0", 1}, {"baseLevel1", 0}, {"levelCount1", static_cast<int>(kTotalLevels)}, {"baseLayer1", 0}, {"layerCount1", static_cast<int>(kTotalLayers)}, {"_resourceSuccess", false}},
        ParamRecord{{"levelCount0", static_cast<int>(kSliceCount)}, {"layerCount0", 1}, {"baseLevel1", static_cast<int>(kBaseLevel + kSliceCount)}, {"levelCount1", 3}, {"baseLayer1", static_cast<int>(kBaseLayer)}, {"layerCount1", 1}, {"_resourceSuccess", true}},
        ParamRecord{{"levelCount0", static_cast<int>(kSliceCount)}, {"layerCount0", 1}, {"baseLevel1", static_cast<int>(kBaseLevel + kSliceCount - 1)}, {"levelCount1", 3}, {"baseLayer1", static_cast<int>(kBaseLayer)}, {"layerCount1", 1}, {"_resourceSuccess", false}},
        ParamRecord{{"levelCount0", 1}, {"layerCount0", static_cast<int>(kSliceCount)}, {"baseLevel1", static_cast<int>(kBaseLevel)}, {"levelCount1", 1}, {"baseLayer1", static_cast<int>(kBaseLayer + kSliceCount)}, {"layerCount1", 3}, {"_resourceSuccess", true}},
        ParamRecord{{"levelCount0", 1}, {"layerCount0", static_cast<int>(kSliceCount)}, {"baseLevel1", static_cast<int>(kBaseLevel)}, {"levelCount1", 1}, {"baseLayer1", static_cast<int>(kBaseLayer + kSliceCount - 1)}, {"layerCount1", 3}, {"_resourceSuccess", false}},
        ParamRecord{{"levelCount0", static_cast<int>(kSliceCount)}, {"layerCount0", static_cast<int>(kSliceCount)}, {"baseLevel1", static_cast<int>(kBaseLevel + kSliceCount)}, {"levelCount1", 3}, {"baseLayer1", static_cast<int>(kBaseLayer + kSliceCount)}, {"layerCount1", 3}, {"_resourceSuccess", true}},
        ParamRecord{{"levelCount0", static_cast<int>(kSliceCount)}, {"layerCount0", static_cast<int>(kSliceCount)}, {"baseLevel1", static_cast<int>(kBaseLevel + kSliceCount - 1)}, {"levelCount1", 3}, {"baseLayer1", static_cast<int>(kBaseLayer + kSliceCount - 1)}, {"layerCount1", 3}, {"_resourceSuccess", false}},
    };
}

CTS_TEST(testGroup, "subresources_and_binding_types_combination_for_color")
    .desc(R"(
    Test the resource usage rules by using two views of the same GPUTexture in a usage scope.)")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(colorUsageComputeCombos())
            .beginSubcases()
            .combine("binding0InBundle", {false, true})
            .combine("binding1InBundle", {false, true})
            .filter([](const ParamRecord& p) {
                const bool compute = valueAs<bool>(*findParam(p, "compute"));
                const bool b0 = valueAs<bool>(*findParam(p, "binding0InBundle"));
                const bool b1 = valueAs<bool>(*findParam(p, "binding1InBundle"));
                const std::string type0 = valueAs<std::string>(*findParam(p, "type0"));
                const std::string type1 = valueAs<std::string>(*findParam(p, "type1"));
                return !((b0 && type0 == "render-target") || (b1 && type1 == "render-target") ||
                         (compute && (b0 || b1 || type0 == "render-target" || type1 == "render-target")));
            })
            .combineWithParams(subresourceCombos())
            .filter([](const ParamRecord& p) {
                const std::string type0 = valueAs<std::string>(*findParam(p, "type0"));
                const std::string type1 = valueAs<std::string>(*findParam(p, "type1"));
                const int levelCount0 = valueAs<int>(*findParam(p, "levelCount0"));
                const int layerCount0 = valueAs<int>(*findParam(p, "layerCount0"));
                const int baseLevel1 = valueAs<int>(*findParam(p, "baseLevel1"));
                const int levelCount1 = valueAs<int>(*findParam(p, "levelCount1"));
                const int layerCount1 = valueAs<int>(*findParam(p, "layerCount1"));
                return !((type0 != "sampled-texture" && (levelCount0 != 1 || layerCount0 != 1)) ||
                         (type1 != "sampled-texture" && (levelCount1 != 1 || layerCount1 != 1)) ||
                         (type0 == "render-target" && type1 == "render-target" && baseLevel1 != static_cast<int>(kBaseLevel)));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool compute = t.param<bool>("compute");
        const bool b0Bundle = t.param<bool>("binding0InBundle");
        const bool b1Bundle = t.param<bool>("binding1InBundle");
        const uint32_t levelCount0 = static_cast<uint32_t>(t.param<int>("levelCount0"));
        const uint32_t layerCount0 = static_cast<uint32_t>(t.param<int>("layerCount0"));
        const uint32_t baseLevel1 = static_cast<uint32_t>(t.param<int>("baseLevel1"));
        const uint32_t baseLayer1 = static_cast<uint32_t>(t.param<int>("baseLayer1"));
        const uint32_t levelCount1 = static_cast<uint32_t>(t.param<int>("levelCount1"));
        const uint32_t layerCount1 = static_cast<uint32_t>(t.param<int>("layerCount1"));
        const std::string type0 = t.param<std::string>("type0");
        const std::string type1 = t.param<std::string>("type1");
        const bool usageOK = t.param<bool>("_usageOK");
        const bool resourceSuccess = t.param<bool>("_resourceSuccess");
        skipIfStorageUnsupported(t, type0);
        skipIfStorageUnsupported(t, type1);

        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding | WGPUTextureUsage_RenderAttachment,
            kTotalLayers, kTotalLevels);
        const WGPUTextureViewDimension dim0 = layerCount0 != 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
        const WGPUTextureViewDimension dim1 = layerCount1 != 1 ? WGPUTextureViewDimension_2DArray : WGPUTextureViewDimension_2D;
        WGPUTextureView view0 = createView(t, texture, dim0, kBaseLevel, levelCount0, kBaseLayer, layerCount0);
        WGPUTextureView view1 = createView(t, texture, dim1, baseLevel1, levelCount1, baseLayer1, layerCount1);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (type0 == "render-target") {
            WGPURenderPassColorAttachment a0 = colorAttachment(view0);
            WGPURenderPassColorAttachment a1 = colorAttachment(view1);
            std::vector<WGPURenderPassColorAttachment> colors = {a0, a1};
            WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
            wgpuRenderPassEncoderEnd(pass);
        } else if (compute) {
            std::vector<WGPUBindGroupLayout> layouts;
            layouts.push_back(createBindGroupLayout(t, 0, type0, dim0));
            WGPUBindGroup bg0 = createBindGroup(t, 0, view0, type0, dim0);
            WGPUBindGroup bg1 = nullptr;
            if (type1 != "render-target") {
                layouts.push_back(createBindGroupLayout(t, 1, type1, dim1));
                bg1 = createBindGroup(t, 1, view1, type1, dim1);
            }
            WGPUComputePipeline pipe = computePipeline(t, pipelineLayout(t, layouts));
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            if (bg1) wgpuComputePassEncoderSetBindGroup(pass, 1, bg1, 0, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, pipe);
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
        } else {
            WGPUTexture passTexture = type1 == "render-target" ? texture : createTexture(t);
            WGPUTextureView passView = type1 == "render-target" ? view1 : createView(t, passTexture);
            WGPURenderPassColorAttachment ca = colorAttachment(passView);
            std::vector<WGPURenderPassColorAttachment> colors = {ca};
            WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
            WGPUBindGroup bg0 = createBindGroup(t, 0, view0, type0, dim0);
            WGPUBindGroup bg1 = nullptr;
            if (type1 != "render-target") bg1 = createBindGroup(t, 1, view1, type1, dim1);
            if (b0Bundle) {
                WGPUTextureFormat fmt = WGPUTextureFormat_R32Float;
                WGPURenderBundleEncoderDescriptor bd = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                bd.colorFormatCount = 1;
                bd.colorFormats = &fmt;
                WGPURenderBundleEncoder be = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bd);
                wgpuRenderBundleEncoderSetBindGroup(be, 0, bg0, 0, nullptr);
                WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(be, nullptr);
                wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
            } else {
                wgpuRenderPassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            }
            if (bg1) {
                if (b1Bundle) {
                    WGPUTextureFormat fmt = WGPUTextureFormat_R32Float;
                    WGPURenderBundleEncoderDescriptor bd = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                    bd.colorFormatCount = 1;
                    bd.colorFormats = &fmt;
                    WGPURenderBundleEncoder be = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bd);
                    wgpuRenderBundleEncoderSetBindGroup(be, 1, bg1, 0, nullptr);
                    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(be, nullptr);
                    wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
                } else {
                    wgpuRenderPassEncoderSetBindGroup(pass, 1, bg1, 0, nullptr);
                }
            }
            wgpuRenderPassEncoderEnd(pass);
        }
        t.expectValidationError([&] { t.finishTracked(encoder); }, !(resourceSuccess || usageOK));
    });

std::vector<Value> depthStencilFormatValues() {
    return {
        std::string("stencil8"),
        std::string("depth16unorm"),
        std::string("depth24plus"),
        std::string("depth24plus-stencil8"),
        std::string("depth32float"),
        std::string("depth32float-stencil8"),
    };
}

WGPUTextureFormat parseFormat(const std::string& format) {
    if (format == "stencil8") return WGPUTextureFormat_Stencil8;
    if (format == "depth16unorm") return WGPUTextureFormat_Depth16Unorm;
    if (format == "depth24plus") return WGPUTextureFormat_Depth24Plus;
    if (format == "depth24plus-stencil8") return WGPUTextureFormat_Depth24PlusStencil8;
    if (format == "depth32float") return WGPUTextureFormat_Depth32Float;
    if (format == "depth32float-stencil8") return WGPUTextureFormat_Depth32FloatStencil8;
    std::abort();
}

bool hasDepth(WGPUTextureFormat format) {
    return textureFormatInfo(format).hasDepth;
}

bool hasStencil(WGPUTextureFormat format) {
    return textureFormatInfo(format).hasStencil;
}

CTS_TEST(testGroup, "subresources_and_binding_types_combination_for_aspect")
    .desc(R"(
    Test the resource usage rules by using two views of the same GPUTexture in a usage scope.)")
    .params([](ParamsBuilder u) {
        return u.combine("compute", {false, true})
            .combine("binding0InBundle", {false, true})
            .combine("binding1InBundle", {false, true})
            .combine("format", depthStencilFormatValues())
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"baseLevel", static_cast<int>(kBaseLevel)}, {"baseLayer", static_cast<int>(kBaseLayer)}, {"_resourceSuccess", false}},
                ParamRecord{{"baseLevel", static_cast<int>(kBaseLevel + 1)}, {"baseLayer", static_cast<int>(kBaseLayer)}, {"_resourceSuccess", true}},
                ParamRecord{{"baseLevel", static_cast<int>(kBaseLevel)}, {"baseLayer", static_cast<int>(kBaseLayer + 1)}, {"_resourceSuccess", true}},
            })
            .combine("aspect0", {std::string("all"), std::string("depth-only"), std::string("stencil-only")})
            .combine("aspect1", {std::string("all"), std::string("depth-only"), std::string("stencil-only")})
            .filter([](const ParamRecord& p) {
                const WGPUTextureFormat f = parseFormat(valueAs<std::string>(*findParam(p, "format")));
                const std::string a0 = valueAs<std::string>(*findParam(p, "aspect0"));
                const std::string a1 = valueAs<std::string>(*findParam(p, "aspect1"));
                return !((a0 == "stencil-only" && !hasStencil(f)) || (a1 == "stencil-only" && !hasStencil(f)) ||
                         (a0 == "depth-only" && !hasDepth(f)) || (a1 == "depth-only" && !hasDepth(f)));
            })
            .combineWithParams({
                ParamRecord{{"type0", std::string("sampled-texture")}, {"type1", std::string("sampled-texture")}, {"_usageSuccess", true}},
                ParamRecord{{"type0", std::string("sampled-texture")}, {"type1", std::string("render-target")}, {"_usageSuccess", false}},
            })
            .filter([](const ParamRecord& p) {
                const bool compute = valueAs<bool>(*findParam(p, "compute"));
                const bool b0 = valueAs<bool>(*findParam(p, "binding0InBundle"));
                const bool b1 = valueAs<bool>(*findParam(p, "binding1InBundle"));
                const WGPUTextureFormat f = parseFormat(valueAs<std::string>(*findParam(p, "format")));
                const std::string a0 = valueAs<std::string>(*findParam(p, "aspect0"));
                const std::string a1 = valueAs<std::string>(*findParam(p, "aspect1"));
                const std::string type0 = valueAs<std::string>(*findParam(p, "type0"));
                const std::string type1 = valueAs<std::string>(*findParam(p, "type1"));
                return !((hasDepth(f) && hasStencil(f) && ((a0 == "all" && type0 == "sampled-texture") ||
                                                          (a1 == "all" && type1 == "sampled-texture"))) ||
                         (b1 && type1 == "render-target") ||
                         (compute && (b0 || b1 || type1 == "render-target")) ||
                         (type1 == "render-target" && hasDepth(f) && hasStencil(f) && a1 != "all"));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool compute = t.param<bool>("compute");
        const bool b0Bundle = t.param<bool>("binding0InBundle");
        const bool b1Bundle = t.param<bool>("binding1InBundle");
        const WGPUTextureFormat format = parseFormat(t.param<std::string>("format"));
        const uint32_t baseLevel = static_cast<uint32_t>(t.param<int>("baseLevel"));
        const uint32_t baseLayer = static_cast<uint32_t>(t.param<int>("baseLayer"));
        const std::string aspect0 = t.param<std::string>("aspect0");
        const std::string aspect1 = t.param<std::string>("aspect1");
        const std::string type1 = t.param<std::string>("type1");
        const bool resourceSuccess = t.param<bool>("_resourceSuccess");
        const bool usageSuccess = t.param<bool>("_usageSuccess");
        t.skipIfTextureFormatNotSupported(format);
        WGPUTexture texture = createTexture(t, format, WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding,
            kTotalLayers, kTotalLevels);
        WGPUTextureView view0 = createView(t, texture, WGPUTextureViewDimension_2D, kBaseLevel, 1, kBaseLayer, 1,
            viewAspectForBinding(format, aspect0));
        const WGPUTextureAspect view1Aspect = type1 == "render-target" ? aspectFor(aspect1) :
            viewAspectForBinding(format, aspect1);
        WGPUTextureView view1 = createView(t, texture, WGPUTextureViewDimension_2D, baseLevel, 1, baseLayer, 1, view1Aspect);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassEncoder cp = nullptr;
        WGPURenderPassEncoder rp = nullptr;
        if (compute) {
            cp = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        } else {
            std::vector<WGPURenderPassColorAttachment> colors;
            WGPURenderPassDepthStencilAttachment ds = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            WGPURenderPassDepthStencilAttachment* dsPtr = nullptr;
            if (type1 == "render-target") {
                ds.view = view1;
                if (hasDepth(format)) {
                    ds.depthLoadOp = WGPULoadOp_Load;
                    ds.depthStoreOp = WGPUStoreOp_Discard;
                }
                if (hasStencil(format)) {
                    ds.stencilLoadOp = WGPULoadOp_Load;
                    ds.stencilStoreOp = WGPUStoreOp_Discard;
                }
                dsPtr = &ds;
            }
            WGPUTexture color = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_RenderAttachment, 1, 1, 1, kSize >> baseLevel);
            colors.push_back(colorAttachment(createView(t, color)));
            rp = beginRenderPass(encoder, colors, dsPtr);
        }
        const std::string sample0 = sampleTypeNameForAspect(format, aspect0);
        WGPUBindGroup bg0 = createBindGroup(t, 0, view0, "sampled-texture", WGPUTextureViewDimension_2D,
            WGPUShaderStage_Compute | WGPUShaderStage_Fragment, sample0);
        if (compute) wgpuComputePassEncoderSetBindGroup(cp, 0, bg0, 0, nullptr);
        else if (b0Bundle) {
            WGPUTextureFormat colorFmt = WGPUTextureFormat_R32Float;
            WGPURenderBundleEncoderDescriptor bd = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            bd.colorFormatCount = 1;
            bd.colorFormats = &colorFmt;
            bd.depthStencilFormat = type1 == "render-target" ? format : WGPUTextureFormat_Undefined;
            WGPURenderBundleEncoder be = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bd);
            wgpuRenderBundleEncoderSetBindGroup(be, 0, bg0, 0, nullptr);
            WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(be, nullptr);
            wgpuRenderPassEncoderExecuteBundles(rp, 1, &bundle);
        } else wgpuRenderPassEncoderSetBindGroup(rp, 0, bg0, 0, nullptr);
        if (type1 != "render-target") {
            const std::string sample1 = sampleTypeNameForAspect(format, aspect1);
            WGPUBindGroup bg1 = createBindGroup(t, 1, view1, "sampled-texture", WGPUTextureViewDimension_2D,
                WGPUShaderStage_Compute | WGPUShaderStage_Fragment, sample1);
            if (compute) wgpuComputePassEncoderSetBindGroup(cp, 1, bg1, 0, nullptr);
            else if (b1Bundle) {
                WGPUTextureFormat colorFmt = WGPUTextureFormat_R32Float;
                WGPURenderBundleEncoderDescriptor bd = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                bd.colorFormatCount = 1;
                bd.colorFormats = &colorFmt;
                WGPURenderBundleEncoder be = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bd);
                wgpuRenderBundleEncoderSetBindGroup(be, 1, bg1, 0, nullptr);
                WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(be, nullptr);
                wgpuRenderPassEncoderExecuteBundles(rp, 1, &bundle);
            } else wgpuRenderPassEncoderSetBindGroup(rp, 1, bg1, 0, nullptr);
        }
        if (compute) {
            wgpuComputePassEncoderSetPipeline(cp, computePipeline(t));
            wgpuComputePassEncoderDispatchWorkgroups(cp, 1, 1, 1);
            wgpuComputePassEncoderEnd(cp);
        } else {
            wgpuRenderPassEncoderEnd(rp);
        }
        const bool disjointAspects = (aspect0 == "depth-only" && aspect1 == "stencil-only") ||
                                     (aspect0 == "stencil-only" && aspect1 == "depth-only");
        t.expectValidationError([&] { t.finishTracked(encoder); }, !(disjointAspects || resourceSuccess || usageSuccess));
    });

CTS_TEST(testGroup, "shader_stages_and_visibility,storage_write")
    .desc(R"(
    Test that stage visibility doesn't affect resource usage validation.)")
    .params([](ParamsBuilder u) {
        return u.combine("compute", {false, true})
            .beginSubcases()
            .combine("secondUseConflicts", {false, true})
            .combine("readVisibility", {0, static_cast<int>(WGPUShaderStage_Vertex), static_cast<int>(WGPUShaderStage_Fragment), static_cast<int>(WGPUShaderStage_Compute)})
            .combine("writeVisibility", {0, static_cast<int>(WGPUShaderStage_Fragment), static_cast<int>(WGPUShaderStage_Compute)})
            .combine("readEntry", {std::string("texture"), std::string("storageTexture_read-only")})
            .combine("storageWriteAccess", {std::string("write-only"), std::string("read-write")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool compute = t.param<bool>("compute");
        const bool conflicts = t.param<bool>("secondUseConflicts");
        const WGPUShaderStage readVisibility = static_cast<WGPUShaderStage>(t.param<int>("readVisibility"));
        const WGPUShaderStage writeVisibility = static_cast<WGPUShaderStage>(t.param<int>("writeVisibility"));
        const std::string readEntry = t.param<std::string>("readEntry");
        const std::string writeAccess = t.param<std::string>("storageWriteAccess");
        skipIfStorageUnsupported(t, writeAccess == "write-only" ? "writeonly-storage-texture" : "readwrite-storage-texture");
        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding);
        WGPUTexture texture2 = conflicts ? texture : createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding);
        WGPUTextureView view0 = createView(t, texture);
        WGPUTextureView view1 = createView(t, texture2);
        WGPUBindGroupLayoutEntry entries[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        entries[0].binding = 0;
        entries[0].visibility = readVisibility;
        if (readEntry == "texture") {
            entries[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        } else {
            entries[0].storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
            entries[0].storageTexture.format = WGPUTextureFormat_R32Float;
            entries[0].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        }
        entries[1].binding = 1;
        entries[1].visibility = writeVisibility;
        entries[1].storageTexture.access = writeAccess == "write-only" ? WGPUStorageTextureAccess_WriteOnly : WGPUStorageTextureAccess_ReadWrite;
        entries[1].storageTexture.format = WGPUTextureFormat_R32Float;
        entries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 2;
        bglDesc.entries = entries;
        WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);
        WGPUBindGroupEntry bgEntries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        bgEntries[0].binding = 0;
        bgEntries[0].textureView = view0;
        bgEntries[1].binding = 1;
        bgEntries[1].textureView = view1;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 2;
        bgDesc.entries = bgEntries;
        WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (compute) {
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
            wgpuComputePassEncoderSetPipeline(pass, computePipeline(t, pipelineLayout(t, {bgl})));
            wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
        } else {
            WGPUTexture color = createTexture(t);
            WGPURenderPassColorAttachment ca = colorAttachment(createView(t, color));
            std::vector<WGPURenderPassColorAttachment> colors = {ca};
            WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass);
        }
        t.expectValidationError([&] { t.finishTracked(encoder); }, conflicts);
    });

CTS_TEST(testGroup, "shader_stages_and_visibility,attachment_write")
    .desc(R"(
    Test that stage visibility doesn't affect resource usage validation.)")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("secondUseConflicts", {false, true})
            .combine("readVisibility", {0, static_cast<int>(WGPUShaderStage_Vertex), static_cast<int>(WGPUShaderStage_Fragment), static_cast<int>(WGPUShaderStage_Compute)})
            .combine("readEntry", {std::string("texture"), std::string("storageTexture_read-only")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool conflicts = t.param<bool>("secondUseConflicts");
        const WGPUShaderStage visibility = static_cast<WGPUShaderStage>(t.param<int>("readVisibility"));
        const std::string readEntry = t.param<std::string>("readEntry");
        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_StorageBinding);
        WGPUTexture texture2 = conflicts ? texture : createTexture(t, WGPUTextureFormat_R32Float,
            WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_StorageBinding);
        WGPUTextureView view = createView(t, texture);
        WGPUTextureView view2 = createView(t, texture2);
        WGPUBindGroup bg = createBindGroup(t, 0, view, readEntry == "texture" ? "sampled-texture" : "readonly-storage-texture",
            WGPUTextureViewDimension_2D, visibility);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassColorAttachment ca = colorAttachment(view2);
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
        wgpuRenderPassEncoderEnd(pass);
        t.expectValidationError([&] { t.finishTracked(encoder); }, conflicts);
    });

CTS_TEST(testGroup, "replaced_binding")
    .desc(R"(
    Test whether a binding that's been replaced by another setBindGroup call can still cause validation to fail.)")
    .params([](ParamsBuilder u) {
        return u.combine("compute", {false, true})
            .combine("callDrawOrDispatch", {false, true})
            .combine("entry", {std::string("texture"), std::string("storageTexture_read-only"), std::string("storageTexture_write-only"), std::string("storageTexture_read-write")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool compute = t.param<bool>("compute");
        const bool call = t.param<bool>("callDrawOrDispatch");
        const std::string entry = t.param<std::string>("entry");
        const std::string entryType = entry == "texture" ? "sampled-texture" :
            entry == "storageTexture_read-only" ? "readonly-storage-texture" :
            entry == "storageTexture_write-only" ? "writeonly-storage-texture" : "readwrite-storage-texture";
        skipIfStorageUnsupported(t, entryType);
        WGPUTexture sampled = createTexture(t);
        WGPUTexture sampledStorage = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_StorageBinding | WGPUTextureUsage_TextureBinding);
        WGPUTextureView sampledView = createView(t, sampled);
        WGPUTextureView storageView = createView(t, sampledStorage);
        WGPUBindGroupLayoutEntry entries[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT, WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entries[0].texture.viewDimension = WGPUTextureViewDimension_2D;
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        if (entryType == "sampled-texture") {
            entries[1].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
            entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        } else {
            entries[1].storageTexture.access = storageAccessFor(entryType);
            entries[1].storageTexture.format = WGPUTextureFormat_R32Float;
            entries[1].storageTexture.viewDimension = WGPUTextureViewDimension_2D;
        }
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 2;
        bglDesc.entries = entries;
        WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);
        WGPUBindGroupEntry bgEntries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        bgEntries[0].binding = 0;
        bgEntries[0].textureView = sampledView;
        bgEntries[1].binding = 1;
        bgEntries[1].textureView = storageView;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 2;
        bgDesc.entries = bgEntries;
        WGPUBindGroup bg0 = t.createBindGroupTracked(bgDesc);
        WGPUBindGroup bg1 = createBindGroup(t, 0, storageView, "sampled-texture", WGPUTextureViewDimension_2D);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (compute) {
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            wgpuComputePassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            if (call) {
                wgpuComputePassEncoderSetPipeline(pass, computePipeline(t));
                wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            }
            wgpuComputePassEncoderSetBindGroup(pass, 0, bg1, 0, nullptr);
            wgpuComputePassEncoderEnd(pass);
        } else {
            WGPUTexture color = createTexture(t);
            WGPURenderPassColorAttachment ca = colorAttachment(createView(t, color));
            std::vector<WGPURenderPassColorAttachment> colors = {ca};
            WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
            if (call) {
                wgpuRenderPassEncoderSetPipeline(pass, renderPipeline(t));
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            }
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg1, 0, nullptr);
            wgpuRenderPassEncoderEnd(pass);
        }
        bool success = entryType != "writeonly-storage-texture" && entryType != "readwrite-storage-texture";
        success = success || compute;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
    });

CTS_TEST(testGroup, "bindings_in_bundle")
    .desc(R"(
    Test the texture usages in bundles by using two bindings of the same texture with various combination of usages.)")
    .params([](ParamsBuilder u) {
        std::vector<Value> types = {std::string("render-target")};
        std::vector<Value> bindings = textureBindingTypes();
        types.insert(types.end(), bindings.begin(), bindings.end());
        return u.combine("type0", types)
            .combine("type1", types)
            .beginSubcases()
            .combine("binding0InBundle", {false, true})
            .combine("binding1InBundle", {false, true})
            .filter([](const ParamRecord& p) {
                const std::string type0 = valueAs<std::string>(*findParam(p, "type0"));
                const std::string type1 = valueAs<std::string>(*findParam(p, "type1"));
                const bool b0 = valueAs<bool>(*findParam(p, "binding0InBundle"));
                const bool b1 = valueAs<bool>(*findParam(p, "binding1InBundle"));
                return !((b0 && type0 == "render-target") || (b1 && type1 == "render-target") ||
                         (!b0 && !b1) ||
                         (type0 == "multisampled-texture" && type1 == "sampled-texture") ||
                         (type0 == "sampled-texture" && type1 == "multisampled-texture") ||
                         ((type0 == "multisampled-texture" || type1 == "multisampled-texture") &&
                          (isStorage(type0) || isStorage(type1))));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string type0 = t.param<std::string>("type0");
        const std::string type1 = t.param<std::string>("type1");
        const bool b0 = t.param<bool>("binding0InBundle");
        const bool b1 = t.param<bool>("binding1InBundle");
        skipIfStorageUnsupported(t, type0);
        skipIfStorageUnsupported(t, type1);
        const uint32_t sampleCount = (type0 == "multisampled-texture" || type1 == "multisampled-texture") ? 4u : 1u;
        if (sampleCount > 1 && !t.isTextureFormatMultisampled(WGPUTextureFormat_R32Float)) t.skip("r32float is not multisampled");
        WGPUTextureUsage usage = usageForType(type0) | usageForType(type1);
        if (sampleCount == 4) usage |= WGPUTextureUsage_RenderAttachment;
        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float, usage, 1, 1, sampleCount);
        WGPUTextureView view = createView(t, texture);
        WGPUBindGroup bg0 = nullptr;
        WGPUBindGroup bg1 = nullptr;
        if (type0 != "render-target") bg0 = createBindGroup(t, 0, view, type0, WGPUTextureViewDimension_2D);
        if (type1 != "render-target") bg1 = createBindGroup(t, 1, view, type1, WGPUTextureViewDimension_2D);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUTexture passTexture = (type0 == "render-target" || type1 == "render-target") ? texture : createTexture(t);
        WGPURenderPassColorAttachment ca = colorAttachment((type0 == "render-target" || type1 == "render-target") ? view : createView(t, passTexture));
        std::vector<WGPURenderPassColorAttachment> colors = {ca};
        WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
        WGPUBindGroup groups[2] = {bg0, bg1};
        bool inBundle[2] = {b0, b1};
        for (uint32_t i = 0; i < 2; ++i) {
            if (inBundle[i]) {
                WGPUTextureFormat fmt = WGPUTextureFormat_R32Float;
                WGPURenderBundleEncoderDescriptor bd = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                bd.colorFormatCount = 1;
                bd.colorFormats = &fmt;
                WGPURenderBundleEncoder be = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bd);
                wgpuRenderBundleEncoderSetBindGroup(be, i, groups[i], 0, nullptr);
                WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(be, nullptr);
                wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
            } else if (groups[i]) {
                wgpuRenderPassEncoderSetBindGroup(pass, i, groups[i], 0, nullptr);
            }
        }
        wgpuRenderPassEncoderEnd(pass);
        const bool success = (isReadOnly(type0) && isReadOnly(type1)) || type0 == type1;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
    });

CTS_TEST(testGroup, "unused_bindings_in_pipeline")
    .desc(R"(
    Test that for compute pipelines with 'auto' layout, only bindings used by the pipeline count toward the usage scope.)")
    .params([](ParamsBuilder u) {
        return u.combine("compute", {false, true})
            .combine("readOnlyUsage", {std::string("sampled-texture"), std::string("readonly-storage-texture")})
            .combine("writableUsage", {std::string("writeonly-storage-texture"), std::string("readwrite-storage-texture")})
            .combine("useBindGroup0", {false, true})
            .combine("useBindGroup1", {false, true})
            .combine("setBindGroupsOrder", {std::string("common"), std::string("reversed")})
            .combine("setPipeline", {std::string("before"), std::string("middle"), std::string("after"), std::string("none")})
            .combine("callDrawOrDispatch", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool compute = t.param<bool>("compute");
        const std::string readOnlyUsage = t.param<std::string>("readOnlyUsage");
        const std::string writableUsage = t.param<std::string>("writableUsage");
        const std::string order = t.param<std::string>("setBindGroupsOrder");
        const std::string setPipe = t.param<std::string>("setPipeline");
        const bool call = t.param<bool>("callDrawOrDispatch");
        skipIfStorageUnsupported(t, writableUsage);
        WGPUTexture texture = createTexture(t, WGPUTextureFormat_R32Float, WGPUTextureUsage_TextureBinding | WGPUTextureUsage_StorageBinding);
        WGPUTextureView view = createView(t, texture);
        WGPUBindGroup bg0 = createBindGroup(t, 0, view, readOnlyUsage, WGPUTextureViewDimension_2D);
        WGPUBindGroup bg1 = createBindGroup(t, 0, view, writableUsage, WGPUTextureViewDimension_2D);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (compute) {
            WGPUComputePipeline pipe = computePipeline(t);
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            const uint32_t i0 = order == "common" ? 0u : 1u;
            const uint32_t i1 = order == "common" ? 1u : 0u;
            if (setPipe == "before") wgpuComputePassEncoderSetPipeline(pass, pipe);
            wgpuComputePassEncoderSetBindGroup(pass, i0, bg0, 0, nullptr);
            if (setPipe == "middle") wgpuComputePassEncoderSetPipeline(pass, pipe);
            wgpuComputePassEncoderSetBindGroup(pass, i1, bg1, 0, nullptr);
            if (setPipe == "after") wgpuComputePassEncoderSetPipeline(pass, pipe);
            if (call) wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
            wgpuComputePassEncoderEnd(pass);
        } else {
            WGPURenderPipeline pipe = renderPipeline(t, nullptr, WGPUColorWriteMask_None);
            WGPUTexture color = createTexture(t);
            WGPURenderPassColorAttachment ca = colorAttachment(createView(t, color));
            std::vector<WGPURenderPassColorAttachment> colors = {ca};
            WGPURenderPassEncoder pass = beginRenderPass(encoder, colors);
            const uint32_t i0 = order == "common" ? 0u : 1u;
            const uint32_t i1 = order == "common" ? 1u : 0u;
            if (setPipe == "before") wgpuRenderPassEncoderSetPipeline(pass, pipe);
            wgpuRenderPassEncoderSetBindGroup(pass, i0, bg0, 0, nullptr);
            if (setPipe == "middle") wgpuRenderPassEncoderSetPipeline(pass, pipe);
            wgpuRenderPassEncoderSetBindGroup(pass, i1, bg1, 0, nullptr);
            if (setPipe == "after") wgpuRenderPassEncoderSetPipeline(pass, pipe);
            if (call) wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        }
        bool success = compute;
        if (call && setPipe == "none") success = false;
        t.expectValidationError([&] { t.finishTracked(encoder); }, !success);
    });

CTS_TEST(testGroup, "scope,dispatch")
    .desc(R"(
    Tests that in a compute pass, no usage validation occurs without a dispatch call.)")
    .params([](ParamsBuilder u) {
        return u.combine("dispatch", {std::string("none"), std::string("direct"), std::string("indirect")})
            .combine("pair", conflictingPairs())
            .beginSubcases()
            .combine("setBindGroup0", {false, true})
            .combine("setBindGroup1", {false, true})
            .filter([](const ParamRecord& p) {
                const std::string dispatch = valueAs<std::string>(*findParam(p, "dispatch"));
                if (dispatch == "none") return true;
                return valueAs<bool>(*findParam(p, "setBindGroup0")) &&
                       valueAs<bool>(*findParam(p, "setBindGroup1"));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string dispatch = t.param<std::string>("dispatch");
        const std::string pair = t.param<std::string>("pair");
        const bool set0 = t.param<bool>("setBindGroup0");
        const bool set1 = t.param<bool>("setBindGroup1");
        const std::string usage0 = pairFirst(pair);
        const std::string usage1 = pairSecond(pair);
        ScopeObjects o = testValidationScope(t, true, usage0, usage1);
        wgpuComputePassEncoderSetPipeline(o.computePass,
            computePipelineUsingPair(t, pipelineLayout(t, {o.layout0, o.layout1}), usage0, usage1));
        if (set0) wgpuComputePassEncoderSetBindGroup(o.computePass, 0, o.bindGroup0, 0, nullptr);
        if (set1) wgpuComputePassEncoderSetBindGroup(o.computePass, 1, o.bindGroup1, 0, nullptr);
        if (dispatch == "direct") {
            wgpuComputePassEncoderDispatchWorkgroups(o.computePass, 1, 1, 1);
        } else if (dispatch == "indirect") {
            WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
            bd.size = 12;
            bd.usage = WGPUBufferUsage_Indirect;
            WGPUBuffer indirect = t.createBufferTracked(bd);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(o.computePass, indirect, 0);
        }
        wgpuComputePassEncoderEnd(o.computePass);
        t.expectValidationError([&] { t.finishTracked(o.encoder); }, dispatch != "none" && set0 && set1);
    });

CTS_TEST(testGroup, "scope,basic,render")
    .desc(R"(
    Tests that in a render pass, validation occurs even without a pipeline or draw call.)")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("setBindGroup0", {false, true})
            .combine("setBindGroup1", {false, true})
            .combine("pair", conflictingPairs());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool set0 = t.param<bool>("setBindGroup0");
        const bool set1 = t.param<bool>("setBindGroup1");
        const std::string pair = t.param<std::string>("pair");
        ScopeObjects o = testValidationScope(t, false, pairFirst(pair), pairSecond(pair));
        if (set0) wgpuRenderPassEncoderSetBindGroup(o.renderPass, 0, o.bindGroup0, 0, nullptr);
        if (set1) wgpuRenderPassEncoderSetBindGroup(o.renderPass, 1, o.bindGroup1, 0, nullptr);
        wgpuRenderPassEncoderEnd(o.renderPass);
        t.expectValidationError([&] { t.finishTracked(o.encoder); }, set0 && set1);
    });

CTS_TEST(testGroup, "scope,pass_boundary,compute")
    .desc(R"(
    Test using two conflicting bind groups in separate dispatch calls, with/without a pass boundary.)")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("splitPass", {false, true}).combine("pair", conflictingPairs());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool split = t.param<bool>("splitPass");
        const std::string pair = t.param<std::string>("pair");
        ScopeObjects o = testValidationScope(t, true, pairFirst(pair), pairSecond(pair));
        WGPUComputePipeline pipe0 = computePipeline(t, pipelineLayout(t, {o.layout0}));
        WGPUComputePipeline pipe1 = computePipeline(t, pipelineLayout(t, {o.layout1}));
        WGPUComputePassEncoder pass = o.computePass;
        wgpuComputePassEncoderSetPipeline(pass, pipe0);
        wgpuComputePassEncoderSetBindGroup(pass, 0, o.bindGroup0, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        if (split) {
            wgpuComputePassEncoderEnd(pass);
            pass = wgpuCommandEncoderBeginComputePass(o.encoder, nullptr);
        }
        wgpuComputePassEncoderSetPipeline(pass, pipe1);
        wgpuComputePassEncoderSetBindGroup(pass, 0, o.bindGroup1, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        t.finishTracked(o.encoder);
    });

CTS_TEST(testGroup, "scope,pass_boundary,render")
    .desc(R"(
    Test using two conflicting bind groups in separate draw calls, with/without a pass boundary.)")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("splitPass", {false, true})
            .combine("draw", {false, true})
            .combine("pair", conflictingPairs());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool split = t.param<bool>("splitPass");
        const bool draw = t.param<bool>("draw");
        const std::string pair = t.param<std::string>("pair");
        ScopeObjects o = testValidationScope(t, false, pairFirst(pair), pairSecond(pair));
        WGPURenderPipeline pipe0 = renderPipeline(t, pipelineLayout(t, {o.layout0}));
        WGPURenderPipeline pipe1 = renderPipeline(t, pipelineLayout(t, {o.layout1}));
        WGPURenderPassEncoder pass = o.renderPass;
        wgpuRenderPassEncoderSetPipeline(pass, pipe0);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, o.bindGroup0, 0, nullptr);
        if (draw) wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        if (split) {
            wgpuRenderPassEncoderEnd(pass);
            WGPUTexture color = createTexture(t);
            WGPURenderPassColorAttachment ca = colorAttachment(createView(t, color));
            std::vector<WGPURenderPassColorAttachment> colors = {ca};
            pass = beginRenderPass(o.encoder, colors);
        }
        wgpuRenderPassEncoderSetPipeline(pass, pipe1);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, o.bindGroup1, 0, nullptr);
        if (draw) wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        t.expectValidationError([&] { t.finishTracked(o.encoder); }, !split);
    });

}  // namespace
