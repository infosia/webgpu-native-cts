// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/culling_tests.spec.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2020 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Native spelling: the `null` value of the `depthStencilFormat` subcase param is `_undef_`.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_test_utils.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,render_pipeline,culling_tests",
    R"(Test culling and rasterization state.

Test coverage:
Test all culling combinations of GPUFrontFace and GPUCullMode show the correct output.
)");

constexpr uint32_t kSize = 4;
constexpr WGPUTextureFormat kFormat = WGPUTextureFormat_RGBA8Unorm;

using Bytes4 = std::array<uint8_t, 4>;

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

bool faceIsCulled(std::string_view face, std::string_view frontFace, std::string_view cullMode) {
    return cullMode != "none" && (frontFace == face) == (cullMode == "front");
}

Bytes4 faceColor(std::string_view face, std::string_view frontFace, std::string_view cullMode) {
    // front facing color is green, non front facing is red, background is blue
    const bool isCulled = faceIsCulled(face, frontFace, cullMode);
    if (!isCulled && face == frontFace) {
        return Bytes4{0x00, 0xff, 0x00, 0xff};
    } else if (isCulled) {
        return Bytes4{0x00, 0x00, 0xff, 0xff};
    }
    return Bytes4{0xff, 0x00, 0x00, 0xff};
}

WGPUFrontFace parseFrontFace(std::string_view s) {
    if (s == "ccw") {
        return WGPUFrontFace_CCW;
    }
    if (s == "cw") {
        return WGPUFrontFace_CW;
    }
    std::abort();
}

WGPUCullMode parseCullMode(std::string_view s) {
    if (s == "none") {
        return WGPUCullMode_None;
    }
    if (s == "front") {
        return WGPUCullMode_Front;
    }
    if (s == "back") {
        return WGPUCullMode_Back;
    }
    std::abort();
}

WGPUPrimitiveTopology parseTopology(std::string_view s) {
    if (s == "triangle-list") {
        return WGPUPrimitiveTopology_TriangleList;
    }
    if (s == "triangle-strip") {
        return WGPUPrimitiveTopology_TriangleStrip;
    }
    std::abort();
}

void submit(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

// CullingTest.checkCornerPixels.
void checkCornerPixels(AllFeaturesMaxLimitsGpuTest& t,
                       WGPUTexture texture,
                       const Bytes4& expectedTopLeftColor,
                       const Bytes4& expectedBottomRightColor) {
    expectSinglePixelComparisonsAreOkInTexture(
        t, texture, kFormat, 0,
        {
            PixelComparison{WGPUOrigin3D{0, 0, 0},
                            texelBytesToColor(kFormat, {expectedTopLeftColor.begin(), expectedTopLeftColor.end()})},
            PixelComparison{WGPUOrigin3D{kSize - 1, kSize - 1, 0},
                            texelBytesToColor(kFormat,
                                              {expectedBottomRightColor.begin(), expectedBottomRightColor.end()})},
        });
}

// `vec4f(${Array.from(color).map(v => v / 255)})`: JS joins the numbers with ",".
std::string colorLiteral(const Bytes4& color) {
    std::string out;
    for (size_t i = 0; i < color.size(); ++i) {
        if (i != 0) {
            out += ",";
        }
        out += color[i] == 255 ? "1" : (color[i] == 0 ? "0" : std::to_string(color[i] / 255.0));
    }
    return out;
}

// CullingTest.drawFullClipSpaceTriangleAndCheckCornerPixels.
void drawFullClipSpaceTriangleAndCheckCornerPixels(AllFeaturesMaxLimitsGpuTest& t,
                                                   WGPUTexture texture,
                                                   WGPUPrimitiveTopology topology,
                                                   const Bytes4& color,
                                                   const WGPUDepthStencilState& depthStencil,
                                                   const WGPURenderPassDepthStencilAttachment& depthStencilAttachment,
                                                   const Bytes4& expectedTopLeftColor,
                                                   const Bytes4& expectedBottomRightColor) {
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = t.createViewTracked(texture, viewDesc);
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthStencilAttachment;

    WGPUShaderModule vertexModule = t.createShaderModuleTracked(R"(
              @vertex fn main(
                @builtin(vertex_index) VertexIndex : u32
                ) -> @builtin(position) vec4<f32> {
                  var pos : array<vec2<f32>, 3> = array<vec2<f32>, 3>(
                    vec2<f32>(-1.0, -1.0),
                    vec2<f32>( 3.0, -1.0),
                    vec2<f32>(-1.0,  3.0));
                return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
              })");
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(
        "\n              @fragment fn main() -> @location(0) vec4<f32> {\n                return vec4f("
        + colorLiteral(color) + ");\n              }");

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = kFormat;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipelineDesc.layout = nullptr; // 'auto'
    pipelineDesc.vertex.module = vertexModule;
    pipelineDesc.vertex.entryPoint = stringView("main");
    pipelineDesc.fragment = &fragment;
    pipelineDesc.primitive.topology = topology;
    pipelineDesc.depthStencil = &depthStencil;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, t.createRenderPipelineTracked(pipelineDesc));
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submit(t, encoder);

    checkCornerPixels(t, texture, expectedTopLeftColor, expectedBottomRightColor);
}

constexpr std::string_view kTriangleListPositions = R"(
                    vec2<f32>(-1.0,  1.0),
                    vec2<f32>(-1.0,  0.0),
                    vec2<f32>( 0.0,  1.0),
                    vec2<f32>( 0.0, -1.0),
                    vec2<f32>( 1.0,  0.0),
                    vec2<f32>( 1.0, -1.0));
                )";

constexpr std::string_view kTriangleStripPositions = R"(
                    vec2<f32>( 0.0,  2.0),
                    vec2<f32>(-2.0,  0.0),
                    vec2<f32>( 0.0,  0.0),
                    vec2<f32>( 0.0,  0.0),
                    vec2<f32>( 0.0, -2.0),
                    vec2<f32>( 2.0,  0.0));
                )";

constexpr std::string_view kFrontFacingFragmentShader = R"(
              @fragment fn main(
                @builtin(front_facing) FrontFacing : bool
                ) -> @location(0) vec4<f32> {
                var color : vec4<f32>;
                if (FrontFacing) {
                  color = vec4<f32>(0.0, 1.0, 0.0, 1.0);
                } else {
                  color = vec4<f32>(1.0, 0.0, 0.0, 1.0);
                }
                return color;
              })";

CTS_TEST(g, "culling")
    .desc(R"(
    Test 2 triangles with different winding orders:

    - Test that the counter-clock wise triangle has correct output for:
      - All FrontFaces (ccw, cw)
      - All CullModes (none, front, back)
      - All depth stencil attachment types (none, depth24plus, depth32float, depth24plus-stencil8)
      - Some primitive topologies (triangle-list, triangle-strip)

    - Test that the clock wise triangle has correct output for:
      - All FrontFaces (ccw, cw)
      - All CullModes (none, front, back)
      - All depth stencil attachment types (none, depth24plus, depth32float, depth24plus-stencil8)
      - Some primitive topologies (triangle-list, triangle-strip)
    )")
    .params([](ParamsBuilder u) {
        return u.combine("frontFace", {"ccw", "cw"})
            .combine("cullMode", {"none", "front", "back"})
            .beginSubcases()
            .combine("depthStencilFormat", {Value::undef(), Value("depth24plus"), Value("depth32float"),
                                            Value("depth24plus-stencil8")})
            .combine("topology", {"triangle-list", "triangle-strip"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string frontFace = t.param<std::string>("frontFace");
        const std::string cullMode = t.param<std::string>("cullMode");
        const bool hasDepthStencilFormat = !t.paramIsUndefined("depthStencilFormat");
        const WGPUTextureFormat depthStencilFormat = hasDepthStencilFormat
            ? parseTextureFormat(t.param<std::string>("depthStencilFormat"))
            : WGPUTextureFormat_Undefined;
        const std::string topologyName = t.param<std::string>("topology");
        const WGPUPrimitiveTopology topology = parseTopology(topologyName);

        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.size = WGPUExtent3D{kSize, kSize, 1};
        textureDesc.format = kFormat;
        textureDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture texture = t.createTextureTracked(textureDesc);

        const bool haveStencil = hasDepthStencilFormat && isStencilTextureFormat(depthStencilFormat);
        WGPURenderPassDepthStencilAttachment depthStencilAttachment = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        if (hasDepthStencilFormat) {
            WGPUTextureDescriptor depthDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            depthDesc.size = WGPUExtent3D{kSize, kSize, 1};
            depthDesc.format = depthStencilFormat;
            depthDesc.usage = WGPUTextureUsage_RenderAttachment;
            WGPUTextureViewDescriptor depthViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            depthStencilAttachment.view = t.createViewTracked(t.createTextureTracked(depthDesc), depthViewDesc);
            depthStencilAttachment.depthClearValue = 1.0f;
            depthStencilAttachment.depthLoadOp = WGPULoadOp_Clear;
            depthStencilAttachment.depthStoreOp = WGPUStoreOp_Store;

            applyJsStencilFaceDefaults(depthStencil);
            depthStencil.format = depthStencilFormat;
            depthStencil.depthCompare = WGPUCompareFunction_Less;
            depthStencil.depthWriteEnabled = WGPUOptionalBool_True;

            if (haveStencil) {
                depthStencilAttachment.stencilLoadOp = WGPULoadOp_Clear;
                depthStencilAttachment.stencilStoreOp = WGPUStoreOp_Store;
                depthStencil.stencilFront.passOp = WGPUStencilOperation_IncrementClamp;
                depthStencil.stencilBack.passOp = WGPUStencilOperation_IncrementClamp;
            }
        }

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = t.createViewTracked(texture, viewDesc);
        colorAttachment.clearValue = WGPUColor{0, 0, 1, 1};
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;
        passDesc.depthStencilAttachment = hasDepthStencilFormat ? &depthStencilAttachment : nullptr;

        // Draw triangles with different winding orders:
        //
        // for triangle-list, 2 triangles
        //   1. The top-left one is counterclockwise (CCW)
        //   2. The bottom-right one is clockwise (CW)
        //
        // for triangle-strip, 4 triangles
        // note: for triangle-strip the index order swaps every other triangle
        // so the order is 012, 213, 234, 435
        //
        //   1. The top left is counterclockwise (CCW)
        //   2. zero size
        //   3. zero size
        //   4. The bottom right one is clockwise (CW)
        const std::string vertexCode = std::string(R"(
              @vertex fn main(
                @builtin(vertex_index) VertexIndex : u32
                ) -> @builtin(position) vec4<f32> {
                  var pos : array<vec2<f32>, 6> = array<vec2<f32>, 6>(
                )") + std::string(topologyName == "triangle-list" ? kTriangleListPositions : kTriangleStripPositions)
            + R"(
                return vec4<f32>(pos[VertexIndex], 0.0, 1.0);
              })";
        WGPUShaderModule vertexModule = t.createShaderModuleTracked(vertexCode);
        WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFrontFacingFragmentShader);

        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        target.format = kFormat;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fragmentModule;
        fragment.entryPoint = stringView("main");
        fragment.targetCount = 1;
        fragment.targets = &target;

        WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = nullptr; // 'auto'
        pipelineDesc.vertex.module = vertexModule;
        pipelineDesc.vertex.entryPoint = stringView("main");
        pipelineDesc.fragment = &fragment;
        pipelineDesc.primitive.topology = topology;
        pipelineDesc.primitive.frontFace = parseFrontFace(frontFace);
        pipelineDesc.primitive.cullMode = parseCullMode(cullMode);
        pipelineDesc.depthStencil = hasDepthStencilFormat ? &depthStencil : nullptr;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, t.createRenderPipelineTracked(pipelineDesc));
        wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        submit(t, encoder);

        // front facing color is green, non front facing is red, background is blue
        const Bytes4 kCCWTriangleTopLeftColor = faceColor("ccw", frontFace, cullMode);
        const Bytes4 kCWTriangleBottomRightColor = faceColor("cw", frontFace, cullMode);
        checkCornerPixels(t, texture, kCCWTriangleTopLeftColor, kCWTriangleBottomRightColor);

        if (hasDepthStencilFormat) {
            // draw a triangle that covers all of clip space in yellow at the same depth as the
            // previous triangles with the depth test set to 'less'. We should only draw yellow
            // where the previous triangles did not.
            depthStencilAttachment.depthLoadOp = WGPULoadOp_Load;

            if (haveStencil) {
                depthStencilAttachment.stencilLoadOp = WGPULoadOp_Load;
                depthStencil.stencilFront.passOp = WGPUStencilOperation_Keep;
                depthStencil.stencilBack.passOp = WGPUStencilOperation_Keep;
            }

            const Bytes4 k2ndDrawColor = {255, 255, 0, 255};

            const bool isTopLeftCulled = faceIsCulled("ccw", frontFace, cullMode);
            const Bytes4 kExpectedTopLeftColor = isTopLeftCulled ? k2ndDrawColor : kCCWTriangleTopLeftColor;

            const bool isBottomRightCulled = faceIsCulled("cw", frontFace, cullMode);
            const Bytes4 kExpectedBottomRightColor =
                isBottomRightCulled ? k2ndDrawColor : kCWTriangleBottomRightColor;

            drawFullClipSpaceTriangleAndCheckCornerPixels(t, texture, topology, k2ndDrawColor, depthStencil,
                                                          depthStencilAttachment, kExpectedTopLeftColor,
                                                          kExpectedBottomRightColor);

            if (haveStencil) {
                // draw a triangle that covers all of clip space in cyan with the stencil compare
                // set to 'equal'. The reference value defaults to 0 so we should only render cyan
                // where the first two triangles did not.
                depthStencil.depthCompare = WGPUCompareFunction_Always;
                depthStencil.stencilFront.compare = WGPUCompareFunction_Equal;
                depthStencil.stencilBack.compare = WGPUCompareFunction_Equal;

                const Bytes4 k3rdDrawColor = {0, 255, 255, 255};
                const Bytes4 kExpectedTopLeftColor3 = isTopLeftCulled ? k3rdDrawColor : kCCWTriangleTopLeftColor;
                const Bytes4 kExpectedBottomRightColor3 =
                    isBottomRightCulled ? k3rdDrawColor : kCWTriangleBottomRightColor;

                drawFullClipSpaceTriangleAndCheckCornerPixels(t, texture, topology, k3rdDrawColor, depthStencil,
                                                              depthStencilAttachment, kExpectedTopLeftColor3,
                                                              kExpectedBottomRightColor3);
            }
        }
    });

} // namespace
