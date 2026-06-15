// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/createBindGroup.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,createBindGroup",
    R"(
  createBindGroup validation tests.

  TODO: Ensure sure tests cover all createBindGroup validation rules.
)");

constexpr WGPUTextureFormat kTestFormat = WGPUTextureFormat_R32Float;
constexpr WGPUShaderStage kAllShaderStages =
    WGPUShaderStage_Compute | WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;

std::vector<Value> textureUsageValues() {
    std::vector<Value> values;
    values.reserve(kTextureUsages.size());
    for (WGPUTextureUsage usage : kTextureUsages) {
        values.emplace_back(static_cast<uint64_t>(usage));
    }
    return values;
}

std::vector<Value> bufferUsageValues() {
    std::vector<Value> values;
    values.reserve(kBufferUsages.size());
    for (WGPUBufferUsage usage : kBufferUsages) {
        values.emplace_back(static_cast<uint64_t>(usage));
    }
    return values;
}

std::vector<Value> textureViewDimensionValues() {
    std::vector<Value> values;
    values.reserve(kTextureViewDimensions.size());
    for (WGPUTextureViewDimension dimension : kTextureViewDimensions) {
        values.emplace_back(std::string(textureViewDimensionIdentifier(dimension)));
    }
    return values;
}

std::vector<Value> bindingEntryKeyValues(std::vector<std::string_view> keys) {
    std::vector<Value> values;
    values.reserve(keys.size());
    for (std::string_view key : keys) {
        values.emplace_back(std::string(key));
    }
    return values;
}

std::vector<Value> allBindingEntryValues() {
    return bindingEntryKeyValues(allBindingEntries(false));
}

std::vector<Value> bufferBindingEntryValues(bool includeUndefined) {
    std::vector<std::string_view> keys;
    if (includeUndefined) {
        keys.push_back("buffer_undefined");
    }
    keys.push_back("buffer_uniform");
    keys.push_back("buffer_storage");
    keys.push_back("buffer_read-only-storage");
    return bindingEntryKeyValues(keys);
}

std::vector<Value> sampledAndStorageBindingEntryValues(bool includeUndefined) {
    std::vector<std::string_view> keys;
    if (includeUndefined) {
        keys.push_back("texture_ms-undefined");
    }
    keys.push_back("texture_ms-false");
    keys.push_back("texture_ms-true");
    keys.push_back("storageTexture_write-only");
    keys.push_back("storageTexture_read-only");
    keys.push_back("storageTexture_read-write");
    return bindingEntryKeyValues(keys);
}

std::vector<Value> bufferBindingTypeValues() {
    std::vector<Value> values;
    values.reserve(kBufferBindingTypes.size());
    for (WGPUBufferBindingType type : kBufferBindingTypes) {
        values.emplace_back(std::string(bufferBindingTypeIdentifier(type)));
    }
    return values;
}

std::vector<Value> samplerBindingTypeValues() {
    return {
        std::string("filtering"),
        std::string("non-filtering"),
        std::string("comparison"),
    };
}

std::vector<Value> bindableResourceValues() {
    return {
        std::string("uniformBuf"),
        std::string("storageBuf"),
        std::string("filtSamp"),
        std::string("nonFiltSamp"),
        std::string("compareSamp"),
        std::string("sampledTex"),
        std::string("sampledTexMS"),
        std::string("readonlyStorageTex"),
        std::string("writeonlyStorageTex"),
        std::string("readwriteStorageTex"),
        std::string("errorBuf"),
        std::string("errorSamp"),
        std::string("errorTex"),
    };
}

std::vector<Value> resourceStateParamValues() {
    return resourceStateValues();
}

std::vector<WGPUTextureFormat> possibleStorageTextureFormats() {
    std::vector<WGPUTextureFormat> result;
    for (WGPUTextureFormat format : kStorageTextureFormats) {
        result.push_back(format);
    }
    result.push_back(WGPUTextureFormat_BGRA8Unorm);
    for (WGPUTextureFormat format : kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly) {
        result.push_back(format);
    }
    return result;
}

std::vector<Value> possibleStorageTextureFormatValues() {
    const std::vector<WGPUTextureFormat> formats = possibleStorageTextureFormats();
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

std::vector<Value> regularTextureFormatValues() {
    return formatIdentifierValues(kRegularTextureFormats);
}

std::vector<Value> compareFunctionValuesWithUndefined() {
    return {
        Value::undef(),
        std::string("never"),
        std::string("less"),
        std::string("equal"),
        std::string("less-equal"),
        std::string("greater"),
        std::string("not-equal"),
        std::string("greater-equal"),
        std::string("always"),
    };
}

WGPUCompareFunction parseCompareFunction(std::string_view value) {
    if (value == "never") return WGPUCompareFunction_Never;
    if (value == "less") return WGPUCompareFunction_Less;
    if (value == "equal") return WGPUCompareFunction_Equal;
    if (value == "less-equal") return WGPUCompareFunction_LessEqual;
    if (value == "greater") return WGPUCompareFunction_Greater;
    if (value == "not-equal") return WGPUCompareFunction_NotEqual;
    if (value == "greater-equal") return WGPUCompareFunction_GreaterEqual;
    if (value == "always") return WGPUCompareFunction_Always;
    std::abort();
}

WGPUSamplerBindingType parseSamplerBindingType(std::string_view value) {
    if (value == "filtering") return WGPUSamplerBindingType_Filtering;
    if (value == "non-filtering") return WGPUSamplerBindingType_NonFiltering;
    if (value == "comparison") return WGPUSamplerBindingType_Comparison;
    std::abort();
}

WGPUTextureDimension textureDimensionFromView(WGPUTextureViewDimension dimension) {
    switch (dimension) {
        case WGPUTextureViewDimension_1D:
            return WGPUTextureDimension_1D;
        case WGPUTextureViewDimension_2D:
        case WGPUTextureViewDimension_2DArray:
        case WGPUTextureViewDimension_Cube:
        case WGPUTextureViewDimension_CubeArray:
            return WGPUTextureDimension_2D;
        case WGPUTextureViewDimension_3D:
            return WGPUTextureDimension_3D;
        default:
            std::abort();
    }
}

bool entryKeyIsBufferLocal(std::string_view key) {
    return key == "buffer_undefined" || entryKeyIsBuffer(key);
}

bool entryKeyIsTextureLocal(std::string_view key) {
    return key == "texture_ms-undefined" || key == "texture_ms-false" || key == "texture_ms-true";
}

WGPUBufferBindingType entryKeyBufferTypeLocal(std::string_view key) {
    if (key == "buffer_undefined") return WGPUBufferBindingType_Uniform;
    return entryKeyBufferType(key);
}

bool entryKeyTextureMultisampled(std::string_view key) {
    return key == "texture_ms-true";
}

WGPUShaderStage validStagesForEntryKeyLocal(std::string_view key) {
    if (key == "buffer_undefined" || key == "texture_ms-undefined") {
        return kValidStagesAll;
    }
    return validStagesForEntryKey(key);
}

WGPUBindGroupLayoutEntry bglEntryFromKeyLocal(std::string_view key) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    if (key == "buffer_undefined") {
        entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry.buffer.type = WGPUBufferBindingType_Uniform;
    } else if (key == "texture_ms-undefined") {
        entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entry.texture.multisampled = WGPU_FALSE;
    } else {
        entry = bglEntryFromKey(key);
    }
    return entry;
}

std::string resourceForEntryKey(std::string_view key) {
    if (entryKeyIsBufferLocal(key)) {
        const WGPUBufferBindingType type = entryKeyBufferTypeLocal(key);
        return type == WGPUBufferBindingType_Uniform ? "uniformBuf" : "storageBuf";
    }
    if (key == "sampler_comparison") return "compareSamp";
    if (key == "sampler_filtering") return "filtSamp";
    if (key == "sampler_non-filtering") return "nonFiltSamp";
    if (entryKeyIsTextureLocal(key)) {
        return entryKeyTextureMultisampled(key) ? "sampledTexMS" : "sampledTex";
    }
    if (key == "storageTexture_read-only") return "readonlyStorageTex";
    if (key == "storageTexture_write-only") return "writeonlyStorageTex";
    if (key == "storageTexture_read-write") return "readwriteStorageTex";
    std::abort();
}

WGPUBufferUsage bufferUsageForEntryKey(std::string_view key) {
    return entryKeyBufferTypeLocal(key) == WGPUBufferBindingType_Uniform
        ? WGPUBufferUsage_Uniform
        : WGPUBufferUsage_Storage;
}

WGPUTextureUsage textureUsageForEntryKey(std::string_view key) {
    return entryKeyIsTextureLocal(key) ? WGPUTextureUsage_TextureBinding : WGPUTextureUsage_StorageBinding;
}

bool isStorageTextureResourceType(std::string_view resourceType) {
    return resourceType == "readonlyStorageTex"
        || resourceType == "readwriteStorageTex"
        || resourceType == "writeonlyStorageTex";
}

bool skipIfResourceNotSupportedInStages(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view entryKey,
    WGPUShaderStage visibility) {
    const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();
    if ((visibility & WGPUShaderStage_Fragment) != 0 && entryKeyIsBufferLocal(entryKey)) {
        const WGPUBufferBindingType type = entryKeyBufferTypeLocal(entryKey);
        if ((type == WGPUBufferBindingType_Storage || type == WGPUBufferBindingType_ReadOnlyStorage)
            && !(compat.maxStorageBuffersInFragmentStage >= 2)) {
            t.skip("maxStorageBuffersInFragmentStage < 2");
        }
    }
    if ((visibility & WGPUShaderStage_Fragment) != 0 && entryKeyIsStorageTexture(entryKey)
        && !(compat.maxStorageTexturesInFragmentStage >= 1)) {
        t.skip("maxStorageTexturesInFragmentStage < 1");
    }
    if ((visibility & WGPUShaderStage_Vertex) != 0 && entryKeyIsBufferLocal(entryKey)) {
        const WGPUBufferBindingType type = entryKeyBufferTypeLocal(entryKey);
        if ((type == WGPUBufferBindingType_Storage || type == WGPUBufferBindingType_ReadOnlyStorage)
            && !(compat.maxStorageBuffersInVertexStage >= 2)) {
            t.skip("maxStorageBuffersInVertexStage < 2");
        }
    }
    if ((visibility & WGPUShaderStage_Vertex) != 0 && entryKeyIsStorageTexture(entryKey)
        && !(compat.maxStorageTexturesInVertexStage >= 1)) {
        t.skip("maxStorageTexturesInVertexStage < 1");
    }
    return false;
}

WGPUBindGroupLayout makeBindGroupLayout(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::vector<WGPUBindGroupLayoutEntry>& entries) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBuffer createBufferOnDevice(
    WGPUDevice device,
    uint64_t size,
    WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return wgpuDeviceCreateBuffer(device, &desc);
}

WGPUTexture createTextureOnDevice(
    WGPUDevice device,
    WGPUTextureUsage usage,
    WGPUTextureFormat format,
    uint32_t sampleCount = 1) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{16, 16, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = sampleCount;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = usage;
    return wgpuDeviceCreateTexture(device, &desc);
}

WGPUSampler createSamplerOnDevice(WGPUDevice device, bool comparison) {
    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    if (comparison) {
        desc.compare = WGPUCompareFunction_Less;
    }
    return wgpuDeviceCreateSampler(device, &desc);
}

struct BindingResource {
    WGPUBuffer buffer = nullptr;
    WGPUSampler sampler = nullptr;
    WGPUTexture texture = nullptr;
    WGPUTextureView textureView = nullptr;
    bool untracked = false;
};

WGPUBindGroupEntry bindGroupEntryFromResource(
    uint32_t binding,
    const BindingResource& resource,
    uint64_t offset = 0,
    uint64_t size = WGPU_WHOLE_SIZE) {
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.offset = offset;
    entry.size = size;
    entry.buffer = resource.buffer;
    entry.sampler = resource.sampler;
    entry.textureView = resource.textureView;
    return entry;
}

void releaseUntrackedResource(BindingResource& resource) {
    if (!resource.untracked) {
        return;
    }
    if (resource.textureView != nullptr) {
        wgpuTextureViewRelease(resource.textureView);
        resource.textureView = nullptr;
    }
    if (resource.texture != nullptr) {
        wgpuTextureRelease(resource.texture);
        resource.texture = nullptr;
    }
    if (resource.sampler != nullptr) {
        wgpuSamplerRelease(resource.sampler);
        resource.sampler = nullptr;
    }
    if (resource.buffer != nullptr) {
        wgpuBufferRelease(resource.buffer);
        resource.buffer = nullptr;
    }
}

BindingResource makeBindingResource(
    AllFeaturesMaxLimitsGpuTest& t,
    std::string_view resourceType,
    bool mismatched = false) {
    WGPUDevice device = mismatched ? t.mismatchedDevice() : t.device();
    BindingResource resource;
    resource.untracked = mismatched;

    if (resourceType == "uniformBuf") {
        if (mismatched) {
            resource.buffer = createBufferOnDevice(device, 64, WGPUBufferUsage_Uniform);
        } else {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = 64;
            desc.usage = WGPUBufferUsage_Uniform;
            resource.buffer = t.createBufferTracked(desc);
        }
    } else if (resourceType == "storageBuf") {
        if (mismatched) {
            resource.buffer = createBufferOnDevice(device, 64, WGPUBufferUsage_Storage);
        } else {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = 64;
            desc.usage = WGPUBufferUsage_Storage;
            resource.buffer = t.createBufferTracked(desc);
        }
    } else if (resourceType == "errorBuf") {
        resource.buffer = t.getErrorBuffer();
    } else if (resourceType == "filtSamp" || resourceType == "nonFiltSamp") {
        if (mismatched) {
            WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
            if (resourceType == "filtSamp") {
                desc.minFilter = WGPUFilterMode_Linear;
            }
            resource.sampler = wgpuDeviceCreateSampler(device, &desc);
        } else {
            WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
            if (resourceType == "filtSamp") {
                desc.minFilter = WGPUFilterMode_Linear;
            }
            resource.sampler = t.createSamplerTracked(desc);
        }
    } else if (resourceType == "compareSamp") {
        if (mismatched) {
            resource.sampler = createSamplerOnDevice(device, true);
        } else {
            WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
            desc.compare = WGPUCompareFunction_Less;
            resource.sampler = t.createSamplerTracked(desc);
        }
    } else if (resourceType == "errorSamp") {
        WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        desc.lodMinClamp = -1.0f;
        t.expectValidationError([&] {
            resource.sampler = t.createSamplerTracked(desc);
        }, true);
    } else {
        WGPUTextureUsage usage = WGPUTextureUsage_TextureBinding;
        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        uint32_t sampleCount = 1;
        if (resourceType == "sampledTexMS") {
            usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
            sampleCount = 4;
        } else if (resourceType == "readonlyStorageTex"
                   || resourceType == "writeonlyStorageTex"
                   || resourceType == "readwriteStorageTex") {
            usage = WGPUTextureUsage_StorageBinding;
            format = kTestFormat;
        } else if (resourceType == "errorTex") {
            usage = WGPUTextureUsage_None;
        }

        if (mismatched) {
            resource.texture = createTextureOnDevice(device, usage, format, sampleCount);
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            resource.textureView = wgpuTextureCreateView(resource.texture, &viewDesc);
        } else if (resourceType == "errorTex") {
            WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            desc.size = WGPUExtent3D{16, 16, 1};
            desc.mipLevelCount = 1;
            desc.sampleCount = sampleCount;
            desc.dimension = WGPUTextureDimension_2D;
            desc.format = format;
            desc.usage = WGPUTextureUsage_TextureBinding;
            resource.texture = t.createTextureWithState(ResourceState::Invalid, desc);
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            t.expectValidationError([&] {
                resource.textureView = t.createViewTracked(resource.texture, viewDesc);
            }, true);
        } else {
            WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            desc.size = WGPUExtent3D{16, 16, 1};
            desc.mipLevelCount = 1;
            desc.sampleCount = sampleCount;
            desc.dimension = WGPUTextureDimension_2D;
            desc.format = format;
            desc.usage = usage;
            resource.texture = t.createTextureTracked(desc);
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            resource.textureView = t.createViewTracked(resource.texture, viewDesc);
        }
    }
    return resource;
}

void createBindGroupWithEntries(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBindGroupLayout layout,
    const std::vector<WGPUBindGroupEntry>& entries) {
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    t.createBindGroupTracked(desc);
}

ParamsBuilder externalTextureUsageParams(ParamsBuilder u) {
    return u.combine("usage0", textureUsageValues())
        .combine("usage1", textureUsageValues())
        .filter([](const ParamRecord& params) {
            const WGPUTextureUsage usage0 =
                static_cast<WGPUTextureUsage>(valueAs<uint64_t>(*findParam(params, "usage0")));
            const WGPUTextureUsage usage1 =
                static_cast<WGPUTextureUsage>(valueAs<uint64_t>(*findParam(params, "usage1")));
            return isValidTextureUsageCombination(usage0 | usage1);
        });
}

CTS_TEST(g, "binding_count_mismatch")
    .desc("Test that the number of entries must match the number of entries in the BindGroupLayout.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("layoutEntryCount", {1, 2, 3})
            .combine("bindGroupEntryCount", {1, 2, 3});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int layoutEntryCount = t.param<int>("layoutEntryCount");
        const int bindGroupEntryCount = t.param<int>("bindGroupEntryCount");

        std::vector<WGPUBindGroupLayoutEntry> layoutEntries;
        for (int i = 0; i < layoutEntryCount; ++i) {
            WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entry.binding = static_cast<uint32_t>(i);
            entry.visibility = WGPUShaderStage_Compute;
            entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            entry.buffer.type = WGPUBufferBindingType_Storage;
            layoutEntries.push_back(entry);
        }
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, layoutEntries);

        BindingResource storageBuffer = makeBindingResource(t, "storageBuf");
        std::vector<WGPUBindGroupEntry> entries;
        for (int i = 0; i < bindGroupEntryCount; ++i) {
            entries.push_back(bindGroupEntryFromResource(static_cast<uint32_t>(i), storageBuffer));
        }

        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, layoutEntryCount != bindGroupEntryCount);
    });

CTS_TEST(g, "binding_must_be_present_in_layout")
    .desc("Test that the binding slot for each entry matches a binding slot defined in the BindGroupLayout.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("layoutBinding", {0, 1, 2}).combine("binding", {0, 1, 2});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t layoutBinding = static_cast<uint32_t>(t.param<int>("layoutBinding"));
        const uint32_t binding = static_cast<uint32_t>(t.param<int>("binding"));

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = layoutBinding;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        BindingResource storageBuffer = makeBindingResource(t, "storageBuf");
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(binding, storageBuffer)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, layoutBinding != binding);
    });

CTS_TEST(g, "binding_must_contain_resource_defined_in_layout")
    .desc("Test that only compatible resource types specified in the BindGroupLayout are allowed for each entry.")
    .params([](ParamsBuilder u) {
        return u.combine("resourceType", bindableResourceValues()).combine("entry", allBindingEntryValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string resourceType = t.param<std::string>("resourceType");
        const std::string entryKey = t.param<std::string>("entry");

        WGPUBindGroupLayoutEntry layoutEntry = bglEntryFromKeyLocal(entryKey);
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        BindingResource resource = makeBindingResource(t, resourceType);
        const std::string expectedResource = resourceForEntryKey(entryKey);
        bool compatible = false;
        if (expectedResource == "filtSamp") {
            compatible = resourceType == "filtSamp" || resourceType == "nonFiltSamp";
        } else if (expectedResource == "nonFiltSamp") {
            compatible = resourceType == "nonFiltSamp";
        } else if (isStorageTextureResourceType(expectedResource)) {
            compatible = isStorageTextureResourceType(resourceType);
        } else {
            compatible = expectedResource == resourceType;
        }

        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, !compatible);
    });

CTS_TEST(g, "texture_binding_must_have_correct_usage")
    .desc("Tests that texture bindings must have the correct usage.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("entry", sampledAndStorageBindingEntryValues(false))
            .combine("usage", textureUsageValues())
            .filter([](const ParamRecord& params) {
                const std::string entryKey = valueAs<std::string>(*findParam(params, "entry"));
                const WGPUTextureUsage usage =
                    static_cast<WGPUTextureUsage>(valueAs<uint64_t>(*findParam(params, "usage")));
                if (usage == WGPUTextureUsage_TransientAttachment && !entryKeyTextureMultisampled(entryKey)) {
                    return false;
                }
                return !(usage == WGPUTextureUsage_StorageBinding && entryKeyTextureMultisampled(entryKey));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string entryKey = t.param<std::string>("entry");
        const WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(t.param<uint64_t>("usage"));

        if (entryKeyTextureMultisampled(entryKey) && !t.isTextureFormatMultisampled(kTestFormat)) {
            t.skip("The test requires r32float multisampled support.");
        }
        if ((usage & WGPUTextureUsage_TransientAttachment) != 0) {
            t.skipIfTransientAttachmentNotSupported();
        }

        WGPUBindGroupLayoutEntry layoutEntry = bglEntryFromKeyLocal(entryKey);
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUTextureUsage appliedUsage = usage;
        if (entryKeyTextureMultisampled(entryKey)) {
            appliedUsage = appliedUsage | WGPUTextureUsage_RenderAttachment;
        }
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{16, 16, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = entryKeyTextureMultisampled(entryKey) ? 4 : 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = kTestFormat;
        texDesc.usage = appliedUsage;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);

        BindingResource resource;
        resource.texture = texture;
        resource.textureView = view;
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, (usage & textureUsageForEntryKey(entryKey)) == 0);
    });

CTS_TEST(g, "texture_must_have_correct_component_type")
    .desc(R"(
    Tests that texture bindings must have a format that matches the sample type specified in the BindGroupLayout.
    - Tests a compatible format for every sample type
    - Tests an incompatible format for every sample type)")
    .params([](ParamsBuilder u) {
        return u.combine("sampleType", {std::string("float"), std::string("sint"), std::string("uint")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string sampleTypeString = t.param<std::string>("sampleType");
        const WGPUTextureSampleType sampleType = parseTextureSampleType(sampleTypeString);

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Fragment;
        layoutEntry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        layoutEntry.texture.sampleType = sampleType;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUTextureFormat goodFormat = WGPUTextureFormat_R8Unorm;
        if (sampleType == WGPUTextureSampleType_Sint) {
            goodFormat = WGPUTextureFormat_R8Sint;
        } else if (sampleType == WGPUTextureSampleType_Uint) {
            goodFormat = WGPUTextureFormat_R8Uint;
        }

        auto makeView = [&](WGPUTextureFormat format) {
            WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            desc.size = WGPUExtent3D{16, 16, 1};
            desc.mipLevelCount = 1;
            desc.sampleCount = 1;
            desc.dimension = WGPUTextureDimension_2D;
            desc.format = format;
            desc.usage = WGPUTextureUsage_TextureBinding;
            WGPUTexture texture = t.createTextureTracked(desc);
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            return t.createViewTracked(texture, viewDesc);
        };

        BindingResource goodResource;
        goodResource.textureView = makeView(goodFormat);
        createBindGroupWithEntries(t, layout, {bindGroupEntryFromResource(0, goodResource)});

        const std::array<WGPUTextureFormat, 3> candidates = {
            WGPUTextureFormat_R8Unorm,
            WGPUTextureFormat_R8Sint,
            WGPUTextureFormat_R8Uint,
        };
        for (WGPUTextureFormat badFormat : candidates) {
            if (badFormat == goodFormat) {
                continue;
            }
            BindingResource badResource;
            badResource.textureView = makeView(badFormat);
            const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, badResource)};
            t.expectValidationError([&] {
                createBindGroupWithEntries(t, layout, entries);
            }, true);
        }
    });

CTS_TEST(g, "texture_must_have_correct_dimension")
    .desc(R"(
    Test that bound texture views match the dimensions supplied in the BindGroupLayout
      - Test for every GPUTextureViewDimension
      - Test for both TEXTURE_BINDING and STORAGE_BINDING.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("usage", {static_cast<uint64_t>(WGPUTextureUsage_TextureBinding),
                                   static_cast<uint64_t>(WGPUTextureUsage_StorageBinding)})
            .combine("viewDimension", textureViewDimensionValues())
            .filter([](const ParamRecord& params) {
                const WGPUTextureUsage usage =
                    static_cast<WGPUTextureUsage>(valueAs<uint64_t>(*findParam(params, "usage")));
                const WGPUTextureViewDimension viewDimension =
                    parseTextureViewDimension(valueAs<std::string>(*findParam(params, "viewDimension")));
                return !(usage == WGPUTextureUsage_StorageBinding
                         && (viewDimension == WGPUTextureViewDimension_Cube
                             || viewDimension == WGPUTextureViewDimension_CubeArray));
            })
            .beginSubcases()
            .combine("dimension", textureViewDimensionValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureUsage usage = static_cast<WGPUTextureUsage>(t.param<uint64_t>("usage"));
        const WGPUTextureViewDimension viewDimension = parseTextureViewDimension(t.param<std::string>("viewDimension"));
        const WGPUTextureViewDimension dimension = parseTextureViewDimension(t.param<std::string>("dimension"));

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        if (usage == WGPUTextureUsage_TextureBinding) {
            layoutEntry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
            layoutEntry.texture.sampleType = WGPUTextureSampleType_Float;
            layoutEntry.texture.viewDimension = viewDimension;
        } else {
            layoutEntry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
            layoutEntry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
            layoutEntry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
            layoutEntry.storageTexture.viewDimension = viewDimension;
        }
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        uint32_t height = 16;
        uint32_t depthOrArrayLayers = 6;
        if (dimension == WGPUTextureViewDimension_1D) {
            height = 1;
            depthOrArrayLayers = 1;
        }

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{16, height, depthOrArrayLayers};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = textureDimensionFromView(dimension);
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = usage;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        t.skipIfTextureViewDimensionNotSupported(viewDimension);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.dimension = dimension;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);

        BindingResource resource;
        resource.textureView = view;
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, viewDimension != dimension);
    });

CTS_TEST(g, "multisampled_validation")
    .desc(R"(
    Test that the sample count of the texture is greater than 1 if the BindGroup entry's
    multisampled is true. Otherwise, the texture's sampleCount should be 1.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("multisampled", {true, false}).beginSubcases().combine("sampleCount", {1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool multisampled = t.param<bool>("multisampled");
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        layoutEntry.texture.multisampled = multisampled ? WGPU_TRUE : WGPU_FALSE;
        if (multisampled) {
            layoutEntry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        } else {
            layoutEntry.texture.sampleType = WGPUTextureSampleType_Float;
        }
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{16, 16, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = sampleCount;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);

        BindingResource resource;
        resource.textureView = view;
        const bool isValid = (!multisampled && sampleCount == 1) || (multisampled && sampleCount > 1);
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, !isValid);
    });

CTS_TEST(g, "buffer_offset_and_size_for_bind_groups_match")
    .desc(R"(
    Test that a buffer binding's [offset, offset + size) must be contained in the BindGroup entry's buffer.
    - Test for various offsets and sizes)")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"offset", 0}, {"size", 512}, {"_success", true}},
            ParamRecord{{"offset", 256}, {"size", 256}, {"_success", true}},
            ParamRecord{{"bindBufferResource", true}, {"_success", true}},
            ParamRecord{{"offset", 0}, {"size", 1024}, {"_success", true}},
            ParamRecord{{"offset", 0}, {"size", Value::undef()}, {"_success", true}},
            ParamRecord{{"offset", 256 * 3}, {"size", 256}, {"_success", true}},
            ParamRecord{{"offset", 256 * 3}, {"size", Value::undef()}, {"_success", true}},
            ParamRecord{{"offset", 0}, {"size", 0}, {"_success", false}},
            ParamRecord{{"offset", 256}, {"size", 0}, {"_success", false}},
            ParamRecord{{"offset", 1024}, {"size", 0}, {"_success", false}},
            ParamRecord{{"offset", 1024}, {"size", Value::undef()}, {"_success", false}},
            ParamRecord{{"offset", 1}, {"size", 256}, {"_success", false}},
            ParamRecord{{"offset", 1}, {"size", Value::undef()}, {"_success", false}},
            ParamRecord{{"offset", 127}, {"size", 256}, {"_success", false}},
            ParamRecord{{"offset", 255}, {"size", 256}, {"_success", false}},
            ParamRecord{{"offset", 256 * 5}, {"size", 0}, {"_success", false}},
            ParamRecord{{"offset", 0}, {"size", 256 * 5}, {"_success", false}},
            ParamRecord{{"offset", 1024}, {"size", 1}, {"_success", false}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = 1024;
        bufferDesc.usage = WGPUBufferUsage_Storage;
        BindingResource resource;
        resource.buffer = t.createBufferTracked(bufferDesc);

        const bool bindBufferResource = t.hasParam("bindBufferResource") && t.param<bool>("bindBufferResource");
        const uint64_t offset = bindBufferResource ? 0 : static_cast<uint64_t>(t.param<int>("offset"));
        const uint64_t size = bindBufferResource || t.paramIsUndefined("size")
            ? WGPU_WHOLE_SIZE
            : static_cast<uint64_t>(t.param<int>("size"));
        const bool success = t.param<bool>("_success");

        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource, offset, size)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, !success);
    });

CTS_TEST(g, "minBindingSize")
    .desc("Tests that minBindingSize is correctly enforced.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("minBindingSize", {Value::undef(), 4, 8, 256})
            .expand("size", [](const ParamRecord& params) {
                const Value* minValue = findParam(params, "minBindingSize");
                if (std::holds_alternative<Value::Undefined>(minValue->data())) {
                    return std::vector<Value>{4, 256};
                }
                const int minSize = valueAs<int>(*minValue);
                return std::vector<Value>{minSize - 4, minSize, minSize + 4};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t minBindingSize = t.paramIsUndefined("minBindingSize")
            ? 0
            : static_cast<uint64_t>(t.param<int>("minBindingSize"));
        const uint64_t size = static_cast<uint64_t>(t.param<int>("size"));

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = WGPUBufferBindingType_Storage;
        layoutEntry.buffer.minBindingSize = minBindingSize;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = size;
        bufferDesc.usage = WGPUBufferUsage_Storage;
        BindingResource resource;
        resource.buffer = t.createBufferTracked(bufferDesc);
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, minBindingSize != 0 && size < minBindingSize);
    });

CTS_TEST(g, "buffer,resource_state")
    .desc("Test bind group creation with various buffer resource states")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("state", resourceStateParamValues())
            .combine("entry", bufferBindingEntryValues(true))
            .combine("visibilityMask", {static_cast<uint64_t>(kAllShaderStages),
                                        static_cast<uint64_t>(WGPUShaderStage_Compute)})
            .combine("bindBufferResource", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ResourceState state = parseResourceState(t.param<std::string>("state"));
        const std::string entryKey = t.param<std::string>("entry");
        const WGPUShaderStage visibility =
            static_cast<WGPUShaderStage>(validStagesForEntryKeyLocal(entryKey) & t.param<uint64_t>("visibilityMask"));
        skipIfResourceNotSupportedInStages(t, entryKey, visibility);

        WGPUBindGroupLayoutEntry layoutEntry = bglEntryFromKeyLocal(entryKey);
        layoutEntry.binding = 0;
        layoutEntry.visibility = visibility;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = 4;
        bufferDesc.usage = bufferUsageForEntryKey(entryKey);
        BindingResource resource;
        resource.buffer = t.createBufferWithState(state, bufferDesc);
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, state == ResourceState::Invalid);
    });

CTS_TEST(g, "texture,resource_state")
    .desc("Test bind group creation with various texture resource states")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("state", resourceStateParamValues())
            .combine("entry", sampledAndStorageBindingEntryValues(true))
            .combine("visibilityMask", {static_cast<uint64_t>(kAllShaderStages),
                                        static_cast<uint64_t>(WGPUShaderStage_Compute)})
            .combine("bindTextureResource", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ResourceState state = parseResourceState(t.param<std::string>("state"));
        const std::string entryKey = t.param<std::string>("entry");
        const WGPUShaderStage visibility =
            static_cast<WGPUShaderStage>(validStagesForEntryKeyLocal(entryKey) & t.param<uint64_t>("visibilityMask"));
        skipIfResourceNotSupportedInStages(t, entryKey, visibility);

        WGPUBindGroupLayoutEntry layoutEntry = bglEntryFromKeyLocal(entryKey);
        layoutEntry.binding = 0;
        layoutEntry.visibility = visibility;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUTextureUsage usage = textureUsageForEntryKey(entryKey);
        if (entryKeyTextureMultisampled(entryKey)) {
            usage = usage | WGPUTextureUsage_RenderAttachment;
        }
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{1, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = entryKeyTextureMultisampled(entryKey) ? 4 : 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = entryKeyIsStorageTexture(entryKey) ? WGPUTextureFormat_R32Float : WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = usage;
        BindingResource resource;
        resource.texture = t.createTextureWithState(state, texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        t.expectValidationError([&] {
            resource.textureView = t.createViewTracked(resource.texture, viewDesc);
        }, state == ResourceState::Invalid);

        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, state == ResourceState::Invalid);
    });

CTS_TEST(g, "bind_group_layout,device_mismatch")
    .desc("Tests createBindGroup cannot be called with a bind group layout created from another device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
        std::vector<WGPUBindGroupLayoutEntry> layoutEntries = {layoutEntry};
        WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.entryCount = layoutEntries.size();
        layoutDesc.entries = layoutEntries.data();
        WGPUBindGroupLayout layout = mismatched
            ? t.createBindGroupLayoutOnMismatchedDevice(layoutDesc)
            : t.createBindGroupLayoutTracked(layoutDesc);

        BindingResource resource = makeBindingResource(t, "uniformBuf");
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, mismatched);
    });

CTS_TEST(g, "binding_resources,device_mismatch")
    .desc(R"(
    Tests createBindGroup cannot be called with various resources created from another device
    Test with two resources to make sure all resources can be validated:
    - resource0 and resource1 from same device
    - resource0 and resource1 from different device

    TODO: test GPUExternalTexture as a resource
    )")
    .params([](ParamsBuilder u) {
        return u.combine("entry", bindingEntryKeyValues({
                "buffer_storage",
                "sampler_filtering",
                "texture_ms-false",
                "storageTexture_write-only",
                "storageTexture_read-only",
                "storageTexture_read-write",
            }))
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"resource0Mismatched", false}, {"resource1Mismatched", false}},
                ParamRecord{{"resource0Mismatched", true}, {"resource1Mismatched", false}},
                ParamRecord{{"resource0Mismatched", false}, {"resource1Mismatched", true}},
            })
            .combine("visibilityMask", {static_cast<uint64_t>(kAllShaderStages),
                                        static_cast<uint64_t>(WGPUShaderStage_Compute)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string entryKey = t.param<std::string>("entry");
        const bool resource0Mismatched = t.param<bool>("resource0Mismatched");
        const bool resource1Mismatched = t.param<bool>("resource1Mismatched");
        const WGPUShaderStage visibility =
            static_cast<WGPUShaderStage>(validStagesForEntryKeyLocal(entryKey) & t.param<uint64_t>("visibilityMask"));
        skipIfResourceNotSupportedInStages(t, entryKey, visibility);

        BindingResource resource0 = makeBindingResource(t, resourceForEntryKey(entryKey), resource0Mismatched);
        BindingResource resource1 = makeBindingResource(t, resourceForEntryKey(entryKey), resource1Mismatched);

        WGPUBindGroupLayoutEntry layoutEntry0 = bglEntryFromKeyLocal(entryKey);
        layoutEntry0.binding = 0;
        layoutEntry0.visibility = visibility;
        WGPUBindGroupLayoutEntry layoutEntry1 = layoutEntry0;
        layoutEntry1.binding = 1;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry0, layoutEntry1});

        const std::vector<WGPUBindGroupEntry> entries = {
            bindGroupEntryFromResource(0, resource0),
            bindGroupEntryFromResource(1, resource1),
        };
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, resource0Mismatched || resource1Mismatched);

        releaseUntrackedResource(resource0);
        releaseUntrackedResource(resource1);
    });

CTS_TEST(g, "storage_texture,usage")
    .desc(R"(
    Test that the texture usage contains STORAGE_BINDING if the BindGroup entry defines
    storageTexture.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("usage0", textureUsageValues())
            .combine("usage1", textureUsageValues())
            .filter([](const ParamRecord& params) {
                const WGPUTextureUsage usage0 =
                    static_cast<WGPUTextureUsage>(valueAs<uint64_t>(*findParam(params, "usage0")));
                const WGPUTextureUsage usage1 =
                    static_cast<WGPUTextureUsage>(valueAs<uint64_t>(*findParam(params, "usage1")));
                return isValidTextureUsageCombination(usage0 | usage1);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureUsage usage0 = static_cast<WGPUTextureUsage>(t.param<uint64_t>("usage0"));
        const WGPUTextureUsage usage1 = static_cast<WGPUTextureUsage>(t.param<uint64_t>("usage1"));
        const WGPUTextureUsage usage = usage0 | usage1;
        if ((usage & WGPUTextureUsage_TransientAttachment) != 0) {
            t.skipIfTransientAttachmentNotSupported();
        }

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        layoutEntry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        layoutEntry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{16, 16, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = usage;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);
        BindingResource resource;
        resource.textureView = view;
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, (usage & WGPUTextureUsage_StorageBinding) == 0);
    });

CTS_TEST(g, "storage_texture,mip_level_count")
    .desc(R"(
    Test that the mip level count of the resource of the BindGroup entry as a descriptor is 1 if the
    BindGroup entry defines storageTexture. If the mip level count is not 1, a validation error
    should be generated.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("baseMipLevel", {1, 2}).combine("mipLevelCount", {1, 2});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t baseMipLevel = static_cast<uint32_t>(t.param<int>("baseMipLevel"));
        const uint32_t mipLevelCount = static_cast<uint32_t>(t.param<int>("mipLevelCount"));

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        layoutEntry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        layoutEntry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{16, 16, 1};
        texDesc.mipLevelCount = 4;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_StorageBinding;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.baseMipLevel = baseMipLevel;
        viewDesc.mipLevelCount = mipLevelCount;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);
        BindingResource resource;
        resource.textureView = view;
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, mipLevelCount != 1);
    });

CTS_TEST(g, "storage_texture,format")
    .desc(R"(
    Test that the format of the storage texture is equal to resource's descriptor format if the
    BindGroup entry defines storageTexture.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("storageTextureFormat", possibleStorageTextureFormatValues())
            .combine("resourceFormat", possibleStorageTextureFormatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat storageTextureFormat = parseTextureFormat(t.param<std::string>("storageTextureFormat"));
        const WGPUTextureFormat resourceFormat = parseTextureFormat(t.param<std::string>("resourceFormat"));
        t.skipIfTextureFormatNotSupported(storageTextureFormat);
        t.skipIfTextureFormatNotSupported(resourceFormat);
        if (!t.isTextureFormatUsableWithStorageAccessMode(storageTextureFormat, WGPUStorageTextureAccess_WriteOnly)
            || !t.isTextureFormatUsableWithStorageAccessMode(resourceFormat, WGPUStorageTextureAccess_WriteOnly)) {
            t.skip("format is not usable as write-only storage texture");
        }

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        layoutEntry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        layoutEntry.storageTexture.format = storageTextureFormat;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{16, 16, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        texDesc.format = resourceFormat;
        texDesc.usage = WGPUTextureUsage_StorageBinding;
        WGPUTexture texture = t.createTextureTracked(texDesc);
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.format = resourceFormat;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);
        BindingResource resource;
        resource.textureView = view;
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, storageTextureFormat != resourceFormat);
    });

CTS_TEST(g, "buffer,usage")
    .desc(R"(
    Test that the buffer usage contains 'UNIFORM' if the BindGroup entry defines buffer and it's
    type is 'uniform', and the buffer usage contains 'STORAGE' if the BindGroup entry's buffer type
    is 'storage'|read-only-storage'.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("type", bufferBindingTypeValues())
            .beginSubcases()
            .combine("usage0", bufferUsageValues())
            .combine("usage1", bufferUsageValues())
            .filter([](const ParamRecord& params) {
                const WGPUBufferUsage usage0 =
                    static_cast<WGPUBufferUsage>(valueAs<uint64_t>(*findParam(params, "usage0")));
                const WGPUBufferUsage usage1 =
                    static_cast<WGPUBufferUsage>(valueAs<uint64_t>(*findParam(params, "usage1")));
                return ((usage0 | usage1) & (WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite)) == 0;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUBufferBindingType type = parseBufferBindingType(t.param<std::string>("type"));
        const WGPUBufferUsage usage =
            static_cast<WGPUBufferUsage>(t.param<uint64_t>("usage0") | t.param<uint64_t>("usage1"));

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = type;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = 4;
        bufferDesc.usage = usage;
        BindingResource resource;
        resource.buffer = t.createBufferTracked(bufferDesc);
        const bool isValid = type == WGPUBufferBindingType_Uniform
            ? (usage & WGPUBufferUsage_Uniform) != 0
            : (usage & WGPUBufferUsage_Storage) != 0;
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, !isValid);
    });

CTS_TEST(g, "buffer,resource_offset")
    .desc(R"(
    Test that the resource.offset of the BindGroup entry is a multiple of limits.
    'minUniformBufferOffsetAlignment|minStorageBufferOffsetAlignment' if the BindGroup entry defines
    buffer and the buffer type is 'uniform|storage|read-only-storage'.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("type", bufferBindingTypeValues())
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"offsetAdd", 0}, {"offsetMult", 0.0}},
                ParamRecord{{"offsetAdd", 0}, {"offsetMult", 0.5}},
                ParamRecord{{"offsetAdd", 0}, {"offsetMult", 1.5}},
                ParamRecord{{"offsetAdd", 2}, {"offsetMult", 0.0}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUBufferBindingType type = parseBufferBindingType(t.param<std::string>("type"));
        const WGPULimits limits = t.getLimits();
        const uint64_t minAlignment = type == WGPUBufferBindingType_Uniform
            ? limits.minUniformBufferOffsetAlignment
            : limits.minStorageBufferOffsetAlignment;
        const uint64_t offset = static_cast<uint64_t>(static_cast<double>(minAlignment) * t.param<double>("offsetMult"))
            + static_cast<uint64_t>(t.param<int>("offsetAdd"));

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = type;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = 1024;
        bufferDesc.usage = type == WGPUBufferBindingType_Uniform ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage;
        BindingResource resource;
        resource.buffer = t.createBufferTracked(bufferDesc);
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource, offset)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, offset % minAlignment != 0);
    });

CTS_TEST(g, "buffer,resource_binding_size")
    .desc(R"(
    Test that the buffer binding size of the BindGroup entry is equal to or less than limits.
    'maxUniformBufferBindingSize|maxStorageBufferBindingSize' if the BindGroup entry defines
    buffer and the buffer type is 'uniform|storage|read-only-storage'.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("type", bufferBindingTypeValues())
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"bindingSizeBase", 1}, {"bindingSizeLimit", 0}},
                ParamRecord{{"bindingSizeBase", 0}, {"bindingSizeLimit", 1}},
                ParamRecord{{"bindingSizeBase", 1}, {"bindingSizeLimit", 1}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUBufferBindingType type = parseBufferBindingType(t.param<std::string>("type"));
        const WGPULimits limits = t.getLimits();
        const uint64_t mult = type == WGPUBufferBindingType_Uniform ? 1 : 4;
        uint64_t maxBindingSize = type == WGPUBufferBindingType_Uniform
            ? limits.maxUniformBufferBindingSize
            : limits.maxStorageBufferBindingSize;
        if (type != WGPUBufferBindingType_Uniform) {
            maxBindingSize = (maxBindingSize / 4) * 4;
        }
        const uint64_t bindingSize =
            static_cast<uint64_t>(t.param<int>("bindingSizeBase")) * mult
            + maxBindingSize * static_cast<uint64_t>(t.param<int>("bindingSizeLimit"));

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = type;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = maxBindingSize;
        bufferDesc.usage = type == WGPUBufferBindingType_Uniform ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage;
        BindingResource resource;
        resource.buffer = t.createBufferTracked(bufferDesc);
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource, 0, bindingSize)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, bindingSize > maxBindingSize);
    });

CTS_TEST(g, "buffer,effective_buffer_binding_size")
    .desc(R"(
  Test that the effective buffer binding size of the BindGroup entry must be a multiple of 4 if the
  buffer type is 'storage|read-only-storage', while there is no such restriction on uniform buffers.
)")
    .params([](ParamsBuilder u) {
        return u.combine("type", bufferBindingTypeValues())
            .beginSubcases()
            .combine("offsetMult", {0, 1})
            .combine("bufferSizeAddition", {8, 10})
            .combine("bindingSize", {Value::undef(), 2, 4, 6});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUBufferBindingType type = parseBufferBindingType(t.param<std::string>("type"));
        const WGPULimits limits = t.getLimits();
        const uint64_t minAlignment = type == WGPUBufferBindingType_Uniform
            ? limits.minUniformBufferOffsetAlignment
            : limits.minStorageBufferOffsetAlignment;
        const uint64_t offset = minAlignment * static_cast<uint64_t>(t.param<int>("offsetMult"));
        const uint64_t bufferSize = minAlignment + static_cast<uint64_t>(t.param<int>("bufferSizeAddition"));
        const uint64_t bindingSize = t.paramIsUndefined("bindingSize")
            ? WGPU_WHOLE_SIZE
            : static_cast<uint64_t>(t.param<int>("bindingSize"));
        const uint64_t effectiveBindingSize = bindingSize == WGPU_WHOLE_SIZE ? bufferSize - offset : bindingSize;

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        layoutEntry.buffer.type = type;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = bufferSize;
        bufferDesc.usage = type == WGPUBufferBindingType_Uniform ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage;
        BindingResource resource;
        resource.buffer = t.createBufferTracked(bufferDesc);
        const bool isValid = type == WGPUBufferBindingType_Uniform || effectiveBindingSize % 4 == 0;
        const std::vector<WGPUBindGroupEntry> entries =
            {bindGroupEntryFromResource(0, resource, offset, bindingSize)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, !isValid);
    });

CTS_TEST(g, "sampler,device_mismatch")
    .desc("Tests createBindGroup cannot be called with a sampler created from another device.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        layoutEntry.sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        BindingResource resource = makeBindingResource(t, "filtSamp", mismatched);
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, mismatched);
        releaseUntrackedResource(resource);
    });

CTS_TEST(g, "sampler,compare_function_with_binding_type")
    .desc(R"(
  Test that the sampler of the BindGroup has a 'compareFunction' value if the sampler type of the
  BindGroupLayout is 'comparison'. Other sampler types should not have 'compare' field in
  the descriptor of the sampler.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("bgType", samplerBindingTypeValues())
            .beginSubcases()
            .combine("compareFunction", compareFunctionValuesWithUndefined());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string bgType = t.param<std::string>("bgType");
        const bool compareUndefined = t.paramIsUndefined("compareFunction");

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.sampler = WGPU_SAMPLER_BINDING_LAYOUT_INIT;
        layoutEntry.sampler.type = parseSamplerBindingType(bgType);
        WGPUBindGroupLayout layout = makeBindGroupLayout(t, {layoutEntry});

        WGPUSamplerDescriptor samplerDesc = WGPU_SAMPLER_DESCRIPTOR_INIT;
        if (!compareUndefined) {
            samplerDesc.compare = parseCompareFunction(t.param<std::string>("compareFunction"));
        }
        BindingResource resource;
        resource.sampler = t.createSamplerTracked(samplerDesc);

        const bool isValid = bgType == "comparison" ? !compareUndefined : compareUndefined;
        const std::vector<WGPUBindGroupEntry> entries = {bindGroupEntryFromResource(0, resource)};
        t.expectValidationError([&] {
            createBindGroupWithEntries(t, layout, entries);
        }, !isValid);
    });

CTS_TEST(g, "external_texture,texture_view,usage")
    .desc(R"(
    Test that the external texture usage contains TEXTURE_BINDING if the BindGroup entry defines
    externalTexture for a GPUTextureView resource.
  )")
    .params(externalTextureUsageParams)
    .unimplemented("N/A: native webgpu headers do not expose GPUExternalTexture creation/binding.");

CTS_TEST(g, "external_texture,texture_view,dimension")
    .desc(R"(
    Test that the external texture dimension is 2d if the BindGroup entry defines
    externalTexture for a GPUTextureView resource.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("dimension", textureViewDimensionValues());
    })
    .unimplemented("N/A: native webgpu headers do not expose GPUExternalTexture creation/binding.");

CTS_TEST(g, "external_texture,texture_view,mip_level_count")
    .desc(R"(
    Test that the mip level count of the resource of the BindGroup entry as a descriptor is 1 if the
    BindGroup entry defines externalTexture for a GPUTextureView resource. If the mip level count is
    not 1, a validation error should be generated.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("baseMipLevel", {1, 2}).combine("mipLevelCount", {1, 2});
    })
    .unimplemented("N/A: native webgpu headers do not expose GPUExternalTexture creation/binding.");

CTS_TEST(g, "external_texture,texture_view,format")
    .desc(R"(
    Test that the format of the external texture is "rgba8unorm", "bgra8unorm", or "rgba16float"
    if the BindGroup entry defines externalTexture for a GPUTextureView resource.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("format", regularTextureFormatValues());
    })
    .unimplemented("N/A: native webgpu headers do not expose GPUExternalTexture creation/binding.");

CTS_TEST(g, "external_texture,texture_view,sample_count")
    .desc(R"(
  Test that the sample count of the resource of the BindGroup entry as a texture is 1 if the
  BindGroup entry defines externalTexture for a GPUTextureView resource. If the sample count is
  not 1, a validation error should be generated.
)")
    .params([](ParamsBuilder u) {
        return u.combine("sampleCount", {1, 4});
    })
    .unimplemented("N/A: native webgpu headers do not expose GPUExternalTexture creation/binding.");

} // namespace
