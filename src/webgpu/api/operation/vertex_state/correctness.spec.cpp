// Ported from gpuweb/cts src/webgpu/api/operation/vertex_state/correctness.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports the vertex_format_to_shader_format_conversion representative-format subset (9 formats, 1 case each).

#include <array>
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
    "api,operation,vertex_state,correctness",
    "Vertex state correctness operation tests.");

// ------------------------------------------------------------------
// Scalar type of the decoded vertex attribute as seen by WGSL.
// ------------------------------------------------------------------
enum class WgslScalarType { F32, U32, I32 };

// ------------------------------------------------------------------
// Per-format test descriptor.
// ------------------------------------------------------------------
struct VertexFormatCase {
    // Display name (used as the "format" param value).
    std::string name;
    // WebGPU vertex format enum.
    WGPUVertexFormat format;
    // WGSL type for vec4<T> in the shader.
    WgslScalarType scalarType;
    // Raw 16-byte vertex data (arrayStride = 16; trailing bytes are zero).
    std::array<uint8_t, 16> vertexBytes;
    // Expected decoded values.  For u32/i32 formats the float bit-pattern
    // stores the raw integer bits so the check lambda can memcpy them back.
    std::array<float, 4> expected;
    // Per-channel tolerance.  0.0 = exact (bit-equal) comparison.
    float tolerance;
};

// ------------------------------------------------------------------
// Helper: pack a little-endian u32 into 4 bytes.
// ------------------------------------------------------------------
static void packU32LE(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>((v)       & 0xFFu);
    dst[1] = static_cast<uint8_t>((v >>  8) & 0xFFu);
    dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
    dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
}

// ------------------------------------------------------------------
// Helper: pack a little-endian f32 into 4 bytes.
// ------------------------------------------------------------------
static void packF32LE(uint8_t* dst, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    packU32LE(dst, bits);
}

// ------------------------------------------------------------------
// Helper: pack a little-endian i32 into 4 bytes.
// ------------------------------------------------------------------
static void packI32LE(uint8_t* dst, int32_t v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, 4);
    packU32LE(dst, bits);
}

// ------------------------------------------------------------------
// Build the format-case table.  All encoding is done programmatically.
// ------------------------------------------------------------------
static std::vector<VertexFormatCase> makeFormatTable() {
    std::vector<VertexFormatCase> table;
    table.reserve(9);

    // ---- float32x4 ----
    // 4×f32 LE: 1.0, -2.0, 3.5, -4.25  →  exact.
    {
        VertexFormatCase c;
        c.name      = "float32x4";
        c.format    = WGPUVertexFormat_Float32x4;
        c.scalarType = WgslScalarType::F32;
        c.vertexBytes = {};
        packF32LE(c.vertexBytes.data() +  0,  1.0f);
        packF32LE(c.vertexBytes.data() +  4, -2.0f);
        packF32LE(c.vertexBytes.data() +  8,  3.5f);
        packF32LE(c.vertexBytes.data() + 12, -4.25f);
        c.expected  = {1.0f, -2.0f, 3.5f, -4.25f};
        c.tolerance = 0.0f;
        table.push_back(c);
    }

    // ---- float16x4 ----
    // 4×f16 LE: 0x3C00 (1.0), 0x4000 (2.0), 0x3800 (0.5), 0xBC00 (-1.0)
    // tolerance 1e-3.
    {
        VertexFormatCase c;
        c.name      = "float16x4";
        c.format    = WGPUVertexFormat_Float16x4;
        c.scalarType = WgslScalarType::F32;
        c.vertexBytes = {};
        const uint16_t halves[4] = {0x3C00u, 0x4000u, 0x3800u, 0xBC00u};
        for (int i = 0; i < 4; ++i) {
            std::memcpy(c.vertexBytes.data() + i * 2, &halves[i], 2);
        }
        c.expected  = {1.0f, 2.0f, 0.5f, -1.0f};
        c.tolerance = 1e-3f;
        table.push_back(c);
    }

    // ---- uint32x4 ----
    // 4×u32 LE: 1, 0x12345678, 0, 0xFFFFFFFF  →  exact.
    // The expected array stores each uint32 bit-pattern reinterpreted as float.
    {
        VertexFormatCase c;
        c.name      = "uint32x4";
        c.format    = WGPUVertexFormat_Uint32x4;
        c.scalarType = WgslScalarType::U32;
        c.vertexBytes = {};
        const uint32_t uvals[4] = {1u, 0x12345678u, 0u, 0xFFFFFFFFu};
        for (int i = 0; i < 4; ++i) {
            packU32LE(c.vertexBytes.data() + i * 4, uvals[i]);
            float f = 0.0f;
            std::memcpy(&f, &uvals[i], 4);
            c.expected[i] = f;
        }
        c.tolerance = 0.0f;
        table.push_back(c);
    }

    // ---- uint8x4 ----
    // 4×u8: 10, 20, 30, 40  →  exact.
    {
        VertexFormatCase c;
        c.name      = "uint8x4";
        c.format    = WGPUVertexFormat_Uint8x4;
        c.scalarType = WgslScalarType::U32;
        c.vertexBytes = {};
        const uint8_t ubytes[4] = {10u, 20u, 30u, 40u};
        for (int i = 0; i < 4; ++i) {
            c.vertexBytes[i] = ubytes[i];
            uint32_t uv = ubytes[i];
            float f = 0.0f;
            std::memcpy(&f, &uv, 4);
            c.expected[i] = f;
        }
        c.tolerance = 0.0f;
        table.push_back(c);
    }

    // ---- sint32x4 ----
    // 4×i32 LE: -1, 2147483647, -2147483648, 42  →  exact.
    {
        VertexFormatCase c;
        c.name      = "sint32x4";
        c.format    = WGPUVertexFormat_Sint32x4;
        c.scalarType = WgslScalarType::I32;
        c.vertexBytes = {};
        const int32_t ivals[4] = {-1, 2147483647, -2147483648, 42};
        for (int i = 0; i < 4; ++i) {
            packI32LE(c.vertexBytes.data() + i * 4, ivals[i]);
            float f = 0.0f;
            std::memcpy(&f, &ivals[i], 4);
            c.expected[i] = f;
        }
        c.tolerance = 0.0f;
        table.push_back(c);
    }

    // ---- sint8x4 ----
    // 4×i8: -10, 20, -30, 127  →  exact.
    {
        VertexFormatCase c;
        c.name      = "sint8x4";
        c.format    = WGPUVertexFormat_Sint8x4;
        c.scalarType = WgslScalarType::I32;
        c.vertexBytes = {};
        const int8_t ibytes[4] = {-10, 20, -30, 127};
        for (int i = 0; i < 4; ++i) {
            c.vertexBytes[i] = static_cast<uint8_t>(ibytes[i]);
            int32_t iv = ibytes[i];
            float f = 0.0f;
            std::memcpy(&f, &iv, 4);
            c.expected[i] = f;
        }
        c.tolerance = 0.0f;
        table.push_back(c);
    }

    // ---- unorm8x4 ----
    // 4×u8: 0, 51, 128, 255  →  tolerance 1/255.
    // unorm decode: v / 255.
    {
        VertexFormatCase c;
        c.name      = "unorm8x4";
        c.format    = WGPUVertexFormat_Unorm8x4;
        c.scalarType = WgslScalarType::F32;
        c.vertexBytes = {};
        const uint8_t ubytes[4] = {0u, 51u, 128u, 255u};
        for (int i = 0; i < 4; ++i) {
            c.vertexBytes[i] = ubytes[i];
        }
        c.expected = {
            0.0f   / 255.0f,
            51.0f  / 255.0f,
            128.0f / 255.0f,
            255.0f / 255.0f,
        };
        c.tolerance = 1.0f / 255.0f;
        table.push_back(c);
    }

    // ---- snorm8x4 ----
    // 4×i8: -128, -127, 0, 127  →  tolerance 1/127.
    // snorm8 decode: max(v / 127, -1).
    {
        VertexFormatCase c;
        c.name      = "snorm8x4";
        c.format    = WGPUVertexFormat_Snorm8x4;
        c.scalarType = WgslScalarType::F32;
        c.vertexBytes = {};
        const int8_t ibytes[4] = {-128, -127, 0, 127};
        for (int i = 0; i < 4; ++i) {
            c.vertexBytes[i] = static_cast<uint8_t>(ibytes[i]);
        }
        auto snorm8 = [](int8_t v) -> float {
            float d = static_cast<float>(v) / 127.0f;
            return d < -1.0f ? -1.0f : d;
        };
        c.expected = {snorm8(ibytes[0]), snorm8(ibytes[1]), snorm8(ibytes[2]), snorm8(ibytes[3])};
        c.tolerance = 1.0f / 127.0f;
        table.push_back(c);
    }

    // ---- unorm10_10_10_2 ----
    // 1×u32 LE: R=1023, G=512, B=256, A=3  packed as R|(G<<10)|(B<<20)|(A<<30).
    // unorm decode: R/1023, G/1023, B/1023, A/3.  Tolerance 1/1023.
    {
        VertexFormatCase c;
        c.name      = "unorm10_10_10_2";
        c.format    = WGPUVertexFormat_Unorm10_10_10_2;
        c.scalarType = WgslScalarType::F32;
        c.vertexBytes = {};
        const uint32_t R = 1023u, G = 512u, B = 256u, A = 3u;
        const uint32_t packed = R | (G << 10u) | (B << 20u) | (A << 30u);
        packU32LE(c.vertexBytes.data(), packed);
        c.expected = {
            static_cast<float>(R) / 1023.0f,
            static_cast<float>(G) / 1023.0f,
            static_cast<float>(B) / 1023.0f,
            static_cast<float>(A) / 3.0f,
        };
        c.tolerance = 1.0f / 1023.0f;
        table.push_back(c);
    }

    return table;
}

// Build the table once and keep it in a static.
static const std::vector<VertexFormatCase>& formatTable() {
    static const std::vector<VertexFormatCase> table = makeFormatTable();
    return table;
}

// Return the VertexFormatCase for a given format-name param string.
static const VertexFormatCase& findCase(const std::string& name) {
    for (const auto& c : formatTable()) {
        if (c.name == name) {
            return c;
        }
    }
    std::abort();
}

// ------------------------------------------------------------------
// WGSL shader — one template per scalar type.
// ------------------------------------------------------------------
static std::string makeShader(WgslScalarType scalarType) {
    const char* T = nullptr;
    switch (scalarType) {
        case WgslScalarType::F32: T = "f32"; break;
        case WgslScalarType::U32: T = "u32"; break;
        case WgslScalarType::I32: T = "i32"; break;
    }
    std::ostringstream src;
    src << "struct VOut {\n"
        << "  @builtin(position) pos : vec4<f32>,\n"
        << "  @location(0) @interpolate(flat) attr : vec4<" << T << ">,\n"
        << "};\n"
        << "@vertex fn vs(@location(0) a : vec4<" << T << ">) -> VOut {\n"
        << "  var o : VOut;\n"
        << "  o.pos = vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
        << "  o.attr = a;\n"
        << "  return o;\n"
        << "}\n"
        << "@group(0) @binding(0) var<storage, read_write> out : array<" << T << ", 4>;\n"
        << "@fragment fn fs(i : VOut) -> @location(0) vec4<f32> {\n"
        << "  out[0] = i.attr.x;\n"
        << "  out[1] = i.attr.y;\n"
        << "  out[2] = i.attr.z;\n"
        << "  out[3] = i.attr.w;\n"
        << "  return vec4<f32>(0.0);\n"
        << "}\n";
    return src.str();
}

static WGPUStringView sv(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// ------------------------------------------------------------------
// Run a single vertex-format decode test case.
// ------------------------------------------------------------------
static void runVertexFormatCase(AllFeaturesMaxLimitsGpuTest& t, const VertexFormatCase& fc) {
    // Storage buffer: 16 bytes (array<T, 4>), zero-initialized.
    // Zero-init is deliberate — a no-write decode bug must surface as zeros.
    WGPUBufferDescriptor storageBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    storageBufDesc.size  = 16;
    storageBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer storageBuf = t.createBufferTracked(storageBufDesc);

    // Vertex buffer: 16 bytes with the encoded input.
    WGPUBuffer vertexBuf = t.makeBufferWithContents(
        fc.vertexBytes.data(),
        fc.vertexBytes.size(),
        WGPUBufferUsage_Vertex);

    // Color attachment: 1×1 rgba8unorm — needed so the point rasterizes
    // and the fragment shader executes.
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_RenderAttachment;
    WGPUTexture colorTex  = t.createTextureTracked(texDesc);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView colorView = t.createViewTracked(colorTex, viewDesc);

    // Bind group layout: FRAGMENT-visibility read_write storage buffer.
    WGPUBindGroupLayoutEntry bglEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
    bglEntry.binding     = 0;
    bglEntry.visibility  = WGPUShaderStage_Fragment;
    bglEntry.buffer      = WGPU_BUFFER_BINDING_LAYOUT_INIT;
    bglEntry.buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    bglDesc.entryCount = 1;
    bglDesc.entries    = &bglEntry;
    WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);

    // Pipeline layout.
    WGPUPipelineLayoutDescriptor plDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts     = &bgl;
    WGPUPipelineLayout pipelineLayout = t.createPipelineLayoutTracked(plDesc);

    // Shader module.
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(makeShader(fc.scalarType));

    // Vertex attribute + buffer layout.
    WGPUVertexAttribute attr = WGPU_VERTEX_ATTRIBUTE_INIT;
    attr.format         = fc.format;
    attr.offset         = 0;
    attr.shaderLocation = 0;

    WGPUVertexBufferLayout vbl = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vbl.arrayStride    = 16;
    vbl.stepMode       = WGPUVertexStepMode_Vertex;
    vbl.attributeCount = 1;
    vbl.attributes     = &attr;

    // Color target state.
    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module      = shaderModule;
    fragment.entryPoint  = sv("fs");
    fragment.targetCount = 1;
    fragment.targets     = &colorTarget;

    // Render pipeline.
    WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rpDesc.layout             = pipelineLayout;
    rpDesc.vertex.module      = shaderModule;
    rpDesc.vertex.entryPoint  = sv("vs");
    rpDesc.vertex.bufferCount = 1;
    rpDesc.vertex.buffers     = &vbl;
    rpDesc.primitive.topology = WGPUPrimitiveTopology_PointList;
    rpDesc.multisample.count  = 1;
    rpDesc.fragment            = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(rpDesc);

    // Bind group.
    WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bgEntry.binding = 0;
    bgEntry.buffer  = storageBuf;
    bgEntry.offset  = 0;
    bgEntry.size    = 16;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &bgEntry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

    // Render pass + draw.
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view       = colorView;
    colorAttachment.loadOp     = WGPULoadOp_Clear;
    colorAttachment.storeOp    = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vertexBuf, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 1, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    // ------------------------------------------------------------------
    // Readback: compare 16-byte storage buffer against expected values.
    // ------------------------------------------------------------------
    const WgslScalarType scalarType = fc.scalarType;
    const std::array<float, 4> expected = fc.expected;
    const float tolerance = fc.tolerance;
    const std::string caseName = fc.name;

    t.expectGPUBufferValuesPassCheck(
        storageBuf,
        [scalarType, expected, tolerance, caseName](
            const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < 16) {
                std::ostringstream msg;
                msg << caseName << ": readback buffer too small (" << len << " < 16)";
                return msg.str();
            }
            for (int ch = 0; ch < 4; ++ch) {
                const size_t off = static_cast<size_t>(ch) * 4;
                std::ostringstream msg;
                if (scalarType == WgslScalarType::U32) {
                    uint32_t got = 0, exp = 0;
                    std::memcpy(&got, actual + off, 4);
                    std::memcpy(&exp, &expected[ch], 4);
                    if (got != exp) {
                        msg << caseName << ": component[" << ch
                            << "]: expected u32 " << exp << ", got " << got;
                        return msg.str();
                    }
                } else if (scalarType == WgslScalarType::I32) {
                    int32_t got = 0, exp = 0;
                    std::memcpy(&got, actual + off, 4);
                    std::memcpy(&exp, &expected[ch], 4);
                    if (got != exp) {
                        msg << caseName << ": component[" << ch
                            << "]: expected i32 " << exp << ", got " << got;
                        return msg.str();
                    }
                } else {
                    float got = 0.0f;
                    std::memcpy(&got, actual + off, 4);
                    const float exp = expected[ch];
                    if (tolerance == 0.0f) {
                        uint32_t gotBits = 0, expBits = 0;
                        std::memcpy(&gotBits, &got, 4);
                        std::memcpy(&expBits, &exp, 4);
                        if (gotBits != expBits) {
                            msg << caseName << ": component[" << ch
                                << "]: expected f32 " << exp << ", got " << got;
                            return msg.str();
                        }
                    } else {
                        const float diff = got > exp ? got - exp : exp - got;
                        if (diff > tolerance) {
                            msg << caseName << ": component[" << ch
                                << "]: expected f32 " << exp << " ± " << tolerance
                                << ", got " << got << " (diff " << diff << ")";
                            return msg.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0,
        16);
}

// ------------------------------------------------------------------
// Build the params value list from the format table.
// ------------------------------------------------------------------
static std::vector<Value> formatValues() {
    std::vector<Value> values;
    for (const auto& c : formatTable()) {
        values.emplace_back(c.name);
    }
    return values;
}

// ------------------------------------------------------------------
// Single parametric test — one case per representative format.
// ------------------------------------------------------------------
CTS_TEST(g, "vertex_format_to_shader_format_conversion")
    .params([](ParamsBuilder u) {
        return u.combine("format", formatValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string formatName = t.param<std::string>("format");
        runVertexFormatCase(t, findCase(formatName));
    });

} // namespace
