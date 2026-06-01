// Ported from gpuweb/cts src/webgpu/api/validation/createBindGroupLayout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,createBindGroupLayout",
    "createBindGroupLayout validation tests.");

WGPUBindGroupLayoutEntry storageBufferEntry(uint32_t binding) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = WGPUShaderStage_Compute;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = WGPUBufferBindingType_Storage;
    return entry;
}

std::vector<Value> shaderStageCombinationValues() {
    std::vector<Value> values;
    values.reserve(kShaderStageCombinations.size());
    for (WGPUShaderStage stage : kShaderStageCombinations) {
        values.emplace_back(static_cast<uint64_t>(stage));
    }
    return values;
}

std::vector<Value> bufferBindingTypeValues() {
    std::vector<Value> values;
    values.reserve(kBufferBindingTypes.size());
    for (WGPUBufferBindingType type : kBufferBindingTypes) {
        values.emplace_back(static_cast<uint64_t>(type));
    }
    return values;
}

std::vector<Value> bindingEntryKeyValues() {
    std::vector<Value> values;
    const std::vector<std::string_view> entries = allBindingEntries(false);
    values.reserve(entries.size());
    for (std::string_view key : entries) {
        values.emplace_back(std::string(key));
    }
    return values;
}

std::vector<Value> storageTextureAccessValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kStorageTextureAccessValues.size() + 1);
    values.push_back(Value::undef());
    for (WGPUStorageTextureAccess access : kStorageTextureAccessValues) {
        values.emplace_back(static_cast<uint64_t>(access));
    }
    return values;
}

std::vector<Value> storageTextureAccessValues() {
    std::vector<Value> values;
    values.reserve(kStorageTextureAccessValues.size());
    for (WGPUStorageTextureAccess access : kStorageTextureAccessValues) {
        values.emplace_back(static_cast<uint64_t>(access));
    }
    return values;
}

std::vector<Value> textureSampleTypeValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kTextureSampleTypes.size() + 1);
    values.push_back(Value::undef());
    for (WGPUTextureSampleType sampleType : kTextureSampleTypes) {
        values.emplace_back(static_cast<uint64_t>(sampleType));
    }
    return values;
}

std::vector<Value> textureViewDimensionValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kTextureViewDimensions.size() + 1);
    values.push_back(Value::undef());
    for (WGPUTextureViewDimension dimension : kTextureViewDimensions) {
        values.emplace_back(static_cast<int64_t>(dimension));
    }
    return values;
}

std::vector<Value> allTextureFormatValues() {
    std::vector<Value> values;
    values.reserve(kAllTextureFormats.size());
    for (WGPUTextureFormat format : kAllTextureFormats) {
        values.emplace_back(static_cast<int64_t>(format));
    }
    return values;
}

bool isValidBufferTypeForStages(
    const WGPUCompatibilityModeLimits& compat,
    WGPUShaderStage visibility,
    WGPUBufferBindingType bufferType) {
    if (bufferType == WGPUBufferBindingType_Storage
        || bufferType == WGPUBufferBindingType_ReadOnlyStorage) {
        if ((visibility & WGPUShaderStage_Vertex)
            && !(compat.maxStorageBuffersInVertexStage > 0)) {
            return false;
        }
        if ((visibility & WGPUShaderStage_Fragment)
            && !(compat.maxStorageBuffersInFragmentStage > 0)) {
            return false;
        }
    }
    return true;
}

bool isValidStorageTextureForStages(
    const WGPUCompatibilityModeLimits& compat,
    WGPUShaderStage visibility) {
    if ((visibility & WGPUShaderStage_Vertex)
        && !(compat.maxStorageTexturesInVertexStage > 0)) {
        return false;
    }
    if ((visibility & WGPUShaderStage_Fragment)
        && !(compat.maxStorageTexturesInFragmentStage > 0)) {
        return false;
    }
    return true;
}

bool isValidBGLEntryForStages(
    const WGPUCompatibilityModeLimits& compat,
    WGPUShaderStage visibility,
    std::string_view entryKey) {
    if (entryKeyIsStorageTexture(entryKey)) {
        return isValidStorageTextureForStages(compat, visibility);
    }
    if (entryKeyIsBuffer(entryKey)) {
        return isValidBufferTypeForStages(compat, visibility, entryKeyBufferType(entryKey));
    }
    return true;
}

CTS_TEST(g, "duplicate_bindings")
    .desc("Test that uniqueness of binding numbers across entries is enforced.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"b0", 0}, {"b1", 1}, {"_valid", true}},
            ParamRecord{{"b0", 0}, {"b1", 0}, {"_valid", false}},
        });
    })
    .fn([](GpuTest& t) {
        WGPUBindGroupLayoutEntry entries[] = {
            storageBufferEntry(static_cast<uint32_t>(t.param<int>("b0"))),
            storageBufferEntry(static_cast<uint32_t>(t.param<int>("b1"))),
        };

        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 2;
        desc.entries = entries;

        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, !t.param<bool>("_valid"));
    });

CTS_TEST(g, "maximum_binding_limit")
    .desc("Test that a validation error is generated if the binding number exceeds the maximum binding limit.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("bindingVariant", {
            1,
            4,
            8,
            256,
            "default",
            "default-minus-one",
        });
    })
    .fn([](GpuTest& t) {
        const WGPULimits limits = t.getLimits();
        uint32_t binding = 0;
        if (t.paramIsString("bindingVariant")) {
            const std::string variant = t.param<std::string>("bindingVariant");
            if (variant == "default") {
                binding = limits.maxBindingsPerBindGroup;
            } else if (variant == "default-minus-one") {
                binding = limits.maxBindingsPerBindGroup - 1;
            } else {
                t.fail("unexpected bindingVariant");
            }
        } else {
            binding = static_cast<uint32_t>(t.param<int>("bindingVariant"));
        }

        WGPUBindGroupLayoutEntry entry = storageBufferEntry(binding);
        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;

        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, binding >= limits.maxBindingsPerBindGroup);
    });

CTS_TEST(g, "visibility")
    .desc("Test shader stage visibility validation for bind group layout entries.")
    .params([](ParamsBuilder u) {
        return u.combine("visibility", shaderStageCombinationValues())
            .beginSubcases()
            .combine("entry", bindingEntryKeyValues());
    })
    .fn([](GpuTest& t) {
        const WGPUShaderStage visibility = static_cast<WGPUShaderStage>(t.param<uint64_t>("visibility"));
        const std::string entryKey = t.param<std::string>("entry");
        const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();

        WGPUBindGroupLayoutEntry entry = bglEntryFromKey(entryKey);
        entry.binding = 0;
        entry.visibility = visibility;

        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;

        const bool success = (visibility & ~validStagesForEntryKey(entryKey)) == 0
            && isValidBGLEntryForStages(compat, visibility, entryKey);
        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, !success);
    });

CTS_TEST(g, "visibility,VERTEX_shader_stage_buffer_type")
    .desc("Test VERTEX visibility validation for buffer binding types.")
    .params([](ParamsBuilder u) {
        return u.combine("shaderStage", shaderStageCombinationValues())
            .beginSubcases()
            .combine("type", bufferBindingTypeValues());
    })
    .fn([](GpuTest& t) {
        const WGPUShaderStage shaderStage = static_cast<WGPUShaderStage>(t.param<uint64_t>("shaderStage"));
        const WGPUBufferBindingType type = static_cast<WGPUBufferBindingType>(t.param<uint64_t>("type"));
        const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();

        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = shaderStage;
        entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entry.buffer.type = type;

        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;

        const bool success = !(type == WGPUBufferBindingType_Storage
                               && (shaderStage & WGPUShaderStage_Vertex))
            && isValidBufferTypeForStages(compat, shaderStage, type);
        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, !success);
    });

CTS_TEST(g, "visibility,VERTEX_shader_stage_storage_texture_access")
    .desc("Test VERTEX visibility validation for storage texture access values.")
    .params([](ParamsBuilder u) {
        return u.combine("shaderStage", shaderStageCombinationValues())
            .beginSubcases()
            .combine("access", storageTextureAccessValuesWithUndefined());
    })
    .fn([](GpuTest& t) {
        const WGPUShaderStage shaderStage = static_cast<WGPUShaderStage>(t.param<uint64_t>("shaderStage"));
        const bool accessIsUndefined = t.paramIsUndefined("access");
        const WGPUStorageTextureAccess appliedAccess = accessIsUndefined
            ? WGPUStorageTextureAccess_WriteOnly
            : static_cast<WGPUStorageTextureAccess>(t.param<uint64_t>("access"));
        const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();

        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = shaderStage;
        entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        entry.storageTexture.access = appliedAccess;
        entry.storageTexture.format = WGPUTextureFormat_R32Uint;

        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;

        const bool success = !((shaderStage & WGPUShaderStage_Vertex)
                               && appliedAccess != WGPUStorageTextureAccess_ReadOnly)
            && isValidStorageTextureForStages(compat, shaderStage);
        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, !success);
    });

CTS_TEST(g, "multisampled_validation")
    .desc("Test multisampled texture binding layout validation.")
    .params([](ParamsBuilder u) {
        return u.combine("viewDimension", textureViewDimensionValuesWithUndefined())
            .beginSubcases()
            .combine("sampleType", textureSampleTypeValuesWithUndefined());
    })
    .fn([](GpuTest& t) {
        const bool viewDimensionIsUndefined = t.paramIsUndefined("viewDimension");
        const WGPUTextureViewDimension viewDimension = viewDimensionIsUndefined
            ? WGPUTextureViewDimension_Undefined
            : static_cast<WGPUTextureViewDimension>(t.param<int64_t>("viewDimension"));
        const bool sampleTypeIsUndefined = t.paramIsUndefined("sampleType");
        const WGPUTextureSampleType appliedSampleType = sampleTypeIsUndefined
            ? WGPUTextureSampleType_Float
            : static_cast<WGPUTextureSampleType>(t.param<uint64_t>("sampleType"));

        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
        entry.texture.multisampled = WGPU_TRUE;
        if (!viewDimensionIsUndefined) {
            entry.texture.viewDimension = viewDimension;
        }
        if (!sampleTypeIsUndefined) {
            entry.texture.sampleType = appliedSampleType;
        }

        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;

        const bool success = (viewDimension == WGPUTextureViewDimension_2D || viewDimensionIsUndefined)
            && appliedSampleType != WGPUTextureSampleType_Float;
        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, !success);
    });

CTS_TEST(g, "storage_texture,layout_dimension")
    .desc("Test storage texture binding layout view dimension validation.")
    .params([](ParamsBuilder u) {
        return u.combine("viewDimension", textureViewDimensionValuesWithUndefined());
    })
    .fn([](GpuTest& t) {
        const bool viewDimensionIsUndefined = t.paramIsUndefined("viewDimension");
        const WGPUTextureViewDimension viewDimension = viewDimensionIsUndefined
            ? WGPUTextureViewDimension_Undefined
            : static_cast<WGPUTextureViewDimension>(t.param<int64_t>("viewDimension"));

        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
        entry.storageTexture.format = WGPUTextureFormat_RGBA8Unorm;
        if (!viewDimensionIsUndefined) {
            entry.storageTexture.viewDimension = viewDimension;
        }

        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;

        const bool success = viewDimension != WGPUTextureViewDimension_Cube
            && viewDimension != WGPUTextureViewDimension_CubeArray;
        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, !success);
    });

CTS_TEST(g, "storage_texture,formats")
    .desc("Test storage texture binding layout format and access validation.")
    .params([](ParamsBuilder u) {
        return u.combine("format", allTextureFormatValues())
            .combine("access", storageTextureAccessValues());
    })
    .fn([](GpuTest& t) {
        const WGPUTextureFormat format = static_cast<WGPUTextureFormat>(t.param<int64_t>("format"));
        const WGPUStorageTextureAccess access = static_cast<WGPUStorageTextureAccess>(t.param<uint64_t>("access"));

        t.skipIfTextureFormatNotSupported(format);

        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Compute;
        entry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
        entry.storageTexture.format = format;
        entry.storageTexture.access = access;

        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = 1;
        desc.entries = &entry;

        const bool success = t.isTextureFormatUsableWithStorageAccessMode(format, access);
        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, !success);
    });

} // namespace
