// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/command_buffer/queries/occlusionQuery.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// (see docs/UPSTREAM.md).
//
// Port notes:
// - Upstream's QueryHelper / QueryStarter / RenderPassHelper classes are folded into one
//   `RenderPassHelper` that forwards setPipeline/setVertexBuffer/draw to the pass ('direct') or to a
//   per-query render bundle encoder ('render-bundle'), exactly in upstream's call order.
// - Render pipelines depend only on the setup parameters, so they are cached per device by key.

#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,command_buffer,queries,occlusionQuery",
    R"(API operations tests for occlusion queries.

- test query with
  - scissor
  - sample mask
  - alpha to coverage
  - stencil
  - depth test
- test empty query (no draw) (should be cleared?)
- test via render bundle
- test resolveQuerySet with non-zero firstIndex
- test no queries is zero
- test 0x0 -> 0x3 sample mask
- test 0 -> 1 alpha to coverage
- test resolving twice in same pass keeps values
- test resolving twice across pass keeps values
- test resolveQuerySet destinationOffset
- test using the same query multiple times in different passes without resolving in between)");

// capability_info.ts kMaxQueryCount.
constexpr uint32_t kMaxQueryCount = 4096;
constexpr uint64_t kRequiredQueryBufferOffsetAlignment = 256;
constexpr uint64_t kBytesPerQuery = 8;
constexpr uint32_t kTextureSize = 4;

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Upstream SetupParams. `sampleCount == 0` means "not set"; `hasAlpha == false` means alpha is
// undefined.
struct SetupParams {
    uint32_t numQueries = 0;
    WGPUTextureFormat depthStencilFormat = WGPUTextureFormat_Undefined;
    uint32_t sampleCount = 0;
    uint32_t sampleMask = 0xffffffffu;
    bool hasAlpha = false;
    double alpha = 0.0;
    uint32_t writeMask = 0xf;
    bool bufferOffsetNonZero = false;
    bool querySetOffsetNonZero = false;
    bool renderBundle = false;
};

// Resources returned by upstream's OcclusionQueryTest.setup(). Holds the render pass descriptor
// storage, so it is filled in place and never copied.
struct Resources {
    WGPUBuffer readBuffer = nullptr;
    uint64_t readBufferSize = 0;
    WGPUBuffer vertexBuffer = nullptr;
    WGPUBuffer queryResolveBuffer = nullptr;
    uint64_t queryResolveBufferOffset = 0;
    WGPUQuerySet occlusionQuerySet = nullptr;
    uint32_t occlusionQuerySetCount = 0;
    WGPUTexture renderTargetTexture = nullptr;
    WGPURenderPipeline pipeline = nullptr;
    uint32_t querySetOffset = 0;
    bool renderBundle = false;
    bool haveDepth = false;
    bool haveStencil = false;
    uint32_t sampleCount = 0;

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    WGPURenderPassDepthStencilAttachment depthStencilAttachment =
        WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    WGPURenderPassDescriptor renderPassDescriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;

    Resources() = default;
    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    uint32_t numQueries() const { return occlusionQuerySetCount - querySetOffset; }
};

struct OcclusionPipelineCache : DeviceScopedObject {
    std::unordered_map<WGPUDevice, std::map<std::string, WGPURenderPipeline>> byDevice;

    ~OcclusionPipelineCache() override {
        for (auto& [device, pipelines] : byDevice) {
            (void)device;
            for (auto& [key, pipeline] : pipelines) {
                (void)key;
                wgpuRenderPipelineRelease(pipeline);
            }
        }
    }
};

std::string formatNumber(double value) {
    std::ostringstream out;
    out << value;
    return out.str();
}

WGPURenderPipeline getPipeline(AllFeaturesMaxLimitsGpuTest& t, const SetupParams& params) {
    const bool haveDepth = params.depthStencilFormat == WGPUTextureFormat_Depth24Plus;
    const bool haveStencil = params.depthStencilFormat == WGPUTextureFormat_Stencil8;
    const std::string alphaText = params.hasAlpha ? formatNumber(params.alpha) : std::string("1");

    std::ostringstream key;
    key << static_cast<int>(params.depthStencilFormat) << '/' << params.sampleCount << '/'
        << params.sampleMask << '/' << (params.hasAlpha ? alphaText : std::string("none")) << '/'
        << params.writeMask;

    auto& pipelines = deviceScoped<OcclusionPipelineCache>().byDevice[t.device()];
    auto it = pipelines.find(key.str());
    if (it != pipelines.end()) {
        return it->second;
    }

    const std::string code = R"(
        @vertex fn vs(@location(0) pos: vec4f) -> @builtin(position) vec4f {
          return pos;
        }

        @fragment fn fs() -> @location(0) vec4f {
          return vec4f(0, 0, 0, )" + alphaText + R"();
        }
      )";
    WGPUShaderModule module = t.createShaderModuleTracked(code);

    WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
    attribute.shaderLocation = 0;
    attribute.offset = 0;
    attribute.format = WGPUVertexFormat_Float32x3;
    WGPUVertexBufferLayout vertexBuffer = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBuffer.arrayStride = 3 * 4;
    vertexBuffer.stepMode = WGPUVertexStepMode_Vertex;
    vertexBuffer.attributeCount = 1;
    vertexBuffer.attributes = &attribute;

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = static_cast<WGPUColorWriteMask>(params.writeMask);

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = stringView("fs");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.vertex.module = module;
    desc.vertex.entryPoint = stringView("vs");
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexBuffer;
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.fragment = &fragment;
    if (params.sampleCount != 0) {
        desc.multisample.count = params.sampleCount;
        desc.multisample.mask = params.hasAlpha ? 0xffffffffu : params.sampleMask;
        desc.multisample.alphaToCoverageEnabled = params.hasAlpha ? WGPU_TRUE : WGPU_FALSE;
    }

    WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
    if (params.depthStencilFormat != WGPUTextureFormat_Undefined) {
        depthStencil.format = params.depthStencilFormat;
        depthStencil.depthWriteEnabled = haveDepth ? WGPUOptionalBool_True : WGPUOptionalBool_False;
        depthStencil.depthCompare = haveDepth ? WGPUCompareFunction_LessEqual : WGPUCompareFunction_Always;
        if (haveStencil) {
            depthStencil.stencilFront.compare = WGPUCompareFunction_Equal;
        }
        desc.depthStencil = &depthStencil;
    }

    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(t.device(), &desc);
    if (pipeline == nullptr) {
        t.fail("failed to create the occlusion query render pipeline");
    }
    pipelines.emplace(key.str(), pipeline);
    return pipeline;
}

WGPUBuffer createVertexBuffer(AllFeaturesMaxLimitsGpuTest& t, const std::vector<float>& data) {
    return t.makeBufferWithContents(data.data(), data.size() * sizeof(float), WGPUBufferUsage_Vertex);
}

WGPUBuffer createSingleTriangleVertexBuffer(AllFeaturesMaxLimitsGpuTest& t, float z) {
    return createVertexBuffer(t, {
        -0.5f, -0.5f, z,
         0.5f, -0.5f, z,
        -0.5f,  0.5f, z,
    });
}

std::vector<uint64_t> readBufferAsU64(AllFeaturesMaxLimitsGpuTest& t, WGPUBuffer buffer, uint64_t size) {
    t.expectMapAsync(buffer, WGPUMapMode_Read, true, 0, static_cast<size_t>(size));
    const void* mapped = wgpuBufferGetConstMappedRange(buffer, 0, static_cast<size_t>(size));
    if (mapped == nullptr) {
        wgpuBufferUnmap(buffer);
        t.fail("failed to get the mapped range of the query read buffer");
    }
    std::vector<uint64_t> result(static_cast<size_t>(size / sizeof(uint64_t)));
    std::memcpy(result.data(), mapped, result.size() * sizeof(uint64_t));
    wgpuBufferUnmap(buffer);
    return result;
}

WGPUTexture create2DTexture(AllFeaturesMaxLimitsGpuTest& t, WGPUTextureFormat format, uint32_t sampleCount) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kTextureSize, kTextureSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = sampleCount;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

WGPUTextureView createView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, desc);
}

// Upstream OcclusionQueryTest.setup().
void setup(AllFeaturesMaxLimitsGpuTest& t, const SetupParams& params, Resources& r) {
    r.queryResolveBufferOffset = params.bufferOffsetNonZero ? kRequiredQueryBufferOffsetAlignment : 0;
    {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = params.numQueries * kBytesPerQuery + r.queryResolveBufferOffset;
        desc.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        r.queryResolveBuffer = t.createBufferTracked(desc);
    }
    {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = params.numQueries * kBytesPerQuery;
        desc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        r.readBuffer = t.createBufferTracked(desc);
        r.readBufferSize = desc.size;
    }

    r.vertexBuffer = createSingleTriangleVertexBuffer(t, 0.0f);
    r.renderTargetTexture = create2DTexture(t, WGPUTextureFormat_RGBA8Unorm, 1);
    WGPUTexture multisampleRenderTarget = nullptr;
    if (params.sampleCount != 0) {
        multisampleRenderTarget = create2DTexture(t, WGPUTextureFormat_RGBA8Unorm, params.sampleCount);
    }
    WGPUTexture depthStencilTexture = nullptr;
    if (params.depthStencilFormat != WGPUTextureFormat_Undefined) {
        depthStencilTexture = create2DTexture(t, params.depthStencilFormat, 1);
    }

    r.haveDepth = params.depthStencilFormat == WGPUTextureFormat_Depth24Plus;
    r.haveStencil = params.depthStencilFormat == WGPUTextureFormat_Stencil8;
    r.sampleCount = params.sampleCount;
    r.renderBundle = params.renderBundle;

    r.pipeline = getPipeline(t, params);

    r.querySetOffset = params.querySetOffsetNonZero ? 7 : 0;
    {
        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type = WGPUQueryType_Occlusion;
        desc.count = params.numQueries + r.querySetOffset;
        r.occlusionQuerySet = t.createQuerySetTracked(desc);
        r.occlusionQuerySetCount = desc.count;
    }

    r.colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    if (params.sampleCount != 0) {
        r.colorAttachment.view = createView(t, multisampleRenderTarget);
        r.colorAttachment.resolveTarget = createView(t, r.renderTargetTexture);
    } else {
        r.colorAttachment.view = createView(t, r.renderTargetTexture);
    }
    r.colorAttachment.loadOp = WGPULoadOp_Clear;
    r.colorAttachment.storeOp = WGPUStoreOp_Store;
    r.colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    r.renderPassDescriptor = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    r.renderPassDescriptor.colorAttachmentCount = 1;
    r.renderPassDescriptor.colorAttachments = &r.colorAttachment;
    r.renderPassDescriptor.occlusionQuerySet = r.occlusionQuerySet;

    r.depthStencilAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
    if (r.haveDepth) {
        r.depthStencilAttachment.view = createView(t, depthStencilTexture);
        r.depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
        r.depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;
        r.depthStencilAttachment.depthClearValue = 0.5f;
        r.renderPassDescriptor.depthStencilAttachment = &r.depthStencilAttachment;
    }
    if (r.haveStencil) {
        r.depthStencilAttachment.view = createView(t, depthStencilTexture);
        r.depthStencilAttachment.stencilClearValue = 0;
        r.depthStencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
        r.depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Store;
        r.renderPassDescriptor.depthStencilAttachment = &r.depthStencilAttachment;
    }
}

// Upstream RenderPassHelper + QueryHelper{Direct,RenderBundle} + QueryStarter{Direct,RenderBundle}.
// beginOcclusionQuery() returns the helper itself, which then records setPipeline /
// setVertexBuffer / draw on the pass ('direct') or on a fresh render bundle encoder
// ('render-bundle'); end() executes the bundle (if any) and ends the query.
class RenderPassHelper {
  public:
    RenderPassHelper(AllFeaturesMaxLimitsGpuTest& t, WGPURenderPassEncoder pass, const Resources& r)
        : t_(t), pass_(pass), renderBundle_(r.renderBundle) {
        bundleDesc_ = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bundleDesc_.colorFormatCount = 1;
        bundleDesc_.colorFormats = &colorFormat_;
        if (r.haveDepth) {
            bundleDesc_.depthStencilFormat = WGPUTextureFormat_Depth24Plus;
        } else if (r.haveStencil) {
            bundleDesc_.depthStencilFormat = WGPUTextureFormat_Stencil8;
        }
        bundleDesc_.sampleCount = r.sampleCount != 0 ? 4 : 1;
    }

    void setScissorRect(uint32_t x, uint32_t y, uint32_t width, uint32_t height) {
        assertNotInQuery();
        wgpuRenderPassEncoderSetScissorRect(pass_, x, y, width, height);
    }
    void setStencilReference(uint32_t ref) {
        assertNotInQuery();
        wgpuRenderPassEncoderSetStencilReference(pass_, ref);
    }
    RenderPassHelper& beginOcclusionQuery(uint32_t queryIndex) {
        assertNotInQuery();
        wgpuRenderPassEncoderBeginOcclusionQuery(pass_, queryIndex);
        inQuery_ = true;
        if (renderBundle_) {
            bundleEncoder_ = t_.createRenderBundleEncoderTracked(bundleDesc_);
        }
        return *this;
    }
    void setPipeline(WGPURenderPipeline pipeline) {
        assertInQuery();
        if (renderBundle_) {
            wgpuRenderBundleEncoderSetPipeline(bundleEncoder_, pipeline);
        } else {
            wgpuRenderPassEncoderSetPipeline(pass_, pipeline);
        }
    }
    void setVertexBuffer(WGPUBuffer buffer) {
        assertInQuery();
        if (renderBundle_) {
            wgpuRenderBundleEncoderSetVertexBuffer(bundleEncoder_, 0, buffer, 0, WGPU_WHOLE_SIZE);
        } else {
            wgpuRenderPassEncoderSetVertexBuffer(pass_, 0, buffer, 0, WGPU_WHOLE_SIZE);
        }
    }
    void draw(uint32_t count) {
        assertInQuery();
        if (renderBundle_) {
            wgpuRenderBundleEncoderDraw(bundleEncoder_, count, 1, 0, 0);
        } else {
            wgpuRenderPassEncoderDraw(pass_, count, 1, 0, 0);
        }
    }
    void end() {
        assertInQuery();
        if (renderBundle_) {
            WGPURenderBundle bundle = t_.finishRenderBundleTracked(bundleEncoder_);
            wgpuRenderPassEncoderExecuteBundles(pass_, 1, &bundle);
            bundleEncoder_ = nullptr;
        }
        inQuery_ = false;
        wgpuRenderPassEncoderEndOcclusionQuery(pass_);
    }

  private:
    void assertNotInQuery() const {
        if (inQuery_) {
            t_.fail("RenderPassHelper: called while an occlusion query is active");
        }
    }
    void assertInQuery() const {
        if (!inQuery_) {
            t_.fail("RenderPassHelper: called while no occlusion query is active");
        }
    }

    AllFeaturesMaxLimitsGpuTest& t_;
    WGPURenderPassEncoder pass_;
    bool renderBundle_;
    bool inQuery_ = false;
    WGPUTextureFormat colorFormat_ = WGPUTextureFormat_RGBA8Unorm;
    WGPURenderBundleEncoderDescriptor bundleDesc_;
    WGPURenderBundleEncoder bundleEncoder_ = nullptr;
};

using EncodePassFn = std::function<void(RenderPassHelper& helper, uint32_t queryIndex)>;
using CheckQueryIndexResultFn = std::function<void(bool passed, uint32_t queryIndex)>;

void encodeQueries(AllFeaturesMaxLimitsGpuTest& t,
                   const Resources& r,
                   WGPUCommandEncoder encoder,
                   const WGPURenderPassDescriptor* renderPassDescriptor,
                   const EncodePassFn& encodePassFn) {
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, renderPassDescriptor);
    RenderPassHelper helper(t, pass, r);
    for (uint32_t i = 0; i < r.numQueries(); ++i) {
        encodePassFn(helper, i + r.querySetOffset);
    }
    wgpuRenderPassEncoderEnd(pass);
}

void encodeAndResolveQueries(AllFeaturesMaxLimitsGpuTest& t,
                             const Resources& r,
                             WGPUCommandEncoder encoder,
                             const WGPURenderPassDescriptor* renderPassDescriptor,
                             const EncodePassFn& encodePassFn) {
    if (renderPassDescriptor != nullptr) {
        encodeQueries(t, r, encoder, renderPassDescriptor, encodePassFn);
    }
    wgpuCommandEncoderResolveQuerySet(encoder, r.occlusionQuerySet, r.querySetOffset, r.numQueries(),
                                      r.queryResolveBuffer, r.queryResolveBufferOffset);
    wgpuCommandEncoderCopyBufferToBuffer(encoder, r.queryResolveBuffer, r.queryResolveBufferOffset,
                                         r.readBuffer, 0, r.readBufferSize);
}

std::vector<uint64_t> checkQueryResults(AllFeaturesMaxLimitsGpuTest& t,
                                        const Resources& r,
                                        const CheckQueryIndexResultFn& checkQueryIndexResultFn) {
    std::vector<uint64_t> result = readBufferAsU64(t, r.readBuffer, r.readBufferSize);
    for (uint32_t i = 0; i < r.numQueries(); ++i) {
        const uint32_t queryIndex = i + r.querySetOffset;
        const bool passed = result[i] != 0;
        checkQueryIndexResultFn(passed, queryIndex);
    }
    return result;
}

std::vector<uint64_t> runQueryTest(AllFeaturesMaxLimitsGpuTest& t,
                                   const Resources& r,
                                   const WGPURenderPassDescriptor* renderPassDescriptor,
                                   const EncodePassFn& encodePassFn,
                                   const CheckQueryIndexResultFn& checkQueryIndexResultFn) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    encodeAndResolveQueries(t, r, encoder, renderPassDescriptor, encodePassFn);
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    return checkQueryResults(t, r, checkQueryIndexResultFn);
}

// kQueryTestBaseParams.
ParamsBuilder queryTestBaseParams(ParamsBuilder u) {
    return u.combine("writeMask", {0xf, 0x0})
        .combine("renderMode", {"direct", "render-bundle"})
        .combine("bufferOffset", {"zero", "non-zero"})
        .combine("querySetOffset", {"zero", "non-zero"});
}

SetupParams baseSetupParams(const AllFeaturesMaxLimitsGpuTest& t, uint32_t numQueries) {
    SetupParams params;
    params.numQueries = numQueries;
    params.writeMask = static_cast<uint32_t>(t.param<int>("writeMask"));
    params.renderBundle = t.param<std::string>("renderMode") == "render-bundle";
    params.bufferOffsetNonZero = t.param<std::string>("bufferOffset") == "non-zero";
    params.querySetOffsetNonZero = t.param<std::string>("querySetOffset") == "non-zero";
    return params;
}

std::string passedText(bool value) {
    return value ? "true" : "false";
}

void expectPassedEquals(AllFeaturesMaxLimitsGpuTest& t, bool passed, bool expectPassed, uint32_t queryIndex) {
    t.expect(passed == expectPassed,
             "queryIndex: " + std::to_string(queryIndex) + ", was: " + passedText(passed) +
                 ", expected: " + passedText(expectPassed));
}

CTS_TEST(g, "occlusion_query,initial")
    .desc("Test getting contents of QuerySet without any queries.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        SetupParams params;
        params.numQueries = kMaxQueryCount;
        Resources resources;
        setup(t, params, resources);
        runQueryTest(
            t, resources, nullptr, [](RenderPassHelper&, uint32_t) {},
            [&t](bool passed, uint32_t) { t.expect(!passed); });
    });

CTS_TEST(g, "occlusion_query,unsubmitted_render_pass")
    .desc(R"(
Test getting contents of QuerySet without any queries with an unsubmitted render pass that uses them.
This is a regression test for a bug in Dawn where encoding the pass marked the slots as ok to read
when they should not have been marked as okay to read until the command buffer was submitted.
)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        SetupParams params;
        params.numQueries = kMaxQueryCount;
        Resources resources;
        setup(t, params, resources);

        // Encode a render pass that uses the QuerySet but don't submit it.
        {
            WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachment.view = createView(t, resources.renderTargetTexture);
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;
            passDesc.occlusionQuerySet = resources.occlusionQuerySet;

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
            for (uint32_t i = 0; i < resources.occlusionQuerySetCount; i += 2) {
                wgpuRenderPassEncoderBeginOcclusionQuery(pass, i);
                wgpuRenderPassEncoderEndOcclusionQuery(pass);
            }
            wgpuRenderPassEncoderEnd(pass);
            t.finishTracked(encoder);  // Intentionally not submitted.
        }

        runQueryTest(
            t, resources, nullptr, [](RenderPassHelper&, uint32_t) {},
            [&t](bool passed, uint32_t) { t.expect(!passed); });
    });

CTS_TEST(g, "occlusion_query,basic")
    .desc("Test all queries pass")
    .params([](ParamsBuilder u) { return queryTestBaseParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 30;
        Resources resources;
        setup(t, baseSetupParams(t, kNumQueries), resources);

        runQueryTest(
            t, resources, &resources.renderPassDescriptor,
            [&resources](RenderPassHelper& helper, uint32_t queryIndex) {
                RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
                queryHelper.setPipeline(resources.pipeline);
                queryHelper.setVertexBuffer(resources.vertexBuffer);
                queryHelper.draw(3);
                queryHelper.end();
            },
            [&t](bool passed, uint32_t queryIndex) { expectPassedEquals(t, passed, true, queryIndex); });
    });

CTS_TEST(g, "occlusion_query,reuse")
    .desc(R"(
Test queries can be reused.

This tests that if you write to a query twice in the same command buffer (different passes)
and resolve after, that you get the 2nd result.
  )")
    .params([](ParamsBuilder u) { return queryTestBaseParams(u).combine("noop", {"before", "after"}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 30;
        const std::string noop = t.param<std::string>("noop");
        Resources resources;
        setup(t, baseSetupParams(t, kNumQueries), resources);

        const EncodePassFn opEncodePassFn = [&resources](RenderPassHelper& helper, uint32_t queryIndex) {
            RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
            queryHelper.setPipeline(resources.pipeline);
            queryHelper.setVertexBuffer(resources.vertexBuffer);
            queryHelper.draw(3);
            queryHelper.end();
        };
        const EncodePassFn noopEncodePassFn = [](RenderPassHelper& helper, uint32_t queryIndex) {
            RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
            queryHelper.end();
        };
        const bool noopBefore = noop == "before";
        const EncodePassFn& firstOp = noopBefore ? noopEncodePassFn : opEncodePassFn;
        const EncodePassFn& secondOp = noopBefore ? opEncodePassFn : noopEncodePassFn;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        encodeQueries(t, resources, encoder, &resources.renderPassDescriptor, firstOp);
        encodeAndResolveQueries(t, resources, encoder, &resources.renderPassDescriptor, secondOp);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        checkQueryResults(t, resources, [&t, noopBefore](bool passed, uint32_t queryIndex) {
            expectPassedEquals(t, passed, noopBefore, queryIndex);
        });
    });

CTS_TEST(g, "occlusion_query,empty")
    .desc(R"(
      Test beginOcclusionQuery/endOcclusionQuery with nothing in between clears the queries

      Calls beginOcclusionQuery/draw/endOcclusionQuery that should show passing fragments
      and validates they passed. Then executes the same queries (same QuerySet) without drawing.
      Those queries should have not passed.
    )")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 30;
        SetupParams params;
        params.numQueries = kNumQueries;
        Resources resources;
        setup(t, params, resources);

        const auto makeQueryRunner = [&resources](bool draw) {
            return EncodePassFn([&resources, draw](RenderPassHelper& helper, uint32_t queryIndex) {
                RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
                queryHelper.setPipeline(resources.pipeline);
                queryHelper.setVertexBuffer(resources.vertexBuffer);
                if (draw) {
                    queryHelper.draw(3);
                }
                queryHelper.end();
            });
        };
        const auto makeQueryChecker = [&t](bool draw) {
            return CheckQueryIndexResultFn([&t, draw](bool passed, uint32_t queryIndex) {
                const bool expectPassed = draw;
                t.expect(passed == expectPassed,
                         "draw: " + passedText(draw) + ", queryIndex: " + std::to_string(queryIndex) +
                             ", was: " + passedText(passed) + ", expected: " + passedText(expectPassed));
            });
        };

        runQueryTest(t, resources, &resources.renderPassDescriptor, makeQueryRunner(true),
                     makeQueryChecker(true));
        runQueryTest(t, resources, &resources.renderPassDescriptor, makeQueryRunner(false),
                     makeQueryChecker(false));
    });

struct ScissorCase {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    bool occluded;
    const char* name;
};

ScissorCase getScissorRect(uint32_t i) {
    const uint32_t width = kTextureSize;
    const uint32_t height = kTextureSize;
    switch (i % 4) {
        case 0:  // whole target
            return ScissorCase{0, 0, width, height, false, "whole target"};
        case 1:  // center
            return ScissorCase{width / 4, height / 4, width / 2, height / 2, false, "center"};
        case 2:  // none
            return ScissorCase{width / 4, height / 4, 0, 0, true, "none"};
        default:  // top 1/4
            return ScissorCase{0, 0, width, height / 2, true, "top quarter"};
    }
}

CTS_TEST(g, "occlusion_query,scissor")
    .desc(R"(
      Test beginOcclusionQuery/endOcclusionQuery using scissor to occlude
    )")
    .params([](ParamsBuilder u) { return queryTestBaseParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 30;
        Resources resources;
        setup(t, baseSetupParams(t, kNumQueries), resources);

        runQueryTest(
            t, resources, &resources.renderPassDescriptor,
            [&resources](RenderPassHelper& helper, uint32_t queryIndex) {
                const ScissorCase rect = getScissorRect(queryIndex);
                helper.setScissorRect(rect.x, rect.y, rect.width, rect.height);
                RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
                queryHelper.setPipeline(resources.pipeline);
                queryHelper.setVertexBuffer(resources.vertexBuffer);
                queryHelper.draw(3);
                queryHelper.end();
            },
            [&t](bool passed, uint32_t queryIndex) {
                const ScissorCase rect = getScissorRect(queryIndex);
                const bool expectPassed = !rect.occluded;
                t.expect(passed == expectPassed,
                         "queryIndex: " + std::to_string(queryIndex) + ", scissorCase: " + rect.name +
                             ", was: " + passedText(passed) + ", expected: " + passedText(expectPassed));
            });
    });

CTS_TEST(g, "occlusion_query,depth")
    .desc(R"(
      Test beginOcclusionQuery/endOcclusionQuery using depth test to occlude

      Compares depth against 0.5, with alternating vertex buffers which have a depth
      of 0 and 1. When depth check passes, we expect non-zero successful fragments.
    )")
    .params([](ParamsBuilder u) { return queryTestBaseParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 30;
        SetupParams params = baseSetupParams(t, kNumQueries);
        params.depthStencilFormat = WGPUTextureFormat_Depth24Plus;
        Resources resources;
        setup(t, params, resources);
        const WGPUBuffer vertexBufferAtZ0 = resources.vertexBuffer;
        const WGPUBuffer vertexBufferAtZ1 = createSingleTriangleVertexBuffer(t, 1.0f);

        runQueryTest(
            t, resources, &resources.renderPassDescriptor,
            [&resources, vertexBufferAtZ0, vertexBufferAtZ1](RenderPassHelper& helper, uint32_t queryIndex) {
                RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
                queryHelper.setPipeline(resources.pipeline);
                queryHelper.setVertexBuffer(queryIndex % 2 ? vertexBufferAtZ1 : vertexBufferAtZ0);
                queryHelper.draw(3);
                queryHelper.end();
            },
            [&t](bool passed, uint32_t queryIndex) {
                expectPassedEquals(t, passed, queryIndex % 2 == 0, queryIndex);
            });
    });

CTS_TEST(g, "occlusion_query,stencil")
    .desc(R"(
      Test beginOcclusionQuery/endOcclusionQuery using stencil to occlude

      Compares stencil against 0, with alternating stencil reference values of
      of 0 and 1. When stencil test passes, we expect non-zero successful fragments.
    )")
    .params([](ParamsBuilder u) { return queryTestBaseParams(u); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 30;
        SetupParams params = baseSetupParams(t, kNumQueries);
        params.depthStencilFormat = WGPUTextureFormat_Stencil8;
        Resources resources;
        setup(t, params, resources);

        runQueryTest(
            t, resources, &resources.renderPassDescriptor,
            [&resources](RenderPassHelper& helper, uint32_t queryIndex) {
                helper.setStencilReference(queryIndex % 2);
                RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
                queryHelper.setPipeline(resources.pipeline);
                queryHelper.setVertexBuffer(resources.vertexBuffer);
                queryHelper.draw(3);
                queryHelper.end();
            },
            [&t](bool passed, uint32_t queryIndex) {
                expectPassedEquals(t, passed, queryIndex % 2 == 0, queryIndex);
            });
    });

WGPUBuffer createQuad(AllFeaturesMaxLimitsGpuTest& t, float xOffset, float yOffset) {
    return createVertexBuffer(t, {
        xOffset + 0.0f,  yOffset + 0.0f,  0.0f,
        xOffset + 0.25f, yOffset + 0.0f,  0.0f,
        xOffset + 0.0f,  yOffset + 0.25f, 0.0f,
        xOffset + 0.0f,  yOffset + 0.25f, 0.0f,
        xOffset + 0.25f, yOffset + 0.0f,  0.0f,
        xOffset + 0.25f, yOffset + 0.25f, 0.0f,
    });
}

CTS_TEST(g, "occlusion_query,sample_mask")
    .desc(R"(
      Test beginOcclusionQuery/endOcclusionQuery using sample_mask to occlude

      Set sampleMask to 0, 2, 4, 6 and draw quads in top right or bottom left corners of the texel.
      If the corner we draw to matches the corner masked we expect non-zero successful fragments.

      See: https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_standard_multisample_quality_levels
    )")
    .params([](ParamsBuilder u) { return queryTestBaseParams(u).combine("sampleMask", {0, 2, 4, 6}); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 30;
        const uint32_t sampleMask = static_cast<uint32_t>(t.param<int>("sampleMask"));
        SetupParams params = baseSetupParams(t, kNumQueries);
        params.sampleCount = 4;
        params.sampleMask = sampleMask;
        Resources resources;
        setup(t, params, resources);

        // upstream createQuad(offset) == createQuad(offset, offset)
        const WGPUBuffer vertexBufferBL = createQuad(t, 0.0f, 0.0f);
        const WGPUBuffer vertexBufferTR = createQuad(t, 0.25f, 0.25f);

        runQueryTest(
            t, resources, &resources.renderPassDescriptor,
            [&resources, vertexBufferBL, vertexBufferTR](RenderPassHelper& helper, uint32_t queryIndex) {
                RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
                queryHelper.setPipeline(resources.pipeline);
                queryHelper.setVertexBuffer(queryIndex % 2 ? vertexBufferTR : vertexBufferBL);
                queryHelper.draw(6);
                queryHelper.end();
            },
            [&t, sampleMask](bool passed, uint32_t queryIndex) {
                // drawMask is the "sampleMask" representation of the corner we drew to.
                const uint32_t drawMask = queryIndex % 2 ? 2u : 4u;
                const bool expectPassed = (sampleMask & drawMask) != 0;
                expectPassedEquals(t, passed, expectPassed, queryIndex);
            });
    });

CTS_TEST(g, "occlusion_query,alpha_to_coverage")
    .desc(R"(
      Test beginOcclusionQuery/endOcclusionQuery using alphaToCoverage to occlude

      Set alpha to 0, 0.25, 0.5, 0.75, and 1, draw quads in 4 corners of texel.
      Some should be culled. We count how many passed via queries. It's undefined which
      will pass but it is defined how many will pass for a given alpha value.

      Note: It seems like the result is well defined but if we find some devices/drivers
      don't follow this exactly then we can relax check for the expected number of passed
      queries.

      See: https://bgolus.medium.com/anti-aliased-alpha-test-the-esoteric-alpha-to-coverage-8b177335ae4f
    )")
    .params([](ParamsBuilder u) {
        return queryTestBaseParams(u).combine("alpha", {0.0, 0.25, 0.5, 0.75, 1.0});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 32;
        const double alpha = t.param<double>("alpha");
        SetupParams params = baseSetupParams(t, kNumQueries);
        params.sampleCount = 4;
        params.hasAlpha = true;
        params.alpha = alpha;
        Resources resources;
        setup(t, params, resources);

        const std::vector<WGPUBuffer> vertexBuffers = {
            createQuad(t, 0.0f, 0.0f),
            createQuad(t, 0.25f, 0.0f),
            createQuad(t, 0.0f, 0.25f),
            createQuad(t, 0.25f, 0.25f),
        };

        std::vector<uint32_t> numPassedPerGroup(kNumQueries / 4, 0);

        // Count the queries (not queryIndex) so every group of 4 draws covers each quadrant of a
        // pixel once, independent of querySetOffset.
        uint32_t queryCount = 0;
        uint32_t resultCount = 0;
        runQueryTest(
            t, resources, &resources.renderPassDescriptor,
            [&resources, &vertexBuffers, &queryCount](RenderPassHelper& helper, uint32_t queryIndex) {
                RenderPassHelper& queryHelper = helper.beginOcclusionQuery(queryIndex);
                queryHelper.setPipeline(resources.pipeline);
                queryHelper.setVertexBuffer(vertexBuffers[queryCount++ % 4]);
                queryHelper.draw(6);
                queryHelper.end();
            },
            [&numPassedPerGroup, &resultCount](bool passed, uint32_t) {
                const uint32_t groupIndex = resultCount++ / 4;
                numPassedPerGroup[groupIndex] += passed ? 1 : 0;
            });

        const uint32_t expected = static_cast<uint32_t>(alpha / 0.25);
        for (size_t queryGroup = 0; queryGroup < numPassedPerGroup.size(); ++queryGroup) {
            const uint32_t numPassed = numPassedPerGroup[queryGroup];
            t.expect(numPassed == expected,
                     "queryGroup: " + std::to_string(queryGroup) + ", was: " + std::to_string(numPassed) +
                         ", expected: " + std::to_string(expected));
        }
    });

CTS_TEST(g, "occlusion_query,multi_resolve")
    .desc("Test calling resolveQuerySet more than once does not change results")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t kNumQueries = 30;
        SetupParams params;
        params.numQueries = kNumQueries;
        Resources resources;
        setup(t, params, resources);

        WGPUBufferDescriptor readDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readDesc.size = resources.readBufferSize;
        readDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        const WGPUBuffer readBuffer = resources.readBuffer;
        const WGPUBuffer readBuffer2 = t.createBufferTracked(readDesc);
        const WGPUBuffer readBuffer3 = t.createBufferTracked(readDesc);
        const uint64_t readSize = resources.readBufferSize;

        const auto renderSomething = [&t, &resources](WGPUCommandEncoder encoder) {
            WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &resources.renderPassDescriptor);
            wgpuRenderPassEncoderSetPipeline(pass, resources.pipeline);
            wgpuRenderPassEncoderSetVertexBuffer(pass, 0, resources.vertexBuffer, 0, WGPU_WHOLE_SIZE);
            wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, kTextureSize, kTextureSize);
            wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(pass);
        };

        {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            {
                WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &resources.renderPassDescriptor);
                wgpuRenderPassEncoderSetPipeline(pass, resources.pipeline);
                wgpuRenderPassEncoderSetVertexBuffer(pass, 0, resources.vertexBuffer, 0, WGPU_WHOLE_SIZE);
                for (uint32_t i = 0; i < kNumQueries; ++i) {
                    wgpuRenderPassEncoderBeginOcclusionQuery(pass, i);
                    if (i % 2) {
                        wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, kTextureSize, kTextureSize);
                    } else {
                        wgpuRenderPassEncoderSetScissorRect(pass, 0, 0, 0, 0);
                    }
                    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
                    wgpuRenderPassEncoderEndOcclusionQuery(pass);
                }
                wgpuRenderPassEncoderEnd(pass);
            }

            // Intentionally call resolveQuerySet twice
            wgpuCommandEncoderResolveQuerySet(encoder, resources.occlusionQuerySet, 0, kNumQueries,
                                              resources.queryResolveBuffer, 0);
            wgpuCommandEncoderResolveQuerySet(encoder, resources.occlusionQuerySet, 0, kNumQueries,
                                              resources.queryResolveBuffer, 0);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, resources.queryResolveBuffer, 0, readBuffer, 0, readSize);

            // Rendering stuff unrelated should not affect results.
            renderSomething(encoder);

            wgpuCommandEncoderResolveQuerySet(encoder, resources.occlusionQuerySet, 0, kNumQueries,
                                              resources.queryResolveBuffer, 0);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, resources.queryResolveBuffer, 0, readBuffer2, 0, readSize);
            WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }

        // Encode something else and draw again, then read the results
        // They should not be affected.
        {
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            renderSomething(encoder);
            wgpuCommandEncoderResolveQuerySet(encoder, resources.occlusionQuerySet, 0, kNumQueries,
                                              resources.queryResolveBuffer, 0);
            wgpuCommandEncoderCopyBufferToBuffer(encoder, resources.queryResolveBuffer, 0, readBuffer3, 0, readSize);
            WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
        }

        const std::vector<std::vector<uint64_t>> results = {
            readBufferAsU64(t, readBuffer, readSize),
            readBufferAsU64(t, readBuffer2, readSize),
            readBufferAsU64(t, readBuffer3, readSize),
        };

        for (size_t r = 0; r < results.size(); ++r) {
            for (uint32_t i = 0; i < kNumQueries; ++i) {
                const bool passed = results[r][i] != 0;
                const bool expectPassed = (i % 2) != 0;
                t.expect(passed == expectPassed,
                         "result(" + std::to_string(r) + "): queryIndex: " + std::to_string(i) +
                             ", passed: " + passedText(passed) + ", expected: " + passedText(expectPassed));
            }
        }
    });

} // namespace
