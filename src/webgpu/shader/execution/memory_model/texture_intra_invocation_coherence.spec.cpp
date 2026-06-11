// Ported from gpuweb/cts src/webgpu/shader/execution/memory_model/texture_intra_invocation_coherence.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes:
//  - Upstream uses UniqueFeaturesOrLimitsGPUTest (a default-limits device) because the
//    texture sizes are derived from maxComputeInvocationsPerWorkgroup and would exceed
//    the maximum texture size on a max-limits device (see the upstream MAINTENANCE_TODO).
//    The C API allows only one device per adapter, and the harness's shared adapter is
//    consumed by the cached all-features/max-limits device in suite runs, so a private
//    default-limits device must come from a private adapter. The UniqueDeviceGpuTest
//    fixture below routes device()/queue() to GpuTest::mismatchedDevice(), which
//    requests exactly that (a fresh adapter + default-features/default-limits device
//    from the harness's shared WGPUInstance, so the harness's event pumping and
//    readback helpers keep working) and is released by GpuTest::finalize().
//  - Upstream's t.skipIfLanguageFeatureNotSupported('readonly_and_readwrite_storage_textures')
//    and the WGSL `requires readonly_and_readwrite_storage_textures;` directive are omitted:
//    the WGPUInstance is not exposed through the harness API so
//    wgpuInstanceHasWGSLLanguageFeature cannot be called from test bodies, and the native
//    backends (naga/tint) support read_write storage textures unconditionally (consistent
//    with api/operation/storage_texture/read_write.spec.cpp and
//    api/validation/render_pipeline/resource_compatibility.spec.cpp).
//  - Upstream's beforeAllSubcases selectDeviceForTextureFormatOrSkipTestCase maps to the
//    runtime skipIfTextureFormatNotSupported check.
//  - skipIfTextureFormatNotUsableWithStorageAccessMode('read-write', format) maps to the
//    isTextureFormatUsableWithStorageAccessMode predicate + skip.
//  - For formats outside {r32uint, r32sint, r32float} upstream's texel() helper hits
//    unreachable() (only possible on a texture-formats-tier2 device); mirrored with t.fail.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/shader/execution/memory_model/memory_model_setup.h"
#include "webgpu/texture_format.h"

using namespace cts;
using namespace cts::memory_model;

namespace {

// Port of upstream's UniqueFeaturesOrLimitsGPUTest for this file: a per-case
// private device with default features and default limits. Backed by
// GpuTest::mismatchedDevice(), which requests a fresh adapter and device from
// the harness's shared WGPUInstance and releases both in GpuTest::finalize().
class UniqueDeviceGpuTest : public GpuTest {
  public:
    WGPUDevice device() const override {
        return const_cast<UniqueDeviceGpuTest*>(this)->mismatchedDevice();
    }

    WGPUQueue queue() const override {
        UniqueDeviceGpuTest* self = const_cast<UniqueDeviceGpuTest*>(this);
        if (self->privateQueue_ == nullptr) {
            self->privateQueue_ = wgpuDeviceGetQueue(self->device());
        }
        return self->privateQueue_;
    }

    void finalize() override {
        if (privateQueue_ != nullptr) {
            wgpuQueueRelease(privateQueue_);
            privateQueue_ = nullptr;
        }
        GpuTest::finalize();
    }

  private:
    WGPUQueue privateQueue_ = nullptr;
};

TestGroup<UniqueDeviceGpuTest> g = MakeTestGroup<UniqueDeviceGpuTest>(
    "shader,execution,memory_model,texture_intra_invocation_coherence",
    R"(
Test that read/write storage textures are coherent within an invocation.

Each invocation is assigned several random writing indices and a single
read index from among those. Writes are randomly predicated (except the
one corresponding to the read). Checks that an invocation can read data
it has written to the texture previously.
Does not test coherence between invocations

Some platform (e.g. Metal) require a fence call to make writes visible
to reads performed by the same invocation. These tests attempt to ensure
WebGPU implementations emit correct fence calls.)");

// Mirrors upstream kPossibleReadWriteStorageTextureFormats (format_info.ts):
// the three core read-write storage formats followed by the formats enabled
// by texture-formats-tier2, in upstream order.
constexpr std::array<const char*, 18> kPossibleReadWriteStorageTextureFormats = {
    "r32uint",
    "r32sint",
    "r32float",
    "r8unorm",
    "r8uint",
    "r8sint",
    "rgba8unorm",
    "rgba8uint",
    "rgba8sint",
    "r16uint",
    "r16sint",
    "r16float",
    "rgba16uint",
    "rgba16sint",
    "rgba16float",
    "rgba32uint",
    "rgba32sint",
    "rgba32float",
};

constexpr std::array<const char*, 4> kDimensions = {"1d", "2d", "2d-array", "3d"};

std::string indexToCoord(const std::string& dim) {
    if (dim == "1d") {
        return R"(
fn indexToCoord(idx : u32) -> u32 {
  return idx;
})";
    }
    if (dim == "2d" || dim == "2d-array") {
        return R"(
fn indexToCoord(idx : u32) -> vec2u {
  return vec2u(idx % (wgx * num_wgs_x), idx / (wgx * num_wgs_x));
})";
    }
    if (dim == "3d") {
        return R"(
fn indexToCoord(idx : u32) -> vec3u {
  return vec3u(idx % (wgx * num_wgs_x), idx / (wgx * num_wgs_x), 0);
})";
    }
    std::abort();
}

std::string textureType(const std::string& format, const std::string& dim) {
    std::string typeName = "texture_storage_";
    if (dim == "1d") {
        typeName += "1d";
    } else if (dim == "2d") {
        typeName += "2d";
    } else if (dim == "2d-array") {
        typeName += "2d_array";
    } else if (dim == "3d") {
        typeName += "3d";
    } else {
        std::abort();
    }
    typeName += "<" + format + ", read_write>";
    return typeName;
}

std::string textureStore(const std::string& dim, const std::string& index) {
    std::string code = "textureStore(t, indexToCoord(" + index + "), ";
    if (dim == "2d-array") {
        code += "0, ";
    }
    code += "texel)";
    return code;
}

std::string textureLoad(const std::string& dim, const std::string& format) {
    std::string code = "textureLoad(t, indexToCoord(read_index[global_index])";
    if (dim == "2d-array") {
        code += ", 0";
    }
    code += ").x";
    if (format != "r32uint") {
        code = "u32(" + code + ")";
    }
    return code;
}

// Returns the WGSL texel expression; mirrors upstream texel(), which hits
// unreachable() for any format outside the three core read-write formats.
std::string texelExpr(GpuTest& t, const std::string& format) {
    if (format == "r32uint") {
        return "vec4u(global_index,0,0,0)";
    }
    if (format == "r32sint") {
        return "vec4i(i32(global_index),0,0,0)";
    }
    if (format == "r32float") {
        return "vec4f(f32(global_index),0,0,0)";
    }
    t.fail("unhandled format: " + format);
}

WGPUExtent3D getTextureSize(uint32_t numTexels, const std::string& dim) {
    WGPUExtent3D size = WGPUExtent3D{1, 1, 1};
    if (dim == "1d") {
        size.width = numTexels;
    } else if (dim == "2d" || dim == "2d-array" || dim == "3d") {
        size.width = numTexels / 2;
        size.height = numTexels / 2;
        // depthOrArrayLayers stays 1
    } else {
        std::abort();
    }
    return size;
}

WGPUTextureViewDimension viewDimension(const std::string& dim) {
    if (dim == "1d") {
        return WGPUTextureViewDimension_1D;
    }
    if (dim == "2d") {
        return WGPUTextureViewDimension_2D;
    }
    if (dim == "2d-array") {
        return WGPUTextureViewDimension_2DArray;
    }
    if (dim == "3d") {
        return WGPUTextureViewDimension_3D;
    }
    std::abort();
}

WGPUTextureDimension textureDimension(const std::string& dim) {
    if (dim == "1d") {
        return WGPUTextureDimension_1D;
    }
    if (dim == "2d" || dim == "2d-array") {
        return WGPUTextureDimension_2D;
    }
    if (dim == "3d") {
        return WGPUTextureDimension_3D;
    }
    std::abort();
}

template <size_t N>
uint32_t indexInList(const std::array<const char*, N>& list, const std::string& value) {
    for (uint32_t i = 0; i < static_cast<uint32_t>(N); i++) {
        if (value == list[i]) {
            return i;
        }
    }
    std::abort();
}

CTS_TEST(g, "texture_intra_invocation_coherence")
    .desc("Tests writes from an invocation are visible to reads from the same invocation")
    .params([](ParamsBuilder u) {
        std::vector<Value> formats;
        formats.reserve(kPossibleReadWriteStorageTextureFormats.size());
        for (const char* format : kPossibleReadWriteStorageTextureFormats) {
            formats.emplace_back(format);
        }
        std::vector<Value> dims;
        dims.reserve(kDimensions.size());
        for (const char* dim : kDimensions) {
            dims.emplace_back(dim);
        }
        return u.combine("format", formats).combine("dim", dims);
    })
    .fn([](GpuTest& t) {
        const std::string format = t.param<std::string>("format");
        const std::string dim = t.param<std::string>("dim");

        const WGPUTextureFormat wgpuFormat = parseTextureFormat(format);
        // Upstream beforeAllSubcases: selectDeviceForTextureFormatOrSkipTestCase.
        t.skipIfTextureFormatNotSupported(wgpuFormat);
        // Upstream: skipIfTextureFormatNotUsableWithStorageAccessMode('read-write', format).
        if (!t.isTextureFormatUsableWithStorageAccessMode(
                wgpuFormat, WGPUStorageTextureAccess_ReadWrite)) {
            t.skip("texture format is not usable with read-write storage access");
        }

        const uint32_t wgx = 16;
        const uint32_t wgy = t.getLimits().maxComputeInvocationsPerWorkgroup / wgx;
        const uint32_t numWgsX = 2;
        const uint32_t numWgsY = 2;
        const uint32_t invocations = wgx * wgy * numWgsX * numWgsY;
        const uint32_t numWritesPerInvocation = 4;

        std::ostringstream codeStream;
        codeStream
            << "\n@group(0) @binding(0)\n"
            << "var t : " << textureType(format, dim) << ";\n"
            << "\n@group(1) @binding(0)\n"
            << "var<storage> write_indices : array<vec4u>;\n"
            << "\n@group(1) @binding(1)\n"
            << "var<storage> read_index : array<u32>;\n"
            << "\n@group(1) @binding(2)\n"
            << "var<storage> write_mask : array<vec4u>;\n"
            << "\n@group(1) @binding(3)\n"
            << "var<storage, read_write> output : array<u32>;\n"
            << "\nconst wgx = " << wgx << "u;\n"
            << "const wgy = " << wgy << "u;\n"
            << "const num_wgs_x = " << numWgsX << "u;\n"
            << "const num_wgs_y = " << numWgsY << "u;\n"
            << "\n" << indexToCoord(dim) << "\n"
            << "\n@compute @workgroup_size(wgx, wgy, 1)\n"
            << "fn main(@builtin(global_invocation_id) gid : vec3u) {\n"
            << "  let global_index = gid.x + gid.y * num_wgs_x * wgx;\n"
            << "\n"
            << "  let write_index = write_indices[global_index];\n"
            << "  let mask = write_mask[global_index];\n"
            << "  let texel = " << texelExpr(t, format) << ";\n"
            << "\n"
            << "  if mask.x != 0 {\n"
            << "    " << textureStore(dim, "write_index.x") << ";\n"
            << "  }\n"
            << "  if mask.y != 0 {\n"
            << "    " << textureStore(dim, "write_index.y") << ";\n"
            << "  }\n"
            << "  if mask.z != 0 {\n"
            << "    " << textureStore(dim, "write_index.z") << ";\n"
            << "  }\n"
            << "  if mask.w != 0 {\n"
            << "    " << textureStore(dim, "write_index.w") << ";\n"
            << "  }\n"
            << "  output[global_index] = " << textureLoad(dim, format) << ";\n"
            << "}";
        const std::string code = codeStream.str();

        // To get a variety of testing, seed the random number generator based on which case
        // this is. This means subcases will not execute the same code.
        const uint32_t seed =
            indexInList(kPossibleReadWriteStorageTextureFormats, format) *
                static_cast<uint32_t>(kPossibleReadWriteStorageTextureFormats.size()) +
            indexInList(kDimensions, dim);
        PRNG prng(seed);

        const uint32_t numWriteIndices = invocations * numWritesPerInvocation;
        std::vector<uint32_t> writeIndices(numWriteIndices);
        std::iota(writeIndices.begin(), writeIndices.end(), 0u);
        std::vector<uint32_t> writeMasks(numWriteIndices, 0u);
        // Shuffle the indices.
        for (uint32_t i = 0; i < numWriteIndices; i++) {
            const uint32_t remaining = numWriteIndices - i;
            const uint32_t swapIdx = (prng.randomU32() % remaining) + i;
            const uint32_t tmp = writeIndices[swapIdx];
            writeIndices[swapIdx] = writeIndices[i];
            writeIndices[i] = tmp;

            // Assign random write masks
            writeMasks[i] = prng.randomU32() % 2;
        }
        const uint32_t numReadIndices = invocations;
        std::vector<uint32_t> readIndices(numReadIndices, 0u);
        for (uint32_t i = 0; i < numReadIndices; i++) {
            // Pick a random index from this invocation's writes to read from.
            // Ensure that write is not masked out.
            const uint32_t readIdx = prng.randomU32() % numWritesPerInvocation;
            readIndices[i] = writeIndices[numWritesPerInvocation * i + readIdx];
            writeMasks[numWritesPerInvocation * i + readIdx] = 1;
        }

        // Buffers
        WGPUBuffer writeIndexBuffer = t.makeBufferWithContents(
            writeIndices.data(), writeIndices.size() * sizeof(uint32_t),
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage);
        WGPUBuffer readIndexBuffer = t.makeBufferWithContents(
            readIndices.data(), readIndices.size() * sizeof(uint32_t),
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage);
        WGPUBuffer writeMaskBuffer = t.makeBufferWithContents(
            writeMasks.data(), writeMasks.size() * sizeof(uint32_t),
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_Storage);
        // Output buffer is created zero-initialized (never pre-filled with expected values).
        WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        outputDesc.size = static_cast<uint64_t>(invocations) * 4;
        outputDesc.usage =
            WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
        WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

        // Texture
        const WGPUExtent3D textureSize =
            getTextureSize(invocations * numWritesPerInvocation, dim);
        WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        textureDesc.format = wgpuFormat;
        textureDesc.dimension = textureDimension(dim);
        textureDesc.size = textureSize;
        textureDesc.mipLevelCount = 1;
        textureDesc.sampleCount = 1;
        textureDesc.usage = WGPUTextureUsage_StorageBinding;
        WGPUTexture texture = t.createTextureTracked(textureDesc);

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);
        constexpr std::string_view kEntryPoint = "main";
        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = nullptr; // layout: 'auto'
        pipelineDesc.compute.module = shaderModule;
        pipelineDesc.compute.entryPoint = WGPUStringView{kEntryPoint.data(), kEntryPoint.size()};
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        viewDesc.format = wgpuFormat;
        viewDesc.dimension = viewDimension(dim);
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;
        WGPUTextureView textureView = t.createViewTracked(texture, viewDesc);

        // Bind group 0: the storage texture. The getter-returned bind group layouts must be
        // released manually after the bind groups are created.
        WGPUBindGroupLayout bgl0 = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry textureEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        textureEntry.binding = 0;
        textureEntry.textureView = textureView;
        WGPUBindGroupDescriptor bg0Desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bg0Desc.layout = bgl0;
        bg0Desc.entryCount = 1;
        bg0Desc.entries = &textureEntry;
        WGPUBindGroup bg0 = t.createBindGroupTracked(bg0Desc);
        wgpuBindGroupLayoutRelease(bgl0);

        // Bind group 1: the index/mask/output buffers.
        WGPUBindGroupLayout bgl1 = wgpuComputePipelineGetBindGroupLayout(pipeline, 1);
        std::array<WGPUBindGroupEntry, 4> bufferEntries;
        for (uint32_t i = 0; i < bufferEntries.size(); i++) {
            bufferEntries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            bufferEntries[i].binding = i;
            bufferEntries[i].offset = 0;
            bufferEntries[i].size = WGPU_WHOLE_SIZE;
        }
        bufferEntries[0].buffer = writeIndexBuffer;
        bufferEntries[1].buffer = readIndexBuffer;
        bufferEntries[2].buffer = writeMaskBuffer;
        bufferEntries[3].buffer = outputBuffer;
        WGPUBindGroupDescriptor bg1Desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bg1Desc.layout = bgl1;
        bg1Desc.entryCount = bufferEntries.size();
        bg1Desc.entries = bufferEntries.data();
        WGPUBindGroup bg1 = t.createBindGroupTracked(bg1Desc);
        wgpuBindGroupLayoutRelease(bgl1);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bg0, 0, nullptr);
        wgpuComputePassEncoderSetBindGroup(pass, 1, bg1, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, numWgsX, numWgsY, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

        std::vector<uint32_t> expectedOutput(numReadIndices);
        std::iota(expectedOutput.begin(), expectedOutput.end(), 0u);
        t.expectGPUBufferValuesEqual(
            outputBuffer, expectedOutput.data(), expectedOutput.size() * sizeof(uint32_t));
    });

} // namespace
