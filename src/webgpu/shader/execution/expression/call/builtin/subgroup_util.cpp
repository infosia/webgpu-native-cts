// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroup_util.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/call/builtin/subgroup_util.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include "webgpu/texture_format.h"

namespace cts {
namespace subgroups {

using expression::fp::sparseScalarF16Range;
using expression::fp::sparseScalarF32Range;

namespace {

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

uint32_t alignUp(uint32_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

// Float16 encode/decode (round-to-nearest-even), used by runAccuracyTest f16 path
// and the f16 fragment-input expansion.
uint16_t f32ToF16Bits(double value) {
    float f = static_cast<float>(value);
    uint32_t x;
    std::memcpy(&x, &f, sizeof(x));
    const uint32_t sign = (x >> 16) & 0x8000u;
    int32_t exp = static_cast<int32_t>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t mant = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFFu) == 0xFFu) {
        // Inf / NaN.
        return static_cast<uint16_t>(sign | 0x7C00u | (mant != 0u ? 0x200u : 0u));
    }
    if (exp >= 0x1F) {
        return static_cast<uint16_t>(sign | 0x7C00u); // overflow -> inf
    }
    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<uint16_t>(sign); // underflow -> 0
        }
        mant |= 0x800000u;
        const int32_t shift = 14 - exp;
        uint32_t result = mant >> shift;
        // round to nearest even
        const uint32_t roundBit = 1u << (shift - 1);
        if ((mant & roundBit) && ((mant & (roundBit - 1)) || (result & 1u))) {
            result += 1;
        }
        return static_cast<uint16_t>(sign | result);
    }
    uint16_t half = static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
    const uint32_t roundBit = 1u << 12;
    if ((mant & roundBit) && ((mant & (roundBit - 1)) || (half & 1u))) {
        half += 1; // may carry into exponent, which is correct
    }
    return half;
}

double f16BitsToF32(uint16_t h) {
    const uint32_t sign = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    uint32_t exp = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    uint32_t mant = static_cast<uint32_t>(h) & 0x3FFu;
    uint32_t bits;
    if (exp == 0) {
        if (mant == 0) {
            bits = sign;
        } else {
            exp = 127 - 15 + 1;
            while ((mant & 0x400u) == 0) {
                mant <<= 1;
                exp -= 1;
            }
            mant &= 0x3FFu;
            bits = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 0x1F) {
        bits = sign | 0x7F800000u | (mant << 13);
    } else {
        bits = sign | ((exp + (127 - 15)) << 23) | (mant << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return static_cast<double>(f);
}

// ---------------------------------------------------------------------------
// CTS PRNG (TinyMT), faithfully ported from util/prng.ts. Deterministic per seed.
// ---------------------------------------------------------------------------
class PRNG {
  public:
    explicit PRNG(uint32_t seed) {
        state_[0] = seed;
        state_[1] = kMat1;
        state_[2] = kMat2;
        state_[3] = kTMat;
        for (uint32_t i = 1; i < kMinLoop; i++) {
            const uint32_t prev = state_[(i - 1u) & 3u];
            state_[i & 3u] ^= i + 1812433253u * (prev ^ (prev >> 30));
        }
        for (uint32_t i = 0; i < kPreLoop; i++) {
            next();
        }
    }

    uint32_t randomU32() {
        next();
        return temper();
    }

    uint32_t uniformInt(uint64_t n) {
        const uint64_t upperBound = 0x100000000ull;
        const uint64_t keepZoneSize = upperBound - (upperBound % n);
        uint64_t candidate = 0;
        do {
            candidate = randomU32();
        } while (candidate >= keepZoneSize);
        return static_cast<uint32_t>(candidate % n);
    }

  private:
    static constexpr uint32_t kMat1 = 0x8f7011eeu;
    static constexpr uint32_t kMat2 = 0xfc78ff1fu;
    static constexpr uint32_t kTMat = 0x3793fdffu;
    static constexpr uint32_t kMask = 0x7fffffffu;
    static constexpr uint32_t kMinLoop = 8;
    static constexpr uint32_t kPreLoop = 8;
    static constexpr uint32_t kSH0 = 1;
    static constexpr uint32_t kSH1 = 10;
    static constexpr uint32_t kSH8 = 8;

    void next() {
        uint32_t x = (state_[0] & kMask) ^ state_[1] ^ state_[2];
        uint32_t y = state_[3];
        x ^= x << kSH0;
        y ^= (y >> kSH0) ^ x;
        state_[0] = state_[1];
        state_[1] = state_[2];
        state_[2] = x ^ (y << kSH1);
        state_[3] = y;
        if ((y & 1u) != 0u) {
            state_[1] ^= kMat1;
            state_[2] ^= kMat2;
        }
    }

    uint32_t temper() {
        uint32_t t0 = state_[3];
        uint32_t t1 = state_[0] + (state_[2] >> kSH8);
        t0 ^= t1;
        if ((t1 & 1u) != 0u) {
            t0 ^= kTMat;
        }
        return t0;
    }

    std::array<uint32_t, 4> state_{};
};

// Skips the case if the workgroup size exceeds the device's compute limits.
void skipIfWorkgroupTooLarge(SubgroupTest& t, const WGSize& wgSize) {
    const WGPULimits limits = t.getLimits();
    const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];
    if (limits.maxComputeInvocationsPerWorkgroup < wgThreads ||
        limits.maxComputeWorkgroupSizeX < wgSize[0] ||
        limits.maxComputeWorkgroupSizeY < wgSize[1] ||
        limits.maxComputeWorkgroupSizeZ < wgSize[2]) {
        t.skip("Workgroup size too large");
    }
}

WGPUBuffer makeStorageBuffer(SubgroupTest& t, const std::vector<uint32_t>& data) {
    return t.makeBufferWithContents(
        data.data(), data.size() * sizeof(uint32_t),
        static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst |
                                     WGPUBufferUsage_Storage));
}

WGPUBindGroup makeThreeBufferBindGroup(
    SubgroupTest& t,
    WGPUBindGroupLayout bgl,
    WGPUBuffer b0,
    WGPUBuffer b1,
    WGPUBuffer b2) {
    std::array<WGPUBindGroupEntry, 3> entries{};
    const std::array<WGPUBuffer, 3> buffers{b0, b1, b2};
    for (uint32_t i = 0; i < 3; ++i) {
        entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[i].binding = i;
        entries[i].buffer = buffers[i];
        entries[i].offset = 0;
        entries[i].size = WGPU_WHOLE_SIZE;
    }
    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = bgl;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupTracked(desc);
}

// Reads `count` uints back from `buffer` into `out`.
void readbackUints(SubgroupTest& t, WGPUBuffer buffer, size_t count, std::vector<uint32_t>& out) {
    out.assign(count, 0);
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            (void)len;
            std::memcpy(out.data(), actual, count * sizeof(uint32_t));
            return std::nullopt;
        },
        0, count * sizeof(uint32_t));
}

} // namespace

// ---------------------------------------------------------------------------
// Constant tables.
// ---------------------------------------------------------------------------

const std::vector<WGSize>& kWGSizes() {
    static const std::vector<WGSize> sizes = {
        {4, 1, 1},   {8, 1, 1},   {16, 1, 1},  {32, 1, 1},   {64, 1, 1},  {128, 1, 1}, {256, 1, 1},
        {1, 4, 1},   {1, 8, 1},   {1, 16, 1},  {1, 32, 1},   {1, 64, 1},  {1, 128, 1}, {1, 256, 1},
        {1, 1, 4},   {1, 1, 8},   {1, 1, 16},  {1, 1, 32},   {1, 1, 64},  {3, 3, 3},   {4, 4, 4},
        {16, 16, 1}, {16, 1, 16}, {1, 16, 16}, {15, 3, 3},   {3, 15, 3},  {3, 3, 15},
    };
    return sizes;
}

std::string wgSizeToString(const WGSize& size) {
    std::ostringstream out;
    out << "[" << size[0] << "," << size[1] << "," << size[2] << "]";
    return out.str();
}

WGSize wgSizeFromString(const std::string& text) {
    WGSize size{1, 1, 1};
    std::sscanf(text.c_str(), "[%u,%u,%u]", &size[0], &size[1], &size[2]);
    return size;
}

const std::vector<FramebufferSize>& kFramebufferSizes() {
    static const std::vector<FramebufferSize> sizes = {
        {15, 15}, {16, 16}, {17, 17}, {19, 13}, {13, 10}, {111, 3},
        {3, 111}, {35, 3},  {3, 35},  {53, 13}, {13, 53}, {3, 3},
    };
    return sizes;
}

std::string framebufferSizeToString(const FramebufferSize& size) {
    std::ostringstream out;
    out << "[" << size[0] << "," << size[1] << "]";
    return out.str();
}

FramebufferSize framebufferSizeFromString(const std::string& text) {
    FramebufferSize size{3, 3};
    std::sscanf(text.c_str(), "[%u,%u]", &size[0], &size[1]);
    return size;
}

const std::vector<PredicateCase>& kPredicateCases() {
    static const std::vector<PredicateCase> cases = {
        {"every_even", "id % 2 == 0",
         [](uint32_t id, uint32_t /*size*/) { return id % 2u == 0u; }},
        {"every_odd", "id % 2 == 1",
         [](uint32_t id, uint32_t /*size*/) { return id % 2u == 1u; }},
        {"lower_half", "id < subgroupSize / 2",
         [](uint32_t id, uint32_t size) { return id < size / 2u; }},
        {"upper_half", "id >= subgroupSize / 2",
         [](uint32_t id, uint32_t size) { return id >= size / 2u; }},
        {"first_two", "id == 0 || id == 1",
         [](uint32_t id, uint32_t /*size*/) { return id == 0u || id == 1u; }},
    };
    return cases;
}

const PredicateCase& predicateCaseByName(const std::string& name) {
    for (const PredicateCase& c : kPredicateCases()) {
        if (c.name == name) {
            return c;
        }
    }
    std::abort();
}

UintsPerFramebuffer getUintsPerFramebuffer(WGPUTextureFormat format, uint32_t width, uint32_t /*height*/) {
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    const uint32_t blocksPerRow = width / info.blockWidth;
    // 256 minimum arises from image copy requirements.
    const uint32_t bytesPerRow = alignUp(blocksPerRow * info.bytesPerBlock, 256);
    UintsPerFramebuffer result{};
    result.uintsPerRow = bytesPerRow / 4u;
    result.uintsPerTexel = info.bytesPerBlock / info.blockWidth / info.blockHeight / 4u;
    return result;
}

// ---------------------------------------------------------------------------
// runComputeTest.
// ---------------------------------------------------------------------------

void runComputeTest(
    SubgroupTest& t,
    const std::string& wgsl,
    const WGSize& wgSize,
    uint32_t outputUintsPerElement,
    const std::vector<uint32_t>& inputData,
    const std::function<std::optional<std::string>(
        const std::vector<uint32_t>& metadata,
        const std::vector<uint32_t>& output)>& checkFunction) {
    skipIfWorkgroupTooLarge(t, wgSize);

    const uint32_t wgThreads = wgSize[0] * wgSize[1] * wgSize[2];

    WGPUBuffer inputBuffer = makeStorageBuffer(t, inputData);

    const size_t outputUints = static_cast<size_t>(outputUintsPerElement) * wgThreads;
    std::vector<uint32_t> outputInit(outputUints, kDataSentinel);
    WGPUBuffer outputBuffer = makeStorageBuffer(t, outputInit);

    const size_t numMetadata = static_cast<size_t>(2) * wgThreads;
    std::vector<uint32_t> metadataInit(numMetadata, kDataSentinel);
    WGPUBuffer metadataBuffer = makeStorageBuffer(t, metadataInit);

    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout = nullptr; // auto
    pipeDesc.compute.module = module;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bg = makeThreeBufferBindGroup(t, bgl, inputBuffer, outputBuffer, metadataBuffer);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    std::vector<uint32_t> metadata;
    std::vector<uint32_t> output;
    readbackUints(t, metadataBuffer, numMetadata, metadata);
    readbackUints(t, outputBuffer, outputUints, output);

    const std::optional<std::string> error = checkFunction(metadata, output);
    if (error.has_value()) {
        t.fail(*error);
    }
}

// ---------------------------------------------------------------------------
// runFragmentTest.
// ---------------------------------------------------------------------------

void runFragmentTest(
    SubgroupTest& t,
    WGPUTextureFormat format,
    const std::string& fsShader,
    uint32_t width,
    uint32_t height,
    const std::vector<uint32_t>& inputData,
    FragmentInputKind inputKind,
    const std::function<std::optional<std::string>(const std::vector<uint32_t>& data)>& checker) {
    static const char* kVsShader = R"(
@vertex
fn vsMain(@builtin(vertex_index) index : u32) -> @builtin(position) vec4f {
  const vertices = array(
    vec2(-2, 4), vec2(-2, -4), vec2(2, 0),
  );
  return vec4f(vec2f(vertices[index]), 0, 1);
}
)";

    WGPUShaderModule vsModule = t.createShaderModuleTracked(kVsShader);
    WGPUShaderModule fsModule = t.createShaderModuleTracked(fsShader);

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = format;
    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = fsModule;
    fragment.entryPoint = sv("main");
    fragment.targetCount = 1;
    fragment.targets = &target;
    WGPURenderPipelineDescriptor rpDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    rpDesc.layout = nullptr; // auto
    rpDesc.vertex.module = vsModule;
    rpDesc.vertex.entryPoint = sv("vsMain");
    rpDesc.fragment = &fragment;
    rpDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(rpDesc);

    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    const uint32_t blocksPerRow = width / info.blockWidth;
    const uint32_t blocksPerColumn = height / info.blockHeight;
    const uint32_t bytesPerRow = alignUp(blocksPerRow * info.bytesPerBlock, 256);
    const uint64_t byteLength = static_cast<uint64_t>(bytesPerRow) * blocksPerColumn;
    const size_t uintLength = static_cast<size_t>(byteLength / 4u);

    // Expand each input element to a vec4-stride slot (component 0 holds the value).
    // The element interpretation comes from inputKind: U32/F32 are 4-byte elements
    // (one per uint in inputData); F16 elements are 2 bytes (two per uint).
    std::vector<uint8_t> expanded;
    if (inputKind == FragmentInputKind::F16) {
        const size_t elementCount = inputData.size() * 2; // two f16 per uint
        expanded.assign(elementCount * 4 * sizeof(uint16_t), 0);
        for (size_t i = 0; i < elementCount; ++i) {
            const uint16_t halfBits =
                static_cast<uint16_t>((inputData[i / 2] >> ((i % 2) * 16)) & 0xFFFFu);
            std::memcpy(&expanded[i * 4 * sizeof(uint16_t)], &halfBits, sizeof(halfBits));
        }
    } else {
        const size_t elementCount = inputData.size();
        expanded.assign(elementCount * 4 * sizeof(uint32_t), 0);
        for (size_t i = 0; i < elementCount; ++i) {
            const uint32_t v = inputData[i];
            std::memcpy(&expanded[i * 4 * sizeof(uint32_t)], &v, sizeof(v));
        }
    }
    if (expanded.empty()) {
        expanded.assign(16, 0); // at least one vec4 slot for the unused-input case
    }

    WGPUBuffer buffer = t.makeBufferWithContents(
        expanded.data(), expanded.size(),
        static_cast<WGPUBufferUsage>(WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst));

    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = buffer;
    entry.offset = 0;
    entry.size = expanded.size();
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries = &entry;
    WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{width, height, 1};
    texDesc.format = format;
    texDesc.usage = static_cast<WGPUTextureUsage>(
        WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst | WGPUTextureUsage_RenderAttachment |
        WGPUTextureUsage_TextureBinding);
    WGPUTexture framebuffer = t.createTextureTracked(texDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(framebuffer, viewDesc);
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
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    WGPUBufferDescriptor obDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    obDesc.size = byteLength;
    obDesc.usage = static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);
    WGPUBuffer outputBuffer = t.createBufferTracked(obDesc);
    t.copyTextureToBuffer(encoder, framebuffer, outputBuffer, bytesPerRow,
                          WGPUExtent3D{width, height, 1});
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    std::vector<uint32_t> data;
    readbackUints(t, outputBuffer, uintLength, data);

    const std::optional<std::string> error = checker(data);
    if (error.has_value()) {
        t.fail(*error);
    }
}

// ---------------------------------------------------------------------------
// runAccuracyTest.
// ---------------------------------------------------------------------------

namespace {

// checkAccuracy from subgroup_util.ts.
std::optional<std::string> checkAccuracy(
    const std::vector<uint32_t>& metadata,
    const std::vector<double>& output,
    uint32_t idx1,
    uint32_t idx2,
    double val1,
    double val2,
    double identity,
    const std::function<FPInterval(double, double)>& intervalGen) {
    const uint32_t subgroupIdIdx1 = metadata[idx1];
    const uint32_t subgroupIdIdx2 = metadata[idx2];
    for (size_t i = 0; i < output.size(); ++i) {
        const uint32_t subgroupId = metadata[i];
        const double v1 = subgroupId == subgroupIdIdx1 ? val1 : identity;
        const double v2 = subgroupId == subgroupIdIdx2 ? val2 : identity;
        const FPInterval interval = intervalGen(v1, v2);
        if (!interval.contains(output[i])) {
            std::ostringstream msg;
            msg << "Invocation " << i << ", subgroup id " << subgroupId
                << ": incorrect result\n- interval: [" << interval.begin << ", " << interval.end
                << "]\n- output: " << output[i];
            return msg.str();
        }
    }
    return std::nullopt;
}

} // namespace

void runAccuracyTest(
    SubgroupTest& t,
    uint32_t seed,
    const WGSize& wgSize,
    const std::string& operation,
    FPKind type,
    double identity,
    const std::function<FPInterval(double x, double y)>& intervalGen) {
    PRNG prng(seed);

    skipIfWorkgroupTooLarge(t, wgSize);

    // Bias half the cases to lower indices since most subgroup sizes are <= 64.
    uint32_t indexLimit = kStride;
    if (seed < kNumCases / 4) {
        indexLimit = 16;
    } else if (seed < kNumCases / 2) {
        indexLimit = 64;
    }

    // Ensure two distinct indices are picked.
    const uint32_t idx1 = prng.uniformInt(indexLimit);
    uint32_t idx2 = prng.uniformInt(indexLimit - 1);
    if (idx1 == idx2) {
        idx2++;
    }

    // Select two random values from the interesting range.
    const std::vector<double>& range =
        type == FPKind::F16 ? sparseScalarF16Range() : sparseScalarF32Range();
    const uint32_t numVals = static_cast<uint32_t>(range.size());
    const double val1 = range[prng.uniformInt(numVals)];
    const double val2 = range[prng.uniformInt(numVals)];

    const std::string extraEnables = type == FPKind::F16 ? "enable f16;" : "";
    const std::string typeName = type == FPKind::F16 ? "f16" : "f32";
    std::ostringstream wgsl;
    wgsl << "\nenable subgroups;\n"
         << extraEnables << "\n\n"
         << "@group(0) @binding(0)\n"
         << "var<storage> inputs : array<" << typeName << ">;\n\n"
         << "@group(0) @binding(1)\n"
         << "var<storage, read_write> outputs : array<" << typeName << ">;\n\n"
         << "struct Metadata {\n"
         << "  subgroup_id : array<u32, " << kStride << ">,\n"
         << "}\n\n"
         << "@group(0) @binding(2)\n"
         << "var<storage, read_write> metadata : Metadata;\n\n"
         << "@compute @workgroup_size(" << wgSize[0] << ", " << wgSize[1] << ", " << wgSize[2]
         << ")\n"
         << "fn main(\n"
         << "  @builtin(local_invocation_index) lid : u32,\n"
         << ") {\n"
         << "  metadata.subgroup_id[lid] = subgroupBroadcast(lid, 0);\n"
         << "  outputs[lid] = " << operation << "(inputs[lid]);\n"
         << "}";

    // Build packed input bytes (f16 packs two-per-uint; f32 one-per-uint).
    auto inputValueAt = [&](uint32_t x) -> double {
        if (x == idx1) {
            return val1;
        }
        if (x == idx2) {
            return val2;
        }
        return identity;
    };

    std::vector<uint32_t> inputData;
    if (type == FPKind::F16) {
        inputData.assign(kStride / 2, 0);
        for (uint32_t x = 0; x < kStride; ++x) {
            const uint16_t bits = f32ToF16Bits(inputValueAt(x));
            inputData[x / 2] |= static_cast<uint32_t>(bits) << ((x % 2) * 16);
        }
    } else {
        inputData.assign(kStride, 0);
        for (uint32_t x = 0; x < kStride; ++x) {
            const float f = static_cast<float>(inputValueAt(x));
            std::memcpy(&inputData[x], &f, sizeof(f));
        }
    }

    WGPUBuffer inputBuffer = makeStorageBuffer(t, inputData);

    // Output buffer (zero-initialized; f32 stores one per uint, f16 two per uint).
    const size_t outputUints = type == FPKind::F16 ? kStride / 2 : kStride;
    std::vector<uint32_t> outputInit(outputUints, 0);
    WGPUBuffer outputBuffer = makeStorageBuffer(t, outputInit);

    std::vector<uint32_t> metadataInit(kStride, 0);
    WGPUBuffer metadataBuffer = makeStorageBuffer(t, metadataInit);

    WGPUShaderModule module = t.createShaderModuleTracked(wgsl.str());
    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout = nullptr;
    pipeDesc.compute.module = module;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    WGPUBindGroup bg = makeThreeBufferBindGroup(t, bgl, inputBuffer, outputBuffer, metadataBuffer);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bg, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    std::vector<uint32_t> metadata;
    std::vector<uint32_t> outputRaw;
    readbackUints(t, metadataBuffer, kStride, metadata);
    readbackUints(t, outputBuffer, outputUints, outputRaw);

    // Decode output to doubles.
    std::vector<double> output(kStride, 0.0);
    if (type == FPKind::F16) {
        for (uint32_t i = 0; i < kStride; ++i) {
            const uint16_t bits = static_cast<uint16_t>((outputRaw[i / 2] >> ((i % 2) * 16)) & 0xFFFFu);
            output[i] = f16BitsToF32(bits);
        }
    } else {
        for (uint32_t i = 0; i < kStride; ++i) {
            float f;
            std::memcpy(&f, &outputRaw[i], sizeof(f));
            output[i] = static_cast<double>(f);
        }
    }

    const std::optional<std::string> error =
        checkAccuracy(metadata, output, idx1, idx2, val1, val2, identity, intervalGen);
    if (error.has_value()) {
        t.fail(*error);
    }
}

// ---------------------------------------------------------------------------
// generateTypedInputs.
// ---------------------------------------------------------------------------

std::vector<uint32_t> generateTypedInputs(
    const std::array<uint32_t, 4>& scalarValues,
    uint32_t elements,
    bool requiresF16) {
    if (requiresF16) {
        switch (elements) {
            case 1:
                return {
                    scalarValues[0] | (scalarValues[1] << 16),
                    scalarValues[2] | (scalarValues[3] << 16),
                };
            case 2:
                return {
                    scalarValues[0] | (scalarValues[0] << 16),
                    scalarValues[1] | (scalarValues[1] << 16),
                    scalarValues[2] | (scalarValues[2] << 16),
                    scalarValues[3] | (scalarValues[3] << 16),
                };
            case 3:
                return {
                    scalarValues[0] | (scalarValues[0] << 16),
                    scalarValues[0] | (kDataSentinel << 16),
                    scalarValues[1] | (scalarValues[1] << 16),
                    scalarValues[1] | (kDataSentinel << 16),
                    scalarValues[2] | (scalarValues[2] << 16),
                    scalarValues[2] | (kDataSentinel << 16),
                    scalarValues[3] | (scalarValues[3] << 16),
                    scalarValues[3] | (kDataSentinel << 16),
                };
            case 4:
                return {
                    scalarValues[0] | (scalarValues[0] << 16),
                    scalarValues[0] | (scalarValues[0] << 16),
                    scalarValues[1] | (scalarValues[1] << 16),
                    scalarValues[1] | (scalarValues[1] << 16),
                    scalarValues[2] | (scalarValues[2] << 16),
                    scalarValues[2] | (scalarValues[2] << 16),
                    scalarValues[3] | (scalarValues[3] << 16),
                    scalarValues[3] | (scalarValues[3] << 16),
                };
            default:
                std::abort();
        }
    }

    const uint32_t bound = elements == 3 ? 4 : elements;
    std::vector<uint32_t> values;
    values.reserve(static_cast<size_t>(4) * bound);
    for (uint32_t i = 0; i < 4; ++i) {
        for (uint32_t j = 0; j < bound; ++j) {
            values.push_back(j < elements ? scalarValues[i] : kDataSentinel);
        }
    }
    return values;
}

} // namespace subgroups
} // namespace cts
