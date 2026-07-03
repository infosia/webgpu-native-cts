// Ported from gpuweb/cts src/webgpu/shader/execution/shader_io/fragment_builtins.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for the reviewer):
// - Param encoding: the harness Value type supports scalars/strings only, so
//   upstream's structured params are encoded as strings (same convention as
//   api/operation/buffers/map.spec.cpp):
//     * nearFar: [0, 1]            -> "[0,1]" / "[0.25,0.75]"
//     * interpolation: {type, sampling} -> "type" or "type,sampling"
//       (e.g. {type:'perspective', sampling:'center'} -> "perspective,center";
//        {type:'flat'} -> "flat")
//     * size: [15, 15]             -> "[15,15]" (subgroup tests)
//     * vertices: [...]            -> "[0.3,0.3,...]" (discarded_primitves)
//   Test names, param names, param order and value order mirror upstream.
// - showExpected() (a t.debug() pretty-printer) is omitted; failure messages
//   list the failing samples instead.
// - Upstream caches the multisample-copy pipeline and the flat/either
//   provoking-vertex probe per device (WeakMap); this port recreates them per
//   case (correctness-identical, slightly slower).
// - skipIfInterpolationTypeOrSamplingNotSupported and the t.isCompatibility
//   skips (inputs,sample_index / inputs,sample_mask) are compat-mode-only
//   upstream; this harness only runs core mode, so they are no-ops here.
// - checkSampleRectsApproximatelyEqual is implemented directly on the f32
//   readback data with upstream's rgba32float ulpFromZero comparison
//   (maxDiffULPsForFloatFormat semantics) instead of going through TexelView.
// - readGPUBufferRangeTyped / mapAsync readbacks map to
//   expectGPUBufferValuesPassCheck (the harness does the staging copy).
// - Subgroup limits come from WGPUAdapterInfo.subgroupMinSize/subgroupMaxSize
//   via a temporary WGPUInstance + default adapter (wgpuAdapterGetInfo), cached
//   per process — same pattern as compute_builtins.spec.cpp querySubgroupRange.
//   wgpuDeviceGetAdapterInfo is absent from yawgpu and panics in wgpu-native.
// - Output buffers rely on WebGPU zero-init / clear loads; nothing is
//   pre-filled with expected values. The primitive_index render target is
//   cleared to 0xffffffff exactly like upstream so primitive id 0 is testable.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,shader_io,fragment_builtins",
    R"(Test fragment shader builtin variables and inter-stage variables

* test builtin(position)
* test @interpolate
* test builtin(sample_index)
* test builtin(front_facing)
* test builtin(sample_mask)

Note: @interpolate settings and sample_index affect whether or not the fragment shader
is evaluated per-fragment or per-sample. With @interpolate(, sample) or usage of
@builtin(sample_index) the fragment shader should be executed per-sample.

* sample_mask output is tested in
  src/webgpu/api/operation/render_pipeline/sample_mask.spec.ts

* frag_depth output is tested in
  src/webgpu/api/operation/rendering/depth_clip_clamp.spec.ts
)");

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// Small math helpers (ports of util/math.ts pieces used by this file)
// ---------------------------------------------------------------------------

using Vec = std::vector<double>;
using VecList = std::vector<Vec>;

double dotProduct(const Vec& a, const Vec& b) {
    double sum = 0.0;
    for (size_t i = 0; i < a.size(); ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

Vec subtractVectors(const Vec& a, const Vec& b) {
    Vec out(a.size());
    for (size_t i = 0; i < a.size(); ++i) {
        out[i] = a[i] - b[i];
    }
    return out;
}

uint32_t alignUp(uint32_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

// Formats a double the way JS template literals do for the values used here
// (default ostream formatting: 6 significant digits, no trailing zeros).
std::string fmtNum(double v) {
    std::ostringstream out;
    out << v;
    return out.str();
}

// Parses a bracketed comma-separated number list like "[0.25,0.75]".
Vec parseNumberList(const std::string& text) {
    Vec out;
    size_t i = 0;
    const size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.') {
            size_t end = i;
            while (end < n && text[end] != ',' && text[end] != ']') {
                ++end;
            }
            out.push_back(std::stod(text.substr(i, end - i)));
            i = end;
        } else {
            ++i;
        }
    }
    return out;
}

// Decoded form of the upstream `interpolation: {type, sampling}` param.
struct InterpolationAttr {
    std::string type;
    std::string sampling; // empty when upstream omits `sampling`
};

InterpolationAttr parseInterpolation(const std::string& text) {
    const size_t comma = text.find(',');
    if (comma == std::string::npos) {
        return InterpolationAttr{text, ""};
    }
    return InterpolationAttr{text.substr(0, comma), text.substr(comma + 1)};
}

// ---------------------------------------------------------------------------
// Multisample fragment offsets (port of multisample_info.ts, the sampleCounts
// used by this file: 1 and 4)
// ---------------------------------------------------------------------------

const VecList& getMultisampleFragmentOffsets(uint32_t sampleCount) {
    // Sample positions on a 16x16 grid, divided by 16 (see upstream table,
    // based on the D3D11 standard sample positions).
    static const VecList kOffsets1 = {{8.0 / 16, 8.0 / 16}};
    static const VecList kOffsets4 = {
        {6.0 / 16, 2.0 / 16},
        {14.0 / 16, 6.0 / 16},
        {2.0 / 16, 10.0 / 16},
        {10.0 / 16, 14.0 / 16},
    };
    if (sampleCount == 1) {
        return kOffsets1;
    }
    if (sampleCount == 4) {
        return kOffsets4;
    }
    std::abort(); // unreachable for this file's params
}

// ---------------------------------------------------------------------------
// Geometry helpers (direct ports)
// ---------------------------------------------------------------------------

/* column constants */
constexpr size_t kX = 0;
constexpr size_t kY = 1;
constexpr size_t kZ = 2;
constexpr size_t kW = 3;

Vec getColumn(const VecList& values, size_t colNum) {
    Vec out;
    out.reserve(values.size());
    for (const Vec& v : values) {
        out.push_back(v[colNum]);
    }
    return out;
}

double linearInterpolation(const Vec& baryCoords, const Vec& interCoords) {
    return dotProduct(baryCoords, interCoords);
}

double perspectiveInterpolation(
    const Vec& barycentricCoords,
    const VecList& clipSpaceTriangleCoords,
    const Vec& interCoords) {
    const double a = barycentricCoords[0];
    const double b = barycentricCoords[1];
    const double c = barycentricCoords[2];
    const double fa = interCoords[0];
    const double fb = interCoords[1];
    const double fc = interCoords[2];
    const double wa = clipSpaceTriangleCoords[0][kW];
    const double wb = clipSpaceTriangleCoords[1][kW];
    const double wc = clipSpaceTriangleCoords[2][kW];

    return ((a * fa) / wa + (b * fb) / wb + (c * fc) / wc) / (a / wa + b / wb + c / wc);
}

Vec clipSpaceToNDC(const Vec& point) {
    Vec out(point.size());
    for (size_t i = 0; i < point.size(); ++i) {
        out[i] = point[i] / point[kW];
    }
    return out;
}

Vec ndcToWindow(const Vec& ndcPoint, const std::array<double, 6>& viewport) {
    const double xd = ndcPoint[0];
    const double yd = ndcPoint[1];
    const double zd = ndcPoint[2];
    const double px = viewport[2];
    const double py = viewport[3];
    const double ox = viewport[0] + px / 2;
    const double oy = viewport[1] + py / 2;
    const double zNear = viewport[4];
    const double zFar = viewport[5];
    return Vec{
        px / 2 * xd + ox,
        -py / 2 * yd + oy,
        zd * (zFar - zNear) + zNear,
    };
}

Vec calcBarycentricCoordinates(const VecList& trianglePoints, const Vec& p) {
    const Vec& a = trianglePoints[0];
    const Vec& b = trianglePoints[1];
    const Vec& c = trianglePoints[2];

    const Vec v0 = subtractVectors(b, a);
    const Vec v1 = subtractVectors(c, a);
    const Vec v2 = subtractVectors(p, a);

    const double dot00 = dotProduct(v0, v0);
    const double dot01 = dotProduct(v0, v1);
    const double dot11 = dotProduct(v1, v1);
    const double dot20 = dotProduct(v2, v0);
    const double dot21 = dotProduct(v2, v1);

    const double denom = 1 / (dot00 * dot11 - dot01 * dot01);
    const double v = (dot11 * dot20 - dot01 * dot21) * denom;
    const double w = (dot00 * dot21 - dot01 * dot20) * denom;
    const double u = 1 - v - w;

    return Vec{u, v, w};
}

bool isInsideTriangle(const Vec& barycentricCoords) {
    for (const double v : barycentricCoords) {
        if (v < 0 || v > 1) {
            return false;
        }
    }
    return true;
}

bool isTriangleClockwise(const VecList& windowPoints) {
    double sum = 0;
    for (size_t i = 0; i < 3; ++i) {
        const Vec& p0 = windowPoints[i];
        const Vec& p1 = windowPoints[(i + 1) % 3];
        sum += p0[kX] * p1[kY] - p1[kX] * p0[kY];
    }
    return sum >= 0;
}

// ---------------------------------------------------------------------------
// generateFragmentInputs (direct port)
// ---------------------------------------------------------------------------

struct FragData {
    size_t baseVertexIndex = 0;
    Vec fragmentPoint;             // 2 components
    Vec fragmentBarycentricCoords; // 3 components
    Vec sampleBarycentricCoords;   // 3 components
    const VecList* clipSpacePoints = nullptr; // all triangles' clip-space points
    const VecList* windowPoints = nullptr;    // current triangle's window points
    uint32_t sampleIndex = 0;
    uint32_t sampleMask = 0;
    bool frontFacing = false;
};

using InterpolateFn = std::function<std::array<double, 4>(const FragData&)>;

struct GenerateFragmentInputsOptions {
    uint32_t width = 0;
    uint32_t height = 0;
    std::array<double, 2> nearFar{};
    uint32_t sampleCount = 1;
    std::string frontFace; // "" when upstream passes undefined
    const VecList* clipSpacePoints = nullptr;
    InterpolateFn interpolateFn;
};

std::vector<float> generateFragmentInputs(const GenerateFragmentInputsOptions& o) {
    std::vector<float> expected(
        static_cast<size_t>(o.width) * o.height * o.sampleCount * 4, 0.0f);

    const std::array<double, 6> viewport = {
        0, 0, static_cast<double>(o.width), static_cast<double>(o.height),
        o.nearFar[0], o.nearFar[1]};

    const VecList& clipSpacePoints = *o.clipSpacePoints;

    // For each triangle
    for (size_t vertexIndex = 0; vertexIndex + 2 < clipSpacePoints.size(); vertexIndex += 3) {
        VecList ndcPoints;
        for (size_t i = 0; i < 3; ++i) {
            ndcPoints.push_back(clipSpaceToNDC(clipSpacePoints[vertexIndex + i]));
        }
        VecList windowPoints;
        for (const Vec& p : ndcPoints) {
            windowPoints.push_back(ndcToWindow(p, viewport));
        }
        VecList windowPoints2D;
        for (const Vec& p : windowPoints) {
            windowPoints2D.push_back(Vec{p[0], p[1]});
        }

        const bool cw = isTriangleClockwise(windowPoints2D);
        const bool frontFacing = (o.frontFace == "cw") ? cw : !cw;
        const VecList& fragmentOffsets = getMultisampleFragmentOffsets(o.sampleCount);

        for (uint32_t y = 0; y < o.height; ++y) {
            for (uint32_t x = 0; x < o.width; ++x) {
                uint32_t sampleMask = 0;
                for (uint32_t sampleIndex = 0; sampleIndex < o.sampleCount; ++sampleIndex) {
                    const uint32_t localSampleMask = 1u << sampleIndex;
                    const Vec& multisampleOffset = fragmentOffsets[sampleIndex];
                    const Vec sampleFragmentPoint = {
                        x + multisampleOffset[0], y + multisampleOffset[1]};
                    const Vec sampleBarycentricCoords =
                        calcBarycentricCoordinates(windowPoints2D, sampleFragmentPoint);

                    if (isInsideTriangle(sampleBarycentricCoords)) {
                        sampleMask |= localSampleMask;
                    }
                }

                for (uint32_t sampleIndex = 0; sampleIndex < o.sampleCount; ++sampleIndex) {
                    const Vec fragmentPoint = {x + 0.5, y + 0.5};
                    const Vec& multisampleOffset = fragmentOffsets[sampleIndex];
                    const Vec sampleFragmentPoint = {
                        x + multisampleOffset[0], y + multisampleOffset[1]};
                    const Vec fragmentBarycentricCoords =
                        calcBarycentricCoordinates(windowPoints2D, fragmentPoint);
                    const Vec sampleBarycentricCoords =
                        calcBarycentricCoordinates(windowPoints2D, sampleFragmentPoint);

                    if (isInsideTriangle(sampleBarycentricCoords)) {
                        FragData fragData;
                        fragData.baseVertexIndex = vertexIndex;
                        fragData.fragmentPoint = fragmentPoint;
                        fragData.fragmentBarycentricCoords = fragmentBarycentricCoords;
                        fragData.sampleBarycentricCoords = sampleBarycentricCoords;
                        fragData.clipSpacePoints = &clipSpacePoints;
                        fragData.windowPoints = &windowPoints;
                        fragData.sampleIndex = sampleIndex;
                        fragData.sampleMask = sampleMask;
                        fragData.frontFacing = frontFacing;

                        const std::array<double, 4> output = o.interpolateFn(fragData);

                        const size_t offset =
                            ((static_cast<size_t>(y) * o.width + x) * o.sampleCount +
                             sampleIndex) *
                            4;
                        for (size_t i = 0; i < 4; ++i) {
                            expected[offset + i] = static_cast<float>(output[i]);
                        }
                    }
                }
            }
        }
    }
    return expected;
}

// ---------------------------------------------------------------------------
// interpolateFn implementations (direct ports)
// ---------------------------------------------------------------------------

std::array<double, 4> computeFragmentPosition(const FragData& fd) {
    return {
        fd.fragmentPoint[0],
        fd.fragmentPoint[1],
        linearInterpolation(fd.fragmentBarycentricCoords, getColumn(*fd.windowPoints, kZ)),
        1 / perspectiveInterpolation(
                fd.fragmentBarycentricCoords,
                *fd.clipSpacePoints,
                getColumn(*fd.clipSpacePoints, kW)),
    };
}

std::array<double, 4> computeFragmentSampleIndex(const FragData& fd) {
    return {static_cast<double>(fd.sampleIndex), 0, 0, 0};
}

std::array<double, 4> computeFragmentFrontFacing(const FragData& fd) {
    return {fd.frontFacing ? 1.0 : 0.0, 0, 0, 0};
}

std::array<double, 4> computeSampleMask(const FragData& fd) {
    return {static_cast<double>(fd.sampleMask), 0, 0, 0};
}

// ---------------------------------------------------------------------------
// Provoking-vertex probe for @interpolate(flat, either)
// (port of inter_stage.ts getProvokingVertexForFlatInterpolationEitherSampling;
//  upstream caches per device, this port re-runs the tiny probe per use)
// ---------------------------------------------------------------------------

std::string getProvokingVertexForFlatInterpolationEitherSampling(AllFeaturesMaxLimitsGpuTest& t) {
    constexpr std::string_view kProbeShader = R"(
        struct VSOut {
          @builtin(position) position: vec4f,
          @location(0) @interpolate(flat, either) vertexIndex: u32,
        };

        @vertex fn vs(
          @builtin(vertex_index) vertexIndex : u32,
        ) -> VSOut {
          let pos = array(vec2f(-1, 3), vec2f(3, -1), vec2f(-1, -1));
          var vsOutput: VSOut;
          vsOutput.position = vec4f(pos[vertexIndex], 0, 1);
          vsOutput.vertexIndex = vertexIndex;
          return vsOutput;
        }

        @fragment fn fs(@location(0) @interpolate(flat, either) vertexIndex: u32) -> @location(0) vec4u {
          return vec4u(vertexIndex);
        }
    )";

    WGPUShaderModule module = t.createShaderModuleTracked(kProbeShader);

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA8Uint;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("fs");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rpDesc.layout = nullptr; // auto
    rpDesc.vertex.module = module;
    rpDesc.vertex.entryPoint = sv("vs");
    rpDesc.fragment = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(rpDesc);

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.format = WGPUTextureFormat_RGBA8Uint;
    texDesc.size = WGPUExtent3D{1, 1, 1};
    texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture texture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, viewDesc);

    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size = 4;
    bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer buffer = t.createBufferTracked(bufDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    {
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = WGPUColor{255.0, 255.0, 255.0, 255.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
    }
    // Single-row copy: bytesPerRow may stay undefined.
    t.copyTextureToBuffer(encoder, texture, buffer, WGPU_COPY_STRIDE_UNDEFINED,
                          WGPUExtent3D{1, 1, 1});
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    uint8_t result = 0xff;
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&result](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len > 0) {
                result = actual[0];
            }
            return std::nullopt;
        },
        0, 4);

    t.expect(result == 0 || result == 2,
             "expected flat/either provoking-vertex probe result to be 0 or 2, was " +
                 std::to_string(result));
    return result == 2 ? "last" : "first";
}

// ---------------------------------------------------------------------------
// createInterStageInterpolationFn + Between0And1 variant (direct ports)
// ---------------------------------------------------------------------------

InterpolateFn createInterStageInterpolationFn(
    AllFeaturesMaxLimitsGpuTest& t,
    const VecList& interStagePoints,
    const std::string& type,
    const std::string& sampling) {
    std::string provokingVertex = "first";
    if (type == "flat" && sampling == "either") {
        provokingVertex = getProvokingVertexForFlatInterpolationEitherSampling(t);
    }

    return [interStagePoints, type, sampling, provokingVertex](
               const FragData& fd) -> std::array<double, 4> {
        VecList triangleInterStagePoints(
            interStagePoints.begin() + static_cast<std::ptrdiff_t>(fd.baseVertexIndex),
            interStagePoints.begin() + static_cast<std::ptrdiff_t>(fd.baseVertexIndex + 3));
        const Vec& barycentricCoords =
            (sampling == "center" || sampling.empty()) ? fd.fragmentBarycentricCoords
                                                       : fd.sampleBarycentricCoords;
        std::array<double, 4> out{};
        if (type == "perspective") {
            for (size_t colNum = 0; colNum < 4; ++colNum) {
                out[colNum] = perspectiveInterpolation(
                    barycentricCoords,
                    *fd.clipSpacePoints,
                    getColumn(triangleInterStagePoints, colNum));
            }
        } else if (type == "linear") {
            for (size_t colNum = 0; colNum < 4; ++colNum) {
                out[colNum] = linearInterpolation(
                    barycentricCoords, getColumn(triangleInterStagePoints, colNum));
            }
        } else if (type == "flat") {
            const Vec& p = triangleInterStagePoints[provokingVertex == "first" ? 0 : 2];
            out = {p[0], p[1], p[2], p[3]};
        } else {
            std::abort(); // unreachable
        }
        return out;
    };
}

InterpolateFn createInterStageInterpolationBetween0And1TestFn(
    AllFeaturesMaxLimitsGpuTest& t,
    const VecList& interStagePoints,
    const std::string& type,
    const std::string& sampling) {
    InterpolateFn interpolateFn = createInterStageInterpolationFn(t, interStagePoints, type, sampling);
    return [interpolateFn](const FragData& fd) -> std::array<double, 4> {
        const std::array<double, 4> interpolatedValues = interpolateFn(fd);
        bool allTrue = true;
        for (const double v : interpolatedValues) {
            allTrue = allTrue && v >= 0 && v <= 1;
        }
        return {allTrue ? 1.0 : -1.0, 0, 0, 0};
    };
}

// ---------------------------------------------------------------------------
// renderFragmentShaderInputsTo4TexturesAndReadbackValues (direct port).
// Returns the storage buffer holding f32 values laid out as
// ((y * width + x) * sampleCount + sampleIndex) * 4 + component.
// ---------------------------------------------------------------------------

struct RenderInputsOptions {
    std::string interpolationType;
    std::string interpolationSampling; // empty when upstream omits sampling
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t sampleCount = 1;
    std::string frontFace; // "", "cw", "ccw"
    std::array<double, 2> nearFar{};
    const VecList* clipSpacePoints = nullptr;
    const VecList* interStagePoints = nullptr;
    std::string fragInCode;
    std::string outputCode;
};

std::string joinVec4fList(const VecList& points) {
    std::ostringstream out;
    for (size_t i = 0; i < points.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }
        out << "vec4f(" << fmtNum(points[i][0]) << ", " << fmtNum(points[i][1]) << ", "
            << fmtNum(points[i][2]) << ", " << fmtNum(points[i][3]) << ")";
    }
    return out.str();
}

WGPUBuffer renderFragmentShaderInputsTo4Textures(
    AllFeaturesMaxLimitsGpuTest& t, const RenderInputsOptions& o) {
    const std::string interpolate =
        o.interpolationSampling.empty()
            ? o.interpolationType
            : o.interpolationType + ", " + o.interpolationSampling;

    std::ostringstream code;
    code << "      struct Uniforms {\n"
         << "        resolution: vec2f,\n"
         << "      };\n"
         << "\n"
         << "      @group(0) @binding(0) var<uniform> uni: Uniforms;\n"
         << "\n"
         << "      struct VertexOut {\n"
         << "        @builtin(position) position: vec4f,\n"
         << "        @location(0) @interpolate(" << interpolate << ") interpolatedValue: vec4f,\n"
         << "      };\n"
         << "\n"
         << "      @vertex fn vs(@builtin(vertex_index) vNdx: u32) -> VertexOut {\n"
         << "        let pos = array(\n"
         << "          " << joinVec4fList(*o.clipSpacePoints) << "\n"
         << "        );\n"
         << "        let interStage = array(\n"
         << "          " << joinVec4fList(*o.interStagePoints) << "\n"
         << "        );\n"
         << "        var v: VertexOut;\n"
         << "        v.position = pos[vNdx];\n"
         << "        v.interpolatedValue = interStage[vNdx];\n"
         << "        _ = uni;\n"
         << "        return v;\n"
         << "      }\n"
         << "\n"
         << "      struct FragmentIn {\n"
         << "        @builtin(position) position: vec4f,\n"
         << "        @location(0) @interpolate(" << interpolate << ") interpolatedValue: vec4f,\n"
         << "        " << o.fragInCode << "\n"
         << "      };\n"
         << "\n"
         << "      struct FragOut {\n"
         << "        @location(0) out0: vec4f,\n"
         << "        @location(1) out1: vec4f,\n"
         << "        @location(2) out2: vec4f,\n"
         << "        @location(3) out3: vec4f,\n"
         << "      };\n"
         << "\n"
         << "      fn u32ToRGBAUnorm(u: u32) -> vec4f {\n"
         << "        return vec4f(\n"
         << "          f32((u >> 24) & 0xFF) / 255.0,\n"
         << "          f32((u >> 16) & 0xFF) / 255.0,\n"
         << "          f32((u >>  8) & 0xFF) / 255.0,\n"
         << "          f32((u >>  0) & 0xFF) / 255.0,\n"
         << "        );\n"
         << "      }\n"
         << "\n"
         << "      @fragment fn fs(fin: FragmentIn) -> FragOut {\n"
         << "        var f: FragOut;\n"
         << "        let v = " << o.outputCode << ";\n"
         << "        let u = bitcast<vec4u>(v);\n"
         << "        f.out0 = u32ToRGBAUnorm(u[0]);\n"
         << "        f.out1 = u32ToRGBAUnorm(u[1]);\n"
         << "        f.out2 = u32ToRGBAUnorm(u[2]);\n"
         << "        f.out3 = u32ToRGBAUnorm(u[3]);\n"
         << "        _ = fin.interpolatedValue;\n"
         << "        return f;\n"
         << "      }\n";

    WGPUShaderModule module = t.createShaderModuleTracked(code.str());

    WGPUTexture textures[4];
    for (int i = 0; i < 4; ++i) {
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{o.width, o.height, 1};
        texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding |
                        WGPUTextureUsage_CopySrc;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.sampleCount = o.sampleCount;
        textures[i] = t.createTextureTracked(texDesc);
    }

    WGPUColorTargetState targets[4];
    for (int i = 0; i < 4; ++i) {
        targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
        targets[i].format = WGPUTextureFormat_RGBA8Unorm;
    }

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("fs");
    fragment.targetCount = 4;
    fragment.targets = targets;

    WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rpDesc.layout = nullptr; // auto
    rpDesc.vertex.module = module;
    rpDesc.vertex.entryPoint = sv("vs");
    rpDesc.fragment = &fragment;
    if (o.frontFace == "cw") {
        rpDesc.primitive.frontFace = WGPUFrontFace_CW;
    } else if (o.frontFace == "ccw") {
        rpDesc.primitive.frontFace = WGPUFrontFace_CCW;
    }
    rpDesc.multisample.count = o.sampleCount;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(rpDesc);

    WGPUBufferDescriptor uniDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    uniDesc.size = 8;
    uniDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    WGPUBuffer uniformBuffer = t.createBufferTracked(uniDesc);
    const float resolution[2] = {static_cast<float>(o.width), static_cast<float>(o.height)};
    t.queueWriteBuffer(uniformBuffer, 0, resolution, sizeof(resolution));

    {
        WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = uniformBuffer;
        entry.offset = 0;
        entry.size = 8;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        WGPUTextureView attachmentViews[4];
        WGPURenderPassColorAttachment colorAttachments[4];
        for (int i = 0; i < 4; ++i) {
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            attachmentViews[i] = t.createViewTracked(textures[i], viewDesc);
            colorAttachments[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachments[i].view = attachmentViews[i];
            colorAttachments[i].loadOp = WGPULoadOp_Clear;
            colorAttachments[i].storeOp = WGPUStoreOp_Store;
            colorAttachments[i].clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        }

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 4;
        passDesc.colorAttachments = colorAttachments;

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetViewport(
            pass, 0.0f, 0.0f, static_cast<float>(o.width), static_cast<float>(o.height),
            static_cast<float>(o.nearFar[0]), static_cast<float>(o.nearFar[1]));
        wgpuRenderPassEncoderDraw(
            pass, static_cast<uint32_t>(o.clipSpacePoints->size()), 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    }

    // -----------------------------------------------------------------
    // Copy the 4 (possibly multisampled) rgba8unorm textures into a single
    // f32 storage buffer (port of
    // copyRGBA8EncodedFloatTexturesToBufferIncludingMultisampledTextures).
    // -----------------------------------------------------------------
    const bool isMultisampled = o.sampleCount > 1;
    const std::string pipelineType =
        isMultisampled ? "texture_multisampled_2d" : "texture_2d";
    const std::string numSamples = isMultisampled ? "textureNumSamples(texture0)" : "1u";
    const std::string sampleIndexExpr = isMultisampled ? "sampleIndex" : "0";

    std::ostringstream copyCode;
    copyCode
        << "        @group(0) @binding(0) var texture0: " << pipelineType << "<f32>;\n"
        << "        @group(0) @binding(1) var texture1: " << pipelineType << "<f32>;\n"
        << "        @group(0) @binding(2) var texture2: " << pipelineType << "<f32>;\n"
        << "        @group(0) @binding(3) var texture3: " << pipelineType << "<f32>;\n"
        << "        @group(0) @binding(4) var<storage, read_write> buffer: array<f32>;\n"
        << "\n"
        << "        @compute @workgroup_size(1) fn cs(@builtin(global_invocation_id) id: vec3u) {\n"
        << "          let numSamples = " << numSamples << ";\n"
        << "          let dimensions = textureDimensions(texture0);\n"
        << "          let sampleIndex = id.x % numSamples;\n"
        << "          let tx = id.x / numSamples;\n"
        << "          let offset = ((id.y * dimensions.x + tx) * numSamples + sampleIndex) * 4;\n"
        << "          let r = vec4u(textureLoad(texture0, vec2u(tx, id.y), " << sampleIndexExpr << ") * 255.0);\n"
        << "          let g = vec4u(textureLoad(texture1, vec2u(tx, id.y), " << sampleIndexExpr << ") * 255.0);\n"
        << "          let b = vec4u(textureLoad(texture2, vec2u(tx, id.y), " << sampleIndexExpr << ") * 255.0);\n"
        << "          let a = vec4u(textureLoad(texture3, vec2u(tx, id.y), " << sampleIndexExpr << ") * 255.0);\n"
        << "\n"
        << "          // expand rgba8unorm values back to their byte form, add them together\n"
        << "          // and cast them to an f32 so we can recover the f32 values we encoded\n"
        << "          // in the rgba8unorm texture.\n"
        << "          buffer[offset + 0] = bitcast<f32>(dot(r, vec4u(0x1000000, 0x10000, 0x100, 0x1)));\n"
        << "          buffer[offset + 1] = bitcast<f32>(dot(g, vec4u(0x1000000, 0x10000, 0x100, 0x1)));\n"
        << "          buffer[offset + 2] = bitcast<f32>(dot(b, vec4u(0x1000000, 0x10000, 0x100, 0x1)));\n"
        << "          buffer[offset + 3] = bitcast<f32>(dot(a, vec4u(0x1000000, 0x10000, 0x100, 0x1)));\n"
        << "        }\n";

    WGPUShaderModule copyModule = t.createShaderModuleTracked(copyCode.str());

    WGPUComputePipelineDescriptor cpDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    cpDesc.layout = nullptr; // auto
    cpDesc.compute.module = copyModule;
    cpDesc.compute.entryPoint = sv("cs");
    WGPUComputePipeline copyPipeline = t.createComputePipelineTracked(cpDesc);

    const uint64_t copyBufferSize =
        static_cast<uint64_t>(o.width) * o.height * o.sampleCount * 4 * 4;
    WGPUBufferDescriptor copyBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    copyBufDesc.size = copyBufferSize;
    copyBufDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer copyBuffer = t.createBufferTracked(copyBufDesc);

    {
        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(copyPipeline, 0);
        WGPUBindGroupEntry entries[5];
        for (uint32_t i = 0; i < 4; ++i) {
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            entries[i].binding = i;
            entries[i].textureView = t.createViewTracked(textures[i], viewDesc);
        }
        entries[4] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[4].binding = 4;
        entries[4].buffer = copyBuffer;
        entries[4].offset = 0;
        entries[4].size = copyBufferSize;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 5;
        bgDesc.entries = entries;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, copyPipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, o.width * o.sampleCount, o.height, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
    }

    return copyBuffer;
}

// ---------------------------------------------------------------------------
// checkSampleRectsApproximatelyEqual: rgba32float ulpFromZero comparison
// (maxDiffULPsForFloatFormat semantics of upstream findFailedPixels)
// ---------------------------------------------------------------------------

int64_t ulpFromZeroF32(float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    const int64_t magnitude = static_cast<int64_t>(bits & 0x7fffffffu);
    return (bits & 0x80000000u) != 0u ? -magnitude : magnitude;
}

void checkSampleRectsApproximatelyEqual(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t width,
    uint32_t height,
    uint32_t sampleCount,
    WGPUBuffer actualBuffer,
    const std::vector<float>& expected,
    int64_t maxDiffULPsForFloatFormat) {
    const size_t byteLength = expected.size() * sizeof(float);
    t.expectGPUBufferValuesPassCheck(
        actualBuffer,
        [&expected, width, height, sampleCount,
         maxDiffULPsForFloatFormat](const uint8_t* actualBytes, size_t len)
            -> std::optional<std::string> {
            std::vector<float> actual(len / sizeof(float));
            std::memcpy(actual.data(), actualBytes, len);

            std::ostringstream msg;
            int failures = 0;
            constexpr int kMaxReportedFailures = 10;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t xs = 0; xs < width * sampleCount; ++xs) {
                    for (uint32_t c = 0; c < 4; ++c) {
                        const size_t i =
                            (static_cast<size_t>(y) * width * sampleCount + xs) * 4 + c;
                        const int64_t ulpA = ulpFromZeroF32(actual[i]);
                        const int64_t ulpE = ulpFromZeroF32(expected[i]);
                        const int64_t diff = ulpA > ulpE ? ulpA - ulpE : ulpE - ulpA;
                        if (diff > maxDiffULPsForFloatFormat) {
                            ++failures;
                            if (failures <= kMaxReportedFailures) {
                                msg << "\n  at (x=" << (xs / sampleCount)
                                    << ", y=" << y << ", sample=" << (xs % sampleCount)
                                    << ") component " << c << ": expected "
                                    << expected[i] << ", actual " << actual[i]
                                    << " (ULP diff " << diff << " > "
                                    << maxDiffULPsForFloatFormat << ")";
                            }
                        }
                    }
                }
            }
            if (failures > 0) {
                return "Texture level had unexpected contents: " +
                       std::to_string(failures) + " failed component(s)" + msg.str();
            }
            return std::nullopt;
        },
        0, byteLength);
}

// ---------------------------------------------------------------------------
// Shared driver for the inputs,* tests
// ---------------------------------------------------------------------------

struct InputsTestConfig {
    std::array<double, 2> nearFar{};
    uint32_t sampleCount = 1;
    InterpolationAttr interpolation;
    std::string frontFace; // only set by inputs,front_facing
    const VecList* clipSpacePoints = nullptr;
    const VecList* interStagePoints = nullptr;
    std::string fragInCode;
    std::string outputCode;
    InterpolateFn interpolateFn;
    int64_t maxDiffULPsForFloatFormat = 0;
    // Invoked with the CPU-side expected values before the GPU comparison
    // (used for upstream's sanity asserts).
    std::function<void(const std::vector<float>&)> beforeCheck;
};

void runInputsTest(AllFeaturesMaxLimitsGpuTest& t, const InputsTestConfig& c) {
    constexpr uint32_t kWidth = 4;
    constexpr uint32_t kHeight = 4;

    RenderInputsOptions ro;
    ro.interpolationType = c.interpolation.type;
    ro.interpolationSampling = c.interpolation.sampling;
    ro.width = kWidth;
    ro.height = kHeight;
    ro.sampleCount = c.sampleCount;
    ro.frontFace = c.frontFace;
    ro.nearFar = c.nearFar;
    ro.clipSpacePoints = c.clipSpacePoints;
    ro.interStagePoints = c.interStagePoints;
    ro.fragInCode = c.fragInCode;
    ro.outputCode = c.outputCode;
    WGPUBuffer actualBuffer = renderFragmentShaderInputsTo4Textures(t, ro);

    GenerateFragmentInputsOptions go;
    go.width = kWidth;
    go.height = kHeight;
    go.nearFar = c.nearFar;
    go.sampleCount = c.sampleCount;
    go.frontFace = c.frontFace;
    go.clipSpacePoints = c.clipSpacePoints;
    go.interpolateFn = c.interpolateFn;
    const std::vector<float> expected = generateFragmentInputs(go);

    if (c.beforeCheck) {
        c.beforeCheck(expected);
    }

    checkSampleRectsApproximatelyEqual(
        t, kWidth, kHeight, c.sampleCount, actualBuffer, expected,
        c.maxDiffULPsForFloatFormat);
}

std::array<double, 2> parseNearFar(const std::string& text) {
    const Vec values = parseNumberList(text);
    return {values[0], values[1]};
}

// Shared geometry for inputs,position / inputs,interStage / inputs,sample_index.
const VecList kStandardClipSpacePoints = {
    // ndc values
    {0.333, 0.333, 0.333, 0.333}, //  1,  1, 1
    {1.0, -3.0, 0.25, 1.0},       //  1, -3, 0.25
    {-1.5, 0.5, 0.25, 0.5},       // -3,  1, 0.5
};

const VecList kStandardInterStagePoints = {
    {1, 2, 3, 4},
    {5, 6, 7, 8},
    {9, 10, 11, 12},
};

// ---------------------------------------------------------------------------
// inputs,position
// ---------------------------------------------------------------------------

CTS_TEST(g, "inputs,position")
    .desc(R"(
    Test fragment shader builtin(position) values.

    Note: @builtin(position) is always a fragment position, never a sample position.
  )")
    .params([](ParamsBuilder u) {
        return u
            .combine("nearFar", {"[0,1]", "[0.25,0.75]"})
            .combine("sampleCount", {1, 4})
            .combine("interpolation",
                     {"perspective,center", "perspective,centroid", "perspective,sample",
                      "linear,center", "linear,centroid", "linear,sample",
                      "flat,first", "flat,either"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<double, 2> nearFar = parseNearFar(t.param<std::string>("nearFar"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));
        const InterpolationAttr interpolation =
            parseInterpolation(t.param<std::string>("interpolation"));
        // Upstream beforeAllSubcases: skipIfInterpolationTypeOrSamplingNotSupported
        // is compat-mode-only; no-op on this core-mode harness.

        InputsTestConfig c;
        c.nearFar = nearFar;
        c.sampleCount = sampleCount;
        c.interpolation = interpolation;
        c.clipSpacePoints = &kStandardClipSpacePoints;
        c.interStagePoints = &kStandardInterStagePoints;
        c.fragInCode = "";
        c.outputCode = "fin.position";
        c.interpolateFn = computeFragmentPosition;
        c.maxDiffULPsForFloatFormat = 4;
        c.beforeCheck = [&t](const std::vector<float>& expected) {
            // Since @builtin(position) is always a fragment position, never a
            // sample position, check the first coordinate. It should be
            // 0.5, 0.5 always (double-checks computeFragmentPosition).
            t.expect(expected[0] == 0.5f, "expected[0] === 0.5");
            t.expect(expected[1] == 0.5f, "expected[1] === 0.5");
        };
        runInputsTest(t, c);
    });

// ---------------------------------------------------------------------------
// inputs,interStage
// ---------------------------------------------------------------------------

CTS_TEST(g, "inputs,interStage")
    .desc(R"(
    Test fragment shader inter-stage variable values except for centroid interpolation.
  )")
    .params([](ParamsBuilder u) {
        return u
            .combine("nearFar", {"[0,1]", "[0.25,0.75]"})
            .combine("sampleCount", {1, 4})
            .combine("interpolation",
                     {"perspective", "perspective,center", "perspective,sample",
                      "linear", "linear,center", "linear,sample",
                      "flat", "flat,first", "flat,either"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<double, 2> nearFar = parseNearFar(t.param<std::string>("nearFar"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));
        const InterpolationAttr interpolation =
            parseInterpolation(t.param<std::string>("interpolation"));

        InputsTestConfig c;
        c.nearFar = nearFar;
        c.sampleCount = sampleCount;
        c.interpolation = interpolation;
        c.clipSpacePoints = &kStandardClipSpacePoints;
        c.interStagePoints = &kStandardInterStagePoints;
        c.fragInCode = "";
        c.outputCode = "fin.interpolatedValue";
        c.interpolateFn = createInterStageInterpolationFn(
            t, kStandardInterStagePoints, interpolation.type, interpolation.sampling);
        c.maxDiffULPsForFloatFormat = 4;
        runInputsTest(t, c);
    });

// ---------------------------------------------------------------------------
// inputs,interStage,centroid
// ---------------------------------------------------------------------------

CTS_TEST(g, "inputs,interStage,centroid")
    .desc(R"(
    Test fragment shader inter-stage variable values in centroid sampling mode.

    We set the interStage values to barycentric coords. We expect
    that when sampling mode is 'center', some interpolated values
    will be outside of the triangle (ie, one or more of their values will
    be outside the 0 to 1 range). In sampling mode = 'centroid' mode, none
    of the values will be outside of the 0 to 1 range.

    Note: generateFragmentInputs generates "expected". Values not
    rendered to will be 0. Values rendered to outside the triangle will
    be -1. Values rendered to inside the triangle will be 1.
  )")
    .params([](ParamsBuilder u) {
        return u
            .combine("nearFar", {"[0,1]", "[0.25,0.75]"})
            .combine("sampleCount", {1, 4})
            .combine("interpolation",
                     {"perspective,center", "perspective,centroid",
                      "linear,center", "linear,centroid"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<double, 2> nearFar = parseNearFar(t.param<std::string>("nearFar"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));
        const InterpolationAttr interpolation =
            parseInterpolation(t.param<std::string>("interpolation"));

        // One triangle that cuts the viewport (see upstream diagram).
        static const VecList kClipSpacePoints = {
            {1, -2, 0, 1},
            {-1, 2, 0, 1},
            {1, 2, 0, 1},
        };
        static const VecList kInterStagePoints = {
            {1, 0, 0, 0},
            {0, 1, 0, 0},
            {0, 0, 1, 0},
        };

        InputsTestConfig c;
        c.nearFar = nearFar;
        c.sampleCount = sampleCount;
        c.interpolation = interpolation;
        c.clipSpacePoints = &kClipSpacePoints;
        c.interStagePoints = &kInterStagePoints;
        c.fragInCode = "";
        c.outputCode =
            "vec4f(select(-1.0, 1.0, all(fin.interpolatedValue >= vec4f(0)) && "
            "all(fin.interpolatedValue <= vec4f(1))), 0, 0, 0)";
        c.interpolateFn = createInterStageInterpolationBetween0And1TestFn(
            t, kInterStagePoints, interpolation.type, interpolation.sampling);
        c.maxDiffULPsForFloatFormat = 3;
        runInputsTest(t, c);
    });

// ---------------------------------------------------------------------------
// inputs,sample_index
// ---------------------------------------------------------------------------

CTS_TEST(g, "inputs,sample_index")
    .desc(R"(
    Test fragment shader builtin(sample_index) values.
  )")
    .params([](ParamsBuilder u) {
        return u
            .combine("nearFar", {"[0,1]", "[0.25,0.75]"})
            .combine("sampleCount", {1, 4})
            .combine("interpolation",
                     {"perspective,center", "perspective,centroid", "perspective,sample",
                      "linear,center", "linear,centroid", "linear,sample",
                      "flat,first", "flat,either"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<double, 2> nearFar = parseNearFar(t.param<std::string>("nearFar"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));
        const InterpolationAttr interpolation =
            parseInterpolation(t.param<std::string>("interpolation"));
        // Upstream skips when t.isCompatibility (sample_index not supported in
        // compat mode); this harness only runs core mode, so no skip.

        InputsTestConfig c;
        c.nearFar = nearFar;
        c.sampleCount = sampleCount;
        c.interpolation = interpolation;
        c.clipSpacePoints = &kStandardClipSpacePoints;
        c.interStagePoints = &kStandardInterStagePoints;
        c.fragInCode = "@builtin(sample_index) sampleIndex: u32,";
        c.outputCode = "vec4f(f32(fin.sampleIndex), 0, 0, 0)";
        c.interpolateFn = computeFragmentSampleIndex;
        c.maxDiffULPsForFloatFormat = 1;
        runInputsTest(t, c);
    });

// ---------------------------------------------------------------------------
// inputs,front_facing
// ---------------------------------------------------------------------------

CTS_TEST(g, "inputs,front_facing")
    .desc(R"(
    Test fragment shader builtin(front_facing) values.

    Draws a quad from 2 triangles that entirely cover clip space.
    One triangle is clockwise, the other is counter clockwise. The triangles
    bisect pixels so that different samples are covered by each triangle so that some
    samples should get different values for front_facing for the same fragment.
  )")
    .params([](ParamsBuilder u) {
        return u
            .combine("nearFar", {"[0,1]", "[0.25,0.75]"})
            .combine("sampleCount", {1, 4})
            .combine("frontFace", {"cw", "ccw"})
            .combine("interpolation",
                     {"perspective,center", "perspective,centroid", "perspective,sample",
                      "linear,center", "linear,centroid", "linear,sample",
                      "flat,first", "flat,either"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<double, 2> nearFar = parseNearFar(t.param<std::string>("nearFar"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));
        const std::string frontFace = t.param<std::string>("frontFace");
        const InterpolationAttr interpolation =
            parseInterpolation(t.param<std::string>("interpolation"));

        // 2 triangles from y = -2 to y = +2 (see upstream diagram).
        static const VecList kClipSpacePoints = {
            // ccw
            {-1, -2, 0, 1},
            {1, -2, 0, 1},
            {-1, 2, 0, 1},

            // cw
            {1, -2, 0, 1},
            {-1, 2, 0, 1},
            {1, 2, 0, 1},
        };
        static const VecList kInterStagePoints = {
            {1, 2, 3, 4},
            {5, 6, 7, 8},
            {9, 10, 11, 12},

            {13, 14, 15, 16},
            {17, 18, 19, 20},
            {21, 22, 23, 24},
        };

        InputsTestConfig c;
        c.nearFar = nearFar;
        c.sampleCount = sampleCount;
        c.interpolation = interpolation;
        c.frontFace = frontFace;
        c.clipSpacePoints = &kClipSpacePoints;
        c.interStagePoints = &kInterStagePoints;
        c.fragInCode = "@builtin(front_facing) frontFacing: bool,";
        c.outputCode = "vec4f(select(0.0, 1.0, fin.frontFacing), 0, 0, 0)";
        c.interpolateFn = computeFragmentFrontFacing;
        c.maxDiffULPsForFloatFormat = 0;
        c.beforeCheck = [&t](const std::vector<float>& expected) {
            bool hasZero = false;
            bool hasNonZero = false;
            for (const float v : expected) {
                if (v == 0.0f) {
                    hasZero = true;
                } else {
                    hasNonZero = true;
                }
            }
            t.expect(hasZero, "expect some values to be 0");
            t.expect(hasNonZero, "expect some values to be non 0");
        };
        runInputsTest(t, c);
    });

// ---------------------------------------------------------------------------
// inputs,sample_mask
// ---------------------------------------------------------------------------

CTS_TEST(g, "inputs,sample_mask")
    .desc(R"(
    Test fragment shader builtin(sample_mask) values.

    Draws various triangles that should trigger different sample_mask values.
    Checks that sample_mask matches what's expected. Note: the triangles
    are selected so they do not intersect sample points as we don't want
    to test precision issues on whether or not a sample point is inside
    or outside the triangle when right on the edge.
  )")
    .params([](ParamsBuilder u) {
        return u
            .combine("nearFar", {"[0,1]", "[0.25,0.75]"})
            .combine("sampleCount", {1, 4})
            .combine("interpolation",
                     // given that 'sample' effects whether things are run
                     // per-sample or per-fragment we test all of these to make
                     // sure they don't affect the result differently than expected.
                     {"perspective,center", "perspective,centroid", "perspective,sample",
                      "linear,center", "linear,centroid", "linear,sample",
                      "flat,first", "flat,either"})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"x", -1}, {"y", -1}},
                ParamRecord{{"x", -1}, {"y", -2}},
                ParamRecord{{"x", -1}, {"y", 1}},
                ParamRecord{{"x", -1}, {"y", 3}},
                ParamRecord{{"x", -2}, {"y", -1}},
                ParamRecord{{"x", -2}, {"y", 3}},
                ParamRecord{{"x", -3}, {"y", -1}},
                ParamRecord{{"x", -3}, {"y", -2}},
                ParamRecord{{"x", -3}, {"y", 1}},
                ParamRecord{{"x", 1}, {"y", -1}},
                ParamRecord{{"x", 1}, {"y", -3}},
                ParamRecord{{"x", 1}, {"y", 1}},
                ParamRecord{{"x", 1}, {"y", 2}},
                ParamRecord{{"x", 2}, {"y", -2}},
                ParamRecord{{"x", 2}, {"y", -3}},
                ParamRecord{{"x", 2}, {"y", 1}},
                ParamRecord{{"x", 2}, {"y", 2}},
                ParamRecord{{"x", 3}, {"y", -1}},
                ParamRecord{{"x", 3}, {"y", -3}},
                ParamRecord{{"x", 3}, {"y", 1}},
                ParamRecord{{"x", 3}, {"y", 2}},
                ParamRecord{{"x", 3}, {"y", 3}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::array<double, 2> nearFar = parseNearFar(t.param<std::string>("nearFar"));
        const uint32_t sampleCount = static_cast<uint32_t>(t.param<int>("sampleCount"));
        const InterpolationAttr interpolation =
            parseInterpolation(t.param<std::string>("interpolation"));
        const double x = static_cast<double>(t.param<int>("x"));
        const double y = static_cast<double>(t.param<int>("y"));
        // Upstream also skips when t.isCompatibility (sample_mask not supported
        // in compat mode); no-op on this core-mode harness.

        const VecList clipSpacePoints = {
            {x + 0.2, -y, 0, 1},
            {-x + 0.2, y, 0, 1},
            {x + 0.2, y, 0, 1},
        };
        static const VecList kInterStagePoints = {
            {13, 14, 15, 16},
            {17, 18, 19, 20},
            {21, 22, 23, 24},
        };

        InputsTestConfig c;
        c.nearFar = nearFar;
        c.sampleCount = sampleCount;
        c.interpolation = interpolation;
        c.clipSpacePoints = &clipSpacePoints;
        c.interStagePoints = &kInterStagePoints;
        c.fragInCode = "@builtin(sample_mask) sample_mask: u32,";
        c.outputCode = "vec4f(f32(fin.sample_mask), 0, 0, 0)";
        c.interpolateFn = computeSampleMask;
        c.maxDiffULPsForFloatFormat = 0;
        runInputsTest(t, c);
    });

// ---------------------------------------------------------------------------
// Subgroup tests
// ---------------------------------------------------------------------------

// Upstream kSizes, encoded as strings for param identity.
std::vector<Value> kSizeParamValues() {
    return {
        "[15,15]", "[16,16]", "[17,17]", "[19,13]", "[13,10]", "[111,2]",
        "[2,111]", "[35,2]", "[2,35]", "[53,13]", "[13,53]",
    };
}

// Returns the population count of input (no compiler builtins: MSVC-portable).
uint32_t popcount(uint32_t input) {
    uint32_t n = input;
    n = n - ((n >> 1) & 0x55555555u);
    n = (n & 0x33333333u) + ((n >> 2) & 0x33333333u);
    return (((n + (n >> 4)) & 0x0f0f0f0fu) * 0x01010101u) >> 24;
}

// ---------------------------------------------------------------------------
// adapterInfo.subgroupMinSize / subgroupMaxSize
// ---------------------------------------------------------------------------
// wgpuDeviceGetAdapterInfo is absent from yawgpu and panics in wgpu-native, so
// the range is queried via a temporary instance + default adapter (same pattern
// as compute_builtins.spec.cpp). Cached per process.

struct SubgroupRange {
    uint32_t minSize;
    uint32_t maxSize;
};

SubgroupRange querySubgroupRange(GpuTest& t) {
    static bool cached = false;
    static SubgroupRange range = {0, 0};
    if (cached) {
        return range;
    }

    WGPUInstance instance = createInstance();
    if (instance == nullptr) {
        t.fail("failed to create a WebGPU instance for the adapter-info query");
    }
    AdapterResult adapter = requestAdapterSync(instance, adapterOptions());
    if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
        wgpuInstanceRelease(instance);
        t.fail("failed to request an adapter for the adapter-info query: " + adapter.message);
    }

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    const WGPUStatus status = wgpuAdapterGetInfo(adapter.adapter, &info);
    if (status == WGPUStatus_Success) {
        range.minSize = info.subgroupMinSize;
        range.maxSize = info.subgroupMaxSize;
        wgpuAdapterInfoFreeMembers(info);
    }
    wgpuAdapterRelease(adapter.adapter);
    wgpuInstanceRelease(instance);

    if (status != WGPUStatus_Success) {
        t.fail("wgpuAdapterGetInfo failed for the adapter-info query");
    }
    cached = true;
    return range;
}

constexpr uint32_t kMaximumSubgroupSize = 128;
// A non-zero magic number indicating no expectation error, in order to prevent
// the false no-error result from zero-initialization.
constexpr uint32_t kSubgroupShaderNoError = 17;

// rgba32uint: 16 bytes per texel (the only format these tests use).
constexpr uint32_t kSubgroupBytesPerTexel = 16;

// Runs a subgroup builtin test for fragment shaders.
// Draws a full screen in 2 separate draw calls (half screen each); results are
// checked for each draw. (Port of runSubgroupTest; format is always rgba32uint.)
void runSubgroupTest(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t width,
    uint32_t height,
    const std::string& fsShader,
    const std::function<std::optional<std::string>(const std::vector<uint32_t>&)>& checker) {
    constexpr std::string_view kVsShader = R"(
@vertex
fn vsMain(@builtin(vertex_index) index : u32) -> @builtin(position) vec4f {
  const vertices = array(
    vec2(-1, -1), vec2(-1,  1), vec2( 1,  1),
    vec2(-1, -1), vec2( 1, -1), vec2( 1,  1),
  );
  return vec4f(vec2f(vertices[index]), 0, 1);
})";

    WGPUShaderModule vsModule = t.createShaderModuleTracked(kVsShader);
    WGPUShaderModule fsModule = t.createShaderModuleTracked(fsShader);

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA32Uint;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fsModule;
    fragment.entryPoint = sv("fsMain");
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rpDesc.layout = nullptr; // auto
    rpDesc.vertex.module = vsModule;
    rpDesc.vertex.entryPoint = sv("vsMain");
    rpDesc.fragment = &fragment;
    rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(rpDesc);

    // Image copies require bytesPerRow to be a multiple of 256.
    const uint32_t bytesPerRow = alignUp(width * kSubgroupBytesPerTexel, 256);
    const uint32_t byteLength = bytesPerRow * height;

    for (uint32_t i = 0; i < 2; ++i) {
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{width, height, 1};
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
                        WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
        texDesc.format = WGPUTextureFormat_RGBA32Uint;
        WGPUTexture framebuffer = t.createTextureTracked(texDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(framebuffer, viewDesc);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = byteLength;
        bufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        {
            WGPURenderPassColorAttachment colorAttachment =
                WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAttachment.view = view;
            colorAttachment.loadOp = WGPULoadOp_Clear;
            colorAttachment.storeOp = WGPUStoreOp_Store;
            colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &colorAttachment;

            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            wgpuRenderPassEncoderSetPipeline(pass, pipeline);
            // Draw the upper-left triangle (vertices 0-2) or the lower-right
            // triangle (vertices 3-5).
            wgpuRenderPassEncoderDraw(pass, 3, 1, i * 3, 0);
            wgpuRenderPassEncoderEnd(pass);
        }
        t.copyTextureToBuffer(encoder, framebuffer, buffer, bytesPerRow,
                              WGPUExtent3D{width, height, 1});
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        t.expectGPUBufferValuesPassCheck(
            buffer,
            [&checker](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                std::vector<uint32_t> data(len / 4);
                std::memcpy(data.data(), actual, data.size() * 4);
                return checker(data);
            },
            0, byteLength);
    }
}

// Checks subgroup_size builtin value consistency (direct port).
std::optional<std::string> checkSubgroupSizeConsistency(
    const std::vector<uint32_t>& data,
    uint32_t min,
    uint32_t max,
    uint32_t width,
    uint32_t height) {
    const uint32_t bytesPerRow = alignUp(width * kSubgroupBytesPerTexel, 256);
    const uint32_t uintsPerRow = bytesPerRow / 4;
    const uint32_t uintsPerTexel = kSubgroupBytesPerTexel / 4;

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset =
                static_cast<size_t>(uintsPerRow) * row + static_cast<size_t>(col) * uintsPerTexel;
            const uint32_t subgroupSize = data[offset];
            const uint32_t countActive = data[offset + 1];
            const uint32_t ballotedSubgroupSize = data[offset + 2];
            const uint32_t error = data[offset + 3];

            std::ostringstream msg;
            if (error == 0) {
                // Inactive fragment gets error `0` instead of noError. Check
                // all output being zero.
                if (subgroupSize != 0 || countActive != 0 || ballotedSubgroupSize != 0) {
                    msg << "Unexpected zero error with non-zero outputs for (" << row << ", "
                        << col << "): got output [" << subgroupSize << ", " << countActive
                        << ", " << ballotedSubgroupSize << ", " << error << "]";
                    return msg.str();
                }
                continue;
            }

            if (popcount(subgroupSize) != 1) {
                msg << "Subgroup size '" << subgroupSize << "' is not a power of two";
                return msg.str();
            }

            if (subgroupSize < min) {
                msg << "Subgroup size '" << subgroupSize << "' is less than minimum '" << min
                    << "'";
                return msg.str();
            }
            if (max < subgroupSize) {
                msg << "Subgroup size '" << subgroupSize << "' is greater than maximum '" << max
                    << "'";
                return msg.str();
            }

            if (subgroupSize < countActive) {
                msg << "Unexpected active invocations number larger than subgroup size"
                    << "\n-       icoord: (" << row << ", " << col << ")"
                    << "\n- subgroupSize: " << subgroupSize
                    << "\n-  countActive: " << countActive;
                return msg.str();
            }

            if (subgroupSize != ballotedSubgroupSize) {
                msg << "Inconsistent subgroup size"
                    << "\n-                 icoord: (" << row << ", " << col << ")"
                    << "\n-           subgroupSize: " << subgroupSize
                    << "\n- balloted subgroup size: " << ballotedSubgroupSize;
                return msg.str();
            }

            if (error != kSubgroupShaderNoError) {
                msg << "Unexpected error value"
                    << "\n-   icoord: (" << row << ", " << col << ")"
                    << "\n- expected: noError (" << kSubgroupShaderNoError << ")"
                    << "\n-      got: " << error;
                return msg.str();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "subgroup_size")
    .desc("Tests subgroup_size values")
    .params([](ParamsBuilder u) {
        return u
            .combine("size", kSizeParamValues())
            .beginSubcases()
            .combineWithParams({ParamRecord{{"format", "rgba32uint"}}});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
            t.skip("subgroups feature is not supported on this device");
        }
        const Vec size = parseNumberList(t.param<std::string>("size"));
        const uint32_t width = static_cast<uint32_t>(size[0]);
        const uint32_t height = static_cast<uint32_t>(size[1]);

        const SubgroupRange sgRange = querySubgroupRange(t);
        const uint32_t subgroupMinSize = sgRange.minSize;
        const uint32_t subgroupMaxSize = sgRange.maxSize;

        std::ostringstream fs;
        fs << "enable subgroups;\n"
           << "\n"
           << "const subgroupMaxSize = " << kMaximumSubgroupSize << "u;\n"
           << "const noError = " << kSubgroupShaderNoError << "u;\n"
           << "\n"
           << "const width = " << width << ";\n"
           << "const height = " << height << ";\n"
           << "\n"
           << "@fragment\n"
           << "fn fsMain(\n"
           << "  @builtin(position) pos : vec4f,\n"
           << "  @builtin(subgroup_size) sg_size : u32,\n"
           << ") -> @location(0) vec4u {\n"
           << "  var error: u32 = noError;\n"
           << "\n"
           << "  let ballotActive = countOneBits(subgroupBallot(true));\n"
           << "  let countActive = ballotActive.x + ballotActive.y + ballotActive.z + ballotActive.w;\n"
           << "  // Validate that balloted active invocations number no larger than subgroup size\n"
           << "  if (countActive > sg_size) {\n"
           << "    error++;\n"
           << "  }\n"
           << "\n"
           << "  var subgroupSizeBallotedInvocations: u32 = 0u;\n"
           << "  var ballotedSubgroupSize: u32 = 0u;\n"
           << "  for (var i: u32 = 0; i <= subgroupMaxSize; i++) {\n"
           << "    let ballotSubgroupSizeEqualI = countOneBits(subgroupBallot(sg_size == i));\n"
           << "    let countSubgroupSizeEqualI = ballotSubgroupSizeEqualI.x + ballotSubgroupSizeEqualI.y + ballotSubgroupSizeEqualI.z + ballotSubgroupSizeEqualI.w;\n"
           << "    subgroupSizeBallotedInvocations += countSubgroupSizeEqualI;\n"
           << "    // Validate that all active invocations see the same subgroup size, i.e. ballotedSubgroupSize\n"
           << "    ballotedSubgroupSize = select(ballotedSubgroupSize, i, countSubgroupSizeEqualI == countActive);\n"
           << "    error = select(error, error + 1, countSubgroupSizeEqualI != countActive && countSubgroupSizeEqualI != 0);\n"
           << "  }\n"
           << "  // Validate that all active invocations balloted in previous loop\n"
           << "  if (subgroupSizeBallotedInvocations != countActive) {\n"
           << "    error++;\n"
           << "  }\n"
           << "  // Validate that ballotedSubgroupSize is identical to subgroup_size\n"
           << "  if (ballotedSubgroupSize != sg_size) {\n"
           << "    error++;\n"
           << "  }\n"
           << "\n"
           << "  return vec4u(sg_size, countActive, ballotedSubgroupSize, error);\n"
           << "}";

        runSubgroupTest(
            t, width, height, fs.str(),
            [subgroupMinSize, subgroupMaxSize, width,
             height](const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkSubgroupSizeConsistency(
                    data, subgroupMinSize, subgroupMaxSize, width, height);
            });
    });

// Checks subgroup_invocation_id value consistency (direct port).
std::optional<std::string> checkSubgroupInvocationIdConsistency(
    const std::vector<uint32_t>& data,
    uint32_t width,
    uint32_t height) {
    const uint32_t bytesPerRow = alignUp(width * kSubgroupBytesPerTexel, 256);
    const uint32_t uintsPerRow = bytesPerRow / 4;
    const uint32_t uintsPerTexel = kSubgroupBytesPerTexel / 4;

    for (uint32_t row = 0; row < height; ++row) {
        for (uint32_t col = 0; col < width; ++col) {
            const size_t offset =
                static_cast<size_t>(uintsPerRow) * row + static_cast<size_t>(col) * uintsPerTexel;
            const uint32_t id = data[offset];
            const uint32_t sgSize = data[offset + 1];
            const uint32_t ballotSize = data[offset + 2];
            const uint32_t error = data[offset + 3];

            std::ostringstream msg;
            if (error == 0) {
                // Inactive fragment gets error `0` instead of noError. Check
                // all output being zero.
                if (id != 0 || sgSize != 0 || ballotSize != 0) {
                    msg << "Unexpected zero error with non-zero outputs for (" << row << ", "
                        << col << "): got output [" << id << ", " << sgSize << ", "
                        << ballotSize << ", " << error << "]";
                    return msg.str();
                }
                continue;
            }

            if (sgSize < id) {
                msg << "Invocation id '" << id << "' is greater than subgroup size '" << sgSize
                    << "' for (" << row << ", " << col << ")";
                return msg.str();
            }

            if (sgSize < ballotSize) {
                msg << "Ballot size '" << ballotSize << "' is greater than subgroup size '"
                    << sgSize << "' for (" << row << ", " << col << ")";
                return msg.str();
            }

            if (error != kSubgroupShaderNoError) {
                msg << "Unexpected error value"
                    << "\n-   icoord: (" << row << ", " << col << ")"
                    << "\n- expected: noError (" << kSubgroupShaderNoError << ")"
                    << "\n-      got: " << error;
                return msg.str();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "subgroup_invocation_id")
    .desc("Tests subgroup_invocation_id built-in value")
    .params([](ParamsBuilder u) {
        return u
            .combine("size", kSizeParamValues())
            .beginSubcases()
            .combineWithParams({ParamRecord{{"format", "rgba32uint"}}});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
            t.skip("subgroups feature is not supported on this device");
        }
        const Vec size = parseNumberList(t.param<std::string>("size"));
        const uint32_t width = static_cast<uint32_t>(size[0]);
        const uint32_t height = static_cast<uint32_t>(size[1]);

        std::ostringstream fs;
        fs << "enable subgroups;\n"
           << "\n"
           << "const width = " << width << ";\n"
           << "const height = " << height << ";\n"
           << "\n"
           << "const subgroupMaxSize = " << kMaximumSubgroupSize << "u;\n"
           << "// A non-zero magic number indicating no expectation error, in order to prevent the\n"
           << "// false no-error result from zero-initialization.\n"
           << "const noError = " << kSubgroupShaderNoError << "u;\n"
           << "\n"
           << "@fragment\n"
           << "fn fsMain(\n"
           << "  @builtin(position) pos : vec4f,\n"
           << "  @builtin(subgroup_invocation_id) id : u32,\n"
           << "  @builtin(subgroup_size) sg_size : u32,\n"
           << ") -> @location(0) vec4u {\n"
           << "\n"
           << "  var error: u32 = noError;\n"
           << "\n"
           << "  // Validate that reported subgroup size is no larger than subgroupMaxSize\n"
           << "  if (sg_size > subgroupMaxSize) {\n"
           << "    error++;\n"
           << "  }\n"
           << "\n"
           << "  // Validate that reported subgroup invocation id is smaller than subgroup size\n"
           << "  if (id >= sg_size) {\n"
           << "    error++;\n"
           << "  }\n"
           << "\n"
           << "  // Validate that each subgroup id is assigned to at most one active invocation\n"
           << "  // in the subgroup\n"
           << "  var countAssignedId: u32 = 0u;\n"
           << "  for (var i: u32 = 0; i < subgroupMaxSize; i++) {\n"
           << "    let ballotIdEqualsI = countOneBits(subgroupBallot(id == i));\n"
           << "    let countInvocationIdEqualsI = ballotIdEqualsI.x + ballotIdEqualsI.y + ballotIdEqualsI.z + ballotIdEqualsI.w;\n"
           << "    // Validate an id assigned at most once\n"
           << "    error += select(1u, 0u, countInvocationIdEqualsI <= 1);\n"
           << "    // Validate id larger than subgroup size will not get balloted\n"
           << "    error += select(1u, 0u, (id < sg_size) || (countInvocationIdEqualsI == 0));\n"
           << "    // Sum up the assigned invocation number of each id\n"
           << "    countAssignedId += countInvocationIdEqualsI;\n"
           << "  }\n"
           << "  // Validate that all active invocation get counted during the above loop\n"
           << "  let ballotActive = countOneBits(subgroupBallot(true));\n"
           << "  let activeInvocations = ballotActive.x + ballotActive.y + ballotActive.z + ballotActive.w;\n"
           << "  if (activeInvocations != countAssignedId) {\n"
           << "    error++;\n"
           << "  }\n"
           << "\n"
           << "  return vec4u(id, sg_size, activeInvocations, error);\n"
           << "}";

        runSubgroupTest(
            t, width, height, fs.str(),
            [width, height](const std::vector<uint32_t>& data) -> std::optional<std::string> {
                return checkSubgroupInvocationIdConsistency(data, width, height);
            });
    });

// ---------------------------------------------------------------------------
// primitive_index tests
// ---------------------------------------------------------------------------

constexpr std::string_view kPrimitiveIndexShader = R"(
enable primitive_index;

@vertex
fn vsFullscreenMain(@builtin(vertex_index) index : u32) -> @builtin(position) vec4f {
  const vertices = array(
    vec2(-1, -1), vec2( 3,  -1), vec2(-1,  3),
  );
  return vec4f(vec2f(vertices[index%3]), 0, 1);
}

@vertex
fn vsBufferMain(@builtin(vertex_index) index : u32, @location(0) pos : vec2f) -> @builtin(position) vec4f {
  return vec4f(pos, 0, 1);
}

@fragment
fn fsMain(@builtin(primitive_index) pid : u32) -> @location(0) vec4u {
  return vec4u(pid, 0, 0, 0);
})";

struct PrimitiveIndexExpectation {
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t value = 0;
};

struct PrimitiveIndexTestOptions {
    uint32_t count = 0;
    uint32_t instances = 1;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
    uint32_t firstIndex = 0;
    const std::vector<float>* vertices = nullptr;   // null = use fullscreen VS
    const std::vector<uint32_t>* indices = nullptr; // null = non-indexed draw
    WGPUPrimitiveTopology topology = WGPUPrimitiveTopology_TriangleList;
    WGPUCullMode cullMode = WGPUCullMode_None;
    uint32_t width = 4;
    uint32_t height = 4;
    // Either a single expected value for the whole texture...
    bool hasExpectedValue = false;
    uint32_t expectedValue = 0;
    // ...or a list of per-pixel expectations.
    std::vector<PrimitiveIndexExpectation> expectedPixels;
};

// Renders triangles writing primitive_index to an r32uint target, then reads
// back the texture and compares against the expectation (port of
// runPrimitiveIndexTest).
void runPrimitiveIndexTest(AllFeaturesMaxLimitsGpuTest& t, const PrimitiveIndexTestOptions& o) {
    WGPUShaderModule module = t.createShaderModuleTracked(kPrimitiveIndexShader);

    WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
    attribute.format = WGPUVertexFormat_Float32x2;
    attribute.offset = 0;
    attribute.shaderLocation = 0;

    WGPUVertexBufferLayout vertexLayout = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
    vertexLayout.arrayStride = sizeof(float) * 2;
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = &attribute;

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_R32Uint;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("fsMain");
    fragment.targetCount = 1;
    fragment.targets = &target;

    const bool isStrip = o.topology == WGPUPrimitiveTopology_TriangleStrip ||
                         o.topology == WGPUPrimitiveTopology_LineStrip;

    WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rpDesc.layout = nullptr; // auto
    rpDesc.vertex.module = module;
    rpDesc.vertex.entryPoint = sv(o.vertices != nullptr ? "vsBufferMain" : "vsFullscreenMain");
    if (o.vertices != nullptr) {
        rpDesc.vertex.bufferCount = 1;
        rpDesc.vertex.buffers = &vertexLayout;
    }
    rpDesc.fragment = &fragment;
    rpDesc.primitive.topology = o.topology;
    rpDesc.primitive.cullMode = o.cullMode;
    if (isStrip) {
        rpDesc.primitive.stripIndexFormat = WGPUIndexFormat_Uint32;
    }
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(rpDesc);

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{o.width, o.height, 1};
    texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
                    WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    texDesc.format = WGPUTextureFormat_R32Uint;
    WGPUTexture framebuffer = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(framebuffer, viewDesc);

    WGPUBuffer vertexBuffer = nullptr;
    if (o.vertices != nullptr) {
        vertexBuffer = t.makeBufferWithContents(
            o.vertices->data(), o.vertices->size() * sizeof(float),
            WGPUBufferUsage_CopyDst | WGPUBufferUsage_Vertex);
    }

    WGPUBuffer indexBuffer = nullptr;
    if (o.indices != nullptr) {
        indexBuffer = t.makeBufferWithContents(
            o.indices->data(), o.indices->size() * sizeof(uint32_t),
            WGPUBufferUsage_CopyDst | WGPUBufferUsage_Index);
    }

    const uint32_t bytesPerRow = alignUp(o.width * 4, 256);
    const uint32_t byteLength = bytesPerRow * o.height;
    WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    readbackDesc.size = byteLength;
    readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readbackBuffer = t.createBufferTracked(readbackDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    {
        WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        // Clear to max uint32 to ensure that primitive id 0 is testable.
        colorAttachment.clearValue = WGPUColor{4294967295.0, 0.0, 0.0, 0.0};

        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        if (vertexBuffer != nullptr) {
            wgpuRenderPassEncoderSetVertexBuffer(
                pass, 0, vertexBuffer, 0, o.vertices->size() * sizeof(float));
        }
        if (indexBuffer != nullptr) {
            wgpuRenderPassEncoderSetIndexBuffer(
                pass, indexBuffer, WGPUIndexFormat_Uint32, 0,
                o.indices->size() * sizeof(uint32_t));
            wgpuRenderPassEncoderDrawIndexed(
                pass, o.count, o.instances, o.firstIndex,
                static_cast<int32_t>(o.firstVertex), o.firstInstance);
        } else {
            wgpuRenderPassEncoderDraw(pass, o.count, o.instances, o.firstVertex, o.firstInstance);
        }
        wgpuRenderPassEncoderEnd(pass);
    }
    t.copyTextureToBuffer(encoder, framebuffer, readbackBuffer, bytesPerRow,
                          WGPUExtent3D{o.width, o.height, 1});
    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    const uint32_t width = o.width;
    const uint32_t height = o.height;
    const bool hasExpectedValue = o.hasExpectedValue;
    const uint32_t expectedValue = o.expectedValue;
    const std::vector<PrimitiveIndexExpectation> expectedPixels = o.expectedPixels;
    t.expectGPUBufferValuesPassCheck(
        readbackBuffer,
        [width, height, hasExpectedValue, expectedValue, expectedPixels, bytesPerRow](
            const uint8_t* actualBytes, size_t len) -> std::optional<std::string> {
            std::vector<uint32_t> data(len / 4);
            std::memcpy(data.data(), actualBytes, data.size() * 4);
            const uint32_t uintsPerRow = bytesPerRow / 4;

            std::ostringstream msg;
            if (!expectedPixels.empty()) {
                for (const PrimitiveIndexExpectation& e : expectedPixels) {
                    const uint32_t actual =
                        data[static_cast<size_t>(e.y) * uintsPerRow + e.x];
                    if (actual != e.value) {
                        msg << "primitive_index mismatch at (" << e.x << ", " << e.y
                            << "): expected " << e.value << ", actual " << actual;
                        return msg.str();
                    }
                }
                return std::nullopt;
            }
            if (hasExpectedValue) {
                for (uint32_t y = 0; y < height; ++y) {
                    for (uint32_t x = 0; x < width; ++x) {
                        const uint32_t actual =
                            data[static_cast<size_t>(y) * uintsPerRow + x];
                        if (actual != expectedValue) {
                            msg << "primitive_index mismatch at (" << x << ", " << y
                                << "): expected " << expectedValue << ", actual " << actual;
                            return msg.str();
                        }
                    }
                }
            }
            return std::nullopt;
        },
        0, byteLength);
}

CTS_TEST(g, "primitive_index,basic")
    .desc("Tests primitive_index built-in value")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combine("triCount", {1, 4, 16})
            // None of the following should affect the primitive_index
            .combine("instances", {1, 4, 16})
            .combine("firstVertex", {0, 1, 4})
            .combine("firstIndex", {0, 3, 9})
            .combine("firstInstance", {0, 1, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_PrimitiveIndex)) {
            t.skip("primitive-index feature is not supported on this device");
        }
        const uint32_t triCount = static_cast<uint32_t>(t.param<int>("triCount"));
        const uint32_t instances = static_cast<uint32_t>(t.param<int>("instances"));
        const uint32_t firstVertex = static_cast<uint32_t>(t.param<int>("firstVertex"));
        const uint32_t firstIndex = static_cast<uint32_t>(t.param<int>("firstIndex"));
        const uint32_t firstInstance = static_cast<uint32_t>(t.param<int>("firstInstance"));

        {
            PrimitiveIndexTestOptions o;
            o.count = triCount * 3;
            o.instances = instances;
            o.firstVertex = firstVertex;
            o.firstInstance = firstInstance;
            o.hasExpectedValue = true;
            o.expectedValue = triCount - 1;
            runPrimitiveIndexTest(t, o);
        }

        std::vector<uint32_t> indices;
        const uint32_t extraTris = (firstIndex + 2) / 3; // ceil(firstIndex / 3)
        for (uint32_t i = 0; i < triCount + extraTris; ++i) {
            indices.push_back(0);
            indices.push_back(1);
            indices.push_back(2);
        }

        {
            PrimitiveIndexTestOptions o;
            o.count = triCount * 3;
            o.instances = instances;
            o.firstVertex = firstVertex;
            o.firstInstance = firstInstance;
            o.firstIndex = firstIndex;
            o.indices = &indices;
            o.hasExpectedValue = true;
            o.expectedValue = triCount - 1;
            runPrimitiveIndexTest(t, o);
        }
    });

CTS_TEST(g, "primitive_index,primitive_reset")
    .desc(
        "Tests that the primitive_index built-in value does not increment or reset across "
        "primitive resets")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_PrimitiveIndex)) {
            t.skip("primitive-index feature is not supported on this device");
        }
        const std::vector<uint32_t> indices = {0, 1, 2, 0, 1, 0xffffffffu, 0, 1, 2, 0};
        PrimitiveIndexTestOptions o;
        o.count = 10;
        o.topology = WGPUPrimitiveTopology_TriangleStrip;
        o.indices = &indices;
        o.hasExpectedValue = true;
        o.expectedValue = 4;
        runPrimitiveIndexTest(t, o);
    });

// Upstream `vertices` subcase values, encoded as strings for param identity.
struct DiscardedPrimitivesCase {
    const char* param;
    std::array<float, 6> vertices;
};

const DiscardedPrimitivesCase kDiscardedPrimitivesCases[] = {
    // Zero size triangle
    {"[0.3,0.3,0.3,0.3,0.3,0.3]", {0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 0.3f}},
    // Degenerate triangle
    {"[0.3,0.3,0.3,0.3,0.3,1.3]", {0.3f, 0.3f, 0.3f, 0.3f, 0.3f, 1.3f}},
    // Sub-pixel triangle
    {"[0.3,0.3,0.30001,0.3,0.3,0.30001]", {0.3f, 0.3f, 0.30001f, 0.3f, 0.3f, 0.30001f}},
    // Offscreen triangle
    {"[2,2,2,3,3,2]", {2, 2, 2, 3, 3, 2}},
    // Backface culled triangle
    {"[-1,-1,-1,3,3,-1]", {-1, -1, -1, 3, 3, -1}},
};

CTS_TEST(g, "primitive_index,discarded_primitves")
    .desc(
        "Tests that the primitives which are discarded due to culling, size, or shape still "
        "increment the primitive_index built-in")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine(
            "vertices",
            {"[0.3,0.3,0.3,0.3,0.3,0.3]", "[0.3,0.3,0.3,0.3,0.3,1.3]",
             "[0.3,0.3,0.30001,0.3,0.3,0.30001]", "[2,2,2,3,3,2]", "[-1,-1,-1,3,3,-1]"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_PrimitiveIndex)) {
            t.skip("primitive-index feature is not supported on this device");
        }
        const std::string verticesParam = t.param<std::string>("vertices");
        const DiscardedPrimitivesCase* testCase = nullptr;
        for (const DiscardedPrimitivesCase& c : kDiscardedPrimitivesCases) {
            if (verticesParam == c.param) {
                testCase = &c;
                break;
            }
        }
        if (testCase == nullptr) {
            t.fail("unknown vertices param: " + verticesParam);
        }

        // Append a fullscreen triangle to the test vertices.
        std::vector<float> vertices(testCase->vertices.begin(), testCase->vertices.end());
        const float fullscreen[6] = {-1, -1, 3, -1, -1, 3};
        vertices.insert(vertices.end(), fullscreen, fullscreen + 6);

        PrimitiveIndexTestOptions o;
        o.count = 6;
        o.vertices = &vertices;
        o.cullMode = WGPUCullMode_Back;
        o.hasExpectedValue = true;
        o.expectedValue = 1;
        runPrimitiveIndexTest(t, o);
    });

CTS_TEST(g, "primitive_index,topologies")
    .desc("Tests that the primitive_index built-in value works every topology")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_PrimitiveIndex)) {
            t.skip("primitive-index feature is not supported on this device");
        }

        // 4 triangles, one per quadrant (see upstream diagrams).
        const std::vector<float> triListVertices = {
            0, 0, -2, 0, 0, 2, // upper-left
            0, 0, 0, 2, 2, 0,  // upper-right
            0, 0, -2, 0, 0, -2, // lower-left
            0, 0, 0, -2, 2, 0,  // lower-right
        };
        {
            PrimitiveIndexTestOptions o;
            o.count = 12;
            o.topology = WGPUPrimitiveTopology_TriangleList;
            o.vertices = &triListVertices;
            o.width = 2;
            o.height = 2;
            o.expectedPixels = {
                {0, 0, 0},
                {1, 0, 1},
                {0, 1, 2},
                {1, 1, 3},
            };
            runPrimitiveIndexTest(t, o);
        }

        // Triangle strip: see upstream diagram (primitives #0..#5).
        const std::vector<float> triStripVertices = {
            -2, 0, 0, 0, 0, 2, 2, 0, 0, 0, 0, -2, 0, 0, -2, 0};
        {
            PrimitiveIndexTestOptions o;
            o.count = 8;
            o.topology = WGPUPrimitiveTopology_TriangleStrip;
            o.vertices = &triStripVertices;
            o.width = 2;
            o.height = 2;
            o.expectedPixels = {
                {0, 0, 0},
                {1, 0, 2},
                {1, 1, 3},
                {0, 1, 5},
            };
            runPrimitiveIndexTest(t, o);
        }

        // Two vertical lines drawn twice (see upstream diagram).
        const std::vector<float> lineVertices = {
            -0.5f, -1, -0.5f, 1, 0.5f, 1, 0.5f, -1, -0.5f, -1, -0.5f, 1, 0.5f, 1, 0.5f, -1};
        {
            PrimitiveIndexTestOptions o;
            o.count = 4;
            o.topology = WGPUPrimitiveTopology_LineList;
            o.vertices = &lineVertices;
            o.width = 2;
            o.expectedPixels = {
                {0, 0, 0},
                {0, 1, 0},
                {1, 0, 1},
                {1, 1, 1},
            };
            runPrimitiveIndexTest(t, o);
        }
        {
            PrimitiveIndexTestOptions o;
            o.count = 8;
            o.topology = WGPUPrimitiveTopology_LineList;
            o.vertices = &lineVertices;
            o.width = 2;
            o.expectedPixels = {
                {0, 0, 2},
                {0, 1, 2},
                {1, 0, 3},
                {1, 1, 3},
            };
            runPrimitiveIndexTest(t, o);
        }
        {
            PrimitiveIndexTestOptions o;
            o.count = 4;
            o.topology = WGPUPrimitiveTopology_LineStrip;
            o.vertices = &lineVertices;
            o.width = 2;
            o.expectedPixels = {
                {0, 0, 0},
                {0, 1, 0},
                {1, 0, 2},
                {1, 1, 2},
            };
            runPrimitiveIndexTest(t, o);
        }
        {
            PrimitiveIndexTestOptions o;
            o.count = 8;
            o.topology = WGPUPrimitiveTopology_LineStrip;
            o.vertices = &lineVertices;
            o.width = 2;
            o.expectedPixels = {
                {0, 0, 4},
                {0, 1, 4},
                {1, 0, 6},
                {1, 1, 6},
            };
            runPrimitiveIndexTest(t, o);
        }

        // 4 points, one per texel of a 2x2 target, drawn twice (see diagram).
        const std::vector<float> pointVertices = {
            -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f,
            -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f};
        {
            PrimitiveIndexTestOptions o;
            o.count = 4;
            o.topology = WGPUPrimitiveTopology_PointList;
            o.vertices = &pointVertices;
            o.width = 2;
            o.height = 2;
            o.expectedPixels = {
                {0, 1, 0},
                {1, 1, 1},
                {0, 0, 2},
                {1, 0, 3},
            };
            runPrimitiveIndexTest(t, o);
        }
        {
            PrimitiveIndexTestOptions o;
            o.count = 8;
            o.topology = WGPUPrimitiveTopology_PointList;
            o.vertices = &pointVertices;
            o.width = 2;
            o.height = 2;
            o.expectedPixels = {
                {0, 1, 4},
                {1, 1, 5},
                {0, 0, 6},
                {1, 0, 7},
            };
            runPrimitiveIndexTest(t, o);
        }
    });

} // namespace
