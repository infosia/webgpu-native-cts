// Ported from gpuweb/cts src/webgpu/shader/execution/statement/discard.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// Port notes / deviations:
// - Upstream gates the storage-buffer variants on
//   `t.isCompatibility && !(limits.maxStorageBuffersInFragmentStage >= 2)`.
//   This harness has no isCompatibility flag, so the guard checks the
//   maxStorageBuffersInFragmentStage limit directly; core devices always
//   report >= 2, so behavior matches upstream on both core and compat.
// - Upstream's checkElementsPassPredicate table printer is simplified to a
//   first-failures message; the pass/fail predicates themselves are ported
//   exactly (exact equality for derivative values, per the spec rule).
// - The framebuffer staging buffer is pre-filled with the upstream sentinel
//   value (kWidth * kHeight) to detect unintended writes, exactly as
//   upstream does; the storage data buffer relies on WebGPU zero-init.

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
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
    "shader,execution,statement,discard",
    R"(
Execution tests for discard.

The discard statement converts invocations into helpers.
This results in the following conditions:
  * No outputs are written
  * No resources are written
  * Atomics are undefined

Conditions that still occur:
  * Derivative calculations are correct
  * Reads
  * Writes to non-external memory
)");

// Framebuffer dimensions
constexpr uint32_t kWidth = 64;
constexpr uint32_t kHeight = 64;
constexpr uint32_t kBytesPerWord = 4;
// Sentinel value the framebuffer staging buffer is pre-filled with.
constexpr uint32_t kSentinel = kWidth * kHeight;
// Number of vec2f data elements * 2 components (floats) in the data buffer.
constexpr size_t kDataElems = size_t{2} * kWidth * kHeight;
constexpr size_t kDataSize = kDataElems * kBytesPerWord;
constexpr size_t kFbElems = size_t{2} * kWidth * kHeight;
constexpr float kHalfWidth = 0.5f * static_cast<float>(kWidth);
constexpr float kHalfHeight = 0.5f * static_cast<float>(kHeight);

constexpr const char kSharedCode[] = R"(
@group(0) @binding(0) var<storage, read_write> output: array<vec2f>;
@group(0) @binding(1) var<storage, read_write> atomicIndex : atomic<u32>;
@group(0) @binding(2) var<uniform> uniformValues : array<vec4u, 5>;

@vertex
fn vsMain(@builtin(vertex_index) index : u32) -> @builtin(position) vec4f {
  const vertices = array(
    vec2(-1, -1), vec2(-1,  0), vec2( 0, -1),
    vec2(-1,  0), vec2( 0,  0), vec2( 0, -1),

    vec2( 0, -1), vec2( 0,  0), vec2( 1, -1),
    vec2( 0,  0), vec2( 1,  0), vec2( 1, -1),

    vec2(-1,  0), vec2(-1,  1), vec2( 0,  0),
    vec2(-1,  1), vec2( 0,  1), vec2( 0,  0),

    vec2( 0,  0), vec2( 0,  1), vec2( 1,  0),
    vec2( 0,  1), vec2( 1,  1), vec2( 1,  0),
  );
  return vec4f(vec2f(vertices[index]), 0, 1);
}
)";

WGPUStringView stringView(std::string_view value) {
    return WGPUStringView{value.data(), value.size()};
}

// Picks the storage-buffer snippet or the non-storage snippet (mirrors the
// upstream `useStorageBuffers ? ... : ...` template interpolation).
std::string selectSnippet(bool useStorageBuffers, const char* storage, const char* nonStorage) {
    if (useStorageBuffers) {
        return std::string(storage);
    }
    return std::string(nonStorage);
}

std::string maybeStorageSnippet(bool useStorageBuffers, const char* storage) {
    return selectSnippet(useStorageBuffers, storage, "");
}

// Simplified port of upstream checkElementsPassPredicate: runs `predicate`
// per element and reports the first few failing elements.
template <typename T, typename Predicate, typename Expected>
std::optional<std::string> checkElementsPass(
    const char* header,
    const std::vector<T>& values,
    Predicate&& predicate,
    Expected&& expected) {
    constexpr size_t kMaxReported = 8;
    size_t failureCount = 0;
    std::ostringstream details;
    for (size_t i = 0; i < values.size(); ++i) {
        if (!predicate(i, values)) {
            if (failureCount < kMaxReported) {
                details << "  [" << i << "] got " << values[i]
                        << ", expected " << expected(i) << "\n";
            }
            ++failureCount;
        }
    }
    if (failureCount == 0) {
        return std::nullopt;
    }
    std::ostringstream message;
    message << header << ": " << failureCount << " element(s) failed the predicate\n"
            << details.str();
    return message.str();
}

using DataChecker = std::function<std::optional<std::string>(const std::vector<float>&)>;
using FbChecker = std::function<std::optional<std::string>(const std::vector<uint32_t>&)>;

// No storage writes occur.
DataChecker zeroDataChecker() {
    return [](const std::vector<float>& a) {
        return checkElementsPass<float>(
            "data",
            a,
            [](size_t idx, const std::vector<float>& v) { return v[idx] == 0.0f; },
            [](size_t) { return std::string("0"); });
    };
}

// No fragment outputs occur.
FbChecker sentinelFbChecker() {
    return [](const std::vector<uint32_t>& a) {
        return checkElementsPass<uint32_t>(
            "fb",
            a,
            [](size_t idx, const std::vector<uint32_t>& v) { return v[idx] == kSentinel; },
            [](size_t) { return std::to_string(kSentinel); });
    };
}

// Port of upstream drawFullScreen(): renders 24 vertices (a fullscreen grid
// of 4 quads) into an rg32uint target pre-loaded with a sentinel value, then
// validates the storage data buffer (when used) and the framebuffer contents.
void drawFullScreen(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& code,
    bool useStorageBuffers,
    const DataChecker& dataChecker,
    const FbChecker& fbChecker) {
    if (useStorageBuffers) {
        const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();
        if (compat.maxStorageBuffersInFragmentStage < 2) {
            t.skip(
                "maxStorageBuffersInFragmentStage "
                + std::to_string(compat.maxStorageBuffersInFragmentStage) + " is less than 2");
        }
    }

    WGPUShaderModule module = t.createShaderModuleTracked(code);

    WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
    colorTarget.format = WGPUTextureFormat_RG32Uint;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = stringView("fsMain");
    fragment.targetCount = 1;
    fragment.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout = nullptr; // layout: 'auto'
    pipeDesc.vertex.module = module;
    pipeDesc.vertex.entryPoint = stringView("vsMain");
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count = 1;
    pipeDesc.fragment = &fragment;
    WGPURenderPipeline pipeline = t.createRenderPipelineTracked(pipeDesc);

    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size = WGPUExtent3D{kWidth, kHeight, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.format = WGPUTextureFormat_RG32Uint;
    texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst
        | WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;
    WGPUTexture framebuffer = t.createTextureTracked(texDesc);

    // Buffer the framebuffer contents are copied from/into.
    // Initialized with a sentinel value and loaded to detect unintended writes.
    const std::vector<uint32_t> fbInit(kFbElems, kSentinel);
    WGPUBuffer fbBuffer = t.makeBufferWithContents(
        fbInit.data(),
        fbInit.size() * sizeof(uint32_t),
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

    // Storage shader resources: vec2f array (kDataSize bytes) followed by
    // 'atomicIndex' packed at the end of the buffer. Zero-initialized.
    WGPUBufferDescriptor dataDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    dataDesc.size = kDataSize + kBytesPerWord;
    dataDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
    WGPUBuffer dataBuffer = t.createBufferTracked(dataDesc);

    // Loop bound, [derivative constants].
    const std::array<uint32_t, 20> uniformData = {
        4, 0, 0, 0,
        1, 0, 0, 0,
        4, 0, 0, 0,
        4, 0, 0, 0,
        7, 0, 0, 0,
    };
    constexpr size_t kUniformSize = kBytesPerWord * 5 * 4;
    WGPUBuffer uniformBuffer = t.makeBufferWithContents(
        uniformData.data(),
        kUniformSize,
        WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);

    WGPUBindGroupLayout bgl = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    std::vector<WGPUBindGroupEntry> entries;
    if (useStorageBuffers) {
        WGPUBindGroupEntry outputEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        outputEntry.binding = 0;
        outputEntry.buffer = dataBuffer;
        outputEntry.offset = 0;
        outputEntry.size = kDataSize;
        entries.push_back(outputEntry);

        WGPUBindGroupEntry atomicEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        atomicEntry.binding = 1;
        atomicEntry.buffer = dataBuffer;
        atomicEntry.offset = kDataSize;
        atomicEntry.size = kBytesPerWord;
        entries.push_back(atomicEntry);
    }
    WGPUBindGroupEntry uniformEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    uniformEntry.binding = 2;
    uniformEntry.buffer = uniformBuffer;
    uniformEntry.offset = 0;
    uniformEntry.size = kUniformSize;
    entries.push_back(uniformEntry);

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(framebuffer, viewDesc);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    t.copyBufferToTexture(
        encoder, fbBuffer, kWidth * kBytesPerWord * 2, framebuffer, WGPUExtent3D{kWidth, kHeight, 1});

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = view;
    colorAttachment.loadOp = WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderDraw(pass, 24, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    t.copyTextureToBuffer(
        encoder, framebuffer, fbBuffer, kWidth * kBytesPerWord * 2, WGPUExtent3D{kWidth, kHeight, 1});

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    if (useStorageBuffers) {
        t.expectGPUBufferValuesPassCheck(
            dataBuffer,
            [&dataChecker](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                std::vector<float> values(len / sizeof(float));
                std::memcpy(values.data(), actual, values.size() * sizeof(float));
                return dataChecker(values);
            },
            0,
            kDataSize);
    }

    t.expectGPUBufferValuesPassCheck(
        fbBuffer,
        [&fbChecker](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            std::vector<uint32_t> values(len / sizeof(uint32_t));
            std::memcpy(values.data(), actual, values.size() * sizeof(uint32_t));
            return fbChecker(values);
        },
        0,
        kFbElems * sizeof(uint32_t));
}

CTS_TEST(g, "all")
    .desc("Test a shader that discards all fragments")
    .params([](ParamsBuilder u) {
        return u.combine("useStorageBuffers", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool useStorageBuffers = t.param<bool>("useStorageBuffers");
        const std::string code = std::string(kSharedCode) + R"(
@fragment
fn fsMain(@builtin(position) pos : vec4f) -> @location(0) vec2u {
  _ = uniformValues[0];
  discard;
)" + maybeStorageSnippet(useStorageBuffers, R"(
  let idx = atomicAdd(&atomicIndex, 1);
  output[idx] = pos.xy;
)") + R"(
  return vec2u(1);
}
)";

        drawFullScreen(t, code, useStorageBuffers, zeroDataChecker(), sentinelFbChecker());
    });

CTS_TEST(g, "three_quarters")
    .desc("Test a shader that discards all but the upper-left quadrant fragments")
    .params([](ParamsBuilder u) {
        return u.combine("useStorageBuffers", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool useStorageBuffers = t.param<bool>("useStorageBuffers");
        const std::string code = std::string(kSharedCode) + R"(
@fragment
fn fsMain(@builtin(position) pos : vec4f) -> @location(0) vec2u {
  _ = uniformValues[0];
  if (pos.x >= 0.5 * 64 || pos.y >= 0.5 * 64) {
    discard;
  }
)" + selectSnippet(useStorageBuffers, R"(
  let idx = atomicAdd(&atomicIndex, 1);
  output[idx] = pos.xy;
  return vec2u(idx);
)", R"(
  return vec2(u32(pos.x), u32(pos.y));
)") + R"(
}
)";

        // Only the upper left quadrant is kept.
        DataChecker dataChecker = [](const std::vector<float>& a) {
            return checkElementsPass<float>(
                "data",
                a,
                [](size_t idx, const std::vector<float>& v) {
                    const bool isX = idx % 2 == 0;
                    if (isX) {
                        return v[idx] < kHalfWidth;
                    }
                    return v[idx] < kHalfHeight;
                },
                [](size_t idx) {
                    const bool isX = idx % 2 == 0;
                    return isX
                        ? "< " + std::to_string(kWidth / 2)
                        : "< " + std::to_string(kHeight / 2);
                });
        };
        FbChecker fbChecker = [useStorageBuffers](const std::vector<uint32_t>& a) {
            const auto discarded = [](size_t x, size_t y) {
                return x >= kWidth / 2 || y >= kHeight / 2;
            };
            return checkElementsPass<uint32_t>(
                "fb",
                a,
                [useStorageBuffers, discarded](size_t idx, const std::vector<uint32_t>& v) {
                    const size_t fragId = idx / 2;
                    const size_t x = fragId % kWidth;
                    const size_t y = fragId / kWidth;
                    if (discarded(x, y)) {
                        return v[idx] == kSentinel;
                    }
                    if (useStorageBuffers) {
                        return v[idx] < kSentinel / 4;
                    }
                    return size_t{v[idx]} == (idx % 2 != 0 ? y : x);
                },
                [useStorageBuffers, discarded](size_t idx) {
                    const size_t fragId = idx / 2;
                    const size_t x = fragId % kWidth;
                    const size_t y = fragId / kWidth;
                    if (discarded(x, y)) {
                        return std::to_string(kSentinel);
                    }
                    if (useStorageBuffers) {
                        return std::string("< ") + std::to_string(kSentinel / 4);
                    }
                    return std::to_string(idx % 2 != 0 ? y : x);
                });
        };

        drawFullScreen(t, code, useStorageBuffers, dataChecker, fbChecker);
    });

CTS_TEST(g, "function_call")
    .desc("Test discards happening in a function call")
    .params([](ParamsBuilder u) {
        return u.combine("useStorageBuffers", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool useStorageBuffers = t.param<bool>("useStorageBuffers");
        const std::string code = std::string(kSharedCode) + R"(
fn foo(pos : vec2f) {
  let p = vec2i(pos);
  if p.x <= 64 / 2 && p.y <= 64 / 2 {
    discard;
  }
  if p.x >= 64 / 2 && p.y >= 64 / 2 {
    discard;
  }
}

@fragment
fn fsMain(@builtin(position) pos : vec4f) -> @location(0) vec2u {
  _ = uniformValues[0];
  foo(pos.xy);
)" + selectSnippet(useStorageBuffers, R"(
  let idx = atomicAdd(&atomicIndex, 1);
  output[idx] = pos.xy;
  return vec2u(idx);
)", R"(
  return vec2u(u32(pos.x), u32(pos.y));
)") + R"(
}
)";

        // Only the upper right and bottom left quadrants are kept.
        DataChecker dataChecker = [](const std::vector<float>& a) {
            return checkElementsPass<float>(
                "data",
                a,
                [](size_t idx, const std::vector<float>& v) {
                    const bool isX = idx % 2 == 0;
                    const float value = v[idx];
                    if (value == 0.0f) {
                        return isX ? v[idx + 1] == 0.0f : v[idx - 1] == 0.0f;
                    }
                    const float expect = isX ? kHalfWidth : kHalfHeight;
                    if (value < expect) {
                        return isX ? v[idx + 1] > kHalfWidth : v[idx - 1] > kHalfHeight;
                    }
                    return isX ? v[idx + 1] < kHalfWidth : v[idx - 1] < kHalfHeight;
                },
                [](size_t idx) {
                    if (idx < (size_t{kWidth} * kHeight) / 2) {
                        return std::string("any");
                    }
                    return std::string("0");
                });
        };
        FbChecker fbChecker = [useStorageBuffers](const std::vector<uint32_t>& a) {
            const auto discarded = [](size_t x, size_t y) {
                return (x >= kWidth / 2 && y >= kHeight / 2)
                    || (x <= kWidth / 2 && y <= kHeight / 2);
            };
            return checkElementsPass<uint32_t>(
                "fb",
                a,
                [useStorageBuffers, discarded](size_t idx, const std::vector<uint32_t>& v) {
                    const size_t fragId = idx / 2;
                    const size_t x = fragId % kWidth;
                    const size_t y = fragId / kWidth;
                    if (discarded(x, y)) {
                        return v[idx] == kSentinel;
                    }
                    if (useStorageBuffers) {
                        return v[idx] < kSentinel / 2;
                    }
                    return size_t{v[idx]} == (idx % 2 != 0 ? y : x);
                },
                [useStorageBuffers, discarded](size_t idx) {
                    const size_t fragId = idx / 2;
                    const size_t x = fragId % kWidth;
                    const size_t y = fragId / kWidth;
                    if (discarded(x, y)) {
                        return std::to_string(kSentinel);
                    }
                    if (useStorageBuffers) {
                        return std::string("< ") + std::to_string(kSentinel / 2);
                    }
                    return std::to_string(idx % 2 != 0 ? y : x);
                });
        };

        drawFullScreen(t, code, useStorageBuffers, dataChecker, fbChecker);
    });

CTS_TEST(g, "loop")
    .desc("Test discards in a loop")
    .params([](ParamsBuilder u) {
        return u.combine("useStorageBuffers", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool useStorageBuffers = t.param<bool>("useStorageBuffers");
        const std::string code = std::string(kSharedCode) + R"(
@fragment
fn fsMain(@builtin(position) pos : vec4f) -> @location(0) vec2u {
  _ = uniformValues[0];
  for (var i = 0; i < 2; i++) {
    if i > 0 {
      discard;
    }
  }
)" + maybeStorageSnippet(useStorageBuffers, R"(
  let idx = atomicAdd(&atomicIndex, 1);
  output[idx] = pos.xy;
)") + R"(
  return vec2u(1);
}
)";

        drawFullScreen(t, code, useStorageBuffers, zeroDataChecker(), sentinelFbChecker());
    });

CTS_TEST(g, "continuing")
    .desc("Test discards in a loop")
    .params([](ParamsBuilder u) {
        return u.combine("useStorageBuffers", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool useStorageBuffers = t.param<bool>("useStorageBuffers");
        const std::string code = std::string(kSharedCode) + R"(
@fragment
fn fsMain(@builtin(position) pos : vec4f) -> @location(0) vec2u {
  _ = uniformValues[0];
  var i = 0;
  loop {
    continuing {
      if i > 0 {
        discard;
      }
      i++;
      break if i >= 2;
    }
  }
)" + maybeStorageSnippet(useStorageBuffers, R"(
  let idx = atomicAdd(&atomicIndex, 1);
  output[idx] = pos.xy;
)") + R"(
  return vec2u(1);
}
)";

        drawFullScreen(t, code, useStorageBuffers, zeroDataChecker(), sentinelFbChecker());
    });

CTS_TEST(g, "uniform_read_loop")
    .desc("Test that helpers read a uniform value in a loop")
    .params([](ParamsBuilder u) {
        return u.combine("useStorageBuffers", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool useStorageBuffers = t.param<bool>("useStorageBuffers");
        const std::string code = std::string(kSharedCode) + R"(
@fragment
fn fsMain(@builtin(position) pos : vec4f) -> @location(0) vec2u {
  discard;
  for (var i = 0u; i < uniformValues[0].x; i++) {
  }
)" + maybeStorageSnippet(useStorageBuffers, R"(
  let idx = atomicAdd(&atomicIndex, 1);
  output[idx] = pos.xy;
)") + R"(
  return vec2u(1);
}
)";

        drawFullScreen(t, code, useStorageBuffers, zeroDataChecker(), sentinelFbChecker());
    });

CTS_TEST(g, "derivatives")
    .desc("Test that derivatives are correct in the presence of discard")
    .params([](ParamsBuilder u) {
        return u.combine("useStorageBuffers", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool useStorageBuffers = t.param<bool>("useStorageBuffers");
        const std::string code = std::string(kSharedCode) + R"(
@fragment
fn fsMain(@builtin(position) pos : vec4f) -> @location(0) vec2u {
  let ipos = vec2i(pos.xy);
  let lsb = ipos & vec2(0x1);
  let left_sel = select(2, 4, lsb.y == 1);
  let right_sel = select(1, 3, lsb.y == 1);
  let uidx = select(left_sel, right_sel, lsb.x == 1);
  if ((lsb.x | lsb.y) & 0x1) == 0 {
    discard;
  }

  let v = uniformValues[uidx].x;
  let dx = dpdx(f32(v));
  let dy = dpdy(f32(v));
)" + selectSnippet(useStorageBuffers, R"(
  let idx = atomicAdd(&atomicIndex, 1);
  output[idx] = vec2(dx, dy);
  return vec2u(idx);
)", R"(
  return bitcast<vec2u>(vec2f(dx, dy));
)") + R"(
}
)";

        // One pixel per quad is discarded. The derivative values are always
        // the same +/- 3.
        DataChecker dataChecker = [](const std::vector<float>& a) {
            return checkElementsPass<float>(
                "data",
                a,
                [](size_t idx, const std::vector<float>& v) {
                    if (idx < (3 * kDataElems) / 4) {
                        return v[idx] == -3.0f || v[idx] == 3.0f;
                    }
                    return v[idx] == 0.0f;
                },
                [](size_t idx) {
                    if (idx < (3 * kDataElems) / 4) {
                        return std::string("+/- 3");
                    }
                    return std::string("0");
                });
        };
        // 3/4 of the fragments are written.
        FbChecker fbChecker = [useStorageBuffers](const std::vector<uint32_t>& a) {
            const auto discarded = [](size_t x, size_t y) {
                return ((x | y) & 0x1) == 0;
            };
            return checkElementsPass<uint32_t>(
                "fb",
                a,
                [useStorageBuffers, discarded](size_t idx, const std::vector<uint32_t>& v) {
                    const size_t fragId = idx / 2;
                    const size_t x = fragId % kWidth;
                    const size_t y = fragId / kWidth;
                    if (discarded(x, y)) {
                        return v[idx] == kSentinel;
                    }
                    if (useStorageBuffers) {
                        return v[idx] < (3 * kSentinel) / 4;
                    }
                    float asFloat = 0.0f;
                    const uint32_t bits = v[idx];
                    std::memcpy(&asFloat, &bits, sizeof(asFloat));
                    return asFloat == -3.0f || asFloat == 3.0f;
                },
                [useStorageBuffers, discarded](size_t idx) {
                    const size_t fragId = idx / 2;
                    const size_t x = fragId % kWidth;
                    const size_t y = fragId / kWidth;
                    if (discarded(x, y)) {
                        return std::to_string(kSentinel);
                    }
                    if (useStorageBuffers) {
                        return std::string("< ") + std::to_string((3 * kSentinel) / 4);
                    }
                    return std::string("+/- 3 (as f32 bits)");
                });
        };

        drawFullScreen(t, code, useStorageBuffers, dataChecker, fbChecker);
    });

} // namespace
