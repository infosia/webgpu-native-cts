// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/min_buffer_binding_size.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.

#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "common/webgpu/backend.h"
#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/language_features.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,min_buffer_binding_size",
    "Minimum buffer binding size tests");

struct RequiredSizeCase {
    const char* name;
    const char* code;
    uint64_t size;  // Size must be greater than 4
    WGPUBufferBindingType bindingType;
    bool requiresBufferView;
    bool requiresUnrestrictedPointerParameters;
};

const std::vector<RequiredSizeCase> kRequiredSizeCases = {
    {
        "ro_storage_32bytes",
        R"wgsl(
@group(0) @binding(0) var<storage> v : array<vec4u, 2>;
@compute @workgroup_size(1)
fn main() {
  _ = v;
})wgsl",
        32,
        WGPUBufferBindingType_ReadOnlyStorage,
        false,
        false,
    },
    {
        "storage_32bytes",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : array<vec4u, 2>;
@compute @workgroup_size(1)
fn main() {
  _ = v;
})wgsl",
        32,
        WGPUBufferBindingType_Storage,
        false,
        false,
    },
    {
        "uniform_32bytes",
        R"wgsl(
@group(0) @binding(0) var<uniform> v : array<vec4u, 2>;
@compute @workgroup_size(1)
fn main() {
  _ = v;
})wgsl",
        32,
        WGPUBufferBindingType_Uniform,
        false,
        false,
    },
    {
        "ro_storage_sized_buffer",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer<32>;
@compute @workgroup_size(1)
fn main() {
  _ = &v;
})wgsl",
        32,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "storage_sized_buffer",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer<32>;
@compute @workgroup_size(1)
fn main() {
  _ = &v;
})wgsl",
        32,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "uniform_sized_buffer",
        R"wgsl(
@group(0) @binding(0) var<uniform> v : buffer<32>;
@compute @workgroup_size(1)
fn main() {
  _ = &v;
})wgsl",
        32,
        WGPUBufferBindingType_Uniform,
        true,
        false,
    },
    {
        "ro_storage_unsized_buffer_bufferView1",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<array<u32, 16>>(&v, 0);
})wgsl",
        16 * 4,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "ro_storage_unsized_buffer_bufferView2",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<array<u32, 16>>(&v, 56);
})wgsl",
        // Offset does not count toward min binding size.
        16 * 4,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferView3",
        R"wgsl(
struct S {
  a: vec4u,
  b: vec2u,
}
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let x = 16u;
  let p = bufferView<S>(&v, x);
})wgsl",
        32,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferView4",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<array<vec2u>>(&v, 0);
})wgsl",
        8,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferView5",
        R"wgsl(
struct S {
  a: array<u32, 4>
}

@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<vec2u>(&v, 0);
  let q = bufferView<S>(&v, 0);
})wgsl",
        16,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferView6",
        R"wgsl(
struct S {
  a: array<u32, 4>
}

@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<u32>(&v, 16);
  let q = bufferView<S>(&v, 0);
})wgsl",
        // Offset does not count toward min binding size.
        16,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferView7",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<vec2i>(&v, 16);
}
@fragment
fn main2() {
  let q = bufferView<vec4u>(&v, 32);
})wgsl",
        // Offset does not count toward min binding size.
        8,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferView8",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<vec2f>(&v, 16);
}
fn foo() {
  let q = bufferView<vec4u>(&v, 32);
})wgsl",
        // Offset does not count toward min binding size.
        8,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferView9",
        R"wgsl(
@group(0) @binding(0) var<storage, read_write> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferView<u32>(&v, 16);
  foo(&v);
}
fn foo(p : ptr<storage, buffer, read_write>) {
  let q = bufferView<vec4u>(&v, 32);
})wgsl",
        // Offset does not count toward min binding size.
        16,
        WGPUBufferBindingType_Storage,
        true,
        true,
    },
    {
        "ro_storage_unsized_buffer_bufferArrayView1",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferArrayView<array<vec2u>>(&v, 0, 32);
})wgsl",
        // Size does not count toward min binding size.
        8,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "ro_storage_unsized_buffer_bufferArrayView2",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let p = bufferArrayView<array<vec2f>>(&v, 16, 32);
})wgsl",
        // Offset and size do not count toward min binding size.
        8,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "ro_storage_unsized_buffer_bufferArrayView3",
        R"wgsl(
@group(0) @binding(0) var<storage> v : buffer;
@compute @workgroup_size(1)
fn main() {
  let o = 16;
  let s = 32;
  let p = bufferArrayView<array<vec2u>>(&v, o, s);
})wgsl",
        8,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "ro_storage_unsized_buffer_bufferArrayView4",
        R"wgsl(
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
})wgsl",
        32,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferArrayView5",
        R"wgsl(
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
})wgsl",
        32,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferArrayView6",
        R"wgsl(
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
})wgsl",
        16,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferArrayView7",
        R"wgsl(
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
})wgsl",
        16,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
    {
        "storage_unsized_buffer_bufferArrayView8",
        R"wgsl(
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
})wgsl",
        32,
        WGPUBufferBindingType_Storage,
        true,
        false,
    },
};

const std::vector<RequiredSizeCase> kRequiredRenderSizeCases = {
    {
        "fragment_larger_buffer_view_ro_storage",
        R"wgsl(
@group(0) @binding(0) var<storage> buf : buffer;
@group(0) @binding(1) var<uniform> uni : u32;

@vertex
fn vs() -> @builtin(position) vec4f {
  _ = uni;
  _ = bufferView<array<u32, 2>>(&buf, 0);
  return vec4f(0);
}

@fragment
fn fs() -> @location(0) vec4f {
  _ = uni;
  _ = bufferView<array<u32, 4>>(&buf, 0);
  return vec4f(0);
}
)wgsl",
        16,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "vertex_larger_buffer_view_ro_storage",
        R"wgsl(
@group(0) @binding(0) var<storage> buf : buffer;
@group(0) @binding(1) var<uniform> uni : u32;

@vertex
fn vs() -> @builtin(position) vec4f {
  _ = uni;
  _ = bufferView<array<vec4f, 2>>(&buf, 0);
  return vec4f(0);
}

@fragment
fn fs() -> @location(0) vec4f {
  _ = uni;
  _ = bufferView<array<u32, 4>>(&buf, 0);
  return vec4f(0);
}
)wgsl",
        32,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "fragment_larger_buffer_array_view_ro_storage",
        R"wgsl(
@group(0) @binding(0) var<storage> buf : buffer;
@group(0) @binding(1) var<uniform> uni : u32;

@vertex
fn vs() -> @builtin(position) vec4f {
  _ = bufferArrayView<array<u32>>(&buf, 0, uni);
  return vec4f(0);
}

@fragment
fn fs() -> @location(0) vec4f {
  _ = bufferArrayView<array<vec4u>>(&buf, 0, uni);
  return vec4f(0);
}
)wgsl",
        16,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
    {
        "vertex_larger_buffer_array_view_ro_storage",
        R"wgsl(
@group(0) @binding(0) var<storage> buf : buffer;
@group(0) @binding(1) var<uniform> uni : u32;

@vertex
fn vs() -> @builtin(position) vec4f {
  _ = bufferArrayView<array<vec4f>>(&buf, 0, uni);
  return vec4f(0);
}

@fragment
fn fs() -> @location(0) vec4f {
  _ = bufferArrayView<array<u32>>(&buf, 0, uni);
  return vec4f(0);
}
)wgsl",
        16,
        WGPUBufferBindingType_ReadOnlyStorage,
        true,
        false,
    },
};

std::vector<Value> caseNames(const std::vector<RequiredSizeCase>& cases) {
    std::vector<Value> values;
    values.reserve(cases.size());
    for (const RequiredSizeCase& testcase : cases) {
        values.emplace_back(std::string(testcase.name));
    }
    return values;
}

const RequiredSizeCase& findCase(const std::vector<RequiredSizeCase>& cases, const std::string& name) {
    for (const RequiredSizeCase& testcase : cases) {
        if (name == testcase.name) {
            return testcase;
        }
    }
    std::abort();
}

void skipIfLanguageFeaturesNotSupported(AllFeaturesMaxLimitsGpuTest& t, const RequiredSizeCase& testcase) {
    if (testcase.requiresBufferView && !hasBufferViewLanguageFeature()) {
        t.skip("Missing buffer_view language feature");
    }
    if (testcase.requiresUnrestrictedPointerParameters
        && !hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters)) {
        t.skip("Missing unrestricted_pointer_parameters language feature");
    }
}

WGPUBuffer createTestBuffer(AllFeaturesMaxLimitsGpuTest& t, const RequiredSizeCase& testcase, bool valid) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = valid ? testcase.size : testcase.size - 4;
    desc.usage = testcase.bindingType == WGPUBufferBindingType_Uniform ? WGPUBufferUsage_Uniform
                                                                       : WGPUBufferUsage_Storage;
    return t.createBufferTracked(desc);
}

CTS_TEST(g, "compute,shader_required_buffer_size")
    .desc("Test that dispatch time validation occurs about the required buffer size")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kRequiredSizeCases))
            .beginSubcases()
            .combine("valid", {false, true})
            .combine("layout", {Value("auto"), Value("explicit")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const RequiredSizeCase& testcase = findCase(kRequiredSizeCases, t.param<std::string>("case"));
        const bool valid = t.param<bool>("valid");
        const bool autoLayout = t.param<std::string>("layout") == "auto";
        skipIfLanguageFeaturesNotSupported(t, testcase);

        WGPUBuffer buffer = createTestBuffer(t, testcase, valid);

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
        if (pipeline == nullptr) {
            t.fail("createComputePipeline returned null");
        }

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
            t.expectValidationError([&] { t.createBindGroupTracked(bgDesc); }, true);
        } else {
            // Expect dispatch time validation.
            WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);

            WGPUCommandEncoder commandEncoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder computePassEncoder = t.beginComputePassTracked(commandEncoder, nullptr);
            wgpuComputePassEncoderSetPipeline(computePassEncoder, pipeline);
            wgpuComputePassEncoderSetBindGroup(computePassEncoder, 0, bg, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(computePassEncoder, 1, 1, 1);
            wgpuComputePassEncoderEnd(computePassEncoder);

            t.expectValidationError([&] { t.finishTracked(commandEncoder); }, !valid);
        }
        wgpuBindGroupLayoutRelease(pipelineBindGroupLayout);
    });

CTS_TEST(g, "render,shader_required_buffer_size")
    .params([](ParamsBuilder u) {
        return u.combine("case", caseNames(kRequiredRenderSizeCases))
            .beginSubcases()
            .combine("valid", {false, true})
            .combine("layout", {Value("auto"), Value("explicit")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const RequiredSizeCase& testcase = findCase(kRequiredRenderSizeCases, t.param<std::string>("case"));
        const bool valid = t.param<bool>("valid");
        const bool autoLayout = t.param<std::string>("layout") == "auto";
        skipIfLanguageFeaturesNotSupported(t, testcase);

        WGPUBuffer buffer = createTestBuffer(t, testcase, valid);

        WGPUBufferDescriptor uniformDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        uniformDesc.size = 4;
        uniformDesc.usage = WGPUBufferUsage_Uniform;
        WGPUBuffer uniform = t.createBufferTracked(uniformDesc);

        WGPUBindGroupLayoutEntry bglEntries[2] = {WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT,
                                                  WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT};
        bglEntries[0].binding = 0;
        bglEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bglEntries[0].buffer.type = testcase.bindingType;
        bglEntries[0].buffer.minBindingSize = 0;
        bglEntries[1].binding = 1;
        bglEntries[1].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        bglEntries[1].buffer.type = WGPUBufferBindingType_Uniform;
        bglEntries[1].buffer.minBindingSize = 4;
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 2;
        bglDesc.entries = bglEntries;
        WGPUBindGroupLayout bgLayout = t.createBindGroupLayoutTracked(bglDesc);
        WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.bindGroupLayoutCount = 1;
        layoutDesc.bindGroupLayouts = &bgLayout;
        WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

        WGPUTextureDescriptor colorDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        colorDesc.size = {1, 1, 1};
        colorDesc.format = WGPUTextureFormat_RGBA8Unorm;
        colorDesc.usage = WGPUTextureUsage_RenderAttachment;
        WGPUTexture colorTexture = t.createTextureTracked(colorDesc);

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(testcase.code);
        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = shaderModule;
        fragment.targetCount = 1;
        fragment.targets = &colorTarget;
        WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = autoLayout ? nullptr : layout;
        pipelineDesc.vertex.module = shaderModule;
        pipelineDesc.fragment = &fragment;
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipelineDesc);
        if (pipeline == nullptr) {
            t.fail("createRenderPipeline returned null");
        }

        WGPUBindGroupLayout pipelineBindGroupLayout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        entries[0].binding = 0;
        entries[0].buffer = buffer;
        entries[0].size = WGPU_WHOLE_SIZE;
        entries[1].binding = 1;
        entries[1].buffer = uniform;
        entries[1].size = WGPU_WHOLE_SIZE;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = pipelineBindGroupLayout;
        bgDesc.entryCount = 2;
        bgDesc.entries = entries;

        if (autoLayout && !valid) {
            // 'auto' layout get minBindingSize from shaders
            t.expectValidationError([&] { t.createBindGroupTracked(bgDesc); }, true);
        } else {
            WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachment.view = t.createViewTracked(colorTexture, viewDesc);
            colorAttachment.clearValue = WGPUColor{0, 0, 0, 0};
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
            wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);

            t.expectValidationError([&] { t.finishTracked(encoder); }, !valid);
        }
        wgpuBindGroupLayoutRelease(pipelineBindGroupLayout);
    });

}  // namespace
