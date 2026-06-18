// Ported from gpuweb/cts src/webgpu/shader/execution/expression/expression.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.

#include "webgpu/shader/execution/expression/expression.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

namespace cts {
namespace expression {

namespace {

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

uint32_t alignUp(uint32_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

const char* scalarKindName(ScalarKind kind) {
    switch (kind) {
        case ScalarKind::U32:
            return "u32";
        case ScalarKind::I32:
            return "i32";
        case ScalarKind::Bool:
            return "bool";
        case ScalarKind::F32:
            return "f32";
        case ScalarKind::F16:
            return "f16";
        case ScalarKind::AbstractInt:
        case ScalarKind::AbstractFloat:
            // Abstract numeric types are spelled implicitly via untyped literals; there is no
            // concrete WGSL type name for them in this harness.
            break;
    }
    std::abort();
}

// Element byte width: f16 is 2 bytes, everything else (u32/i32/f32/bool-as-u32) is 4.
uint32_t elementBytes(ScalarKind kind) {
    return kind == ScalarKind::F16 ? 2u : 4u;
}

// WGSL type spelling of an ExprType (e.g. "u32", "vec3<i32>").
std::string typeName(ExprType ty) {
    if (ty.width == 1) {
        return scalarKindName(ty.kind);
    }
    return "vec" + std::to_string(ty.width) + "<" + scalarKindName(ty.kind) + ">";
}

// The storage element kind: bool is stored as u32. The width is preserved.
ScalarKind storageKind(ScalarKind kind) {
    return kind == ScalarKind::Bool ? ScalarKind::U32 : kind;
}

ExprType storageType(ExprType ty) {
    return ExprType{storageKind(ty.kind), ty.width};
}

std::string storageTypeName(ExprType ty) {
    return typeName(storageType(ty));
}

// size and alignment in bytes of the type, per upstream sizeAndAlignmentOf.
struct SizeAlign {
    uint32_t size;
    uint32_t alignment;
};

SizeAlign sizeAndAlignmentOf(ExprType ty) {
    // scalar: size = element bytes (4 for u32/i32/f32/bool, 2 for f16), alignment == size.
    const uint32_t eb = elementBytes(ty.kind);
    SizeAlign out{eb, eb};
    if (ty.width > 1) {
        const uint32_t n = ty.width == 3 ? 4u : static_cast<uint32_t>(ty.width);
        out.size = eb * n;
        out.alignment = eb * n;
    }
    return out;
}

uint32_t strideOf(ExprType ty) {
    SizeAlign sa = sizeAndAlignmentOf(ty);
    return alignUp(sa.size, sa.alignment);
}

// structLayout over the member types, invoking 'cb' per member, returning {size, stride}.
struct MemberInfo {
    int index;
    ExprType type;
    uint32_t size;
    uint32_t alignment;
    uint32_t offset;
};

struct StructLayout {
    uint32_t size;
    uint32_t stride;
    uint32_t alignment;
};

StructLayout structLayout(
    const std::vector<ExprType>& members,
    InputSource source,
    const std::function<void(const MemberInfo&)>& cb) {
    uint32_t offset = 0;
    uint32_t alignment = 1;
    for (size_t i = 0; i < members.size(); ++i) {
        SizeAlign sa = sizeAndAlignmentOf(members[i]);
        offset = alignUp(offset, sa.alignment);
        if (cb) {
            cb(MemberInfo{static_cast<int>(i), members[i], sa.size, sa.alignment, offset});
        }
        offset += sa.size;
        if (sa.alignment > alignment) {
            alignment = sa.alignment;
        }
    }
    // MAINTENANCE_TODO(#4485): remove when implementors support uniform_buffer_standard_layout.
    if (source == InputSource::Uniform) {
        alignment = alignUp(alignment, 16);
    }
    const uint32_t size = offset;
    const uint32_t stride = alignUp(size, alignment);
    return StructLayout{size, stride, alignment};
}

uint32_t structStride(const std::vector<ExprType>& members, InputSource source) {
    return structLayout(members, source, nullptr).stride;
}

// WGSL spelling of the struct members for the Input struct.
std::string wgslMembers(
    const std::vector<ExprType>& members,
    InputSource source,
    const std::function<std::string(int)>& memberName) {
    std::ostringstream out;
    int line = 0;
    StructLayout layout = structLayout(members, source, [&](const MemberInfo& m) {
        out << "  @size(" << m.size << ") " << memberName(line) << " : " << typeName(m.type) << ",\n";
        ++line;
    });
    const uint32_t padding = layout.stride - layout.size;
    if (padding > 0) {
        // Pad with an 'f16' if the padding requires an odd multiple of 2 bytes.
        const char* ty = (padding & 2u) != 0 ? "f16" : "i32";
        out << "  @size(" << padding << ") padding : " << ty << ",\n";
    }
    return out.str();
}

// WGSL expression converting a value from the storage type (bool: != 0).
std::string fromStorage(ExprType ty, const std::string& expr) {
    if (ty.kind == ScalarKind::Bool) {
        if (ty.width == 1) {
            return expr + " != 0u";
        }
        return "(" + expr + " != vec" + std::to_string(ty.width) + "<u32>(0u))";
    }
    return expr;
}

// WGSL expression converting a value to the storage type (bool: select(0,1,e)).
std::string toStorage(ExprType ty, const std::string& expr) {
    if (ty.kind == ScalarKind::Bool) {
        if (ty.width == 1) {
            return "select(0u, 1u, " + expr + ")";
        }
        const std::string z = "vec" + std::to_string(ty.width) + "<u32>(0u)";
        const std::string o = "vec" + std::to_string(ty.width) + "<u32>(1u)";
        return "select(" + z + ", " + o + ", " + expr + ")";
    }
    return expr;
}

// Emits a decimal literal that exactly represents the given finite float value, with an
// explicit decimal point and the given WGSL suffix ('f' for f32, '' for AbstractFloat).
// Uses round-trip-precise digits. Only used for finite values.
std::string floatLiteral(double value, const char* suffix) {
    std::ostringstream out;
    out.precision(17);
    out << value;
    std::string s = out.str();
    // Ensure a decimal point / exponent so it is parsed as a float, not an int.
    if (s.find('.') == std::string::npos && s.find('e') == std::string::npos &&
        s.find('E') == std::string::npos && s.find("inf") == std::string::npos &&
        s.find("nan") == std::string::npos) {
        s += ".0";
    }
    return s + suffix;
}

float f32FromBits(uint32_t bits) {
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

// WGSL literal for a scalar value as a const-expression input.
std::string scalarWgsl(const Scalar& s) {
    switch (s.kind) {
        case ScalarKind::U32:
            return std::to_string(s.bits) + "u";
        case ScalarKind::I32: {
            // i32Bits: reinterpret as a signed value, then emit bitcast<i32> of the u32 pattern
            // to avoid literal-range issues for high-bit-set values.
            return "bitcast<i32>(" + std::to_string(s.bits) + "u)";
        }
        case ScalarKind::Bool:
            return s.bits != 0 ? "true" : "false";
        case ScalarKind::F32:
            // Exact: reinterpret the stored 32-bit pattern (handles all finite values precisely).
            return "bitcast<f32>(" + std::to_string(s.bits) + "u)";
        case ScalarKind::F16:
            // Exact: pack the 16-bit pattern into the low half of a u32 and take the low element.
            return "bitcast<vec2<f16>>(" + std::to_string(s.bits & 0xFFFFu) + "u).x";
        case ScalarKind::AbstractInt:
            // AbstractInt literal: emit the signed decimal value (no suffix).
            return std::to_string(static_cast<int32_t>(s.bits));
        case ScalarKind::AbstractFloat:
            // AbstractFloat literal: emit an exact decimal of the finite f32 value (no suffix).
            return floatLiteral(static_cast<double>(f32FromBits(s.bits)), "");
    }
    std::abort();
}

// WGSL literal for a value (scalar or vecN constructor).
std::string valueWgsl(const CaseValue& v, ScalarKind kind) {
    if (v.width == 1) {
        return scalarWgsl(v.elements[0]);
    }
    // Abstract vectors are spelled without an explicit element type (vecN(...)).
    const bool isAbstract =
        kind == ScalarKind::AbstractInt || kind == ScalarKind::AbstractFloat;
    std::ostringstream out;
    if (isAbstract) {
        out << "vec" << v.width << "(";
    } else {
        out << "vec" << v.width << "<" << scalarKindName(kind) << ">(";
    }
    for (int i = 0; i < v.width; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << scalarWgsl(v.elements[static_cast<size_t>(i)]);
    }
    out << ")";
    return out.str();
}

// WGSL output struct + bound output array declaration.
std::string wgslOutputs(ExprType resultType, size_t count) {
    std::ostringstream out;
    out << "struct Output {\n"
        << "  @size(" << strideOf(resultType) << ") value : " << storageTypeName(resultType) << "\n"
        << "};\n"
        << "@group(0) @binding(0) var<storage, read_write> outputs : array<Output, " << count
        << ">;\n";
    return out.str();
}

// WGSL var declaration for a runtime input source.
std::string wgslInputVar(InputSource source, size_t count) {
    const std::string suffix = "inputs : array<Input, " + std::to_string(count) + ">;";
    switch (source) {
        case InputSource::StorageR:
            return "@group(0) @binding(1) var<storage, read> " + suffix;
        case InputSource::StorageRW:
            return "@group(0) @binding(1) var<storage, read_write> " + suffix;
        case InputSource::Uniform:
            return "@group(0) @binding(1) var<uniform> " + suffix;
        case InputSource::Const:
            break;
    }
    std::abort();
}

// Builds the WGSL shader for a batch of cases.
std::string buildShader(
    const ExpressionBuilder& exprBuilder,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    const std::vector<Case>& cases) {
    std::ostringstream out;

    // Emit `enable f16;` if any parameter or the result type is an f16 type.
    bool usesF16 = resultType.kind == ScalarKind::F16;
    for (ExprType ty : parameterTypes) {
        usesF16 = usesF16 || ty.kind == ScalarKind::F16;
    }
    if (usesF16) {
        out << "enable f16;\n";
    }

    if (inputSource == InputSource::Const) {
        // Constant eval, 'direct' mode: assign each case's evaluated expression to the output.
        out << wgslOutputs(resultType, cases.size()) << "\n";
        out << "@compute @workgroup_size(1)\nfn main() {\n";
        for (size_t i = 0; i < cases.size(); ++i) {
            std::vector<std::string> args;
            args.reserve(parameterTypes.size());
            for (size_t p = 0; p < parameterTypes.size(); ++p) {
                args.push_back(valueWgsl(cases[i].inputs[p], parameterTypes[p].kind));
            }
            const std::string expr = exprBuilder(args);
            out << "  outputs[" << i << "].value = " << toStorage(resultType, expr) << ";\n";
        }
        out << "}\n";
        return out.str();
    }

    // Runtime eval (uniform / storage_r / storage_rw).
    std::vector<ExprType> storageParams;
    storageParams.reserve(parameterTypes.size());
    for (ExprType ty : parameterTypes) {
        storageParams.push_back(storageType(ty));
    }

    out << "struct Input {\n"
        << wgslMembers(storageParams, inputSource,
                       [](int i) { return "param" + std::to_string(i); })
        << "}\n\n";
    out << wgslOutputs(resultType, cases.size()) << "\n";
    out << wgslInputVar(inputSource, cases.size()) << "\n\n";

    std::vector<std::string> args;
    args.reserve(parameterTypes.size());
    for (size_t p = 0; p < parameterTypes.size(); ++p) {
        args.push_back(fromStorage(parameterTypes[p], "inputs[i].param" + std::to_string(p)));
    }
    const std::string expr = toStorage(resultType, exprBuilder(args));

    out << "@compute @workgroup_size(1)\nfn main() {\n"
        << "  for (var i = 0; i < " << cases.size() << "; i++) {\n"
        << "    outputs[i].value = " << expr << ";\n"
        << "  }\n"
        << "}\n";
    return out.str();
}

WGPUComputePipeline createComputePipelineAuto(GpuTest& t, const std::string& wgsl) {
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout = nullptr;
    desc.compute.module = shaderModule;
    desc.compute.entryPoint = sv("main");
    return t.createComputePipelineTracked(desc);
}

// Packs scalar cases into vectors of the given width (packScalarsToVector).
std::vector<Case> packScalarsToVector(
    std::vector<ExprType>& parameterTypes,
    ExprType& resultType,
    const std::vector<Case>& cases,
    int vectorWidth) {
    std::vector<Case> packed;
    auto clampIdx = [&](size_t idx) { return idx < cases.size() ? idx : cases.size() - 1; };

    size_t caseIdx = 0;
    while (caseIdx < cases.size()) {
        Case pc;
        pc.inputs.resize(parameterTypes.size());
        for (size_t p = 0; p < parameterTypes.size(); ++p) {
            std::vector<Scalar> els;
            els.reserve(static_cast<size_t>(vectorWidth));
            for (int i = 0; i < vectorWidth; ++i) {
                const Case& src = cases[clampIdx(caseIdx + static_cast<size_t>(i))];
                els.push_back(src.inputs[p].elements[0]);
            }
            pc.inputs[p] = CaseValue::vec(std::move(els));
        }
        std::vector<Scalar> rels;
        rels.reserve(static_cast<size_t>(vectorWidth));
        bool anyAccept = false;
        for (int i = 0; i < vectorWidth; ++i) {
            const Case& src = cases[clampIdx(caseIdx + static_cast<size_t>(i))];
            rels.push_back(src.expected.elements[0]);
            anyAccept = anyAccept || !src.expectedAccept.empty();
        }
        pc.expected = CaseValue::vec(std::move(rels));
        if (anyAccept) {
            pc.expectedAccept.reserve(static_cast<size_t>(vectorWidth));
            for (int i = 0; i < vectorWidth; ++i) {
                const Case& src = cases[clampIdx(caseIdx + static_cast<size_t>(i))];
                if (!src.expectedAccept.empty()) {
                    pc.expectedAccept.push_back(src.expectedAccept[0]);
                } else {
                    // No acceptance override on this source: require exact match of its expected.
                    ExpectedElement ee;
                    ee.acceptBits.push_back(src.expected.elements[0].bits);
                    pc.expectedAccept.push_back(ee);
                }
            }
        }
        packed.push_back(std::move(pc));
        caseIdx += static_cast<size_t>(vectorWidth);
    }

    for (ExprType& ty : parameterTypes) {
        ty.width = vectorWidth;
    }
    resultType.width = vectorWidth;
    return packed;
}

std::string formatValue(const CaseValue& v) {
    std::ostringstream out;
    if (v.width > 1) {
        out << "(";
    }
    for (int i = 0; i < v.width; ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << v.elements[static_cast<size_t>(i)].bits;
    }
    if (v.width > 1) {
        out << ")";
    }
    return out.str();
}

// Runs one batch of cases (all of which fit within the binding limits).
void submitBatch(
    GpuTest& t,
    const ExpressionBuilder& exprBuilder,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    const std::vector<Case>& cases) {
    const std::string source =
        buildShader(exprBuilder, parameterTypes, resultType, inputSource, cases);
    WGPUComputePipeline pipeline = createComputePipelineAuto(t, source);

    // Output buffer: one Output (stride-padded) per case.
    const uint32_t outputStride = structStride({resultType}, InputSource::StorageRW);
    const uint64_t outputBufferSize = alignUp(static_cast<uint32_t>(cases.size()) * outputStride, 4);
    WGPUBufferDescriptor obDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    obDesc.size = outputBufferSize;
    obDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
    WGPUBuffer outputBuffer = t.createBufferTracked(obDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    std::vector<WGPUBindGroupEntry> entries;

    WGPUBuffer inputBuffer = nullptr;
    uint32_t caseStride = 0;
    if (inputSource != InputSource::Const) {
        caseStride = structStride(parameterTypes, inputSource);
        const uint32_t inputSize = alignUp(static_cast<uint32_t>(cases.size()) * caseStride, 4);
        std::vector<uint8_t> inputData(inputSize, 0);
        for (size_t caseIdx = 0; caseIdx < cases.size(); ++caseIdx) {
            const uint32_t base = static_cast<uint32_t>(caseIdx) * caseStride;
            structLayout(parameterTypes, inputSource, [&](const MemberInfo& m) {
                const CaseValue& arg = cases[caseIdx].inputs[static_cast<size_t>(m.index)];
                const uint32_t eb = elementBytes(m.type.kind);
                // Each element occupies 'eb' bytes (2 for f16, 4 otherwise); vec3 pads the 4th
                // element slot which we leave zero.
                for (int e = 0; e < arg.width; ++e) {
                    const uint32_t bits = arg.elements[static_cast<size_t>(e)].bits;
                    std::memcpy(&inputData[base + m.offset + static_cast<uint32_t>(e) * eb], &bits, eb);
                }
            });
        }
        WGPUBufferUsage usage = WGPUBufferUsage_CopySrc |
                                (inputSource == InputSource::Uniform ? WGPUBufferUsage_Uniform
                                                                     : WGPUBufferUsage_Storage);
        inputBuffer = t.makeBufferWithContents(inputData.data(), inputData.size(), usage);
    }

    WGPUBindGroupEntry e0 = WGPU_BIND_GROUP_ENTRY_INIT;
    e0.binding = 0;
    e0.buffer = outputBuffer;
    e0.offset = 0;
    e0.size = outputBufferSize;
    entries.push_back(e0);
    if (inputBuffer != nullptr) {
        WGPUBindGroupEntry e1 = WGPU_BIND_GROUP_ENTRY_INIT;
        e1.binding = 1;
        e1.buffer = inputBuffer;
        e1.offset = 0;
        // Storage/uniform binding sizes must be a multiple of 4; the buffer itself is allocated
        // 4-aligned, so binding the rounded-up size stays within bounds (matters for f16, whose
        // case stride can be 2 bytes).
        e1.size = alignUp(static_cast<uint32_t>(cases.size()) * caseStride, 4);
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

    // Read back and compare (bit-exact unless the case carries a per-element acceptance set).
    const ExprType result = resultType;
    const uint32_t stride = outputStride;
    const uint32_t resElemBytes = elementBytes(resultType.kind);
    std::vector<Case> casesCopy = cases;
    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [result, stride, resElemBytes, casesCopy](
            const uint8_t* data, size_t len) -> std::optional<std::string> {
            std::ostringstream errs;
            int failures = 0;
            for (size_t caseIdx = 0; caseIdx < casesCopy.size(); ++caseIdx) {
                const uint32_t off = static_cast<uint32_t>(caseIdx) * stride;
                CaseValue got;
                got.width = result.width;
                got.elements.resize(static_cast<size_t>(result.width));
                bool overflow = false;
                for (int e = 0; e < result.width; ++e) {
                    const size_t byteOff = off + static_cast<size_t>(e) * resElemBytes;
                    if (byteOff + resElemBytes > len) {
                        overflow = true;
                        break;
                    }
                    uint32_t bits = 0;
                    std::memcpy(&bits, data + byteOff, resElemBytes);
                    got.elements[static_cast<size_t>(e)] = Scalar{result.kind, bits};
                }
                if (overflow) {
                    return std::string("readback buffer too small");
                }
                const CaseValue& exp = casesCopy[caseIdx].expected;
                const std::vector<ExpectedElement>& acc = casesCopy[caseIdx].expectedAccept;
                bool matched = exp.width == got.width;
                if (matched && !acc.empty()) {
                    // Per-element acceptance: 'any' passes; otherwise the read-back bits must equal
                    // one of acceptBits, with optional any-NaN acceptance for float results.
                    for (int e = 0; matched && e < got.width; ++e) {
                        const ExpectedElement& ee = acc[static_cast<size_t>(e)];
                        const uint32_t gb = got.elements[static_cast<size_t>(e)].bits;
                        if (ee.any) {
                            continue;
                        }
                        bool ok = false;
                        for (uint32_t ab : ee.acceptBits) {
                            const uint32_t mask =
                                ee.floatWidth == 16 ? 0xFFFFu : 0xFFFFFFFFu;
                            if ((gb & mask) == (ab & mask)) {
                                ok = true;
                                break;
                            }
                        }
                        if (!ok && ee.anyNaN) {
                            // Accept any NaN bit pattern at the result element width.
                            if (ee.floatWidth == 16) {
                                const uint32_t exp16 = (gb >> 10) & 0x1Fu;
                                const uint32_t mant16 = gb & 0x3FFu;
                                ok = exp16 == 0x1Fu && mant16 != 0;
                            } else {
                                const uint32_t exp32 = (gb >> 23) & 0xFFu;
                                const uint32_t mant32 = gb & 0x7FFFFFu;
                                ok = exp32 == 0xFFu && mant32 != 0;
                            }
                        }
                        if (!ok) {
                            matched = false;
                        }
                    }
                } else {
                    for (int e = 0; matched && e < exp.width; ++e) {
                        if (exp.elements[static_cast<size_t>(e)].bits !=
                            got.elements[static_cast<size_t>(e)].bits) {
                            matched = false;
                        }
                    }
                }
                if (!matched) {
                    if (failures < 8) {
                        errs << "\ninput " << formatValue(casesCopy[caseIdx].inputs.empty()
                                                               ? CaseValue(Scalar{})
                                                               : casesCopy[caseIdx].inputs[0])
                             << " returned " << formatValue(got) << " expected "
                             << formatValue(exp);
                    }
                    ++failures;
                }
            }
            if (failures > 0) {
                return "expression mismatch (" + std::to_string(failures) + " failures):" +
                       errs.str();
            }
            return std::nullopt;
        },
        0,
        outputBufferSize);
}

} // namespace

const char* inputSourceName(InputSource source) {
    switch (source) {
        case InputSource::Const:
            return "const";
        case InputSource::Uniform:
            return "uniform";
        case InputSource::StorageR:
            return "storage_r";
        case InputSource::StorageRW:
            return "storage_rw";
    }
    std::abort();
}

InputSource inputSourceFromParam(const std::string& value) {
    if (value == "const") {
        return InputSource::Const;
    }
    if (value == "uniform") {
        return InputSource::Uniform;
    }
    if (value == "storage_r") {
        return InputSource::StorageR;
    }
    if (value == "storage_rw") {
        return InputSource::StorageRW;
    }
    std::abort();
}

ExpressionBuilder builtin(const std::string& name) {
    return [name](const std::vector<std::string>& values) {
        std::ostringstream out;
        out << name << "(";
        for (size_t i = 0; i < values.size(); ++i) {
            if (i > 0) {
                out << ", ";
            }
            out << values[i];
        }
        out << ")";
        return out.str();
    };
}

void run(
    GpuTest& t,
    const ExpressionBuilder& exprBuilder,
    const std::vector<ExprType>& parameterTypes,
    ExprType resultType,
    InputSource inputSource,
    int vectorize,
    const std::vector<Case>& cases) {
    std::vector<ExprType> params = parameterTypes;
    ExprType result = resultType;
    std::vector<Case> work = cases;

    if (vectorize != 0) {
        work = packScalarsToVector(params, result, work, vectorize);
    }

    // Determine cases-per-batch to keep within binding limits.
    WGPULimits limits = t.getLimits();
    size_t casesPerBatch;
    switch (inputSource) {
        case InputSource::Const:
            casesPerBatch = 32;
            break;
        case InputSource::Uniform: {
            const uint32_t stride = structStride(params, inputSource);
            const uint64_t maxUniform = limits.maxUniformBufferBindingSize;
            const uint64_t cap = maxUniform < 2048 ? maxUniform : 2048;
            casesPerBatch = static_cast<size_t>(cap / stride);
            if (casesPerBatch == 0) {
                casesPerBatch = 1;
            }
            break;
        }
        case InputSource::StorageR:
        case InputSource::StorageRW: {
            const uint32_t stride = structStride(params, inputSource);
            casesPerBatch = static_cast<size_t>(limits.maxStorageBufferBindingSize / stride);
            if (casesPerBatch == 0) {
                casesPerBatch = 1;
            }
            break;
        }
        default:
            casesPerBatch = 32;
            break;
    }

    for (size_t i = 0; i < work.size(); i += casesPerBatch) {
        const size_t end = std::min(i + casesPerBatch, work.size());
        std::vector<Case> batch(work.begin() + static_cast<std::ptrdiff_t>(i),
                                work.begin() + static_cast<std::ptrdiff_t>(end));
        submitBatch(t, exprBuilder, params, result, inputSource, batch);
    }
}

} // namespace expression
} // namespace cts
