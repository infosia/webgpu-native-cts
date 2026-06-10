// Ported from gpuweb/cts src/webgpu/shader/execution/zero_init.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes (intentional deviations, all documented for the reviewer):
// - Upstream's `workgroupSize` case param is a JS number array (query text
//   `workgroupSize=[1,1,1]`). The harness Value type has no array variant, so
//   the param is encoded as the JSON-style string "[1,1,1]" (query text
//   `workgroupSize="[1,1,1]"`). Values and ordering mirror upstream exactly.
// - Upstream private params (leading underscore) are excluded from upstream
//   query strings; this harness includes every param in the query. Following
//   existing-port convention, `_containerDepth` appears in the subcase query.
//   The upstream-private `_type` param is not carried as a param at all: the
//   type is deterministically regenerated in the test body from
//   (addressSpace, _containerDepth) and matched against `shaderTypeParam`.
// - Upstream `.batch(15)` has no harness analog; subcases run individually.
// - Upstream's declaredStructTypes map is keyed by JS object identity; this
//   port keys it by the structural pretty-print, so structurally identical
//   structs share one WGSL declaration instead of being re-declared under a
//   different name. The generated test variable and checks are unaffected.
// - `createComputePipelineAsync` maps to the synchronous pipeline creation.
// - f16 subcases are excluded by upstream itself ("Fewer subcases" continue),
//   so no shader-f16 feature skip is needed here.

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,zero_init",
    "Test that variables in the shader are zero initialized");

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// ============================================================
// ShaderTypeInfo: port of the upstream discriminated union.
//   kScalar           : scalarType, isAtomic
//   kVector / kMatrix : containerType ("vec2".."mat4x4"), scalarType
//   kArray            : children[0] = element type, length
//   kStruct           : children = members
// ============================================================
struct ShaderTypeInfo {
    enum Kind { kScalar, kVector, kMatrix, kArray, kStruct };

    Kind kind = kScalar;
    std::string scalarType;               // kScalar / kVector / kMatrix
    bool isAtomic = false;                // kScalar only
    std::string containerType;            // kVector / kMatrix
    int length = 0;                       // kArray
    std::vector<ShaderTypeInfo> children; // kArray (1 elem) / kStruct (members)
};

ShaderTypeInfo makeScalar(std::string scalarType, bool isAtomic) {
    ShaderTypeInfo t;
    t.kind = ShaderTypeInfo::kScalar;
    t.scalarType = std::move(scalarType);
    t.isAtomic = isAtomic;
    return t;
}

ShaderTypeInfo makeVectorOrMatrix(ShaderTypeInfo::Kind kind, std::string containerType, std::string scalarType) {
    ShaderTypeInfo t;
    t.kind = kind;
    t.containerType = std::move(containerType);
    t.scalarType = std::move(scalarType);
    return t;
}

ShaderTypeInfo makeArray(ShaderTypeInfo elementType, int length) {
    ShaderTypeInfo t;
    t.kind = ShaderTypeInfo::kArray;
    t.length = length;
    t.children.push_back(std::move(elementType));
    return t;
}

ShaderTypeInfo makeStruct(std::vector<ShaderTypeInfo> members) {
    ShaderTypeInfo t;
    t.kind = ShaderTypeInfo::kStruct;
    t.children = std::move(members);
    return t;
}

// prettyPrint: faithful port of the upstream serializer (defines the
// `shaderTypeParam` query identity, so the text must match exactly).
std::string prettyPrint(const ShaderTypeInfo& t) {
    switch (t.kind) {
        case ShaderTypeInfo::kArray:
            return "array<" + prettyPrint(t.children[0]) + ", " + std::to_string(t.length) + ">";
        case ShaderTypeInfo::kStruct: {
            std::string out = "struct { ";
            for (size_t i = 0; i < t.children.size(); ++i) {
                if (i != 0) {
                    out += ", ";
                }
                out += prettyPrint(t.children[i]);
            }
            out += " }";
            return out;
        }
        case ShaderTypeInfo::kVector:
        case ShaderTypeInfo::kMatrix:
            return t.containerType + "<" + t.scalarType + ">";
        case ShaderTypeInfo::kScalar:
            if (t.isAtomic) {
                return "atomic<" + t.scalarType + ">";
            }
            return t.scalarType;
    }
    return "";
}

// ============================================================
// Type generation: faithful port of the upstream generateTypes()
// generator (including the "Fewer subcases" filters and the
// struct round-robin/rotation dedup algorithm).
// ============================================================

// Depth-0 (scalar-level) types.
//   atomicsSupported mirrors upstream supportsAtomics(): true iff
//   addressSpace == "workgroup" (storage is not reachable in this file).
//   containerDepth is the subcase's _containerDepth (upstream filters the
//   innermost scalar level on p._containerDepth, not the recursion depth).
std::vector<ShaderTypeInfo> generateScalarLevelTypes(bool atomicsSupported, int containerDepth) {
    static const char* const kScalarTypes[] = {"i32", "u32", "f16", "f32", "bool"};
    static const char* const kVectorContainerTypes[] = {"vec2", "vec3", "vec4"};
    static const char* const kMatrixContainerTypes[] = {
        "mat2x2", "mat3x2", "mat4x2",
        "mat2x3", "mat3x3", "mat4x3",
        "mat2x4", "mat3x4", "mat4x4"};

    std::vector<ShaderTypeInfo> out;
    std::vector<bool> atomicChoices = atomicsSupported ? std::vector<bool>{true, false} : std::vector<bool>{false};
    for (bool isAtomic : atomicChoices) {
        for (const char* scalarTypeC : kScalarTypes) {
            const std::string scalarType = scalarTypeC;
            // supportedScalarTypes(): atomics only on i32/u32; bool/f16 pass the
            // host-sharable filter because these address spaces are not host-shared.
            const bool scalarSupportsAtomics = scalarType == "i32" || scalarType == "u32";
            if (isAtomic && !scalarSupportsAtomics) {
                continue;
            }
            // Fewer subcases: f16 is excluded by upstream.
            if (scalarType == "f16") {
                continue;
            }
            // Fewer subcases: for nested types, skip atomic u32 and non-atomic i32.
            if (containerDepth > 0) {
                if (scalarType == "u32" && isAtomic) {
                    continue;
                }
                if (scalarType == "i32" && !isAtomic) {
                    continue;
                }
            }

            out.push_back(makeScalar(scalarType, isAtomic));

            if (!isAtomic) {
                // Vector types.
                for (const char* vectorTypeC : kVectorContainerTypes) {
                    const std::string vectorType = vectorTypeC;
                    // Fewer subcases: for nested types, only include
                    // vec2<u32>, vec3<i32>, and vec4<f32>.
                    if (containerDepth > 0) {
                        const bool keep =
                            (vectorType == "vec2" && scalarType == "u32") ||
                            (vectorType == "vec3" && scalarType == "i32") ||
                            (vectorType == "vec4" && scalarType == "f32");
                        if (!keep) {
                            continue;
                        }
                    }
                    out.push_back(makeVectorOrMatrix(ShaderTypeInfo::kVector, vectorType, scalarType));
                }
                // Matrices can only be f32.
                if (scalarType == "f32") {
                    for (const char* matrixType : kMatrixContainerTypes) {
                        out.push_back(makeVectorOrMatrix(ShaderTypeInfo::kMatrix, matrixType, scalarType));
                    }
                }
            }
        }
    }
    return out;
}

// Container level (depth >= 1) built over the previous level's full list.
std::vector<ShaderTypeInfo> generateContainerLevelTypes(int depth, const std::vector<ShaderTypeInfo>& innerTypes) {
    std::vector<ShaderTypeInfo> out;

    // 'array' first, then 'struct' (upstream iteration order).
    {
        // kElementCounts[1] = {1, 3, 67}; kElementCounts[2] = {1, 3}.
        std::vector<int> elementCounts = depth == 1 ? std::vector<int>{1, 3, 67} : std::vector<int>{1, 3};
        for (int elementCount : elementCounts) {
            for (const ShaderTypeInfo& innerType : innerTypes) {
                out.push_back(makeArray(innerType, elementCount));
            }
        }
    }

    {
        static const int kMemberCounts[] = {1, 3};
        for (int memberCount : kMemberCounts) {
            std::vector<size_t> memberIndices(static_cast<size_t>(memberCount));
            for (size_t m = 0; m < memberIndices.size(); ++m) {
                memberIndices[m] = m;
            }

            // Round-robin through the types, concatenated forward and backward,
            // three times, deduplicating on the pretty-printed serialization.
            std::vector<const ShaderTypeInfo*> memberTypes;
            memberTypes.reserve(innerTypes.size() * 2);
            for (const ShaderTypeInfo& t : innerTypes) {
                memberTypes.push_back(&t);
            }
            for (size_t i = innerTypes.size(); i > 0; --i) {
                memberTypes.push_back(&innerTypes[i - 1]);
            }

            std::set<std::string> seenTypes;
            size_t typeIndex = 0;
            const size_t limit = memberTypes.size() * 3;
            while (typeIndex < limit) {
                const size_t prevTypeIndex = typeIndex;
                std::vector<ShaderTypeInfo> members(static_cast<size_t>(memberCount));
                for (size_t m : memberIndices) {
                    members[m] = *memberTypes[typeIndex % memberTypes.size()];
                    typeIndex += 1;
                }

                ShaderTypeInfo t = makeStruct(std::move(members));
                std::string serialized = prettyPrint(t);
                if (seenTypes.count(serialized) != 0) {
                    // Identical type produced: rotate the member indices, revert
                    // typeIndex to just past where this loop started, and retry.
                    std::rotate(memberIndices.begin(), memberIndices.begin() + 1, memberIndices.end());
                    typeIndex = prevTypeIndex + 1;
                    continue;
                }
                seenTypes.insert(std::move(serialized));
                out.push_back(std::move(t));
            }
        }
    }

    return out;
}

// Full list for a given (addressSpace-derived atomics support, _containerDepth).
std::vector<ShaderTypeInfo> generateTypeList(bool atomicsSupported, int containerDepth) {
    std::vector<ShaderTypeInfo> level = generateScalarLevelTypes(atomicsSupported, containerDepth);
    for (int depth = 1; depth <= containerDepth; ++depth) {
        level = generateContainerLevelTypes(depth, level);
    }
    return level;
}

// ============================================================
// WGSL builders (ensureType / checkZero), faithful to upstream.
// ============================================================

struct WgslTypeBuilder {
    std::string* moduleScope = nullptr;
    // Keyed by structural pretty-print (see deviation note in the header).
    std::map<std::string, std::string> declaredStructTypes;

    std::string ensureType(const std::string& typeName, const ShaderTypeInfo& type) {
        switch (type.kind) {
            case ShaderTypeInfo::kArray:
                return "array<" + ensureType(typeName + "_ArrayElement", type.children[0]) + ", " +
                       std::to_string(type.length) + ">";
            case ShaderTypeInfo::kStruct: {
                const std::string key = prettyPrint(type);
                auto it = declaredStructTypes.find(key);
                if (it != declaredStructTypes.end()) {
                    return it->second;
                }

                std::string members;
                for (size_t i = 0; i < type.children.size(); ++i) {
                    members += "\n    member" + std::to_string(i) + " : " +
                               ensureType(typeName + "_Member" + std::to_string(i), type.children[i]) + ",";
                }
                declaredStructTypes.emplace(key, typeName);
                *moduleScope += "\nstruct " + typeName + " {";
                *moduleScope += members;
                *moduleScope += "\n};";
                return typeName;
            }
            case ShaderTypeInfo::kVector:
            case ShaderTypeInfo::kMatrix:
                return type.containerType + "<" + type.scalarType + ">";
            case ShaderTypeInfo::kScalar:
                if (type.isAtomic) {
                    return "atomic<" + type.scalarType + ">";
                }
                return type.scalarType;
        }
        return "";
    }
};

std::string checkZero(const std::string& value, const ShaderTypeInfo& type, int depth) {
    const std::string d = std::to_string(depth);
    switch (type.kind) {
        case ShaderTypeInfo::kArray: {
            const std::string i = "i" + d;
            return "\nfor (var " + i + " = 0u; " + i + " < " + std::to_string(type.length) + "u + zero; " +
                   i + " = " + i + " + 1u) {\n" +
                   checkZero(value + "[" + i + "]", type.children[0], depth + 1) +
                   "\n}";
        }
        case ShaderTypeInfo::kStruct: {
            std::string out;
            for (size_t m = 0; m < type.children.size(); ++m) {
                if (m != 0) {
                    out += "\n";
                }
                out += checkZero(value + ".member" + std::to_string(m), type.children[m], depth + 1);
            }
            return out;
        }
        case ShaderTypeInfo::kVector: {
            // containerType is "vecN": length is the character at index 3.
            const std::string length(1, type.containerType[3]);
            const std::string i = "i" + d;
            return "\nfor (var " + i + " = 0u; " + i + " < " + length + "u + zero; " +
                   i + " = " + i + " + 1u) {\n" +
                   checkZero(value + "[" + i + "]", makeScalar(type.scalarType, false), depth + 1) +
                   "\n}";
        }
        case ShaderTypeInfo::kMatrix: {
            // containerType is "matCxR": cols at index 3, rows at index 5.
            const std::string cols(1, type.containerType[3]);
            const std::string rows(1, type.containerType[5]);
            const std::string c = "c" + d;
            const std::string r = "r" + d;
            return "\nfor (var " + c + " = 0u; " + c + " < " + cols + "u + zero; " +
                   c + " = " + c + " + 1u) {\n" +
                   "for (var " + r + " = 0u; " + r + " < " + rows + "u; " +
                   r + " = " + r + " + 1u) {\n" +
                   checkZero(value + "[" + c + "][" + r + "]", makeScalar(type.scalarType, false), depth + 1) +
                   "\n}\n}";
        }
        case ShaderTypeInfo::kScalar: {
            std::string expected;
            if (type.scalarType == "bool") {
                expected = "false";
            } else if (type.scalarType == "f32") {
                expected = "0.0";
            } else if (type.scalarType == "i32") {
                expected = "0";
            } else if (type.scalarType == "u32") {
                expected = "0u";
            }
            std::string loadValue = value;
            if (type.isAtomic) {
                loadValue = "atomicLoad(&" + value + ")";
            }
            // Note: this could have an early return, but upstream omits it because
            // it makes the tests fail with DXGI_ERROR_DEVICE_HUNG on Windows.
            return "\nif (" + loadValue + " != " + expected + ") { atomicStore(&output.failed, 1u); }";
        }
    }
    return "";
}

// ============================================================
// Param helpers
// ============================================================

// workgroupSize is encoded as the JSON-style string "[x,y,z]".
std::array<uint32_t, 3> parseWorkgroupSize(const std::string& text) {
    std::array<uint32_t, 3> dims = {1, 1, 1};
    size_t slot = 0;
    uint32_t current = 0;
    bool inNumber = false;
    for (char c : text) {
        if (c >= '0' && c <= '9') {
            current = current * 10u + static_cast<uint32_t>(c - '0');
            inNumber = true;
        } else if (inNumber) {
            if (slot < dims.size()) {
                dims[slot++] = current;
            }
            current = 0;
            inNumber = false;
        }
    }
    if (inNumber && slot < dims.size()) {
        dims[slot] = current;
    }
    return dims;
}

// ============================================================
// Test: compute,zero_init
// ============================================================

CTS_TEST(g, "compute,zero_init")
    .desc("Test that uninitialized variables in workgroup, private, and function storage classes "
          "are initialized to zero.")
    .params([](ParamsBuilder u) {
        return u
            // Only workgroup, function, and private variables can be declared without
            // data bound to them. The implementation's shader translator should
            // ensure these values are initialized.
            .combine("addressSpace", {"workgroup", "private", "function"})
            .expand("workgroupSize",
                    [](const ParamRecord& p) -> std::vector<Value> {
                        const std::string addressSpace = valueAs<std::string>(*findParam(p, "addressSpace"));
                        if (addressSpace == "workgroup") {
                            return {
                                "[1,1,1]",
                                "[1,32,1]",
                                "[64,1,1]",
                                "[1,1,48]",
                                "[1,47,1]",
                                "[33,1,1]",
                                "[1,1,63]",
                                "[8,8,2]",
                                "[7,7,3]",
                            };
                        }
                        return {"[1,1,1]"};
                    })
            .beginSubcases()
            // Fewer subcases: only 0 and 2. If double-nested containers work,
            // single-nested should too.
            .combine("_containerDepth", {0, 2})
            .expand("shaderTypeParam",
                    [](const ParamRecord& p) -> std::vector<Value> {
                        const std::string addressSpace = valueAs<std::string>(*findParam(p, "addressSpace"));
                        const int containerDepth = valueAs<int>(*findParam(p, "_containerDepth"));
                        const bool atomicsSupported = addressSpace == "workgroup";
                        std::vector<ShaderTypeInfo> types = generateTypeList(atomicsSupported, containerDepth);
                        std::vector<Value> out;
                        out.reserve(types.size());
                        for (const ShaderTypeInfo& t : types) {
                            out.emplace_back(prettyPrint(t));
                        }
                        return out;
                    });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string addressSpace = t.param<std::string>("addressSpace");
        const std::string workgroupSizeText = t.param<std::string>("workgroupSize");
        const int containerDepth = t.param<int>("_containerDepth");
        const std::string shaderTypeParam = t.param<std::string>("shaderTypeParam");

        const std::array<uint32_t, 3> workgroupSize = parseWorkgroupSize(workgroupSizeText);
        const WGPULimits limits = t.getLimits();

        const uint64_t numWorkgroupInvocations =
            static_cast<uint64_t>(workgroupSize[0]) * workgroupSize[1] * workgroupSize[2];
        if (numWorkgroupInvocations > limits.maxComputeInvocationsPerWorkgroup) {
            t.skip("workgroupSize: " + workgroupSizeText +
                   " > maxComputeInvocationsPerWorkgroup: " +
                   std::to_string(limits.maxComputeInvocationsPerWorkgroup));
        }

        // Regenerate the deterministic type list and find the subcase's type
        // (replaces the upstream-private `_type` param; see header note).
        const bool atomicsSupported = addressSpace == "workgroup";
        const std::vector<ShaderTypeInfo> types = generateTypeList(atomicsSupported, containerDepth);
        const ShaderTypeInfo* shaderType = nullptr;
        for (const ShaderTypeInfo& candidate : types) {
            if (prettyPrint(candidate) == shaderTypeParam) {
                shaderType = &candidate;
                break;
            }
        }
        if (shaderType == nullptr) {
            t.fail("internal error: shaderTypeParam not found in regenerated type list: " + shaderTypeParam);
        }

        std::string moduleScope = R"(
      struct Output {
        failed : atomic<u32>
      }
      @group(0) @binding(0) var<storage, read_write> output : Output;

      // This uniform value that's a zero is used to prevent the shader compilers from trying to
      // unroll the massive loops generated by these tests.
      @group(0) @binding(1) var<uniform> zero : u32;
    )";
        std::string functionScope;

        WgslTypeBuilder typeBuilder;
        typeBuilder.moduleScope = &moduleScope;
        const std::string typeDecl = typeBuilder.ensureType("TestType", *shaderType);

        if (addressSpace == "workgroup" || addressSpace == "private") {
            moduleScope += "\nvar<" + addressSpace + "> testVar: " + typeDecl + ";";
        } else {
            functionScope += "\nvar testVar: " + typeDecl + ";";
        }

        const std::string checkZeroCode = checkZero("testVar", *shaderType, 0);

        const std::string workgroupSizeAttr =
            std::to_string(workgroupSize[0]) + "," +
            std::to_string(workgroupSize[1]) + "," +
            std::to_string(workgroupSize[2]);

        const std::string wgsl =
            "\n      " + moduleScope +
            "\n      @compute @workgroup_size(" + workgroupSizeAttr + ")" +
            "\n      fn main() {\n        " + functionScope +
            "\n        " + checkZeroCode +
            "\n        _ = zero;\n      }\n    ";

        if (addressSpace == "workgroup") {
            // Populate the maximum amount of workgroup memory with known values
            // to ensure initialization overrides in another shader.
            const uint32_t wgMemoryLimits = limits.maxComputeWorkgroupStorageSize;
            const uint32_t wgXDim = limits.maxComputeWorkgroupSizeX;
            const std::string limitText = std::to_string(wgMemoryLimits);
            const std::string xDimText = std::to_string(wgXDim);

            const std::string fillWgsl =
                "\n      @group(0) @binding(0) var<storage, read> inputs : array<u32>;"
                "\n      @group(0) @binding(1) var<storage, read_write> outputs : array<u32>;"
                "\n      var<workgroup> wg_mem : array<u32, " + limitText + " / 4>;"
                "\n"
                "\n      @compute @workgroup_size(" + xDimText + ")"
                "\n      fn fill(@builtin(local_invocation_index) lid : u32) {"
                "\n        const num_u32_per_invocation = " + limitText + " / (4 * " + xDimText + ");"
                "\n"
                "\n        for (var i = 0u; i < num_u32_per_invocation; i++) {"
                "\n          let idx = num_u32_per_invocation * lid + i;"
                "\n          wg_mem[idx] = inputs[idx];"
                "\n        }"
                "\n        workgroupBarrier();"
                "\n        // Copy out to avoid wg_mem being elided."
                "\n        for (var i = 0u; i < num_u32_per_invocation; i++) {"
                "\n          let idx = num_u32_per_invocation * lid + i;"
                "\n          outputs[idx] = wg_mem[idx];"
                "\n        }"
                "\n      }"
                "\n      ";

            std::array<WGPUBindGroupLayoutEntry, 2> fillLayoutEntries;
            fillLayoutEntries[0] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            fillLayoutEntries[0].binding = 0;
            fillLayoutEntries[0].visibility = WGPUShaderStage_Compute;
            fillLayoutEntries[0].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            fillLayoutEntries[0].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
            fillLayoutEntries[1] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            fillLayoutEntries[1].binding = 1;
            fillLayoutEntries[1].visibility = WGPUShaderStage_Compute;
            fillLayoutEntries[1].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            fillLayoutEntries[1].buffer.type = WGPUBufferBindingType_Storage;

            WGPUBindGroupLayoutDescriptor fillLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            fillLayoutDesc.entryCount = fillLayoutEntries.size();
            fillLayoutDesc.entries = fillLayoutEntries.data();
            WGPUBindGroupLayout fillLayout = t.createBindGroupLayoutTracked(fillLayoutDesc);

            WGPUPipelineLayoutDescriptor fillPipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
            fillPipelineLayoutDesc.bindGroupLayoutCount = 1;
            fillPipelineLayoutDesc.bindGroupLayouts = &fillLayout;
            WGPUPipelineLayout fillPipelineLayout = t.createPipelineLayoutTracked(fillPipelineLayoutDesc);

            WGPUShaderModule fillModule = t.createShaderModuleTracked(fillWgsl);

            WGPUComputePipelineDescriptor fillPipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            fillPipelineDesc.label = sv("Workgroup Fill Pipeline");
            fillPipelineDesc.layout = fillPipelineLayout;
            fillPipelineDesc.compute.module = fillModule;
            fillPipelineDesc.compute.entryPoint = sv("fill");
            WGPUComputePipeline fillPipeline = t.createComputePipelineTracked(fillPipelineDesc);

            const size_t inputWordCount = wgMemoryLimits / 4u;
            std::vector<uint32_t> inputData(inputWordCount, 0xdeadbeefu);
            WGPUBuffer inputBuffer = t.makeBufferWithContents(
                inputData.data(),
                inputData.size() * sizeof(uint32_t),
                WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst);

            WGPUBufferDescriptor outputBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            outputBufferDesc.size = wgMemoryLimits;
            outputBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
            WGPUBuffer outputBuffer = t.createBufferTracked(outputBufferDesc);

            std::array<WGPUBindGroupEntry, 2> fillEntries;
            fillEntries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
            fillEntries[0].binding = 0;
            fillEntries[0].buffer = inputBuffer;
            fillEntries[0].offset = 0;
            fillEntries[0].size = wgMemoryLimits;
            fillEntries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
            fillEntries[1].binding = 1;
            fillEntries[1].buffer = outputBuffer;
            fillEntries[1].offset = 0;
            fillEntries[1].size = wgMemoryLimits;

            WGPUBindGroupDescriptor fillBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            fillBindGroupDesc.layout = fillLayout;
            fillBindGroupDesc.entryCount = fillEntries.size();
            fillBindGroupDesc.entries = fillEntries.data();
            WGPUBindGroup fillBindGroup = t.createBindGroupTracked(fillBindGroupDesc);

            WGPUCommandEncoder fillEncoder = t.createCommandEncoderTracked();
            WGPUComputePassDescriptor fillPassDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            WGPUComputePassEncoder fillPass = wgpuCommandEncoderBeginComputePass(fillEncoder, &fillPassDesc);
            wgpuComputePassEncoderSetPipeline(fillPass, fillPipeline);
            wgpuComputePassEncoderSetBindGroup(fillPass, 0, fillBindGroup, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(fillPass, 1, 1, 1);
            wgpuComputePassEncoderEnd(fillPass);
            WGPUCommandBuffer fillCommands = t.finishTracked(fillEncoder);
            wgpuQueueSubmit(t.queue(), 1, &fillCommands);
        }

        WGPUShaderModule module = t.createShaderModuleTracked(wgsl);

        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout = nullptr; // auto layout
        pipelineDesc.compute.module = module;
        pipelineDesc.compute.entryPoint = sv("main");
        WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipelineDesc);

        // Result buffer: 4 bytes, zero-initialized by WebGPU (never pre-filled
        // with expected values).
        WGPUBufferDescriptor resultBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        resultBufferDesc.size = 4;
        resultBufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
        WGPUBuffer resultBuffer = t.createBufferTracked(resultBufferDesc);

        WGPUBufferDescriptor zeroBufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        zeroBufferDesc.size = 4;
        zeroBufferDesc.usage = WGPUBufferUsage_Uniform;
        WGPUBuffer zeroBuffer = t.createBufferTracked(zeroBufferDesc);

        WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);

        std::array<WGPUBindGroupEntry, 2> entries;
        entries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[0].binding = 0;
        entries[0].buffer = resultBuffer;
        entries[0].offset = 0;
        entries[0].size = 4;
        entries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[1].binding = 1;
        entries[1].buffer = zeroBuffer;
        entries[1].offset = 0;
        entries[1].size = 4;

        WGPUBindGroupDescriptor bindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bindGroupDesc.layout = bgl;
        bindGroupDesc.entryCount = entries.size();
        bindGroupDesc.entries = entries.data();
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bindGroupDesc);
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

        const uint32_t expected = 0u;
        t.expectGPUBufferValuesEqual(resultBuffer, &expected, sizeof(expected));
    });

} // namespace
