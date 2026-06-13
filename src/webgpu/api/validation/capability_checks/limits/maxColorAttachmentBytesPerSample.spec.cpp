// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/maxColorAttachmentBytesPerSample.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#include <array>
#include <sstream>
#include <string>
#include <vector>

#include "cts/test.h"
#include "limit_utils.h"
#include "webgpu/texture_format.h"

using namespace cts;
using namespace cts::capability_limits;

namespace {

class MaxColorAttachmentBytesPerSampleTest : public LimitTest {
  public:
    const char* limitName() const override { return "maxColorAttachmentBytesPerSample"; }
};

TestGroup<MaxColorAttachmentBytesPerSampleTest> testGroup = MakeTestGroup<MaxColorAttachmentBytesPerSampleTest>(
    "api,validation,capability_checks,limits,maxColorAttachmentBytesPerSample",
    "API Validation Tests for maxColorAttachmentBytesPerSample.");

constexpr std::array<WGPUTextureFormat, 5> kFormatsToUseBySize = {
    WGPUTextureFormat_RGBA32Uint,
    WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RG8Unorm,
    WGPUTextureFormat_R8Unorm,
};

constexpr std::array<WGPUTextureFormat, 5> kInterleaveFormats = {
    WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RG16Uint,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RG8Unorm,
    WGPUTextureFormat_R8Unorm,
};

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

std::vector<Value> interleaveFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kInterleaveFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

std::vector<Value> sampleCountValues() {
    return {uint64_t(1), uint64_t(4)};
}

bool isUintFormat(WGPUTextureFormat format) {
    return format == WGPUTextureFormat_RGBA16Uint || format == WGPUTextureFormat_RG16Uint ||
        format == WGPUTextureFormat_RGBA32Uint;
}

std::vector<WGPUColorTargetState> getAttachments(WGPUTextureFormat interleaveFormat, uint64_t testValue) {
    uint64_t bytesPerSample = 0;
    std::vector<WGPUColorTargetState> targets;
    const auto addTexture = [&](WGPUTextureFormat format) {
        const uint64_t next = alignTo(bytesPerSample, getColorRenderAlignment(format)) + getColorRenderByteCost(format);
        if (next > testValue) return false;
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = format;
        target.writeMask = WGPUColorWriteMask_None;
        targets.push_back(target);
        bytesPerSample = next;
        return true;
    };
    while (bytesPerSample < testValue) {
        (void)addTexture(interleaveFormat);
        for (WGPUTextureFormat format : kFormatsToUseBySize) {
            if (addTexture(format)) break;
        }
    }
    return targets;
}

std::string shaderForFormat(WGPUTextureFormat format) {
    const char* typedVec = isUintFormat(format) ? "vec4u" : "vec4f";
    std::ostringstream code;
    code << "@vertex fn vs() -> @builtin(position) vec4f { return vec4f(0); }\n"
         << "@fragment fn fs() -> @location(0) " << typedVec << " { return " << typedVec << "(0); }\n";
    return code.str();
}

CTS_TEST(testGroup, "createRenderPipeline,at_over")
    .desc("Test using at and over maxColorAttachmentBytesPerSample limit in createRenderPipeline(Async)")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u)
            .combine("async", {false, true})
            .combine("sampleCount", sampleCountValues())
            .combine("interleaveFormat", interleaveFormatValues());
    })
    .fn([](MaxColorAttachmentBytesPerSampleTest& t) {
        const bool async = t.param<bool>("async");
        const auto sampleCount = static_cast<uint32_t>(t.param<uint64_t>("sampleCount"));
        const WGPUTextureFormat interleaveFormat = parseTextureFormat(t.param<std::string>("interleaveFormat"));
        std::vector<LimitRequestEntry> extraLimits = {adapterLimitRequest("maxColorAttachments")};
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                std::vector<WGPUColorTargetState> targets = getAttachments(interleaveFormat, inputs.testValue);
                WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
                WGPULimits limits = queryLimits(inputs.device, &compat);
                if (targets.size() > limits.maxColorAttachments) return;
                std::string code = shaderForFormat(interleaveFormat);
                WGPUShaderModule module = t.createShaderModuleTracked(code);
                WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
                fragment.module = module;
                fragment.entryPoint = sv("fs");
                fragment.targetCount = targets.size();
                fragment.targets = targets.data();
                WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
                depth.depthWriteEnabled = WGPUOptionalBool_True;
                depth.depthCompare = WGPUCompareFunction_Less;
                depth.format = WGPUTextureFormat_Depth24Plus;
                WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
                desc.layout = nullptr;
                desc.vertex.module = module;
                desc.vertex.entryPoint = sv("vs");
                desc.fragment = &fragment;
                desc.depthStencil = &depth;
                desc.multisample.count = sampleCount;
                t.testCreateRenderPipeline(desc, async, inputs.shouldError, code);
            }, extraLimits);
    });

CTS_TEST(testGroup, "beginRenderPass,at_over")
    .desc("Test using at and over maxColorAttachmentBytesPerSample limit in beginRenderPass")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u)
            .combine("sampleCount", sampleCountValues())
            .combine("interleaveFormat", interleaveFormatValues());
    })
    .fn([](MaxColorAttachmentBytesPerSampleTest& t) {
        const WGPUTextureFormat interleaveFormat = parseTextureFormat(t.param<std::string>("interleaveFormat"));
        std::vector<LimitRequestEntry> extraLimits = {adapterLimitRequest("maxColorAttachments")};
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                std::vector<WGPUColorTargetState> targets = getAttachments(interleaveFormat, inputs.testValue);
                WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
                WGPULimits limits = queryLimits(inputs.device, &compat);
                if (targets.size() > limits.maxColorAttachments) return;
                std::vector<WGPUTextureView> views;
                std::vector<WGPURenderPassColorAttachment> attachments;
                for (const WGPUColorTargetState& target : targets) {
                    WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
                    td.size = {1, 1, 1};
                    td.format = target.format;
                    td.usage = WGPUTextureUsage_RenderAttachment;
                    WGPUTexture texture = t.createTextureTracked(td);
                    WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
                    views.push_back(t.createViewTracked(texture, vd));
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
            }, extraLimits);
    });

CTS_TEST(testGroup, "createRenderBundle,at_over")
    .desc("Test using at and over maxColorAttachmentBytesPerSample limit in createRenderBundle")
    .params([](ParamsBuilder u) {
        return kMaximumLimitBaseParams(u)
            .combine("sampleCount", sampleCountValues())
            .combine("interleaveFormat", interleaveFormatValues());
    })
    .fn([](MaxColorAttachmentBytesPerSampleTest& t) {
        const WGPUTextureFormat interleaveFormat = parseTextureFormat(t.param<std::string>("interleaveFormat"));
        std::vector<LimitRequestEntry> extraLimits = {adapterLimitRequest("maxColorAttachments")};
        t.testDeviceWithRequestedMaximumLimits(t.param<std::string>("limitTest"), t.param<std::string>("testValueName"),
            [&](const MaximumLimitTestInputs& inputs) {
                std::vector<WGPUColorTargetState> targets = getAttachments(interleaveFormat, inputs.testValue);
                WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
                WGPULimits limits = queryLimits(inputs.device, &compat);
                if (targets.size() > limits.maxColorAttachments) return;
                std::vector<WGPUTextureFormat> formats;
                for (const WGPUColorTargetState& target : targets) formats.push_back(target.format);
                WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                desc.colorFormatCount = formats.size();
                desc.colorFormats = formats.data();
                t.expectValidationErrorOnLimitDevice([&] {
                    WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
                    if (encoder != nullptr) wgpuRenderBundleEncoderRelease(encoder);
                }, inputs.shouldError);
            }, extraLimits);
    });

} // namespace
