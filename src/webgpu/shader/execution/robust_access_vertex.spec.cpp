// Ported from gpuweb/cts src/webgpu/shader/execution/robust_access_vertex.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.
//
// Inlined upstream helpers:
// - The upstream `DrawCall` class is ported as the `DrawCall` struct below.
// - `ttu.expectSinglePixelComparisonsAreOkInTexture` (texture_test_utils.ts) is
//   inlined as a 1x1 copyTextureToBuffer + expectGPUBufferValuesPassCheck on a
//   zero-initialized readback buffer (same pattern as stage.spec.cpp).
// Deviations:
// - Upstream's `.unless(...)` has no direct ParamsBuilder method in this
//   harness; it is expressed as `.filter(...)` with the negated predicate
//   (identical case/subcase identity and selection).

#include <cstdint>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,robust_access_vertex",
    R"(
Test vertex attributes behave correctly (no crash / data leak) when accessed out of bounds

Test coverage:

The following is parameterized (all combinations tested):

1) Draw call type? (drawIndexed, drawIndirect, drawIndexedIndirect)
  - Run the draw call using an index buffer and/or an indirect buffer.
  - Doesn't test direct draw, as vertex buffer OOB are CPU validated and treated as validation errors.
  - Also the instance step mode vertex buffer OOB are CPU validated for drawIndexed, so we only test
    robustness access for vertex step mode vertex buffers.

2) Draw call parameter (vertexCount, firstVertex, indexCount, firstIndex, baseVertex, instanceCount,
   vertexCountInIndexBuffer)
  - The parameter which goes out of bounds. Filtered depending on the draw call type.
  - vertexCount, firstVertex: used for drawIndirect only, test for vertex step mode buffer OOB
  - instanceCount: used for both drawIndirect and drawIndexedIndirect, test for instance step mode buffer OOB
  - baseVertex, vertexCountInIndexBuffer: used for both drawIndexed and drawIndexedIndirect, test
    for vertex step mode buffer OOB. vertexCountInIndexBuffer indicates how many vertices are used
    within the index buffer, i.e. [0, 1, ..., vertexCountInIndexBuffer-1].
  - indexCount, firstIndex: used for drawIndexedIndirect only, validate the vertex buffer access
    when the vertex itself is OOB in index buffer. This never happens in drawIndexed as we have index
    buffer OOB CPU validation for it.

3) Attribute type (float32, float32x2, float32x3, float32x4)
  - The input attribute type in the vertex shader

4) Error scale (0, 1, 4, 10^2, 10^4, 10^6)
  - Offset to add to the correct draw call parameter
  - 0 For control case

5) Additional vertex buffers (0, +4)
  - Tests that no OOB occurs if more vertex buffers are used

6) Partial last number and offset vertex buffer (false, true)
  - Tricky cases that make vertex buffer OOB.
  - With partial last number enabled, vertex buffer size will be 1 byte less than enough, making the
    last vertex OOB with 1 byte.
  - Offset vertex buffer will bind the vertex buffer to render pass with 4 bytes offset, causing OOB
  - For drawIndexed, these two flags are suppressed for instance step mode vertex buffer to make sure
    it pass the CPU validation.

The tests have one instance step mode vertex buffer bound for instanced attributes, to make sure
instanceCount / firstInstance are tested.

The tests include multiple attributes per vertex buffer.

The vertex buffers are filled by repeating a few values randomly chosen for each test until the
end of the buffer.

The tests run a render pipeline which verifies the following:
1) All vertex attribute values occur in the buffer or are 0 (for control case it can't be 0)
2) All gl_VertexIndex values are within the index buffer or 0

TODO:
Currently firstInstance is not tested, as for drawIndexed it is CPU validated, and for drawIndirect
and drawIndexedIndirect it should always be 0. Once there is an extension to allow making them non-zero,
it should be added into drawCallTestParameter list.
)");

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// Parameterize different sized types (upstream typeInfoMap)
// ---------------------------------------------------------------------------
struct VertexInfo {
    const char* wgslType;
    uint32_t sizeInBytes;
    const char* validationFunc;
    WGPUVertexFormat format;
};

const VertexInfo& typeInfoFor(const std::string& name) {
    static const VertexInfo kFloat32 = {
        "f32",
        4,
        "return valid(v);",
        WGPUVertexFormat_Float32,
    };
    static const VertexInfo kFloat32x2 = {
        "vec2<f32>",
        8,
        "return valid(v.x) && valid(v.y);",
        WGPUVertexFormat_Float32x2,
    };
    static const VertexInfo kFloat32x3 = {
        "vec3<f32>",
        12,
        "return valid(v.x) && valid(v.y) && valid(v.z);",
        WGPUVertexFormat_Float32x3,
    };
    static const VertexInfo kFloat32x4 = {
        "vec4<f32>",
        16,
        "return (valid(v.x) && valid(v.y) && valid(v.z) && valid(v.w)) ||\n"
        "                            (v.x == 0.0 && v.y == 0.0 && v.z == 0.0 && (v.w == 0.0 || v.w == 1.0));",
        WGPUVertexFormat_Float32x4,
    };
    if (name == "float32") {
        return kFloat32;
    }
    if (name == "float32x2") {
        return kFloat32x2;
    }
    if (name == "float32x3") {
        return kFloat32x3;
    }
    if (name == "float32x4") {
        return kFloat32x4;
    }
    throw TestFailed("unknown vertex format: " + name);
}

// ---------------------------------------------------------------------------
// Encapsulates a draw call (either indexed or non-indexed) — upstream DrawCall
// ---------------------------------------------------------------------------
struct DrawCall {
    // Draw
    uint32_t vertexCount = 0;
    uint32_t firstVertex = 0;

    // DrawIndexed
    uint32_t vertexCountInIndexBuffer = 0; // For generating index buffer in drawIndexed and drawIndexedIndirect
    uint32_t indexCount = 0;               // For accessing index buffer in drawIndexed and drawIndexedIndirect
    uint32_t firstIndex = 0;
    uint32_t baseVertex = 0;

    // Both Draw and DrawIndexed
    uint32_t instanceCount = 0;
    uint32_t firstInstance = 0;

    DrawCall(AllFeaturesMaxLimitsGpuTest& test,
             const std::vector<std::vector<float>>& vertexArrays,
             uint32_t vertexCountIn,
             bool partialLastNumber,
             bool offsetVertexBuffer,
             bool keepInstanceStepModeBufferInRange)
        : test_(&test),
          offsetVertexBuffer_(offsetVertexBuffer),
          keepInstanceStepModeBufferInRange_(keepInstanceStepModeBufferInRange) {
        // Default arguments (valid call)
        vertexCount = vertexCountIn;
        firstVertex = 0;
        vertexCountInIndexBuffer = vertexCountIn;
        indexCount = vertexCountIn;
        firstIndex = 0;
        baseVertex = 0;
        instanceCount = vertexCountIn;
        firstInstance = 0;

        // Since vertexCountInIndexBuffer is mutable, generation of the index buffer
        // is deferred to right before calling draw.

        // Generate vertex buffers
        vertexBuffers_.reserve(vertexArrays.size());
        for (size_t i = 0; i < vertexArrays.size(); ++i) {
            if (i == 0 && keepInstanceStepModeBufferInRange_) {
                // Suppress partialLastNumber for the first vertex buffer,
                // aka the instance step mode buffer
                vertexBuffers_.push_back(generateVertexBuffer(vertexArrays[i], false));
            } else {
                vertexBuffers_.push_back(generateVertexBuffer(vertexArrays[i], partialLastNumber));
            }
        }
    }

    // Insert a draw call into |pass| with specified type
    void insertInto(WGPURenderPassEncoder pass, bool indexed, bool indirect) {
        if (indexed) {
            if (indirect) {
                drawIndexedIndirect(pass);
            } else {
                drawIndexed(pass);
            }
        } else {
            if (indirect) {
                drawIndirect(pass);
            } else {
                draw(pass);
            }
        }
    }

    // Insert a draw call into |pass|
    void draw(WGPURenderPassEncoder pass) {
        bindVertexBuffers(pass);
        wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, firstVertex, firstInstance);
    }

    // Insert an indexed draw call into |pass|
    void drawIndexed(WGPURenderPassEncoder pass) {
        WGPUBuffer indexBuffer = generateIndexBuffer();
        bindVertexBuffers(pass);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0,
                                            WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, indexCount, instanceCount, firstIndex,
                                         static_cast<int32_t>(baseVertex), firstInstance);
    }

    // Insert an indirect draw call into |pass|
    void drawIndirect(WGPURenderPassEncoder pass) {
        bindVertexBuffers(pass);
        wgpuRenderPassEncoderDrawIndirect(pass, generateIndirectBuffer(), 0);
    }

    // Insert an indexed indirect draw call into |pass|
    void drawIndexedIndirect(WGPURenderPassEncoder pass) {
        WGPUBuffer indexBuffer = generateIndexBuffer();
        bindVertexBuffers(pass);
        wgpuRenderPassEncoderSetIndexBuffer(pass, indexBuffer, WGPUIndexFormat_Uint32, 0,
                                            WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexedIndirect(pass, generateIndexedIndirectBuffer(), 0);
    }

  private:
    AllFeaturesMaxLimitsGpuTest* test_;
    std::vector<WGPUBuffer> vertexBuffers_;

    // Add a byte offset when binding the vertex buffers
    bool offsetVertexBuffer_;

    // Keep instance step mode vertex buffer in range, in order to test vertex step
    // mode buffer OOB in drawIndexed. Setting true suppresses partialLastNumber
    // and offsetVertexBuffer for the instance step mode vertex buffer.
    bool keepInstanceStepModeBufferInRange_;

    // Bind all vertex buffers generated
    void bindVertexBuffers(WGPURenderPassEncoder pass) {
        uint32_t currSlot = 0;
        for (size_t i = 0; i < vertexBuffers_.size(); ++i) {
            if (i == 0 && keepInstanceStepModeBufferInRange_) {
                // Keep the instance step mode buffer in range
                wgpuRenderPassEncoderSetVertexBuffer(pass, currSlot++, vertexBuffers_[i], 0,
                                                     WGPU_WHOLE_SIZE);
            } else {
                wgpuRenderPassEncoderSetVertexBuffer(pass, currSlot++, vertexBuffers_[i],
                                                     offsetVertexBuffer_ ? 4 : 0, WGPU_WHOLE_SIZE);
            }
        }
    }

    // Create a vertex buffer from |vertexArray|
    // If |partialLastNumber| is true, delete one byte off the end
    WGPUBuffer generateVertexBuffer(const std::vector<float>& vertexArray, bool partialLastNumber) {
        uint64_t size = vertexArray.size() * sizeof(float);
        size_t length = vertexArray.size();
        if (partialLastNumber) {
            size -= 1;   // Shave off one byte from the buffer size.
            length -= 1; // And one whole element from the writeBuffer.
        }
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = size;
        // Ensure that buffer can be used by writeBuffer
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = test_->createBufferTracked(desc);
        test_->queueWriteBuffer(buffer, 0, vertexArray.data(), length * sizeof(float));
        return buffer;
    }

    // Generate the index buffer [0, 1, ..., vertexCountInIndexBuffer-1]
    WGPUBuffer generateIndexBuffer() {
        std::vector<uint32_t> indexArray(vertexCountInIndexBuffer);
        for (uint32_t i = 0; i < vertexCountInIndexBuffer; ++i) {
            indexArray[i] = i;
        }
        return test_->makeBufferWithContents(indexArray.data(),
                                             indexArray.size() * sizeof(uint32_t),
                                             WGPUBufferUsage_Index);
    }

    // Create an indirect buffer containing draw call values
    WGPUBuffer generateIndirectBuffer() {
        const uint32_t indirectArray[4] = {vertexCount, instanceCount, firstVertex, firstInstance};
        return test_->makeBufferWithContents(indirectArray, sizeof(indirectArray),
                                             WGPUBufferUsage_Indirect);
    }

    // Create an indirect buffer containing indexed draw call values
    WGPUBuffer generateIndexedIndirectBuffer() {
        const uint32_t indirectArray[5] = {indexCount, instanceCount, firstIndex, baseVertex,
                                           firstInstance};
        return test_->makeBufferWithContents(indirectArray, sizeof(indirectArray),
                                             WGPUBufferUsage_Indirect);
    }
};

// ---------------------------------------------------------------------------
// Upstream fixture helpers (class F), inlined as free functions
// ---------------------------------------------------------------------------

// Generate identical vertex buffer contents for each of |bufferCount| buffers.
// Only the first buffer is instance step mode, all others are vertex step mode.
std::vector<std::vector<float>> generateBufferContents(uint32_t numVertices,
                                                       uint32_t attributesPerBuffer,
                                                       const VertexInfo& typeInfo,
                                                       const std::vector<int>& arbitraryValues,
                                                       uint32_t bufferCount) {
    // Make an array big enough for the vertices, attributes, and size of each element
    std::vector<float> vertexArray(static_cast<size_t>(numVertices) * attributesPerBuffer *
                                   (typeInfo.sizeInBytes / 4));
    for (size_t i = 0; i < vertexArray.size(); ++i) {
        vertexArray[i] = static_cast<float>(arbitraryValues[i % arbitraryValues.size()]);
    }

    std::vector<std::vector<float>> bufferContents;
    bufferContents.reserve(bufferCount);
    for (uint32_t i = 0; i < bufferCount; ++i) {
        bufferContents.push_back(vertexArray);
    }
    return bufferContents;
}

std::string generateVertexShaderCode(uint32_t bufferCount,
                                     uint32_t attributesPerBuffer,
                                     const std::vector<int>& validValues,
                                     const VertexInfo& typeInfo,
                                     uint32_t vertexIndexOffset,
                                     uint32_t numVertices,
                                     bool isIndexed) {
    // Create layout and attributes listing
    std::ostringstream layoutStr;
    layoutStr << "struct Attributes {";
    std::vector<std::string> attributeNames;
    {
        uint32_t currAttribute = 0;
        for (uint32_t i = 0; i < bufferCount; ++i) {
            for (uint32_t j = 0; j < attributesPerBuffer; ++j) {
                layoutStr << "@location(" << currAttribute << ") a_" << currAttribute << " : "
                          << typeInfo.wgslType << ",\n";
                attributeNames.push_back("a_" + std::to_string(currAttribute));
                ++currAttribute;
            }
        }
    }
    layoutStr << "};";

    std::ostringstream validExpr;
    for (size_t i = 0; i < validValues.size(); ++i) {
        if (i != 0) {
            validExpr << " || ";
        }
        validExpr << "f == " << validValues[i] << ".0";
    }

    std::ostringstream attributesInBoundsExpr;
    for (size_t i = 0; i < attributeNames.size(); ++i) {
        if (i != 0) {
            attributesInBoundsExpr << " && ";
        }
        attributesInBoundsExpr << "validationFunc(attributes." << attributeNames[i] << ")";
    }

    std::ostringstream code;
    code << "\n      " << layoutStr.str() << "\n\n";
    code << "      fn valid(f : f32) -> bool {\n";
    code << "        return " << validExpr.str() << ";\n";
    code << "      }\n\n";
    code << "      fn validationFunc(v : " << typeInfo.wgslType << ") -> bool {\n";
    code << "        " << typeInfo.validationFunc << "\n";
    code << "      }\n\n";
    code << "      @vertex fn main(\n";
    code << "        @builtin(vertex_index) VertexIndex : u32,\n";
    code << "        attributes : Attributes\n";
    code << "        ) -> @builtin(position) vec4<f32> {\n";
    code << "        var attributesInBounds = " << attributesInBoundsExpr.str() << ";\n\n";
    code << "        var indexInBoundsCountFromBaseVertex =\n";
    code << "            (VertexIndex >= " << vertexIndexOffset << "u &&\n";
    code << "            VertexIndex < " << (vertexIndexOffset + numVertices) << "u);\n";
    code << "        var indexInBounds = VertexIndex == 0u || indexInBoundsCountFromBaseVertex;\n\n";
    code << "        var Position : vec4<f32>;\n";
    code << "        if (attributesInBounds && (" << (isIndexed ? "false" : "true")
         << " || indexInBounds)) {\n";
    code << "          // Success case, move the vertex to the right of the viewport\n";
    code << "          Position = vec4<f32>(0.5, 0.0, 0.0, 1.0);\n";
    code << "        } else {\n";
    code << "          // Failure case, move the vertex to the left of the viewport\n";
    code << "          Position = vec4<f32>(-0.5, 0.0, 0.0, 1.0);\n";
    code << "        }\n";
    code << "        return Position;\n";
    code << "      }";
    return code.str();
}

constexpr std::string_view kFragmentShader = R"(
            @fragment fn main() -> @location(0) vec4<f32> {
              return vec4<f32>(1.0, 0.0, 0.0, 1.0);
            })";

void doTest(AllFeaturesMaxLimitsGpuTest& t,
            uint32_t bufferCount,
            uint32_t attributesPerBuffer,
            const VertexInfo& typeInfo,
            const std::vector<int>& validValues,
            uint32_t vertexIndexOffset,
            uint32_t numVertices,
            bool isIndexed,
            bool isIndirect,
            DrawCall& drawCall) {
    // Vertex buffer descriptors (upstream generateVertexBufferDescriptors)
    std::vector<WGPUVertexAttribute> attributes(
        static_cast<size_t>(bufferCount) * attributesPerBuffer, WGPU_VERTEX_ATTRIBUTE_INIT);
    std::vector<WGPUVertexBufferLayout> buffers(bufferCount, WGPU_VERTEX_BUFFER_LAYOUT_INIT);
    {
        uint32_t currAttribute = 0;
        for (uint32_t i = 0; i < bufferCount; ++i) {
            for (uint32_t j = 0; j < attributesPerBuffer; ++j) {
                WGPUVertexAttribute& attr = attributes[currAttribute];
                attr.shaderLocation = currAttribute;
                attr.offset = static_cast<uint64_t>(j) * typeInfo.sizeInBytes;
                attr.format = typeInfo.format;
                ++currAttribute;
            }
            buffers[i].arrayStride = static_cast<uint64_t>(attributesPerBuffer) * typeInfo.sizeInBytes;
            buffers[i].stepMode = (i == 0) ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex;
            buffers[i].attributeCount = attributesPerBuffer;
            buffers[i].attributes = &attributes[static_cast<size_t>(i) * attributesPerBuffer];
        }
    }

    // Pipeline setup (upstream createRenderPipeline), auto layout
    const std::string vertexCode = generateVertexShaderCode(
        bufferCount, attributesPerBuffer, validValues, typeInfo, vertexIndexOffset, numVertices,
        isIndexed);
    WGPUShaderModule vertexModule = t.createShaderModuleTracked(vertexCode);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = sv("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout = nullptr; // auto layout
    pipeDesc.vertex.module = vertexModule;
    pipeDesc.vertex.entryPoint = sv("main");
    pipeDesc.vertex.bufferCount = bufferCount;
    pipeDesc.vertex.buffers = buffers.data();
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
    pipeDesc.multisample.count = 1;
    pipeDesc.fragment = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

    // Texture setup: 2x1 rgba8unorm color attachment
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{2, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_RenderAttachment;
    WGPUTexture colorAttachment = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorAttachmentView = t.createViewTracked(colorAttachment, viewDesc);

    // Readback buffer for pixel (0,0). Zero-initialized by buffer creation —
    // never pre-filled with the expected bytes.
    constexpr uint64_t kReadbackSize = 256;
    WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    readbackDesc.size = kReadbackSize;
    readbackDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer readback = t.createBufferTracked(readbackDesc);

    WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAtt.view = colorAttachmentView;
    colorAtt.loadOp = WGPULoadOp_Clear;
    colorAtt.storeOp = WGPUStoreOp_Store;
    colorAtt.clearValue = WGPUColor{0.0, 1.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAtt;
    passDesc.depthStencilAttachment = nullptr;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);

    // Run the draw variant
    drawCall.insertInto(pass, isIndexed, isIndirect);

    wgpuRenderPassEncoderEnd(pass);

    // Inlined ttu.expectSinglePixelComparisonsAreOkInTexture: copy pixel (0,0)
    // to the readback buffer and compare against the expected RGBA8 bytes.
    t.copyTextureToBuffer(encoder, colorAttachment, readback, /*bytesPerRow=*/256,
                          WGPUExtent3D{1, 1, 1});

    WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

    // Validate we see green on the left pixel, showing that no failure case is detected
    t.expectGPUBufferValuesPassCheck(
        readback,
        [](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 4) {
                return "readback buffer too small (need >= 4 bytes)";
            }
            const uint8_t expected[4] = {0x00, 0xff, 0x00, 0xff};
            if (std::memcmp(actual, expected, 4) != 0) {
                std::ostringstream msg;
                msg << "pixel(0,0) expected rgba={0,255,0,255}, got {"
                    << static_cast<int>(actual[0]) << "," << static_cast<int>(actual[1]) << ","
                    << static_cast<int>(actual[2]) << "," << static_cast<int>(actual[3]) << "}";
                return msg.str();
            }
            return std::nullopt;
        },
        /*srcByteOffset=*/0,
        /*byteLength=*/4);
}

// ---------------------------------------------------------------------------
// vertex_buffer_access
// ---------------------------------------------------------------------------
CTS_TEST(g, "vertex_buffer_access")
    .params([](ParamsBuilder u) {
        return u
            .combineWithParams({
                ParamRecord{{"indexed", false}, {"indirect", true}},
                ParamRecord{{"indexed", true}, {"indirect", false}},
                ParamRecord{{"indexed", true}, {"indirect", true}},
            })
            .expand("drawCallTestParameter",
                    [](const ParamRecord& p) {
                        const bool indexed = valueAs<bool>(*findParam(p, "indexed"));
                        const bool indirect = valueAs<bool>(*findParam(p, "indirect"));
                        std::vector<Value> values;
                        if (indexed) {
                            values.emplace_back("baseVertex");
                            values.emplace_back("vertexCountInIndexBuffer");
                            if (indirect) {
                                values.emplace_back("indexCount");
                                values.emplace_back("instanceCount");
                                values.emplace_back("firstIndex");
                            }
                        } else if (indirect) {
                            values.emplace_back("vertexCount");
                            values.emplace_back("instanceCount");
                            values.emplace_back("firstVertex");
                        }
                        return values;
                    })
            .combine("type", {"float32", "float32x2", "float32x3", "float32x4"})
            .combine("additionalBuffers", {0, 4})
            .combine("partialLastNumber", {false, true})
            .combine("offsetVertexBuffer", {false, true})
            .beginSubcases()
            .combine("errorScale", {0, 1, 4, 100, 10000, 1000000})
            // Upstream: .unless(p => p.drawCallTestParameter === 'instanceCount'
            //                     && p.errorScale > 10 ** 4) — to avoid timeout.
            // Ported as a filter with the negated predicate.
            .filter([](const ParamRecord& p) {
                const std::string param = valueAs<std::string>(*findParam(p, "drawCallTestParameter"));
                const int64_t errorScale = valueAs<int64_t>(*findParam(p, "errorScale"));
                return !(param == "instanceCount" && errorScale > 10000);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool indexed = t.param<bool>("indexed");
        const bool indirect = t.param<bool>("indirect");
        const std::string drawCallTestParameter = t.param<std::string>("drawCallTestParameter");
        const std::string type = t.param<std::string>("type");
        const uint32_t additionalBuffers = static_cast<uint32_t>(t.param<int>("additionalBuffers"));
        const bool partialLastNumber = t.param<bool>("partialLastNumber");
        const bool offsetVertexBuffer = t.param<bool>("offsetVertexBuffer");
        const uint32_t errorScale = static_cast<uint32_t>(t.param<int64_t>("errorScale"));

        const VertexInfo& typeInfo = typeInfoFor(type);

        // Number of vertices to draw
        const uint32_t numVertices = 4;
        // Each buffer is bound to this many attributes (2 would mean 2 attributes per buffer)
        const uint32_t attributesPerBuffer = 2;
        // Some arbitrary values to fill our buffer with to avoid collisions with other tests
        const std::vector<int> arbitraryValues = {990, 685, 446, 175};

        // A valid value is 0 or one in the buffer
        std::vector<int> validValues;
        if (errorScale == 0 && !offsetVertexBuffer && !partialLastNumber) {
            // Control case with no OOB access, must read back valid values in buffer
            validValues = arbitraryValues;
        } else {
            // Testing case with OOB access, can be 0 for OOB data
            validValues.push_back(0);
            validValues.insert(validValues.end(), arbitraryValues.begin(), arbitraryValues.end());
        }

        // Generate vertex buffer contents. Only the first buffer is instance step mode,
        // all others are vertex step mode
        const uint32_t bufferCount = additionalBuffers + 2; // At least one instance step mode and one vertex step mode buffer
        const std::vector<std::vector<float>> bufferContents =
            generateBufferContents(numVertices, attributesPerBuffer, typeInfo, arbitraryValues,
                                   bufferCount);

        // Mutable draw call
        DrawCall draw(t, bufferContents, numVertices, partialLastNumber, offsetVertexBuffer,
                      /*keepInstanceStepModeBufferInRange=*/indexed && !indirect); // keep instance step mode buffer in range for drawIndexed

        // Offset the draw call parameter we are testing by |errorScale|
        // (upstream: draw[p.drawCallTestParameter] += p.errorScale)
        if (drawCallTestParameter == "vertexCount") {
            draw.vertexCount += errorScale;
        } else if (drawCallTestParameter == "firstVertex") {
            draw.firstVertex += errorScale;
        } else if (drawCallTestParameter == "vertexCountInIndexBuffer") {
            draw.vertexCountInIndexBuffer += errorScale;
        } else if (drawCallTestParameter == "indexCount") {
            draw.indexCount += errorScale;
        } else if (drawCallTestParameter == "firstIndex") {
            draw.firstIndex += errorScale;
        } else if (drawCallTestParameter == "baseVertex") {
            draw.baseVertex += errorScale;
        } else if (drawCallTestParameter == "instanceCount") {
            draw.instanceCount += errorScale;
        } else {
            t.fail("unknown drawCallTestParameter: " + drawCallTestParameter);
        }

        // Offset the range checks for gl_VertexIndex in the shader if we use BaseVertex
        uint32_t vertexIndexOffset = 0;
        if (drawCallTestParameter == "baseVertex") {
            vertexIndexOffset += errorScale;
        }

        doTest(t, bufferCount, attributesPerBuffer, typeInfo, validValues, vertexIndexOffset,
               numVertices, indexed, indirect, draw);
    });

} // namespace
