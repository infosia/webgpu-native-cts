// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/workgroupUniformLoad.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Tests that workgroupUniformLoad returns the value previously stored to the
// workgroup variable, for a variety of types. The control barrier itself is
// covered by the memory_model tests; this exercises the load + broadcast.
//
// Port note: upstream encodes the per-type expected words as Uint32Array /
// Float32Array / Int32Array, all compared as raw 4-byte words. We store the
// expected words as their 32-bit bit-patterns. The output buffer is filled
// with the 0xdeadbeef sentinel (matching upstream), so padding words in
// ComplexStruct keep the sentinel and a silent no-write of any data word
// surfaces as a mismatch against its expected non-sentinel value.

#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

uint32_t f32bits(float value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

uint32_t i32bits(int32_t value) {
    uint32_t out = 0;
    std::memcpy(&out, &value, sizeof(out));
    return out;
}

struct TypeConfig {
    std::string type;       // The WGSL type used for the workgroup variable.
    std::string store_val;  // Value assigned to the workgroup variable (empty if store_decl is set).
    std::string store_decl; // Full statement initializing the variable (overrides store_val).
    std::string host_type;  // Host-visible buffer element type, if different from `type`.
    std::string to_host;    // Conversion wrapping the load, if the types differ (use $ for the load expr).
    std::string decls;      // Additional module-scope declarations.
    std::vector<uint32_t> expected; // Expected words (bit-patterns) per invocation.
};

const std::vector<std::pair<std::string, TypeConfig>>& kTypes() {
    static const std::vector<std::pair<std::string, TypeConfig>> types = {
        {"bool",
         {"bool", "true", "", "u32", "u32($)", "", {1}}},
        {"u32",
         {"u32", "42", "", "", "", "", {42}}},
        {"vec4u",
         {"vec4u", "vec4u(42, 1, 0xffffffff, 777)", "", "", "", "", {42, 1, 0xffffffffu, 777}}},
        {"mat3x2f",
         {"mat3x2f", "mat3x2(42, 1, 65536, -42, -1, -65536)", "", "", "", "",
          {f32bits(42.f), f32bits(1.f), f32bits(65536.f), f32bits(-42.f), f32bits(-1.f),
           f32bits(-65536.f)}}},
        {"array<u32, 4>",
         {"array<u32, 4>", "array(42, 1, 0xffffffff, 777)", "", "", "", "",
          {42, 1, 0xffffffffu, 777}}},
        {"SimpleStruct",
         {"SimpleStruct", "SimpleStruct(42, 1, 0xffffffff, 777)", "", "", "",
          "struct SimpleStruct { a: u32, b: u32, c: u32, d: u32, }",
          {42, 1, 0xffffffffu, 777}}},
        {"ComplexStruct",
         {"ComplexStruct", "rhs", "", "", "",
          "struct Inner { v: vec4u, }\n"
          "            struct ComplexStruct {\n"
          "              a: array<Inner, 4>,\n"
          "              @size(28) b: vec4u,\n"
          "              c: u32\n"
          "            }\n"
          "            const v = vec4(42, 1, 0xffffffff, 777);\n"
          "            const rhs = ComplexStruct(\n"
          "              array(Inner(v.xyzw), Inner(v.yzwx), Inner(v.zwxy), Inner(v.wxyz)),\n"
          "              v.xzxz,\n"
          "              0x12345678,\n"
          "              );",
          {// v.xyzw
           42, 1, 0xffffffffu, 777,
           // v.yzwx
           1, 0xffffffffu, 777, 42,
           // v.zwxy
           0xffffffffu, 777, 42, 1,
           // v.wxyz
           777, 42, 1, 0xffffffffu,
           // v.xzxz
           42, 0xffffffffu, 42, 0xffffffffu,
           // 12 bytes of padding
           0xdeadbeefu, 0xdeadbeefu, 0xdeadbeefu, 0x12345678u}}},
        {"atomic<u32>",
         {"atomic<u32>", "", "atomicStore(&(wgvar), 42u);", "u32", "", "", {42}}},
        {"atomic<i32>",
         {"atomic<i32>", "", "atomicStore(&(wgvar), -42i);", "i32", "", "",
          {i32bits(-42)}}},
        {"AtomicInStruct",
         {"AtomicInStruct", "", "atomicStore(&(wgvar.a), 42u);", "u32",
          "workgroupUniformLoad(&(wgvar.a))",
          "struct AtomicInStruct {\n"
          "      x : i32,\n"
          "      a : atomic<u32>,\n"
          "      y : u32,\n"
          "    };",
          {42}}},
    };
    return types;
}

std::vector<Value> typeKeys() {
    std::vector<Value> keys;
    for (const auto& entry : kTypes()) {
        keys.emplace_back(entry.first);
    }
    return keys;
}

const TypeConfig& typeConfig(const std::string& key) {
    for (const auto& entry : kTypes()) {
        if (entry.first == key) {
            return entry.second;
        }
    }
    std::abort();
}

// wgsize is encoded as a JSON-array string "[x,y]" to mirror upstream's
// array-valued combine param; the listing query renders it as that string.
struct WgSize {
    int x;
    int y;
};

const std::vector<WgSize>& kWgSizes() {
    static const std::vector<WgSize> sizes = {{1, 1}, {3, 7}, {1, 128}, {16, 16}};
    return sizes;
}

std::string wgSizeString(const WgSize& s) {
    return "[" + std::to_string(s.x) + "," + std::to_string(s.y) + "]";
}

std::vector<Value> wgSizeValues() {
    std::vector<Value> values;
    for (const WgSize& s : kWgSizes()) {
        values.emplace_back(wgSizeString(s));
    }
    return values;
}

WgSize wgSizeFromString(const std::string& value) {
    for (const WgSize& s : kWgSizes()) {
        if (wgSizeString(s) == value) {
            return s;
        }
    }
    std::abort();
}

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,builtin,workgroupUniformLoad",
    "Executes a control barrier synchronization function that affects memory and atomic operations "
    "in the workgroup address space.");

CTS_TEST(testGroup, "types")
    .desc(
        "Test that the result of a workgroupUniformLoad is the value previously stored to the "
        "workgroup variable, for a variety of types.")
    .params([](ParamsBuilder u) {
        return u.combine("type", typeKeys()).combine("wgsize", wgSizeValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string typeKey = t.param<std::string>("type");
        const TypeConfig& cfg = typeConfig(typeKey);
        const WgSize wgsize = wgSizeFromString(t.param<std::string>("wgsize"));
        const uint32_t numInvocations = static_cast<uint32_t>(wgsize.x) * static_cast<uint32_t>(wgsize.y);
        const uint32_t numWordsPerInvocation = static_cast<uint32_t>(cfg.expected.size());
        const uint32_t totalHostWords = numInvocations * numWordsPerInvocation;

        if (numInvocations > t.getLimits().maxComputeInvocationsPerWorkgroup) {
            t.skip("num_invocations (" + std::to_string(numInvocations) +
                   ") > maxComputeInvocationsPerWorkgroup (" +
                   std::to_string(t.getLimits().maxComputeInvocationsPerWorkgroup) + ")");
        }

        std::string load = "workgroupUniformLoad(&wgvar)";
        if (!cfg.to_host.empty()) {
            std::string converted = cfg.to_host;
            const size_t pos = converted.find('$');
            if (pos != std::string::npos) {
                converted.replace(pos, 1, load);
            }
            load = converted;
        }

        const std::string hostType = cfg.host_type.empty() ? cfg.type : cfg.host_type;
        const std::string storeStmt =
            cfg.store_decl.empty() ? ("wgvar = " + cfg.store_val + ";") : cfg.store_decl;

        const std::string code =
            (cfg.decls.empty() ? "" : (cfg.decls + "\n")) +
            "\n"
            "    @group(0) @binding(0) var<storage, read_write> buffer : array<" + hostType + ", " +
            std::to_string(numInvocations) +
            ">;\n"
            "\n"
            "    var<workgroup> wgvar : " + cfg.type +
            ";\n"
            "\n"
            "    @compute @workgroup_size(" + std::to_string(wgsize.x) + ", " +
            std::to_string(wgsize.y) +
            ")\n"
            "    fn main(@builtin(local_invocation_index) lid: u32) {\n"
            "      if (lid == " + std::to_string(numInvocations - 1) +
            ") {\n"
            "        " + storeStmt +
            "\n"
            "      }\n"
            "      buffer[lid] = " + load +
            ";\n"
            "    }\n";

        WGPUShaderModule shaderModule = t.createShaderModuleTracked(code);
        WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipeDesc.layout = nullptr;
        pipeDesc.compute.module = shaderModule;
        pipeDesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

        // Fill the output buffer with the 0xdeadbeef sentinel (matches upstream).
        std::vector<uint32_t> init(static_cast<size_t>(totalHostWords), 0xdeadbeefu);
        WGPUBuffer outputBuffer = t.makeBufferWithContents(
            init.data(),
            init.size() * sizeof(uint32_t),
            WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = outputBuffer;
        entry.offset = 0;
        entry.size = init.size() * sizeof(uint32_t);
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
        wgpuBindGroupLayoutRelease(bgl);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        wgpuComputePassEncoderSetPipeline(pass, pipeline);
        wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
        wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
        wgpuComputePassEncoderEnd(pass);
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        std::vector<uint32_t> expected(static_cast<size_t>(totalHostWords), 0);
        for (uint32_t i = 0; i < totalHostWords; ++i) {
            expected[i] = cfg.expected[i % numWordsPerInvocation];
        }
        t.expectGPUBufferValuesEqual(outputBuffer, expected.data(), expected.size() * sizeof(uint32_t));
    });

} // namespace
