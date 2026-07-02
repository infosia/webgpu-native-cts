// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/programmable/immediate.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 The webgpu-native-cts Authors, BSD-3-Clause.
//
// Operation tests for immediate data usage in RenderPassEncoder, ComputePassEncoder, and
// RenderBundleEncoder.
//
// PORTING NOTES:
//  - Upstream gates the whole group in init() via supportsImmediateData(gpu). We mirror that
//    per-test via skipImmediateUnsupported() (maxImmediateSize == 0 or undefined), since the
//    harness has no per-fixture async init hook here. Now that wgpuComputePassEncoderSetImmediates
//    / wgpuRenderPassEncoderSetImmediates / wgpuRenderBundleEncoderSetImmediates are exported
//    (both yawgpu and Dawn report maxImmediateSize=64), this test file actually executes rather
//    than always skipping.
//  - `encoder.setImmediates(rangeOffset, data, dataOffset, size)` (JS) maps to
//    `wgpuXxxSetImmediates(encoder, uint32_t offset, void const* data, size_t size)` (C): the
//    dataOffset/size (element-count) pair becomes a `data.data() + dataOffset*elementSize`
//    pointer plus a `size*elementSize`-byte length.
//  - The render path renders into an `rgba32uint` 1-row texture (kBytesPerPixel = 16) and reads
//    values back via a pixel readback, matching upstream's approach of returning results through
//    the fragment shader's integer color output rather than a storage buffer.

#include <cstdint>
#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,programmable,immediate",
    "Operation tests for immediate data usage in RenderPassEncoder, ComputePassEncoder, and "
    "RenderBundleEncoder.");

// kProgrammableEncoderTypes = ['compute pass', 'render pass', 'render bundle']
// (compute pass + kRenderEncodeTypes). See webgpu/util/command_buffer_maker.ts.
const std::vector<Value> kProgrammableEncoderTypes = {
    "compute pass", "render pass", "render bundle"};

// HostSharableTypes = ['i32', 'u32', 'f16', 'f32']. See webgpu/shader/types.ts.
// kVectorContainerTypes = ['vec2', 'vec3', 'vec4'].
//
// kTypedArrayBufferViewKeys (constructor names, in upstream order). See common/util/util.ts.
const std::vector<Value> kTypedArrayBufferViewKeys = {
    "Uint8Array",  "Uint8ClampedArray", "Uint16Array",   "Uint32Array",
    "Int8Array",   "Int16Array",        "Int32Array",    "Float16Array",
    "Float32Array", "Float64Array",     "BigInt64Array", "BigUint64Array"};

constexpr WGPUTextureFormat kRenderTargetFormat = WGPUTextureFormat_RGBA32Uint;
constexpr uint32_t kBytesPerPixel  = 16; // rgba32uint = 4 x u32 = 16 bytes
constexpr uint32_t kMinBytesPerRow = 256; // WebGPU requires bytesPerRow to be a multiple of 256

// ---------------------------------------------------------------------------
// Runtime gate shared by every test.
//
// Upstream gates the whole group in init() via supportsImmediateData(gpu). In native we mirror
// that with the maxImmediateSize limit.
// ---------------------------------------------------------------------------
void skipImmediateUnsupported(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPULimits limits = t.getLimits();
    const uint32_t maxImmediateSize = limits.maxImmediateSize;
    if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
        t.skip("Immediate data not supported (maxImmediateSize == 0)");
    }
}

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

uint32_t align(uint32_t value, uint32_t alignment) {
    return ((value + alignment - 1) / alignment) * alignment;
}

// BYTES_PER_ELEMENT per typed array (upstream key order).
int bytesPerElementForTypedArray(const std::string& key) {
    if (key == "Uint8Array" || key == "Uint8ClampedArray" || key == "Int8Array") {
        return 1;
    }
    if (key == "Uint16Array" || key == "Int16Array" || key == "Float16Array") {
        return 2;
    }
    if (key == "Uint32Array" || key == "Int32Array" || key == "Float32Array") {
        return 4;
    }
    // Float64Array, BigInt64Array, BigUint64Array.
    return 8;
}

// ---------------------------------------------------------------------------
// Pass: a compute-pass / render-pass / render-bundle-encoder handle, generalizing the JS
// union type `GPUComputePassEncoder | GPURenderPassEncoder | GPURenderBundleEncoder`.
// ---------------------------------------------------------------------------
struct Pass {
    std::string encoderType;
    WGPUComputePassEncoder  computePass = nullptr;
    WGPURenderPassEncoder   renderPass  = nullptr;
    WGPURenderBundleEncoder bundleEnc   = nullptr;
};

void passSetPipeline(Pass& p, WGPUComputePipeline computePipeline, WGPURenderPipeline renderPipeline) {
    if (p.encoderType == "compute pass") {
        wgpuComputePassEncoderSetPipeline(p.computePass, computePipeline);
    } else if (p.encoderType == "render pass") {
        wgpuRenderPassEncoderSetPipeline(p.renderPass, renderPipeline);
    } else {
        wgpuRenderBundleEncoderSetPipeline(p.bundleEnc, renderPipeline);
    }
}

void passSetBindGroupDynamic(Pass& p, uint32_t index, WGPUBindGroup bg, uint32_t dynamicOffset) {
    if (p.encoderType == "compute pass") {
        wgpuComputePassEncoderSetBindGroup(p.computePass, index, bg, 1, &dynamicOffset);
    } else if (p.encoderType == "render pass") {
        wgpuRenderPassEncoderSetBindGroup(p.renderPass, index, bg, 1, &dynamicOffset);
    } else {
        wgpuRenderBundleEncoderSetBindGroup(p.bundleEnc, index, bg, 1, &dynamicOffset);
    }
}

void passSetImmediates(Pass& p, uint32_t offset, const void* data, size_t size) {
    if (p.encoderType == "compute pass") {
        wgpuComputePassEncoderSetImmediates(p.computePass, offset, data, size);
    } else if (p.encoderType == "render pass") {
        wgpuRenderPassEncoderSetImmediates(p.renderPass, offset, data, size);
    } else {
        wgpuRenderBundleEncoderSetImmediates(p.bundleEnc, offset, data, size);
    }
}

void passDispatchOrDraw(Pass& p) {
    if (p.encoderType == "compute pass") {
        wgpuComputePassEncoderDispatchWorkgroups(p.computePass, 1, 1, 1);
    } else if (p.encoderType == "render pass") {
        wgpuRenderPassEncoderDraw(p.renderPass, 1, 1, 0, 0); // 1 vertex over 1 instance
    } else {
        wgpuRenderBundleEncoderDraw(p.bundleEnc, 1, 1, 0, 0);
    }
}

// ---------------------------------------------------------------------------
// PipelineHandle: either a compute or a render pipeline, plus the bind group layout used to
// build compatible bind groups (mirrors pipeline.getBindGroupLayout(0) usage upstream, but we
// keep the WGPUBindGroupLayout we built directly since we always construct explicit layouts).
// ---------------------------------------------------------------------------
struct PipelineHandle {
    bool                 isCompute = false;
    WGPUComputePipeline   compute   = nullptr;
    WGPURenderPipeline    render    = nullptr;
    WGPUBindGroupLayout   bgl       = nullptr;
};

// Creates a pipeline for testing immediate data.
//
// For compute pipelines: uses a storage buffer to write results.
// For render pipelines: returns results via the fragment shader's rgba32uint color output,
//   avoiding the need for storage buffers in the fragment stage.
PipelineHandle createPipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType,
    const std::string& wgslDecl,
    const std::string& copyCode,
    const std::string& fragmentReturnExpr,
    uint32_t immediateSize,
    uint32_t renderTargetWidth = 4,
    WGPUPipelineLayout layoutOverride = nullptr,
    WGPUBindGroupLayout bglOverride = nullptr)
{
    PipelineHandle result;
    result.isCompute = (encoderType == "compute pass");

    WGPUBindGroupLayout bgl    = bglOverride;
    WGPUPipelineLayout  layout = layoutOverride;

    if (layout == nullptr) {
        if (bgl == nullptr) {
            std::vector<WGPUBindGroupLayoutEntry> entries;
            if (result.isCompute) {
                WGPUBindGroupLayoutEntry e0 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                e0.binding    = 0;
                e0.visibility = WGPUShaderStage_Compute;
                e0.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
                e0.buffer.type = WGPUBufferBindingType_Storage;
                entries.push_back(e0);

                WGPUBindGroupLayoutEntry e1 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                e1.binding    = 1;
                e1.visibility = WGPUShaderStage_Compute;
                e1.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
                e1.buffer.type             = WGPUBufferBindingType_Uniform;
                e1.buffer.hasDynamicOffset = WGPU_TRUE;
                entries.push_back(e1);
            } else {
                WGPUBindGroupLayoutEntry e0 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
                e0.binding    = 0;
                e0.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
                e0.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
                e0.buffer.type             = WGPUBufferBindingType_Uniform;
                e0.buffer.hasDynamicOffset = WGPU_TRUE;
                entries.push_back(e0);
            }
            WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            bglDesc.entryCount = static_cast<uint32_t>(entries.size());
            bglDesc.entries    = entries.data();
            bgl = t.createBindGroupLayoutTracked(bglDesc);
        }

        WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        plDesc.bindGroupLayoutCount = 1;
        plDesc.bindGroupLayouts     = &bgl;
        plDesc.immediateSize        = immediateSize;
        layout = t.createPipelineLayoutTracked(plDesc);
    }
    result.bgl = bgl;

    if (result.isCompute) {
        const std::string code =
            wgslDecl + "\n"
            "@group(0) @binding(0) var<storage, read_write> output: array<u32>;\n"
            "@group(0) @binding(1) var<uniform> outIndex: u32;\n"
            "\n"
            "@compute @workgroup_size(1) fn cs_main() {\n" + copyCode + "\n}\n";

        WGPUShaderModule computeModule = t.createShaderModuleTracked(code);
        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout             = layout;
        desc.compute.module     = computeModule;
        desc.compute.entryPoint = sv("cs_main");
        result.compute = t.createComputePipelineTracked(desc);
    } else {
        std::ostringstream vs;
        vs << "@group(0) @binding(0) var<uniform> outIndex: u32;\n"
           << "\n"
           << "@vertex fn vs_main() -> @builtin(position) vec4f {\n"
           << "  let x = (f32(outIndex) + 0.5) / f32(" << renderTargetWidth << ") * 2.0 - 1.0;\n"
           << "  return vec4f(x, 0.0, 0.0, 1.0);\n"
           << "}\n";

        const std::string fragmentCode =
            wgslDecl + "\n"
            "@group(0) @binding(0) var<uniform> outIndex: u32;\n"
            "\n"
            "@fragment fn fs_main() -> @location(0) vec4u {\n"
            "  return " + fragmentReturnExpr + ";\n}\n";

        WGPUShaderModule vertexModule   = t.createShaderModuleTracked(vs.str());
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(fragmentCode);

        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = kRenderTargetFormat;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = fragmentModule;
        fragment.entryPoint  = sv("fs_main");
        fragment.targetCount = 1;
        fragment.targets     = &target;

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout                = layout;
        desc.vertex.module         = vertexModule;
        desc.vertex.entryPoint     = sv("vs_main");
        desc.primitive.topology    = WGPUPrimitiveTopology_PointList;
        desc.fragment              = &fragment;
        result.render = t.createRenderPipelineTracked(desc);
    }
    return result;
}

// Create a uniform buffer with output indices at 256-byte aligned offsets for dynamic binding.
// A uniform buffer with dynamic offsets is used to provide the output index because:
//  1. It works uniformly across all shader stages (compute, vertex, fragment).
//  2. It doesn't consume the immediate data capability that these tests are actively exercising.
WGPUBuffer createOutputIndexBuffer(AllFeaturesMaxLimitsGpuTest& t, uint32_t count) {
    std::vector<uint32_t> data(static_cast<size_t>(64) * count, 0); // 256 bytes = 64 u32 per count
    for (uint32_t i = 0; i < count; ++i) {
        data[static_cast<size_t>(i) * 64] = i;
    }
    return t.makeBufferWithContents(data.data(), data.size() * sizeof(uint32_t), WGPUBufferUsage_Uniform);
}

struct EncodedPass {
    WGPUTexture renderTarget = nullptr; // null for compute
};

// Encode a pass for the given encoder type.
// For render paths, creates an rgba32uint render target and returns it so callers can read back.
EncodedPass encodeForPassType(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType,
    WGPUCommandEncoder cmdEnc,
    const std::function<void(Pass&)>& fn,
    uint32_t renderTargetWidth = 4)
{
    if (encoderType == "compute pass") {
        Pass pass;
        pass.encoderType = encoderType;
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        pass.computePass = wgpuCommandEncoderBeginComputePass(cmdEnc, &passDesc);
        fn(pass);
        wgpuComputePassEncoderEnd(pass.computePass);
        wgpuComputePassEncoderRelease(pass.computePass);
        return EncodedPass{};
    }

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{renderTargetWidth, 1, 1};
    texDesc.mipLevelCount  = 1;
    texDesc.sampleCount    = 1;
    texDesc.dimension      = WGPUTextureDimension_2D;
    texDesc.format         = kRenderTargetFormat;
    texDesc.usage          = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture renderTargetTexture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(renderTargetTexture, vDesc);

    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view       = view;
    color.loadOp     = WGPULoadOp_Clear;
    color.storeOp    = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor rpDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments     = &color;

    if (encoderType == "render pass") {
        Pass pass;
        pass.encoderType = encoderType;
        pass.renderPass  = wgpuCommandEncoderBeginRenderPass(cmdEnc, &rpDesc);
        fn(pass);
        wgpuRenderPassEncoderEnd(pass.renderPass);
    } else {
        // Render Bundle
        Pass pass;
        pass.encoderType = encoderType;
        WGPUTextureFormat colorFmt = kRenderTargetFormat;
        WGPURenderBundleEncoderDescriptor bDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bDesc.colorFormatCount = 1;
        bDesc.colorFormats     = &colorFmt;
        pass.bundleEnc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDesc);
        fn(pass);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(pass.bundleEnc, nullptr);

        WGPURenderPassEncoder outerPass = wgpuCommandEncoderBeginRenderPass(cmdEnc, &rpDesc);
        wgpuRenderPassEncoderExecuteBundles(outerPass, 1, &bundle);
        wgpuRenderBundleRelease(bundle);
        wgpuRenderBundleEncoderRelease(pass.bundleEnc);
        wgpuRenderPassEncoderEnd(outerPass);
    }

    EncodedPass result;
    result.renderTarget = renderTargetTexture;
    return result;
}

// ---------------------------------------------------------------------------
// runAndCheck
//
// Run a pipeline and check the output values.
//
// For compute: writes to a storage buffer and checks it directly.
// For render: reads back the rgba32uint render target and checks pixel values.
//
// Simple mode (encodeFn omitted): a single draw/dispatch. setImmediatesFn is called after
// setBindGroup. expectedValues.size() must be <= 4.
//
// Multi-draw mode (encodeFn provided): the caller drives all bind group / immediate / draw calls
// via encodeFn(pass, bindGroup, indexUniformBuffer). numDraws and outputU32sPerDraw control the
// output buffer size and render target width.
// ---------------------------------------------------------------------------
struct RunAndCheckOptions {
    uint32_t numDraws          = 1;
    uint32_t outputU32sPerDraw = 0; // 0 sentinel => use expectedValues.size()
    std::function<void(Pass&, WGPUBindGroup, WGPUBuffer)> encodeFn; // optional
    uint32_t renderTargetWidth = 4;
};

void runAndCheck(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType,
    const PipelineHandle& pipeline,
    const std::function<void(Pass&)>& setImmediatesFn,
    const std::vector<uint32_t>& expectedValues,
    const RunAndCheckOptions& opts = {})
{
    t.expect(!expectedValues.empty(), "expectedValues must not be empty");

    const uint32_t u32sPerDraw =
        opts.outputU32sPerDraw != 0 ? opts.outputU32sPerDraw : static_cast<uint32_t>(expectedValues.size());

    WGPUBuffer indexUniformBuffer;
    if (opts.encodeFn) {
        indexUniformBuffer = createOutputIndexBuffer(t, opts.numDraws);
    } else {
        const uint32_t zero = 0;
        indexUniformBuffer = t.makeBufferWithContents(&zero, sizeof(zero), WGPUBufferUsage_Uniform);
    }

    if (encoderType == "compute pass") {
        WGPUBufferDescriptor outDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        outDesc.size  = static_cast<uint64_t>(u32sPerDraw) * 4 * opts.numDraws;
        outDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer outputBuffer = t.createBufferTracked(outDesc);

        WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
        entries[0].binding = 0;
        entries[0].buffer  = outputBuffer;
        entries[0].size    = WGPU_WHOLE_SIZE;
        entries[1].binding = 1;
        entries[1].buffer  = indexUniformBuffer;
        entries[1].size    = 4;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = pipeline.bgl;
        bgDesc.entryCount = 2;
        bgDesc.entries    = entries;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
        encodeForPassType(t, encoderType, cmdEnc, [&](Pass& pass) {
            passSetPipeline(pass, pipeline.compute, pipeline.render);
            if (opts.encodeFn) {
                opts.encodeFn(pass, bindGroup, indexUniformBuffer);
            } else {
                passSetBindGroupDynamic(pass, 0, bindGroup, 0);
                setImmediatesFn(pass);
                passDispatchOrDraw(pass);
            }
        });

        WGPUCommandBuffer cb = t.finishTracked(cmdEnc);
        wgpuQueueSubmit(t.queue(), 1, &cb);
        t.expectGPUBufferValuesEqual(outputBuffer, expectedValues.data(), expectedValues.size() * sizeof(uint32_t));
    } else {
        t.expect(u32sPerDraw <= 4, "runAndCheck supports at most 4 u32s per draw (one rgba32uint pixel)");

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer  = indexUniformBuffer;
        entry.size    = 4;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = pipeline.bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
        const uint32_t pixelWidth = opts.encodeFn ? opts.renderTargetWidth : 1;
        EncodedPass ep = encodeForPassType(t, encoderType, cmdEnc, [&](Pass& pass) {
            passSetPipeline(pass, pipeline.compute, pipeline.render);
            if (opts.encodeFn) {
                opts.encodeFn(pass, bindGroup, indexUniformBuffer);
            } else {
                passSetBindGroupDynamic(pass, 0, bindGroup, 0);
                setImmediatesFn(pass);
                passDispatchOrDraw(pass);
            }
        }, pixelWidth);

        const uint32_t bytesPerRow = align(pixelWidth * kBytesPerPixel, kMinBytesPerRow);
        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = bytesPerRow;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readbackBuffer = t.createBufferTracked(rbDesc);
        t.copyTextureToBuffer(cmdEnc, ep.renderTarget, readbackBuffer, bytesPerRow, WGPUExtent3D{pixelWidth, 1, 1});

        WGPUCommandBuffer cb = t.finishTracked(cmdEnc);
        wgpuQueueSubmit(t.queue(), 1, &cb);

        // Each pixel is 4 u32s (rgba32uint). Pad each draw's output to 4.
        std::vector<uint32_t> paddedExpected(static_cast<size_t>(pixelWidth) * 4, 0);
        for (uint32_t d = 0; d < opts.numDraws; ++d) {
            for (uint32_t i = 0; i < u32sPerDraw; ++i) {
                paddedExpected[static_cast<size_t>(d) * 4 + i] = expectedValues[static_cast<size_t>(d) * u32sPerDraw + i];
            }
        }
        t.expectGPUBufferValuesEqual(readbackBuffer, paddedExpected.data(), paddedExpected.size() * sizeof(uint32_t));
    }
}

// ---------------------------------------------------------------------------
// basic_execution
// ---------------------------------------------------------------------------
// Upstream params:
//   u.combine('encoderType', kProgrammableEncoderTypes).expandWithParams(function*() {
//     for (const s of HostSharableTypes) yield { dataType: s, scalarType: s, vectorSize: 1 };
//     for (const v of kVectorContainerTypes) {
//       const size = parseInt(v[3]);
//       for (const s of HostSharableTypes) yield { dataType: `${v}<${s}>`, scalarType: s, vectorSize: size };
//     }
//     yield { dataType: 'struct', scalarType: undefined, vectorSize: undefined };
//   })
// Mirrored as combine('encoderType') x combineWithParams(<dataType rows>), preserving order.
std::vector<ParamRecord> basicExecutionDataTypeRows() {
    std::vector<ParamRecord> rows;
    const std::vector<std::string> hostSharable = {"i32", "u32", "f16", "f32"};

    // Scalars.
    for (const std::string& s : hostSharable) {
        rows.push_back(ParamRecord{
            {"dataType", s}, {"scalarType", s}, {"vectorSize", 1}});
    }
    // Vectors.
    const std::vector<std::pair<std::string, int>> vectorTypes = {
        {"vec2", 2}, {"vec3", 3}, {"vec4", 4}};
    for (const auto& v : vectorTypes) {
        for (const std::string& s : hostSharable) {
            rows.push_back(ParamRecord{
                {"dataType", v.first + "<" + s + ">"},
                {"scalarType", s},
                {"vectorSize", v.second}});
        }
    }
    // Struct.
    rows.push_back(ParamRecord{
        {"dataType", "struct"},
        {"scalarType", Value::undef()},
        {"vectorSize", Value::undef()}});
    return rows;
}

CTS_TEST(g, "basic_execution")
    .desc("Verify immediate data is correctly passed to shaders.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypes)
            .combineWithParams(basicExecutionDataTypeRows());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateUnsupported(t);

        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string dataType    = t.param<std::string>("dataType");
        const bool isStruct = (dataType == "struct");

        std::string scalarType;
        int vectorSize = 0;
        if (!isStruct) {
            scalarType = t.param<std::string>("scalarType");
            vectorSize = t.param<int>("vectorSize");
        }
        if (!isStruct && scalarType == "f16") {
            t.skip("Immediate data blocks do not yet support f16 types");
        }

        std::string wgslDecl, copyCode, fragmentReturnExpr;
        uint32_t immediateSize = 0;
        std::vector<uint32_t> expected;
        std::vector<uint32_t> inputData;

        if (isStruct) {
            immediateSize = 8;
            wgslDecl = "struct S { a: u32, b: u32 }\nvar<immediate> data: S;";
            copyCode = "output[0] = data.a; output[1] = data.b;";
            fragmentReturnExpr = "vec4u(data.a, data.b, 0, 0)";
            inputData = {0xdeadbeefu, 0xcafebabeu};
            expected  = {0xdeadbeefu, 0xcafebabeu};
        } else {
            immediateSize = static_cast<uint32_t>(vectorSize) * 4;
            wgslDecl = "var<immediate> data: " + dataType + ";";

            // bitcast<u32> is identity for u32, so we can use it unconditionally.
            for (int i = 0; i < vectorSize; ++i) {
                const std::string valExpr = vectorSize == 1 ? "data" : "data[" + std::to_string(i) + "]";
                copyCode += "output[" + std::to_string(i) + "] = bitcast<u32>(" + valExpr + ");\n";
            }

            // Build fragment return expression: pack values into vec4u, padding with 0.
            if (vectorSize == 1) {
                fragmentReturnExpr = "vec4u(bitcast<u32>(data), 0, 0, 0)";
            } else if (vectorSize == 2) {
                fragmentReturnExpr = "vec4u(bitcast<u32>(data[0]), bitcast<u32>(data[1]), 0, 0)";
            } else if (vectorSize == 3) {
                fragmentReturnExpr =
                    "vec4u(bitcast<u32>(data[0]), bitcast<u32>(data[1]), bitcast<u32>(data[2]), 0)";
            } else {
                fragmentReturnExpr =
                    "vec4u(bitcast<u32>(data[0]), bitcast<u32>(data[1]), bitcast<u32>(data[2]), "
                    "bitcast<u32>(data[3]))";
            }

            inputData.resize(static_cast<size_t>(vectorSize));
            for (int i = 0; i < vectorSize; ++i) {
                if (scalarType == "u32") {
                    const uint32_t val = 0x10000000u + static_cast<uint32_t>(i);
                    inputData[static_cast<size_t>(i)] = val;
                    expected.push_back(val);
                } else if (scalarType == "i32") {
                    const int32_t val = -1000 - i;
                    uint32_t bits;
                    std::memcpy(&bits, &val, sizeof(bits));
                    inputData[static_cast<size_t>(i)] = bits;
                    expected.push_back(bits);
                } else if (scalarType == "f32") {
                    const float val = 1.5f + static_cast<float>(i);
                    uint32_t bits;
                    std::memcpy(&bits, &val, sizeof(bits));
                    inputData[static_cast<size_t>(i)] = bits;
                    expected.push_back(bits);
                } else {
                    t.fail("Unhandled scalar type: " + scalarType);
                }
            }
        }

        PipelineHandle pipeline =
            createPipeline(t, encoderType, wgslDecl, copyCode, fragmentReturnExpr, immediateSize);

        runAndCheck(
            t, encoderType, pipeline,
            [&](Pass& pass) {
                passSetImmediates(pass, 0, inputData.data(), inputData.size() * sizeof(uint32_t));
            },
            expected);
    });

// ---------------------------------------------------------------------------
// update_data
// ---------------------------------------------------------------------------
CTS_TEST(g, "update_data")
    .desc("Verify setImmediates updates data correctly within a pass, including partial updates.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypes);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateUnsupported(t);

        const std::string encoderType = t.param<std::string>("encoderType");
        const uint32_t immediateSize = 16;
        const std::string wgslDecl = "var<immediate> data: vec4<u32>;";
        const std::string copyCode =
            "let base = outIndex * 4;\n"
            "output[base + 0] = data[0];\n"
            "output[base + 1] = data[1];\n"
            "output[base + 2] = data[2];\n"
            "output[base + 3] = data[3];\n";
        const std::string fragmentReturnExpr = "vec4u(data[0], data[1], data[2], data[3])";

        PipelineHandle pipeline =
            createPipeline(t, encoderType, wgslDecl, copyCode, fragmentReturnExpr, immediateSize);

        RunAndCheckOptions opts;
        opts.numDraws          = 3;
        opts.outputU32sPerDraw = 4;
        opts.encodeFn = [](Pass& enc, WGPUBindGroup bindGroup, WGPUBuffer /*indexUniformBuffer*/) {
            // Step 1: Full set [1, 2, 3, 4]
            passSetBindGroupDynamic(enc, 0, bindGroup, 0);
            uint32_t d1[4] = {1, 2, 3, 4};
            passSetImmediates(enc, 0, d1, sizeof(d1));
            passDispatchOrDraw(enc);

            // Step 2: Full update [5, 6, 7, 8]
            passSetBindGroupDynamic(enc, 0, bindGroup, 256);
            uint32_t d2[4] = {5, 6, 7, 8};
            passSetImmediates(enc, 0, d2, sizeof(d2));
            passDispatchOrDraw(enc);

            // Step 3: Partial update offset 4 bytes (index 1) with [9, 10] -> [5, 9, 10, 8]
            passSetBindGroupDynamic(enc, 0, bindGroup, 512);
            uint32_t d3[2] = {9, 10};
            passSetImmediates(enc, 4, d3, sizeof(d3));
            passDispatchOrDraw(enc);
        };

        runAndCheck(t, encoderType, pipeline, [](Pass&) {}, {1, 2, 3, 4, 5, 6, 7, 8, 5, 9, 10, 8}, opts);
    });

// ---------------------------------------------------------------------------
// pipeline_switch
// ---------------------------------------------------------------------------
CTS_TEST(g, "pipeline_switch")
    .desc(
        "Verify immediate data is correctly set after switching pipelines.\n"
        "    - sameImmediateSize=true: Both pipelines use the same immediateSize.\n"
        "    - sameImmediateSize=false: Pipelines use different immediateSize values.\n"
        "    In both cases, immediates must be set correctly between draws/dispatches.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", {"render pass", "compute pass"})
            .combine("sameImmediateSize", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateUnsupported(t);

        const std::string encoderType       = t.param<std::string>("encoderType");
        const bool         sameImmediateSize = t.param<bool>("sameImmediateSize");
        const bool         isCompute         = (encoderType == "compute pass");

        // Pipeline A always uses vec4<u32> (16 bytes).
        const std::string wgslDeclA = "var<immediate> data: vec4<u32>;";
        const std::string copyCodeA =
            "output[0] = data.x; output[1] = data.y; output[2] = data.z; output[3] = data.w;";
        const std::string fragmentReturnExprA = "vec4u(data.x, data.y, data.z, data.w)";

        std::string wgslDeclB, copyCodeB, fragmentReturnExprB;
        uint32_t immediateSizeB;
        if (sameImmediateSize) {
            // Pipeline B has the same immediate layout as A (vec4<u32>, 16 bytes).
            wgslDeclB = wgslDeclA;
            copyCodeB = copyCodeA;
            fragmentReturnExprB = fragmentReturnExprA;
            immediateSizeB = 16;
        } else {
            // Pipeline B uses vec2<u32> (8 bytes) -- different/incompatible layout.
            wgslDeclB = "var<immediate> data: vec2<u32>;";
            copyCodeB = "output[0] = data.x; output[1] = data.y; output[2] = 0u; output[3] = 0u;";
            fragmentReturnExprB = "vec4u(data.x, data.y, 0, 0)";
            immediateSizeB = 8;
        }

        // Same source data for both cases; dataSize controls how many elements are written.
        const uint32_t immDataB[4] = {5, 6, 7, 8};
        const uint32_t immDataSizeB = sameImmediateSize ? 4u : (immediateSizeB / 4);
        const std::vector<uint32_t> expectedB =
            sameImmediateSize ? std::vector<uint32_t>{5, 6, 7, 8} : std::vector<uint32_t>{5, 6, 0, 0};

        // Create a shared bind group layout for both pipelines so they are bind-group-compatible.
        // Compute path needs storage + uniform; render path needs only uniform.
        std::vector<WGPUBindGroupLayoutEntry> bglEntries;
        if (isCompute) {
            WGPUBindGroupLayoutEntry e0 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            e0.binding    = 0;
            e0.visibility = WGPUShaderStage_Compute;
            e0.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            e0.buffer.type = WGPUBufferBindingType_Storage;
            bglEntries.push_back(e0);

            WGPUBindGroupLayoutEntry e1 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            e1.binding    = 1;
            e1.visibility = WGPUShaderStage_Compute;
            e1.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            e1.buffer.type             = WGPUBufferBindingType_Uniform;
            e1.buffer.hasDynamicOffset = WGPU_TRUE;
            bglEntries.push_back(e1);
        } else {
            WGPUBindGroupLayoutEntry e0 = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            e0.binding    = 0;
            e0.visibility = WGPUShaderStage_Fragment | WGPUShaderStage_Vertex;
            e0.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            e0.buffer.type             = WGPUBufferBindingType_Uniform;
            e0.buffer.hasDynamicOffset = WGPU_TRUE;
            bglEntries.push_back(e0);
        }
        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = static_cast<uint32_t>(bglEntries.size());
        bglDesc.entries    = bglEntries.data();
        WGPUBindGroupLayout bindGroupLayout = t.createBindGroupLayoutTracked(bglDesc);

        WGPUPipelineLayoutDescriptor layoutADesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutADesc.bindGroupLayoutCount = 1;
        layoutADesc.bindGroupLayouts     = &bindGroupLayout;
        layoutADesc.immediateSize        = 16;
        WGPUPipelineLayout layoutA = t.createPipelineLayoutTracked(layoutADesc);
        PipelineHandle pipelineA = createPipeline(
            t, encoderType, wgslDeclA, copyCodeA, fragmentReturnExprA, 16, 4, layoutA, bindGroupLayout);

        WGPUPipelineLayoutDescriptor layoutBDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutBDesc.bindGroupLayoutCount = 1;
        layoutBDesc.bindGroupLayouts     = &bindGroupLayout;
        layoutBDesc.immediateSize        = immediateSizeB;
        WGPUPipelineLayout layoutB = t.createPipelineLayoutTracked(layoutBDesc);
        PipelineHandle pipelineB = createPipeline(
            t, encoderType, wgslDeclB, copyCodeB, fragmentReturnExprB, immediateSizeB, 4, layoutB,
            bindGroupLayout);

        WGPUBuffer indexUniformBuffer = createOutputIndexBuffer(t, 1);

        if (isCompute) {
            WGPUBufferDescriptor outDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            outDesc.size  = 16; // 4 u32s at outIndex 0
            outDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
            WGPUBuffer outputBuffer = t.createBufferTracked(outDesc);

            WGPUBindGroupEntry entries[2] = {WGPU_BIND_GROUP_ENTRY_INIT, WGPU_BIND_GROUP_ENTRY_INIT};
            entries[0].binding = 0;
            entries[0].buffer  = outputBuffer;
            entries[0].size    = WGPU_WHOLE_SIZE;
            entries[1].binding = 1;
            entries[1].buffer  = indexUniformBuffer;
            entries[1].size    = 4;
            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout     = bindGroupLayout;
            bgDesc.entryCount = 2;
            bgDesc.entries    = entries;
            WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

            WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
            encodeForPassType(t, encoderType, cmdEnc, [&](Pass& enc) {
                // Only set bind group once between bind-group-compatible pipelines.
                passSetPipeline(enc, pipelineA.compute, pipelineA.render);
                passSetBindGroupDynamic(enc, 0, bindGroup, 0);
                uint32_t dA[4] = {1, 2, 3, 4};
                passSetImmediates(enc, 0, dA, sizeof(dA));

                // Switch to Pipeline B without re-setting the bind group.
                passSetPipeline(enc, pipelineB.compute, pipelineB.render);
                passSetImmediates(enc, 0, immDataB, static_cast<size_t>(immDataSizeB) * sizeof(uint32_t));
                passDispatchOrDraw(enc);
            });

            WGPUCommandBuffer cb = t.finishTracked(cmdEnc);
            wgpuQueueSubmit(t.queue(), 1, &cb);
            t.expectGPUBufferValuesEqual(outputBuffer, expectedB.data(), expectedB.size() * sizeof(uint32_t));
        } else {
            WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
            entry.binding = 0;
            entry.buffer  = indexUniformBuffer;
            entry.size    = 4;
            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout     = bindGroupLayout;
            bgDesc.entryCount = 1;
            bgDesc.entries    = &entry;
            WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

            WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
            // encodeForPassType's renderTargetWidth defaults to 4 (matches pipeline vertex math);
            // only the leading pixel is copied back below.
            EncodedPass ep = encodeForPassType(t, encoderType, cmdEnc, [&](Pass& enc) {
                passSetPipeline(enc, pipelineA.compute, pipelineA.render);
                passSetBindGroupDynamic(enc, 0, bindGroup, 0);
                uint32_t dA[4] = {1, 2, 3, 4};
                passSetImmediates(enc, 0, dA, sizeof(dA));

                passSetPipeline(enc, pipelineB.compute, pipelineB.render);
                passSetImmediates(enc, 0, immDataB, static_cast<size_t>(immDataSizeB) * sizeof(uint32_t));
                passDispatchOrDraw(enc);
            });

            const uint32_t bytesPerRow = align(kBytesPerPixel, kMinBytesPerRow);
            WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            rbDesc.size  = bytesPerRow;
            rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
            WGPUBuffer readbackBuffer = t.createBufferTracked(rbDesc);
            t.copyTextureToBuffer(cmdEnc, ep.renderTarget, readbackBuffer, bytesPerRow, WGPUExtent3D{1, 1, 1});

            WGPUCommandBuffer cb = t.finishTracked(cmdEnc);
            wgpuQueueSubmit(t.queue(), 1, &cb);

            // Pad expected to 4 components.
            std::vector<uint32_t> paddedExpected(4, 0);
            for (size_t i = 0; i < expectedB.size(); ++i) {
                paddedExpected[i] = expectedB[i];
            }
            t.expectGPUBufferValuesEqual(readbackBuffer, paddedExpected.data(), paddedExpected.size() * sizeof(uint32_t));
        }
    });

// ---------------------------------------------------------------------------
// use_max_immediate_size
// ---------------------------------------------------------------------------
CTS_TEST(g, "use_max_immediate_size")
    .desc("Verify setImmediates with maxImmediateSize.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypes);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateUnsupported(t);

        const std::string encoderType = t.param<std::string>("encoderType");
        const uint32_t maxImmediateSize = t.getLimits().maxImmediateSize;
        if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
            t.skip("maxImmediateSize limit is undefined");
        }

        // Create a pipeline that reads the first and last u32 of the immediate data.
        const uint32_t count = maxImmediateSize / 4;
        std::string members;
        for (uint32_t i = 0; i < count; ++i) {
            if (i > 0) {
                members += ", ";
            }
            members += "m" + std::to_string(i) + ": u32";
        }
        const std::string wgslDecl = "struct Large { " + members + " } var<immediate> data: Large;";
        const std::string copyCode =
            "output[0] = data.m0;\noutput[1] = data.m" + std::to_string(count - 1) + ";";
        const std::string fragmentReturnExpr =
            "vec4u(data.m0, data.m" + std::to_string(count - 1) + ", 0, 0)";

        PipelineHandle pipeline =
            createPipeline(t, encoderType, wgslDecl, copyCode, fragmentReturnExpr, maxImmediateSize);

        std::vector<uint32_t> data(count, 0);
        data[0]         = 0xdeadbeefu;
        data[count - 1] = 0xcafebabeu;

        runAndCheck(
            t, encoderType, pipeline,
            [&](Pass& pass) {
                passSetImmediates(pass, 0, data.data(), data.size() * sizeof(uint32_t));
            },
            {0xdeadbeefu, 0xcafebabeu});
    });

// ---------------------------------------------------------------------------
// typed_array_arguments
// ---------------------------------------------------------------------------
int bytesPerElementForTypedArrayLocal(const std::string& key) {
    return bytesPerElementForTypedArray(key);
}

CTS_TEST(g, "typed_array_arguments")
    .desc("Verify dataOffset and dataSize arguments work correctly for all TypedArray types.")
    .params([](ParamsBuilder u) {
        // expandWithParams yields six {dataOffset, dataSize} rows whose values depend on the
        // per-case typedArray element size. Those six rows are the cross product
        // dataOffset in {undefined, 0, smallCount} x dataSize in {undefined, smallCount}, so
        // they are reproduced with two subcase-level expand() calls that read `typedArray` from
        // the case record (mirroring expandWithParams). The subcase keys are only
        // dataOffset/dataSize -- re-emitting the case keys would collide.
        return u.combine("typedArray", kTypedArrayBufferViewKeys)
            .combine("encoderType", kProgrammableEncoderTypes)
            .beginSubcases()
            .expand("dataOffset", [](const ParamRecord& p) -> std::vector<Value> {
                const Value* ta = findParam(p, "typedArray");
                const std::string typedArray =
                    ta != nullptr ? std::get<std::string>(ta->data()) : std::string();
                int smallCount = (4 + bytesPerElementForTypedArrayLocal(typedArray) - 1) /
                                 bytesPerElementForTypedArrayLocal(typedArray);
                if (smallCount < 1) {
                    smallCount = 1;
                }
                return {Value::undef(), Value(0), Value(smallCount)};
            })
            .expand("dataSize", [](const ParamRecord& p) -> std::vector<Value> {
                const Value* ta = findParam(p, "typedArray");
                const std::string typedArray =
                    ta != nullptr ? std::get<std::string>(ta->data()) : std::string();
                int smallCount = (4 + bytesPerElementForTypedArrayLocal(typedArray) - 1) /
                                 bytesPerElementForTypedArrayLocal(typedArray);
                if (smallCount < 1) {
                    smallCount = 1;
                }
                return {Value::undef(), Value(smallCount)};
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateUnsupported(t);

        const std::string typedArray  = t.param<std::string>("typedArray");
        const std::string encoderType = t.param<std::string>("encoderType");
        if (typedArray == "Float16Array") {
            t.skip("TODO(#4297): Float16Array not yet supported");
        }
        const int elementSize = bytesPerElementForTypedArray(typedArray);

        // 64 bytes of immediate data (4 x vec4<u32>). This size must match the WGSL struct below.
        const uint32_t kImmediateByteSize = 64;
        const uint32_t kImmediateU32Count = kImmediateByteSize / 4;
        const std::string wgslDecl =
            "struct ImmediateData {\n"
            "  m0: vec4<u32>,\n"
            "  m1: vec4<u32>,\n"
            "  m2: vec4<u32>,\n"
            "  m3: vec4<u32>\n"
            "}\n"
            "var<immediate> data: ImmediateData;";
        const std::string copyCode =
            "output[0] = data.m0.x;\n output[1] = data.m0.y;\n output[2] = data.m0.z;\n output[3] = data.m0.w;\n"
            "output[4] = data.m1.x;\n output[5] = data.m1.y;\n output[6] = data.m1.z;\n output[7] = data.m1.w;\n"
            "output[8] = data.m2.x;\n output[9] = data.m2.y;\n output[10] = data.m2.z;\n output[11] = data.m2.w;\n"
            "output[12] = data.m3.x;\n output[13] = data.m3.y;\n output[14] = data.m3.z;\n output[15] = data.m3.w;\n";
        // For the render path, use outIndex to select which vec4 to return. We do 4 draws at
        // outIndex 0..3, each returning a different vec4.
        const std::string fragmentReturnExpr =
            "select(select(select(\n"
            "  vec4u(data.m3),\n"
            "  vec4u(data.m2),\n"
            "  outIndex == 2u),\n"
            "  vec4u(data.m1),\n"
            "  outIndex == 1u),\n"
            "  vec4u(data.m0),\n"
            "  outIndex == 0u)";

        PipelineHandle pipeline =
            createPipeline(t, encoderType, wgslDecl, copyCode, fragmentReturnExpr, kImmediateByteSize);

        const bool hasDataOffset = !t.paramIsUndefined("dataOffset");
        const bool hasDataSize   = !t.paramIsUndefined("dataSize");
        const int64_t actualDataOffset = hasDataOffset ? t.param<int64_t>("dataOffset") : 0;
        const int64_t maxElements      = kImmediateByteSize / elementSize;
        const int64_t actualDataSize   = hasDataSize ? t.param<int64_t>("dataSize") : (maxElements - actualDataOffset);

        // Validate that the byte size is 4-byte aligned and fits in the immediate block.
        const int64_t byteSize = actualDataSize * elementSize;
        t.expect(byteSize <= static_cast<int64_t>(kImmediateByteSize) && byteSize % 4 == 0,
                 "byteSize must be <= kImmediateByteSize and a multiple of 4");

        // When dataSize is explicit, add padding elements to verify setImmediates respects the
        // dataSize boundary and doesn't read beyond it. When dataSize is undefined, no padding
        // since setImmediates reads to the end of the array.
        const int64_t paddingElements = hasDataSize ? 4 : 0;
        std::vector<uint8_t> arr(
            static_cast<size_t>((actualDataOffset + actualDataSize + paddingElements) * elementSize), 0);

        // Fill the data region with a recognizable byte pattern.
        const int64_t dataByteOffset = actualDataOffset * elementSize;
        const int64_t dataByteSize   = actualDataSize * elementSize;
        for (int64_t byte = 0; byte < dataByteSize; ++byte) {
            arr[static_cast<size_t>(dataByteOffset + byte)] = static_cast<uint8_t>(0x10 + byte);
        }

        // Baseline clear pattern for the full immediate block.
        std::vector<uint32_t> clearData(kImmediateU32Count);
        for (uint32_t i = 0; i < kImmediateU32Count; ++i) {
            clearData[i] = 0xaaaaaaaau + i * 0x11111111u;
        }

        // Build expected: baseline pattern with the written typed-array bytes overlaid at offset 0.
        std::vector<uint32_t> expected = clearData;
        std::memcpy(expected.data(), arr.data() + dataByteOffset, static_cast<size_t>(dataByteSize));

        // For render path, we do 4 draws (one per vec4 member), each outputting 4 u32s to a pixel.
        // For compute path, a single dispatch writes all 16 u32s.
        const uint32_t numDraws = (encoderType == "compute pass") ? 1 : 4;

        RunAndCheckOptions opts;
        opts.numDraws          = numDraws;
        opts.outputU32sPerDraw = (encoderType == "compute pass") ? kImmediateU32Count : 4;
        opts.encodeFn = [&](Pass& enc, WGPUBindGroup bindGroup, WGPUBuffer /*indexUniformBuffer*/) {
            passSetBindGroupDynamic(enc, 0, bindGroup, 0);

            // Initialize immediates to the baseline clear pattern.
            passSetImmediates(enc, 0, clearData.data(), clearData.size() * sizeof(uint32_t));

            // Overwrite with typed array data using the parametrized offset/size.
            passSetImmediates(enc, 0, arr.data() + dataByteOffset, static_cast<size_t>(dataByteSize));

            if (encoderType == "compute pass") {
                passDispatchOrDraw(enc);
            } else {
                // Draw 4 times, each at a different outIndex to read a different vec4.
                for (uint32_t i = 0; i < 4; ++i) {
                    passSetBindGroupDynamic(enc, 0, bindGroup, i * 256);
                    passDispatchOrDraw(enc);
                }
            }
        };

        runAndCheck(t, encoderType, pipeline, [](Pass&) {}, expected, opts);
    });

// ---------------------------------------------------------------------------
// multiple_updates_before_draw_or_dispatch
// ---------------------------------------------------------------------------
CTS_TEST(g, "multiple_updates_before_draw_or_dispatch")
    .desc(
        "Verify that multiple setImmediates calls before a draw or dispatch result in the latest "
        "content being used (merging updates).")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypes);
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateUnsupported(t);

        const std::string encoderType = t.param<std::string>("encoderType");
        // Use vec4<u32> to allow partial updates.
        const std::string wgslDecl = "var<immediate> data: vec4<u32>;";
        const std::string copyCode =
            "output[0] = data.x; output[1] = data.y; output[2] = data.z; output[3] = data.w;";
        const std::string fragmentReturnExpr = "vec4u(data.x, data.y, data.z, data.w)";
        PipelineHandle pipeline =
            createPipeline(t, encoderType, wgslDecl, copyCode, fragmentReturnExpr, 16);

        runAndCheck(
            t, encoderType, pipeline,
            [](Pass& enc) {
                // 1. Set all to [1, 2, 3, 4]
                uint32_t d1[4] = {1, 2, 3, 4};
                passSetImmediates(enc, 0, d1, sizeof(d1));
                // 2. Update middle two to [5, 6] -> [1, 5, 6, 4]
                uint32_t d2[2] = {5, 6};
                passSetImmediates(enc, 4, d2, sizeof(d2));
                // 3. Update last to [7] -> [1, 5, 6, 7]
                uint32_t d3[1] = {7};
                passSetImmediates(enc, 12, d3, sizeof(d3));
            },
            {1, 5, 6, 7});
    });

// ---------------------------------------------------------------------------
// render_pass_and_bundle_mix (no params upstream)
// ---------------------------------------------------------------------------
CTS_TEST(g, "render_pass_and_bundle_mix")
    .desc("Verify interaction between executeBundles and direct render pass commands.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateUnsupported(t);

        const std::string wgslDecl = "var<immediate> data: vec2<u32>;";
        const std::string fragmentReturnExpr = "vec4u(data.x, data.y, 0, 0)";
        const uint32_t renderTargetWidth = 2;

        PipelineHandle pipeline =
            createPipeline(t, "render pass", wgslDecl, "", fragmentReturnExpr, 8, renderTargetWidth);

        WGPUBuffer indexUniformBuffer = createOutputIndexBuffer(t, 2);

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer  = indexUniformBuffer;
        entry.size    = 4;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = pipeline.bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        // Bundle: Set [1, 10], Draw (Index 0)
        WGPUTextureFormat colorFmt = kRenderTargetFormat;
        WGPURenderBundleEncoderDescriptor bDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bDesc.colorFormatCount = 1;
        bDesc.colorFormats     = &colorFmt;
        WGPURenderBundleEncoder bundleEncoder = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDesc);
        wgpuRenderBundleEncoderSetPipeline(bundleEncoder, pipeline.render);
        uint32_t off0 = 0;
        wgpuRenderBundleEncoderSetBindGroup(bundleEncoder, 0, bindGroup, 1, &off0);
        uint32_t bundleData[2] = {1, 10};
        wgpuRenderBundleEncoderSetImmediates(bundleEncoder, 0, bundleData, sizeof(bundleData));
        wgpuRenderBundleEncoderDraw(bundleEncoder, 1, 1, 0, 0);
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(bundleEncoder, nullptr);

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{renderTargetWidth, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = kRenderTargetFormat;
        texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture renderTargetTexture = t.createTextureTracked(texDesc);

        WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
        WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(renderTargetTexture, vDesc);
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view       = view;
        color.loadOp     = WGPULoadOp_Clear;
        color.storeOp    = WGPUStoreOp_Store;
        color.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        WGPURenderPassDescriptor rpDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments     = &color;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(cmdEnc, &rpDesc);

        // Execute Bundle
        wgpuRenderPassEncoderExecuteBundles(pass, 1, &bundle);
        wgpuRenderBundleRelease(bundle);
        wgpuRenderBundleEncoderRelease(bundleEncoder);

        // Pass: Set [2, 20], Draw (Index 1)
        wgpuRenderPassEncoderSetPipeline(pass, pipeline.render);
        uint32_t off256 = 256;
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 1, &off256);
        uint32_t passData[2] = {2, 20};
        wgpuRenderPassEncoderSetImmediates(pass, 0, passData, sizeof(passData));
        wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);

        wgpuRenderPassEncoderEnd(pass);

        // Read back 2 pixels.
        const uint32_t bytesPerRow = align(renderTargetWidth * kBytesPerPixel, kMinBytesPerRow);
        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = bytesPerRow;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readbackBuffer = t.createBufferTracked(rbDesc);
        t.copyTextureToBuffer(cmdEnc, renderTargetTexture, readbackBuffer, bytesPerRow,
                               WGPUExtent3D{renderTargetWidth, 1, 1});

        WGPUCommandBuffer cb = t.finishTracked(cmdEnc);
        wgpuQueueSubmit(t.queue(), 1, &cb);

        // Each pixel is vec4u; we only use the first 2 components.
        const uint32_t expected[8] = {
            1, 10, 0, 0, // pixel 0 (bundle draw)
            2, 20, 0, 0, // pixel 1 (pass draw)
        };
        t.expectGPUBufferValuesEqual(readbackBuffer, expected, sizeof(expected));
    });

// ---------------------------------------------------------------------------
// render_bundle_isolation (no params upstream)
// ---------------------------------------------------------------------------
CTS_TEST(g, "render_bundle_isolation")
    .desc("Verify that immediate data state is isolated between bundles executed in the same pass.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipImmediateUnsupported(t);

        const std::string wgslDecl = "var<immediate> data: vec2<u32>;";
        const std::string fragmentReturnExpr = "vec4u(data.x, data.y, 0, 0)";
        const uint32_t renderTargetWidth = 2;

        PipelineHandle pipeline =
            createPipeline(t, "render pass", wgslDecl, "", fragmentReturnExpr, 8, renderTargetWidth);

        WGPUBuffer indexUniformBuffer = createOutputIndexBuffer(t, 2);

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer  = indexUniformBuffer;
        entry.size    = 4;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = pipeline.bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        WGPUTextureFormat colorFmt = kRenderTargetFormat;

        // Bundle A: Set [1, 2], Draw (Index 0)
        WGPURenderBundleEncoderDescriptor bDescA = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bDescA.colorFormatCount = 1;
        bDescA.colorFormats     = &colorFmt;
        WGPURenderBundleEncoder bundleEncoderA = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDescA);
        wgpuRenderBundleEncoderSetPipeline(bundleEncoderA, pipeline.render);
        uint32_t offA = 0;
        wgpuRenderBundleEncoderSetBindGroup(bundleEncoderA, 0, bindGroup, 1, &offA);
        uint32_t bundleDataA[2] = {1, 2};
        wgpuRenderBundleEncoderSetImmediates(bundleEncoderA, 0, bundleDataA, sizeof(bundleDataA));
        wgpuRenderBundleEncoderDraw(bundleEncoderA, 1, 1, 0, 0);
        WGPURenderBundle bundleA = wgpuRenderBundleEncoderFinish(bundleEncoderA, nullptr);

        // Bundle B: Set [3, 4], Draw (Index 1)
        WGPURenderBundleEncoderDescriptor bDescB = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bDescB.colorFormatCount = 1;
        bDescB.colorFormats     = &colorFmt;
        WGPURenderBundleEncoder bundleEncoderB = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDescB);
        wgpuRenderBundleEncoderSetPipeline(bundleEncoderB, pipeline.render);
        uint32_t offB = 256;
        wgpuRenderBundleEncoderSetBindGroup(bundleEncoderB, 0, bindGroup, 1, &offB);
        uint32_t bundleDataB[2] = {3, 4};
        wgpuRenderBundleEncoderSetImmediates(bundleEncoderB, 0, bundleDataB, sizeof(bundleDataB));
        wgpuRenderBundleEncoderDraw(bundleEncoderB, 1, 1, 0, 0);
        WGPURenderBundle bundleB = wgpuRenderBundleEncoderFinish(bundleEncoderB, nullptr);

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size          = WGPUExtent3D{renderTargetWidth, 1, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.format        = kRenderTargetFormat;
        texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture renderTargetTexture = t.createTextureTracked(texDesc);

        WGPUCommandEncoder cmdEnc = t.createCommandEncoderTracked();
        WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(renderTargetTexture, vDesc);
        WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        color.view       = view;
        color.loadOp     = WGPULoadOp_Clear;
        color.storeOp    = WGPUStoreOp_Store;
        color.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        WGPURenderPassDescriptor rpDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        rpDesc.colorAttachmentCount = 1;
        rpDesc.colorAttachments     = &color;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(cmdEnc, &rpDesc);

        // Execute Bundles
        WGPURenderBundle bundles[2] = {bundleA, bundleB};
        wgpuRenderPassEncoderExecuteBundles(pass, 2, bundles);
        wgpuRenderBundleRelease(bundleA);
        wgpuRenderBundleRelease(bundleB);
        wgpuRenderBundleEncoderRelease(bundleEncoderA);
        wgpuRenderBundleEncoderRelease(bundleEncoderB);

        wgpuRenderPassEncoderEnd(pass);

        // Read back 2 pixels.
        const uint32_t bytesPerRow = align(renderTargetWidth * kBytesPerPixel, kMinBytesPerRow);
        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = bytesPerRow;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readbackBuffer = t.createBufferTracked(rbDesc);
        t.copyTextureToBuffer(cmdEnc, renderTargetTexture, readbackBuffer, bytesPerRow,
                               WGPUExtent3D{renderTargetWidth, 1, 1});

        WGPUCommandBuffer cb = t.finishTracked(cmdEnc);
        wgpuQueueSubmit(t.queue(), 1, &cb);

        // Each pixel is vec4u; we only use the first 2 components.
        const uint32_t expected[8] = {
            1, 2, 0, 0, // pixel 0 (bundle A)
            3, 4, 0, 0, // pixel 1 (bundle B)
        };
        t.expectGPUBufferValuesEqual(readbackBuffer, expected, sizeof(expected));
    });

} // namespace
