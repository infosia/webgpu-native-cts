// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/encoding/programmable/pipeline_immediate.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup =
    MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
        "api,validation,encoding,programmable,pipeline_immediate",
        "Validate immediate data usage in RenderPassEncoder, "
        "ComputePassEncoder, and RenderBundleEncoder.");

std::vector<Value> programmableEncoderTypes() {
    return {std::string("compute pass"), std::string("render pass"),
            std::string("render bundle")};
}

WGPUStringView sv(const char* s) { return WGPUStringView{s, WGPU_STRLEN}; }

std::vector<ParamRecord> requiredSlotsRows() {
    std::vector<ParamRecord> rows;
    const std::vector<std::string> encoderTypes = {"compute pass", "render pass",
                                                   "render bundle"};
    const std::vector<std::string> scenarios = {
        "scalar",
        "vector",
        "struct_padding",
        "dynamic_indexing",
        "mixed_types",
        "multiple_variables",
    };
    const std::vector<std::string> usages = {"full", "partial", "split",
                                             "overprovision"};
    for (const std::string& encoderType : encoderTypes) {
        const std::vector<std::string> stages =
            encoderType == "compute pass"
                ? std::vector<std::string>{"compute"}
                : std::vector<std::string>{"vertex", "fragment", "both"};
        for (const std::string& scenario : scenarios) {
            for (const std::string& usage : usages) {
                if (scenario == "scalar" && usage == "split") {
                    continue;
                }
                for (const std::string& stage : stages) {
                    rows.push_back(ParamRecord{
                        {"encoderType", encoderType},
                        {"scenario", scenario},
                        {"usage", usage},
                        {"stage", stage},
                    });
                }
            }
        }
    }
    return rows;
}

void skipIfImmediateUnsupported(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPULimits limits = t.getLimits();
    const uint32_t maxImmediateSize = limits.maxImmediateSize;
    if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
        t.skip("Immediate data not supported (maxImmediateSize is 0 or undefined)");
    }
}

WGPUTextureView makeRenderView(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{4, 4, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    WGPUTexture texture = t.createTextureTracked(desc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, viewDesc);
}

WGPUPipelineLayout makeImmediateLayout(AllFeaturesMaxLimitsGpuTest& t,
                                       uint32_t immediateSize) {
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = 0;
    desc.bindGroupLayouts = nullptr;
    desc.immediateSize = immediateSize;
    return t.createPipelineLayoutTracked(desc);
}

WGPUComputePipeline makeComputePipeline(AllFeaturesMaxLimitsGpuTest& t,
                                        WGPUPipelineLayout layout,
                                        std::string_view code) {
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.compute.module = module;
    desc.compute.entryPoint = sv("main_compute");
    return t.createComputePipelineTracked(desc);
}

WGPURenderPipeline makeRenderPipeline(AllFeaturesMaxLimitsGpuTest& t,
                                      WGPUPipelineLayout layout,
                                      std::string_view code) {
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("main_fragment");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("main_vertex");
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

WGPURenderPassEncoder beginRenderPass(WGPUCommandEncoder commandEncoder,
                                      WGPUTextureView view) {
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &color;
    return wgpuCommandEncoderBeginRenderPass(commandEncoder, &passDesc);
}

struct ProgrammableEncoderContext {
    std::string encoderType;
    WGPUCommandEncoder commandEncoder = nullptr;
    WGPUComputePassEncoder computePass = nullptr;
    WGPURenderPassEncoder renderPass = nullptr;
    WGPURenderBundleEncoder bundleEncoder = nullptr;
    WGPURenderPassEncoder bundlePass = nullptr;
};

ProgrammableEncoderContext
makeProgrammableEncoderContext(AllFeaturesMaxLimitsGpuTest& t,
                               const std::string& encoderType) {
    ProgrammableEncoderContext ctx;
    ctx.encoderType = encoderType;
    ctx.commandEncoder = t.createCommandEncoderTracked();

    if (encoderType == "compute pass") {
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        ctx.computePass =
            wgpuCommandEncoderBeginComputePass(ctx.commandEncoder, &passDesc);
    } else {
        WGPUTextureView view = makeRenderView(t);
        if (encoderType == "render pass") {
            ctx.renderPass = beginRenderPass(ctx.commandEncoder, view);
        } else {
            WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
            WGPURenderBundleEncoderDescriptor bundleDesc =
                WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            bundleDesc.colorFormatCount = 1;
            bundleDesc.colorFormats = &format;
            bundleDesc.sampleCount = 1;
            ctx.bundleEncoder =
                wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc);
            ctx.bundlePass = beginRenderPass(ctx.commandEncoder, view);
        }
    }

    return ctx;
}

void ctxSetImmediates(ProgrammableEncoderContext& ctx, uint32_t offset,
                      const void* data, std::size_t size) {
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderSetImmediates(ctx.computePass, offset, data, size);
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetImmediates(ctx.renderPass, offset, data, size);
    } else {
        wgpuRenderBundleEncoderSetImmediates(ctx.bundleEncoder, offset, data, size);
    }
}

void ctxSetZeroImmediates(ProgrammableEncoderContext& ctx, uint32_t offset,
                          std::size_t size) {
    std::vector<uint8_t> data(size, 0);
    ctxSetImmediates(ctx, offset, data.data(), data.size());
}

void ctxRunPass(AllFeaturesMaxLimitsGpuTest& t, ProgrammableEncoderContext& ctx,
                std::string_view code, uint32_t immediateSize) {
    WGPUPipelineLayout layout = makeImmediateLayout(t, immediateSize);
    if (ctx.encoderType == "compute pass") {
        WGPUComputePipeline pipeline = makeComputePipeline(t, layout, code);
        wgpuComputePassEncoderSetPipeline(ctx.computePass, pipeline);
        wgpuComputePassEncoderDispatchWorkgroups(ctx.computePass, 1, 1, 1);
        return;
    }

    WGPURenderPipeline pipeline = makeRenderPipeline(t, layout, code);
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetPipeline(ctx.renderPass, pipeline);
        wgpuRenderPassEncoderDraw(ctx.renderPass, 3, 1, 0, 0);
    } else {
        wgpuRenderBundleEncoderSetPipeline(ctx.bundleEncoder, pipeline);
        wgpuRenderBundleEncoderDraw(ctx.bundleEncoder, 3, 1, 0, 0);
    }
}

WGPUCommandBuffer ctxFinish(AllFeaturesMaxLimitsGpuTest& t,
                            ProgrammableEncoderContext& ctx) {
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderEnd(ctx.computePass);
        wgpuComputePassEncoderRelease(ctx.computePass);
        ctx.computePass = nullptr;
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.renderPass);
        ctx.renderPass = nullptr;
    } else {
        WGPURenderBundle bundle =
            wgpuRenderBundleEncoderFinish(ctx.bundleEncoder, nullptr);
        wgpuRenderBundleEncoderRelease(ctx.bundleEncoder);
        ctx.bundleEncoder = nullptr;
        wgpuRenderPassEncoderExecuteBundles(ctx.bundlePass, 1, &bundle);
        wgpuRenderBundleRelease(bundle);
        wgpuRenderPassEncoderEnd(ctx.bundlePass);
        ctx.bundlePass = nullptr;
    }
    return t.finishTracked(ctx.commandEncoder);
}

void validateFinish(AllFeaturesMaxLimitsGpuTest& t,
                    ProgrammableEncoderContext& ctx, bool shouldSucceed) {
    if (!shouldSucceed) {
        t.expectValidationError([&] { ctxFinish(t, ctx); }, true);
        return;
    }

    WGPUCommandBuffer cb = ctxFinish(t, ctx);
    t.expectValidationError([&] { wgpuQueueSubmit(t.queue(), 1, &cb); }, false);
}

CTS_TEST(testGroup, "required_slots_set")
    .desc("Validate that all immediate data slots required by the pipeline are "
          "set on the encoder.")
    .params([](ParamsBuilder u) {
        return u.combineWithParams(requiredSlotsRows());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipIfImmediateUnsupported(t);

        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string scenario = t.param<std::string>("scenario");
        const std::string usage = t.param<std::string>("usage");
        const std::string stage = t.param<std::string>("stage");

        uint32_t layoutImmediateSize = 0;
        uint32_t trailingPaddingBytes = 0;

        const bool useVertex = stage == "vertex" || stage == "both";
        const bool useFragment = stage == "fragment" || stage == "both";
        const bool bothDifferent = stage == "both";

        std::string declarations;
        std::string helpers;
        std::string callCompute = "use_data();";
        std::string callVertex = "use_data();";
        std::string callFragment = "use_data();";
        std::string computeArgs;
        std::string vertexArgs;
        std::string fragmentArgs;
        std::string fragmentPrelude;

        if (scenario == "scalar") {
            layoutImmediateSize = 4;
            declarations = "var<immediate> data: u32;";
            helpers = "fn use_data() { _ = data; }";
        } else if (scenario == "vector") {
            layoutImmediateSize = 16;
            declarations = "var<immediate> data: vec4<u32>;";
            helpers = "fn use_data() { _ = data; }";
        } else if (scenario == "struct_padding") {
            layoutImmediateSize = 64;
            trailingPaddingBytes = 24;
            declarations = "struct A { v: vec3<u32> }\n"
                           "struct B { v: vec2<u32> }\n"
                           "struct Data { a: A, @align(32) b: B, }\n"
                           "var<immediate> data: Data;";
            helpers = "fn use_a() { _ = data.a.v; }\n"
                      "fn use_b() { _ = data.b.v; }";
            callCompute = "use_b();";
            callVertex = bothDifferent ? "use_a();" : "use_b();";
            callFragment = "use_b();";
        } else if (scenario == "mixed_types") {
            layoutImmediateSize = 32;
            declarations = "struct Mixed { v: u32, f: vec4<u32> }\n"
                           "var<immediate> data: Mixed;";
            helpers = "fn use_v() { _ = data.v; }\n"
                      "fn use_f() { _ = data.f; }";
            callCompute = "use_f();";
            callVertex = bothDifferent ? "use_v();" : "use_f();";
            callFragment = "use_f();";
        } else if (scenario == "dynamic_indexing") {
            layoutImmediateSize = 16;
            declarations = "var<immediate> data: array<u32, 4>;";
            helpers = "fn use_data(i: u32) { _ = data[i]; }";
            computeArgs = "@builtin(local_invocation_index) i: u32";
            callCompute = "use_data(i);";
            vertexArgs = "@builtin(vertex_index) i: u32";
            callVertex = "use_data(i);";
            fragmentArgs = "@builtin(position) pos: vec4<f32>";
            fragmentPrelude = "let i = u32(pos.x);";
            callFragment = "use_data(i);";
        } else if (scenario == "multiple_variables") {
            layoutImmediateSize = 32;
            declarations = "struct S1 { a: u32, x: u32 }\n"
                           "struct S2 { a: u32, y: vec4<u32> }\n"
                           "var<immediate> v1: S1;\n"
                           "var<immediate> v2: S2;";
            helpers = "fn use_v1() { _ = v1.a; }\n"
                      "fn use_v2() { _ = v2.a; }";
            callCompute = "use_v2();";
            callVertex = bothDifferent ? "use_v1();" : "use_v2();";
            callFragment = "use_v2();";
        } else {
            t.fail("unexpected scenario: " + scenario);
            return;
        }

        const std::string code =
            declarations + "\n" + helpers +
            "\n"
            "@compute @workgroup_size(1) fn main_compute(" +
            computeArgs + ") {\n" + callCompute +
            "\n}\n"
            "@vertex fn main_vertex(" +
            vertexArgs + ") -> @builtin(position) vec4<f32> {\n" +
            (useVertex ? callVertex : std::string()) +
            "\nreturn vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
            "}\n"
            "@fragment fn main_fragment(" +
            fragmentArgs + ") -> @location(0) vec4<f32> {\n" + fragmentPrelude +
            "\n" + (useFragment ? callFragment : std::string()) +
            "\nreturn vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
            "}\n";

        const uint32_t requiredSize = layoutImmediateSize - trailingPaddingBytes;
        const uint32_t layoutSize =
            usage == "overprovision" && trailingPaddingBytes == 0
                ? layoutImmediateSize + 4
                : layoutImmediateSize;
        if (layoutSize > t.getLimits().maxImmediateSize) {
            t.skip("maxImmediateSize not large enough for overprovision test");
        }

        ProgrammableEncoderContext ctx =
            makeProgrammableEncoderContext(t, encoderType);

        if (usage == "overprovision") {
            ctxSetZeroImmediates(ctx, 0,
                                 trailingPaddingBytes > 0 ? layoutImmediateSize
                                                          : requiredSize + 4);
        } else if (usage == "full") {
            ctxSetZeroImmediates(ctx, 0, requiredSize);
        } else if (usage == "partial") {
            if (scenario == "multiple_variables") {
                if (stage == "both") {
                    ctxSetZeroImmediates(ctx, 0, 4);
                    ctxSetZeroImmediates(ctx, 16, 16);
                } else if (stage == "vertex") {
                    ctxSetZeroImmediates(ctx, 0, 4);
                } else if (stage == "fragment" || stage == "compute") {
                    ctxSetZeroImmediates(ctx, 16, 16);
                } else {
                    t.fail("unexpected stage: " + stage);
                    return;
                }
            } else {
                const uint32_t partialSize = requiredSize >= 8 ? requiredSize / 2 : 0;
                ctxSetZeroImmediates(ctx, 0, partialSize);
            }
        } else if (usage == "split") {
            if (scenario == "struct_padding") {
                ctxSetZeroImmediates(ctx, 0, 12);
                ctxSetZeroImmediates(ctx, 32, 8);
            } else if (scenario == "mixed_types") {
                ctxSetZeroImmediates(ctx, 0, 4);
                ctxSetZeroImmediates(ctx, 16, 16);
            } else if (scenario == "multiple_variables") {
                ctxSetZeroImmediates(ctx, 0, 8);
                ctxSetZeroImmediates(ctx, 16, 16);
            } else if (scenario == "vector" || scenario == "dynamic_indexing") {
                ctxSetZeroImmediates(ctx, 0, 8);
                ctxSetZeroImmediates(ctx, 8, 8);
            } else {
                t.fail("unexpected split scenario: " + scenario);
                return;
            }
        } else {
            t.fail("unexpected usage: " + usage);
            return;
        }

        ctxRunPass(t, ctx, code, layoutSize);

        const bool shouldSucceed =
            usage == "full" || usage == "split" || usage == "overprovision";
        validateFinish(t, ctx, shouldSucceed);
    });

CTS_TEST(testGroup, "unused_variable")
    .desc("Validate that if an immediate data variable is declared but not "
          "statically used, it does not require slots to be set.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
            .combine("usage", {std::string("none"), std::string("partial_start")})
            .combine("scenario", {std::string("not_referenced"),
                                  std::string("referenced_in_unused_function")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipIfImmediateUnsupported(t);

        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string usage = t.param<std::string>("usage");
        const std::string scenario = t.param<std::string>("scenario");
        constexpr uint32_t kImmediateSize = 16;

        const char* notReferenced =
            "var<immediate> data: vec4<u32>;\n"
            "@compute @workgroup_size(1) fn main_compute() {}\n"
            "@vertex fn main_vertex() -> @builtin(position) vec4<f32> {\n"
            "return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
            "}\n"
            "@fragment fn main_fragment() -> @location(0) vec4<f32> {\n"
            "return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
            "}\n";
        const char* referencedInUnusedFunction =
            "var<immediate> data: vec4<u32>;\n"
            "fn unused_helper() { _ = data; }\n"
            "@compute @workgroup_size(1) fn main_compute() {}\n"
            "@vertex fn main_vertex() -> @builtin(position) vec4<f32> {\n"
            "return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
            "}\n"
            "@fragment fn main_fragment() -> @location(0) vec4<f32> {\n"
            "return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
            "}\n";
        const char* code = scenario == "not_referenced"
                               ? notReferenced
                               : referencedInUnusedFunction;

        ProgrammableEncoderContext ctx =
            makeProgrammableEncoderContext(t, encoderType);
        if (usage == "partial_start") {
            ctxSetZeroImmediates(ctx, 0, 8);
        }

        ctxRunPass(t, ctx, code, kImmediateSize);
        validateFinish(t, ctx, true);
    });

CTS_TEST(testGroup, "overprovisioned_immediate_data")
    .desc("Validate that setting more immediate data than used by the shader "
          "is valid.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", programmableEncoderTypes())
            .combine("scenario", {std::string("larger_than_shader"),
                                  std::string("larger_than_layout")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipIfImmediateUnsupported(t);

        const std::string encoderType = t.param<std::string>("encoderType");
        const std::string scenario = t.param<std::string>("scenario");
        constexpr uint32_t kLayoutSize = 16;
        const char* code =
            "var<immediate> data: u32;\n"
            "fn use_data() { _ = data; }\n"
            "@compute @workgroup_size(1) fn main_compute() { use_data(); }\n"
            "@vertex fn main_vertex() -> @builtin(position) vec4<f32> {\n"
            "use_data(); return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
            "}\n"
            "@fragment fn main_fragment() -> @location(0) vec4<f32> {\n"
            "use_data(); return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
            "}\n";

        ProgrammableEncoderContext ctx =
            makeProgrammableEncoderContext(t, encoderType);
        const uint32_t setSize =
            scenario == "larger_than_layout" ? kLayoutSize + 4 : kLayoutSize;
        ctxSetZeroImmediates(ctx, 0, setSize);
        ctxRunPass(t, ctx, code, kLayoutSize);
        validateFinish(t, ctx, true);
    });

CTS_TEST(testGroup, "render_bundle_execution_state_invalidation")
    .desc("Validate that executeBundles invalidates the current immediate data "
          "state in the RenderPassEncoder.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("resetImmediates", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipIfImmediateUnsupported(t);

        const bool resetImmediates = t.param<bool>("resetImmediates");
        constexpr uint32_t kImmediateSize = 16;
        const char* code =
            "var<immediate> data: vec4<u32>;\n"
            "fn use_data() { _ = data; }\n"
            "@vertex fn main_vertex() -> @builtin(position) vec4<f32> {\n"
            "use_data(); return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
            "}\n"
            "@fragment fn main_fragment() -> @location(0) vec4<f32> {\n"
            "use_data(); return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
            "}\n";
        std::vector<uint8_t> immediateData(kImmediateSize, 0);
        WGPUPipelineLayout layout = makeImmediateLayout(t, kImmediateSize);
        WGPURenderPipeline pipeline = makeRenderPipeline(t, layout, code);

        WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor bundleDesc =
            WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc.colorFormatCount = 1;
        bundleDesc.colorFormats = &format;
        bundleDesc.sampleCount = 1;
        WGPURenderBundleEncoder bundleEncoder =
            wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc);
        wgpuRenderBundleEncoderSetPipeline(bundleEncoder, pipeline);
        wgpuRenderBundleEncoderSetImmediates(
            bundleEncoder, 0, immediateData.data(), immediateData.size());
        wgpuRenderBundleEncoderDraw(bundleEncoder, 3, 1, 0, 0);
        WGPURenderBundle bundle =
            wgpuRenderBundleEncoderFinish(bundleEncoder, nullptr);
        wgpuRenderBundleEncoderRelease(bundleEncoder);

        ProgrammableEncoderContext ctx =
            makeProgrammableEncoderContext(t, std::string("render pass"));
        wgpuRenderPassEncoderSetPipeline(ctx.renderPass, pipeline);
        wgpuRenderPassEncoderSetImmediates(
            ctx.renderPass, 0, immediateData.data(), immediateData.size());
        wgpuRenderPassEncoderExecuteBundles(ctx.renderPass, 1, &bundle);
        wgpuRenderBundleRelease(bundle);
        wgpuRenderPassEncoderSetPipeline(ctx.renderPass, pipeline);
        if (resetImmediates) {
            wgpuRenderPassEncoderSetImmediates(
                ctx.renderPass, 0, immediateData.data(), immediateData.size());
        }
        wgpuRenderPassEncoderDraw(ctx.renderPass, 3, 1, 0, 0);

        validateFinish(t, ctx, resetImmediates);
    });

} // namespace
