// Ported from gpuweb/cts src/webgpu/shader/execution/limits.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 webgpu-native-cts contributors, BSD-3-Clause.

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

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,limits",
    "Execution tests for WGSL limits.");

// The limits that we test (matching upstream constants).
static constexpr int kMaxStructMembers         = 1023;
static constexpr int kMaxCompositeNestingDepth = 15;
static constexpr int kMaxBraceNestingDepth     = 127;
static constexpr int kMaxFunctionParameters    = 255;
static constexpr int kMaxSwitchCaseSelectors   = 1023;
static constexpr int kMaxPrivateStorageSize    = 8192;
static constexpr int kMaxFunctionStorageSize   = 8192;
static constexpr int kMaxConstArrayElements    = 2047;

// ---------------------------------------------------------------------------
// Helper: WGPUStringView from std::string_view
// ---------------------------------------------------------------------------
static WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ---------------------------------------------------------------------------
// runShaderTest
//
// Port of the upstream runShaderTest() helper.
//
// Runs a compute shader that reads/writes a single storage buffer. The buffer
// is pre-filled with the values in `input` (u32 array, length = elementCount).
// After the dispatch the buffer is checked element-by-element against the
// values produced by `expected(i)`.
//
// @param t            The test fixture.
// @param wgsl         WGSL source for the compute shader.
// @param input        Initial buffer contents (uint32_t array).
// @param elementCount Number of u32 elements in the buffer.
// @param expected     Callable (size_t index) -> uint32_t giving the expected
//                     value at each element index after the shader runs.
// @param constants    Optional pipeline-overridable constants (key/value pairs).
// ---------------------------------------------------------------------------
static void runShaderTest(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& wgsl,
    const std::vector<uint32_t>& input,
    const std::function<uint32_t(size_t)>& expected,
    const std::vector<std::pair<std::string, double>>& constants = {})
{
    const size_t elementCount = input.size();
    const uint64_t bufferBytes = static_cast<uint64_t>(elementCount) * sizeof(uint32_t);

    // Upload input data to the storage buffer (pre-filled with input values, not expected values).
    WGPUBuffer outputBuffer = t.makeBufferWithContents(
        input.data(), bufferBytes,
        WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

    // Build the compute pipeline (layout = auto).
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);

    // Build optional constant entries.
    std::vector<WGPUConstantEntry> constEntries;
    constEntries.reserve(constants.size());
    for (const auto& kv : constants) {
        WGPUConstantEntry entry = WGPU_CONSTANT_ENTRY_INIT;
        entry.key   = sv(kv.first);
        entry.value = kv.second;
        constEntries.push_back(entry);
    }

    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout             = nullptr; // auto layout
    pipeDesc.compute.module     = shaderModule;
    pipeDesc.compute.entryPoint = sv("main");
    if (!constEntries.empty()) {
        pipeDesc.compute.constantCount = constEntries.size();
        pipeDesc.compute.constants     = constEntries.data();
    }
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    // Create the bind group.
    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

    WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
    bgEntry.binding = 0;
    bgEntry.buffer  = outputBuffer;
    bgEntry.offset  = 0;
    bgEntry.size    = bufferBytes;

    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout     = bgl;
    bgDesc.entryCount = 1;
    bgDesc.entries    = &bgEntry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    // Dispatch 1 workgroup.
    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor cpDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &cpDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer cmdBuf = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &cmdBuf);

    // Check that the buffer output matches the expected values element-by-element.
    // The lambda captures `expected` and `elementCount` for the element check.
    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [expected, elementCount](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            const size_t needed = elementCount * sizeof(uint32_t);
            if (len < needed) {
                std::ostringstream msg;
                msg << "readback buffer too small: need " << needed << " bytes, got " << len;
                return msg.str();
            }
            for (size_t i = 0; i < elementCount; ++i) {
                uint32_t got = 0;
                std::memcpy(&got, actual + i * sizeof(uint32_t), sizeof(uint32_t));
                const uint32_t want = expected(i);
                if (got != want) {
                    std::ostringstream msg;
                    msg << "element[" << i << "]: expected 0x"
                        << std::hex << want << ", got 0x" << got;
                    return msg.str();
                }
            }
            return std::nullopt;
        },
        /*srcByteOffset=*/0,
        /*byteLength=*/static_cast<size_t>(bufferBytes));
}

// ---------------------------------------------------------------------------
// kArrayElementTypes: mirror of upstream kArrayElements.
// Carries enough information to generate WGSL type names and the
// to_u32 expression that collapses each element to a u32 for the output buffer.
// ---------------------------------------------------------------------------
struct ArrayElementType {
    const char* wgslType;   // WGSL type name used in var declarations
    int         byteSize;   // byte size of one element
    // Returns a WGSL expression that converts a value `x` of this type to u32.
    std::string toU32(const std::string& x) const {
        if (std::string_view(wgslType) == "bool") {
            return "u32(" + x + ")";
        }
        if (std::string_view(wgslType) == "u32") {
            return x;
        }
        // vec4u: dot(x, x) — mirrors upstream `dot(${x}, ${x})`
        return "dot(" + x + ", " + x + ")";
    }
};

// Mirrors upstream `keysOf(kArrayElements)` iteration order: bool, u32, vec4u.
static constexpr ArrayElementType kArrayElementTypes[] = {
    { "bool",  4  },
    { "u32",   4  },
    { "vec4u", 16 },
};

// Returns the ArrayElementType matching the given WGSL type name string.
static const ArrayElementType& lookupArrayElementType(const std::string& typeName) {
    for (const auto& t : kArrayElementTypes) {
        if (typeName == t.wgslType) {
            return t;
        }
    }
    std::abort(); // unreachable; param validation guarantees a valid name
}

// ---------------------------------------------------------------------------
// Test: struct_members
// Checks that structures with the maximum number of members (1023) compile.
// ---------------------------------------------------------------------------
CTS_TEST(g, "struct_members")
    .desc("Test that structures with the maximum number of members are supported.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string code = "struct S {";
        for (int m = 0; m < kMaxStructMembers; ++m) {
            code += "  m";
            code += std::to_string(m);
            code += ": u32,\n";
        }
        code += R"(}

    @group(0) @binding(0) var<storage, read_write> buffer : S;

    @compute @workgroup_size(1)
    fn main() {
      buffer = S();
    }
    )";

        // Input: all sentinel (0xdeadbeef), expected: all zero.
        std::vector<uint32_t> input(static_cast<size_t>(kMaxStructMembers), 0xdeadbeef);
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 0u; });
    });

// ---------------------------------------------------------------------------
// Test: nesting_depth_composite_struct
// Checks that composite struct types can be nested up to the maximum depth (15).
// ---------------------------------------------------------------------------
CTS_TEST(g, "nesting_depth_composite_struct")
    .desc("Test that composite types can be nested up to the maximum level.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string code = "struct S0 { a : u32 }\n";
        for (int s = 1; s < kMaxCompositeNestingDepth; ++s) {
            code += "struct S";
            code += std::to_string(s);
            code += " { a : S";
            code += std::to_string(s - 1);
            code += " }\n";
        }
        code += "\n    @group(0) @binding(0) var<storage, read_write> buffer : S";
        code += std::to_string(kMaxCompositeNestingDepth - 1);
        code += ";\n\n    @compute @workgroup_size(1)\n    fn main() {\n      buffer = S";
        code += std::to_string(kMaxCompositeNestingDepth - 1);
        code += "();\n    }\n    ";

        std::vector<uint32_t> input = {0xdeadbeef};
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 0u; });
    });

// ---------------------------------------------------------------------------
// Test: nesting_depth_composite_array
// Checks that array types can be nested up to the maximum depth (15).
// ---------------------------------------------------------------------------
CTS_TEST(g, "nesting_depth_composite_array")
    .desc("Test that composite types can be nested up to the maximum level.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Build `array<array<...<array<u32, 1>...>, 1>, 1>` with kMaxCompositeNestingDepth levels.
        std::string type;
        for (int m = 0; m < kMaxCompositeNestingDepth; ++m) {
            type += "array<";
        }
        type += "u32";
        for (int m = 0; m < kMaxCompositeNestingDepth; ++m) {
            type += ", 1>";
        }

        std::string code = "\n    @group(0) @binding(0) var<storage, read_write> buffer : ";
        code += type;
        code += ";\n\n    @compute @workgroup_size(1)\n    fn main() {\n      buffer = ";
        code += type;
        code += "();\n    }\n    ";

        std::vector<uint32_t> input = {0xdeadbeef};
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 0u; });
    });

// ---------------------------------------------------------------------------
// Test: nesting_depth_braces
// Checks that brace-enclosed statements can be nested up to the maximum depth (127).
// ---------------------------------------------------------------------------
CTS_TEST(g, "nesting_depth_braces")
    .desc("Test that brace-enclosed statements can be nested up to the maximum level.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string code = "@group(0) @binding(0) var<storage, read_write> buffer : array<u32, ";
        code += std::to_string(kMaxBraceNestingDepth);
        code += ">;\n    @compute @workgroup_size(1)\n\n    fn main() {\n    ";

        // Subtract one for the function body and another for the nested statement itself.
        for (int b = 0; b < kMaxBraceNestingDepth - 2; ++b) {
            code += "  {\n";
        }
        code += "    buffer[0] = 42;\n";
        for (int b = 0; b < kMaxBraceNestingDepth - 2; ++b) {
            code += "  }\n";
        }
        code += "\n    }\n    ";

        // Input: i-th element = i; expected: element[0] = 42, rest unchanged.
        std::vector<uint32_t> input(static_cast<size_t>(kMaxBraceNestingDepth));
        for (int i = 0; i < kMaxBraceNestingDepth; ++i) {
            input[static_cast<size_t>(i)] = static_cast<uint32_t>(i);
        }
        runShaderTest(t, code, input, [](size_t i) -> uint32_t {
            return (i == 0) ? 42u : static_cast<uint32_t>(i);
        });
    });

// ---------------------------------------------------------------------------
// Test: function_parameters
// Checks that functions can have the maximum number of parameters (255).
// ---------------------------------------------------------------------------
CTS_TEST(g, "function_parameters")
    .desc("Test that functions can have the maximum number of parameters.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string code = "@group(0) @binding(0) var<storage, read_write> buffer : array<u32, ";
        code += std::to_string(kMaxFunctionParameters);
        code += ">;\n\n    fn bar(";
        for (int p = 0; p < kMaxFunctionParameters; ++p) {
            code += "p";
            code += std::to_string(p);
            code += ": u32, ";
        }
        code += ") {";

        for (int p = 0; p < kMaxFunctionParameters; ++p) {
            code += "buffer[";
            code += std::to_string(p);
            code += "] = p";
            code += std::to_string(p);
            code += ";\n";
        }

        code += "}\n\n    @compute @workgroup_size(1)\n    fn main() {\n      bar(";
        for (int p = 0; p < kMaxFunctionParameters; ++p) {
            code += std::to_string(p);
            code += ", ";
        }
        code += ");\n    }\n    ";

        std::vector<uint32_t> input(static_cast<size_t>(kMaxFunctionParameters), 0xdeadbeef);
        runShaderTest(t, code, input, [](size_t i) -> uint32_t {
            return static_cast<uint32_t>(i);
        });
    });

// ---------------------------------------------------------------------------
// Test: switch_case_selectors
// Checks that switch statements can have the maximum number of case selectors
// in separate clauses (1023 selectors = 1022 separate case clauses + default).
// ---------------------------------------------------------------------------
CTS_TEST(g, "switch_case_selectors")
    .desc("Test that switch statements can have the maximum number of case selectors in separate clauses.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string code = "@group(0) @binding(0) var<storage, read_write> buffer : array<u32, 2>;\n\n"
                           "    @compute @workgroup_size(1)\n    fn main() {\n      switch (buffer[0]) {\n"
                           "        default {}";
        for (int s = 0; s < kMaxSwitchCaseSelectors - 1; ++s) {
            code += "\n        case ";
            code += std::to_string(s);
            code += " { buffer[1] = ";
            code += std::to_string(s);
            code += "; }";
        }
        code += "\n      };\n    }\n    ";

        // Input: buffer[0]=42, buffer[1]=0xdeadbeef.
        // Shader: switch(42) → case 42 → buffer[1]=42; so both elements become 42.
        std::vector<uint32_t> input = {42u, 0xdeadbeefu};
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 42u; });
    });

// ---------------------------------------------------------------------------
// Test: switch_case_selectors_same_clause
// Checks that switch statements can have the maximum number of case selectors
// in the same clause (1023 selectors all in one case clause).
// ---------------------------------------------------------------------------
CTS_TEST(g, "switch_case_selectors_same_clause")
    .desc("Test that switch statements can have the maximum number of case selectors in the same clause.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        std::string code = "@group(0) @binding(0) var<storage, read_write> buffer : array<u32, 2>;\n\n"
                           "    @compute @workgroup_size(1)\n    fn main() {\n      switch (buffer[0]) {\n"
                           "        default {}\n        case ";
        for (int s = 0; s < kMaxSwitchCaseSelectors - 1; ++s) {
            code += std::to_string(s);
            code += ", ";
        }
        code += " { buffer[1] = 42; }\n      };\n    }\n    ";

        // Input: buffer[0]=999 (not in any of the 0..1021 cases → default, buffer[1] unchanged).
        // Expected: buffer[0]=999, buffer[1]=42 since 999 > 1021 → hits default, but actually
        // since 999 < kMaxSwitchCaseSelectors-1=1022 it IS in the case list: buffer[1]=42.
        // Upstream: input[0]=999, expected _i => (i===0 ? 999 : 42).
        std::vector<uint32_t> input = {999u, 0xdeadbeefu};
        runShaderTest(t, code, input, [](size_t i) -> uint32_t {
            return (i == 0) ? 999u : 42u;
        });
    });

// ---------------------------------------------------------------------------
// Test: private_array_byte_size
// Checks that arrays in private address space up to the maximum size (8192 bytes)
// are supported.
// ---------------------------------------------------------------------------
CTS_TEST(g, "private_array_byte_size")
    .desc("Test that arrays in the private address space up to the maximum size are supported.")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"bool", "u32", "vec4u"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const ArrayElementType& elemType = lookupArrayElementType(typeName);
        const int elements = kMaxPrivateStorageSize / elemType.byteSize;

        std::string code = "\n    @group(0) @binding(0) var<storage, read_write> buffer : array<u32, ";
        code += std::to_string(elements);
        code += ">;\n\n    var<private> arr : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n\n    @compute @workgroup_size(1)\n    fn main() {\n"
                "      for (var i = 0; i < ";
        code += std::to_string(elements);
        code += "; i++) {\n        buffer[i] = ";
        code += elemType.toU32("arr[i]");
        code += ";\n      }\n    }\n    ";

        std::vector<uint32_t> input(static_cast<size_t>(elements), 0xdeadbeefu);
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 0u; });
    });

// ---------------------------------------------------------------------------
// Test: private_array_combined_byte_size
// Checks the combined sizes of variables in the private address space.
// ---------------------------------------------------------------------------
CTS_TEST(g, "private_array_combined_byte_size")
    .desc("Test the combined sizes of variables in the private address space.")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"bool", "u32", "vec4u"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const ArrayElementType& elemType = lookupArrayElementType(typeName);
        const int elements = kMaxPrivateStorageSize / elemType.byteSize / 4;

        std::string code = "\n    @group(0) @binding(0) var<storage, read_write> buffer : array<u32, ";
        code += std::to_string(elements);
        code += ">;\n\n    var<private> arr1 : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n    var<private> arr2 : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n    var<private> arr3 : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n    var<private> arr4 : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n\n    @compute @workgroup_size(1)\n    fn main() {\n"
                "      for (var i = 0; i < ";
        code += std::to_string(elements);
        code += "; i++) {\n        buffer[i] = ";
        code += elemType.toU32("arr1[i]");
        code += " + ";
        code += elemType.toU32("arr2[i]");
        code += " +\n                    ";
        code += elemType.toU32("arr3[i]");
        code += " + ";
        code += elemType.toU32("arr4[i]");
        code += ";\n      }\n    }\n    ";

        std::vector<uint32_t> input(static_cast<size_t>(elements), 0xdeadbeefu);
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 0u; });
    });

// ---------------------------------------------------------------------------
// Test: function_array_byte_size
// Checks that arrays in the function address space up to the maximum size
// (8192 bytes) are supported.
// ---------------------------------------------------------------------------
CTS_TEST(g, "function_array_byte_size")
    .desc("Test that arrays in the function address space up to the maximum size are supported.")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"bool", "u32", "vec4u"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const ArrayElementType& elemType = lookupArrayElementType(typeName);
        const int elements = kMaxFunctionStorageSize / elemType.byteSize;

        std::string code = "\n    @group(0) @binding(0) var<storage, read_write> buffer : array<u32, ";
        code += std::to_string(elements);
        code += ">;\n\n    @compute @workgroup_size(1)\n    fn main() {\n      var arr : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n      for (var i = 0; i < ";
        code += std::to_string(elements);
        code += "; i++) {\n        buffer[i] = ";
        code += elemType.toU32("arr[i]");
        code += ";\n      }\n    }\n    ";

        std::vector<uint32_t> input(static_cast<size_t>(elements), 0xdeadbeefu);
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 0u; });
    });

// ---------------------------------------------------------------------------
// Test: function_variable_combined_byte_size
// Checks the combined sizes of variables in the function address space.
// ---------------------------------------------------------------------------
CTS_TEST(g, "function_variable_combined_byte_size")
    .desc("Test the combined sizes of variables in the function address space.")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"bool", "u32", "vec4u"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string typeName = t.param<std::string>("type");
        const ArrayElementType& elemType = lookupArrayElementType(typeName);
        const int elements = kMaxFunctionStorageSize / elemType.byteSize / 4;

        std::string code = "\n    @group(0) @binding(0) var<storage, read_write> buffer : array<u32, ";
        code += std::to_string(elements);
        code += ">;\n\n    @compute @workgroup_size(1)\n    fn main() {\n      var arr1 : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n      var arr2 : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n      var arr3 : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n      var arr4 : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n      for (var i = 0; i < ";
        code += std::to_string(elements);
        code += "; i++) {\n        buffer[i] = ";
        code += elemType.toU32("arr1[i]");
        code += " + ";
        code += elemType.toU32("arr2[i]");
        code += " +\n                    ";
        code += elemType.toU32("arr3[i]");
        code += " + ";
        code += elemType.toU32("arr4[i]");
        code += ";\n      }\n    }\n    ";

        std::vector<uint32_t> input(static_cast<size_t>(elements), 0xdeadbeefu);
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 0u; });
    });

// ---------------------------------------------------------------------------
// Test: workgroup_array_byte_size
// Checks that arrays in the workgroup address space up to the maximum size
// (device limit maxComputeWorkgroupStorageSize) are supported.
// Note: maxComputeWorkgroupStorageSize is read at runtime from device limits
// (upstream reads it from t.device.limits at test body time).
// ---------------------------------------------------------------------------
CTS_TEST(g, "workgroup_array_byte_size")
    .desc("Test that arrays in the workgroup address space up to the maximum size are supported.")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"bool", "u32", "vec4u"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t maxSize = limits.maxComputeWorkgroupStorageSize;

        const std::string typeName = t.param<std::string>("type");
        const ArrayElementType& elemType = lookupArrayElementType(typeName);
        const int elements = static_cast<int>(maxSize) / elemType.byteSize;

        std::string code = "\n    @group(0) @binding(0) var<storage, read_write> buffer : array<u32, ";
        code += std::to_string(elements);
        code += ">;\n\n    var<workgroup> arr : array<";
        code += typeName;
        code += ", ";
        code += std::to_string(elements);
        code += ">;\n\n    @compute @workgroup_size(1)\n    fn main() {\n"
                "      for (var i = 0; i < ";
        code += std::to_string(elements);
        code += "; i++) {\n        buffer[i] = ";
        code += elemType.toU32("arr[i]");
        code += ";\n      }\n    }\n    ";

        std::vector<uint32_t> input(static_cast<size_t>(elements), 0xdeadbeefu);
        runShaderTest(t, code, input, [](size_t /*i*/) -> uint32_t { return 0u; });
    });

// ---------------------------------------------------------------------------
// Test: workgroup_array_byte_size_override
// Checks that arrays in the workgroup address space up to the maximum size
// work when the array size is given as a pipeline-overridable constant.
// ---------------------------------------------------------------------------
CTS_TEST(g, "workgroup_array_byte_size_override")
    .desc("Test that arrays in the workgroup address space up to the maximum size are supported.")
    .params([](ParamsBuilder u) {
        return u.combine("type", {"bool", "u32", "vec4u"});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const uint32_t maxSize = limits.maxComputeWorkgroupStorageSize;

        const std::string typeName = t.param<std::string>("type");
        const ArrayElementType& elemType = lookupArrayElementType(typeName);
        const int elements = static_cast<int>(maxSize) / elemType.byteSize;

        // Set the default element count far too large (elements * 1000), then override with
        // the valid value via pipeline constants.
        std::string code = "\n    @group(0) @binding(0) var<storage, read_write> buffer : array<u32, ";
        code += std::to_string(elements);
        code += ">;\n\n    // Set the default element count far too large, which we later override with a valid value.\n    override elements = ";
        code += std::to_string(elements);
        code += " * 1000;\n    var<workgroup> arr : array<";
        code += typeName;
        code += ", elements>;\n\n    @compute @workgroup_size(1)\n    fn main() {\n"
                "      for (var i = 0; i < ";
        code += std::to_string(elements);
        code += "; i++) {\n        buffer[i] = ";
        code += elemType.toU32("arr[i]");
        code += ";\n      }\n    }\n    ";

        std::vector<uint32_t> input(static_cast<size_t>(elements), 0xdeadbeefu);
        // Override "elements" with the valid value.
        runShaderTest(t, code, input,
            [](size_t /*i*/) -> uint32_t { return 0u; },
            {{"elements", static_cast<double>(elements)}});
    });

// ---------------------------------------------------------------------------
// Test: const_array_elements
// Checks that constant array expressions with the maximum number of elements
// (up to 2047) are supported. Tests sizeDivisors 64, 8, 1 (largest last).
//
// Note: upstream comment says some backend shader compilers may time out at
// the maximum size (2047 elements), which is allowed as an 'uncategorized
// error'. We keep all three cases; if a backend times out or produces an
// error at sizeDivisor=1 it may manifest as a test failure — this is expected
// per the upstream spec note.
// ---------------------------------------------------------------------------
CTS_TEST(g, "const_array_elements")
    .desc("Test that constant array expressions with the maximum number of elements are supported.")
    .params([](ParamsBuilder u) {
        // Must include 1 to test the largest size.
        return u.combine("sizeDivisor", {64, 8, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int sizeDivisor = t.param<int>("sizeDivisor");
        // Ceiling division: matches upstream Math.ceil(kMaxConstArrayElements / t.params.sizeDivisor).
        const int elementCount = (kMaxConstArrayElements + sizeDivisor - 1) / sizeDivisor;

        const std::string type = "array<u32, " + std::to_string(elementCount) + ">";

        // Build the const-array expression: array<u32, N>(0, 1, 2, ..., N-1).
        std::string expr = type + "(";
        for (int i = 0; i < elementCount; ++i) {
            expr += std::to_string(i);
            expr += ", ";
        }
        expr += ")";

        std::string code = "\n    @group(0) @binding(0) var<storage, read_write> buffer : ";
        code += type;
        code += ";\n\n    @compute @workgroup_size(1)\n    fn main() {\n      buffer = ";
        code += expr;
        code += ";\n    }\n    ";

        std::vector<uint32_t> input(static_cast<size_t>(elementCount), 0xdeadbeefu);
        runShaderTest(t, code, input, [](size_t i) -> uint32_t {
            return static_cast<uint32_t>(i);
        });
    });

} // namespace
