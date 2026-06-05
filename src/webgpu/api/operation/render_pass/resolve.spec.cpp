// Ported from gpuweb/cts src/webgpu/api/operation/render_pass/resolve.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pass,resolve",
    "Render pass resolve operation tests.");

constexpr uint32_t kSize = 4;
constexpr uint32_t kBytesPerPixel = 4;
constexpr uint32_t kSampleCount = 4;
constexpr uint32_t kNumColorAttachments = 2;

struct ResolveResources {
    std::array<WGPUTexture, kNumColorAttachments> colorTextures = {};
    std::array<WGPUTextureView, kNumColorAttachments> colorViews = {};
    std::array<WGPUTexture, kNumColorAttachments> resolveTextures = {};
    std::array<WGPUTextureView, kNumColorAttachments> resolveViews = {};
    std::array<bool, kNumColorAttachments> hasResolveTarget = {};
};

struct PixelExpectation {
    uint32_t x = 0;
    uint32_t y = 0;
    std::array<double, 4> expected = {};
    double maxDiff = 0.0;
};

constexpr std::string_view kVertexShader = R"(
@vertex fn main(@builtin(vertex_index) vertexIndex : u32) -> @builtin(position) vec4f {
  let pos = array(
    vec2f(-1, -1),
    vec2f(-1, 1),
    vec2f(1, 1)
  );
  return vec4f(pos[vertexIndex], 0, 1);
}
)";

constexpr std::string_view kFragmentShader = R"(
struct FragmentOut {
  @location(0) color0 : vec4f,
  @location(1) color1 : vec4f,
};

@fragment fn main() -> FragmentOut {
  return FragmentOut(vec4f(1, 1, 1, 1), vec4f(1, 1, 1, 1));
}
)";

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

std::vector<Value> slotsToResolveValues() {
    return {
        Value("0,2"),
        Value("1,3"),
        Value("0,1,2,3"),
    };
}

bool slotShouldResolve(std::string_view slotsToResolve, uint32_t slot) {
    if (slotsToResolve == "0,2") {
        return slot == 0;
    }
    if (slotsToResolve == "1,3") {
        return slot == 1;
    }
    if (slotsToResolve == "0,1,2,3") {
        return true;
    }
    std::abort();
}

WGPUStoreOp parseStoreOp(std::string_view value) {
    if (value == "store") {
        return WGPUStoreOp_Store;
    }
    if (value == "discard") {
        return WGPUStoreOp_Discard;
    }
    std::abort();
}

WGPUBuffer createBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    return t.createBufferTracked(desc);
}

WGPUTexture createColorTexture(AllFeaturesMaxLimitsGpuTest& t, uint32_t sampleCount, WGPUTextureUsage usage) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kSize, kSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = sampleCount;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = WGPUTextureFormat_RGBA8Unorm;
    desc.usage = usage;
    return t.createTextureTracked(desc);
}

ResolveResources createResolveResources(AllFeaturesMaxLimitsGpuTest& t, std::string_view slotsToResolve) {
    ResolveResources resources;
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    for (uint32_t i = 0; i < kNumColorAttachments; ++i) {
        resources.colorTextures[i] = createColorTexture(t, kSampleCount, WGPUTextureUsage_RenderAttachment);
        resources.colorViews[i] = t.createViewTracked(resources.colorTextures[i], viewDesc);
        resources.hasResolveTarget[i] = slotShouldResolve(slotsToResolve, i);
        if (resources.hasResolveTarget[i]) {
            resources.resolveTextures[i] = createColorTexture(
                t,
                1,
                WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment);
            resources.resolveViews[i] = t.createViewTracked(resources.resolveTextures[i], viewDesc);
        }
    }
    return resources;
}

WGPURenderPipeline createResolvePipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);

    std::array<WGPUColorTargetState, kNumColorAttachments> targets = {{
        WGPU_COLOR_TARGET_STATE_INIT,
        WGPU_COLOR_TARGET_STATE_INIT,
    }};
    for (WGPUColorTargetState& target : targets) {
        target.format = WGPUTextureFormat_RGBA8Unorm;
    }

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = targets.size();
    fragment.targets = targets.data();

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    desc.multisample.count = kSampleCount;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

std::array<WGPURenderPassColorAttachment, kNumColorAttachments> colorAttachments(
    const ResolveResources& resources,
    WGPULoadOp loadOp,
    WGPUStoreOp storeOp,
    bool includeResolveTargets) {
    std::array<WGPURenderPassColorAttachment, kNumColorAttachments> attachments = {{
        WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT,
        WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT,
    }};
    for (uint32_t i = 0; i < kNumColorAttachments; ++i) {
        attachments[i].view = resources.colorViews[i];
        attachments[i].loadOp = loadOp;
        attachments[i].storeOp = storeOp;
        attachments[i].clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        if (includeResolveTargets && resources.hasResolveTarget[i]) {
            attachments[i].resolveTarget = resources.resolveViews[i];
        }
    }
    return attachments;
}

void encodePass(
    WGPUCommandEncoder encoder,
    const ResolveResources& resources,
    WGPURenderPipeline pipeline,
    WGPULoadOp loadOp,
    WGPUStoreOp storeOp,
    bool includeResolveTargets,
    bool draw) {
    auto attachments = colorAttachments(resources, loadOp, storeOp, includeResolveTargets);
    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = attachments.size();
    passDesc.colorAttachments = attachments.data();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    if (draw) {
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    }
    wgpuRenderPassEncoderEnd(pass);
}

void expectPixelsInTexture(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const std::vector<PixelExpectation>& expectations) {
    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(kSize * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (kSize - 1) + static_cast<uint64_t>(kSize) * kBytesPerPixel,
        kBufferCopyAlignment);
    WGPUBuffer buffer = createBuffer(t, byteLength, WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(encoder, texture, buffer, bytesPerRow, WGPUExtent3D{kSize, kSize, 1});
    submit(t, encoder);

    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (const PixelExpectation& expectation : expectations) {
                const uint64_t offset = static_cast<uint64_t>(expectation.y) * bytesPerRow
                    + static_cast<uint64_t>(expectation.x) * kBytesPerPixel;
                if (offset + kBytesPerPixel > len) {
                    std::ostringstream message;
                    message << "rgba8unorm pixel offset out of range: " << offset;
                    return message.str();
                }
                for (uint32_t channel = 0; channel < kBytesPerPixel; ++channel) {
                    const double decoded = static_cast<double>(actual[offset + channel]) / 255.0;
                    if (std::abs(decoded - expectation.expected[channel]) > expectation.maxDiff) {
                        std::ostringstream message;
                        message << "rgba8unorm mismatch at (" << expectation.x << ", " << expectation.y
                                << ") channel " << channel
                                << ": expected " << expectation.expected[channel]
                                << ", got " << decoded;
                        return message.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

void verifyResolveTargets(AllFeaturesMaxLimitsGpuTest& t, const ResolveResources& resources) {
    const std::vector<PixelExpectation> expectations = {
        PixelExpectation{0, 0, {1.0, 1.0, 1.0, 1.0}, 0.0},
        PixelExpectation{3, 3, {0.0, 0.0, 0.0, 0.0}, 0.0},
        PixelExpectation{3, 0, {0.5, 0.5, 0.5, 0.5}, 2.0 / 255.0},
    };
    for (uint32_t i = 0; i < kNumColorAttachments; ++i) {
        if (resources.hasResolveTarget[i]) {
            expectPixelsInTexture(t, resources.resolveTextures[i], expectations);
        }
    }
}

CTS_TEST(g, "render_pass_resolve")
    .params([](ParamsBuilder u) {
        return u.combine("colorFormat", {Value("rgba8unorm")})
            .beginSubcases()
            .combine("separateResolvePass", {false, true})
            .combine("storeOperation", {Value("store"), Value("discard")})
            .combine("slotsToResolve", slotsToResolveValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        t.expect(t.param<std::string>("colorFormat") == "rgba8unorm", "T36 ports rgba8unorm only");
        const bool separateResolvePass = t.param<bool>("separateResolvePass");
        const WGPUStoreOp storeOp = parseStoreOp(t.param<std::string>("storeOperation"));
        const std::string slotsToResolve = t.param<std::string>("slotsToResolve");

        ResolveResources resources = createResolveResources(t, slotsToResolve);
        WGPURenderPipeline pipeline = createResolvePipeline(t);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        if (separateResolvePass) {
            encodePass(encoder, resources, pipeline, WGPULoadOp_Clear, WGPUStoreOp_Store, false, true);
            encodePass(encoder, resources, pipeline, WGPULoadOp_Load, storeOp, true, false);
        } else {
            encodePass(encoder, resources, pipeline, WGPULoadOp_Clear, storeOp, true, true);
        }
        submit(t, encoder);

        verifyResolveTargets(t, resources);
    });

} // namespace
