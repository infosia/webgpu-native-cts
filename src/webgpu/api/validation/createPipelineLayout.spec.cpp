// Ported from gpuweb/cts src/webgpu/api/validation/createPipelineLayout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
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

} // namespace
