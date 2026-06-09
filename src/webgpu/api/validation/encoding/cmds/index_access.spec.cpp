// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/index_access.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,index_access",
    "Validation tests for indexed draws accessing the index buffer.");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view (string literal overload).
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// createIndexBuffer
//   Equivalent to F.createIndexBuffer — allocates a uint32 index buffer
//   filled with the provided values (empty => size=0 buffer).
// ---------------------------------------------------------------------------
static WGPUBuffer createIndexBuffer(AllFeaturesMaxLimitsGpuTest& t,
                                    const std::vector<uint32_t>& indexData)
{
    if (indexData.empty()) {
        // Zero-sized buffer: create via createBufferTracked so it is tracked
        // for cleanup, but do not call makeBufferWithContents (size 0 is valid).
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size  = 0;
        desc.usage = WGPUBufferUsage_Index;
        return t.createBufferTracked(desc);
    }
    return t.makeBufferWithContents(
        indexData.data(),
        indexData.size() * sizeof(uint32_t),
        WGPUBufferUsage_Index);
}

// ---------------------------------------------------------------------------
// createRenderPipeline
//   triangle-strip topology with stripIndexFormat=uint32, single rgba8unorm
//   color attachment.  Mirrors the upstream F.createRenderPipeline().
// ---------------------------------------------------------------------------
static WGPURenderPipeline createRenderPipeline(AllFeaturesMaxLimitsGpuTest& t)
{
    constexpr std::string_view kVertexWGSL =
        "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
        "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
        "}";

    constexpr std::string_view kFragmentWGSL =
        "@fragment fn main() -> @location(0) vec4<f32> {\n"
        "  return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
        "}";

    WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexWGSL);
    WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentWGSL);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = fragModule;
    fragment.entryPoint  = sv("main");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout                   = nullptr; // auto
    desc.vertex.module            = vertModule;
    desc.vertex.entryPoint        = sv("main");
    desc.primitive.topology       = WGPUPrimitiveTopology_TriangleStrip;
    desc.primitive.stripIndexFormat = WGPUIndexFormat_Uint32;
    desc.multisample.count        = 1;
    desc.fragment                 = &fragment;

    return t.createRenderPipelineTracked(desc);
}

// ---------------------------------------------------------------------------
// beginRenderPass
//   Creates a 1×1 rgba8unorm render-attachment texture and opens a render pass.
// ---------------------------------------------------------------------------
static WGPURenderPassEncoder beginRenderPass(AllFeaturesMaxLimitsGpuTest& t,
                                             WGPUCommandEncoder encoder)
{
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.format         = WGPUTextureFormat_RGBA8Unorm;
    texDesc.size           = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount  = 1;
    texDesc.sampleCount    = 1;
    texDesc.dimension      = WGPUTextureDimension_2D;
    texDesc.usage          = WGPUTextureUsage_RenderAttachment;
    WGPUTexture colorTex = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(colorTex, vDesc);

    WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttach.view       = colorView;
    colorAttach.loadOp     = WGPULoadOp_Clear;
    colorAttach.storeOp    = WGPUStoreOp_Store;
    colorAttach.clearValue = WGPUColor{0.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttach;

    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
}

// ---------------------------------------------------------------------------
// drawIndexed
//   Mirrors the upstream F.drawIndexed() helper: sets up the render pass,
//   calls drawIndexed, and either submits (isSuccess=true) or expects a
//   validation error on encoder.finish() (isSuccess=false).
// ---------------------------------------------------------------------------
static void drawIndexed(AllFeaturesMaxLimitsGpuTest& t,
                        WGPUBuffer   indexBuffer,
                        uint32_t     indexCount,
                        uint32_t     instanceCount,
                        uint32_t     firstIndex,
                        int32_t      baseVertex,
                        uint32_t     firstInstance,
                        bool         isSuccess)
{
    WGPURenderPipeline pipeline = createRenderPipeline(t);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = beginRenderPass(t, encoder);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32,
                                        0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDrawIndexed(pass, indexCount, instanceCount, firstIndex,
                                     baseVertex, firstInstance);
    wgpuRenderPassEncoderEnd(pass);

    if (isSuccess) {
        WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
    } else {
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, true);
    }
}

// ---------------------------------------------------------------------------
// Test: out_of_bounds
// ---------------------------------------------------------------------------
CTS_TEST(g, "out_of_bounds")
    .desc(
        "Test drawing with out of bound index access to make sure encoder validation catch the\n"
        "following indexCount and firstIndex OOB conditions\n"
        "- either is within bound but indexCount + firstIndex is out of bound\n"
        "- only firstIndex is out of bound\n"
        "- only indexCount is out of bound\n"
        "- firstIndex much larger than indexCount\n"
        "- indexCount much larger than firstIndex\n"
        "- max uint32 value for both to make sure the sum doesn't overflow\n"
        "- max uint32 indexCount and small firstIndex\n"
        "- max uint32 firstIndex and small indexCount\n"
        "Together with normal and large instanceCount")
    .params([](ParamsBuilder u) {
        return u
            .combineWithParams({
                // draw all 6 out of 6 index
                ParamRecord{{"indexCount", Value(int64_t(6))}, {"firstIndex", Value(int64_t(0))}},
                // draw the last 5 out of 6 index
                ParamRecord{{"indexCount", Value(int64_t(5))}, {"firstIndex", Value(int64_t(1))}},
                // draw the last 1 out of 6 index
                ParamRecord{{"indexCount", Value(int64_t(1))}, {"firstIndex", Value(int64_t(5))}},
                // firstIndex points to the one after last, but (indexCount + firstIndex) * stride <= bufferSize, valid
                ParamRecord{{"indexCount", Value(int64_t(0))}, {"firstIndex", Value(int64_t(6))}},
                // (indexCount + firstIndex) * stride > bufferSize, invalid
                ParamRecord{{"indexCount", Value(int64_t(0))}, {"firstIndex", Value(int64_t(7))}},
                // only indexCount out of bound
                ParamRecord{{"indexCount", Value(int64_t(7))}, {"firstIndex", Value(int64_t(0))}},
                // indexCount + firstIndex out of bound
                ParamRecord{{"indexCount", Value(int64_t(6))}, {"firstIndex", Value(int64_t(1))}},
                // indexCount valid, but (indexCount + firstIndex) out of bound
                ParamRecord{{"indexCount", Value(int64_t(1))}, {"firstIndex", Value(int64_t(6))}},
                // firstIndex much larger than the bound
                ParamRecord{{"indexCount", Value(int64_t(6))}, {"firstIndex", Value(int64_t(10000))}},
                // indexCount much larger than the bound
                ParamRecord{{"indexCount", Value(int64_t(10000))}, {"firstIndex", Value(int64_t(0))}},
                // max uint32 value
                ParamRecord{{"indexCount", Value(int64_t(0xffffffff))}, {"firstIndex", Value(int64_t(0xffffffff))}},
                // max uint32 indexCount and small firstIndex
                ParamRecord{{"indexCount", Value(int64_t(0xffffffff))}, {"firstIndex", Value(int64_t(2))}},
                // small indexCount and max uint32 firstIndex
                ParamRecord{{"indexCount", Value(int64_t(2))}, {"firstIndex", Value(int64_t(0xffffffff))}},
            })
            .combine("instanceCount", {Value(int64_t(1)), Value(int64_t(10000))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t indexCount    = static_cast<uint32_t>(t.param<int64_t>("indexCount"));
        const uint32_t firstIndex    = static_cast<uint32_t>(t.param<int64_t>("firstIndex"));
        const uint32_t instanceCount = static_cast<uint32_t>(t.param<int64_t>("instanceCount"));

        const std::vector<uint32_t> indexData = {0, 1, 2, 3, 1, 2};
        WGPUBuffer indexBuffer = createIndexBuffer(t, indexData);

        // isSuccess: indexCount + firstIndex <= 6  (matches upstream logic; uint64 to avoid overflow)
        const bool isSuccess = (static_cast<uint64_t>(indexCount) + static_cast<uint64_t>(firstIndex)) <= 6;

        drawIndexed(t, indexBuffer, indexCount, instanceCount, firstIndex, 0, 0, isSuccess);
    });

// ---------------------------------------------------------------------------
// Test: out_of_bounds_zero_sized_index_buffer
// ---------------------------------------------------------------------------
CTS_TEST(g, "out_of_bounds_zero_sized_index_buffer")
    .desc(
        "Test drawing with an empty index buffer to make sure the encoder validation catch the\n"
        "following indexCount and firstIndex conditions\n"
        "- indexCount + firstIndex is out of bound\n"
        "- indexCount is 0 but firstIndex is out of bound\n"
        "- only indexCount is out of bound\n"
        "- both are 0s (not out of bound) but index buffer size is 0\n"
        "Together with normal and large instanceCount")
    .params([](ParamsBuilder u) {
        return u
            .combineWithParams({
                // indexCount + firstIndex out of bound
                ParamRecord{{"indexCount", Value(int64_t(3))}, {"firstIndex", Value(int64_t(1))}},
                // indexCount is 0 but firstIndex out of bound
                ParamRecord{{"indexCount", Value(int64_t(0))}, {"firstIndex", Value(int64_t(1))}},
                // only indexCount out of bound
                ParamRecord{{"indexCount", Value(int64_t(3))}, {"firstIndex", Value(int64_t(0))}},
                // just zeros, valid
                ParamRecord{{"indexCount", Value(int64_t(0))}, {"firstIndex", Value(int64_t(0))}},
            })
            .combine("instanceCount", {Value(int64_t(1)), Value(int64_t(10000))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t indexCount    = static_cast<uint32_t>(t.param<int64_t>("indexCount"));
        const uint32_t firstIndex    = static_cast<uint32_t>(t.param<int64_t>("firstIndex"));
        const uint32_t instanceCount = static_cast<uint32_t>(t.param<int64_t>("instanceCount"));

        WGPUBuffer indexBuffer = createIndexBuffer(t, {});

        // isSuccess: indexCount + firstIndex <= 0  (matches upstream logic)
        const bool isSuccess = (static_cast<uint64_t>(indexCount) + static_cast<uint64_t>(firstIndex)) <= 0;

        drawIndexed(t, indexBuffer, indexCount, instanceCount, firstIndex, 0, 0, isSuccess);
    });

} // namespace
