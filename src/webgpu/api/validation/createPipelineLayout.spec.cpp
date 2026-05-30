// Ported from gpuweb/cts src/webgpu/api/validation/createPipelineLayout.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,createPipelineLayout",
    "createPipelineLayout validation tests.");

WGPUBindGroupLayout createEmptyBindGroupLayout(GpuTest& t) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    return t.createBindGroupLayoutTracked(desc);
}

void createPipelineLayoutWithBindGroupLayouts(GpuTest& t, const std::vector<WGPUBindGroupLayout>& layouts) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.data();
    t.createPipelineLayoutTracked(desc);
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

} // namespace
