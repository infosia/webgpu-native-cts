// Ported from gpuweb/cts src/webgpu/shader/execution/expression/access/structure/index.spec.ts
// @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for structure member access expressions (s.member). A WGSL struct with
// heterogeneous members (bool/u32/i32/f32/f16/vec3f/vec4i/vec2i/mat3x2f) is declared; the struct is
// supplied as a uniform/storage buffer (or a const / let / param / nested const value), and one
// member is read back. Value-EXACT: the expected result is the real stored member, compared
// bit-for-bit (no tolerance). f16 members are skipped when 'shader-f16' is absent. The member-offset
// packing honors the WGSL alignment/@align/@size rules (the struct buffer is packed at the exact
// member byte offsets via the shared structLayout helper).
//
// This file does NOT use the generic expression run(); it ports the upstream file's own local run()
// (custom struct in/out buffer packing) faithfully.

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "webgpu/shader/execution/expression/access/vector/access_common.h"
#include "webgpu/shader/execution/expression/expression.h"

using namespace cts;
using namespace cts::expression;
using namespace cts::expression::access;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,access,structure,index",
    "Execution Tests for structure member accessing expressions");

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

// The member type sets used by the tests. Each inner list is one struct layout; the i-th member has
// type kMemberTypes[set][i]. kMemberTypesNoBool excludes any layout containing a bool member.
const std::vector<std::vector<std::string>>& kMemberTypes() {
    static const std::vector<std::vector<std::string>> v = {
        {"bool"},
        {"u32"},
        {"vec3f"},
        {"i32", "u32"},
        {"i32", "f16", "vec4i", "mat3x2f"},
        {"bool", "u32", "f16", "vec3f", "vec2i"},
        {"i32", "u32", "f32", "f16", "vec3f", "vec4i"},
    };
    return v;
}

const std::vector<std::vector<std::string>>& kMemberTypesNoBool() {
    static const std::vector<std::vector<std::string>> v = [] {
        std::vector<std::vector<std::string>> out;
        for (const auto& tys : kMemberTypes()) {
            bool hasBool = false;
            for (const auto& ty : tys) {
                if (ty == "bool") {
                    hasBool = true;
                }
            }
            if (!hasBool) {
                out.push_back(tys);
            }
        }
        return out;
    }();
    return v;
}

// Joins a member-type list into a single comma-separated token used as the 'member_types' param
// value (our Value type holds scalars, not arrays; the comma-joined token keeps case identity 1:1
// with upstream's array param while remaining a scalar string).
std::string joinTypes(const std::vector<std::string>& tys) {
    std::string out;
    for (size_t i = 0; i < tys.size(); ++i) {
        if (i > 0) {
            out += ",";
        }
        out += tys[i];
    }
    return out;
}

std::vector<std::string> splitTypes(const std::string& joined) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : joined) {
        if (c == ',') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    out.push_back(cur);
    return out;
}

bool listHasF16(const std::vector<std::string>& tys) {
    for (const auto& ty : tys) {
        if (ty == "f16") {
            return true;
        }
    }
    return false;
}

// The ExprType for a member-type name.
ExprType memberType(const std::string& name) {
    if (name == "bool") {
        return scalarType(ScalarKind::Bool);
    }
    if (name == "u32") {
        return scalarType(ScalarKind::U32);
    }
    if (name == "i32") {
        return scalarType(ScalarKind::I32);
    }
    if (name == "f32") {
        return scalarType(ScalarKind::F32);
    }
    if (name == "f16") {
        return scalarType(ScalarKind::F16);
    }
    if (name == "vec3f") {
        return vecType(3, ScalarKind::F32);
    }
    if (name == "vec4i") {
        return vecType(4, ScalarKind::I32);
    }
    if (name == "vec2i") {
        return vecType(2, ScalarKind::I32);
    }
    if (name == "mat3x2f") {
        return matType(3, 2, ScalarKind::F32);
    }
    // Unreachable for the member types these tests use.
    return scalarType(ScalarKind::U32);
}

// A member value of type 'name' with every scalar slot set to integer 'value' (upstream
// Type[name].create(value)).
CaseValue memberValue(const std::string& name, int value) {
    const ExprType ty = memberType(name);
    const ScalarKind sk = ty.scalarKind();
    const int slots = (ty.form == TypeForm::Matrix) ? ty.cols * ty.width : ty.width;
    std::vector<Scalar> els;
    els.reserve(static_cast<size_t>(slots));
    for (int i = 0; i < slots; ++i) {
        els.push_back(makeConcrete(sk, value));
    }
    return slots == 1 ? CaseValue(els[0]) : CaseValue::vec(els);
}

// The list of member-index subcases for the given member-type list.
std::vector<Value> memberIndices(const std::vector<std::string>& tys) {
    std::vector<Value> out;
    for (size_t i = 0; i < tys.size(); ++i) {
        out.push_back(Value(static_cast<int64_t>(i)));
    }
    return out;
}

// ---------------------------------------------------------------------------
// The local run(): build the output buffer (one expected value), optionally build/pack the struct
// input buffer, dispatch, read back the expected member, compare bit-exact. Mirrors upstream's
// file-local run(). 'expectedType' is the WGSL type of the read-back value; 'expectedValue' is the
// expected stored value. 'input' (when present) is either packed from member values or pre-built
// bytes. 'inputBytes' (non-empty) overrides member-value packing with a raw buffer.
void runStruct(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& wgsl,
    const ExprType& expectedType,
    const CaseValue& expectedValue,
    bool hasInput,
    InputSource inputSource,
    const std::vector<ExprType>& inputMemberTypes,
    const std::vector<CaseValue>& inputMemberValues,
    const std::vector<uint8_t>& inputBytes) {
    const uint32_t kMinStorageBufferSize = 4;

    // Output buffer: holds the single expected value (storage_rw layout). Zero-filled.
    const uint32_t outputBufferSize =
        std::max(kMinStorageBufferSize, structStride({expectedType}, InputSource::StorageRW));
    WGPUBufferDescriptor odesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    odesc.size = outputBufferSize;
    odesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage;
    WGPUBuffer outputBuffer = t.createBufferTracked(odesc);

    // Input buffer (if any).
    WGPUBuffer inputBuffer = nullptr;
    if (hasInput) {
        std::vector<uint8_t> inputData;
        if (!inputBytes.empty()) {
            inputData = inputBytes;
        } else {
            const uint32_t inputBufferSize =
                std::max(kMinStorageBufferSize, structStride(inputMemberTypes, inputSource));
            inputData.assign(inputBufferSize, 0);
            structLayout(inputMemberTypes, inputSource, [&](const MemberLayout& m) {
                copyValueTo(inputMemberValues[static_cast<size_t>(m.index)], m.type, inputSource,
                            inputData, m.offset);
            });
        }
        const WGPUBufferUsage usage =
            WGPUBufferUsage_CopySrc |
            (inputSource == InputSource::Uniform ? WGPUBufferUsage_Uniform : WGPUBufferUsage_Storage);
        inputBuffer = t.makeBufferWithContents(inputData.data(), inputData.size(), usage);
    }

    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor pdesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pdesc.layout = nullptr;
    pdesc.compute.module = module;
    pdesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pdesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    std::vector<WGPUBindGroupEntry> entries;
    WGPUBindGroupEntry e0 = WGPU_BIND_GROUP_ENTRY_INIT;
    e0.binding = 0;
    e0.buffer = outputBuffer;
    e0.size = outputBufferSize;
    entries.push_back(e0);
    if (inputBuffer != nullptr) {
        WGPUBindGroupEntry e1 = WGPU_BIND_GROUP_ENTRY_INIT;
        e1.binding = 1;
        e1.buffer = inputBuffer;
        e1.size = WGPU_WHOLE_SIZE;
        entries.push_back(e1);
    }
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    WGPUBindGroup group = t.createBindGroupTracked(bgDesc);
    wgpuBindGroupLayoutRelease(bgl);

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
    wgpuComputePassEncoderSetPipeline(pass, pipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, group, 0, nullptr);
    wgpuComputePassEncoderDispatchWorkgroups(pass, 1, 1, 1);
    wgpuComputePassEncoderEnd(pass);
    WGPUCommandBuffer commands = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commands);

    const ExprType exType = expectedType;
    const CaseValue exVal = expectedValue;
    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [exType, exVal](const uint8_t* data, size_t len) -> std::optional<std::string> {
            CaseValue got;
            if (!readValueFrom(exType, InputSource::StorageRW, data, len, 0, got)) {
                return std::string("readback buffer too small");
            }
            bool matched = got.width == exVal.width;
            for (int e = 0; matched && e < exVal.width; ++e) {
                if (got.elements[static_cast<size_t>(e)].bits !=
                    exVal.elements[static_cast<size_t>(e)].bits) {
                    matched = false;
                }
            }
            if (!matched) {
                return std::string("structure member mismatch");
            }
            return std::nullopt;
        },
        0,
        outputBufferSize);
}

// Emits the `struct MyStruct { member_i : ty, ... };` declaration for the given member types.
std::string structDecl(const std::vector<std::string>& tys) {
    std::string out = "struct MyStruct {\n";
    for (size_t i = 0; i < tys.size(); ++i) {
        out += "    member_" + std::to_string(i) + " : " + tys[i] + ",\n";
    }
    out += "};\n";
    return out;
}

// The `MyStruct(v0, v1, ...)` constructor expression from per-member values.
std::string structCtor(const std::vector<std::string>& tys, const std::vector<CaseValue>& values) {
    std::string out = "MyStruct(";
    for (size_t i = 0; i < tys.size(); ++i) {
        if (i > 0) {
            out += ", ";
        }
        out += valueWgsl(values[i], memberType(tys[i]));
    }
    out += ")";
    return out;
}

// ===========================================================================
// buffer / buffer_pointer: struct in a uniform/storage buffer, member read out.
// ===========================================================================
void bufferLike(AllFeaturesMaxLimitsGpuTest& t, bool viaPointer) {
    const std::vector<std::string> memberTypes = splitTypes(t.param<std::string>("member_types"));
    const std::string inputSourceName = t.param<std::string>("inputSource"); // "uniform" | "storage"
    const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
    if (listHasF16(memberTypes)) {
        skipIfNoF16(t);
    }

    std::vector<CaseValue> values;
    std::vector<ExprType> memberExprTypes;
    for (size_t i = 0; i < memberTypes.size(); ++i) {
        values.push_back(memberValue(memberTypes[i], static_cast<int>(i)));
        memberExprTypes.push_back(memberType(memberTypes[i]));
    }
    const ExprType expectedType = memberExprTypes[static_cast<size_t>(memberIndex)];
    const CaseValue expectedValue = values[static_cast<size_t>(memberIndex)];
    const InputSource inputSource =
        inputSourceName == "uniform" ? InputSource::Uniform : InputSource::StorageR;

    std::string wgsl;
    if (listHasF16(memberTypes)) {
        wgsl += "enable f16;\n";
    }
    wgsl += "\n@group(0) @binding(0) var<storage, read_write> output : " +
            storageWgslTypeName(expectedType) + ";\n";
    wgsl += "@group(0) @binding(1) var<" + inputSourceName + "> input : MyStruct;\n\n";
    wgsl += structDecl(memberTypes) + "\n";
    wgsl += "@workgroup_size(1) @compute\nfn main() {\n";
    if (viaPointer) {
        wgsl += "  let ptr = &input;\n";
        wgsl += "  output = (*ptr).member_" + std::to_string(memberIndex) + ";\n";
    } else {
        wgsl += "  output = input.member_" + std::to_string(memberIndex) + ";\n";
    }
    wgsl += "}\n";

    runStruct(t, wgsl, expectedType, expectedValue, /*hasInput=*/true, inputSource, memberExprTypes,
              values, /*inputBytes=*/{});
}

CTS_TEST(testGroup, "buffer")
    .desc("Test accessing of a value structure in a storage or uniform buffer")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const auto& tys : kMemberTypesNoBool()) {
            types.push_back(Value(joinTypes(tys)));
        }
        return u.combine("member_types", types)
            .combine("inputSource", {"uniform", "storage"})
            .beginSubcases()
            .expand("member_index", [](const ParamRecord& p) {
                return memberIndices(splitTypes(valueAs<std::string>(*findParam(p, "member_types"))));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { bufferLike(t, /*viaPointer=*/false); });

CTS_TEST(testGroup, "buffer_pointer")
    .desc("Test accessing of a value structure via a pointer to a storage or uniform buffer")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const auto& tys : kMemberTypesNoBool()) {
            types.push_back(Value(joinTypes(tys)));
        }
        return u.combine("member_types", types)
            .combine("inputSource", {"uniform", "storage"})
            .beginSubcases()
            .expand("member_index", [](const ParamRecord& p) {
                return memberIndices(splitTypes(valueAs<std::string>(*findParam(p, "member_types"))));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) { bufferLike(t, /*viaPointer=*/true); });

// ===========================================================================
// buffer_align / buffer_size: struct in a storage buffer using @align / @size attributes.
// ===========================================================================
CTS_TEST(testGroup, "buffer_align")
    .desc("Test accessing of a value structure in a storage buffer that has members using the "
          "@align attribute")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("member_index", {Value(static_cast<int64_t>(0)), Value(static_cast<int64_t>(1)),
                                      Value(static_cast<int64_t>(2))})
            .combine("alignments", {Value("4,4,4"), Value("4,8,16"), Value("8,4,16"),
                                    Value("8,16,4")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
        const std::vector<std::string> alignStr = splitTypes(t.param<std::string>("alignments"));
        int alignments[3];
        for (int i = 0; i < 3; ++i) {
            alignments[i] = std::stoi(alignStr[static_cast<size_t>(i)]);
        }
        const char* memberTypeNames[3] = {"i32", "u32", "f32"};
        std::vector<CaseValue> values;
        for (int i = 0; i < 3; ++i) {
            values.push_back(memberValue(memberTypeNames[i], i));
        }
        const ExprType expectedType = memberType(memberTypeNames[memberIndex]);
        const CaseValue expectedValue = values[static_cast<size_t>(memberIndex)];

        // struct { pre : i32, @align(a0) member_0 : i32, @align(a1) member_1 : u32,
        //          @align(a2) member_2 : f32, post : i32 }; pack the input bytes by hand.
        std::vector<uint8_t> input(64, 0);
        uint32_t offset = 4; // pre : i32
        for (int i = 0; i < 3; ++i) {
            offset = align(offset, static_cast<uint32_t>(alignments[i]));
            copyValueTo(values[static_cast<size_t>(i)], memberType(memberTypeNames[i]),
                        InputSource::StorageR, input, offset);
            offset += sizeAndAlignmentOf(memberType(memberTypeNames[i]), InputSource::StorageR).size;
        }

        std::string wgsl;
        wgsl += "@group(0) @binding(0) var<storage, read_write> output : " +
                storageWgslTypeName(expectedType) + ";\n";
        wgsl += "@group(0) @binding(1) var<storage> input : MyStruct;\n\n";
        wgsl += "struct MyStruct {\n";
        wgsl += "  pre : i32,\n";
        for (int i = 0; i < 3; ++i) {
            wgsl += "  @align(" + std::to_string(alignments[i]) + ") member_" + std::to_string(i) +
                    " : " + memberTypeNames[i] + ",\n";
        }
        wgsl += "  post : i32,\n";
        wgsl += "};\n\n";
        wgsl += "@workgroup_size(1) @compute\nfn main() {\n";
        wgsl += "output = input.member_" + std::to_string(memberIndex) + ";\n";
        wgsl += "}\n";

        runStruct(t, wgsl, expectedType, expectedValue, /*hasInput=*/true, InputSource::StorageR, {},
                  {}, input);
    });

CTS_TEST(testGroup, "buffer_size")
    .desc("Test accessing of a value structure in a storage buffer that has members using the "
          "@size attribute")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("member_index", {Value(static_cast<int64_t>(0)), Value(static_cast<int64_t>(1)),
                                      Value(static_cast<int64_t>(2))})
            .combine("sizes", {Value("4,4,4"), Value("4,8,16"), Value("8,4,16"), Value("8,16,4")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
        const std::vector<std::string> sizeStr = splitTypes(t.param<std::string>("sizes"));
        int sizes[3];
        for (int i = 0; i < 3; ++i) {
            sizes[i] = std::stoi(sizeStr[static_cast<size_t>(i)]);
        }
        const char* memberTypeNames[3] = {"i32", "u32", "f32"};
        std::vector<CaseValue> values;
        for (int i = 0; i < 3; ++i) {
            values.push_back(memberValue(memberTypeNames[i], i));
        }
        const ExprType expectedType = memberType(memberTypeNames[memberIndex]);
        const CaseValue expectedValue = values[static_cast<size_t>(memberIndex)];

        // struct { pre : i32, @size(s0) member_0 : i32, @size(s1) member_1 : u32,
        //          @size(s2) member_2 : f32, post : i32 }; pack the input bytes by hand.
        std::vector<uint8_t> input(64, 0);
        uint32_t offset = 4; // pre : i32
        for (int i = 0; i < 3; ++i) {
            offset = align(
                offset, sizeAndAlignmentOf(memberType(memberTypeNames[i]), InputSource::StorageR)
                            .alignment);
            copyValueTo(values[static_cast<size_t>(i)], memberType(memberTypeNames[i]),
                        InputSource::StorageR, input, offset);
            offset += static_cast<uint32_t>(sizes[i]);
        }

        std::string wgsl;
        wgsl += "@group(0) @binding(0) var<storage, read_write> output : " +
                storageWgslTypeName(expectedType) + ";\n";
        wgsl += "@group(0) @binding(1) var<storage> input : MyStruct;\n\n";
        wgsl += "struct MyStruct {\n";
        wgsl += "  pre : i32,\n";
        for (int i = 0; i < 3; ++i) {
            wgsl += "  @size(" + std::to_string(sizes[i]) + ") member_" + std::to_string(i) + " : " +
                    memberTypeNames[i] + ",\n";
        }
        wgsl += "  post : i32,\n";
        wgsl += "};\n\n";
        wgsl += "@workgroup_size(1) @compute\nfn main() {\n";
        wgsl += "output = input.member_" + std::to_string(memberIndex) + ";\n";
        wgsl += "}\n";

        runStruct(t, wgsl, expectedType, expectedValue, /*hasInput=*/true, InputSource::StorageR, {},
                  {}, input);
    });

// ===========================================================================
// let / param / const / const_nested: const-eval struct value, member read out.
// ===========================================================================
// Computes the expected (storage) value + type for a member, handling the bool -> u32 mapping that
// the const-eval tests apply (they output select(0u, 1u, v) for bool members).
void constLikeExpected(
    const std::string& memberTypeName,
    const CaseValue& memberVal,
    ExprType& outType,
    CaseValue& outValue,
    bool& isBool) {
    isBool = memberTypeName == "bool";
    if (isBool) {
        outType = scalarType(ScalarKind::U32);
        outValue = CaseValue(u32(memberVal.elements[0].bits != 0 ? 1u : 0u));
    } else {
        outType = memberType(memberTypeName);
        outValue = memberVal;
    }
}

CTS_TEST(testGroup, "let")
    .desc("Test accessing of a let structure")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const auto& tys : kMemberTypes()) {
            types.push_back(Value(joinTypes(tys)));
        }
        return u.combine("member_types", types).beginSubcases().expand(
            "member_index", [](const ParamRecord& p) {
                return memberIndices(splitTypes(valueAs<std::string>(*findParam(p, "member_types"))));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::vector<std::string> memberTypes =
            splitTypes(t.param<std::string>("member_types"));
        const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
        if (listHasF16(memberTypes)) {
            skipIfNoF16(t);
        }
        std::vector<CaseValue> values;
        for (size_t i = 0; i < memberTypes.size(); ++i) {
            values.push_back(memberValue(memberTypes[i], static_cast<int>(i)));
        }
        ExprType expectedType;
        CaseValue expectedValue;
        bool isBool = false;
        constLikeExpected(memberTypes[static_cast<size_t>(memberIndex)],
                          values[static_cast<size_t>(memberIndex)], expectedType, expectedValue,
                          isBool);

        std::string wgsl;
        if (listHasF16(memberTypes)) {
            wgsl += "enable f16;\n";
        }
        wgsl += "\n@group(0) @binding(0) var<storage, read_write> output : " +
                storageWgslTypeName(expectedType) + ";\n\n";
        wgsl += structDecl(memberTypes) + "\n";
        wgsl += "@workgroup_size(1) @compute\nfn main() {\n";
        wgsl += "  let s = " + structCtor(memberTypes, values) + ";\n";
        wgsl += "  let v = s.member_" + std::to_string(memberIndex) + ";\n";
        wgsl += std::string("  output = ") + (isBool ? "select(0u, 1u, v)" : "v") + ";\n";
        wgsl += "}\n";

        runStruct(t, wgsl, expectedType, expectedValue, /*hasInput=*/false, InputSource::Const, {},
                  {}, {});
    });

CTS_TEST(testGroup, "param")
    .desc("Test accessing of a parameter structure")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const auto& tys : kMemberTypes()) {
            types.push_back(Value(joinTypes(tys)));
        }
        return u.combine("member_types", types).beginSubcases().expand(
            "member_index", [](const ParamRecord& p) {
                return memberIndices(splitTypes(valueAs<std::string>(*findParam(p, "member_types"))));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::vector<std::string> memberTypes =
            splitTypes(t.param<std::string>("member_types"));
        const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
        if (listHasF16(memberTypes)) {
            skipIfNoF16(t);
        }
        std::vector<CaseValue> values;
        for (size_t i = 0; i < memberTypes.size(); ++i) {
            values.push_back(memberValue(memberTypes[i], static_cast<int>(i)));
        }
        ExprType expectedType;
        CaseValue expectedValue;
        bool isBool = false;
        constLikeExpected(memberTypes[static_cast<size_t>(memberIndex)],
                          values[static_cast<size_t>(memberIndex)], expectedType, expectedValue,
                          isBool);

        std::string wgsl;
        if (listHasF16(memberTypes)) {
            wgsl += "enable f16;\n";
        }
        wgsl += "\n@group(0) @binding(0) var<storage, read_write> output : " +
                storageWgslTypeName(expectedType) + ";\n\n";
        wgsl += structDecl(memberTypes) + "\n";
        wgsl += "fn f(s : MyStruct) -> " + memberTypes[static_cast<size_t>(memberIndex)] + " {\n";
        wgsl += "  return s.member_" + std::to_string(memberIndex) + ";\n";
        wgsl += "}\n\n";
        wgsl += "@workgroup_size(1) @compute\nfn main() {\n";
        wgsl += "  let v = f(" + structCtor(memberTypes, values) + ");\n";
        wgsl += std::string("  output = ") + (isBool ? "select(0u, 1u, v)" : "v") + ";\n";
        wgsl += "}\n";

        runStruct(t, wgsl, expectedType, expectedValue, /*hasInput=*/false, InputSource::Const, {},
                  {}, {});
    });

CTS_TEST(testGroup, "const")
    .desc("Test accessing of a const value structure")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const auto& tys : kMemberTypes()) {
            types.push_back(Value(joinTypes(tys)));
        }
        return u.combine("member_types", types).beginSubcases().expand(
            "member_index", [](const ParamRecord& p) {
                return memberIndices(splitTypes(valueAs<std::string>(*findParam(p, "member_types"))));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::vector<std::string> memberTypes =
            splitTypes(t.param<std::string>("member_types"));
        const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
        if (listHasF16(memberTypes)) {
            skipIfNoF16(t);
        }
        std::vector<CaseValue> values;
        for (size_t i = 0; i < memberTypes.size(); ++i) {
            values.push_back(memberValue(memberTypes[i], static_cast<int>(i)));
        }
        ExprType expectedType;
        CaseValue expectedValue;
        bool isBool = false;
        constLikeExpected(memberTypes[static_cast<size_t>(memberIndex)],
                          values[static_cast<size_t>(memberIndex)], expectedType, expectedValue,
                          isBool);

        std::string wgsl;
        if (listHasF16(memberTypes)) {
            wgsl += "enable f16;\n";
        }
        wgsl += "\n@group(0) @binding(0) var<storage, read_write> output : " +
                storageWgslTypeName(expectedType) + ";\n\n";
        wgsl += structDecl(memberTypes) + "\n";
        wgsl += "const S = " + structCtor(memberTypes, values) + ";\n\n";
        wgsl += "@workgroup_size(1) @compute\nfn main() {\n";
        wgsl += "  let v = S.member_" + std::to_string(memberIndex) + ";\n";
        wgsl += std::string("  output = ") + (isBool ? "select(0u, 1u, v)" : "v") + ";\n";
        wgsl += "}\n";

        runStruct(t, wgsl, expectedType, expectedValue, /*hasInput=*/false, InputSource::Const, {},
                  {}, {});
    });

CTS_TEST(testGroup, "const_nested")
    .desc("Test accessing of a const value structure nested in another structure")
    .params([](ParamsBuilder u) {
        std::vector<Value> types;
        for (const auto& tys : kMemberTypes()) {
            types.push_back(Value(joinTypes(tys)));
        }
        return u.combine("member_types", types).beginSubcases().expand(
            "member_index", [](const ParamRecord& p) {
                return memberIndices(splitTypes(valueAs<std::string>(*findParam(p, "member_types"))));
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::vector<std::string> memberTypes =
            splitTypes(t.param<std::string>("member_types"));
        const int memberIndex = static_cast<int>(t.param<int64_t>("member_index"));
        if (listHasF16(memberTypes)) {
            skipIfNoF16(t);
        }
        std::vector<CaseValue> values;
        for (size_t i = 0; i < memberTypes.size(); ++i) {
            values.push_back(memberValue(memberTypes[i], static_cast<int>(i)));
        }
        ExprType expectedType;
        CaseValue expectedValue;
        bool isBool = false;
        constLikeExpected(memberTypes[static_cast<size_t>(memberIndex)],
                          values[static_cast<size_t>(memberIndex)], expectedType, expectedValue,
                          isBool);

        std::string wgsl;
        if (listHasF16(memberTypes)) {
            wgsl += "enable f16;\n";
        }
        wgsl += "\n@group(0) @binding(0) var<storage, read_write> output : " +
                storageWgslTypeName(expectedType) + ";\n\n";
        wgsl += structDecl(memberTypes) + "\n";
        wgsl += "struct Outer {\n  pre : i32,\n  inner : MyStruct,\n  post : i32,\n}\n\n";
        wgsl += "const S = Outer(10, " + structCtor(memberTypes, values) + ", 20);\n\n";
        wgsl += "@workgroup_size(1) @compute\nfn main() {\n";
        wgsl += "  let v = S.inner.member_" + std::to_string(memberIndex) + ";\n";
        wgsl += std::string("  output = ") + (isBool ? "select(0u, 1u, v)" : "v") + ";\n";
        wgsl += "}\n";

        runStruct(t, wgsl, expectedType, expectedValue, /*hasInput=*/false, InputSource::Const, {},
                  {}, {});
    });

} // namespace
