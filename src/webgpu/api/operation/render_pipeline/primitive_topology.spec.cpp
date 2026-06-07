// Ported from gpuweb/cts src/webgpu/api/operation/render_pipeline/primitive_topology.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Ports basic for the 5 topologies, non-indirect, no primitive-restart; indirect/restart/unaligned_vertex_count deferred.

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
    "api,operation,render_pipeline,primitive_topology",
    "Render pipeline primitive topology rasterization tests.");

constexpr uint32_t kRTSize = 56;
constexpr uint32_t kBytesPerPixel = 4;
constexpr WGPUTextureFormat kRenderTargetFormat = WGPUTextureFormat_RGBA8Unorm;
constexpr std::array<uint8_t, 4> kValidPixelColor   = {0, 255, 0, 255}; // green — covered
constexpr std::array<uint8_t, 4> kInvalidPixelColor = {0,   0, 0,   0}; // black — not covered

// Vertex shader: pass through a float4 position attribute.
constexpr std::string_view kVertexShader = R"(
@vertex fn main(@location(0) pos: vec4<f32>) -> @builtin(position) vec4<f32> {
  return pos;
}
)";

// Fragment shader: solid green.
constexpr std::string_view kFragmentShader = R"(
@fragment fn main() -> @location(0) vec4<f32> {
  return vec4<f32>(0.0, 1.0, 0.0, 1.0);
}
)";

// ---------- helpers ---------------------------------------------------------

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// Port of upstream Point2D.
struct Point2D {
    double x = 0.0;
    double y = 0.0;
};

// Port of upstream toNDC(): pixel-centre (x+0.5, y+0.5) mapped to NDC.
//   x' = (2*(x+0.5))/kRTSize - 1
//   y' = (-2*(y+0.5))/kRTSize + 1
std::array<float, 4> toNDC(const Point2D& p) {
    const float nx = static_cast<float>((2.0 * (p.x + 0.5)) / kRTSize - 1.0);
    const float ny = static_cast<float>((-2.0 * (p.y + 0.5)) / kRTSize + 1.0);
    return {nx, ny, 0.0f, 1.0f};
}

Point2D getMidpoint(const Point2D& a, const Point2D& b) {
    return {(a.x + b.x) / 2.0, (a.y + b.y) / 2.0};
}

Point2D getCentroid(const Point2D& a, const Point2D& b, const Point2D& c) {
    return {(a.x + b.x + c.x) / 3.0, (a.y + b.y + c.y) / 3.0};
}

// The 6 fixed vertex locations (v1..v6 in upstream, index 0..5 here).
const std::array<Point2D, 6> kVertexLocations = {{
    {8,  24},
    {16,  8},
    {24, 24},
    {32,  8},
    {40, 24},
    {48,  8},
}};

// A check location: pixel coordinate + expected color.
struct LocationCheck {
    Point2D coord;
    std::array<uint8_t, 4> expected;
};

// Build the vertex buffer data: 6 vertices, each as 4 floats (NDC x,y,z,w).
std::vector<float> generateVertexBuffer() {
    std::vector<float> data;
    data.reserve(6 * 4);
    for (const Point2D& p : kVertexLocations) {
        std::array<float, 4> v = toNDC(p);
        data.push_back(v[0]);
        data.push_back(v[1]);
        data.push_back(v[2]);
        data.push_back(v[3]);
    }
    return data;
}

// ---------- per-topology location sets ------------------------------------

// Each helper returns valid or invalid check locations for the relevant topology.

// point-list: valid = the 6 vertex pixel locations.
std::vector<LocationCheck> locationPoint(bool valid) {
    std::vector<LocationCheck> result;
    const std::array<uint8_t, 4> color = valid ? kValidPixelColor : kInvalidPixelColor;
    for (const Point2D& p : kVertexLocations) {
        result.push_back({p, color});
    }
    return result;
}

// line-list: valid = midpoints of {v1,v2}, {v3,v4}, {v5,v6} (pairs 0-1, 2-3, 4-5).
std::vector<LocationCheck> locationLine(bool valid) {
    const std::array<uint8_t, 4> color = valid ? kValidPixelColor : kInvalidPixelColor;
    return {
        {getMidpoint(kVertexLocations[0], kVertexLocations[1]), color},
        {getMidpoint(kVertexLocations[2], kVertexLocations[3]), color},
        {getMidpoint(kVertexLocations[4], kVertexLocations[5]), color},
    };
}

// line-strip extra segments: midpoints of {v2,v3}, {v4,v5} (adjacent pairs 1-2, 3-4).
std::vector<LocationCheck> locationLineStrip(bool valid) {
    const std::array<uint8_t, 4> color = valid ? kValidPixelColor : kInvalidPixelColor;
    return {
        {getMidpoint(kVertexLocations[1], kVertexLocations[2]), color},
        {getMidpoint(kVertexLocations[3], kVertexLocations[4]), color},
    };
}

// triangle-list: valid = centroids of {v1,v2,v3}, {v4,v5,v6} (triples 0-1-2, 3-4-5).
std::vector<LocationCheck> locationTriangleList(bool valid) {
    const std::array<uint8_t, 4> color = valid ? kValidPixelColor : kInvalidPixelColor;
    return {
        {getCentroid(kVertexLocations[0], kVertexLocations[1], kVertexLocations[2]), color},
        {getCentroid(kVertexLocations[3], kVertexLocations[4], kVertexLocations[5]), color},
    };
}

// triangle-strip extra triangles: centroids of {v2,v3,v4}, {v3,v4,v5} (1-2-3, 2-3-4).
std::vector<LocationCheck> locationTriangleStrip(bool valid) {
    const std::array<uint8_t, 4> color = valid ? kValidPixelColor : kInvalidPixelColor;
    return {
        {getCentroid(kVertexLocations[1], kVertexLocations[2], kVertexLocations[3]), color},
        {getCentroid(kVertexLocations[2], kVertexLocations[3], kVertexLocations[4]), color},
    };
}

// Port of upstream getDefaultTestLocations(topology) — non-restart path.
std::vector<LocationCheck> getDefaultTestLocations(std::string_view topology) {
    std::vector<LocationCheck> result;
    auto append = [&](std::vector<LocationCheck> v) {
        for (auto& c : v) { result.push_back(std::move(c)); }
    };

    if (topology == "point-list") {
        // valid: point; invalid: lineStrip, triangleList, triangleStrip
        append(locationPoint(true));
        append(locationLineStrip(false));
        append(locationTriangleList(false));
        append(locationTriangleStrip(false));
    } else if (topology == "line-list") {
        // valid: line; invalid: lineStrip, triangleList, triangleStrip
        append(locationLine(true));
        append(locationLineStrip(false));
        append(locationTriangleList(false));
        append(locationTriangleStrip(false));
    } else if (topology == "line-strip") {
        // valid: line + lineStrip; invalid: triangleList, triangleStrip
        append(locationLine(true));
        append(locationLineStrip(true));
        append(locationTriangleList(false));
        append(locationTriangleStrip(false));
    } else if (topology == "triangle-list") {
        // valid: triangleList; invalid: triangleStrip
        append(locationTriangleList(true));
        append(locationTriangleStrip(false));
    } else if (topology == "triangle-strip") {
        // valid: triangleList + triangleStrip
        append(locationTriangleList(true));
        append(locationTriangleStrip(true));
    }

    return result;
}

// ---------- enum helpers ---------------------------------------------------

WGPUPrimitiveTopology toTopology(std::string_view s) {
    if (s == "point-list")     { return WGPUPrimitiveTopology_PointList; }
    if (s == "line-list")      { return WGPUPrimitiveTopology_LineList; }
    if (s == "line-strip")     { return WGPUPrimitiveTopology_LineStrip; }
    if (s == "triangle-list")  { return WGPUPrimitiveTopology_TriangleList; }
    if (s == "triangle-strip") { return WGPUPrimitiveTopology_TriangleStrip; }
    std::abort();
}

// ---------- GPU helpers ----------------------------------------------------

WGPUTexture createRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size = WGPUExtent3D{kRTSize, kRTSize, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount = 1;
    desc.dimension = WGPUTextureDimension_2D;
    desc.format = kRenderTargetFormat;
    desc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    return t.createTextureTracked(desc);
}

WGPURenderPipeline createPipeline(AllFeaturesMaxLimitsGpuTest& t, std::string_view topology) {
    WGPUShaderModule vertexModule  = t.createShaderModuleTracked(kVertexShader);
    WGPUShaderModule fragmentModule = t.createShaderModuleTracked(kFragmentShader);

    WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
    attribute.format = WGPUVertexFormat_Float32x4;
    attribute.offset = 0;
    attribute.shaderLocation = 0;

    WGPUVertexBufferLayout vertexBufferLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexBufferLayout.arrayStride = 4 * sizeof(float);
    vertexBufferLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexBufferLayout.attributeCount = 1;
    vertexBufferLayout.attributes = &attribute;

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = kRenderTargetFormat;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fragmentModule;
    fragment.entryPoint = stringView("main");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = layout;
    desc.vertex.module = vertexModule;
    desc.vertex.entryPoint = stringView("main");
    desc.vertex.bufferCount = 1;
    desc.vertex.buffers = &vertexBufferLayout;
    desc.primitive.topology = toTopology(topology);
    // stripIndexFormat must be set for strip topologies.
    if (topology == "line-strip" || topology == "triangle-strip") {
        desc.primitive.stripIndexFormat = WGPUIndexFormat_Uint32;
    } else {
        desc.primitive.stripIndexFormat = WGPUIndexFormat_Undefined;
    }
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

void submitEncoder(AllFeaturesMaxLimitsGpuTest& t, WGPUCommandEncoder encoder) {
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
}

// Readback the 56×56 render target and check each test location.
void verifyResult(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUTexture texture,
    const std::vector<LocationCheck>& locations) {
    // bytesPerRow = align(56*4, 256) = 256
    const uint32_t bytesPerRow = static_cast<uint32_t>(
        alignTo(kRTSize * kBytesPerPixel, kBytesPerRowAlignment));
    const uint64_t byteLength = alignTo(
        static_cast<uint64_t>(bytesPerRow) * (kRTSize - 1)
            + static_cast<uint64_t>(kRTSize) * kBytesPerPixel,
        kBufferCopyAlignment);

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = byteLength;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(bufDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyTextureToBuffer(
        encoder,
        texture,
        readback,
        bytesPerRow,
        WGPUExtent3D{kRTSize, kRTSize, 1});
    submitEncoder(t, encoder);

    // Capture by value for the lambda.
    const uint32_t bpr = bytesPerRow;

    t.expectGPUBufferValuesPassCheck(
        readback,
        [locations, bpr](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            for (const LocationCheck& loc : locations) {
                const uint32_t col = static_cast<uint32_t>(std::floor(loc.coord.x));
                const uint32_t row = static_cast<uint32_t>(std::floor(loc.coord.y));
                const uint64_t offset =
                    static_cast<uint64_t>(row) * bpr
                    + static_cast<uint64_t>(col) * kBytesPerPixel;
                if (offset + kBytesPerPixel > len) {
                    std::ostringstream msg;
                    msg << "rgba8unorm pixel offset out of range: " << offset
                        << " (col=" << col << ", row=" << row << ")";
                    return msg.str();
                }
                for (uint32_t ch = 0; ch < kBytesPerPixel; ++ch) {
                    const uint8_t got      = actual[offset + ch];
                    const uint8_t expected = loc.expected[ch];
                    if (got != expected) {
                        std::ostringstream msg;
                        msg << "rgba8unorm mismatch at (" << col << ", " << row
                            << ") channel " << ch
                            << ": expected " << static_cast<int>(expected)
                            << ", got " << static_cast<int>(got);
                        return msg.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        static_cast<size_t>(byteLength));
}

// ---------- test body ------------------------------------------------------

void runBasic(AllFeaturesMaxLimitsGpuTest& t) {
    const std::string topology = t.param<std::string>("topology");

    // Build vertex buffer: 6 vertices, each 4 floats (NDC x,y,z,w).
    const std::vector<float> vertexData = generateVertexBuffer();
    WGPUBuffer vb = t.makeBufferWithContents(
        vertexData.data(),
        vertexData.size() * sizeof(float),
        WGPUBufferUsage_Vertex);

    WGPUTexture target = createRenderTarget(t);
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(target, viewDesc);

    WGPURenderPipeline pipeline = createPipeline(t, topology);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0}; // clear black

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = nullptr;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, vb, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    submitEncoder(t, encoder);

    const std::vector<LocationCheck> locations = getDefaultTestLocations(topology);
    verifyResult(t, target, locations);
}

CTS_TEST(g, "basic")
    .params([](ParamsBuilder u) {
        return u.combine("topology", {
            "point-list",
            "line-list",
            "line-strip",
            "triangle-list",
            "triangle-strip",
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        runBasic(t);
    });

} // namespace
