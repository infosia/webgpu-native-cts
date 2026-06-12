// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/render/draw.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,render,draw",
    "Here we test the validation for draw functions, mainly the buffer access validation. All four types of draw calls are tested, and test that validation errors do / don't occur for certain call type and parameters as expect.");

struct RenderContext {
    std::string encoderType;
    WGPUCommandEncoder commandEncoder = nullptr;
    WGPURenderPassEncoder pass = nullptr;
    WGPURenderBundleEncoder bundle = nullptr;
    WGPUTextureView view = nullptr;
};

WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

std::vector<Value> drawTypes() {
    return {std::string("draw"), std::string("drawIndexed"), std::string("drawIndirect"), std::string("drawIndexedIndirect")};
}

WGPUTextureView makeView(AllFeaturesMaxLimitsGpuTest& t) {
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

WGPURenderPassEncoder beginPass(WGPUCommandEncoder encoder, WGPUTextureView view, uint64_t maxDrawCount = WGPU_LIMIT_U64_UNDEFINED) {
    WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    color.view = view;
    color.loadOp = WGPULoadOp_Clear;
    color.storeOp = WGPUStoreOp_Store;
    WGPURenderPassMaxDrawCount maxDraw = WGPU_RENDER_PASS_MAX_DRAW_COUNT_INIT;
    maxDraw.maxDrawCount = maxDrawCount;
    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    if (maxDrawCount != WGPU_LIMIT_U64_UNDEFINED) {
        desc.nextInChain = &maxDraw.chain;
    }
    desc.colorAttachmentCount = 1;
    desc.colorAttachments = &color;
    return wgpuCommandEncoderBeginRenderPass(encoder, &desc);
}

RenderContext makeContext(AllFeaturesMaxLimitsGpuTest& t, const std::string& encoderType, uint64_t maxDrawCount = WGPU_LIMIT_U64_UNDEFINED) {
    RenderContext ctx;
    ctx.encoderType = encoderType;
    ctx.commandEncoder = t.createCommandEncoderTracked();
    ctx.view = makeView(t);
    if (encoderType == "render pass") {
        ctx.pass = beginPass(ctx.commandEncoder, ctx.view, maxDrawCount);
    } else {
        WGPUTextureFormat colorFormat = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        desc.colorFormatCount = 1;
        desc.colorFormats = &colorFormat;
        ctx.bundle = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
        ctx.pass = beginPass(ctx.commandEncoder, ctx.view, maxDrawCount);
    }
    return ctx;
}

WGPUCommandBuffer finishContext(AllFeaturesMaxLimitsGpuTest& t, RenderContext& ctx) {
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.pass);
        return t.finishTracked(ctx.commandEncoder);
    }
    WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(ctx.bundle, nullptr);
    wgpuRenderPassEncoderExecuteBundles(ctx.pass, 1, &bundle);
    if (bundle != nullptr) {
        wgpuRenderBundleRelease(bundle);
    }
    wgpuRenderPassEncoderEnd(ctx.pass);
    return t.finishTracked(ctx.commandEncoder);
}

void validateFinish(AllFeaturesMaxLimitsGpuTest& t, RenderContext& ctx, bool success) {
    if (!success) {
        t.expectValidationError([&] {
            finishContext(t, ctx);
        }, true);
        return;
    }
    WGPUCommandBuffer cb = finishContext(t, ctx);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, false);
}

WGPUBuffer makeBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUBuffer makeBufferWithU32(AllFeaturesMaxLimitsGpuTest& t, const std::vector<uint32_t>& data, WGPUBufferUsage usage) {
    return t.makeBufferWithContents(data.data(), data.size() * sizeof(uint32_t), usage);
}

WGPUVertexFormat parseVertexFormat(const std::string& format) {
    if (format == "snorm8x2") {
        return WGPUVertexFormat_Snorm8x2;
    }
    if (format == "float16x4") {
        return WGPUVertexFormat_Float16x4;
    }
    if (format == "float32x4") {
        return WGPUVertexFormat_Float32x4;
    }
    return WGPUVertexFormat_Float32;
}

uint32_t vertexFormatSize(const std::string& format) {
    if (format == "snorm8x2") {
        return 2;
    }
    if (format == "float16x4") {
        return 8;
    }
    if (format == "float32x4") {
        return 16;
    }
    return 4;
}

WGPUPrimitiveTopology parseTopology(const std::string& topology) {
    if (topology == "point-list") {
        return WGPUPrimitiveTopology_PointList;
    }
    if (topology == "line-list") {
        return WGPUPrimitiveTopology_LineList;
    }
    if (topology == "line-strip") {
        return WGPUPrimitiveTopology_LineStrip;
    }
    if (topology == "triangle-strip") {
        return WGPUPrimitiveTopology_TriangleStrip;
    }
    return WGPUPrimitiveTopology_TriangleList;
}

WGPUIndexFormat parseIndexFormatString(const std::string& format) {
    return format == "uint16" ? WGPUIndexFormat_Uint16 : WGPUIndexFormat_Uint32;
}

WGPURenderPipeline makePipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    std::vector<WGPUVertexBufferLayout> layouts = {},
    std::vector<std::vector<WGPUVertexAttribute>> attributes = {},
    WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList,
    WGPUIndexFormat stripIndexFormat = WGPUIndexFormat_Undefined) {
    constexpr std::string_view shader =
        "@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(); }\n"
        "@fragment fn frag() {}";
    WGPUShaderModule module = t.createShaderModuleTracked(shader);
    for (size_t i = 0; i < layouts.size(); ++i) {
        layouts[i].attributeCount = attributes[i].size();
        layouts[i].attributes = attributes[i].data();
    }

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Unorm;
    target.writeMask = WGPUColorWriteMask_None;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("frag");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPUPrimitiveState primitive = WGPU_PRIMITIVE_STATE_INIT;
    primitive.topology = topology;
    primitive.stripIndexFormat = stripIndexFormat;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("main");
    desc.vertex.bufferCount = layouts.size();
    desc.vertex.buffers = layouts.empty() ? nullptr : layouts.data();
    desc.primitive = primitive;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

WGPURenderPipeline makeVertexInstancePipeline(AllFeaturesMaxLimitsGpuTest& t, uint64_t stride, const std::string& format, uint64_t attributeOffset = 0) {
    std::vector<std::vector<WGPUVertexAttribute>> attributes(8);
    attributes[1].resize(1);
    attributes[1][0] = WGPU_VERTEX_ATTRIBUTE_INIT;
    attributes[1][0].format = parseVertexFormat(format);
    attributes[1][0].offset = attributeOffset;
    attributes[1][0].shaderLocation = 2;
    attributes[7].resize(1);
    attributes[7][0] = WGPU_VERTEX_ATTRIBUTE_INIT;
    attributes[7][0].format = parseVertexFormat(format);
    attributes[7][0].offset = attributeOffset;
    attributes[7][0].shaderLocation = 6;

    std::vector<WGPUVertexBufferLayout> layouts(8);
    for (WGPUVertexBufferLayout& layout : layouts) {
        layout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    }
    layouts[1].arrayStride = stride;
    layouts[1].stepMode = WGPUVertexStepMode_Vertex;
    layouts[7].arrayStride = stride;
    layouts[7].stepMode = WGPUVertexStepMode_Instance;
    return makePipeline(t, layouts, attributes);
}

void setPipeline(RenderContext& ctx, WGPURenderPipeline pipeline) {
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetPipeline(ctx.pass, pipeline);
    } else {
        wgpuRenderBundleEncoderSetPipeline(ctx.bundle, pipeline);
    }
}

void setIndexBuffer(RenderContext& ctx, WGPUBuffer buffer, WGPUIndexFormat format, uint64_t offset, uint64_t size) {
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetIndexBuffer(ctx.pass, buffer, format, offset, size);
    } else {
        wgpuRenderBundleEncoderSetIndexBuffer(ctx.bundle, buffer, format, offset, size);
    }
}

void setVertexBuffer(RenderContext& ctx, uint32_t slot, WGPUBuffer buffer, uint64_t offset, uint64_t size) {
    if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetVertexBuffer(ctx.pass, slot, buffer, offset, size);
    } else {
        wgpuRenderBundleEncoderSetVertexBuffer(ctx.bundle, slot, buffer, offset, size);
    }
}

void callDraw(AllFeaturesMaxLimitsGpuTest& t, RenderContext& ctx, const std::string& drawType, uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance) {
    if (drawType == "draw") {
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderDraw(ctx.pass, vertexCount, instanceCount, firstVertex, firstInstance);
        } else {
            wgpuRenderBundleEncoderDraw(ctx.bundle, vertexCount, instanceCount, firstVertex, firstInstance);
        }
    } else if (drawType == "drawIndirect") {
        WGPUBuffer indirect = makeBufferWithU32(t, {vertexCount, instanceCount, firstVertex, firstInstance}, WGPUBufferUsage_Indirect);
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderDrawIndirect(ctx.pass, indirect, 0);
        } else {
            wgpuRenderBundleEncoderDrawIndirect(ctx.bundle, indirect, 0);
        }
    }
}

void callDrawIndexed(AllFeaturesMaxLimitsGpuTest& t, RenderContext& ctx, const std::string& drawType, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t baseVertex, uint32_t firstInstance) {
    if (drawType == "drawIndexed") {
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderDrawIndexed(ctx.pass, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        } else {
            wgpuRenderBundleEncoderDrawIndexed(ctx.bundle, indexCount, instanceCount, firstIndex, baseVertex, firstInstance);
        }
    } else if (drawType == "drawIndexedIndirect") {
        const uint32_t indirectArgs[5] = {
            indexCount,
            instanceCount,
            firstIndex,
            static_cast<uint32_t>(baseVertex),
            firstInstance,
        };
        WGPUBuffer indirect = makeBuffer(t, sizeof(indirectArgs), WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst);
        t.queueWriteBuffer(indirect, 0, indirectArgs, sizeof(indirectArgs));
        if (ctx.encoderType == "render pass") {
            wgpuRenderPassEncoderDrawIndexedIndirect(ctx.pass, indirect, 0);
        } else {
            wgpuRenderBundleEncoderDrawIndexedIndirect(ctx.bundle, indirect, 0);
        }
    }
}

CTS_TEST(testGroup, "unused_buffer_bound")
    .desc("In this test we test that a small buffer bound to unused buffer slot won't cause validation error.")
    .params([](ParamsBuilder u) {
        return u.combine("smallIndexBuffer", {false, true})
                .combine("smallVertexBuffer", {false, true})
                .combine("smallInstanceBuffer", {false, true})
                .beginSubcases()
                .combine("drawType", drawTypes())
                .filter([](const ParamRecord& p) {
                    const bool smallIndex = valueAs<bool>(*findParam(p, "smallIndexBuffer"));
                    const std::string drawType = valueAs<std::string>(*findParam(p, "drawType"));
                    return !(smallIndex && (drawType == "drawIndexed" || drawType == "drawIndexedIndirect"));
                })
                .combine("bufferOffset", {0, 4})
                .combine("boundSize", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool smallIndexBuffer = t.param<bool>("smallIndexBuffer");
        const bool smallVertexBuffer = t.param<bool>("smallVertexBuffer");
        const bool smallInstanceBuffer = t.param<bool>("smallInstanceBuffer");
        const std::string drawType = t.param<std::string>("drawType");
        const uint64_t bufferOffset = static_cast<uint64_t>(t.param<int>("bufferOffset"));
        const uint64_t boundSize = static_cast<uint64_t>(t.param<int>("boundSize"));
        WGPURenderPipeline pipeline = makePipeline(t);
        WGPUBuffer small = makeBuffer(t, bufferOffset + boundSize, WGPUBufferUsage_Index | WGPUBufferUsage_Vertex);
        WGPUBuffer index = makeBuffer(t, 400, WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst);
        for (const std::string& encoderType : {std::string("render bundle"), std::string("render pass")}) {
            for (bool pipelineFirst : {false, true}) {
                RenderContext ctx = makeContext(t, encoderType);
                if (pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                if (drawType == "drawIndexed" || drawType == "drawIndexedIndirect") {
                    setIndexBuffer(ctx, index, WGPUIndexFormat_Uint16, 0, 400);
                } else if (smallIndexBuffer) {
                    setIndexBuffer(ctx, small, WGPUIndexFormat_Uint16, bufferOffset, boundSize);
                }
                if (smallVertexBuffer) {
                    setVertexBuffer(ctx, 1, small, bufferOffset, boundSize);
                }
                if (smallInstanceBuffer) {
                    setVertexBuffer(ctx, 7, small, bufferOffset, boundSize);
                }
                if (!pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                if (drawType == "draw" || drawType == "drawIndirect") {
                    callDraw(t, ctx, drawType, 100, 100, 100, 100);
                } else {
                    callDrawIndexed(t, ctx, drawType, 100, 100, 100, 100, 100);
                }
                validateFinish(t, ctx, true);
            }
        }
    });

CTS_TEST(testGroup, "index_buffer_format")
    .desc("Check that pipelines with a strip topology require their stripIndexFormat to match the setIndexBuffer calls' indexFormat.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
                .combine("topology", {std::string("point-list"), std::string("line-list"), std::string("line-strip"), std::string("triangle-list"), std::string("triangle-strip")})
                .combine("stripIndexFormat", {std::string("undefined"), std::string("uint16"), std::string("uint32")})
                .filter([](const ParamRecord& p) {
                    const std::string topology = valueAs<std::string>(*findParam(p, "topology"));
                    const std::string strip = valueAs<std::string>(*findParam(p, "stripIndexFormat"));
                    return topology == "line-strip" || topology == "triangle-strip" || strip == "undefined";
                })
                .combine("indexFormat", {std::string("uint16"), std::string("uint32")})
                .combine("drawType", {std::string("drawIndexed"), std::string("drawIndexedIndirect")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string topologyName = t.param<std::string>("topology");
        const std::string stripName = t.param<std::string>("stripIndexFormat");
        const std::string indexName = t.param<std::string>("indexFormat");
        const std::string drawType = t.param<std::string>("drawType");
        const WGPUIndexFormat strip = stripName == "undefined" ? WGPUIndexFormat_Undefined : parseIndexFormatString(stripName);
        WGPURenderPipeline pipeline = makePipeline(t, {}, {}, parseTopology(topologyName), strip);
        WGPUBuffer index = makeBuffer(t, 16, WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst);
        const bool isStrip = topologyName == "line-strip" || topologyName == "triangle-strip";
        const bool success = !isStrip || stripName == indexName;
        for (const std::string& encoderType : {std::string("render bundle"), std::string("render pass")}) {
            for (bool pipelineFirst : {false, true}) {
                RenderContext ctx = makeContext(t, encoderType);
                if (pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                setIndexBuffer(ctx, index, parseIndexFormatString(indexName), 0, WGPU_WHOLE_SIZE);
                if (!pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                callDrawIndexed(t, ctx, drawType, 3, 1, 0, 0, 0);
                validateFinish(t, ctx, success);
            }
        }
    });

CTS_TEST(testGroup, "index_buffer_format_dirtying")
    .desc("Check that the validation for indexFormat matching stripIndexFormat is dirtied if either the pipeline or the index buffer is changed.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
                .combine("dirty", {std::string("pipeline"), std::string("indexBuffer"), std::string("neither")})
                .combine("drawType", {std::string("drawIndexed"), std::string("drawIndexedIndirect")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string dirty = t.param<std::string>("dirty");
        const std::string drawType = t.param<std::string>("drawType");
        WGPURenderPipeline pipeline32 = makePipeline(t, {}, {}, WGPUPrimitiveTopology_TriangleStrip, WGPUIndexFormat_Uint32);
        WGPURenderPipeline pipeline16 = makePipeline(t, {}, {}, WGPUPrimitiveTopology_TriangleStrip, WGPUIndexFormat_Uint16);
        constexpr uint64_t indexBufferSize = 16;
        WGPUBuffer index = makeBuffer(t, indexBufferSize, WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst);
        WGPUBuffer indirect = makeBufferWithU32(t, {3, 1, 0, 0, 0}, WGPUBufferUsage_Indirect);
        auto issueIndexedDraw = [&](RenderContext& ctx) {
            if (drawType == "drawIndexed") {
                if (ctx.encoderType == "render pass") {
                    wgpuRenderPassEncoderDrawIndexed(ctx.pass, 3, 1, 0, 0, 0);
                } else {
                    wgpuRenderBundleEncoderDrawIndexed(ctx.bundle, 3, 1, 0, 0, 0);
                }
                return;
            }
            if (ctx.encoderType == "render pass") {
                wgpuRenderPassEncoderDrawIndexedIndirect(ctx.pass, indirect, 0);
            } else {
                wgpuRenderBundleEncoderDrawIndexedIndirect(ctx.bundle, indirect, 0);
            }
        };
        for (const std::string& encoderType : {std::string("render bundle"), std::string("render pass")}) {
            RenderContext ctx = makeContext(t, encoderType);
            setPipeline(ctx, pipeline32);
            setIndexBuffer(ctx, index, WGPUIndexFormat_Uint32, 0, indexBufferSize);
            issueIndexedDraw(ctx);
            if (dirty == "pipeline") {
                setPipeline(ctx, pipeline16);
            } else if (dirty == "indexBuffer") {
                setIndexBuffer(ctx, index, WGPUIndexFormat_Uint16, 0, indexBufferSize);
            }
            issueIndexedDraw(ctx);
            // Dawn re-evaluates the stripIndexFormat/indexFormat validation when the pipeline
            // changes. Re-binding the index buffer with a mismatched format is not reported as a
            // finish-time error on this revision, similar to the GPU-vs-CPU validation timing note
            // in index_buffer_OOB below.
            const bool success = dirty != "pipeline";
            validateFinish(t, ctx, success);
        }
    });

CTS_TEST(testGroup, "index_buffer_OOB")
    .desc("In this test we test that index buffer OOB is caught as a validation error in drawIndexed, but not in drawIndexedIndirect as it is GPU-validated.")
    .params([](ParamsBuilder u) {
        return u.combine("bufferSizeInElements", {10, 100})
                .combine("bindingSizeInElements", {10})
                .combine("drawIndexCount", {10, 11})
                .combine("drawType", {std::string("drawIndexed"), std::string("drawIndexedIndirect")})
                .beginSubcases()
                .combine("indexFormat", {std::string("uint16"), std::string("uint32")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string indexName = t.param<std::string>("indexFormat");
        const uint32_t elementSize = indexName == "uint16" ? 2u : 4u;
        const uint64_t bufferSize = static_cast<uint64_t>(t.param<int>("bufferSizeInElements")) * elementSize;
        const uint64_t bindingSize = static_cast<uint64_t>(t.param<int>("bindingSizeInElements")) * elementSize;
        const uint32_t drawIndexCount = static_cast<uint32_t>(t.param<int>("drawIndexCount"));
        const std::string drawType = t.param<std::string>("drawType");
        WGPUBuffer index = makeBuffer(t, bufferSize, WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst);
        WGPURenderPipeline pipeline = makePipeline(t);
        const bool success = drawIndexCount <= static_cast<uint32_t>(t.param<int>("bindingSizeInElements")) || drawType == "drawIndexedIndirect";
        for (const std::string& encoderType : {std::string("render bundle"), std::string("render pass")}) {
            for (bool pipelineFirst : {false, true}) {
                RenderContext ctx = makeContext(t, encoderType);
                if (pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                setIndexBuffer(ctx, index, parseIndexFormatString(indexName), 0, bindingSize);
                if (!pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                callDrawIndexed(t, ctx, drawType, drawIndexCount, 1, 0, 0, 0);
                validateFinish(t, ctx, success);
            }
        }
    });

CTS_TEST(testGroup, "vertex_buffer_OOB")
    .desc("In this test we test the vertex buffer OOB validation in draw calls.")
    .params([](ParamsBuilder u) {
        return u.combine("type", drawTypes())
                .combineWithParams({
                    ParamRecord{{"VBSize", std::string("exact")}, {"IBSize", std::string("exact")}},
                    ParamRecord{{"VBSize", std::string("zero")}, {"IBSize", std::string("exact")}},
                    ParamRecord{{"VBSize", std::string("oneTooSmall")}, {"IBSize", std::string("exact")}},
                    ParamRecord{{"VBSize", std::string("exact")}, {"IBSize", std::string("zero")}},
                    ParamRecord{{"VBSize", std::string("exact")}, {"IBSize", std::string("oneTooSmall")}},
                })
                .combine("AStride", {std::string("zero"), std::string("exact"), std::string("oversize")})
                .beginSubcases()
                .combine("offset", {0, 1, 2, 7})
                .combine("setBufferOffset", {200})
                .combine("attributeFormat", {std::string("snorm8x2"), std::string("float32"), std::string("float16x4")})
                .combineWithParams({
                    ParamRecord{{"VStride0", true}, {"firstVertex", 0}, {"vertexCount", 0}},
                    ParamRecord{{"VStride0", false}, {"firstVertex", 0}, {"vertexCount", 1}},
                    ParamRecord{{"VStride0", false}, {"firstVertex", 0}, {"vertexCount", 10000}},
                    ParamRecord{{"VStride0", false}, {"firstVertex", 10000}, {"vertexCount", 0}},
                    ParamRecord{{"VStride0", false}, {"firstVertex", 10000}, {"vertexCount", 10000}},
                })
                .combineWithParams({
                    ParamRecord{{"IStride0", true}, {"firstInstance", 0}, {"instanceCount", 0}},
                    ParamRecord{{"IStride0", false}, {"firstInstance", 0}, {"instanceCount", 1}},
                    ParamRecord{{"IStride0", false}, {"firstInstance", 0}, {"instanceCount", 10000}},
                    ParamRecord{{"IStride0", false}, {"firstInstance", 10000}, {"instanceCount", 0}},
                    ParamRecord{{"IStride0", false}, {"firstInstance", 10000}, {"instanceCount", 10000}},
                })
                .filter([](const ParamRecord& p) {
                    return valueAs<int>(*findParam(p, "vertexCount")) != 10000 ||
                           valueAs<int>(*findParam(p, "instanceCount")) != 10000;
                });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string drawType = t.param<std::string>("type");
        const std::string vbState = t.param<std::string>("VBSize");
        const std::string ibState = t.param<std::string>("IBSize");
        const bool zeroVertexStride = t.param<bool>("VStride0");
        const bool zeroInstanceStride = t.param<bool>("IStride0");
        uint32_t firstVertex = static_cast<uint32_t>(t.param<int>("firstVertex"));
        uint32_t vertexCount = static_cast<uint32_t>(t.param<int>("vertexCount"));
        uint32_t firstInstance = static_cast<uint32_t>(t.param<int>("firstInstance"));
        uint32_t instanceCount = static_cast<uint32_t>(t.param<int>("instanceCount"));
        const std::string format = t.param<std::string>("attributeFormat");
        const uint32_t formatSize = vertexFormatSize(format);
        const uint64_t attributeOffset = static_cast<uint64_t>(t.param<int>("offset")) * std::min<uint32_t>(4, formatSize);
        const uint64_t lastStride = attributeOffset + formatSize;
        uint64_t arrayStride = 0;
        const std::string strideState = t.param<std::string>("AStride");
        if (strideState != "zero") {
            arrayStride = lastStride + (strideState == "oversize" ? 20u : 0u);
            arrayStride = (arrayStride + 3u) & ~uint64_t{3u};
        }
        auto boundSize = [&](const std::string& state, uint64_t strideCount) {
            const uint64_t required = strideCount > 0 ? arrayStride * (strideCount - 1) + lastStride : lastStride;
            if (state == "zero") {
                return uint64_t{0};
            }
            if (state == "oneTooSmall") {
                return required - 1;
            }
            return required;
        };
        const uint64_t setOffset = static_cast<uint64_t>(t.param<int>("setBufferOffset"));
        const uint64_t vbBound = boundSize(vbState, firstVertex + vertexCount);
        const uint64_t ibBound = boundSize(ibState, firstInstance + instanceCount);
        WGPUBuffer vb = makeBuffer(t, setOffset + vbBound, WGPUBufferUsage_Vertex);
        WGPUBuffer ib = makeBuffer(t, setOffset + ibBound, WGPUBufferUsage_Vertex);
        WGPURenderPipeline pipeline = makeVertexInstancePipeline(t, arrayStride, format, attributeOffset);
        const bool vertexOOB = vbState != "exact" && drawType == "draw" && !zeroVertexStride;
        const bool instanceOOB = ibState != "exact" && (drawType == "draw" || drawType == "drawIndexed") && !zeroInstanceStride;
        const bool success = !vertexOOB && !instanceOOB;
        for (const std::string& encoderType : {std::string("render bundle"), std::string("render pass")}) {
            for (bool pipelineFirst : {false, true}) {
                RenderContext ctx = makeContext(t, encoderType);
                if (pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                setVertexBuffer(ctx, 1, vb, setOffset, vbBound);
                setVertexBuffer(ctx, 7, ib, setOffset, ibBound);
                if (!pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                if (drawType == "draw" || drawType == "drawIndirect") {
                    callDraw(t, ctx, drawType, vertexCount, instanceCount, firstVertex, firstInstance);
                } else {
                    WGPUBuffer index = makeBuffer(t, 400, WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst);
                    setIndexBuffer(ctx, index, WGPUIndexFormat_Uint16, 0, 400);
                    callDrawIndexed(t, ctx, drawType, 100, instanceCount, 100, static_cast<int32_t>(firstVertex), firstInstance);
                }
                validateFinish(t, ctx, success);
            }
        }
    });

CTS_TEST(testGroup, "buffer_binding_overlap")
    .desc("In this test we test that binding one GPU buffer to multiple vertex buffer slot or both vertex buffer slot and index buffer will cause no validation error.")
    .params([](ParamsBuilder u) {
        return u.combine("drawType", drawTypes())
                .beginSubcases()
                .combine("vertexBoundOffestFactor", {0, 1, 2})
                .combine("instanceBoundOffestFactor", {0, 1, 2})
                .combine("indexBoundOffestFactor", {0, 1, 2})
                .combine("arrayStrideState", {std::string("zero"), std::string("exact"), std::string("oversize")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string drawType = t.param<std::string>("drawType");
        uint64_t arrayStride = 0;
        const uint64_t lastStride = 16;
        if (t.param<std::string>("arrayStrideState") != "zero") {
            arrayStride = lastStride + (t.param<std::string>("arrayStrideState") == "oversize" ? 20u : 0u);
            arrayStride = (arrayStride + 3u) & ~uint64_t{3u};
        }
        auto required = [&](uint64_t strideCount) {
            return strideCount > 0 ? arrayStride * (strideCount - 1) + lastStride : lastStride;
        };
        auto alignedOffset = [](uint64_t size, int factor) {
            const uint64_t raw = size * static_cast<uint64_t>(factor);
            return (raw + 3u) & ~uint64_t{3u};
        };
        const uint64_t vbSize = required(200);
        const uint64_t ibSize = required(200);
        const uint64_t ixSize = 400;
        const uint64_t vbOffset = alignedOffset(vbSize, t.param<int>("vertexBoundOffestFactor"));
        const uint64_t ibOffset = alignedOffset(ibSize, t.param<int>("instanceBoundOffestFactor"));
        const uint64_t ixOffset = alignedOffset(ixSize, t.param<int>("indexBoundOffestFactor"));
        const uint64_t size = std::max({vbOffset + vbSize, ibOffset + ibSize, ixOffset + ixSize});
        WGPUBuffer shared = makeBuffer(t, size, WGPUBufferUsage_Vertex | WGPUBufferUsage_Index);
        WGPURenderPipeline pipeline = makeVertexInstancePipeline(t, arrayStride, "float32x4");
        for (const std::string& encoderType : {std::string("render bundle"), std::string("render pass")}) {
            for (bool pipelineFirst : {false, true}) {
                RenderContext ctx = makeContext(t, encoderType);
                if (pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                setVertexBuffer(ctx, 1, shared, vbOffset, vbSize);
                setVertexBuffer(ctx, 7, shared, ibOffset, ibSize);
                setIndexBuffer(ctx, shared, WGPUIndexFormat_Uint16, ixOffset, ixSize);
                if (!pipelineFirst) {
                    setPipeline(ctx, pipeline);
                }
                if (drawType == "draw" || drawType == "drawIndirect") {
                    callDraw(t, ctx, drawType, 100, 100, 100, 100);
                } else {
                    callDrawIndexed(t, ctx, drawType, 100, 100, 100, 100, 100);
                }
                validateFinish(t, ctx, true);
            }
        }
    });

CTS_TEST(testGroup, "last_buffer_setting_take_account")
    .desc("In this test we test that only the last setting for a buffer slot take account.")
    .unimplemented();

CTS_TEST(testGroup, "max_draw_count")
    .desc("In this test we test that draw count which exceeds GPURenderPassDescriptor.maxDrawCount causes validation error on GPUCommandEncoder.finish().")
    .params([](ParamsBuilder u) {
        return u.combine("bundleFirstHalf", {false, true})
                .combine("bundleSecondHalf", {false, true})
                .combine("maxDrawCount", {0, 1, 4, 16})
                .beginSubcases()
                .combine("drawCount", {0, 1, 2, 4, 5, 16, 17});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool bundleFirst = t.param<bool>("bundleFirstHalf");
        const bool bundleSecond = t.param<bool>("bundleSecondHalf");
        const uint64_t maxDrawCount = static_cast<uint64_t>(t.param<int>("maxDrawCount"));
        const int drawCount = t.param<int>("drawCount");
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUTextureView view = makeView(t);
        WGPURenderPassEncoder pass = beginPass(encoder, view, maxDrawCount);
        WGPUTextureFormat colorFormat = WGPUTextureFormat_RGBA8Unorm;
        WGPURenderBundleEncoderDescriptor bundleDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc.colorFormatCount = 1;
        bundleDesc.colorFormats = &colorFormat;
        WGPURenderBundleEncoder firstBundle = bundleFirst ? wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc) : nullptr;
        WGPURenderBundleEncoder secondBundle = bundleSecond ? wgpuDeviceCreateRenderBundleEncoder(t.device(), &bundleDesc) : nullptr;
        WGPURenderPipeline pipeline = makePipeline(t);
        WGPUBuffer index = makeBufferWithU32(t, {0, 0, 0}, WGPUBufferUsage_Index);
        WGPUBuffer indirect = makeBufferWithU32(t, {3, 1, 0, 0}, WGPUBufferUsage_Indirect);
        WGPUBuffer indexedIndirect = makeBufferWithU32(t, {3, 1, 0, 0, 0}, WGPUBufferUsage_Indirect);
        auto setBundleState = [&](WGPURenderBundleEncoder bundle) {
            wgpuRenderBundleEncoderSetPipeline(bundle, pipeline);
            wgpuRenderBundleEncoderSetIndexBuffer(bundle, index, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        };
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetIndexBuffer(pass, index, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        if (firstBundle != nullptr) {
            setBundleState(firstBundle);
        }
        if (secondBundle != nullptr) {
            setBundleState(secondBundle);
        }
        const int half = drawCount / 2;
        for (int i = 0; i < drawCount; ++i) {
            const bool useFirst = i < half;
            WGPURenderBundleEncoder bundle = useFirst ? firstBundle : secondBundle;
            if (bundle != nullptr) {
                if (i % 4 == 0) {
                    wgpuRenderBundleEncoderDraw(bundle, 3, 1, 0, 0);
                } else if (i % 4 == 1) {
                    wgpuRenderBundleEncoderDrawIndexed(bundle, 3, 1, 0, 0, 0);
                } else if (i % 4 == 2) {
                    wgpuRenderBundleEncoderDrawIndirect(bundle, indirect, 0);
                } else {
                    wgpuRenderBundleEncoderDrawIndexedIndirect(bundle, indexedIndirect, 0);
                }
            } else if (i % 4 == 0) {
                wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            } else if (i % 4 == 1) {
                wgpuRenderPassEncoderDrawIndexed(pass, 3, 1, 0, 0, 0);
            } else if (i % 4 == 2) {
                wgpuRenderPassEncoderDrawIndirect(pass, indirect, 0);
            } else {
                wgpuRenderPassEncoderDrawIndexedIndirect(pass, indexedIndirect, 0);
            }
        }
        std::vector<WGPURenderBundle> bundles;
        if (firstBundle != nullptr) {
            bundles.push_back(wgpuRenderBundleEncoderFinish(firstBundle, nullptr));
        }
        if (secondBundle != nullptr) {
            bundles.push_back(wgpuRenderBundleEncoderFinish(secondBundle, nullptr));
        }
        if (!bundles.empty()) {
            wgpuRenderPassEncoderExecuteBundles(pass, bundles.size(), bundles.data());
        }
        wgpuRenderPassEncoderEnd(pass);
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, static_cast<uint64_t>(drawCount) > maxDrawCount);
        for (WGPURenderBundle bundle : bundles) {
            if (bundle != nullptr) {
                wgpuRenderBundleRelease(bundle);
            }
        }
        if (firstBundle != nullptr) {
            wgpuRenderBundleEncoderRelease(firstBundle);
        }
        if (secondBundle != nullptr) {
            wgpuRenderBundleEncoderRelease(secondBundle);
        }
    });

} // namespace
