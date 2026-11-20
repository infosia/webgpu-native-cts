// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/texture/readonly_depth_stencil.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - The beforeAllSubcases skip (depth+stencil formats in compatibility mode) is a no-op on the
//     core devices this harness runs.
//   - The WGSL template interpolations (${!!stencilReadOnly}, ${hasDepth}, ...) are substituted
//     with the same boolean / number literals JS produces; `undefined` read-only values behave as
//     JS falsy (!!undefined === false, !undefined === true).
//   - Upstream omits the vertex entryPoint (each module has a single vertex entry point); the port
//     names it explicitly.
//   - expectSingleColor(r32uint, {R: 1}) is a copy into a fresh (zero-initialized) buffer whose
//     texels are compared to 1.

#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,memory_sync,texture,readonly_depth_stencil",
    R"(
Memory synchronization tests for depth-stencil attachments in a single pass, with checks for readonlyness.
)");

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// Row pitch of the r32uint result readback (3 texels padded to the 256-byte copy alignment).
constexpr uint32_t kResultBytesPerRow = 256;

const char* boolLiteral(bool value) {
    return value ? "true" : "false";
}

constexpr std::string_view kInitShader = R"(
            @vertex fn vs(
                @builtin(instance_index) x : u32, @builtin(vertex_index) y : u32
            ) -> @builtin(position) vec4f {
                let texcoord = (vec2f(f32(x), f32(y)) + vec2f(0.5)) / 3;
                return vec4f((texcoord * 2) - vec2f(1.0), 0, 1);
            }
            @fragment fn fs_with_depth(@builtin(position) pos : vec4f) -> @builtin(frag_depth) f32 {
                return (pos.y + 0.5) / 10;
            }
            @fragment fn fs_no_depth() {
            }
)";

// The test/check module. `depthReadOnly` / `stencilReadOnly` are std::nullopt for `undefined`.
// kFragDepth = 0.15, kStencilRef = 2.
std::string buildTestCheckShader(bool hasDepth,
                                 bool hasStencil,
                                 std::optional<bool> depthReadOnly,
                                 std::optional<bool> stencilReadOnly) {
    const bool depthRO = depthReadOnly.value_or(false);     // !!depthReadOnly
    const bool stencilRO = stencilReadOnly.value_or(false); // !!stencilReadOnly
    std::ostringstream s;
    s << R"(
          @group(0) @binding(0) var depthTex : texture_2d<f32>;
          @group(0) @binding(1) var stencilTex : texture_2d<u32>;

          @vertex fn full_quad_vs(@builtin(vertex_index) id : u32) -> @builtin(position) vec4f {
            let pos = array(vec2f(-3, -1), vec2(3, -1), vec2(0, 2));
            return vec4f(pos[id], 0.15, 1.0);
          }

          @fragment fn test_texture(@builtin(position) pos : vec4f) {
            let texel = vec2u(floor(pos.xy));
            if )" << boolLiteral(stencilRO) << R"( && textureLoad(stencilTex, texel, 0).r > 2 {
                discard;
            }
            if )" << boolLiteral(depthRO) << R"( && textureLoad(depthTex, texel, 0).r > 0.21 {
                discard;
            }
          }

          @fragment fn check_texture(@builtin(position) pos : vec4f) -> @location(0) u32 {
            let texel = vec2u(floor(pos.xy));

            // The current values in the framebuffer.
            let initStencil = texel.x + 1;
            let initDepth = f32(texel.y + 1) / 10.0;

            // Expected results of the test_texture step.
            let stencilTestPasses = !)" << boolLiteral(hasStencil) << R"( || 2 <= initStencil;
            let depthTestPasses = !)" << boolLiteral(hasDepth) << R"( || 0.15 <= initDepth;
            let fsDiscards = ()" << boolLiteral(stencilRO) << R"( && initStencil > 2) ||
                             ()" << boolLiteral(depthRO) << R"( && initDepth > 0.21);

            // Compute the values that should be in the framebuffer.
            var stencil = initStencil;
            var depth = initDepth;

            // When the fragments aren't discarded, fragment output operations happen.
            if depthTestPasses && stencilTestPasses && !fsDiscards {
                if )" << boolLiteral(!stencilRO) << R"( {
                    stencil += 1;
                }
                if )" << boolLiteral(!depthRO) << R"( {
                    depth = 0.15;
                }
            }

            if )" << boolLiteral(hasStencil) << R"( && textureLoad(stencilTex, texel, 0).r != stencil {
                return 0;
            }
            if )" << boolLiteral(hasDepth) << R"( && abs(textureLoad(depthTex, texel, 0).r - depth) > 0.01 {
                return 0;
            }
            return 1;
          }
    )";
    return s.str();
}

std::optional<bool> optionalBoolParam(const AllFeaturesMaxLimitsGpuTest& t, std::string_view key) {
    if (t.paramIsUndefined(key)) {
        return std::nullopt;
    }
    return t.param<bool>(key);
}

WGPUTexture createTexture2D(AllFeaturesMaxLimitsGpuTest& t,
                            WGPUTextureFormat format,
                            uint32_t width,
                            uint32_t height,
                            WGPUTextureUsage usage) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{width, height, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = format;
    desc.usage = usage;
    return t.createTextureTracked(desc);
}

WGPUTextureView createView(AllFeaturesMaxLimitsGpuTest& t, WGPUTexture texture, WGPUTextureAspect aspect) {
    WGPUTextureViewDescriptor desc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    desc.aspect = aspect;
    return t.createViewTracked(texture, desc);
}

WGPUBindGroup createDepthStencilBindGroup(AllFeaturesMaxLimitsGpuTest& t,
                                          WGPURenderPipeline pipeline,
                                          WGPUTextureView depthView,
                                          WGPUTextureView stencilView) {
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entries[2] = {
        WGPU_BIND_GROUP_ENTRY_INIT,
        WGPU_BIND_GROUP_ENTRY_INIT,
    };
    entries[0].binding = 0;
    entries[0].textureView = depthView;
    entries[1].binding = 1;
    entries[1].textureView = stencilView;
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.entryCount = 2;
    desc.entries = entries;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(desc);
    wgpuBindGroupLayoutRelease(layout);
    return bindGroup;
}

std::vector<Value> depthStencilFormatValues() {
    std::vector<Value> values;
    for (WGPUTextureFormat format : kDepthStencilFormats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

CTS_TEST(g, "sampling_while_testing")
    .desc(R"(Tests concurrent sampling and testing of readonly depth-stencil attachments in a render pass.
      - Test for all depth-stencil formats.
      - Test for all valid combinations of depth/stencilReadOnly.

In particular this test checks that a non-readonly aspect can be rendered to, and used for depth/stencil
testing while the other one is used for sampling.
  )")
    .params([](ParamsBuilder u) {
        return u.combine("format", depthStencilFormatValues())
            .combine("depthReadOnly", {Value(true), Value(false), Value::undef()})
            .combine("stencilReadOnly", {Value(true), Value(false), Value::undef()})
            .filter([](const ParamRecord& p) {
                const WGPUTextureFormat format = parseTextureFormat(valueAs<std::string>(*findParam(p, "format")));
                const bool depthUndefined =
                    std::holds_alternative<Value::Undefined>(findParam(p, "depthReadOnly")->data());
                const bool stencilUndefined =
                    std::holds_alternative<Value::Undefined>(findParam(p, "stencilReadOnly")->data());
                const bool depthMatch = !isDepthTextureFormat(format) == depthUndefined;
                const bool stencilMatch = !isStencilTextureFormat(format) == stencilUndefined;
                return depthMatch && stencilMatch;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::optional<bool> depthReadOnly = optionalBoolParam(t, "depthReadOnly");
        const std::optional<bool> stencilReadOnly = optionalBoolParam(t, "stencilReadOnly");
        const bool depthRO = depthReadOnly.value_or(false);
        const bool stencilRO = stencilReadOnly.value_or(false);
        const bool hasDepth = isDepthTextureFormat(format);
        const bool hasStencil = isStencilTextureFormat(format);

        t.skipIfTextureFormatNotSupported(format);

        // The 3x3 depth stencil texture used for the tests.
        WGPUTexture ds = createTexture2D(t, format, 3, 3,
                                         WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);

        // Fill the texture along the X axis with stencil values 1, 2, 3 and along the Y axis depth
        // values 0.1, 0.2, 0.3. The depth value is written using @builtin(frag_depth) while the
        // stencil is written using stencil operation and modifying the stencilReference.
        WGPUShaderModule initModule = t.createShaderModuleTracked(kInitShader);

        WGPUDepthStencilState initDepthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        initDepthStencil.format = format;
        if (hasDepth) {
            initDepthStencil.depthWriteEnabled = WGPUOptionalBool_True;
            initDepthStencil.depthCompare = WGPUCompareFunction_Always;
        }
        if (hasStencil) {
            initDepthStencil.stencilBack.compare = WGPUCompareFunction_Always;
            initDepthStencil.stencilBack.passOp = WGPUStencilOperation_Replace;
            initDepthStencil.stencilFront.compare = WGPUCompareFunction_Always;
            initDepthStencil.stencilFront.passOp = WGPUStencilOperation_Replace;
        }

        WGPUFragmentState initFragment = WGPU_FRAGMENT_STATE_INIT;
        initFragment.module = initModule;
        initFragment.entryPoint = sv(hasDepth ? "fs_with_depth" : "fs_no_depth");
        initFragment.targetCount = 0;

        WGPURenderPipelineDescriptor initPipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        initPipelineDesc.label = sv("initPipeline");
        initPipelineDesc.layout = nullptr; // auto
        initPipelineDesc.vertex.module = initModule;
        initPipelineDesc.vertex.entryPoint = sv("vs");
        initPipelineDesc.fragment = &initFragment;
        initPipelineDesc.depthStencil = &initDepthStencil;
        initPipelineDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
        WGPURenderPipeline initPipeline = t.createRenderPipelineTracked(initPipelineDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        {
            WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            attachment.view = createView(t, ds, WGPUTextureAspect_All);
            if (hasDepth) {
                attachment.depthLoadOp = WGPULoadOp_Clear;
                attachment.depthStoreOp = WGPUStoreOp_Store;
                attachment.depthClearValue = 0.0f;
            }
            if (hasStencil) {
                attachment.stencilLoadOp = WGPULoadOp_Clear;
                attachment.stencilStoreOp = WGPUStoreOp_Store;
                attachment.stencilClearValue = 0;
            }
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 0;
            passDesc.depthStencilAttachment = &attachment;
            WGPURenderPassEncoder initPass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(initPass, initPipeline);
            for (uint32_t i = 0; i < 3; ++i) {
                wgpuRenderPassEncoderSetStencilReference(initPass, i + 1);
                // Draw 3 points (Y = 0, 1, 2) at X = instance_index = i.
                wgpuRenderPassEncoderDraw(initPass, 3, 1, 0, i);
            }
            wgpuRenderPassEncoderEnd(initPass);
        }

        // Perform the actual test:
        //   - The shader outputs depth 0.15 and stencil 2 (via stencilReference).
        //   - Test that the fragdepth / stencilref must be <= to what's in the depth-stencil
        //     attachment. -> Fragments that have depth 0.1 or stencil 1 are tested out.
        //   - Test that sampling the depth / stencil (when possible) is <= 0.2 for depth, <= 2 for
        //     stencil. -> Fragments that have depth 0.3 or stencil 3 are discarded if that aspect
        //     is readonly.
        //   - Write the depth / increment the stencil if the aspect is not readonly.
        //     -> After the test, fragments that passed will have non-readonly aspects updated.
        constexpr uint32_t kStencilRef = 2;
        WGPUShaderModule testAndCheckModule =
            t.createShaderModuleTracked(buildTestCheckShader(hasDepth, hasStencil, depthReadOnly, stencilReadOnly));

        WGPUDepthStencilState testDepthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        testDepthStencil.format = format;
        if (hasDepth) {
            testDepthStencil.depthCompare = WGPUCompareFunction_LessEqual;
            testDepthStencil.depthWriteEnabled = depthRO ? WGPUOptionalBool_False : WGPUOptionalBool_True;
        }
        if (hasStencil) {
            const WGPUStencilOperation passOp =
                stencilRO ? WGPUStencilOperation_Keep : WGPUStencilOperation_IncrementClamp;
            testDepthStencil.stencilBack.compare = WGPUCompareFunction_LessEqual;
            testDepthStencil.stencilBack.passOp = passOp;
            testDepthStencil.stencilFront.compare = WGPUCompareFunction_LessEqual;
            testDepthStencil.stencilFront.passOp = passOp;
        }

        WGPUFragmentState testFragment = WGPU_FRAGMENT_STATE_INIT;
        testFragment.module = testAndCheckModule;
        testFragment.entryPoint = sv("test_texture");
        testFragment.targetCount = 0;

        WGPURenderPipelineDescriptor testPipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        testPipelineDesc.label = sv("testPipeline");
        testPipelineDesc.layout = nullptr; // auto
        testPipelineDesc.vertex.module = testAndCheckModule;
        testPipelineDesc.vertex.entryPoint = sv("full_quad_vs");
        testPipelineDesc.fragment = &testFragment;
        testPipelineDesc.depthStencil = &testDepthStencil;
        testPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        WGPURenderPipeline testPipeline = t.createRenderPipelineTracked(testPipelineDesc);

        // Make fake stencil or depth textures to put in the bindgroup if the aspect is not readonly.
        WGPUTexture fakeStencil = createTexture2D(t, WGPUTextureFormat_R32Uint, 1, 1, WGPUTextureUsage_TextureBinding);
        WGPUTexture fakeDepth = createTexture2D(t, WGPUTextureFormat_R32Float, 1, 1, WGPUTextureUsage_TextureBinding);
        WGPUTextureView stencilView = stencilRO ? createView(t, ds, WGPUTextureAspect_StencilOnly)
                                                : createView(t, fakeStencil, WGPUTextureAspect_All);
        WGPUTextureView depthView = depthRO ? createView(t, ds, WGPUTextureAspect_DepthOnly)
                                            : createView(t, fakeDepth, WGPUTextureAspect_All);
        WGPUBindGroup testBindGroup = createDepthStencilBindGroup(t, testPipeline, depthView, stencilView);

        // Run the test.
        {
            WGPURenderPassDepthStencilAttachment attachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
            attachment.view = createView(t, ds, WGPUTextureAspect_All);
            if (hasDepth) {
                if (depthRO) {
                    attachment.depthReadOnly = WGPU_TRUE;
                } else {
                    attachment.depthLoadOp = WGPULoadOp_Load;
                    attachment.depthStoreOp = WGPUStoreOp_Store;
                }
            }
            if (hasStencil) {
                if (stencilRO) {
                    attachment.stencilReadOnly = WGPU_TRUE;
                } else {
                    attachment.stencilLoadOp = WGPULoadOp_Load;
                    attachment.stencilStoreOp = WGPUStoreOp_Store;
                }
            }
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 0;
            passDesc.depthStencilAttachment = &attachment;
            WGPURenderPassEncoder testPass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(testPass, testPipeline);
            wgpuRenderPassEncoderSetStencilReference(testPass, kStencilRef);
            wgpuRenderPassEncoderSetBindGroup(testPass, 0, testBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(testPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(testPass);
        }

        // Check that the contents of the textures are what we expect. See the shader module for
        // the computation of what's expected, it writes a 1 on success, 0 otherwise.
        WGPUColorTargetState checkTarget = WGPU_COLOR_TARGET_STATE_INIT;
        checkTarget.format = WGPUTextureFormat_R32Uint;

        WGPUFragmentState checkFragment = WGPU_FRAGMENT_STATE_INIT;
        checkFragment.module = testAndCheckModule;
        checkFragment.entryPoint = sv("check_texture");
        checkFragment.targetCount = 1;
        checkFragment.targets = &checkTarget;

        WGPURenderPipelineDescriptor checkPipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        checkPipelineDesc.label = sv("checkPipeline");
        checkPipelineDesc.layout = nullptr; // auto
        checkPipelineDesc.vertex.module = testAndCheckModule;
        checkPipelineDesc.vertex.entryPoint = sv("full_quad_vs");
        checkPipelineDesc.fragment = &checkFragment;
        checkPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        WGPURenderPipeline checkPipeline = t.createRenderPipelineTracked(checkPipelineDesc);

        WGPUBindGroup checkBindGroup = createDepthStencilBindGroup(
            t, checkPipeline,
            hasDepth ? createView(t, ds, WGPUTextureAspect_DepthOnly) : createView(t, fakeDepth, WGPUTextureAspect_All),
            hasStencil ? createView(t, ds, WGPUTextureAspect_StencilOnly)
                       : createView(t, fakeStencil, WGPUTextureAspect_All));

        WGPUTexture resultTexture = createTexture2D(t, WGPUTextureFormat_R32Uint, 3, 3,
                                                    WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc);
        {
            WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            attachment.view = createView(t, resultTexture, WGPUTextureAspect_All);
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
            attachment.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &attachment;
            WGPURenderPassEncoder checkPass = t.beginRenderPassTracked(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(checkPass, checkPipeline);
            wgpuRenderPassEncoderSetBindGroup(checkPass, 0, checkBindGroup, 0, nullptr);
            wgpuRenderPassEncoderDraw(checkPass, 3, 1, 0, 0);
            wgpuRenderPassEncoderEnd(checkPass);
        }

        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        // The check texture should be full of success (a.k.a. 1)!
        // expectSingleColor(resultTexture, 'r32uint', {size: [3, 3, 1], exp: {R: 1}}).
        constexpr uint64_t kReadbackSize = uint64_t{kResultBytesPerRow} * 2 + 3 * 4;
        WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readbackDesc.size = kReadbackSize;
        readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer readback = t.createBufferTracked(readbackDesc);

        WGPUCommandEncoder copyEncoder = t.createCommandEncoderTracked();
        t.copyTextureToBuffer(copyEncoder, resultTexture, readback, kResultBytesPerRow, WGPUExtent3D{3, 3, 1});
        WGPUCommandBuffer copyCommandBuffer = t.finishTracked(copyEncoder);
        wgpuQueueSubmit(t.queue(), 1, &copyCommandBuffer);

        t.expectGPUBufferValuesPassCheck(
            readback,
            [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                for (uint32_t y = 0; y < 3; ++y) {
                    for (uint32_t x = 0; x < 3; ++x) {
                        const size_t offset = static_cast<size_t>(y) * kResultBytesPerRow + static_cast<size_t>(x) * 4;
                        if (offset + 4 > len) {
                            return std::string("readback buffer too small");
                        }
                        uint32_t value = 0;
                        std::memcpy(&value, actual + offset, sizeof(value));
                        if (value != 1u) {
                            std::ostringstream message;
                            message << "resultTexture texel (" << x << "," << y << ") expected 1, got " << value;
                            return message.str();
                        }
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kReadbackSize));
    });

} // namespace
