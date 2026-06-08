// Ported from gpuweb/cts src/webgpu/api/operation/memory_sync/texture/readonly_depth_stencil.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Minimal port: sampling_while_testing with format=depth24plus-stencil8, depthReadOnly=true,
// stencilReadOnly=true (1 case).
// Deferred: other (depthReadOnly, stencilReadOnly) combos incl. mixed read-only/written cases
//   (fakeDepth/fakeStencil bind textures, increment-stencil / write-depth paths), and the
//   kDepthStencilFormats matrix (depth-only / stencil-only formats) (V32a-rest).

#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,memory_sync,texture,readonly_depth_stencil",
    "Memory synchronization tests for depth-stencil attachments in a single pass, with checks "
    "for readonlyness.");

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// Init module WGSL (verbatim from upstream sampling_while_testing).
// Encodes a 3x3 depth24plus-stencil8 texture:
//   stencil(x,y) = x+1 (via instance_index + stencil reference)
//   depth(x,y)   = (y+1)/10  (via frag_depth)
// ---------------------------------------------------------------------------
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
)";

// ---------------------------------------------------------------------------
// Test/check module WGSL (verbatim from upstream, with template literals
// pre-substituted for depthReadOnly=true, stencilReadOnly=true,
// hasDepth=true, hasStencil=true, kFragDepth=0.15, kStencilRef=2).
// ---------------------------------------------------------------------------
constexpr std::string_view kTestCheckShader = R"(
@group(0) @binding(0) var depthTex : texture_2d<f32>;
@group(0) @binding(1) var stencilTex : texture_2d<u32>;
@vertex fn full_quad_vs(@builtin(vertex_index) id : u32) -> @builtin(position) vec4f {
  let pos = array(vec2f(-3, -1), vec2(3, -1), vec2(0, 2));
  return vec4f(pos[id], 0.15, 1.0);
}
@fragment fn test_texture(@builtin(position) pos : vec4f) {
  let texel = vec2u(floor(pos.xy));
  if true && textureLoad(stencilTex, texel, 0).r > 2 { discard; }
  if true && textureLoad(depthTex, texel, 0).r > 0.21 { discard; }
}
@fragment fn check_texture(@builtin(position) pos : vec4f) -> @location(0) u32 {
  let texel = vec2u(floor(pos.xy));
  let initStencil = texel.x + 1;
  let initDepth = f32(texel.y + 1) / 10.0;
  let stencilTestPasses = !true || 2 <= initStencil;
  let depthTestPasses = !true || 0.15 <= initDepth;
  let fsDiscards = (true && initStencil > 2) || (true && initDepth > 0.21);
  var stencil = initStencil;
  var depth = initDepth;
  if depthTestPasses && stencilTestPasses && !fsDiscards {
    if false { stencil += 1; }
    if false { depth = 0.15; }
  }
  if true && textureLoad(stencilTex, texel, 0).r != stencil { return 0; }
  if true && abs(textureLoad(depthTex, texel, 0).r - depth) > 0.01 { return 0; }
  return 1;
}
)";

// ---------------------------------------------------------------------------
// Test case: sampling_while_testing
//
// Fixed params: format=depth24plus-stencil8, depthReadOnly=true, stencilReadOnly=true.
// 1 case.
// ---------------------------------------------------------------------------
CTS_TEST(g, "sampling_while_testing")
    .desc(
        "Tests concurrent sampling and testing of readonly depth-stencil attachments in a "
        "render pass. Fixed slice: format=depth24plus-stencil8, depthReadOnly=true, "
        "stencilReadOnly=true.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // ----- Textures -----
        // ds: 3x3 depth24plus-stencil8, RENDER_ATTACHMENT | TEXTURE_BINDING
        WGPUTextureDescriptor dsDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        dsDesc.size          = WGPUExtent3D{3, 3, 1};
        dsDesc.mipLevelCount = 1;
        dsDesc.sampleCount   = 1;
        dsDesc.dimension     = WGPUTextureDimension_2D;
        dsDesc.format        = WGPUTextureFormat_Depth24PlusStencil8;
        dsDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        WGPUTexture ds = t.createTextureTracked(dsDesc);

        // resultTexture: 3x3 r32uint, RENDER_ATTACHMENT | COPY_SRC
        WGPUTextureDescriptor resultDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        resultDesc.size          = WGPUExtent3D{3, 3, 1};
        resultDesc.mipLevelCount = 1;
        resultDesc.sampleCount   = 1;
        resultDesc.dimension     = WGPUTextureDimension_2D;
        resultDesc.format        = WGPUTextureFormat_R32Uint;
        resultDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture resultTexture = t.createTextureTracked(resultDesc);

        // Full-texture view (used for init pass and test pass DS attachment)
        WGPUTextureViewDescriptor fullViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView dsFullView = t.createViewTracked(ds, fullViewDesc);

        // Depth-only aspect view (bound as texture_2d<f32>)
        WGPUTextureViewDescriptor depthViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        depthViewDesc.aspect = WGPUTextureAspect_DepthOnly;
        WGPUTextureView dsDepthView = t.createViewTracked(ds, depthViewDesc);

        // Stencil-only aspect view (bound as texture_2d<u32>)
        WGPUTextureViewDescriptor stencilViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        stencilViewDesc.aspect = WGPUTextureAspect_StencilOnly;
        WGPUTextureView dsStencilView = t.createViewTracked(ds, stencilViewDesc);

        // Result texture view
        WGPUTextureViewDescriptor resultViewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView resultView = t.createViewTracked(resultTexture, resultViewDesc);

        // ----- Shader modules -----
        WGPUShaderModule initModule      = t.createShaderModuleTracked(kInitShader);
        WGPUShaderModule testCheckModule = t.createShaderModuleTracked(kTestCheckShader);

        // ----- initPipeline -----
        // layout auto, vertex=vs, fragment=fs_with_depth (targets []),
        // depthStencil {format, depthWriteEnabled true, depthCompare always,
        //               stencilFront/Back {compare always, passOp replace}},
        // primitive point-list.
        WGPUDepthStencilState initDS = WGPU_DEPTH_STENCIL_STATE_INIT;
        initDS.format                   = WGPUTextureFormat_Depth24PlusStencil8;
        initDS.depthWriteEnabled        = WGPUOptionalBool_True;
        initDS.depthCompare             = WGPUCompareFunction_Always;
        initDS.stencilFront.compare     = WGPUCompareFunction_Always;
        initDS.stencilFront.passOp      = WGPUStencilOperation_Replace;
        initDS.stencilBack.compare      = WGPUCompareFunction_Always;
        initDS.stencilBack.passOp       = WGPUStencilOperation_Replace;

        WGPUFragmentState initFrag = WGPU_FRAGMENT_STATE_INIT;
        initFrag.module      = initModule;
        initFrag.entryPoint  = sv("fs_with_depth");
        initFrag.targetCount = 0;
        initFrag.targets     = nullptr;

        WGPURenderPipelineDescriptor initPipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        initPipeDesc.layout                = nullptr; // auto
        initPipeDesc.vertex.module         = initModule;
        initPipeDesc.vertex.entryPoint     = sv("vs");
        initPipeDesc.fragment              = &initFrag;
        initPipeDesc.depthStencil          = &initDS;
        initPipeDesc.primitive.topology    = WGPUPrimitiveTopology_PointList;
        initPipeDesc.multisample.count     = 1;
        WGPURenderPipeline initPipeline = t.createRenderPipelineTracked(initPipeDesc);

        // ----- testPipeline -----
        // layout auto, vertex=full_quad_vs, fragment=test_texture (targets []),
        // depthStencil {format, depthCompare less-equal, depthWriteEnabled false,
        //               stencilFront/Back {compare less-equal, passOp keep}},
        // primitive triangle-list.
        WGPUDepthStencilState testDS = WGPU_DEPTH_STENCIL_STATE_INIT;
        testDS.format                   = WGPUTextureFormat_Depth24PlusStencil8;
        testDS.depthWriteEnabled        = WGPUOptionalBool_False;
        testDS.depthCompare             = WGPUCompareFunction_LessEqual;
        testDS.stencilFront.compare     = WGPUCompareFunction_LessEqual;
        testDS.stencilFront.passOp      = WGPUStencilOperation_Keep;
        testDS.stencilBack.compare      = WGPUCompareFunction_LessEqual;
        testDS.stencilBack.passOp       = WGPUStencilOperation_Keep;

        WGPUFragmentState testFrag = WGPU_FRAGMENT_STATE_INIT;
        testFrag.module      = testCheckModule;
        testFrag.entryPoint  = sv("test_texture");
        testFrag.targetCount = 0;
        testFrag.targets     = nullptr;

        WGPURenderPipelineDescriptor testPipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        testPipeDesc.layout                = nullptr; // auto
        testPipeDesc.vertex.module         = testCheckModule;
        testPipeDesc.vertex.entryPoint     = sv("full_quad_vs");
        testPipeDesc.fragment              = &testFrag;
        testPipeDesc.depthStencil          = &testDS;
        testPipeDesc.primitive.topology    = WGPUPrimitiveTopology_TriangleList;
        testPipeDesc.multisample.count     = 1;
        WGPURenderPipeline testPipeline = t.createRenderPipelineTracked(testPipeDesc);

        // ----- checkPipeline -----
        // layout auto, vertex=full_quad_vs, fragment=check_texture (targets [{r32uint}]),
        // primitive triangle-list.  No depthStencil.
        WGPUColorTargetState checkColorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        checkColorTarget.format    = WGPUTextureFormat_R32Uint;
        // writeMask default All via WGPU_COLOR_TARGET_STATE_INIT

        WGPUFragmentState checkFrag = WGPU_FRAGMENT_STATE_INIT;
        checkFrag.module      = testCheckModule;
        checkFrag.entryPoint  = sv("check_texture");
        checkFrag.targetCount = 1;
        checkFrag.targets     = &checkColorTarget;

        WGPURenderPipelineDescriptor checkPipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        checkPipeDesc.layout                = nullptr; // auto
        checkPipeDesc.vertex.module         = testCheckModule;
        checkPipeDesc.vertex.entryPoint     = sv("full_quad_vs");
        checkPipeDesc.fragment              = &checkFrag;
        checkPipeDesc.depthStencil          = nullptr;
        checkPipeDesc.primitive.topology    = WGPUPrimitiveTopology_TriangleList;
        checkPipeDesc.multisample.count     = 1;
        WGPURenderPipeline checkPipeline = t.createRenderPipelineTracked(checkPipeDesc);

        // ----- Bind groups -----
        // testBindGroup: layout from testPipeline, binding 0 = depthView, binding 1 = stencilView
        {
            WGPUBindGroupLayout testBgl = wgpuRenderPipelineGetBindGroupLayout(testPipeline, 0);

            WGPUBindGroupEntry testEntries[2] = {
                WGPU_BIND_GROUP_ENTRY_INIT,
                WGPU_BIND_GROUP_ENTRY_INIT,
            };
            testEntries[0].binding     = 0;
            testEntries[0].textureView = dsDepthView;
            testEntries[1].binding     = 1;
            testEntries[1].textureView = dsStencilView;

            WGPUBindGroupDescriptor testBgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            testBgDesc.layout     = testBgl;
            testBgDesc.entryCount = 2;
            testBgDesc.entries    = testEntries;
            WGPUBindGroup testBindGroup = t.createBindGroupTracked(testBgDesc);
            wgpuBindGroupLayoutRelease(testBgl);

            // checkBindGroup: layout from checkPipeline, same aspect views
            WGPUBindGroupLayout checkBgl = wgpuRenderPipelineGetBindGroupLayout(checkPipeline, 0);

            WGPUBindGroupEntry checkEntries[2] = {
                WGPU_BIND_GROUP_ENTRY_INIT,
                WGPU_BIND_GROUP_ENTRY_INIT,
            };
            checkEntries[0].binding     = 0;
            checkEntries[0].textureView = dsDepthView;
            checkEntries[1].binding     = 1;
            checkEntries[1].textureView = dsStencilView;

            WGPUBindGroupDescriptor checkBgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            checkBgDesc.layout     = checkBgl;
            checkBgDesc.entryCount = 2;
            checkBgDesc.entries    = checkEntries;
            WGPUBindGroup checkBindGroup = t.createBindGroupTracked(checkBgDesc);
            wgpuBindGroupLayoutRelease(checkBgl);

            // ----- One command encoder, three passes -----
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

            // === Pass 1: init ===
            // depthStencilAttachment: depthLoadOp clear, depthStoreOp store, depthClearValue 0,
            //                        stencilLoadOp clear, stencilStoreOp store, stencilClearValue 0.
            {
                WGPURenderPassDepthStencilAttachment initDsAtt =
                    WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
                initDsAtt.view             = dsFullView;
                initDsAtt.depthLoadOp      = WGPULoadOp_Clear;
                initDsAtt.depthStoreOp     = WGPUStoreOp_Store;
                initDsAtt.depthClearValue  = 0.0f;
                initDsAtt.stencilLoadOp    = WGPULoadOp_Clear;
                initDsAtt.stencilStoreOp   = WGPUStoreOp_Store;
                initDsAtt.stencilClearValue = 0;

                WGPURenderPassDescriptor initPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                initPassDesc.colorAttachmentCount   = 0;
                initPassDesc.colorAttachments       = nullptr;
                initPassDesc.depthStencilAttachment = &initDsAtt;

                WGPURenderPassEncoder initPass =
                    wgpuCommandEncoderBeginRenderPass(encoder, &initPassDesc);
                wgpuRenderPassEncoderSetPipeline(initPass, initPipeline);
                // Draw 3 columns (X = 0,1,2), each as instance i; 3 points (Y=0,1,2).
                for (uint32_t i = 0; i < 3; ++i) {
                    wgpuRenderPassEncoderSetStencilReference(initPass, i + 1);
                    wgpuRenderPassEncoderDraw(initPass, 3, 1, 0, i);
                }
                wgpuRenderPassEncoderEnd(initPass);
            }

            // === Pass 2: test (read-only DS attachment + concurrent sampling) ===
            // depthReadOnly=WGPU_TRUE, stencilReadOnly=WGPU_TRUE; load/store ops left Undefined.
            {
                WGPURenderPassDepthStencilAttachment testDsAtt =
                    WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
                testDsAtt.view           = dsFullView;
                testDsAtt.depthReadOnly  = WGPU_TRUE;
                testDsAtt.stencilReadOnly = WGPU_TRUE;
                // depthLoadOp/depthStoreOp and stencilLoadOp/stencilStoreOp remain Undefined
                // (required when the aspect is read-only).

                WGPURenderPassDescriptor testPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                testPassDesc.colorAttachmentCount   = 0;
                testPassDesc.colorAttachments       = nullptr;
                testPassDesc.depthStencilAttachment = &testDsAtt;

                WGPURenderPassEncoder testPass =
                    wgpuCommandEncoderBeginRenderPass(encoder, &testPassDesc);
                wgpuRenderPassEncoderSetPipeline(testPass, testPipeline);
                wgpuRenderPassEncoderSetStencilReference(testPass, 2);
                wgpuRenderPassEncoderSetBindGroup(testPass, 0, testBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(testPass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(testPass);
            }

            // === Pass 3: check ===
            // Color attachment: resultTexture, loadOp clear, clearValue [0,0,0,0], storeOp store.
            {
                WGPURenderPassColorAttachment checkColorAtt =
                    WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
                checkColorAtt.view       = resultView;
                checkColorAtt.loadOp     = WGPULoadOp_Clear;
                checkColorAtt.storeOp    = WGPUStoreOp_Store;
                checkColorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

                WGPURenderPassDescriptor checkPassDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
                checkPassDesc.colorAttachmentCount   = 1;
                checkPassDesc.colorAttachments       = &checkColorAtt;
                checkPassDesc.depthStencilAttachment = nullptr;

                WGPURenderPassEncoder checkPass =
                    wgpuCommandEncoderBeginRenderPass(encoder, &checkPassDesc);
                wgpuRenderPassEncoderSetPipeline(checkPass, checkPipeline);
                wgpuRenderPassEncoderSetBindGroup(checkPass, 0, checkBindGroup, 0, nullptr);
                wgpuRenderPassEncoderDraw(checkPass, 3, 1, 0, 0);
                wgpuRenderPassEncoderEnd(checkPass);
            }

            // Submit all three passes.
            WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
            wgpuQueueSubmit(t.queue(), 1, &cmdBuf);
        }

        // ----- Verify -----
        // Copy resultTexture (3x3 r32uint) to a readback buffer.
        // bytesPerRow 256, rowsPerImage 3, size {3,3,1}.
        // Texel (x,y) lives at byte y*256 + x*4; must be u32 == 1.
        constexpr uint64_t kBytesPerRow  = 256;
        constexpr uint64_t kRowsPerImage = 3;
        constexpr uint64_t kReadbackSize = kBytesPerRow * kRowsPerImage; // 768 bytes

        WGPUBufferDescriptor rbDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        rbDesc.size  = kReadbackSize;
        rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        // Zero-init: do not pre-fill with expected value (would mask no-write bugs).
        WGPUBuffer readback = t.createBufferTracked(rbDesc);

        {
            WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            src.texture  = resultTexture;
            src.mipLevel = 0;
            src.origin   = WGPUOrigin3D{0, 0, 0};
            src.aspect   = WGPUTextureAspect_All;

            WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
            dst.buffer              = readback;
            dst.layout.offset       = 0;
            dst.layout.bytesPerRow  = static_cast<uint32_t>(kBytesPerRow);
            dst.layout.rowsPerImage = static_cast<uint32_t>(kRowsPerImage);

            WGPUExtent3D copySize = WGPUExtent3D{3, 3, 1};

            WGPUCommandEncoder copyEncoder = t.createCommandEncoderTracked();
            wgpuCommandEncoderCopyTextureToBuffer(copyEncoder, &src, &dst, &copySize);
            WGPUCommandBuffer copyCmdBuf = t.finishTracked(copyEncoder);
            wgpuQueueSubmit(t.queue(), 1, &copyCmdBuf);
        }

        t.expectGPUBufferValuesPassCheck(
            readback,
            [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                // Each of the 9 texels must read back as u32 == 1.
                for (uint32_t y = 0; y < 3; ++y) {
                    for (uint32_t x = 0; x < 3; ++x) {
                        const size_t byteOffset = y * 256u + x * 4u;
                        if (byteOffset + 4 > len) {
                            std::ostringstream msg;
                            msg << "readback buffer too small at texel (" << x << "," << y
                                << "): need " << (byteOffset + 4) << " bytes, got " << len;
                            return msg.str();
                        }
                        uint32_t value = 0;
                        std::memcpy(&value, actual + byteOffset, 4);
                        if (value != 1u) {
                            std::ostringstream msg;
                            msg << "resultTexture texel (" << x << "," << y
                                << ") expected 1, got " << value;
                            return msg.str();
                        }
                    }
                }
                return std::nullopt;
            },
            0,
            static_cast<size_t>(kReadbackSize));
    });

} // namespace
