// Ported from gpuweb/cts src/webgpu/api/validation/dispatch.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/language_features.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,dispatch",
    "Compute dispatch validation tests.");

// The dispatch sizes shared by the linear_indexing_range tests.
struct LinearIndexingCounts {
    uint32_t x;
    uint32_t countX;
    uint32_t countY;
    uint32_t countZ;
};

LinearIndexingCounts getLinearIndexingCounts(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string builtin = t.param<std::string>("builtin");
    const std::string size = t.param<std::string>("size");

    // Other builtins are not tested due to onerous runtimes.
    if (!hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_LinearIndexing)) {
        t.skip("Missing linear_indexing language feature");
    }

    // Spec limits:
    // - maxComputeWorkgroupsPerDimension = 65535
    const uint32_t maxComputeWorkgroupsPerDimension = t.getLimits().maxComputeWorkgroupsPerDimension;
    const uint32_t x = builtin == "global_invocation_index" ? 2 : 1;
    const uint32_t y = 1;
    const uint32_t z = 1;
    const uint32_t wgSize = x * y * z;
    const uint32_t countX = maxComputeWorkgroupsPerDimension;
    const uint32_t countY = size == "max" ? maxComputeWorkgroupsPerDimension : 1;
    const uint32_t countZ = builtin == "workgroup_index" ? 2 : 1;

    const double totalInvocations = static_cast<double>(wgSize) * static_cast<double>(countX)
        * static_cast<double>(countY) * static_cast<double>(countZ);
    if (size == "max" && totalInvocations <= 4294967295.0) {
        t.skip("Uninteresting test");
    }
    return {x, countX, countY, countZ};
}

WGPUComputePipeline createAutoLayoutComputePipeline(AllFeaturesMaxLimitsGpuTest& t, const std::string& code) {
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.compute.module = t.createShaderModuleTracked(code);
    return t.createComputePipelineTracked(desc);
}

CTS_TEST(g, "dispatch,linear_indexing_range")
    .desc("Tests validation of total invocations for linear_indexing built-in values")
    .params([](ParamsBuilder u) {
        return u
            .combine("builtin", {Value(std::string("global_invocation_index")), Value(std::string("workgroup_index"))})
            .beginSubcases()
            .combine("size", {Value(std::string("max")), Value(std::string("valid"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const LinearIndexingCounts counts = getLinearIndexingCounts(t);
        const std::string builtin = t.param<std::string>("builtin");

        const std::string code =
            "\n@compute @workgroup_size(" + std::to_string(counts.x) + ", 1, 1)\n"
            "fn main(@builtin(" + builtin + ") input : u32) {\n"
            "  _ = input;\n"
            "}";

        WGPUComputePipeline computePipeline = createAutoLayoutComputePipeline(t, code);
        WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();
        WGPUComputePassEncoder computePassEncoder = t.beginComputePassTracked(commandEncoder, nullptr);
        wgpuComputePassEncoderSetPipeline(computePassEncoder, computePipeline);
        wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, counts.countX, counts.countY, counts.countZ);
        wgpuComputePassEncoderEnd(computePassEncoder);

        t.expectValidationError([&] {
            t.finishTracked(commandEncoder);
        }, t.param<std::string>("size") == "max");
    });

CTS_TEST(g, "dispatchIndirect,linear_indexing_range")
    .desc("Tests dispatchIndirect skips when linear_indexing is out of range")
    .params([](ParamsBuilder u) {
        return u
            .combine("builtin", {Value(std::string("global_invocation_index")), Value(std::string("workgroup_index"))})
            .beginSubcases()
            .combine("size", {Value(std::string("max")), Value(std::string("valid"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const LinearIndexingCounts counts = getLinearIndexingCounts(t);
        const std::string builtin = t.param<std::string>("builtin");

        constexpr uint32_t kMagic = 0xdeadbeef;
        const std::string code =
            "\n@group(0) @binding(0)\n"
            "var<storage, read_write> out : u32;\n"
            "\n"
            "@compute @workgroup_size(" + std::to_string(counts.x) + ", 1, 1)\n"
            "fn main(@builtin(" + builtin + ") input : u32,\n"
            "        @builtin(global_invocation_id) gid : vec3u) {\n"
            "  _ = input;\n"
            "  if (gid.x == 0 && gid.y == 0 && gid.z == 0) {\n"
            "    out = " + std::to_string(kMagic) + ";\n"
            "  }\n"
            "}";

        const std::array<uint32_t, 3> dispatchIndirectCounts = {counts.countX, counts.countY, counts.countZ};
        WGPUBuffer indirectBuffer = t.makeBufferWithContents(
            dispatchIndirectCounts.data(),
            sizeof(dispatchIndirectCounts),
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Indirect);
        const uint32_t zero = 0;
        WGPUBuffer outputBuffer = t.makeBufferWithContents(
            &zero, sizeof(zero), WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage);

        WGPUComputePipeline computePipeline = createAutoLayoutComputePipeline(t, code);

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = outputBuffer;
        entry.size = WGPU_WHOLE_SIZE;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = wgpuComputePipelineGetBindGroupLayout(computePipeline, 0);
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgDesc.layout);

        WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();
        WGPUComputePassEncoder computePassEncoder = t.beginComputePassTracked(commandEncoder, nullptr);
        wgpuComputePassEncoderSetPipeline(computePassEncoder, computePipeline);
        wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, bg, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(computePassEncoder, indirectBuffer, 0);
        wgpuComputePassEncoderEnd(computePassEncoder);
        WGPUCommandBuffer commandBuffer = t.finishTracked(commandEncoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        const uint32_t expected = t.param<std::string>("size") == "max" ? 0 : kMagic;
        t.expectGPUBufferValuesEqual(outputBuffer, &expected, sizeof(expected));
    });

struct RequiredSizeCase {
    const char* name;
    const char* code;
    // Size must be greater than 4
    uint64_t size;
    WGPUBufferBindingType bindingType;
    bool requiresBufferView;
    bool requiresUnrestrictedPointerParameters;
};

const std::array<RequiredSizeCase, 23> kRequiredSizeCases = {{
    {"ro_storage_32bytes",
     R"(
@group(0) @binding(0) var<storage> v : array<vec4u, 2>;
@compute @workgroup_size(1)
fn main() {
  _ = v;
})",
     32,
     WGPUBufferBindingType_ReadOnlyStorage,
     false,
     false},
    {"storage_32bytes",
     R"(
@group(0) @binding(0) var<storage, read_write> v : array<vec4u, 2>;
@compute @workgroup_size(1)
fn main() {
  _ = v;
})",
     32,
     WGPUBufferBindingType_Storage,
     false,
     false},
    {"uniform_32bytes",
     R"(
@group(0) @binding(0) var<uniform> v : array<vec4u, 2>;
@compute @workgroup_size(1)
fn main() {
  _ = v;
})",
     32,
     WGPUBufferBindingType_Uniform,
     false,
     false},
    {"ro_storage_sized_buffer",
     R"(
@group(0) @binding(0) var<storage> v : buffer<32>;
@compute @workgroup_size(1)
fn main() {
  _ = &v;
})",
     32,
     WGPUBufferBindingType_ReadOnlyStorage,
     true,
     false},
    {"storage_sized_buffer",
     R"(
@group(0) @binding(0) var<storage, read_write> v : buffer<32>;
@compute @workgroup_size(1)
fn main() {
  _ = &v;
})",
     32,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"uniform_sized_buffer",
     R"(
@group(0) @binding(0) var<uniform> v : buffer<32>;
@compute @workgroup_size(1)
fn main() {
  _ = &v;
})",
     32,
     WGPUBufferBindingType_Uniform,
     true,
     false},
    {"ro_storage_unsized_buffer_bufferView1",
     R"(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<array<u32, 16>>(&v, 0);
})",
     16*4,
     WGPUBufferBindingType_ReadOnlyStorage,
     true,
     false},
    {"ro_storage_unsized_buffer_bufferView2",
     R"(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<array<u32, 16>>(&v, 56);
})",
    // Offset does not count toward min binding size.
     16*4,
     WGPUBufferBindingType_ReadOnlyStorage,
     true,
     false},
    {"storage_unsized_buffer_bufferView3",
     R"(
struct S {
  a: vec4u,
  b: vec2u,
}
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let x = 16u;
  let p = bufferView<S>(&v, x);
})",
     32,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferView4",
     R"(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<array<vec2u>>(&v, 0);
})",
     8,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferView5",
     R"(
struct S {
  a: array<u32, 4>
}

@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<vec2u>(&v, 0);
  let q = bufferView<S>(&v, 0);
})",
     16,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferView6",
     R"(
struct S {
  a: array<u32, 4>
}

@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<u32>(&v, 16);
  let q = bufferView<S>(&v, 0);
})",
    // Offset does not count toward min binding size.
     16,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferView7",
     R"(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<vec2i>(&v, 16);
}
@fragment
fn main2() {
  let q = bufferView<vec4u>(&v, 32);
})",
    // Offset does not count toward min binding size.
     8,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferView8",
     R"(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<vec2f>(&v, 16);
}
fn foo() {
  let q = bufferView<vec4u>(&v, 32);
})",
    // Offset does not count toward min binding size.
     8,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferView9",
     R"(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<u32>(&v, 16);
  foo(&v);
}
fn foo(p : ptr<storage, buffer, read_write>) {
  let q = bufferView<vec4u>(&v, 32);
})",
    // Offset does not count toward min binding size.
     16,
     WGPUBufferBindingType_Storage,
     true,
     true},
    {"ro_storage_unsized_buffer_bufferArrayView1",
     R"(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferArrayView<array<vec2u>>(&v, 0, 32);
})",
    // Size does not count toward min binding size.
     8,
     WGPUBufferBindingType_ReadOnlyStorage,
     true,
     false},
    {"ro_storage_unsized_buffer_bufferArrayView2",
     R"(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferArrayView<array<vec2f>>(&v, 16, 32);
})",
    // Offset and size do not count toward min binding size.
     8,
     WGPUBufferBindingType_ReadOnlyStorage,
     true,
     false},
    {"ro_storage_unsized_buffer_bufferArrayView3",
     R"(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let o = 16;
  let s = 32;
  let p = bufferArrayView<array<vec2u>>(&v, o, s);
})",
     8,
     WGPUBufferBindingType_ReadOnlyStorage,
     true,
     false},
    {"ro_storage_unsized_buffer_bufferArrayView4",
     R"(
struct S {
  a: vec4u,
  b: u32,
}
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let o = 16;
  let s = 32;
  let p = bufferArrayView<array<S>>(&v, o, s);
})",
     32,
     WGPUBufferBindingType_ReadOnlyStorage,
     true,
     false},
    {"storage_unsized_buffer_bufferArrayView5",
     R"(
struct S {
  a : vec4f,
  b : vec4f,
}
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let o = 0u;
  let s = 64;
  let p1 = bufferArrayView<array<u32>>(&v, o, s);
  let p2 = bufferArrayView<array<S>>(&v, o, s);
})",
     32,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferArrayView6",
     R"(
struct S {
  a : vec4f,
  b : vec4f,
}
var<private> o : i32;
var<private> s : i32;
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferArrayView<array<vec4u>>(&v, o, s);
}
fn foo() {
  let p = bufferArrayView<array<S>>(&v, o, s);
})",
     16,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferArrayView7",
     R"(
struct S {
  a : vec4f,
  b : vec4f,
}
var<private> o : i32;
var<private> s : i32;
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferArrayView<array<vec4u>>(&v, o, s);
}
@fragment
fn foo() {
  let p = bufferArrayView<array<S>>(&v, o, s);
})",
     16,
     WGPUBufferBindingType_Storage,
     true,
     false},
    {"storage_unsized_buffer_bufferArrayView8",
     R"(
struct S {
  a : vec4f,
  b : vec4f,
}
var<private> o : i32;
var<private> s : i32;
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  bar();
  foo();
}
fn bar() {
  let p = bufferArrayView<array<vec4u>>(&v, o, s);
}
fn foo() {
  let p = bufferArrayView<array<S>>(&v, o, s);
})",
     32,
     WGPUBufferBindingType_Storage,
     true,
     false},
}};

std::vector<Value> requiredSizeCaseValues() {
    std::vector<Value> values;
    values.reserve(kRequiredSizeCases.size());
    for (const RequiredSizeCase& testcase : kRequiredSizeCases) {
        values.emplace_back(std::string(testcase.name));
    }
    return values;
}

const RequiredSizeCase& requiredSizeCase(const std::string& name) {
    for (const RequiredSizeCase& testcase : kRequiredSizeCases) {
        if (name == testcase.name) {
            return testcase;
        }
    }
    std::abort();
}

CTS_TEST(g, "shader_required_buffer_size")
    .desc("Test that dispatch time validation occurs about the required buffer size")
    .params([](ParamsBuilder u) {
        return u.combine("case", requiredSizeCaseValues())
            .beginSubcases()
            .combine("valid", {false, true})
            .combine("layout", {Value(std::string("auto")), Value(std::string("explicit"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const RequiredSizeCase& testcase = requiredSizeCase(t.param<std::string>("case"));
        const bool valid = t.param<bool>("valid");
        const bool autoLayout = t.param<std::string>("layout") == "auto";

        if (testcase.requiresBufferView && !hasBufferViewLanguageFeature()) {
            t.skip("Missing buffer_view language feature");
        }
        if (testcase.requiresUnrestrictedPointerParameters
            && !hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters)) {
            t.skip("Missing unrestricted_pointer_parameters language feature");
        }

        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = valid ? testcase.size : testcase.size - 4;
        bufferDesc.usage = testcase.bindingType == WGPUBufferBindingType_Uniform
            ? WGPUBufferUsage_Uniform
            : WGPUBufferUsage_Storage;
        WGPUBuffer buffer = t.createBufferTracked(bufferDesc);

        WGPUBindGroupLayoutEntry bglEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        bglEntry.binding = 0;
        bglEntry.visibility = WGPUShaderStage_Compute;
        bglEntry.buffer.type = testcase.bindingType;
        bglEntry.buffer.minBindingSize = 0;
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries = &bglEntry;
        WGPUBindGroupLayout bgLayout = t.createBindGroupLayoutTracked(bglDesc);
        WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.bindGroupLayoutCount = 1;
        layoutDesc.bindGroupLayouts = &bgLayout;
        WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = autoLayout ? nullptr : layout;
        pipelineDesc.compute.module = t.createShaderModuleTracked(testcase.code);
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

        WGPUBindGroupLayout pipelineBindGroupLayout = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = buffer;
        entry.size = WGPU_WHOLE_SIZE;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = pipelineBindGroupLayout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;

        if (autoLayout && !valid) {
            // 'auto' layout get minBindingSize from the shader.
            t.expectValidationError([&] {
                t.createBindGroupTracked(bgDesc);
            }, true);
        } else {
            // Expect dispatch time validation.
            WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);

            WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder computePassEncoder = t.beginComputePassTracked(commandEncoder, nullptr);
            wgpuComputePassEncoderSetPipeline(computePassEncoder, pipeline);
            wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, bg, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, 1, 1, 1);
            wgpuComputePassEncoderEnd(computePassEncoder);

            t.expectValidationError([&] {
                t.finishTracked(commandEncoder);
            }, !valid);
        }
        wgpuBindGroupLayoutRelease(pipelineBindGroupLayout);
    });

} // namespace
