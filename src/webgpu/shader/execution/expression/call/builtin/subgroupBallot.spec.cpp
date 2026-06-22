// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/builtin/subgroupBallot.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Execution tests for subgroupBallot.
//
// Note: There is a lack of portability for non-uniform execution so these tests
// restrict themselves to uniform control flow or returning early.
// Note: There is no guaranteed mapping between subgroup_invocation_id and
// local_invocation_index. Tests should avoid assuming there is.

#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/shader/execution/expression/call/builtin/subgroup_util.h"
#include "webgpu/texture_format.h"

using namespace cts;
namespace sg = cts::subgroups;

namespace {

WGPUStringView sv(std::string_view text) {
    return WGPUStringView{text.data(), text.size()};
}

uint32_t alignUp(uint32_t n, uint32_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

TestGroup<sg::SubgroupTest> g = MakeTestGroup<sg::SubgroupTest>(
    "shader,execution,expression,call,builtin,subgroupBallot",
    "Execution tests for subgroupBallot");

void skipIfNoSubgroups(sg::SubgroupTest& t) {
    if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_Subgroups)) {
        t.skip("subgroups feature not available");
    }
}

// 128 is the maximum possible subgroup size.
constexpr uint32_t kInvocations = 128;

// Minimal 128-bit unsigned integer (4 x 32-bit limbs, little-endian) supporting
// the operations used by the ballot checks (mirrors the upstream BigInt math).
struct U128 {
    std::array<uint32_t, 4> limbs{0, 0, 0, 0};

    bool operator==(const U128& o) const { return limbs == o.limbs; }
    bool operator!=(const U128& o) const { return !(*this == o); }

    U128 operator&(const U128& o) const {
        U128 r;
        for (int i = 0; i < 4; ++i) {
            r.limbs[i] = limbs[i] & o.limbs[i];
        }
        return r;
    }

    // Shift left by n bits (n in [0, 128]).
    U128 operator<<(uint32_t n) const {
        U128 r;
        if (n >= 128) {
            return r;
        }
        const uint32_t wordShift = n / 32;
        const uint32_t bitShift = n % 32;
        for (int i = 3; i >= 0; --i) {
            uint32_t v = 0;
            const int64_t src = static_cast<int64_t>(i) - wordShift;
            if (src >= 0) {
                v = limbs[src] << bitShift;
                if (bitShift != 0 && src - 1 >= 0) {
                    v |= limbs[src - 1] >> (32 - bitShift);
                }
            }
            r.limbs[i] = v;
        }
        return r;
    }

    // Returns bit `index` (index in [0, 127]).
    uint32_t bit(uint32_t index) const {
        if (index >= 128) {
            return 0;
        }
        return (limbs[index / 32] >> (index % 32)) & 1u;
    }

    std::string toHex() const {
        std::ostringstream s;
        s << std::hex;
        bool started = false;
        for (int i = 3; i >= 0; --i) {
            if (!started) {
                if (limbs[i] == 0 && i != 0) {
                    continue;
                }
                s << limbs[i];
                started = true;
            } else {
                s << std::setw(8) << std::setfill('0') << limbs[i];
            }
        }
        if (!started) {
            s << 0;
        }
        return s.str();
    }
};

// (1 << size) - 1, for size in [0, 128].
U128 getMask(uint32_t size) {
    U128 one;
    one.limbs[0] = 1;
    U128 shifted = one << size;
    // shifted - 1
    U128 r = shifted;
    uint64_t borrow = 1;
    for (int i = 0; i < 4 && borrow; ++i) {
        const uint64_t cur = static_cast<uint64_t>(r.limbs[i]) - borrow;
        r.limbs[i] = static_cast<uint32_t>(cur);
        borrow = (cur >> 63) & 1u; // borrow out if it wrapped
    }
    return r;
}

// Builds a 128-bit value by repeating a 32-bit pattern in every limb.
U128 repeatPattern(uint32_t pattern) {
    U128 r;
    r.limbs = {pattern, pattern, pattern, pattern};
    return r;
}

// A ballot test case: WGSL condition, host-side filter, and expected ballot value.
struct BallotCase {
    std::string name;
    std::string cond;
    std::function<bool(uint32_t id, uint32_t size)> filter;
    std::function<U128(uint32_t size)> expect;
};

const std::vector<BallotCase>& kCases() {
    static const std::vector<BallotCase> cases = {
        {"every_even", "id % 2 == 0",
         [](uint32_t id, uint32_t /*size*/) { return id % 2u == 0u; },
         [](uint32_t size) { return repeatPattern(0x55555555u) & getMask(size); }},
        {"every_odd", "id % 2 == 1",
         [](uint32_t id, uint32_t /*size*/) { return id % 2u == 1u; },
         [](uint32_t size) { return repeatPattern(0xAAAAAAAAu) & getMask(size); }},
        {"lower_half", "id < subgroupSize / 2",
         [](uint32_t id, uint32_t size) { return id < size / 2u; },
         [](uint32_t size) { return getMask(size / 2u); }},
        {"upper_half", "id >= subgroupSize / 2",
         [](uint32_t id, uint32_t size) { return id >= size / 2u; },
         [](uint32_t size) { return getMask(size / 2u) << (size / 2u); }},
        {"first_two", "id == 0 || id == 1",
         [](uint32_t id, uint32_t /*size*/) { return id == 0u || id == 1u; },
         [](uint32_t /*size*/) { return getMask(2); }},
    };
    return cases;
}
const BallotCase& caseByName(const std::string& name) {
    for (const BallotCase& c : kCases()) {
        if (c.name == name) {
            return c;
        }
    }
    return kCases()[0];
}

// kBothCases for predicate_and_control_flow.
const std::vector<BallotCase>& kBothCasesWithPred(std::vector<std::string>& predsOut) {
    // BallotCase reused; the pred string is parallel-stored below.
    static const std::vector<BallotCase> cases = {
        {"empty", "id < subgroupSize / 2",
         [](uint32_t id, uint32_t size) { return id < size / 2u; },
         [](uint32_t /*size*/) { return U128{}; }},
        {"full", "id < 128",
         [](uint32_t /*id*/, uint32_t /*size*/) { return true; },
         [](uint32_t size) { return getMask(size); }},
        {"one_in_four", "id % 2 == 0",
         [](uint32_t id, uint32_t /*size*/) { return id % 2u == 0u; },
         [](uint32_t size) { return repeatPattern(0x11111111u) & getMask(size); }},
        {"middle_half", "id >= subgroupSize / 4",
         [](uint32_t id, uint32_t size) { return id >= size / 4u; },
         [](uint32_t size) { return getMask(size / 2u) << (size / 4u); }},
        {"middle_half_every_other", "(id >= subgroupSize / 4) && (id < 3 * (subgroupSize / 4))",
         [](uint32_t id, uint32_t size) { return id >= size / 4u && id < 3u * (size / 4u); },
         [](uint32_t size) {
             return repeatPattern(0x55555555u) & (getMask(size / 2u) << (size / 4u));
         }},
    };
    static const std::vector<std::string> preds = {
        "id >= subgroupSize / 2",
        "lid < 128",
        "id % 4 == 0",
        "id < 3 * (subgroupSize / 4)",
        "id % 2 == 0",
    };
    predsOut = preds;
    return cases;
}

std::optional<std::string> checkBallots(
    const std::vector<uint32_t>& data,
    uint32_t subgroupSize,
    const std::function<bool(uint32_t id, uint32_t s)>& filter,
    const std::function<U128(uint32_t s)>& expect,
    bool allActive) {
    for (uint32_t i = 0; i < kInvocations; ++i) {
        const uint32_t idx = i * 4;
        U128 actual;
        for (uint32_t j = 0; j < 4; ++j) {
            actual.limbs[j] = data[idx + j];
        }
        U128 expectedResult = expect(subgroupSize);
        const uint32_t subgroupId = i % subgroupSize;
        if (!allActive && !filter(subgroupId, subgroupSize)) {
            expectedResult = U128{};
        }
        if (expectedResult != actual) {
            std::ostringstream msg;
            msg << "Invocation " << i << ", subgroup inv id " << (i % subgroupSize) << ", size "
                << subgroupSize << "\n- expected: " << expectedResult.toHex()
                << "\n-      got: " << actual.toHex();
            return msg.str();
        }
    }

    return std::nullopt;
}

// Bespoke 2-buffer compute dispatch (subgroupBallot.spec.ts runTest).
void runTest(
    sg::SubgroupTest& t,
    const std::string& wgsl,
    const std::function<bool(uint32_t id, uint32_t s)>& filter,
    const std::function<U128(uint32_t s)>& expect,
    bool allActive) {
    const std::vector<uint32_t> sizeInit{0};
    WGPUBuffer sizeBuffer = t.makeBufferWithContents(
        sizeInit.data(), sizeInit.size() * sizeof(uint32_t),
        static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst |
                                     WGPUBufferUsage_Storage));

    const uint32_t outputNumInts = kInvocations * 4;
    const std::vector<uint32_t> outputInit(outputNumInts, 0);
    WGPUBuffer outputBuffer = t.makeBufferWithContents(
        outputInit.data(), outputInit.size() * sizeof(uint32_t),
        static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst |
                                     WGPUBufferUsage_Storage));

    WGPUShaderModule module = t.createShaderModuleTracked(wgsl);
    WGPUComputePipelineDescriptor pipeDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    pipeDesc.layout = nullptr; // auto
    pipeDesc.compute.module = module;
    pipeDesc.compute.entryPoint = sv("main");
    WGPUComputePipeline pipeline = t.createComputePipelineTracked(pipeDesc);

    WGPUBindGroupLayout bgl = wgpuComputePipelineGetBindGroupLayout(pipeline, 0);
    std::array<WGPUBindGroupEntry, 2> entries{};
    const std::array<WGPUBuffer, 2> buffers{sizeBuffer, outputBuffer};
    for (uint32_t i = 0; i < 2; ++i) {
        entries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
        entries[i].binding = i;
        entries[i].buffer = buffers[i];
        entries[i].offset = 0;
        entries[i].size = WGPU_WHOLE_SIZE;
    }
    WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    bgDesc.layout = bgl;
    bgDesc.entryCount = entries.size();
    bgDesc.entries = entries.data();
    WGPUBindGroup bg = t.createBindGroupTracked(bgDesc);
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

    uint32_t subgroupSize = 0;
    t.expectGPUBufferValuesPassCheck(
        sizeBuffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            (void)len;
            std::memcpy(&subgroupSize, actual, sizeof(subgroupSize));
            return std::nullopt;
        },
        0, sizeof(uint32_t));

    std::vector<uint32_t> output(outputNumInts, 0);
    t.expectGPUBufferValuesPassCheck(
        outputBuffer,
        [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
            (void)len;
            std::memcpy(output.data(), actual, outputNumInts * sizeof(uint32_t));
            return std::nullopt;
        },
        0, outputNumInts * sizeof(uint32_t));

    const std::optional<std::string> error =
        checkBallots(output, subgroupSize, filter, expect, allActive);
    if (error.has_value()) {
        t.fail(*error);
    }
}

CTS_TEST(g, "compute,split")
    .desc("Tests ballot in a split subgroup")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (const BallotCase& c : kCases()) {
            cases.emplace_back(c.name);
        }
        return u.combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const BallotCase& testcase = caseByName(t.param<std::string>("case"));
        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage, read_write> size : u32;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<vec4u>;\n\n"
             << "@compute @workgroup_size(" << kInvocations << ")\n"
             << "fn main(@builtin(subgroup_size) subgroupSize : u32,\n"
             << "        @builtin(subgroup_invocation_id) id : u32,\n"
             << "        @builtin(local_invocation_index) lid : u32) {\n"
             << "  if (lid == 0) {\n"
             << "    size = subgroupSize;\n"
             << "  }\n"
             << "  if " << testcase.cond << " {\n"
             << "    output[lid] = subgroupBallot(true);\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        runTest(t, wgsl.str(), testcase.filter, testcase.expect, false);
    });

CTS_TEST(g, "fragment,split").unimplemented();

CTS_TEST(g, "predicate")
    .desc("Tests the predicate parameter")
    .params([](ParamsBuilder u) {
        std::vector<Value> cases;
        for (const BallotCase& c : kCases()) {
            cases.emplace_back(c.name);
        }
        return u.combine("case", cases);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const BallotCase& testcase = caseByName(t.param<std::string>("case"));
        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage, read_write> size : u32;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<vec4u>;\n\n"
             << "@compute @workgroup_size(" << kInvocations << ")\n"
             << "fn main(@builtin(subgroup_size) subgroupSize : u32,\n"
             << "        @builtin(subgroup_invocation_id) id : u32,\n"
             << "        @builtin(local_invocation_index) lid : u32) {\n"
             << "  if (lid == 0) {\n"
             << "    size = subgroupSize;\n"
             << "  }\n"
             << "  let cond = " << testcase.cond << ";\n"
             << "  let b = subgroupBallot(cond);\n"
             << "  output[lid] = b;\n"
             << "}";

        runTest(t, wgsl.str(), testcase.filter, testcase.expect, true);
    });

CTS_TEST(g, "predicate_and_control_flow")
    .desc("Test dynamic predicate and control flow together")
    .params([](ParamsBuilder u) {
        std::vector<std::string> preds;
        const std::vector<BallotCase>& cases = kBothCasesWithPred(preds);
        std::vector<Value> names;
        for (const BallotCase& c : cases) {
            names.emplace_back(c.name);
        }
        return u.combine("case", names);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        std::vector<std::string> preds;
        const std::vector<BallotCase>& cases = kBothCasesWithPred(preds);
        const std::string caseName = t.param<std::string>("case");
        size_t idx = 0;
        for (size_t i = 0; i < cases.size(); ++i) {
            if (cases[i].name == caseName) {
                idx = i;
                break;
            }
        }
        const BallotCase& testcase = cases[idx];
        const std::string pred = preds[idx];

        std::ostringstream wgsl;
        wgsl << "\nenable subgroups;\n\n"
             << "diagnostic(off, subgroup_uniformity);\n\n"
             << "@group(0) @binding(0)\n"
             << "var<storage, read_write> size : u32;\n\n"
             << "@group(0) @binding(1)\n"
             << "var<storage, read_write> output : array<vec4u>;\n\n"
             << "@compute @workgroup_size(" << kInvocations << ")\n"
             << "fn main(@builtin(subgroup_size) subgroupSize : u32,\n"
             << "        @builtin(subgroup_invocation_id) id : u32,\n"
             << "        @builtin(local_invocation_index) lid : u32) {\n"
             << "  if (lid == 0) {\n"
             << "    size = subgroupSize;\n"
             << "  }\n"
             << "  if " << testcase.cond << " {\n"
             << "    output[lid] = subgroupBallot(" << pred << ");\n"
             << "  } else {\n"
             << "    return;\n"
             << "  }\n"
             << "}";

        runTest(t, wgsl.str(), testcase.filter, testcase.expect, false);
    });

// Fragment predicate cases. Filters should always skip the last row and column.
struct FragmentPredicate {
    std::string name;
    std::string cond;
    std::function<bool(uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t id,
                       uint32_t size)>
        filter;
};

const std::vector<FragmentPredicate>& kFragmentPredicates() {
    static const auto skipBorder = [](uint32_t row, uint32_t col, uint32_t width,
                                      uint32_t height) -> bool {
        return row == height - 1 || col == width - 1;
    };
    static const std::vector<FragmentPredicate> preds = {
        {"odd_row", "u32(pos.y) % 2 == 1",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t /*id*/,
            uint32_t /*size*/) { return skipBorder(row, col, width, height) ? false : row % 2 == 1; }},
        {"even_row", "u32(pos.y) % 2 == 0",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t /*id*/,
            uint32_t /*size*/) { return skipBorder(row, col, width, height) ? false : row % 2 == 0; }},
        {"odd_col", "u32(pos.x) % 2 == 1",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t /*id*/,
            uint32_t /*size*/) { return skipBorder(row, col, width, height) ? false : col % 2 == 1; }},
        {"even_col", "u32(pos.x) % 2 == 0",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t /*id*/,
            uint32_t /*size*/) { return skipBorder(row, col, width, height) ? false : col % 2 == 0; }},
        {"odd_id", "id % 2 == 1",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t id,
            uint32_t /*size*/) { return skipBorder(row, col, width, height) ? false : id % 2 == 1; }},
        {"even_id", "id % 2 == 0",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t id,
            uint32_t /*size*/) { return skipBorder(row, col, width, height) ? false : id % 2 == 0; }},
        {"upper_half", "id > subgroupSize / 2",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t id,
            uint32_t size) { return skipBorder(row, col, width, height) ? false : id > size / 2u; }},
        {"lower_half", "id < subgroupSize / 2",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t id,
            uint32_t size) { return skipBorder(row, col, width, height) ? false : id < size / 2u; }},
        {"first_two_or_diagonal", "id == 0 || id == 1 || u32(pos.x) == u32(pos.y)",
         [](uint32_t row, uint32_t col, uint32_t width, uint32_t height, uint32_t id,
            uint32_t /*size*/) {
             return skipBorder(row, col, width, height) ? false
                                                        : (id == 0 || id == 1 || row == col);
         }},
    };
    return preds;
}
const FragmentPredicate& fragmentPredicateByName(const std::string& name) {
    for (const FragmentPredicate& p : kFragmentPredicates()) {
        if (p.name == name) {
            return p;
        }
    }
    return kFragmentPredicates()[0];
}

// Checks the result of subgroupBallot in fragment shaders.
std::optional<std::string> checkFragmentBallots(
    const std::vector<uint32_t>& ballots,
    const std::vector<uint32_t>& metadata,
    WGPUTextureFormat format,
    uint32_t width,
    uint32_t height,
    const FragmentPredicate& fp) {
    if (width < 3 || height < 3) {
        std::ostringstream msg;
        msg << "Insufficient framebuffer size [" << width << "w x " << height
            << "h]. Minimum is [3w x 3h].";
        return msg.str();
    }

    const sg::UintsPerFramebuffer dims = sg::getUintsPerFramebuffer(format, width, height);
    const uint32_t uintsPerRow = dims.uintsPerRow;
    const uint32_t uintsPerTexel = dims.uintsPerTexel;

    std::map<uint32_t, U128> mapping;

    // Iteration skips last row and column to avoid helper invocations because it is not
    // guaranteed whether or not they participate in the subgroup operation.
    for (uint32_t row = 0; row < height - 1; ++row) {
        for (uint32_t col = 0; col < width - 1; ++col) {
            const size_t offset = static_cast<size_t>(uintsPerRow) * row + col * uintsPerTexel;

            const uint32_t id = metadata[offset];
            const uint32_t subgroupSize = metadata[offset + 1];
            const uint32_t subgroupId = metadata[offset + 2];

            U128 ballot;
            ballot.limbs[0] = ballots[offset];
            ballot.limbs[1] = ballots[offset + 1];
            ballot.limbs[2] = ballots[offset + 2];
            ballot.limbs[3] = ballots[offset + 3];

            const uint32_t expectBit =
                fp.filter(row, col, width, height, id, subgroupSize) ? 1u : 0u;
            const uint32_t gotBit = ballot.bit(id);

            if (expectBit != gotBit) {
                std::ostringstream msg;
                msg << "Row " << row << ", col " << col << ": incorrect ballot bit " << id
                    << ":\n- expected: " << expectBit << "\n-      got: " << gotBit;
                return msg.str();
            }

            auto it = mapping.find(subgroupId);
            if (it == mapping.end()) {
                mapping[subgroupId] = ballot;
            } else {
                if (it->second != ballot) {
                    std::ostringstream msg;
                    msg << "Row " << row << " col " << col << ": ballot mismatch:\n- expected: "
                        << it->second.toHex() << "\n-      got: " << ballot.toHex();
                    return msg.str();
                }
            }
        }
    }

    return std::nullopt;
}

CTS_TEST(g, "fragment")
    .desc("Tests subgroupBallot in fragment shaders")
    .params([](ParamsBuilder u) {
        std::vector<Value> predicates;
        for (const FragmentPredicate& p : kFragmentPredicates()) {
            predicates.emplace_back(p.name);
        }
        std::vector<Value> sizes;
        for (const sg::FramebufferSize& s : sg::kFramebufferSizes()) {
            sizes.emplace_back(sg::framebufferSizeToString(s));
        }
        std::vector<ParamRecord> formats = {{{"format", Value(std::string("rgba32uint"))}}};
        return u.combine("predicate", predicates)
            .beginSubcases()
            .combine("size", sizes)
            .combineWithParams(formats);
    })
    .fn([](sg::SubgroupTest& t) {
        skipIfNoSubgroups(t);
        const sg::FramebufferSize size = sg::framebufferSizeFromString(t.param<std::string>("size"));
        const uint32_t width = size[0];
        const uint32_t height = size[1];
        const WGPUTextureFormat format = WGPUTextureFormat_RGBA32Uint;
        const FragmentPredicate& testcase =
            fragmentPredicateByName(t.param<std::string>("predicate"));

        std::ostringstream fsShader;
        fsShader << "\nenable subgroups;\n\n"
                 << "struct FSOutput {\n"
                 << "  @location(0) ballot : vec4u,\n"
                 << "  @location(1) metadata : vec4u,\n"
                 << "}\n\n"
                 << "@fragment\n"
                 << "fn main(\n"
                 << "  @builtin(position) pos : vec4f,\n"
                 << "  @builtin(subgroup_size) subgroupSize : u32,\n"
                 << "  @builtin(subgroup_invocation_id) id : u32,\n"
                 << ") -> FSOutput {\n"
                 << "  let linear = u32(pos.x) + u32(pos.y) * " << width << ";\n"
                 << "  let subgroup_id = subgroupBroadcastFirst(linear + 1);\n\n"
                 << "  // Filter out possible helper invocations.\n"
                 << "  let x_in_range = u32(pos.x) < (" << width << " - 1);\n"
                 << "  let y_in_range = u32(pos.y) < (" << height << " - 1);\n"
                 << "  let in_range = x_in_range && y_in_range;\n\n"
                 << "  let cond = " << testcase.cond << ";\n"
                 << "  let ballot = subgroupBallot(in_range && cond);\n\n"
                 << "  var out : FSOutput;\n"
                 << "  out.ballot = ballot;\n"
                 << "  out.metadata = vec4u(id, subgroupSize, subgroup_id, 0);\n"
                 << "  return out;\n"
                 << "}";

        const std::string vsShader =
            "\n@vertex\n"
            "fn vsMain(@builtin(vertex_index) index : u32) -> @builtin(position) vec4f {\n"
            "  const vertices = array(\n"
            "    vec2(-2, 4), vec2(-2, -4), vec2(2, 0),\n"
            "  );\n"
            "  return vec4f(vec2f(vertices[index]), 0, 1);\n"
            "}";

        WGPUShaderModule vsModule = t.createShaderModuleTracked(vsShader);
        WGPUShaderModule fsModule = t.createShaderModuleTracked(fsShader.str());

        std::array<WGPUColorTargetState, 2> targets{};
        for (uint32_t i = 0; i < 2; ++i) {
            targets[i] = WGPU_COLOR_TARGET_STATE_INIT;
            targets[i].format = format;
        }
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module = fsModule;
        fragment.entryPoint = sv("main");
        fragment.targetCount = targets.size();
        fragment.targets = targets.data();
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

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = WGPUExtent3D{width, height, 1};
        texDesc.format = format;
        texDesc.usage = static_cast<WGPUTextureUsage>(
            WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst |
            WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding);
        WGPUTexture ballotFB = t.createTextureTracked(texDesc);
        WGPUTexture metadataFB = t.createTextureTracked(texDesc);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        WGPUTextureView ballotView = t.createViewTracked(ballotFB, viewDesc);
        WGPUTextureView metadataView = t.createViewTracked(metadataFB, viewDesc);
        std::array<WGPURenderPassColorAttachment, 2> colorAtts{};
        const std::array<WGPUTextureView, 2> views{ballotView, metadataView};
        for (uint32_t i = 0; i < 2; ++i) {
            colorAtts[i] = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            colorAtts[i].view = views[i];
            colorAtts[i].loadOp = WGPULoadOp_Clear;
            colorAtts[i].storeOp = WGPUStoreOp_Store;
            colorAtts[i].clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
        }
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = colorAtts.size();
        passDesc.colorAttachments = colorAtts.data();
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderSetPipeline(pass, pipeline);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
        wgpuRenderPassEncoderEnd(pass);

        WGPUBufferDescriptor obDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        obDesc.size = byteLength;
        obDesc.usage =
            static_cast<WGPUBufferUsage>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc);
        WGPUBuffer ballotBuffer = t.createBufferTracked(obDesc);
        WGPUBuffer metadataBuffer = t.createBufferTracked(obDesc);
        t.copyTextureToBuffer(encoder, ballotFB, ballotBuffer, bytesPerRow,
                              WGPUExtent3D{width, height, 1});
        t.copyTextureToBuffer(encoder, metadataFB, metadataBuffer, bytesPerRow,
                              WGPUExtent3D{width, height, 1});
        WGPUCommandBuffer commands = t.finishTracked(encoder);
        wgpuQueueSubmit(t.queue(), 1, &commands);

        std::vector<uint32_t> ballots(uintLength, 0);
        t.expectGPUBufferValuesPassCheck(
            ballotBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                (void)len;
                std::memcpy(ballots.data(), actual, uintLength * sizeof(uint32_t));
                return std::nullopt;
            },
            0, uintLength * sizeof(uint32_t));

        std::vector<uint32_t> metadata(uintLength, 0);
        t.expectGPUBufferValuesPassCheck(
            metadataBuffer,
            [&](const uint8_t* actual, size_t len) -> std::optional<std::string> {
                (void)len;
                std::memcpy(metadata.data(), actual, uintLength * sizeof(uint32_t));
                return std::nullopt;
            },
            0, uintLength * sizeof(uint32_t));

        const std::optional<std::string> error =
            checkFragmentBallots(ballots, metadata, format, width, height, testcase);
        if (error.has_value()) {
            t.fail(*error);
        }
    });

} // namespace
