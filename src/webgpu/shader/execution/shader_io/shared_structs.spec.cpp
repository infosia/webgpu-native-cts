// Ported from gpuweb/cts src/webgpu/shader/execution/shader_io/shared_structs.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations):
// - upstream `ttu.expectSinglePixelComparisonsAreOkInTexture` and
//   `checkElementsEqual` are inlined using `copyTextureToBuffer` +
//   `expectGPUBufferValuesPassCheck` with a per-pixel check lambda, following
//   the pattern established in culling_tests.spec.cpp and primitive_topology.spec.cpp.
// - The readback buffer is zero-initialized (never pre-filled with expected values).
// - `t.queue.submit` maps to `wgpuQueueSubmit(t.queue(), ...)`.
// - `t.makeBufferWithContents` maps to `t.makeBufferWithContents(data, size, usage)`.
// - The render target size is [31, 31] matching upstream exactly.

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> grp = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,shader_io,shared_structs",
    "Test the shared use of structures containing entry point IO attributes");

// Helper: build a WGPUStringView from a string literal / static C string.
// NOTE: this must take `const char*` (static-storage literals), not
// `const std::string&` — a std::string overload called with a literal binds
// to a temporary whose .data() dangles before the descriptor is consumed.
static WGPUStringView sv(const char* s) {
    return WGPUStringView{s, std::strlen(s)};
}

// ---------------------------------------------------------------------------
// Render-test helpers (shared between shared_between_stages and
// shared_with_non_entry_point_function).
// ---------------------------------------------------------------------------

// Pixel check constants for a 31x31 RGBA8Unorm render target.
// bytesPerRow must be a multiple of 256 (the WebGPU required alignment).
constexpr uint32_t kRTWidth       = 31u;
constexpr uint32_t kRTHeight      = 31u;
constexpr uint32_t kBytesPerPixel = 4u;
constexpr uint32_t kBytesPerRow   = 256u;  // ceil(31*4 / 256) * 256 = 256

// Total buffer size to hold all rows.
constexpr uint64_t kReadbackSize =
    static_cast<uint64_t>(kBytesPerRow) * (kRTHeight - 1u) + kRTWidth * kBytesPerPixel;

struct PixelCheck {
    uint32_t x;
    uint32_t y;
    std::array<uint8_t, 4> expected; // RGBA
};

// Run the pixel checks against a readback buffer.
// Returns nullopt on success, or an error string on first mismatch.
static std::optional<std::string> checkPixels(
    const std::vector<PixelCheck>& checks,
    const uint8_t* actual,
    size_t len)
{
    for (const PixelCheck& c : checks) {
        const uint64_t offset =
            static_cast<uint64_t>(c.y) * kBytesPerRow +
            static_cast<uint64_t>(c.x) * kBytesPerPixel;
        if (offset + kBytesPerPixel > len) {
            std::ostringstream msg;
            msg << "pixel offset out of range at (" << c.x << "," << c.y << ")";
            return msg.str();
        }
        for (uint32_t ch = 0u; ch < kBytesPerPixel; ++ch) {
            const uint8_t got      = actual[static_cast<size_t>(offset) + ch];
            const uint8_t expected = c.expected[ch];
            if (got != expected) {
                std::ostringstream msg;
                msg << "rgba8unorm mismatch at (" << c.x << "," << c.y
                    << ") channel " << ch
                    << ": expected " << static_cast<int>(expected)
                    << " got " << static_cast<int>(got);
                return msg.str();
            }
        }
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// shared_with_buffer
// ---------------------------------------------------------------------------

CTS_TEST(grp, "shared_with_buffer")
    .desc(
        "Test sharing an entry point IO struct with a buffer.\n\n"
        "     This test defines a structure that contains both builtin attributes and layout attributes,\n"
        "     and uses that structure as both an entry point input and the store type of a storage buffer.\n"
        "     The builtin attributes should be ignored when used for the storage buffer, and the layout\n"
        "     attributes should be ignored when used as an entry point IO parameter.\n"
        "    ")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Set the dispatch parameters such that we get some interesting
        // (non-zero) built-in variables.
        const uint32_t wgsizeX = 8u, wgsizeY = 4u, wgsizeZ = 2u;
        const uint32_t numGroupsX = 4u, numGroupsY = 2u, numGroupsZ = 8u;

        // Pick a single invocation to copy the input structure to the output
        // buffer.
        const uint32_t targetLocalIndex  = 13u;
        const uint32_t targetGroupX      = 2u;
        const uint32_t targetGroupY      = 1u;
        const uint32_t targetGroupZ      = 5u;

        // Build the WGSL shader via string substitution, mirroring upstream's
        // template literals exactly.
        const std::string wgsl =
            std::string("      struct S {\n") +
            "        /* byte offset:  0 */ @size(32)  @builtin(workgroup_id) group_id : vec3<u32>,\n" +
            "        /* byte offset: 32 */            @builtin(local_invocation_index) local_index : u32,\n" +
            "        /* byte offset: 64 */ @align(64) @builtin(num_workgroups) numGroups : vec3<u32>,\n" +
            "      };\n" +
            "\n" +
            "      @group(0) @binding(0)\n" +
            "      var<storage, read_write> outputs : S;\n" +
            "\n" +
            "      @compute @workgroup_size(" +
                std::to_string(wgsizeX) + ", " +
                std::to_string(wgsizeY) + ", " +
                std::to_string(wgsizeZ) + ")\n" +
            "      fn main(inputs : S) {\n" +
            "        if (inputs.group_id.x == " + std::to_string(targetGroupX) + "u &&\n" +
            "            inputs.group_id.y == " + std::to_string(targetGroupY) + "u &&\n" +
            "            inputs.group_id.z == " + std::to_string(targetGroupZ) + "u &&\n" +
            "            inputs.local_index == " + std::to_string(targetLocalIndex) + "u) {\n" +
            "          outputs = inputs;\n" +
            "        }\n" +
            "      }\n";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = nullptr; // auto layout
        pipelineDesc.compute.module = shaderModule;
        pipelineDesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

        // Allocate a buffer to hold the output structure.
        // 32 uint32 words = 128 bytes.
        constexpr uint32_t bufferNumElements = 32u;
        constexpr uint64_t bufferSize = static_cast<uint64_t>(bufferNumElements) * sizeof(uint32_t);

        // Output/readback buffer: zero-initialized (never pre-filled with
        // expected values — see docs/05-porting-guide.md §readback-buffers).
        WGPUBufferDescriptor outputBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        outputBufferDesc.size  = bufferSize;
        outputBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer outputBuffer = t.createBufferTracked(outputBufferDesc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer  = outputBuffer;
        entry.offset  = 0;
        entry.size    = bufferSize;

        WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.layout     = bgl;
        bindGroupDesc.entryCount = 1;
        bindGroupDesc.entries    = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
        wgpuBindGroupLayoutRelease(bgl);

        // Run the shader.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, numGroupsX, numGroupsY, numGroupsZ);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        // Check the output values.
        // Struct layout (from @size/@align decorators + WGSL default rules):
        //   byte offset  0: group_id  (vec3<u32>) — @size(32), so words 0..7
        //   byte offset 32: local_index (u32)      — word index 8
        //   byte offset 64: numGroups  (vec3<u32>) — @align(64), words 16..18
        const uint32_t capTargetLocalIndex = targetLocalIndex;
        const uint32_t capTargetGroupX     = targetGroupX;
        const uint32_t capTargetGroupY     = targetGroupY;
        const uint32_t capTargetGroupZ     = targetGroupZ;
        const uint32_t capNumGroupsX       = numGroupsX;
        const uint32_t capNumGroupsY       = numGroupsY;
        const uint32_t capNumGroupsZ       = numGroupsZ;

        t.expectGPUBufferValuesPassCheck(
            outputBuffer,
            // The cap* locals are const integrals with constant initializers; reading them
            // inside the lambda is not an odr-use, so no capture is required (-Wunused-lambda-capture).
            [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                if (len < bufferNumElements * sizeof(uint32_t)) {
                    return std::string("readback buffer too small");
                }
                // Reinterpret the buffer as uint32 words.
                const uint32_t* outputs = reinterpret_cast<const uint32_t*>(actual);

                // group_id: words 0,1,2 (x,y,z of the vec3).
                if (outputs[0] != capTargetGroupX ||
                    outputs[1] != capTargetGroupY ||
                    outputs[2] != capTargetGroupZ) {
                    std::ostringstream msg;
                    msg << "group_id comparison failed\n"
                        << "    expected: ["
                        << capTargetGroupX << "," << capTargetGroupY << "," << capTargetGroupZ << "]\n"
                        << "    got:      ["
                        << outputs[0] << "," << outputs[1] << "," << outputs[2] << "]";
                    return msg.str();
                }

                // local_index: word 8 (byte offset 32 / 4 = 8).
                if (outputs[8] != capTargetLocalIndex) {
                    std::ostringstream msg;
                    msg << "local_index comparison failed\n"
                        << "    expected: " << capTargetLocalIndex << "\n"
                        << "    got:      " << outputs[8];
                    return msg.str();
                }

                // numGroups: words 16,17,18 (byte offset 64 / 4 = 16).
                if (outputs[16] != capNumGroupsX ||
                    outputs[17] != capNumGroupsY ||
                    outputs[18] != capNumGroupsZ) {
                    std::ostringstream msg;
                    msg << "numGroups comparison failed\n"
                        << "    expected: ["
                        << capNumGroupsX << "," << capNumGroupsY << "," << capNumGroupsZ << "]\n"
                        << "    got:      ["
                        << outputs[16] << "," << outputs[17] << "," << outputs[18] << "]";
                    return msg.str();
                }

                return std::nullopt;
            },
            /*srcByteOffset=*/ 0,
            /*byteLength=*/ static_cast<size_t>(bufferSize));
    });

// ---------------------------------------------------------------------------
// shared_between_stages
// ---------------------------------------------------------------------------

CTS_TEST(grp, "shared_between_stages")
    .desc(
        "Test sharing an entry point IO struct between different pipeline stages.\n\n"
        "     This test defines an entry point IO structure, and uses it as both the output of a vertex\n"
        "     shader and the input to a fragment shader.\n"
        "    ")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t sizeX = kRTWidth;   // 31
        constexpr uint32_t sizeY = kRTHeight;  // 31

        // Build the WGSL shader. The fragment shader toggles red vs green based
        // on the x position relative to size[0]/2 = 15.5. Upstream's JS
        // `${size[0] / 2}` is real division (31 / 2 = 15.5), so format it as a
        // double — integer division would shift the red/green boundary.
        char halfWidth[32];
        std::snprintf(halfWidth, sizeof(halfWidth), "%g", sizeX / 2.0);
        const std::string wgsl =
            std::string("      struct Interface {\n") +
            "        @builtin(position) position : vec4<f32>,\n" +
            "        @location(0) color : f32,\n" +
            "      };\n" +
            "\n" +
            "      var<private> vertices : array<vec2<f32>, 3> = array<vec2<f32>, 3>(\n" +
            "        vec2<f32>(-0.7, -0.7),\n" +
            "        vec2<f32>( 0.0,  0.7),\n" +
            "        vec2<f32>( 0.7, -0.7),\n" +
            "      );\n" +
            "\n" +
            "      @vertex\n" +
            "      fn vert_main(@builtin(vertex_index) index : u32) -> Interface {\n" +
            "        return Interface(vec4<f32>(vertices[index], 0.0, 1.0), 1.0);\n" +
            "      }\n" +
            "\n" +
            "      @fragment\n" +
            "      fn frag_main(inputs : Interface) -> @location(0) vec4<f32> {\n" +
            "        // Toggle red vs green based on the x position.\n" +
            "        var color = vec4<f32>(0.0, 0.0, 0.0, 1.0);\n" +
            "        if (inputs.position.x > f32(" + std::string(halfWidth) + ")) {\n" +
            "          color.r = inputs.color;\n" +
            "        } else {\n" +
            "          color.g = inputs.color;\n" +
            "        }\n" +
            "        return color;\n" +
            "      }\n";

        // Set up the render pipeline.
        WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module     = shaderModule;
        fragment.entryPoint = sv("frag_main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout             = nullptr; // auto layout
        pipelineDesc.vertex.module      = shaderModule;
        pipelineDesc.vertex.entryPoint  = sv("vert_main");
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count  = 1;
        pipelineDesc.fragment           = &fragment;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipelineDesc);

        // Draw into a 31x31 render target (cleared to black/transparent).
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.size          = WGPUExtent3D{sizeX, sizeY, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture renderTarget = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView renderTargetView   = t.createViewTracked(renderTarget, viewDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view       = renderTargetView;
        colorAttachment.loadOp     = WGPULoadOp_Clear;
        colorAttachment.storeOp    = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        // Copy texture to readback buffer.
        WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readbackDesc.size  = kReadbackSize;
        readbackDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer readbackBuffer = t.createBufferTracked(readbackDesc);

        t.copyTextureToBuffer(encoder, renderTarget, readbackBuffer, kBytesPerRow,
                              WGPUExtent3D{sizeX, sizeY, 1});

        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        // Test a few points to make sure we rendered a half-red/half-green
        // triangle (upstream pixel coordinates verbatim).
        const std::array<uint8_t, 4> redPixel   = {255, 0, 0, 255};
        const std::array<uint8_t, 4> greenPixel  = {0, 255, 0, 255};
        const std::array<uint8_t, 4> blackPixel  = {0, 0, 0, 0};

        const std::vector<PixelCheck> checks = {
            // Red pixels
            {16u, 15u, redPixel},
            {16u,  8u, redPixel},
            {22u, 20u, redPixel},
            // Green pixels
            {14u, 15u, greenPixel},
            {14u,  8u, greenPixel},
            { 8u, 20u, greenPixel},
            // Black pixels
            { 2u,  2u, blackPixel},
            { 2u, 28u, blackPixel},
            {28u,  2u, blackPixel},
            {28u, 28u, blackPixel},
        };

        t.expectGPUBufferValuesPassCheck(
            readbackBuffer,
            [checks](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                return checkPixels(checks, actual, len);
            },
            /*srcByteOffset=*/ 0,
            /*byteLength=*/ static_cast<size_t>(kReadbackSize));
    });

// ---------------------------------------------------------------------------
// shared_with_non_entry_point_function
// ---------------------------------------------------------------------------

CTS_TEST(grp, "shared_with_non_entry_point_function")
    .desc(
        "Test sharing an entry point IO struct with a non entry point function.\n\n"
        "     This test defines structures that contain builtin and location attributes, and uses those\n"
        "     structures as parameter and return types for entry point functions and regular functions.\n"
        "    ")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        constexpr uint32_t sizeX = kRTWidth;   // 31
        constexpr uint32_t sizeY = kRTHeight;  // 31

        // The WGSL shader defines Inputs/Outputs structs with builtin + location
        // attributes, and a `process` helper function that takes/returns them.
        static const char wgsl[] =
            "      struct Inputs {\n"
            "        @builtin(vertex_index) index : u32,\n"
            "        @location(0) color : vec4<f32>,\n"
            "      };\n"
            "      struct Outputs {\n"
            "        @builtin(position) position : vec4<f32>,\n"
            "        @location(0) color : vec4<f32>,\n"
            "      };\n"
            "\n"
            "      var<private> vertices : array<vec2<f32>, 3> = array<vec2<f32>, 3>(\n"
            "        vec2<f32>(-0.7, -0.7),\n"
            "        vec2<f32>( 0.0,  0.7),\n"
            "        vec2<f32>( 0.7, -0.7),\n"
            "      );\n"
            "\n"
            "      fn process(in : Inputs) -> Outputs {\n"
            "        var out : Outputs;\n"
            "        out.position = vec4<f32>(vertices[in.index], 0.0, 1.0);\n"
            "        out.color = in.color;\n"
            "        return out;\n"
            "      }\n"
            "\n"
            "      @vertex\n"
            "      fn vert_main(inputs : Inputs) -> Outputs {\n"
            "        return process(inputs);\n"
            "      }\n"
            "\n"
            "      @fragment\n"
            "      fn frag_main(@location(0) color : vec4<f32>) -> @location(0) vec4<f32> {\n"
            "        return color;\n"
            "      }\n";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(std::string_view(wgsl));

        // Vertex buffer layout: a single float32x4 attribute at location 0
        // (the color), stride 4 * sizeof(float) = 16 bytes.
        WGPUVertexAttribute vertexAttr = WGPU_VERTEX_ATTRIBUTE_INIT;
        vertexAttr.shaderLocation = 0;
        vertexAttr.format         = WGPUVertexFormat_Float32x4;
        vertexAttr.offset         = 0;

        WGPUVertexBufferLayout vertexBufferLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
        vertexBufferLayout.attributeCount = 1;
        vertexBufferLayout.attributes     = &vertexAttr;
        vertexBufferLayout.arrayStride    = 4 * sizeof(float);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module      = shaderModule;
        fragment.entryPoint  = sv("frag_main");
        fragment.targetCount = 1;
        fragment.targets     = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout                     = nullptr; // auto layout
        pipelineDesc.vertex.module              = shaderModule;
        pipelineDesc.vertex.entryPoint          = sv("vert_main");
        pipelineDesc.vertex.bufferCount         = 1;
        pipelineDesc.vertex.buffers             = &vertexBufferLayout;
        pipelineDesc.primitive.topology         = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.multisample.count          = 1;
        pipelineDesc.fragment                   = &fragment;
        WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipelineDesc);

        // The vertex buffer contains the vertex colors (all red: [1,0,0,1] x3).
        // Mirrors upstream: new Float32Array([1,0,0,1, 1,0,0,1, 1,0,0,1])
        const float vertexColorData[12] = {
            1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 0.0f, 1.0f,
        };
        WGPUBuffer vertexBuffer = t.makeBufferWithContents(
            vertexColorData, sizeof(vertexColorData),
            WGPUBufferUsage_Vertex);

        // Render target: 31x31, cleared to black/transparent.
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
        texDesc.size          = WGPUExtent3D{sizeX, sizeY, 1};
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount   = 1;
        texDesc.dimension     = WGPUTextureDimension_2D;
        texDesc.usage         = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        WGPUTexture renderTarget = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView renderTargetView   = t.createViewTracked(renderTarget, viewDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view       = renderTargetView;
        colorAttachment.loadOp     = WGPULoadOp_Clear;
        colorAttachment.storeOp    = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments     = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuffer, 0, sizeof(vertexColorData));
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        // Copy texture to readback buffer.
        WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        readbackDesc.size  = kReadbackSize;
        readbackDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer readbackBuffer = t.createBufferTracked(readbackDesc);

        t.copyTextureToBuffer(encoder, renderTarget, readbackBuffer, kBytesPerRow,
                              WGPUExtent3D{sizeX, sizeY, 1});

        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        // Test a few points to make sure we rendered a red triangle.
        // Pixel coordinates are verbatim from upstream.
        const std::array<uint8_t, 4> redPixel   = {255, 0, 0, 255};
        const std::array<uint8_t, 4> blackPixel  = {0, 0, 0, 0};

        const std::vector<PixelCheck> checks = {
            // Red pixels
            {15u, 15u, redPixel},
            {15u,  8u, redPixel},
            { 8u, 20u, redPixel},
            {22u, 20u, redPixel},
            // Black pixels
            { 2u,  2u, blackPixel},
            { 2u, 28u, blackPixel},
            {28u,  2u, blackPixel},
            {28u, 28u, blackPixel},
        };

        t.expectGPUBufferValuesPassCheck(
            readbackBuffer,
            [checks](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                return checkPixels(checks, actual, len);
            },
            /*srcByteOffset=*/ 0,
            /*byteLength=*/ static_cast<size_t>(kReadbackSize));
    });

} // namespace
