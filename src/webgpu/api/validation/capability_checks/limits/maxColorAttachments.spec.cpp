// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxColorAttachments.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxColorAttachmentsTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxColorAttachments"; }
};

TestGroup<MaxColorAttachmentsTest> testGroup = MakeTestGroup<MaxColorAttachmentsTest>(
    "api,validation,capability_checks,limits,maxColorAttachments",
    "API Validation Tests for maxColorAttachments.");

WGPURenderPipelineDescriptor pipelineDescriptor(
    MaxColorAttachmentsTest& t,
    uint64_t testValue,
    std::vector<WGPUColorTargetState>& targets,
    WGPUFragmentState& fragment) {
    WGPUShaderModule module = t.createShaderModuleTracked(
        "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0); }\n"
        "@fragment fn fs() -> @location(0) vec4f { return vec4f(0); }\n");
    targets.resize(static_cast<size_t>(testValue));
    for (WGPUColorTargetState& target : targets) {
        target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = WGPUTextureFormat_R8Unorm;
        target.writeMask = WGPUColorWriteMask_None;
    }
    fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("fs");
    fragment.targetCount = targets.size();
    fragment.targets = targets.data();
    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.vertex.module = module;
    desc.vertex.entryPoint = sv("vs");
    desc.fragment = &fragment;
    return desc;
}

CTS_TEST(testGroup, "createRenderPipeline,at_over")
    .desc("Test using at and over maxColorAttachments limit in createRenderPipeline(Async)")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u).combine("async", {false, true}); })
    .fn([](MaxColorAttachmentsTest& t) {
        const bool async = t.param<bool>("async");
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                std::vector<WGPUColorTargetState> targets;
                WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                WGPURenderPipelineDescriptor desc = pipelineDescriptor(t, inputs.testValue, targets, fragment);
                t.testCreateRenderPipeline(desc, async, inputs.shouldError);
            });
    });

CTS_TEST(testGroup, "beginRenderPass,at_over")
    .desc("Test using at and over maxColorAttachments limit in beginRenderPass")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxColorAttachmentsTest& t) {
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                std::vector<WGPUTextureView> views;
                std::vector<WGPURenderPassColorAttachment> attachments;
                views.reserve(static_cast<size_t>(inputs.testValue));
                attachments.reserve(static_cast<size_t>(inputs.testValue));
                for (uint64_t i = 0; i < inputs.testValue; ++i) {
                    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
                    texDesc.size = {1, 1, 1};
                    texDesc.format = WGPUTextureFormat_R8Unorm;
                    texDesc.usage = WGPUTextureUsage_RenderAttachment;
                    WGPUTexture texture = t.createTextureTracked(texDesc);
                    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                    views.push_back(t.createViewTracked(texture, viewDesc));
                    WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                    attachment.view = views.back();
                    attachment.loadOp = WGPULoadOp_Clear;
                    attachment.storeOp = WGPUStoreOp_Store;
                    attachments.push_back(attachment);
                }
                WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
                WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                passDesc.colorAttachmentCount = attachments.size();
                passDesc.colorAttachments = attachments.data();
                WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
                if (pass != nullptr) {
                    wgpuRenderPassEncoderEnd(pass);
                    wgpuRenderPassEncoderRelease(pass);
                }
                t.expectValidationErrorOnLimitDevice([&] { t.finishTracked(encoder); }, inputs.shouldError);
            });
    });

CTS_TEST(testGroup, "createRenderBundle,at_over")
    .desc("Test using at and over maxColorAttachments limit in createRenderBundle")
    .params([](ParamsBuilder u) { return kMaximumLimitBaseParams(u); })
    .fn([](MaxColorAttachmentsTest& t) {
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                std::vector<WGPUTextureFormat> formats(static_cast<size_t>(inputs.testValue), WGPUTextureFormat_R8Unorm);
                WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                desc.colorFormatCount = formats.size();
                desc.colorFormats = formats.data();
                t.expectValidationErrorOnLimitDevice([&] {
                    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
                    if (encoder != nullptr) wgpuRenderBundleEncoderRelease(encoder);
                }, inputs.shouldError);
            });
    });

CTS_TEST(testGroup, "validate,maxColorAttachmentBytesPerSample")
    .desc("Test maxColorAttachments against maxColorAttachmentBytesPerSample")
    .fn([](MaxColorAttachmentsTest& t) {
        t.expect(t.defaultLimit <= t.getDefaultLimit("maxColorAttachmentBytesPerSample"));
        t.expect(t.adapterLimit <= t.getAdapterLimit("maxColorAttachmentBytesPerSample"));
    });

CTS_TEST(testGroup, "validate,kMaxColorAttachmentsToTest")
    .desc("Tests that kMaxColorAttachmentsToTest is large enough to test the limits of this device")
    .fn([](MaxColorAttachmentsTest& t) {
        constexpr uint64_t kMaxColorAttachmentsToTest = 32;
        t.expect(t.adapterLimit <= kMaxColorAttachmentsToTest);
    });

} // namespace
