// Ported from gpuweb/cts src/webgpu/api/operation/labels.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//
// 1. The WebGPU C API (webgpu.h) has wgpu*SetLabel() for every object type but
//    NO wgpu*GetLabel() getter.  The upstream JS tests read back object.label
//    to verify round-tripping.  The C port verifies:
//      (a) Objects can be created successfully with a label in the descriptor.
//      (b) wgpu*SetLabel() can be called without crash (post-creation label set).
//    The label-equality assertions (e.g. "buffer.label === label") have no
//    direct C analog and are replaced by object-existence / non-crash checks.
//
// 2. The 'requestDevice' case in upstream kTestFunctions calls getGPU() /
//    navigator.gpu — a web-platform API with no C equivalent.  That case is
//    skipped at runtime (t.skip) rather than marking the whole test
//    unimplemented, so the other 18 cases still run.
//
// 3. createComputePipelineAsync / createRenderPipelineAsync — JS async
//    pipeline creation.  The C API has synchronous createComputePipeline /
//    createRenderPipeline which serve the same purpose for a label test.
//
// 4. createQuerySet / createView / beginRenderPass / beginComputePass /
//    finish / createRenderBundleEncoder — not in GpuTest tracked helpers;
//    created via raw wgpu* calls and released manually.
//
// 5. wrappers_do_not_share_labels: the upstream test asserts JS wrapper-object
//    identity (layout1 !== layout2) and per-wrapper label storage.  The C API
//    has no wrapper-object layer and no wgpuBindGroupLayoutGetLabel getter, so
//    this test has no faithful C-API analog and is marked .unimplemented().

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// Helper: build a WGPUStringView from a std::string whose storage is owned
// by the caller and outlives the consuming wgpu*Create* call.
static WGPUStringView sv(const std::string& s) {
    return WGPUStringView{s.data(), s.size()};
}

static WGPUStringView sv(const char* s) {
    return WGPUStringView{s, std::string_view(s).size()};
}

// The wgpu*SetLabel() family is not implemented by every backend (yawgpu does
// not export these symbols). The upstream label-equality checks have no C
// analog anyway (there is no wgpu*GetLabel), so these post-creation SetLabel
// calls are pure "does not crash" smoke checks. On backends that lack the
// symbols, compile them out so the create-with-label coverage still links/runs.
#if defined(CTS_BACKEND_YAWGPU)
template <typename T>
static void setLabelIfSupported(T /*obj*/, WGPUStringView /*label*/) {}
#else
static void setLabelIfSupported(WGPUBuffer o, WGPUStringView l) { wgpuBufferSetLabel(o, l); }
static void setLabelIfSupported(WGPUTexture o, WGPUStringView l) { wgpuTextureSetLabel(o, l); }
static void setLabelIfSupported(WGPUTextureView o, WGPUStringView l) { wgpuTextureViewSetLabel(o, l); }
static void setLabelIfSupported(WGPUSampler o, WGPUStringView l) { wgpuSamplerSetLabel(o, l); }
static void setLabelIfSupported(WGPUBindGroupLayout o, WGPUStringView l) { wgpuBindGroupLayoutSetLabel(o, l); }
static void setLabelIfSupported(WGPUPipelineLayout o, WGPUStringView l) { wgpuPipelineLayoutSetLabel(o, l); }
static void setLabelIfSupported(WGPUBindGroup o, WGPUStringView l) { wgpuBindGroupSetLabel(o, l); }
static void setLabelIfSupported(WGPUShaderModule o, WGPUStringView l) { wgpuShaderModuleSetLabel(o, l); }
static void setLabelIfSupported(WGPUComputePipeline o, WGPUStringView l) { wgpuComputePipelineSetLabel(o, l); }
static void setLabelIfSupported(WGPURenderPipeline o, WGPUStringView l) { wgpuRenderPipelineSetLabel(o, l); }
static void setLabelIfSupported(WGPUCommandEncoder o, WGPUStringView l) { wgpuCommandEncoderSetLabel(o, l); }
static void setLabelIfSupported(WGPURenderBundleEncoder o, WGPUStringView l) { wgpuRenderBundleEncoderSetLabel(o, l); }
static void setLabelIfSupported(WGPUQuerySet o, WGPUStringView l) { wgpuQuerySetSetLabel(o, l); }
static void setLabelIfSupported(WGPURenderPassEncoder o, WGPUStringView l) { wgpuRenderPassEncoderSetLabel(o, l); }
static void setLabelIfSupported(WGPUComputePassEncoder o, WGPUStringView l) { wgpuComputePassEncoderSetLabel(o, l); }
static void setLabelIfSupported(WGPUCommandBuffer o, WGPUStringView l) { wgpuCommandBufferSetLabel(o, l); }
#endif

// ---------------------------------------------------------------------------
// Group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,labels",
    R"(
Tests for object labels.
)");

// ---------------------------------------------------------------------------
// Shared WGSL snippets (stored as static const std::string so sv() doesn't
// dangle — the string outlives all create calls).
// ---------------------------------------------------------------------------

static const std::string kVertexWgsl =
    "@vertex fn vs() -> @builtin(position) vec4f {\n"
    " return vec4f(0, 0, 0, 1);\n"
    "}\n";

static const std::string kComputeWgsl =
    "@compute @workgroup_size(1u) fn foo() {}\n";

static const std::string kVertexNamedFooWgsl =
    "@group(0) @binding(0) var<uniform> pos: vec4f;\n"
    "@vertex fn main() -> @builtin(position) vec4f {\n"
    "  return pos;\n"
    "}\n";

static const std::string kFragmentWgsl =
    "@fragment fn main() -> @location(0) vec4f { return vec4f(0); }\n";

static const std::string kRenderVertexWgsl =
    "@vertex fn foo() -> @builtin(position) vec4f {\n"
    " return vec4f(0, 0, 0, 1);\n"
    "}\n";

// ---------------------------------------------------------------------------
// Per-name test body helpers
// ---------------------------------------------------------------------------

// createBuffer: create with label, verify non-null; destroy; set label again.
static void testCreateBuffer(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = 16;
    desc.usage = WGPUBufferUsage_CopyDst;
    desc.label = sv(label);
    WGPUBuffer buffer = t.createBufferTracked(desc);
    // Upstream: t.expect(buffer.label === label) — no GetLabel in C API.
    t.expect(buffer != nullptr, "createBuffer with label returned null");
    wgpuBufferDestroy(buffer);
    // Upstream: t.expect(buffer.label === label) after destroy — no GetLabel.
    // Verify SetLabel does not crash after destroy.
    setLabelIfSupported(buffer, sv(label));
}

// requestDevice: uses navigator.gpu — web-platform, skip with reason.
static void testRequestDevice(AllFeaturesMaxLimitsGpuTest& t, const std::string& /*label*/) {
    t.skip("requestDevice case uses navigator.gpu / getGPU() which has no C "
           "API equivalent; skipped (no WGPUInstance-level label API in webgpu.h)");
}

// createTexture: create with label, destroy, verify SetLabel does not crash.
static void testCreateTexture(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.size = WGPUExtent3D{1, 1, 1};
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(desc);
    t.expect(texture != nullptr, "createTexture with label returned null");
    wgpuTextureDestroy(texture);
    setLabelIfSupported(texture, sv(label));
}

// createSampler: create with label, verify non-null.
static void testCreateSampler(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUSamplerDescriptor desc = WGPU_SAMPLER_DESCRIPTOR_INIT;
    desc.label = sv(label);
    WGPUSampler sampler = t.createSamplerTracked(desc);
    t.expect(sampler != nullptr, "createSampler with label returned null");
    setLabelIfSupported(sampler, sv(label));
}

// createBindGroupLayout: create with label, verify non-null.
static void testCreateBindGroupLayout(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.entryCount = 0;
    desc.entries = nullptr;
    WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(desc);
    t.expect(bgl != nullptr, "createBindGroupLayout with label returned null");
    setLabelIfSupported(bgl, sv(label));
}

// createPipelineLayout: create with label, verify non-null.
static void testCreatePipelineLayout(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.bindGroupLayoutCount = 0;
    desc.bindGroupLayouts = nullptr;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(desc);
    t.expect(layout != nullptr, "createPipelineLayout with label returned null");
    setLabelIfSupported(layout, sv(label));
}

// createBindGroup: create layout + bindGroup with label, verify non-null.
static void testCreateBindGroup(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bglDesc.entryCount = 0;
    bglDesc.entries = nullptr;
    WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.label = sv(label);
    bgDesc.layout = bgl;
    bgDesc.entryCount = 0;
    bgDesc.entries = nullptr;
    WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
    t.expect(bg != nullptr, "createBindGroup with label returned null");
    setLabelIfSupported(bg, sv(label));
}

// createShaderModule: create with label, verify non-null.
static void testCreateShaderModule(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUShaderSourceWGSL wgslSource = WGPU_SHADER_SOURCE_WGSL_INIT;
    wgslSource.code = sv(kVertexWgsl);
    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.nextInChain = &wgslSource.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(t.device(), &desc);
    t.expect(shaderModule != nullptr, "createShaderModule with label returned null");
    setLabelIfSupported(shaderModule, sv(label));
    wgpuShaderModuleRelease(shaderModule);
}

// createComputePipeline: create with label, verify non-null.
static void testCreateComputePipeline(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUShaderModule computeModule = t.createShaderModuleTracked(kComputeWgsl);

    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.layout = nullptr; // 'auto' layout
    desc.compute.module = computeModule;
    desc.compute.entryPoint = sv("foo");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(desc);
    t.expect(pipeline != nullptr, "createComputePipeline with label returned null");
    setLabelIfSupported(pipeline, sv(label));
}

// createRenderPipeline: create with label, verify non-null.
static void testCreateRenderPipeline(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kRenderVertexWgsl);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentWgsl);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragState = WGPU_FRAGMENT_STATE_INIT;
    fragState.module = fragModule;
    fragState.entryPoint = sv("main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.layout = nullptr; // 'auto' layout
    desc.vertex.module = vertModule;
    desc.vertex.entryPoint = sv("foo");
    desc.fragment = &fragState;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(desc);
    t.expect(pipeline != nullptr, "createRenderPipeline with label returned null");
    setLabelIfSupported(pipeline, sv(label));
}

// createComputePipelineAsync: JS async — use sync createComputePipeline instead.
static void testCreateComputePipelineAsync(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& label) {
    // Upstream is async; C port uses synchronous createComputePipeline.
    WGPUShaderModule computeModule = t.createShaderModuleTracked(kComputeWgsl);

    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.layout = nullptr; // 'auto' layout
    desc.compute.module = computeModule;
    desc.compute.entryPoint = sv("foo");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(desc);
    t.expect(pipeline != nullptr, "createComputePipelineAsync (sync) with label returned null");
    setLabelIfSupported(pipeline, sv(label));
}

// createRenderPipelineAsync: JS async — use sync createRenderPipeline instead.
static void testCreateRenderPipelineAsync(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& label) {
    // Upstream is async; C port uses synchronous createRenderPipeline.
    WGPUShaderModule vertModule = t.createShaderModuleTracked(kRenderVertexWgsl);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentWgsl);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragState = WGPU_FRAGMENT_STATE_INIT;
    fragState.module = fragModule;
    fragState.entryPoint = sv("main");
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.layout = nullptr; // 'auto' layout
    desc.vertex.module = vertModule;
    desc.vertex.entryPoint = sv("foo");
    desc.fragment = &fragState;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(desc);
    t.expect(pipeline != nullptr, "createRenderPipelineAsync (sync) with label returned null");
    setLabelIfSupported(pipeline, sv(label));
}

// createCommandEncoder: create with label, verify non-null.
static void testCreateCommandEncoder(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUCommandEncoderDescriptor desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    desc.label = sv(label);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &desc);
    t.expect(encoder != nullptr, "createCommandEncoder with label returned null");
    setLabelIfSupported(encoder, sv(label));
    // Drop the encoder (not submitted — just testing label; finish would
    // require a submission which is out of scope here).
    wgpuCommandEncoderRelease(encoder);
}

// createRenderBundleEncoder: create with label, verify non-null.
static void testCreateRenderBundleEncoder(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& label) {
    const WGPUTextureFormat colorFormat = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.colorFormatCount = 1;
    desc.colorFormats = &colorFormat;
    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
    t.expect(encoder != nullptr, "createRenderBundleEncoder with label returned null");
    setLabelIfSupported(encoder, sv(label));
    wgpuRenderBundleEncoderRelease(encoder);
}

// createQuerySet: create with label, verify non-null, destroy, set label.
static void testCreateQuerySet(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.label = sv(label);
    desc.type = WGPUQueryType_Occlusion;
    desc.count = 1;
    WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &desc);
    t.expect(querySet != nullptr, "createQuerySet with label returned null");
    wgpuQuerySetDestroy(querySet);
    // Upstream: t.expect(querySet.label === label) after destroy — no GetLabel.
    setLabelIfSupported(querySet, sv(label));
    wgpuQuerySetRelease(querySet);
}

// beginRenderPass: create texture + encoder with labels, begin pass with label.
// Also verifies that the encoder label survives the pass (SetLabel round-trip).
static void testBeginRenderPass(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.label = sv(label);
    texDesc.size = WGPUExtent3D{1, 1, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(texDesc);

    const std::string label2 = label + "-2";

    WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encDesc.label = sv(label2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = wgpuTextureCreateView(texture, &viewDesc);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.label = sv(label);
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    t.expect(pass != nullptr, "beginRenderPass with label returned null");
    // Upstream: t.expect(renderPass.label === label) — no GetLabel in C API.
    wgpuRenderPassEncoderEnd(pass);
    // Upstream: t.expect(renderPass.label === label) after end() — no GetLabel.
    // Upstream: t.expect(encoder.label === label2) — no GetLabel in C API.
    // Verify SetLabel on encoder and pass do not crash.
    setLabelIfSupported(pass, sv(label));
    setLabelIfSupported(encoder, sv(label2));
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);
    wgpuTextureDestroy(texture);
}

// beginComputePass: create encoder with labels, begin pass with label.
static void testBeginComputePass(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    const std::string label2 = label + "-2";

    WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    encDesc.label = sv(label2);
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);

    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    passDesc.label = sv(label);
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    t.expect(pass != nullptr, "beginComputePass with label returned null");
    // Upstream: t.expect(computePass.label === label) — no GetLabel in C API.
    wgpuComputePassEncoderEnd(pass);
    // Upstream: t.expect(computePass.label === label) after end() — no GetLabel.
    // Upstream: t.expect(encoder.label === label2) — no GetLabel in C API.
    setLabelIfSupported(pass, sv(label));
    setLabelIfSupported(encoder, sv(label2));
    wgpuCommandEncoderRelease(encoder);
}

// finish: create encoder, finish command buffer with label.
static void testFinish(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), nullptr);
    WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    cbDesc.label = sv(label);
    WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
    t.expect(cb != nullptr, "encoder.finish with label returned null");
    // Upstream: t.expect(commandBuffer.label === label) — no GetLabel in C API.
    setLabelIfSupported(cb, sv(label));
    wgpuCommandEncoderRelease(encoder);
    wgpuCommandBufferRelease(cb);
}

// createView: create texture, create view with label.
static void testCreateView(AllFeaturesMaxLimitsGpuTest& t, const std::string& label) {
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{1, 1, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    viewDesc.label = sv(label);
    WGPUTextureView view = wgpuTextureCreateView(texture, &viewDesc);
    t.expect(view != nullptr, "createView with label returned null");
    // Upstream: t.expect(view.label === label) — no GetLabel in C API.
    setLabelIfSupported(view, sv(label));
    wgpuTextureDestroy(texture);
    // Upstream: t.expect(view.label === label) after texture.destroy() — no GetLabel.
    setLabelIfSupported(view, sv(label));
    wgpuTextureViewRelease(view);
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Upstream kTestFunctions keys in order.
static const std::array<const char*, 19> kTestFunctionNames = {{
    "createBuffer",
    "requestDevice",
    "createTexture",
    "createSampler",
    "createBindGroupLayout",
    "createPipelineLayout",
    "createBindGroup",
    "createShaderModule",
    "createComputePipeline",
    "createRenderPipeline",
    "createComputePipelineAsync",
    "createRenderPipelineAsync",
    "createCommandEncoder",
    "createRenderBundleEncoder",
    "createQuerySet",
    "beginRenderPass",
    "beginComputePass",
    "finish",
    "createView",
}};

static std::vector<Value> kTestFunctionNameValues() {
    std::vector<Value> names;
    names.reserve(kTestFunctionNames.size());
    for (const char* n : kTestFunctionNames) {
        names.emplace_back(std::string(n));
    }
    return names;
}

CTS_TEST(g, "object_has_descriptor_label")
    .desc(
        "For every create function, the descriptor.label is carried over to the object.label.\n"
        "\n"
        "TODO: test importExternalTexture\n"
        "TODO: make a best effort and generating an error that is likely to use label. "
        "There's nothing to check for\n"
        "      but it may surface bugs related to unusual labels.\n"
        "\n"
        "Note (C port): The WebGPU C API has no GetLabel function. This test verifies\n"
        "that objects can be created with labels in descriptors and that SetLabel can be\n"
        "called without crashing. The requestDevice case is skipped (uses navigator.gpu).\n"
        "createComputePipelineAsync/createRenderPipelineAsync use the synchronous variants.")
    .params([](ParamsBuilder u) {
        return u.combine("name", kTestFunctionNameValues())
            .beginSubcases()
            .combine("label", {
                std::string("label"),
                std::string("\0", 1),
                std::string("null\0in\0label", 13),
                std::string("\xf0\x9f\x8c\x9e\xf0\x9f\x91\x86", 8)  // UTF-8: 🌞👆
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string name = t.param<std::string>("name");
        const std::string label = t.param<std::string>("label");

        if (name == "createBuffer") {
            testCreateBuffer(t, label);
        } else if (name == "requestDevice") {
            testRequestDevice(t, label);
        } else if (name == "createTexture") {
            testCreateTexture(t, label);
        } else if (name == "createSampler") {
            testCreateSampler(t, label);
        } else if (name == "createBindGroupLayout") {
            testCreateBindGroupLayout(t, label);
        } else if (name == "createPipelineLayout") {
            testCreatePipelineLayout(t, label);
        } else if (name == "createBindGroup") {
            testCreateBindGroup(t, label);
        } else if (name == "createShaderModule") {
            testCreateShaderModule(t, label);
        } else if (name == "createComputePipeline") {
            testCreateComputePipeline(t, label);
        } else if (name == "createRenderPipeline") {
            testCreateRenderPipeline(t, label);
        } else if (name == "createComputePipelineAsync") {
            testCreateComputePipelineAsync(t, label);
        } else if (name == "createRenderPipelineAsync") {
            testCreateRenderPipelineAsync(t, label);
        } else if (name == "createCommandEncoder") {
            testCreateCommandEncoder(t, label);
        } else if (name == "createRenderBundleEncoder") {
            testCreateRenderBundleEncoder(t, label);
        } else if (name == "createQuerySet") {
            testCreateQuerySet(t, label);
        } else if (name == "beginRenderPass") {
            testBeginRenderPass(t, label);
        } else if (name == "beginComputePass") {
            testBeginComputePass(t, label);
        } else if (name == "finish") {
            testFinish(t, label);
        } else if (name == "createView") {
            testCreateView(t, label);
        } else {
            t.fail("unknown test function name: " + name);
        }
    });

CTS_TEST(g, "wrappers_do_not_share_labels")
    .desc(
        "test that different wrapper objects for the same GPU object do not share labels\n"
        "\n"
        "Note (C port): the upstream test asserts JS wrapper-object identity\n"
        "(layout1 !== layout2 as distinct JS objects) and per-wrapper label storage\n"
        "(layout1.label === 'foo' while layout2.label === 'bar'). The WebGPU C API\n"
        "has no wrapper-object layer — wgpuRenderPipelineGetBindGroupLayout returns a\n"
        "ref-counted handle that may be the same or a new pointer; there is no per-wrapper\n"
        "label storage and no wgpuBindGroupLayoutGetLabel getter. This test has no\n"
        "faithful C-API analog and is marked unimplemented.")
    // JS wrapper-object identity and per-wrapper label storage have no native C-API
    // analog: the C API has no wrapper-object layer and no GetLabel getter, so the
    // two getBindGroupLayout(0) calls cannot be tested for independent label storage.
    .unimplemented(
        "JS wrapper-object identity / per-wrapper labels have no native C-API analog: "
        "the WebGPU C API has no wrapper-object layer and no wgpuBindGroupLayoutGetLabel "
        "to verify independent per-handle label storage.");

} // namespace
