// Ported from gpuweb/cts src/webgpu/shader/execution/shader_io/compute_builtins.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for the reviewer):
//
// 1. linear_indexing (inputs test): upstream gates extra checks on
//    `t.hasLanguageFeature('linear_indexing')`. The C API's WGSL
//    language-feature query (wgpuInstanceHasWGSLLanguageFeature) is
//    unimplemented in wgpu-native and not implemented by yawgpu, and the
//    harness exposes no WGPUInstance to test bodies (precedent:
//    shader/execution/memory_layout.spec.cpp). linear_indexing is therefore
//    always treated as false: the global_invocation_index / workgroup_index
//    builtins are not emitted or checked, exactly matching upstream behavior
//    on implementations without that language feature. Case identity is
//    unaffected (linear_indexing is not a case param).
//
// 2. Object/array case params: upstream `groupSize`/`numGroups` are JS
//    objects and `sizes`/`lid` are JS arrays. The harness Value type has no
//    object/array variant, so each is encoded as the upstream JSON text in a
//    string param (zero_init.spec.cpp precedent), e.g.
//    groupSize="{"x":1,"y":1,"z":1}" and sizes="[1,1,1]". Values and ordering
//    mirror upstream exactly.
//
// 3. skipIfLanguageFeatureNotSupported('subgroup_id') (subgroup_id and
//    num_subgroups tests) cannot be queried (see note 1). The generated WGSL
//    keeps upstream's `requires subgroup_id;` directive verbatim, so a
//    backend without the language feature surfaces a shader-creation error
//    through the uncaptured-error hook instead of producing a silent pass
//    (swizzle_assignment.spec.cpp precedent). On such backends these tests
//    FAIL (loudly) where upstream would SKIP.
//
// 4. adapterInfo.subgroupMinSize/subgroupMaxSize: wgpuDeviceGetAdapterInfo is
//    unimplemented in wgpu-native (panics) and absent from yawgpu, so the
//    range is queried through a temporary WGPUInstance + default adapter via
//    wgpuAdapterGetInfo (error_scope.spec.cpp precedent for creating a local
//    instance in a test body), cached per process. This assumes the default
//    adapter is the same physical adapter the harness uses (true on the
//    single-GPU machines this suite targets).
//
// 5. subgroup_size_attribute: the 'subgroup-size-control' feature enum
//    (WGPUFeatureName_SubgroupSizeControl) only exists in Dawn's webgpu.h.
//    On non-Dawn backends the test body is compiled down to an unconditional
//    runtime-skip with a reason, which faithfully mirrors a device without
//    the feature (and avoids MSVC C4702 unreachable-code). Test registration
//    (listing) is identical on all backends.
//
// 6. Upstream's `await t.device.popErrorScope()` (subgroup_size_attribute)
//    needs an event pump; the harness does not expose its WGPUInstance, so
//    the pop callback (mode AllowProcessEvents) is pumped indirectly through
//    t.onSubmittedWorkDoneSync(), which processes events on the shared
//    harness instance.
//
// 7. Upstream's `t.fail(...); break;` inside the subgroup_size_attribute
//    readback loop records the failure and keeps iterating outer subgroup
//    sizes; this harness's fail() throws, so the first mismatch ends the
//    test. The final verdict (fail) is the same.
//
// 8. `readGPUBufferRangeTyped` + `expectOK` are replaced with
//    expectGPUBufferValuesPassCheck used as a synchronous readback (the check
//    lambda copies the data out), followed by an explicit check + t.fail.
//
// 9. checkSubgroupInvocationIdConsistency: upstream tracks ballots in a
//    BigInt; the port uses a fixed 256-bit ballot. The "too large" popcount
//    intentionally counts only the low 128 bits, mirroring upstream's
//    4 x 32-bit popcount quirk. A subgroup_invocation_id >= 256 is reported
//    as an explicit error (unreachable for conformant outputs; upstream's
//    BigInt would silently extend).
//
// 10. checkSubgroupIdConsistency: upstream's `seen[sid] !== 0` is also true
//     for an out-of-bounds sid (undefined !== 0), yielding the "reused"
//     error; the port mirrors that by bounds-checking first.
//
// 11. checkSubgroupSizeConsistency: upstream's trailing `invocations`
//     parameter is unused in its body and is omitted here.
//
// 12. `t.skipIf(cond, msg)` is not a harness method; ported as
//     `if (cond) { t.skip(msg); }`. Zero-filled upstream buffers
//     (makeBufferWithContents of zeros) become zero-initialized
//     createBufferTracked storage buffers; 999-prefilled buffers keep the
//     upstream sentinel via makeBufferWithContents.

#include <array>
#include <cstdint>
#include <cstring>
#include <map>
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
    "shader,execution,shader_io,compute_builtins",
    "Test compute shader builtin variables");

static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// Small shared helpers
// ---------------------------------------------------------------------------

struct Vec3U {
    uint32_t x;
    uint32_t y;
    uint32_t z;
};

// Extracts up to three unsigned integers from a JSON-style param string such
// as "[3,7,5]" or "{"x":3,"y":7,"z":5}" (the keys contain no digits).
std::array<uint32_t, 3> parseTriple(const std::string& text) {
    std::array<uint32_t, 3> out{};
    size_t count = 0;
    size_t i = 0;
    while (i < text.size() && count < 3) {
        const char c = text[i];
        if (c >= '0' && c <= '9') {
            uint32_t v = 0;
            while (i < text.size() && text[i] >= '0' && text[i] <= '9') {
                v = v * 10u + static_cast<uint32_t>(text[i] - '0');
                ++i;
            }
            out[count++] = v;
        } else {
            ++i;
        }
    }
    return out;
}

Vec3U parseVec3(const std::string& text) {
    const std::array<uint32_t, 3> t3 = parseTriple(text);
    return Vec3U{t3[0], t3[1], t3[2]};
}

// Faithful port of upstream popcount() (32-bit population count).
uint32_t popcount32(uint32_t input) {
    uint32_t n = input;
    n = n - ((n >> 1) & 0x55555555u);
    n = (n & 0x33333333u) + ((n >> 2) & 0x33333333u);
    return (((n + (n >> 4)) & 0x0f0f0f0fu) * 0x01010101u) >> 24;
}

// Faithful port of upstream ErrorMsg().
std::string errorMsg(const std::string& msg, uint32_t got, uint32_t expected) {
    return msg + ":\n-      got: " + std::to_string(got) +
           "\n- expected: " + std::to_string(expected);
}

// Faithful port of upstream genLID(): returns a WGSL function generating a
// linear local id with the dimension order permuted by (p0, p1, p2).
std::string genLID(uint32_t p0, uint32_t p1, uint32_t p2, const std::array<uint32_t, 3>& sizes) {
    return std::string("\nfn getLID(lid : vec3u) -> u32 {\n") +
           "  let p0 = lid[" + std::to_string(p0) + "];\n" +
           "  let p1 = lid[" + std::to_string(p1) + "] * " + std::to_string(sizes[p0]) + ";\n" +
           "  let p2 = lid[" + std::to_string(p2) + "] * " + std::to_string(sizes[p0]) + " * " +
           std::to_string(sizes[p1]) + ";\n" +
           "  return p0 + p1 + p2;\n" +
           "}";
}

// Upstream kWGSizes table, verbatim values and order.
constexpr std::array<std::array<uint32_t, 3>, 31> kWGSizes = {{
    {{1, 1, 1}},
    {{4, 1, 1}},
    {{8, 1, 1}},
    {{16, 1, 1}},
    {{32, 1, 1}},
    {{64, 1, 1}},
    {{128, 1, 1}},
    {{256, 1, 1}},
    {{1, 4, 1}},
    {{1, 8, 1}},
    {{1, 16, 1}},
    {{1, 32, 1}},
    {{1, 64, 1}},
    {{1, 128, 1}},
    {{1, 256, 1}},
    {{1, 1, 4}},
    {{1, 1, 8}},
    {{1, 1, 16}},
    {{1, 1, 32}},
    {{1, 1, 64}},
    {{3, 3, 3}},
    {{4, 4, 4}},
    {{16, 16, 1}},
    {{16, 1, 16}},
    {{1, 16, 16}},
    {{15, 3, 3}},
    {{3, 15, 3}},
    {{3, 3, 15}},
    {{17, 5, 2}},
    {{17, 4, 2}},
    {{15, 2, 8}},
}};

// Upstream lid permutation table, verbatim values and order.
constexpr std::array<std::array<uint32_t, 3>, 6> kLIDPermutations = {{
    {{0, 1, 2}},
    {{0, 2, 1}},
    {{1, 0, 2}},
    {{1, 2, 0}},
    {{2, 0, 1}},
    {{2, 1, 0}},
}};

std::string tripleToJsonArray(const std::array<uint32_t, 3>& t3) {
    return "[" + std::to_string(t3[0]) + "," + std::to_string(t3[1]) + "," +
           std::to_string(t3[2]) + "]";
}

std::vector<Value> wgSizesValues() {
    std::vector<Value> values;
    values.reserve(kWGSizes.size());
    for (const auto& s : kWGSizes) {
        values.emplace_back(tripleToJsonArray(s));
    }
    return values;
}

std::vector<Value> lidValues() {
    std::vector<Value> values;
    values.reserve(kLIDPermutations.size());
    for (const auto& p : kLIDPermutations) {
        values.emplace_back(tripleToJsonArray(p));
    }
    return values;
}

// Zero-initialized storage buffer holding `count` u32 values (WebGPU
// guarantees zero-init; upstream fills these with zeros explicitly).
WGPUBuffer createZeroedU32Buffer(GpuTest& t, uint64_t count) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = count * sizeof(uint32_t);
    desc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    return t.createBufferTracked(desc);
}

// Storage buffer holding `count` u32 values pre-filled with the upstream
// sentinel (e.g. placeholderValue = 999).
WGPUBuffer createFilledU32Buffer(GpuTest& t, uint64_t count, uint32_t fill) {
    std::vector<uint32_t> data(static_cast<size_t>(count), fill);
    return t.makeBufferWithContents(
        data.data(),
        data.size() * sizeof(uint32_t),
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
}

// layout:'auto' compute pipeline with entry point "main".
WGPUComputePipeline createComputePipelineAuto(GpuTest& t, const std::string& wgsl) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    // layout:auto (null)
    pipeDesc.compute.module = shaderModule;
    pipeDesc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(pipeDesc);
}

struct BufferBinding {
    WGPUBuffer buffer;
    uint64_t size;
};

// Bind group at group 0 with sequential bindings 0..N-1, using the
// pipeline's auto layout. The entries vector outlives the create call.
WGPUBindGroup makeAutoBindGroup(GpuTest& t,
                                WGPUComputePipeline pipeline,
                                const std::vector<BufferBinding>& buffers) {
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    std::vector<WGPUBindGroupEntry> entries(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[i].binding = static_cast<uint32_t>(i);
        entries[i].buffer = buffers[i].buffer;
        entries[i].offset = 0;
        entries[i].size = buffers[i].size;
    }

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);
    return bindGroup;
}

// Encode + submit a single compute pass dispatching (x, 1, 1) workgroups.
void runComputePassX(GpuTest& t,
                     WGPUComputePipeline pipeline,
                     WGPUBindGroup bindGroup,
                     uint32_t x) {
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, x, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);
}

// Synchronous u32 readback built on expectGPUBufferValuesPassCheck
// (see header note 8).
std::vector<uint32_t> readBackU32(GpuTest& t, WGPUBuffer buffer, size_t count) {
    std::vector<uint32_t> out(count, 0);
    t.expectGPUBufferValuesPassCheck(
        buffer,
        [&out, count](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            if (len < count * sizeof(uint32_t)) {
                return std::string("readback buffer too small");
            }
            std::memcpy(out.data(), actual, count * sizeof(uint32_t));
            return std::nullopt;
        },
        /*srcByteOffset=*/0,
        count * sizeof(uint32_t));
    return out;
}

// ---------------------------------------------------------------------------
// adapterInfo.subgroupMinSize / subgroupMaxSize (see header note 4)
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// inputs
// ---------------------------------------------------------------------------

// Faithful port of upstream checkEachIndex() with linear_indexing = false
// (see header note 1). `output` views the readback as u32 words.
std::optional<std::string> checkInputsOutput(const uint32_t* output,
                                             size_t wordCount,
                                             Vec3U groupSize,
                                             Vec3U numGroups) {
    // Offsets are in u32 size units (upstream constants; the
    // global_index/group_index fields do not exist without linear_indexing).
    constexpr uint32_t kLocalIdOffset = 0;
    constexpr uint32_t kLocalIndexOffset = 3;
    constexpr uint32_t kGlobalIdOffset = 4;
    constexpr uint32_t kGroupIdOffset = 8;
    constexpr uint32_t kNumGroupsOffset = 12;
    constexpr uint32_t kOutputElementSize = 16;

    const uint32_t invocationsPerGroup = groupSize.x * groupSize.y * groupSize.z;
    const uint64_t totalInvocations = static_cast<uint64_t>(invocationsPerGroup) * numGroups.x *
                                      numGroups.y * numGroups.z;
    if (wordCount < totalInvocations * kOutputElementSize) {
        return std::string("readback buffer too small");
    }

    // Loop over workgroups.
    for (uint32_t gz = 0; gz < numGroups.z; gz++) {
        for (uint32_t gy = 0; gy < numGroups.y; gy++) {
            for (uint32_t gx = 0; gx < numGroups.x; gx++) {
                // Loop over invocations within a group.
                for (uint32_t lz = 0; lz < groupSize.z; lz++) {
                    for (uint32_t ly = 0; ly < groupSize.y; ly++) {
                        for (uint32_t lx = 0; lx < groupSize.x; lx++) {
                            const uint32_t groupIndex =
                                (gz * numGroups.y + gy) * numGroups.x + gx;
                            const uint32_t localIndex =
                                (lz * groupSize.y + ly) * groupSize.x + lx;
                            const uint32_t globalIndex =
                                groupIndex * invocationsPerGroup + localIndex;
                            const size_t globalOffset =
                                static_cast<size_t>(globalIndex) * kOutputElementSize;
                            const uint32_t gidX = gx * groupSize.x + lx;
                            const uint32_t gidY = gy * groupSize.y + ly;
                            const uint32_t gidZ = gz * groupSize.z + lz;

                            // The stray ')' after local(...) mirrors the
                            // upstream error text verbatim.
                            auto expectEqual =
                                [&](const std::string& name, uint32_t expected,
                                    uint32_t actual) -> std::optional<std::string> {
                                if (actual != expected) {
                                    std::ostringstream msg;
                                    msg << name << " failed at group(" << gx << "," << gy << ","
                                        << gz << ") local(" << lx << "," << ly << "," << lz
                                        << "))\n"
                                        << "    expected: " << expected << "\n"
                                        << "    got:      " << actual;
                                    return msg.str();
                                }
                                return std::nullopt;
                            };

                            auto checkVec3Value =
                                [&](const std::string& name, uint32_t fieldOffset,
                                    Vec3U expected) -> std::optional<std::string> {
                                const size_t offset = globalOffset + fieldOffset;
                                if (auto e = expectEqual(name + ".x", expected.x,
                                                         output[offset + 0])) {
                                    return e;
                                }
                                if (auto e = expectEqual(name + ".y", expected.y,
                                                         output[offset + 1])) {
                                    return e;
                                }
                                return expectEqual(name + ".z", expected.z, output[offset + 2]);
                            };

                            if (auto e = checkVec3Value("local_id", kLocalIdOffset,
                                                        Vec3U{lx, ly, lz})) {
                                return e;
                            }
                            if (auto e = checkVec3Value("global_id", kGlobalIdOffset,
                                                        Vec3U{gidX, gidY, gidZ})) {
                                return e;
                            }
                            if (auto e = checkVec3Value("group_id", kGroupIdOffset,
                                                        Vec3U{gx, gy, gz})) {
                                return e;
                            }
                            if (auto e = checkVec3Value("num_groups", kNumGroupsOffset,
                                                        numGroups)) {
                                return e;
                            }
                            if (auto e = expectEqual("local_index", localIndex,
                                                     output[globalOffset + kLocalIndexOffset])) {
                                return e;
                            }
                        }
                    }
                }
            }
        }
    }
    return std::nullopt;
}

CTS_TEST(g, "inputs")
    .desc("Test compute shader builtin inputs values")
    .params([](ParamsBuilder u) {
        return u
            .combine("method", {"param", "struct", "mixed"})
            .combine("dispatch", {"direct", "indirect"})
            .combineWithParams({
                ParamRecord{{"groupSize", "{\"x\":1,\"y\":1,\"z\":1}"},
                            {"numGroups", "{\"x\":1,\"y\":1,\"z\":1}"}},
                ParamRecord{{"groupSize", "{\"x\":8,\"y\":4,\"z\":2}"},
                            {"numGroups", "{\"x\":1,\"y\":1,\"z\":1}"}},
                ParamRecord{{"groupSize", "{\"x\":1,\"y\":1,\"z\":1}"},
                            {"numGroups", "{\"x\":8,\"y\":4,\"z\":2}"}},
                ParamRecord{{"groupSize", "{\"x\":3,\"y\":7,\"z\":5}"},
                            {"numGroups", "{\"x\":13,\"y\":9,\"z\":11}"}},
            })
            .beginSubcases();
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // linear_indexing is always false in this port (see header note 1).
        const std::string method = t.param<std::string>("method");
        const std::string dispatchType = t.param<std::string>("dispatch");
        const Vec3U groupSize = parseVec3(t.param<std::string>("groupSize"));
        const Vec3U numGroups = parseVec3(t.param<std::string>("numGroups"));

        const uint32_t invocationsPerGroup = groupSize.x * groupSize.y * groupSize.z;
        const uint32_t totalInvocations =
            invocationsPerGroup * numGroups.x * numGroups.y * numGroups.z;

        // Generate the structures, parameters, and builtin expressions used
        // in the shader (the linear_indexing-only lines are omitted).
        std::string params;
        std::string structures;
        std::string localIdExpr;
        std::string localIndexExpr;
        std::string globalIdExpr;
        std::string groupIdExpr;
        std::string numGroupsExpr;
        if (method == "param") {
            params =
                "\n          @builtin(local_invocation_id) local_id : vec3<u32>,\n"
                "          @builtin(local_invocation_index) local_index : u32,\n"
                "          @builtin(global_invocation_id) global_id : vec3<u32>,\n"
                "          @builtin(workgroup_id) group_id : vec3<u32>,\n"
                "          @builtin(num_workgroups) num_groups : vec3<u32>,\n";
            localIdExpr = "local_id";
            localIndexExpr = "local_index";
            globalIdExpr = "global_id";
            groupIdExpr = "group_id";
            numGroupsExpr = "num_groups";
        } else if (method == "struct") {
            structures =
                "struct Inputs {\n"
                "            @builtin(local_invocation_id) local_id : vec3<u32>,\n"
                "            @builtin(local_invocation_index) local_index : u32,\n"
                "            @builtin(global_invocation_id) global_id : vec3<u32>,\n"
                "            @builtin(workgroup_id) group_id : vec3<u32>,\n"
                "            @builtin(num_workgroups) num_groups : vec3<u32>,\n"
                "          };";
            params = "inputs : Inputs";
            localIdExpr = "inputs.local_id";
            localIndexExpr = "inputs.local_index";
            globalIdExpr = "inputs.global_id";
            groupIdExpr = "inputs.group_id";
            numGroupsExpr = "inputs.num_groups";
        } else {  // mixed
            structures =
                "struct InputsA {\n"
                "          @builtin(local_invocation_index) local_index : u32,\n"
                "          @builtin(global_invocation_id) global_id : vec3<u32>,\n"
                "        };\n"
                "        struct InputsB {\n"
                "          @builtin(workgroup_id) group_id : vec3<u32>\n"
                "        };";
            params =
                "@builtin(local_invocation_id) local_id : vec3<u32>,\n"
                "                  inputsA : InputsA,\n"
                "                  inputsB : InputsB,\n"
                "                  @builtin(num_workgroups) num_groups : vec3<u32>,\n";
            localIdExpr = "local_id";
            localIndexExpr = "inputsA.local_index";
            globalIdExpr = "inputsA.global_id";
            groupIdExpr = "inputsB.group_id";
            numGroupsExpr = "num_groups";
        }

        // WGSL shader that stores every builtin value to a buffer, for every
        // invocation in the grid.
        const std::string wgsl =
            std::string(
                "\n      struct Outputs {\n"
                "        local_id: vec3u,\n"
                "        local_index: u32,\n"
                "        global_id: vec3u,\n"
                "        group_id: vec3u,\n"
                "        num_groups: vec3u,\n"
                "      };\n"
                "      @group(0) @binding(0) var<storage, read_write> outputs : array<Outputs>;\n"
                "\n      ") +
            structures + "\n\n" +
            "      const group_width = " + std::to_string(groupSize.x) + "u;\n" +
            "      const group_height = " + std::to_string(groupSize.y) + "u;\n" +
            "      const group_depth = " + std::to_string(groupSize.z) + "u;\n" +
            "\n" +
            "      @compute @workgroup_size(group_width, group_height, group_depth)\n" +
            "      fn main(\n        " + params + "\n        ) {\n" +
            "        let o_group_index = ((" + groupIdExpr + ".z * " + numGroupsExpr + ".y) + " +
            groupIdExpr + ".y) * " + numGroupsExpr + ".x + " + groupIdExpr + ".x;\n" +
            "        let o_global_index = o_group_index * " + std::to_string(invocationsPerGroup) +
            "u + " + localIndexExpr + ";\n" +
            "        var o: Outputs;\n" +
            "        o.local_id = " + localIdExpr + ";\n" +
            "        o.local_index = " + localIndexExpr + ";\n" +
            "        o.global_id = " + globalIdExpr + ";\n" +
            "        o.group_id = " + groupIdExpr + ";\n" +
            "        o.num_groups = " + numGroupsExpr + ";\n" +
            "        outputs[o_global_index] = o;\n" +
            "      }\n";

        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);

        // Outputs element size in u32 units (linear_indexing = false).
        constexpr uint32_t kOutputElementSize = 16;

        // Create the output buffer (zero-initialized; upstream relies on
        // WebGPU zero-init too — the output is never pre-filled with
        // expected values).
        const uint64_t bufferSize =
            static_cast<uint64_t>(totalInvocations) * kOutputElementSize * 4u;
        WGPUBufferDescriptor outDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        outDesc.size = bufferSize;
        outDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer outputBuffer = t.createBufferTracked(outDesc);

        WGPUBindGroup bindGroup = makeAutoBindGroup(t, pipeline, {{outputBuffer, bufferSize}});

        // Run the shader.
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        if (dispatchType == "direct") {
            wgpuComputePassEncoderDispatchWorkgroups(pass, numGroups.x, numGroups.y, numGroups.z);
        } else {
            const uint32_t dispatchData[3] = {numGroups.x, numGroups.y, numGroups.z};
            WGPUBuffer dispatchBuffer = t.makeBufferWithContents(
                dispatchData, sizeof(dispatchData), WGPUBufferUsage_Indirect);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(pass, dispatchBuffer, 0);
        }
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        t.expectGPUBufferValuesPassCheck(
            outputBuffer,
            [groupSize, numGroups](const uint8_t* actual,
                                   size_t len) -> std::optional<std::string> {
                return checkInputsOutput(
                    reinterpret_cast<const uint32_t*>(actual), len / 4, groupSize, numGroups);
            },
            /*srcByteOffset=*/0,
            static_cast<size_t>(bufferSize));
    });

// ---------------------------------------------------------------------------
// subgroup_size
// ---------------------------------------------------------------------------

// Faithful port of upstream checkSubgroupSizeConsistency() (the unused
// `invocations` parameter is omitted — see header note 11).
std::optional<std::string> checkSubgroupSizeConsistency(
    const std::vector<uint32_t>& subgroupSizes,
    const std::vector<uint32_t>& ballotSizes,
    uint32_t min,
    uint32_t max) {
    if (subgroupSizes.empty()) {
        return std::string("no subgroup size data");
    }
    const uint32_t subgroupSize = subgroupSizes[0];
    if (popcount32(subgroupSize) != 1) {
        return "Subgroup size '" + std::to_string(subgroupSize) + "' is not a power of two";
    }
    if (subgroupSize < min) {
        return "Subgroup size '" + std::to_string(subgroupSize) + "' is less than minimum '" +
               std::to_string(min) + "'";
    }
    if (max < subgroupSize) {
        return "Subgroup size '" + std::to_string(subgroupSize) + "' is greater than maximum '" +
               std::to_string(max) + "'";
    }

    // Check that remaining invocations record a consistent subgroup size.
    for (size_t i = 1; i < subgroupSizes.size(); i++) {
        if (subgroupSizes[i] != subgroupSize) {
            return errorMsg("Invocation " + std::to_string(i) + ": subgroup size inconsistency",
                            subgroupSizes[i], subgroupSize);
        }
    }

    for (size_t i = 0; i < ballotSizes.size(); i++) {
        if (ballotSizes[i] > subgroupSize) {
            return "Invocation " + std::to_string(i) + ", subgroup size, " +
                   std::to_string(ballotSizes[i]) + ", is greater than built-in value, " +
                   std::to_string(subgroupSize);
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "subgroup_size")
    .desc("Tests subgroup_size values")
    .params([](ParamsBuilder u) {
        return u
            .combine("sizes", wgSizesValues())
            .beginSubcases()
            .combine("numWGs", {1, 2})
            .combine("lid", lidValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
            t.skip("device does not have the 'subgroups' feature");
        }
        const SubgroupRange range = querySubgroupRange(t);

        const std::array<uint32_t, 3> sizes = parseTriple(t.param<std::string>("sizes"));
        const std::array<uint32_t, 3> lid = parseTriple(t.param<std::string>("lid"));
        const uint32_t numWGs = static_cast<uint32_t>(t.param<int>("numWGs"));
        const uint32_t wgx = sizes[0];
        const uint32_t wgy = sizes[1];
        const uint32_t wgz = sizes[2];
        const uint32_t wgThreads = wgx * wgy * wgz;

        // Compatibility mode has lower workgroup limits.
        const WGPULimits limits = t.getLimits();
        if (limits.maxComputeInvocationsPerWorkgroup < wgThreads ||
            limits.maxComputeWorkgroupSizeX < wgx ||
            limits.maxComputeWorkgroupSizeY < wgy ||
            limits.maxComputeWorkgroupSizeZ < wgz) {
            t.skip("Workgroup size too large");
        }

        const std::string wgsl =
            std::string("\nenable subgroups;\n") +
            "\nconst stride = " + std::to_string(wgThreads) + ";\n" +
            genLID(lid[0], lid[1], lid[2], sizes) + "\n" +
            "\n@group(0) @binding(0)\n" +
            "var<storage, read_write> output : array<u32>;\n" +
            "\n@group(0) @binding(1)\n" +
            "var<storage, read_write> compare : array<u32>;\n" +
            "\n@compute @workgroup_size(" + std::to_string(wgx) + ", " + std::to_string(wgy) +
            ", " + std::to_string(wgz) + ")\n" +
            "fn main(@builtin(subgroup_size) size : u32,\n" +
            "        @builtin(workgroup_id) wgid : vec3u,\n" +
            "        @builtin(local_invocation_id) local_id : vec3u) {\n" +
            "  // Remap local ids according to test linearity.\n" +
            "  let lid = getLID(local_id);\n" +
            "\n" +
            "  output[lid + wgid.x * stride] = size;\n" +
            "  let ballot = countOneBits(subgroupBallot(true));\n" +
            "  let ballotSize = ballot[0] + ballot[1] + ballot[2] + ballot[3];\n" +
            "  compare[lid + wgid.x * stride] = ballotSize;\n" +
            "}";

        const uint32_t numInvocations = wgThreads * numWGs;
        WGPUBuffer sizesBuffer = createZeroedU32Buffer(t, numInvocations);
        WGPUBuffer compareBuffer = createZeroedU32Buffer(t, numInvocations);

        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBindGroup bindGroup = makeAutoBindGroup(
            t, pipeline,
            {{sizesBuffer, static_cast<uint64_t>(numInvocations) * sizeof(uint32_t)},
             {compareBuffer, static_cast<uint64_t>(numInvocations) * sizeof(uint32_t)}});
        runComputePassX(t, pipeline, bindGroup, numWGs);

        const std::vector<uint32_t> sizesData = readBackU32(t, sizesBuffer, numInvocations);
        const std::vector<uint32_t> compareData = readBackU32(t, compareBuffer, numInvocations);

        if (auto err = checkSubgroupSizeConsistency(sizesData, compareData, range.minSize,
                                                    range.maxSize)) {
            t.fail(*err);
        }
    });

// ---------------------------------------------------------------------------
// subgroup_invocation_id
// ---------------------------------------------------------------------------

// 256-bit ballot replacing upstream's BigInt (see header note 9).
struct Ballot256 {
    std::array<uint64_t, 4> words{};

    void set(uint32_t bit) {
        words[bit >> 6] |= (uint64_t{1} << (bit & 63u));
    }

    // Mirrors upstream's popcount over the low 128 bits (4 x 32-bit chunks).
    uint32_t popcountLow128() const {
        uint32_t onebits = popcount32(static_cast<uint32_t>(words[0]));
        onebits += popcount32(static_cast<uint32_t>(words[0] >> 32));
        onebits += popcount32(static_cast<uint32_t>(words[1]));
        onebits += popcount32(static_cast<uint32_t>(words[1] >> 32));
        return onebits;
    }

    // (ballot & (ballot + 1)) == 0, i.e. the set bits are contiguous from
    // bit 0 (or the ballot is empty).
    bool contiguousFromLSB() const {
        std::array<uint64_t, 4> plusOne = words;
        for (size_t w = 0; w < plusOne.size(); ++w) {
            if (plusOne[w] != ~uint64_t{0}) {
                plusOne[w] += 1;
                break;
            }
            plusOne[w] = 0;
        }
        for (size_t w = 0; w < words.size(); ++w) {
            if ((words[w] & plusOne[w]) != 0) {
                return false;
            }
        }
        return true;
    }

    // Mirrors BigInt.prototype.toString(2) for error messages.
    std::string toBinaryString() const {
        int highest = -1;
        for (int bit = 255; bit >= 0; --bit) {
            if ((words[static_cast<size_t>(bit) >> 6] >> (static_cast<uint32_t>(bit) & 63u)) &
                1u) {
                highest = bit;
                break;
            }
        }
        if (highest < 0) {
            return "0";
        }
        std::string out;
        out.reserve(static_cast<size_t>(highest) + 1);
        for (int bit = highest; bit >= 0; --bit) {
            const bool isSet =
                ((words[static_cast<size_t>(bit) >> 6] >>
                  (static_cast<uint32_t>(bit) & 63u)) &
                 1u) != 0;
            out.push_back(isSet ? '1' : '0');
        }
        return out;
    }
};

// Faithful port of upstream checkSubgroupInvocationIdConsistency().
std::optional<std::string> checkSubgroupInvocationIdConsistency(
    const std::vector<uint32_t>& data,
    const std::vector<uint32_t>& ids,
    uint32_t subgroupSize,
    uint32_t invocations,
    uint32_t numWGs) {
    for (uint32_t wg = 0; wg < numWGs; wg++) {
        // Tracks an effective ballot of each subgroup based on the
        // representative id (global id of invocation 0 in the subgroup).
        std::map<uint32_t, Ballot256> mappings;
        for (uint32_t i = 0; i < invocations; i++) {
            const size_t idx = static_cast<size_t>(i) + static_cast<size_t>(invocations) * wg;
            const uint32_t subgroupId = ids[idx];
            if (subgroupId == 999u) {
                return "Invocation " + std::to_string(i) + ": no data";
            }
            const uint32_t bit = data[idx];
            if (bit >= 256u) {
                // Port guard (see header note 9); unreachable for conformant outputs.
                return "Invocation " + std::to_string(i) + ": subgroup_invocation_id " +
                       std::to_string(bit) + " exceeds the supported ballot width (256)";
            }
            mappings[subgroupId].set(bit);
        }

        for (const auto& entry : mappings) {
            const uint32_t id = entry.first;
            const Ballot256& ballot = entry.second;

            const uint32_t onebits = ballot.popcountLow128();
            if (onebits > subgroupSize) {
                return "Subgroup including invocation " + std::to_string(id) +
                       " is too large, " + std::to_string(onebits) + ", for subgroup size, " +
                       std::to_string(subgroupSize);
            }

            if (!ballot.contiguousFromLSB()) {
                // "non-continguous" mirrors the upstream message verbatim.
                return "Subgroup including invocation " + std::to_string(id) +
                       " has non-continguous ids: " + ballot.toBinaryString();
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "subgroup_invocation_id")
    .desc(
        "Tests subgroup_invocation_id values. No mapping between local_invocation_index and "
        "subgroup_invocation_id can be relied upon.")
    .params([](ParamsBuilder u) {
        return u
            .combine("sizes", wgSizesValues())
            .beginSubcases()
            .combine("numWGs", {1, 2})
            .combine("lid", lidValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
            t.skip("device does not have the 'subgroups' feature");
        }
        const std::array<uint32_t, 3> sizes = parseTriple(t.param<std::string>("sizes"));
        const std::array<uint32_t, 3> lid = parseTriple(t.param<std::string>("lid"));
        const uint32_t numWGs = static_cast<uint32_t>(t.param<int>("numWGs"));
        const uint32_t wgx = sizes[0];
        const uint32_t wgy = sizes[1];
        const uint32_t wgz = sizes[2];
        const uint32_t wgThreads = wgx * wgy * wgz;

        // Compatibility mode has lower workgroup limits.
        const WGPULimits limits = t.getLimits();
        if (limits.maxComputeInvocationsPerWorkgroup < wgThreads ||
            limits.maxComputeWorkgroupSizeX < wgx ||
            limits.maxComputeWorkgroupSizeY < wgy ||
            limits.maxComputeWorkgroupSizeZ < wgz) {
            t.skip("Workgroup size too large");
        }

        const std::string wgsl =
            std::string("\nenable subgroups;\n") +
            "\nconst stride = " + std::to_string(wgThreads) + ";\n" +
            genLID(lid[0], lid[1], lid[2], sizes) + "\n" +
            "\n@group(0) @binding(0)\n" +
            "var<storage, read_write> output : array<u32>;\n" +
            "\n// This var stores the global id of invocation 0 in the subgroup.\n" +
            "@group(0) @binding(1)\n" +
            "var<storage, read_write> subgroup_ids : array<u32>;\n" +
            "\n@group(0) @binding(2)\n" +
            "var<storage, read_write> sizes : array<u32>;\n" +
            "\n@compute @workgroup_size(" + std::to_string(wgx) + ", " + std::to_string(wgy) +
            ", " + std::to_string(wgz) + ")\n" +
            "fn main(@builtin(subgroup_size) size : u32,\n" +
            "        @builtin(subgroup_invocation_id) id : u32,\n" +
            "        @builtin(workgroup_id) wgid : vec3u,\n" +
            "        @builtin(local_invocation_id) local_id : vec3u) {\n" +
            "  // Remap local ids according to test linearity.\n" +
            "  let lid = getLID(local_id);\n" +
            "\n" +
            "  // Representative subgroup_id value.\n" +
            "  let gid = lid + stride * wgid.x;\n" +
            "\n" +
            "  let b = subgroupBroadcast(gid, 0);\n" +
            "  output[gid] = id;\n" +
            "  subgroup_ids[gid] = b;\n" +
            "  if (lid == 0) {\n" +
            "    sizes[wgid.x] = size;\n" +
            "  }\n" +
            "}";

        const uint32_t numInvocations = wgThreads * numWGs;
        constexpr uint32_t kPlaceholderValue = 999;
        WGPUBuffer outputBuffer = createZeroedU32Buffer(t, numInvocations);
        WGPUBuffer idsBuffer = createFilledU32Buffer(t, numInvocations, kPlaceholderValue);
        WGPUBuffer sizeBuffer = createZeroedU32Buffer(t, numWGs);

        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBindGroup bindGroup = makeAutoBindGroup(
            t, pipeline,
            {{outputBuffer, static_cast<uint64_t>(numInvocations) * sizeof(uint32_t)},
             {idsBuffer, static_cast<uint64_t>(numInvocations) * sizeof(uint32_t)},
             {sizeBuffer, static_cast<uint64_t>(numWGs) * sizeof(uint32_t)}});
        runComputePassX(t, pipeline, bindGroup, numWGs);

        // Upstream reads only one element of the size buffer (typedLength: 1).
        const std::vector<uint32_t> sizeData = readBackU32(t, sizeBuffer, 1);
        const std::vector<uint32_t> outputData = readBackU32(t, outputBuffer, numInvocations);
        const std::vector<uint32_t> idsData = readBackU32(t, idsBuffer, numInvocations);

        if (auto err = checkSubgroupInvocationIdConsistency(outputData, idsData, sizeData[0],
                                                            wgThreads, numWGs)) {
            t.fail(*err);
        }
    });

// ---------------------------------------------------------------------------
// subgroup_id
// ---------------------------------------------------------------------------

// Upstream `const skipValue = 0xffff0000` (interpolated into WGSL as
// 4294901760u).
constexpr uint32_t kSkipValue = 0xffff0000u;

// Faithful port of upstream checkSubgroupIdConsistency().
std::optional<std::string> checkSubgroupIdConsistency(const std::vector<uint32_t>& outputData,
                                                      uint32_t wgSize,
                                                      uint32_t numWGs) {
    for (uint32_t wg = 0; wg < numWGs; wg++) {
        // Max wgSize is 256 and min subgroup size is 4
        std::vector<uint32_t> seen((wgSize + 3u) / 4u, 0u);
        for (uint32_t inv = 0; inv < wgSize; inv++) {
            const size_t gid = static_cast<size_t>(wg) * wgSize + inv;
            const size_t outputIdx = gid * 4u;
            const uint32_t compare = outputData[outputIdx];
            const uint32_t inRange = outputData[outputIdx + 1];
            const uint32_t sid = outputData[outputIdx + 2];

            if (compare != 1u) {
                return "Invocation " + std::to_string(gid) +
                       ": not all invocations in subgroup have same subgroup_id: " +
                       std::to_string(compare);
            }
            if (inRange != 1u) {
                return "Invocation " + std::to_string(gid) +
                       ": subgroup_id out of range of num_subgroups: " + std::to_string(inRange);
            }

            if (sid != kSkipValue) {
                // An out-of-bounds sid also reports "reused" in upstream JS
                // (undefined !== 0); mirrored here (see header note 10).
                if (sid >= seen.size() || seen[sid] != 0u) {
                    return "Invocation " + std::to_string(gid) +
                           ": subgroup_id reused among different subgroups";
                }
                seen[sid] = 1u;
            }
        }

        int64_t firstZero = -1;
        int64_t lastOne = -1;
        for (size_t i = 0; i < seen.size(); ++i) {
            if (seen[i] == 0u && firstZero == -1) {
                firstZero = static_cast<int64_t>(i);
            }
            if (seen[i] == 1u) {
                lastOne = static_cast<int64_t>(i);
            }
        }
        if (firstZero != -1 && firstZero < lastOne) {
            return "Subgroup id values are not densely packed: missing " +
                   std::to_string(firstZero);
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "subgroup_id")
    .desc(
        "Tests subgroup_id values. No mapping between local_invocation_index and subgroup_id can "
        "be relied upon.")
    .params([](ParamsBuilder u) {
        return u
            .combine("sizes", wgSizesValues())
            .beginSubcases()
            .combine("numWGs", {1, 2})
            .combine("lid", lidValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
            t.skip("device does not have the 'subgroups' feature");
        }
        // Upstream: t.skipIfLanguageFeatureNotSupported('subgroup_id').
        // Not queryable here; the WGSL keeps `requires subgroup_id;` so an
        // unsupported backend errors loudly instead (see header note 3).
        const std::array<uint32_t, 3> sizes = parseTriple(t.param<std::string>("sizes"));
        const std::array<uint32_t, 3> lid = parseTriple(t.param<std::string>("lid"));
        const uint32_t numWGs = static_cast<uint32_t>(t.param<int>("numWGs"));
        const uint32_t wgx = sizes[0];
        const uint32_t wgy = sizes[1];
        const uint32_t wgz = sizes[2];
        const uint32_t wgThreads = wgx * wgy * wgz;

        // Compatibility mode has lower workgroup limits.
        const WGPULimits limits = t.getLimits();
        if (limits.maxComputeInvocationsPerWorkgroup < wgThreads ||
            limits.maxComputeWorkgroupSizeX < wgx ||
            limits.maxComputeWorkgroupSizeY < wgy ||
            limits.maxComputeWorkgroupSizeZ < wgz) {
            t.skip("Workgroup size too large");
        }

        const std::string wgsl =
            std::string("\nenable subgroups;\nrequires subgroup_id;\n") +
            "\nconst stride = " + std::to_string(wgThreads) + ";\n" +
            genLID(lid[0], lid[1], lid[2], sizes) + "\n" +
            "\n@group(0) @binding(0)\n" +
            "var<storage, read_write> output : array<vec4u>;\n" +
            "\n@compute @workgroup_size(" + std::to_string(wgx) + ", " + std::to_string(wgy) +
            ", " + std::to_string(wgz) + ")\n" +
            "fn main(@builtin(local_invocation_id) local_id : vec3u,\n" +
            "        @builtin(workgroup_id) wgid : vec3u,\n" +
            "        @builtin(subgroup_id) sid : u32,\n" +
            "        @builtin(num_subgroups) num_subgroups : u32) {\n" +
            "  // Remapped local id.\n" +
            "  let lid = getLID(local_id);\n" +
            "\n" +
            "  let gid = lid + stride * wgid.x;\n" +
            "\n" +
            "  // Is the subgroup_id equivalent for all members?\n" +
            "  let broadcast_id = subgroupBroadcastFirst(sid);\n" +
            "  let compare = subgroupAll(broadcast_id == sid);\n" +
            "\n" +
            "  // Is subgroup_id in the range of num_subgroups?\n" +
            "  let in_range = sid < num_subgroups;\n" +
            "\n" +
            "  var out_sid = " + std::to_string(kSkipValue) + "u;\n" +
            "  if subgroupElect() {\n" +
            "    out_sid = sid;\n" +
            "  }\n" +
            "\n" +
            "  output[gid] = vec4u(\n" +
            "    select(0u, 1u, compare),\n" +
            "    select(0u, 1u, in_range),\n" +
            "    out_sid,\n" +
            "    0);\n" +
            "}\n";

        const uint32_t numInvocations = wgThreads * numWGs;
        const uint32_t numUints = 4u * numInvocations;
        constexpr uint32_t kPlaceholderValue = 999;
        WGPUBuffer outputBuffer = createFilledU32Buffer(t, numUints, kPlaceholderValue);

        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBindGroup bindGroup = makeAutoBindGroup(
            t, pipeline,
            {{outputBuffer, static_cast<uint64_t>(numUints) * sizeof(uint32_t)}});
        runComputePassX(t, pipeline, bindGroup, numWGs);

        const std::vector<uint32_t> outputData = readBackU32(t, outputBuffer, numUints);

        if (auto err = checkSubgroupIdConsistency(outputData, wgThreads, numWGs)) {
            t.fail(*err);
        }
    });

// ---------------------------------------------------------------------------
// num_subgroups
// ---------------------------------------------------------------------------

// Faithful port of upstream checkNumSubgroupsConsistency().
std::optional<std::string> checkNumSubgroupsConsistency(const std::vector<uint32_t>& countData,
                                                        const std::vector<uint32_t>& outputData,
                                                        uint32_t wgSize,
                                                        uint32_t numWGs) {
    for (uint32_t wg = 0; wg < numWGs; wg++) {
        const uint32_t count = countData[wg];
        for (uint32_t i = 0; i < wgSize; i++) {
            const uint32_t got = outputData[static_cast<size_t>(wg) * wgSize + i];
            if (got != count) {
                return "Workgroup " + std::to_string(wg) +
                       ": inconsistent num_subgroups:\n- expected: " + std::to_string(count) +
                       "\n-      got: " + std::to_string(got);
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "num_subgroups")
    .desc("Tests num_subgroups values.")
    .params([](ParamsBuilder u) {
        return u
            .combine("sizes", wgSizesValues())
            .beginSubcases()
            .combine("numWGs", {1, 2})
            .combine("lid", lidValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
            t.skip("device does not have the 'subgroups' feature");
        }
        // Upstream: t.skipIfLanguageFeatureNotSupported('subgroup_id').
        // Not queryable here; the WGSL keeps `requires subgroup_id;` so an
        // unsupported backend errors loudly instead (see header note 3).
        const std::array<uint32_t, 3> sizes = parseTriple(t.param<std::string>("sizes"));
        const std::array<uint32_t, 3> lid = parseTriple(t.param<std::string>("lid"));
        const uint32_t numWGs = static_cast<uint32_t>(t.param<int>("numWGs"));
        const uint32_t wgx = sizes[0];
        const uint32_t wgy = sizes[1];
        const uint32_t wgz = sizes[2];
        const uint32_t wgThreads = wgx * wgy * wgz;

        // Compatibility mode has lower workgroup limits.
        const WGPULimits limits = t.getLimits();
        if (limits.maxComputeInvocationsPerWorkgroup < wgThreads ||
            limits.maxComputeWorkgroupSizeX < wgx ||
            limits.maxComputeWorkgroupSizeY < wgy ||
            limits.maxComputeWorkgroupSizeZ < wgz) {
            t.skip("Workgroup size too large");
        }

        const std::string wgsl =
            std::string("\nenable subgroups;\nrequires subgroup_id;\n") +
            "\nconst stride = " + std::to_string(wgThreads) + ";\n" +
            genLID(lid[0], lid[1], lid[2], sizes) + "\n" +
            "\n@group(0) @binding(0)\n" +
            "var<storage, read_write> numSubgroups : array<u32>;\n" +
            "\n@group(0) @binding(1)\n" +
            "var<storage, read_write> output : array<u32>;\n" +
            "\nvar<workgroup> count : atomic<u32>;\n" +
            "\n@compute @workgroup_size(" + std::to_string(wgx) + ", " + std::to_string(wgy) +
            ", " + std::to_string(wgz) + ")\n" +
            "fn main(@builtin(local_invocation_id) local_id : vec3u,\n" +
            "        @builtin(workgroup_id) wgid : vec3u,\n" +
            "        @builtin(subgroup_id) sid : u32,\n" +
            "        @builtin(num_subgroups) num_subgroups : u32) {\n" +
            "  // Remapped local id.\n" +
            "  let lid = getLID(local_id);\n" +
            "\n" +
            "  let gid = lid + stride * wgid.x;\n" +
            "\n" +
            "  if subgroupElect() {\n" +
            "    atomicAdd(&count, 1);\n" +
            "  }\n" +
            "\n" +
            "  workgroupBarrier();\n" +
            "\n" +
            "  if lid == 0 {\n" +
            "    numSubgroups[wgid.x] = atomicLoad(&count);\n" +
            "  }\n" +
            "\n" +
            "  output[gid] = num_subgroups;\n" +
            "}\n";

        const uint32_t numInvocations = wgThreads * numWGs;
        constexpr uint32_t kPlaceholderValue = 999;
        WGPUBuffer countBuffer = createFilledU32Buffer(t, numWGs, kPlaceholderValue);
        // Upstream allocates numInvocations * 4 elements (4x larger than the
        // shader needs); mirrored verbatim.
        WGPUBuffer outputBuffer =
            createFilledU32Buffer(t, static_cast<uint64_t>(numInvocations) * 4u,
                                  kPlaceholderValue);

        WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
        WGPUBindGroup bindGroup = makeAutoBindGroup(
            t, pipeline,
            {{countBuffer, static_cast<uint64_t>(numWGs) * sizeof(uint32_t)},
             {outputBuffer, static_cast<uint64_t>(numInvocations) * 4u * sizeof(uint32_t)}});
        runComputePassX(t, pipeline, bindGroup, numWGs);

        const std::vector<uint32_t> countData = readBackU32(t, countBuffer, numWGs);
        const std::vector<uint32_t> outputData = readBackU32(t, outputBuffer, numInvocations);

        if (auto err = checkNumSubgroupsConsistency(countData, outputData, wgThreads, numWGs)) {
            t.fail(*err);
        }
    });

// ---------------------------------------------------------------------------
// subgroup_size_attribute
// ---------------------------------------------------------------------------

#if defined(CTS_BACKEND_DAWN)
struct PopScopeState {
    bool completed = false;
    WGPUPopErrorScopeStatus status = WGPUPopErrorScopeStatus_Error;
    WGPUErrorType type = WGPUErrorType_NoError;
};

void onPopErrorScope(WGPUPopErrorScopeStatus status,
                     WGPUErrorType type,
                     WGPUStringView,
                     void* userdata1,
                     void*) {
    auto* state = static_cast<PopScopeState*>(userdata1);
    state->status = status;
    state->type = type;
    state->completed = true;
}

// Pops the current validation error scope and returns whether an error was
// captured. The callback is pumped via onSubmittedWorkDoneSync (see header
// note 6).
bool popValidationScopeHadError(GpuTest& t) {
    PopScopeState state;
    WGPUPopErrorScopeCallbackInfo callbackInfo = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = onPopErrorScope;
    callbackInfo.userdata1 = &state;
    (void)wgpuDevicePopErrorScope(t.device(), callbackInfo);

    for (int i = 0; i < 100 && !state.completed; ++i) {
        t.onSubmittedWorkDoneSync();
    }
    if (!state.completed) {
        t.fail("popErrorScope did not complete");
    }
    if (state.status != WGPUPopErrorScopeStatus_Success) {
        t.fail("popErrorScope failed");
    }
    return state.type != WGPUErrorType_NoError;
}
#endif  // defined(CTS_BACKEND_DAWN)

CTS_TEST(g, "subgroup_size_attribute")
    .desc(
        "Tests that at least one power-of-two value in [subgroupMinSize, subgroupMaxSize] can be "
        "used as\n    the @subgroup_size attribute in a simple compute pipeline. The value of the "
        "subgroup_size\n    builtin must equal the value of the @subgroup_size attribute.")
    .params([](ParamsBuilder u) {
        return u.combine("numWorkGroups", {1, 2}).combine("numSubgroups", {1, 2, 4});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Upstream: t.skipIfDeviceDoesNotHaveFeature('subgroup-size-control').
        // The feature enum only exists in Dawn's webgpu.h (see header note 5).
        // The whole body is compiled out on non-Dawn backends so that the
        // unconditional skip leaves no unreachable code (MSVC /W4 C4702).
#if !defined(CTS_BACKEND_DAWN)
        t.skip(
            "device does not have the 'subgroup-size-control' feature "
            "(not exposed by this backend's webgpu.h)");
#else
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_SubgroupSizeControl)) {
            t.skip("device does not have the 'subgroup-size-control' feature");
        }

        const uint32_t numWorkGroups = static_cast<uint32_t>(t.param<int>("numWorkGroups"));
        const uint32_t numSubgroups = static_cast<uint32_t>(t.param<int>("numSubgroups"));

        const SubgroupRange range = querySubgroupRange(t);
        if (range.minSize == 0) {
            t.fail("adapter reports subgroupMinSize == 0");
        }

        bool atLeastOneSucceeded = false;

        for (uint32_t subgroupSize = range.minSize; subgroupSize <= range.maxSize;
             subgroupSize *= 2) {
            const uint32_t wgx = subgroupSize * numSubgroups;

            const std::string wgsl =
                std::string("\nenable subgroups;\nenable subgroup_size_control;\n") +
                "\n@group(0) @binding(0)\n" +
                "var<storage, read_write> output : array<u32>;\n" +
                "\n@compute @workgroup_size(" + std::to_string(wgx) + ", 1, 1) @subgroup_size(" +
                std::to_string(subgroupSize) + ")\n" +
                "fn main(@builtin(subgroup_size) builtin_size : u32,\n" +
                "        @builtin(local_invocation_index) lid : u32,\n" +
                "        @builtin(workgroup_id) wgid : vec3u) {\n" +
                "  let gid = lid + wgid.x * " + std::to_string(wgx) + "u;\n" +
                "  // Store 1 if builtin subgroup_size matches the @subgroup_size attribute, 0 "
                "otherwise.\n" +
                "  output[gid] = select(0u, 1u, builtin_size == " + std::to_string(subgroupSize) +
                "u);\n" +
                "}";

            // Try to create the pipeline; skip this subgroup size if it
            // fails validation.
            wgpuDevicePushErrorScope(t.device(), WGPUErrorFilter_Validation);
            WGPUComputePipeline pipeline = createComputePipelineAuto(t, wgsl);
            if (popValidationScopeHadError(t)) {
                continue;
            }

            atLeastOneSucceeded = true;

            const uint32_t numInvocations = wgx * numWorkGroups;
            WGPUBuffer outputBuffer = createZeroedU32Buffer(t, numInvocations);

            WGPUBindGroup bindGroup = makeAutoBindGroup(
                t, pipeline,
                {{outputBuffer, static_cast<uint64_t>(numInvocations) * sizeof(uint32_t)}});
            runComputePassX(t, pipeline, bindGroup, numWorkGroups);

            const std::vector<uint32_t> outputData = readBackU32(t, outputBuffer, numInvocations);

            for (uint32_t i = 0; i < numInvocations; i++) {
                if (outputData[i] != 1u) {
                    // Upstream records the failure and continues with the
                    // next subgroup size; this fail() throws (header note 7).
                    t.fail("@subgroup_size(" + std::to_string(subgroupSize) + "): invocation " +
                           std::to_string(i) + " has builtin subgroup_size != " +
                           std::to_string(subgroupSize));
                }
            }
        }

        t.expect(atLeastOneSucceeded,
                 "No valid @subgroup_size value found in [subgroupMinSize, subgroupMaxSize]");
#endif  // defined(CTS_BACKEND_DAWN)
    });

} // namespace
