// Ported from gpuweb/cts src/webgpu/api/validation/texture/rg11b10ufloat_renderable.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,texture,rg11b10ufloat_renderable",
    "Tests for capabilities added by rg11b10ufloat-renderable flag.");

// Returns a WGPUStringView pointing at a null-terminated string.
static WGPUStringView sv(const char* s) {
    WGPUStringView v = WGPU_STRING_VIEW_INIT;
    v.data   = s;
    v.length = WGPU_STRLEN;
    return v;
}

// No-op vertex shader (matches vtu.getNoOpShaderCode('VERTEX')).
static constexpr const char* kVertexShader = R"(
    @vertex fn main() -> @builtin(position) vec4<f32> {
      return vec4<f32>();
    }
)";

// No-op fragment shader (matches vtu.getNoOpShaderCode('FRAGMENT')).
static constexpr const char* kFragmentShader = "@fragment fn main() {}";

CTS_TEST(g, "create_texture")
    .desc(R"(
Test that it is valid to create rg11b10ufloat texture with RENDER_ATTACHMENT usage and/or
sampleCount > 1, iff rg11b10ufloat-renderable feature is enabled.
Note, the createTexture tests cover these validation cases where this feature is not enabled.
)")
    .params([](ParamsBuilder u) {
        return u.combine("sampleCount", {Value(1), Value(4)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable) == 0) {
            t.skip("rg11b10ufloat-renderable feature is not available on this device");
        }

        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size.width           = 1;
        desc.size.height          = 1;
        desc.size.depthOrArrayLayers = 1;
        desc.format               = WGPUTextureFormat_RG11B10Ufloat;
        desc.sampleCount          = sampleCount;
        desc.usage                = WGPUTextureUsage_RenderAttachment;
        t.createTextureTracked(desc);
    });

CTS_TEST(g, "begin_render_pass_single_sampled")
    .desc(R"(
Test that it is valid to begin render pass with rg11b10ufloat texture format
iff rg11b10ufloat-renderable feature is enabled. Single sampled case.
)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable) == 0) {
            t.skip("rg11b10ufloat-renderable feature is not available on this device");
        }

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size.width            = 1;
        texDesc.size.height           = 1;
        texDesc.size.depthOrArrayLayers = 1;
        texDesc.format                = WGPUTextureFormat_RG11B10Ufloat;
        texDesc.sampleCount           = 1;
        texDesc.usage                 = WGPUTextureUsage_RenderAttachment;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(texture, viewDesc);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view       = view;
        colorAttachment.loadOp     = WGPULoadOp_Clear;
        colorAttachment.storeOp    = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        t.finishTracked(encoder);
    });

CTS_TEST(g, "begin_render_pass_msaa_and_resolve")
    .desc(R"(
Test that it is valid to begin render pass with rg11b10ufloat texture format
iff rg11b10ufloat-renderable feature is enabled. MSAA and resolve case.
)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable) == 0) {
            t.skip("rg11b10ufloat-renderable feature is not available on this device");
        }

        // MSAA render texture (sampleCount=4).
        WGPUTextureDescriptor renderTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        renderTexDesc.size.width            = 1;
        renderTexDesc.size.height           = 1;
        renderTexDesc.size.depthOrArrayLayers = 1;
        renderTexDesc.format                = WGPUTextureFormat_RG11B10Ufloat;
        renderTexDesc.sampleCount           = 4;
        renderTexDesc.usage                 = WGPUTextureUsage_RenderAttachment;
        WGPUTexture renderTexture = t.createTextureTracked(renderTexDesc);

        // Single-sampled resolve target.
        WGPUTextureDescriptor resolveTexDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        resolveTexDesc.size.width            = 1;
        resolveTexDesc.size.height           = 1;
        resolveTexDesc.size.depthOrArrayLayers = 1;
        resolveTexDesc.format                = WGPUTextureFormat_RG11B10Ufloat;
        resolveTexDesc.sampleCount           = 1;
        resolveTexDesc.usage                 = WGPUTextureUsage_RenderAttachment;
        WGPUTexture resolveTexture = t.createTextureTracked(resolveTexDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView renderView  = t.createViewTracked(renderTexture,  viewDesc);
        WGPUTextureView resolveView = t.createViewTracked(resolveTexture, viewDesc);

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view          = renderView;
        colorAttachment.resolveTarget = resolveView;
        colorAttachment.loadOp        = WGPULoadOp_Clear;
        colorAttachment.storeOp       = WGPUStoreOp_Store;
        colorAttachment.clearValue    = WGPUColor{1.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttachment;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        t.finishTracked(encoder);
    });

CTS_TEST(g, "begin_render_bundle_encoder")
    .desc(R"(
Test that it is valid to begin render bundle encoder with rg11b10ufloat texture
format iff rg11b10ufloat-renderable feature is enabled.
)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable) == 0) {
            t.skip("rg11b10ufloat-renderable feature is not available on this device");
        }

        WGPUTextureFormat colorFormat = WGPUTextureFormat_RG11B10Ufloat;

        WGPURenderBundleEncoderDescriptor bDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        bDesc.colorFormatCount = 1;
        bDesc.colorFormats     = &colorFormat;

        WGPURenderBundleEncoder bundleEnc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDesc);
        wgpuRenderBundleEncoderRelease(bundleEnc);
    });

CTS_TEST(g, "create_render_pipeline")
    .desc(R"(
Test that it is valid to create render pipeline with rg11b10ufloat texture format
in descriptor.fragment.targets iff rg11b10ufloat-renderable feature is enabled.
)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (wgpuDeviceHasFeature(t.device(), WGPUFeatureName_RG11B10UfloatRenderable) == 0) {
            t.skip("rg11b10ufloat-renderable feature is not available on this device");
        }

        WGPUShaderModule vertModule = t.createShaderModuleTracked(kVertexShader);
        WGPUShaderModule fragModule = t.createShaderModuleTracked(kFragmentShader);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format    = WGPUTextureFormat_RG11B10Ufloat;
        colorTarget.writeMask = WGPUColorWriteMask_None;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = fragModule;
        fragment.entryPoint  = sv("main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        // layout: 'auto' — pipeDesc.layout is null after INIT
        pipeDesc.vertex.module     = vertModule;
        pipeDesc.vertex.entryPoint = sv("main");
        pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipeDesc.fragment           = &fragment;
        t.createRenderPipelineTracked(pipeDesc);
    });

} // namespace
