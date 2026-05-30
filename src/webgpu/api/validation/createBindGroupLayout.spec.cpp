// Ported from gpuweb/cts src/webgpu/api/validation/createBindGroupLayout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

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

} // namespace
