// Ported from gpuweb/cts src/webgpu/api/validation/createPipelineLayout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,createPipelineLayout",
    "createPipelineLayout validation tests.");

WGPUBindGroupLayout createEmptyBindGroupLayout(GpuTest& t) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBindGroupLayout createBindGroupLayoutWithEntries(
    GpuTest& t,
    const std::vector<WGPUBindGroupLayoutEntry>& entries) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

WGPUBindGroupLayout createBindGroupLayoutOnMismatchedDeviceWithEntries(
    GpuTest& t,
    const std::vector<WGPUBindGroupLayoutEntry>& entries) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutOnMismatchedDevice(desc);
}

void createPipelineLayoutWithBindGroupLayouts(GpuTest& t, const std::vector<WGPUBindGroupLayout>& layouts) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.data();
    t.createPipelineLayoutTracked(desc);
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

std::string_view bufferTypeKey(WGPUBufferBindingType type) {
    switch (type) {
        case WGPUBufferBindingType_Uniform:
            return "buffer_uniform";
        case WGPUBufferBindingType_Storage:
            return "buffer_storage";
        case WGPUBufferBindingType_ReadOnlyStorage:
            return "buffer_read-only-storage";
        default:
            std::abort();
    }
}

std::vector<Value> bufferBindingTypeValues() {
    std::vector<Value> values;
    values.reserve(kBufferBindingTypes.size());
    for (WGPUBufferBindingType type : kBufferBindingTypes) {
        values.emplace_back(static_cast<uint64_t>(type));
    }
    return values;
}

inline constexpr std::array<std::string_view, 4> kMaybeNullBGLTypes = {
    "Null",
    "Undefined",
    "Empty",
    "NonEmpty",
};

std::vector<Value> maybeNullBGLTypeValues() {
    std::vector<Value> values;
    values.reserve(kMaybeNullBGLTypes.size());
    for (std::string_view type : kMaybeNullBGLTypes) {
        values.emplace_back(std::string(type));
    }
    return values;
}

std::vector<Value> maybeNullBGLTypeValuesIfActive(const ParamRecord& params, int slot) {
    const int bglCount = valueAs<int>(*findParam(params, "_bglCount"));
    if (bglCount > slot) {
        return maybeNullBGLTypeValues();
    }
    return {Value("Null")};
}

std::string paramString(const ParamRecord& params, std::string_view key) {
    return valueAs<std::string>(*findParam(params, key));
}

std::string bglSlotParamName(int index) {
    return "_bgl" + std::to_string(index);
}

bool hasExactlyOneMaybeNullBGLSlot(const ParamRecord& params) {
    const int bglCount = valueAs<int>(*findParam(params, "_bglCount"));
    int maybeNullCount = 0;
    for (int i = 0; i < bglCount; ++i) {
        if (paramString(params, bglSlotParamName(i)) != "NonEmpty") {
            ++maybeNullCount;
        }
    }
    return maybeNullCount == 1;
}

WGPUBindGroupLayoutEntry textureEntry(uint32_t binding) {
    WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    entry.binding = binding;
    entry.visibility = WGPUShaderStage_Compute;
    entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
    entry.texture.sampleType = WGPUTextureSampleType_Float;
    return entry;
}

WGPUBindGroupLayout createMaybeNullBindGroupLayout(GpuTest& t, const std::string& type) {
    if (type == "Null" || type == "Undefined") {
        return nullptr;
    }
    if (type == "Empty") {
        return createEmptyBindGroupLayout(t);
    }
    if (type == "NonEmpty") {
        return createBindGroupLayoutWithEntries(t, {textureEntry(0)});
    }
    t.fail("unexpected bind group layout type");
}

CTS_TEST(g, "number_of_bind_group_layouts_exceeds_the_maximum_value")
    .desc("Test that creating a pipeline layout fails if the number of bind group layouts exceeds the maximum value.")
    .fn([](GpuTest& t) {
        const WGPULimits limits = t.getLimits();
        WGPUBindGroupLayout layout = createEmptyBindGroupLayout(t);

        std::vector<WGPUBindGroupLayout> maxBindGroupLayouts(limits.maxBindGroups, layout);
        t.expectValidationError([&] {
            createPipelineLayoutWithBindGroupLayouts(t, maxBindGroupLayouts);
        }, false);

        std::vector<WGPUBindGroupLayout> tooManyBindGroupLayouts(limits.maxBindGroups + 1, layout);
        t.expectValidationError([&] {
            createPipelineLayoutWithBindGroupLayouts(t, tooManyBindGroupLayouts);
        }, true);
    });

CTS_TEST(g, "bind_group_layouts,null_bind_group_layouts")
    .desc("Test that pipeline layouts accept null, undefined, and empty bind group layout slots.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("_bglCount", {1, 2, 3, 4})
            .combine("_bgl0", maybeNullBGLTypeValues())
            .expand("_bgl1", [](const ParamRecord& params) {
                return maybeNullBGLTypeValuesIfActive(params, 1);
            })
            .expand("_bgl2", [](const ParamRecord& params) {
                return maybeNullBGLTypeValuesIfActive(params, 2);
            })
            .expand("_bgl3", [](const ParamRecord& params) {
                return maybeNullBGLTypeValuesIfActive(params, 3);
            })
            .filter(hasExactlyOneMaybeNullBGLSlot);
    })
    .fn([](GpuTest& t) {
        const int bglCount = t.param<int>("_bglCount");
        std::vector<WGPUBindGroupLayout> layouts;
        layouts.reserve(static_cast<size_t>(bglCount));
        for (int i = 0; i < bglCount; ++i) {
            layouts.push_back(createMaybeNullBindGroupLayout(t, t.param<std::string>(bglSlotParamName(i))));
        }

        t.expectValidationError([&] {
            createPipelineLayoutWithBindGroupLayouts(t, layouts);
        }, false);
    });

CTS_TEST(g, "number_of_dynamic_buffers_exceeds_the_maximum_value")
    .desc("Test that creating a pipeline layout fails if the dynamic buffer count exceeds the maximum value.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("visibility", {Value(0), Value(2), Value(4), Value(6)})
            .combine("type", bufferBindingTypeValues());
    })
    .fn([](GpuTest& t) {
        const WGPUShaderStage visibility = static_cast<WGPUShaderStage>(t.param<int>("visibility"));
        const WGPUBufferBindingType type = static_cast<WGPUBufferBindingType>(t.param<uint64_t>("type"));
        const WGPULimits limits = t.getLimits();
        const uint32_t maxDynamicLimit = bufferTypeMaxDynamicBuffersLimit(limits, type);
        const uint32_t limit = getBindingLimitForBindingType(t, visibility, bufferTypeKey(type));
        const uint32_t maxDynamic = std::min(maxDynamicLimit, limit);

        if (limit == 0) {
            t.skip("binding limit for type == 0");
        }

        std::vector<WGPUBindGroupLayoutEntry> maxDynamicEntries;
        maxDynamicEntries.reserve(maxDynamic);
        for (uint32_t b = 0; b < maxDynamic; ++b) {
            maxDynamicEntries.push_back(bufferEntry(b, visibility, type, true));
        }
        WGPUBindGroupLayout bgl0 = createBindGroupLayoutWithEntries(t, maxDynamicEntries);

        if (limit > maxDynamic) {
            WGPUBindGroupLayout good = createBindGroupLayoutWithEntries(
                t,
                {bufferEntry(0, visibility, type, false)});
            createPipelineLayoutWithBindGroupLayouts(t, {bgl0, good});
        }

        WGPUBindGroupLayout bad = createBindGroupLayoutWithEntries(
            t,
            {bufferEntry(0, visibility, type, true)});
        t.expectValidationError([&] {
            createPipelineLayoutWithBindGroupLayouts(t, {bgl0, bad});
        }, true);
    });

CTS_TEST(g, "immediate_data_size")
    .desc("Test immediate data size validation.")
    .params([](ParamsBuilder u) {
        return u.combine("immediateSize", {
            Value(0),
            Value(4),
            Value("max"),
            Value(1),
            Value(2),
            Value(3),
            Value(5),
            Value("exceedLimit"),
        });
    })
    .fn([](GpuTest& t) {
        const uint32_t maxImmediateSize = t.getLimits().maxImmediateSize;
        if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
            t.skip("immediate data not supported");
        }

        uint32_t size = 0;
        if (t.paramIsString("immediateSize")) {
            const std::string variant = t.param<std::string>("immediateSize");
            if (variant == "max") {
                size = maxImmediateSize;
            } else if (variant == "exceedLimit") {
                size = maxImmediateSize + 4;
            } else {
                t.fail("unexpected immediateSize");
            }
        } else {
            size = static_cast<uint32_t>(t.param<int>("immediateSize"));
        }

        WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        desc.immediateSize = size;

        const bool shouldSucceed = size % 4 == 0 && size <= maxImmediateSize;
        t.expectValidationError([&] {
            t.createPipelineLayoutTracked(desc);
        }, !shouldSucceed);
    });

CTS_TEST(g, "bind_group_layouts,device_mismatch")
    .desc("Test that pipeline layouts reject bind group layouts created from another device.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"layout0Mismatched", false}, {"layout1Mismatched", false}},
            ParamRecord{{"layout0Mismatched", true}, {"layout1Mismatched", false}},
            ParamRecord{{"layout0Mismatched", false}, {"layout1Mismatched", true}},
        });
    })
    .fn([](GpuTest& t) {
        const bool layout0Mismatched = t.param<bool>("layout0Mismatched");
        const bool layout1Mismatched = t.param<bool>("layout1Mismatched");

        const std::vector<WGPUBindGroupLayoutEntry> emptyEntries;
        WGPUBindGroupLayout layout0 = layout0Mismatched
            ? createBindGroupLayoutOnMismatchedDeviceWithEntries(t, emptyEntries)
            : createBindGroupLayoutWithEntries(t, emptyEntries);
        WGPUBindGroupLayout layout1 = layout1Mismatched
            ? createBindGroupLayoutOnMismatchedDeviceWithEntries(t, emptyEntries)
            : createBindGroupLayoutWithEntries(t, emptyEntries);

        t.expectValidationError([&] {
            createPipelineLayoutWithBindGroupLayouts(t, {layout0, layout1});
        }, layout0Mismatched || layout1Mismatched);
    });

} // namespace
