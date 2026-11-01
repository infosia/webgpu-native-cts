// Ported from gpuweb/cts src/webgpu/api/validation/createBindGroupLayout.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa

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

WGPUBindGroupLayoutEntry bufferEntry(
    uint32_t binding,
    WGPUShaderStage visibility,
    WGPUBufferBindingType type,
    bool hasDynamicOffset) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = visibility;
    entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    entry.buffer.type = type;
    entry.buffer.hasDynamicOffset = hasDynamicOffset ? WGPU_TRUE : WGPU_FALSE;
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

std::vector<Value> shaderStageValues() {
    std::vector<Value> values;
    values.reserve(kShaderStages.size());
    for (WGPUShaderStage stage : kShaderStages) {
        values.emplace_back(static_cast<uint64_t>(stage));
    }
    return values;
}

std::vector<Value> bufferBindingTypeValues() {
    std::vector<Value> values;
    values.reserve(kBufferBindingTypes.size());
    for (WGPUBufferBindingType type : kBufferBindingTypes) {
        values.emplace_back(std::string(bufferBindingTypeIdentifier(type)));
    }
    return values;
}

std::vector<Value> bindingEntryKeyValues(std::vector<std::string_view> entries) {
    std::vector<Value> values;
    values.reserve(entries.size());
    for (std::string_view key : entries) {
        values.emplace_back(std::string(key));
    }
    return values;
}

std::vector<Value> bindingEntryKeyValues() {
    return bindingEntryKeyValues(allBindingEntries(false));
}

std::vector<Value> storageTextureAccessValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kStorageTextureAccessValues.size() + 1);
    values.push_back(Value::undef());
    for (WGPUStorageTextureAccess access : kStorageTextureAccessValues) {
        values.emplace_back(std::string(storageTextureAccessIdentifier(access)));
    }
    return values;
}

std::vector<Value> storageTextureAccessValues() {
    std::vector<Value> values;
    values.reserve(kStorageTextureAccessValues.size());
    for (WGPUStorageTextureAccess access : kStorageTextureAccessValues) {
        values.emplace_back(std::string(storageTextureAccessIdentifier(access)));
    }
    return values;
}

std::vector<Value> textureSampleTypeValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kTextureSampleTypes.size() + 1);
    values.push_back(Value::undef());
    for (WGPUTextureSampleType sampleType : kTextureSampleTypes) {
        values.emplace_back(std::string(textureSampleTypeIdentifier(sampleType)));
    }
    return values;
}

std::vector<Value> textureViewDimensionValuesWithUndefined() {
    std::vector<Value> values;
    values.reserve(kTextureViewDimensions.size() + 1);
    values.push_back(Value::undef());
    for (WGPUTextureViewDimension dimension : kTextureViewDimensions) {
        values.emplace_back(std::string(textureViewDimensionIdentifier(dimension)));
    }
    return values;
}

std::vector<Value> allTextureFormatValues() {
    return formatIdentifierValues(kAllTextureFormats);
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

std::vector<WGPUBindGroupLayoutEntry> makeEntries(
    std::string_view entryKey,
    WGPUShaderStage visibility,
    uint32_t count,
    uint32_t firstBinding = 0) {
    std::vector<WGPUBindGroupLayoutEntry> entries;
    entries.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        WGPUBindGroupLayoutEntry entry = bglEntryFromKey(entryKey);
        entry.binding = firstBinding + i;
        entry.visibility = visibility;
        entries.push_back(entry);
    }
    return entries;
}

WGPUBindGroupLayout createBindGroupLayoutWithEntries(
    GpuTest& t,
    const std::vector<WGPUBindGroupLayoutEntry>& entries) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

void createPipelineLayoutWithBindGroupLayouts(
    GpuTest& t,
    const std::vector<WGPUBindGroupLayout>& layouts) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.data();
    t.createPipelineLayoutTracked(desc);
}

ParamsBuilder kMaxResourcesCases(ParamsBuilder u) {
    return u.combine("maxedEntry", bindingEntryKeyValues())
        .beginSubcases()
        .combine("maxedVisibility", shaderStageValues())
        .filter([](const ParamRecord& params) {
            const std::string maxedEntry = valueAs<std::string>(*findParam(params, "maxedEntry"));
            const WGPUShaderStage maxedVisibility =
                static_cast<WGPUShaderStage>(valueAs<uint64_t>(*findParam(params, "maxedVisibility")));
            return (validStagesForEntryKey(maxedEntry) & maxedVisibility) != 0;
        })
        .expand("extraEntry", [](const ParamRecord& params) {
            const std::string maxedEntry = valueAs<std::string>(*findParam(params, "maxedEntry"));
            std::vector<std::string_view> extraEntries = pickExtraBindingTypesForPerStage(maxedEntry, true);
            std::vector<std::string_view> differentClassEntries =
                pickExtraBindingTypesForPerStage(maxedEntry, false);
            extraEntries.insert(extraEntries.end(), differentClassEntries.begin(), differentClassEntries.end());
            return bindingEntryKeyValues(extraEntries);
        })
        .combine("extraVisibility", shaderStageValues())
        .filter([](const ParamRecord& params) {
            const std::string extraEntry = valueAs<std::string>(*findParam(params, "extraEntry"));
            const WGPUShaderStage extraVisibility =
                static_cast<WGPUShaderStage>(valueAs<uint64_t>(*findParam(params, "extraVisibility")));
            return (validStagesForEntryKey(extraEntry) & extraVisibility) != 0;
        });
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
        const WGPUBufferBindingType type = parseBufferBindingType(t.param<std::string>("type"));
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
            : parseStorageTextureAccess(t.param<std::string>("access"));
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
            : parseTextureViewDimension(t.param<std::string>("viewDimension"));
        const bool sampleTypeIsUndefined = t.paramIsUndefined("sampleType");
        const WGPUTextureSampleType appliedSampleType = sampleTypeIsUndefined
            ? WGPUTextureSampleType_Float
            : parseTextureSampleType(t.param<std::string>("sampleType"));

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
            : parseTextureViewDimension(t.param<std::string>("viewDimension"));

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
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const WGPUStorageTextureAccess access = parseStorageTextureAccess(t.param<std::string>("access"));

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

CTS_TEST(g, "max_dynamic_buffers")
    .desc("Test dynamic-offset buffer count limits.")
    .params([](ParamsBuilder u) {
        return u.combine("type", bufferBindingTypeValues())
            .beginSubcases()
            .combine("extraDynamicBuffers", {Value(0), Value(1)})
            .combine("staticBuffers", {Value(0), Value(1)});
    })
    .fn([](GpuTest& t) {
        const WGPUBufferBindingType type = parseBufferBindingType(t.param<std::string>("type"));
        const uint32_t extraDynamicBuffers = static_cast<uint32_t>(t.param<int>("extraDynamicBuffers"));
        const uint32_t staticBuffers = static_cast<uint32_t>(t.param<int>("staticBuffers"));
        const WGPULimits limits = t.getLimits();
        const uint32_t maxDynamic = bufferTypeMaxDynamicBuffersLimit(limits, type);
        const uint32_t dynamicBufferCount = maxDynamic + extraDynamicBuffers;
        const uint32_t perStageLimit = bufferTypePerStageComputeLimit(limits, type);

        std::vector<WGPUBindGroupLayoutEntry> entries;
        entries.reserve(static_cast<size_t>(dynamicBufferCount) + staticBuffers);
        for (uint32_t i = 0; i < dynamicBufferCount; ++i) {
            entries.push_back(bufferEntry(i, WGPUShaderStage_Compute, type, true));
        }
        for (uint32_t i = dynamicBufferCount; i < dynamicBufferCount + staticBuffers; ++i) {
            entries.push_back(bufferEntry(i, WGPUShaderStage_Compute, type, false));
        }

        WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        desc.entryCount = entries.size();
        desc.entries = entries.data();

        const bool shouldError = extraDynamicBuffers > 0 || entries.size() > perStageLimit;
        t.expectValidationError([&] {
            t.createBindGroupLayoutTracked(desc);
        }, shouldError);
    });

CTS_TEST(g, "max_resources_per_stage,in_bind_group_layout")
    .desc("Test per-stage binding resource limits in one bind group layout.")
    .params(kMaxResourcesCases)
    .fn([](GpuTest& t) {
        const std::string maxedEntry = t.param<std::string>("maxedEntry");
        const WGPUShaderStage maxedVisibility =
            static_cast<WGPUShaderStage>(t.param<uint64_t>("maxedVisibility"));
        const std::string extraEntry = t.param<std::string>("extraEntry");
        const WGPUShaderStage extraVisibility =
            static_cast<WGPUShaderStage>(t.param<uint64_t>("extraVisibility"));
        const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();

        if (!isValidBGLEntryForStages(compat, extraVisibility, extraEntry)) {
            t.skip("extra entry is not valid for the requested shader stage");
        }

        const uint32_t maxedCount = getBindingLimitForBindingType(t, maxedVisibility, maxedEntry);
        std::vector<WGPUBindGroupLayoutEntry> entries =
            makeEntries(maxedEntry, maxedVisibility, maxedCount);
        createBindGroupLayoutWithEntries(t, entries);

        WGPUBindGroupLayoutEntry extra = bglEntryFromKey(extraEntry);
        extra.binding = maxedCount;
        extra.visibility = extraVisibility;
        entries.push_back(extra);

        const bool sameLimit = (maxedVisibility & extraVisibility) != 0
            && bglEntryPerStageLimitClass(maxedEntry) == bglEntryPerStageLimitClass(extraEntry);
        t.expectValidationError([&] {
            createBindGroupLayoutWithEntries(t, entries);
        }, sameLimit);
    });

CTS_TEST(g, "max_resources_per_stage,in_pipeline_layout")
    .desc("Test per-stage binding resource limits across pipeline layout bind group layouts.")
    .params(kMaxResourcesCases)
    .fn([](GpuTest& t) {
        const std::string maxedEntry = t.param<std::string>("maxedEntry");
        const WGPUShaderStage maxedVisibility =
            static_cast<WGPUShaderStage>(t.param<uint64_t>("maxedVisibility"));
        const std::string extraEntry = t.param<std::string>("extraEntry");
        const WGPUShaderStage extraVisibility =
            static_cast<WGPUShaderStage>(t.param<uint64_t>("extraVisibility"));
        const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();

        if (!isValidBGLEntryForStages(compat, extraVisibility, extraEntry)) {
            t.skip("extra entry is not valid for the requested shader stage");
        }

        const uint32_t maxedCount = getBindingLimitForBindingType(t, maxedVisibility, maxedEntry);
        const std::vector<WGPUBindGroupLayoutEntry> maxedEntries =
            makeEntries(maxedEntry, maxedVisibility, maxedCount);
        WGPUBindGroupLayout goodLayout = createBindGroupLayoutWithEntries(t, maxedEntries);
        createPipelineLayoutWithBindGroupLayouts(t, {goodLayout});

        const std::vector<WGPUBindGroupLayoutEntry> extraEntries =
            makeEntries(extraEntry, extraVisibility, 1);
        WGPUBindGroupLayout extraLayout = createBindGroupLayoutWithEntries(t, extraEntries);

        const bool sameLimit = (maxedVisibility & extraVisibility) != 0
            && bglEntryPerStageLimitClass(maxedEntry) == bglEntryPerStageLimitClass(extraEntry);
        t.expectValidationError([&] {
            createPipelineLayoutWithBindGroupLayouts(t, {goodLayout, extraLayout});
        }, sameLimit);
    });

// BINDINGS_OF_TYPES: at most one member of an entry is set per value. The union of two values
// mirrors upstream's `{ ...entry1, ...entry2 }`. Object params are not representable as a `Value`,
// so they are named after the member they set ("none" is upstream's `{}`).
std::vector<Value> bindingsOfTypesValues() {
    return {
        std::string("none"),
        std::string("buffer"),
        std::string("sampler"),
        std::string("texture"),
        std::string("storageTexture"),
    };
}

void addBindingOfType(WGPUBindGroupLayoutEntry& entry, const std::string& type) {
    if (type == "buffer") {
        entry.buffer.type = WGPUBufferBindingType_Uniform;
    } else if (type == "sampler") {
        entry.sampler.type = WGPUSamplerBindingType_Comparison;
    } else if (type == "texture") {
        entry.texture.multisampled = WGPU_FALSE;
        entry.texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
    } else if (type == "storageTexture") {
        entry.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
        entry.storageTexture.format = WGPUTextureFormat_R32Uint;
    }
}

CTS_TEST(g, "binding_type")
    .desc(
        "Test that exactly one of the binding types is specified for each entry.\n"
        "- Test each type of bind group resource.\n"
        "- Test that a validation error is generated if more than one type is specified.\n"
        "- Test that a validation error is generated if no type is specified.")
    .params([](ParamsBuilder u) {
        return u.combine("entry1", bindingsOfTypesValues()).combine("entry2", bindingsOfTypesValues());
    })
    .fn([](GpuTest& t) {
        const std::string entry1 = t.param<std::string>("entry1");
        const std::string entry2 = t.param<std::string>("entry2");

        // The INIT macro leaves all four binding members at their "not used" value.
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = 0;
        entry.visibility = WGPUShaderStage_Vertex;
        addBindingOfType(entry, entry1);
        addBindingOfType(entry, entry2);

        const size_t joinedKeys = entry1 == entry2 ? (entry1 == "none" ? 0u : 1u)
            : (entry1 == "none" || entry2 == "none") ? 1u
                                                     : 2u;

        t.expectValidationError([&] {
            createBindGroupLayoutWithEntries(t, {entry});
        }, joinedKeys != 1);
    });

} // namespace
