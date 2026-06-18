// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/derivatives.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/derivatives.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace cts {
namespace derivatives {

namespace {

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

uint32_t alignUp(uint32_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

// ---------------------------------------------------------------------------
// Self-contained f32 acceptance-interval logic, reproducing the relevant parts
// of upstream util/floating_point.ts for subtraction / addition / abs.
// ---------------------------------------------------------------------------

// f32 limit constants (bit patterns from util/constants.ts kValue.f32).
float bitsToF32(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

const float kF32PosMin = bitsToF32(0x00800000u); // smallest positive normal
const float kF32PosMax = bitsToF32(0x7f7fffffu); // largest finite
const float kF32NegMax = bitsToF32(0x80800000u); // largest negative normal (-min)
const float kF32NegMin = bitsToF32(0xff7fffffu); // smallest finite (-max)

constexpr double kInf = std::numeric_limits<double>::infinity();

struct Interval {
    double begin;
    double end;
    bool finite() const {
        return std::isfinite(begin) && std::isfinite(end);
    }
    bool contains(double n) const {
        return begin <= n && n >= -kInf && end >= n;
    }
};

const Interval kUnbounded{-kInf, kInf};

bool isSubnormalF32(double n) {
    return n > static_cast<double>(kF32NegMax) && n < static_cast<double>(kF32PosMin) && n != 0.0;
}

bool isFiniteF32(double n) {
    return n >= static_cast<double>(kF32NegMin) && n <= static_cast<double>(kF32PosMax);
}

// nextAfter in f64 space, towards +/-inf, flushing subnormals to 0 (mode 'flush').
double nextAfterF64Flush(double val, bool positive) {
    if (std::isnan(val)) {
        return val;
    }
    if (val == kInf) {
        return kInf;
    }
    if (val == -kInf) {
        return -kInf;
    }
    // Flush subnormal f64 (anything below smallest normal double). In practice the
    // values we feed are f32-magnitude so subnormal-f64 flushing never triggers;
    // keep the f32 result-flush semantics by treating the value as-is.
    if (val == 0.0) {
        // 'flush' mode: closest normal in the requested direction.
        return positive ? static_cast<double>(kF32PosMin) : static_cast<double>(kF32NegMax);
    }
    uint64_t bits;
    std::memcpy(&bits, &val, sizeof(bits));
    bool isPositive = (bits & 0x8000000000000000ull) == 0ull;
    if (isPositive == positive) {
        bits += 1;
    } else {
        bits -= 1;
    }
    double out;
    std::memcpy(&out, &bits, sizeof(out));
    if (std::isinf(out)) {
        return positive ? kInf : -kInf;
    }
    return out;
}

// correctlyRoundedIntervalWithUnboundedPrecisionForAddition(val, large, small).
Interval crUnboundedAdd(double val, double largeVal, double smallVal) {
    if (val == largeVal && smallVal != 0.0) {
        if (smallVal >= 0.0) {
            return Interval{largeVal, nextAfterF64Flush(largeVal, true)};
        } else {
            return Interval{nextAfterF64Flush(largeVal, false), largeVal};
        }
    }
    // correctlyRoundedInterval(val): for finite val just [val, val]; OOB -> widen.
    if (val == kInf) {
        return Interval{static_cast<double>(kF32PosMax), kInf};
    }
    if (val == -kInf) {
        return Interval{-kInf, static_cast<double>(kF32NegMin)};
    }
    if (val > static_cast<double>(kF32PosMax)) {
        return Interval{static_cast<double>(kF32PosMax), kInf};
    }
    if (val < static_cast<double>(kF32NegMin)) {
        return Interval{-kInf, static_cast<double>(kF32NegMin)};
    }
    return Interval{val, val};
}

Interval span(const Interval& a, const Interval& b) {
    return Interval{std::min(a.begin, b.begin), std::max(a.end, b.end)};
}

// op.impl for subtraction (x - y) with unbounded precision.
Interval subImpl(double x, double y) {
    double difference = x - y;
    double largeVal = std::abs(x) > std::abs(y) ? x : -y;
    double smallVal = std::abs(x) > std::abs(y) ? -y : x;
    return crUnboundedAdd(difference, largeVal, smallVal);
}

// op.impl for addition (x + y) with unbounded precision.
Interval addImpl(double x, double y) {
    double sum = x + y;
    double largeVal = std::abs(x) > std::abs(y) ? x : y;
    double smallVal = std::abs(x) > std::abs(y) ? y : x;
    return crUnboundedAdd(sum, largeVal, smallVal);
}

// addFlushedIfNeeded: returns {v} or {v, 0} if v is a non-zero subnormal.
// Inputs are exact f32, so correctlyRounded(v) == {v}.
std::vector<double> flushedInputs(double v) {
    if (isSubnormalF32(v)) {
        return {v, 0.0};
    }
    return {v};
}

// roundAndFlush + runScalarPairToIntervalOp for a binary op, given exact f32
// inputs x and y.
template <typename Op>
Interval runPairOp(double x, double y, Op op) {
    if (!isFiniteF32(x) || !isFiniteF32(y)) {
        return kUnbounded;
    }
    std::vector<double> xs = flushedInputs(x);
    std::vector<double> ys = flushedInputs(y);
    Interval result{kInf, -kInf};
    for (double ix : xs) {
        for (double iy : ys) {
            result = span(result, op(ix, iy));
        }
    }
    if (!result.finite()) {
        return kUnbounded;
    }
    return result;
}

Interval subtractionInterval(double x, double y) {
    return runPairOp(x, y, subImpl);
}

Interval additionInterval(double x, double y) {
    return runPairOp(x, y, addImpl);
}

// abs over an interval: |.| of [begin,end], handling subnormal flushing of the
// endpoints. Reproduces absInterval (correctlyRoundedInterval(|n|) per endpoint,
// flushing inputs). Operates on an existing (already result) interval.
Interval absInterval(const Interval& iv) {
    if (!iv.finite()) {
        return kUnbounded;
    }
    // |interval|: if the interval straddles zero, min is 0; else min of |begin|,|end|.
    double a = std::abs(iv.begin);
    double b = std::abs(iv.end);
    double lo;
    double hi = std::max(a, b);
    if (iv.begin <= 0.0 && iv.end >= 0.0) {
        lo = 0.0;
    } else {
        lo = std::min(a, b);
    }
    return Interval{lo, hi};
}

// Add two intervals (independent endpoints): a + b, applying the addition
// acceptance interval to the endpoint combinations and spanning. Used for fwidth.
Interval addIntervals(const Interval& a, const Interval& b) {
    if (!a.finite() || !b.finite()) {
        return kUnbounded;
    }
    Interval r1 = additionInterval(a.begin, b.begin);
    Interval r2 = additionInterval(a.begin, b.end);
    Interval r3 = additionInterval(a.end, b.begin);
    Interval r4 = additionInterval(a.end, b.end);
    return span(span(r1, r2), span(r3, r4));
}

// ---------------------------------------------------------------------------
// Case inputs.
// ---------------------------------------------------------------------------

const std::vector<float>& interestingValuesImpl() {
    static const std::vector<float> values = {
        kF32NegMin,                  // negative.min
        -10.0f,                      // -10
        -1.0f,                       // -1
        -0.125f,                     // -0.125
        kF32NegMax,                  // negative.max
        bitsToF32(0x807fffffu),      // negative.subnormal.min
        bitsToF32(0x80000001u),      // negative.subnormal.max
        -0.0f,                       // -0
        0.0f,                        // 0
        bitsToF32(0x00000001u),      // positive.subnormal.min
        bitsToF32(0x007fffffu),      // positive.subnormal.max
        kF32PosMin,                  // positive.min
        0.125f,                      // 0.125
        1.0f,                        // 1
        10.0f,                       // 10
        kF32PosMax,                  // positive.max
    };
    return values;
}

uint32_t f32Bits(float v) {
    uint32_t b;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

float f32FromBits(uint32_t b) {
    float v;
    std::memcpy(&v, &b, sizeof(v));
    return v;
}

WGPURenderPipeline makePipeline(AllFeaturesMaxLimitsGpuTest& t, const std::string& code) {
    WGPUShaderModule module = t.createShaderModuleTracked(code);
    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_RGBA32Uint;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = sv("frag");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rpDesc.layout = nullptr; // auto
    rpDesc.vertex.module = module;
    rpDesc.vertex.entryPoint = sv("vert");
    rpDesc.fragment = &fragment;
    rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    return t.createRenderPipelineTracked(rpDesc);
}

} // namespace

const std::vector<float>& sparseScalarF32Range() {
    return interestingValuesImpl();
}

// ---------------------------------------------------------------------------
// dpdx / dpdy harness (ported from derivatives.ts runDerivativeTest).
// ---------------------------------------------------------------------------

void runDerivativeTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& builtin,
    bool non_uniform_discard,
    int vectorize) {
    const std::vector<float>& range = sparseScalarF32Range();

    // Build the scalar cases: cartesianProduct(range, range), input = (p0, p1).
    struct ScalarCase {
        float p0;
        float p1;
    };
    std::vector<ScalarCase> scalarCases;
    scalarCases.reserve(range.size() * range.size());
    for (float a : range) {
        for (float b : range) {
            scalarCases.push_back({a, b});
        }
    }

    const int vw = vectorize == 0 ? 1 : vectorize;

    // Pack into vector cases (packScalarsToVector), clamping the trailing index.
    // Each packed case has vw components; component c uses scalarCases[idx+c].
    struct PackedCase {
        // For each of the vw components, the (p0,p1) scalar pair.
        std::array<ScalarCase, 4> comp;
    };
    std::vector<PackedCase> cases;
    for (size_t idx = 0; idx < scalarCases.size(); idx += static_cast<size_t>(vw)) {
        PackedCase pc{};
        for (int c = 0; c < vw; ++c) {
            size_t ci = std::min(idx + static_cast<size_t>(c), scalarCases.size() - 1);
            pc.comp[static_cast<size_t>(c)] = scalarCases[ci];
        }
        cases.push_back(pc);
    }

    // Determine direction from builtin name (dpd[x|y]...).
    const char dir = builtin[3];

    const uint32_t valueStride = 16; // bytes (vec4f)

    std::string conversionFromInput = "input.x";
    std::string conversionToOutput = "vec4f(v)";
    if (vectorize == 2) {
        conversionFromInput = "input.xy";
        conversionToOutput = "vec4f(v, 0, 0)";
    } else if (vectorize == 3) {
        conversionFromInput = "input.xyz";
        conversionToOutput = "vec4f(v, 0)";
    } else if (vectorize == 4) {
        conversionFromInput = "input";
        conversionToOutput = "v";
    }

    const std::string posComp = dir == 'x' ? "y" : "x";
    const std::string invComp(1, dir);
    const std::string discardSnippet =
        non_uniform_discard ? "if inv_idx == 0 { discard; }" : "";

    std::ostringstream code;
    code << "struct CaseInfo {\n"
         << "  @builtin(position) position: vec4f,\n"
         << "  @location(0) @interpolate(flat, either) quad_idx: u32,\n"
         << "}\n\n"
         << "@vertex\n"
         << "fn vert(@builtin(vertex_index) vertex_idx: u32,\n"
         << "        @builtin(instance_index) instance_idx: u32) -> CaseInfo {\n"
         << "  const kVertices = array(\n"
         << "    vec2f(-2, -2),\n"
         << "    vec2f( 2, -2),\n"
         << "    vec2f( 0,  2),\n"
         << "  );\n"
         << "  return CaseInfo(vec4(kVertices[vertex_idx], 0, 1), instance_idx);\n"
         << "}\n\n"
         << "@group(0) @binding(0) var<uniform> inputs : array<vec4f, " << (cases.size() * 2)
         << ">;\n\n"
         << "@fragment\n"
         << "fn frag(info : CaseInfo) -> @location(0) vec4u {\n"
         << "  let case_idx = u32(info.position." << posComp << ");\n"
         << "  let inv_idx = u32(info.position." << invComp << ");\n"
         << "  let index = info.quad_idx*4 + case_idx*2 + inv_idx;\n"
         << "  let input = inputs[index];\n"
         << "  " << discardSnippet << "\n"
         << "  let v = " << builtin << "(" << conversionFromInput << ");\n"
         << "  return bitcast<vec4u>(" << conversionToOutput << ");\n"
         << "}\n";

    WGPURenderPipeline pipeline = makePipeline(t, code.str());

    // Populate the uniform buffer:
    //   inputs[(i*2+1)] = case[i].input[0]  (the p0 vector)
    //   inputs[(i*2)]   = case[i].input[1]  (the p1 vector)
    // Each vec4f slot holds component c at byte (slot*16 + c*4).
    const size_t bufferSize = cases.size() * 2 * valueStride;
    std::vector<uint8_t> values(bufferSize, 0);
    for (size_t i = 0; i < cases.size(); ++i) {
        const PackedCase& pc = cases[i];
        for (int c = 0; c < vw; ++c) {
            const ScalarCase& s = pc.comp[static_cast<size_t>(c)];
            // input[0] -> slot (i*2+1)
            uint32_t b0 = f32Bits(s.p0);
            std::memcpy(&values[(i * 2 + 1) * valueStride + static_cast<size_t>(c) * 4], &b0, 4);
            // input[1] -> slot (i*2)
            uint32_t b1 = f32Bits(s.p1);
            std::memcpy(&values[(i * 2) * valueStride + static_cast<size_t>(c) * 4], &b1, 4);
        }
    }
    WGPUBuffer inputBuffer =
        t.makeBufferWithContents(values.data(), values.size(), WGPUBufferUsage_Uniform);

    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = inputBuffer;
    entry.offset = 0;
    entry.size = bufferSize;
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;
    WGPUBindGroup group = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{2, 2, 1};
    texDesc.format = WGPUTextureFormat_RGBA32Uint;
    texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture colorAttachment = t.createTextureTracked(texDesc);

    const uint32_t width = 2;
    const uint32_t height = 2;
    const uint32_t bytesPerRow = alignUp(valueStride * width, 256);

    const size_t numQuads = cases.size() / 2;
    std::vector<WGPUBuffer> results;
    results.reserve(numQuads);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    for (size_t quad = 0; quad < numQuads; ++quad) {
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(colorAttachment, viewDesc);

        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view = view;
        colorAtt.loadOp = WGPULoadOp_Clear;
        colorAtt.storeOp = WGPUStoreOp_Store;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAtt;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, static_cast<uint32_t>(quad));
        wgpuRenderPassEncoderEnd(pass);

        WGPUBufferDescriptor obDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        obDesc.size = static_cast<uint64_t>(bytesPerRow) * height;
        obDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer outputBuffer = t.createBufferTracked(obDesc);
        results.push_back(outputBuffer);
        t.copyTextureToBuffer(encoder, colorAttachment, outputBuffer, bytesPerRow,
                              WGPUExtent3D{width, height, 1});
    }
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    const bool isFine =
        builtin.size() >= 4 && builtin.compare(builtin.size() - 4, 4, "Fine") == 0;

    // Acceptance interval for a packed case along the derivative axis.
    // The true derivative is f(inv1) - f(inv0). From the buffer layout, inv1
    // holds p0 and inv0 holds p1, so derivative == p0 - p1 per component.
    auto matchesCase = [&](const PackedCase& pc, const float* result) -> bool {
        for (int c = 0; c < vw; ++c) {
            const ScalarCase& s = pc.comp[static_cast<size_t>(c)];
            Interval iv = subtractionInterval(static_cast<double>(s.p0), static_cast<double>(s.p1));
            double got = static_cast<double>(result[c]);
            if (std::isnan(got)) {
                if (!(iv.begin == -kInf && iv.end == kInf)) {
                    return false;
                }
            } else if (!iv.contains(got)) {
                return false;
            }
        }
        return true;
    };

    for (size_t quadNdx = 0; quadNdx < results.size(); ++quadNdx) {
        WGPUBuffer outputBuffer = results[quadNdx];
        const uint64_t obSize = static_cast<uint64_t>(bytesPerRow) * height;
        t.expectGPUBufferValuesPassCheck(
            outputBuffer,
            [&, quadNdx](const uint8_t* outputData, size_t len) -> std::optional<std::string> {
                (void)len;
                for (int i = 0; i < 4; ++i) {
                    const int tx = i % 2;
                    const int ty = i / 2;
                    int inputNdx;
                    int caseNdx;
                    if (dir == 'x') {
                        inputNdx = tx;
                        caseNdx = ty;
                    } else {
                        inputNdx = ty;
                        caseNdx = tx;
                    }
                    const int caseNdxAlt = 1 - caseNdx;

                    // Both invocations should produce the same derivative; under
                    // non_uniform_discard, inv 0 discards, so skip it.
                    if (non_uniform_discard && inputNdx == 0) {
                        continue;
                    }

                    const size_t base = quadNdx * 2;
                    const PackedCase& c = cases[base + static_cast<size_t>(caseNdx)];

                    const size_t index =
                        static_cast<size_t>(ty) * bytesPerRow + static_cast<size_t>(tx) * valueStride;
                    float result[4] = {0, 0, 0, 0};
                    for (int comp = 0; comp < 4; ++comp) {
                        uint32_t bits;
                        std::memcpy(&bits, outputData + index + static_cast<size_t>(comp) * 4, 4);
                        result[comp] = f32FromBits(bits);
                    }

                    bool ok = matchesCase(c, result);
                    if (!ok && !isFine) {
                        const PackedCase& c0 = cases[base + static_cast<size_t>(caseNdxAlt)];
                        ok = matchesCase(c0, result);
                    }
                    if (!ok) {
                        std::ostringstream msg;
                        msg << "derivative mismatch at quad " << quadNdx << " texel " << i
                            << " (builtin=" << builtin << ")";
                        const ScalarCase& s = c.comp[0];
                        Interval iv = subtractionInterval(static_cast<double>(s.p0),
                                                          static_cast<double>(s.p1));
                        msg << "; inputs=(" << s.p0 << ", " << s.p1 << ")"
                            << "; expected [" << iv.begin << ", " << iv.end << "]"
                            << "; got " << result[0];
                        return msg.str();
                    }
                }
                return std::nullopt;
            },
            0, obSize);
    }
}

// ---------------------------------------------------------------------------
// fwidth harness (ported from fwidth.ts runFWidthTest).
// ---------------------------------------------------------------------------

void runFWidthTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& builtin,
    bool non_uniform_discard,
    int vectorize) {
    const std::vector<float>& range = sparseScalarF32Range();

    // Cases: cartesianProduct(range, range, range, range); input = (v0,v1,v2,v3).
    struct FWCase {
        float v[4];
        // Expected intervals per quad invocation x (0..3): fwidth at that texel.
        Interval expected[4];
    };

    auto makeCase = [&](float v0, float v1, float v2, float v3) -> FWCase {
        FWCase c{};
        c.v[0] = v0;
        c.v[1] = v1;
        c.v[2] = v2;
        c.v[3] = v3;
        const double x = v0, y = v1, z = v2, w = v3;
        // x == 0: abs(x-y) + abs(x-z)
        c.expected[0] = addIntervals(absInterval(subtractionInterval(x, y)),
                                     absInterval(subtractionInterval(x, z)));
        // x == 1: abs(x-y) + abs(y-w)
        c.expected[1] = addIntervals(absInterval(subtractionInterval(x, y)),
                                     absInterval(subtractionInterval(y, w)));
        // x == 2: abs(z-w) + abs(x-z)
        c.expected[2] = addIntervals(absInterval(subtractionInterval(z, w)),
                                     absInterval(subtractionInterval(x, z)));
        // x == 3: abs(z-w) + abs(y-w)
        c.expected[3] = addIntervals(absInterval(subtractionInterval(z, w)),
                                     absInterval(subtractionInterval(y, w)));
        return c;
    };

    std::vector<FWCase> cases;
    cases.reserve(range.size() * range.size() * range.size() * range.size());
    for (float a : range) {
        for (float b : range) {
            for (float cc : range) {
                for (float dd : range) {
                    cases.push_back(makeCase(a, b, cc, dd));
                }
            }
        }
    }

    const int vectorWidth = vectorize == 0 ? 1 : vectorize;

    const uint32_t valueStride = 16;
    std::string conversionFromInput = "input.x";
    std::string conversionToOutput = "vec4f(v, 0, 0, 0)";
    if (vectorize == 2) {
        conversionFromInput = "input.xy";
        conversionToOutput = "vec4f(v, 0, 0)";
    } else if (vectorize == 3) {
        conversionFromInput = "input.xyz";
        conversionToOutput = "vec4f(v, 0)";
    } else if (vectorize == 4) {
        conversionFromInput = "input";
        conversionToOutput = "v";
    }

    const uint32_t kUniformBufferSize = 16384;
    const uint32_t kNumCasesPerUniformBuffer = kUniformBufferSize / 64; // 256

    const std::string discardSnippet =
        non_uniform_discard ? "if inv_idx == 0 { discard; }" : "";

    std::ostringstream code;
    code << "@vertex\n"
         << "fn vert(@builtin(vertex_index) vertex_idx: u32) -> @builtin(position) vec4f {\n"
         << "  const kVertices = array(\n"
         << "    vec2f( 3, -1),\n"
         << "    vec2f(-1,  3),\n"
         << "    vec2f(-1, -1),\n"
         << "  );\n"
         << "  return vec4(kVertices[vertex_idx], 0, 1);\n"
         << "}\n\n"
         << "@group(0) @binding(0) var<uniform> inputs : array<vec4f, "
         << (kNumCasesPerUniformBuffer * 4) << ">;\n\n"
         << "@fragment\n"
         << "fn frag(@builtin(position) position: vec4f) -> @location(0) vec4u {\n"
         << "  let t = vec2u(position.xy);\n"
         << "  let inv_idx = t.x % 2 + (t.y % 2) * 2;\n"
         << "  let q = t / 2;\n"
         << "  let quad_idx = q.y * 256 + q.x;\n"
         << "  let index = quad_idx * 4 + inv_idx;\n"
         << "  let input = inputs[index];\n"
         << "  " << discardSnippet << "\n"
         << "  let v = " << builtin << "(" << conversionFromInput << ");\n"
         << "  return bitcast<vec4u>(" << conversionToOutput << ");\n"
         << "}\n";

    WGPURenderPipeline pipeline = makePipeline(t, code.str());

    const uint32_t width = kNumCasesPerUniformBuffer * 2; // 512
    const uint32_t height = 2;
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{width, height, 1};
    texDesc.format = WGPUTextureFormat_RGBA32Uint;
    texDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture colorAttachment = t.createTextureTracked(texDesc);
    const uint32_t bytesPerRow = alignUp(width * 16, 256);

    const bool isFine =
        builtin.size() >= 4 && builtin.compare(builtin.size() - 4, 4, "Fine") == 0;

    std::vector<WGPUBuffer> results;
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

    for (size_t cstart = 0; cstart < cases.size(); cstart += kNumCasesPerUniformBuffer) {
        // Populate uniform buffer.
        std::vector<uint8_t> values(kUniformBufferSize, 0);
        for (uint32_t i = 0; i < kNumCasesPerUniformBuffer / static_cast<uint32_t>(vectorWidth);
             ++i) {
            for (int vc = 0; vc < vectorWidth; ++vc) {
                size_t index = cstart + static_cast<size_t>(i) * vectorWidth + vc;
                if (index >= cases.size()) {
                    break;
                }
                const FWCase& c = cases[index];
                for (int x = 0; x < 4; ++x) {
                    uint32_t bits = f32Bits(c.v[x]);
                    size_t off = (static_cast<size_t>(i) * 4 + x) * valueStride +
                                 static_cast<size_t>(vc) * 4;
                    std::memcpy(&values[off], &bits, 4);
                }
            }
        }
        WGPUBuffer inputBuffer =
            t.makeBufferWithContents(values.data(), values.size(), WGPUBufferUsage_Uniform);

        WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = inputBuffer;
        entry.offset = 0;
        entry.size = kUniformBufferSize;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        WGPUBindGroup group = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView view = t.createViewTracked(colorAttachment, viewDesc);
        WGPURenderPassColorAttachment colorAtt = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
        colorAtt.view = view;
        colorAtt.loadOp = WGPULoadOp_Clear;
        colorAtt.storeOp = WGPUStoreOp_Store;
        colorAtt.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAtt;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        WGPUBufferDescriptor obDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        obDesc.size = static_cast<uint64_t>(bytesPerRow) * height;
        obDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
        WGPUBuffer outputBuffer = t.createBufferTracked(obDesc);
        results.push_back(outputBuffer);
        t.copyTextureToBuffer(encoder, colorAttachment, outputBuffer, bytesPerRow,
                              WGPUExtent3D{width, height, 1});
    }
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    for (size_t groupNdx = 0; groupNdx < results.size(); ++groupNdx) {
        WGPUBuffer outputBuffer = results[groupNdx];
        const uint64_t obSize = static_cast<uint64_t>(bytesPerRow) * height;
        const size_t base = groupNdx * kNumCasesPerUniformBuffer;
        t.expectGPUBufferValuesPassCheck(
            outputBuffer,
            [&, base](const uint8_t* outputData, size_t len) -> std::optional<std::string> {
                (void)len;
                const size_t numCases =
                    std::min(static_cast<size_t>(kNumCasesPerUniformBuffer), cases.size() - base);
                const size_t numQuads = numCases / static_cast<size_t>(vectorWidth);
                for (size_t i = 0; i < numQuads; ++i) {
                    for (int vc = 0; vc < vectorWidth; ++vc) {
                        size_t caseNdx = base + i * vectorWidth + vc;
                        if (caseNdx >= cases.size()) {
                            break;
                        }
                        const FWCase& c = cases[caseNdx];
                        for (int x = 0; x < 4; ++x) {
                            if (non_uniform_discard && x == 0) {
                                continue;
                            }
                            const int tx = x % 2;
                            const int ty = x / 2;
                            const size_t index = static_cast<size_t>(ty) * bytesPerRow +
                                                 i * 32 + static_cast<size_t>(tx) * 16 +
                                                 static_cast<size_t>(vc) * 4;
                            uint32_t bits;
                            std::memcpy(&bits, outputData + index, 4);
                            double result = static_cast<double>(f32FromBits(bits));

                            bool ok;
                            if (isFine) {
                                const Interval& iv = c.expected[x];
                                ok = std::isnan(result)
                                         ? (iv.begin == -kInf && iv.end == kInf)
                                         : iv.contains(result);
                            } else {
                                // anyOf: any of the four expected intervals match.
                                ok = false;
                                for (int e = 0; e < 4; ++e) {
                                    const Interval& iv = c.expected[e];
                                    bool m = std::isnan(result)
                                                 ? (iv.begin == -kInf && iv.end == kInf)
                                                 : iv.contains(result);
                                    if (m) {
                                        ok = true;
                                        break;
                                    }
                                }
                            }
                            if (!ok) {
                                std::ostringstream msg;
                                msg << "fwidth mismatch caseNdx=" << caseNdx << " vc=" << vc
                                    << " x=" << x << " (builtin=" << builtin << ")"
                                    << "; inputs=(" << c.v[0] << ", " << c.v[1] << ", " << c.v[2]
                                    << ", " << c.v[3] << ")"
                                    << "; expected[" << x << "]=[" << c.expected[x].begin << ", "
                                    << c.expected[x].end << "]; got " << result;
                                return msg.str();
                            }
                        }
                    }
                }
                return std::nullopt;
            },
            0, obSize);
    }
}

} // namespace cts::derivatives
} // namespace cts
