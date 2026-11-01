// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/expression/call/user/ptr_aliasing.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Port notes:
// - Upstream's Record tables become ordered vectors, so keysOf() order and query identities match.
// - No backend's webgpu.h has an enumerator for 'unrestricted_aliasing' (F-159), so every case that
//   requires it skips, as upstream does where the feature is not reported.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/language_features.h"
#include "webgpu/shader/execution/expression/call/builtin/atomics/harness.h"
#include "webgpu/shader/execution/expression/call/mixed_type_aliasing_utils.h"

using namespace cts;
using namespace cts::mixed_type;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> testGroup = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "shader,execution,expression,call,user,ptr_aliasing",
    R"(
Execution tests for aliased pointer parameters.

With the 'unrestricted_aliasing' language feature, pointer arguments passed to a user-declared
function may alias each other (or a module-scope variable accessed by the callee), even when one of
the accesses is a write. These tests check that implementations honour the WGSL memory semantics in
that case, i.e. that no code generation or downstream compiler step assumes that pointer parameters
are non-aliasing.

Every test is run with 'aliased' set to true and false:
 * aliased=false: The pointers refer to distinct memory locations with distinct root identifiers.
   These cases are valid without the 'unrestricted_aliasing' language feature and act as a control
   for the expectations.
 * aliased=true: The pointers refer to overlapping memory locations. These cases require the
   'unrestricted_aliasing' language feature.

Cases that form pointers using runtime indices into the same root variable always require the
'unrestricted_aliasing' language feature, as they cannot be statically proven not to alias.
)");

std::vector<Value> addressSpaces() {
    return {"function", "private", "workgroup", "storage"};
}
std::vector<Value> moduleScopeAddressSpaces() {
    return {"private", "workgroup", "storage"};
}
std::vector<Value> atomicAddressSpaces() {
    return {Value("workgroup"), Value("storage")};
}
const int kAtomicDataSize = 6; // Num i32s in AtomicData

/**
 * WGSL type declarations shared by all shaders.
 *
 * Data is made entirely of i32 so it has no padding, and its memory layout in i32 units is:
 *   x: 0, y: 1, arr: 2..5, s: 6..9, big: 10..41
 */
const char* const kTypeDecls = R"(
struct S {
  a : i32,
  b : i32,
  c : i32,
  d : i32,
}

struct Data {
  x : i32,
  y : i32,
  arr : array<i32, 4>,
  s : S,
  big : array<i32, 32>,
}

// Structurally similar to Data(x, y, arr).
// Used in two_atomic_pointers tests.
struct AtomicData {
  x : atomic<i32>,
  y : atomic<i32>,
  arr : array<atomic<i32>, 4>,
}

struct In {
  a : Data,
  b : Data,
  p : array<i32, 8>,
}

struct Out {
  r : array<i32, 4>,
  a : Data,
  b : Data,
}
)";

/** The number of i32s in 'Data'. */
const int kDataSize = 42;
/** The offset of 'Data.big' in i32 units. */
const int kBig = 10;
/** The number of elements in 'Data.big'. */
const int kBigSize = 32;
/** The offset of variable 'B' in the simulated memory. Variable 'A' is at offset 0. */
const int kB = kDataSize;

/** The initial contents of variable 'A', then those of variable 'B'. */
std::vector<int32_t> initialMemory() {
    std::vector<int32_t> mem;
    for (int i = 0; i < kDataSize; ++i) {
        mem.push_back(10 + i);
    }
    for (int i = 0; i < kDataSize; ++i) {
        mem.push_back(100 + i);
    }
    return mem;
}

/**
 * The runtime parameters passed to the shader in 'input.p'.
 * Shaders use these to prevent the compiler from constant folding the tests away.
 *  p[0]: 'va' - a value to write.
 *  p[1]: 'vb' - another value to write.
 *  p[2]: 'n'  - a loop count.
 *  p[3]: 'idx' - a runtime index.
 *  p[4]: 'm'  - a long loop count, used to iterate over 'Data.big'. Must be less than kBigSize.
 *  p[5..7]: unused.
 */
const std::array<int32_t, 8> kParams = {1000, 2000, 4, 1, kBigSize - 1, 0, 0, 0};
const int kVA = 1000;
const int kVB = 2000;
const int kN = 4;
const int kIdx = 1;
const int kM = kBigSize - 1;

/** A simulation of the memory of variables 'A' and 'B', and the output results. */
struct Memory {
    std::vector<int32_t> mem = initialMemory();
    // Storage for the result of simulated expectations.
    std::array<int32_t, 4> r = {0, 0, 0, 0};
    int32_t ld(int ptr) const {
        return mem[static_cast<size_t>(ptr)];
    }
    void st(int ptr, int64_t value) {
        mem[static_cast<size_t>(ptr)] = static_cast<int32_t>(value);
    }
    /** @returns the expected contents of the 'Out' buffer. */
    std::vector<int32_t> expected() const {
        std::vector<int32_t> out(r.begin(), r.end());
        out.insert(out.end(), mem.begin(), mem.end());
        return out;
    }
};

/** @returns the WGSL pointer type for a pointer to 'type' in the given address space. */
std::string ptrType(const std::string& space, const std::string& type) {
    if (space == "storage") {
        return "ptr<storage, " + type + ", read_write>";
    }
    return "ptr<" + space + ", " + type + ">";
}

struct ShaderParams {
    /** The address space of variables 'A' and 'B'. */
    std::string space;
    /** Whether the shader requires the 'unrestricted_aliasing' language feature. */
    bool requiresAliasing = false;
    /** Module-scope helper functions. */
    std::string helpers;
    /** The body of the entry point. Runs after 'A' and 'B' are initialized. */
    std::string body;
    /** The expected contents of the output buffer. */
    std::vector<int32_t> expected;
    /**
     * If true, then 'A' and 'B' are of type 'AtomicData', and 'body' is responsible for initializing
     * them and for writing them to 'output'.
     */
    bool atomic = false;
    /**
     * If true, then 'A' and 'B' are untyped 'buffer<N>' variables with the same size as 'Data'.
     * They are initialized from, and written to, 'output' through 'bufferView<Data>'.
     * Requires the 'buffer_view' language feature, and 'space' must be 'workgroup' or 'storage'.
     */
    bool bufferView = false;
};

/**
 * Builds and runs a compute shader that declares two variables 'A' and 'B' in the given address
 * space, initializes them from 'input', runs 'body', then writes 'A' and 'B' to 'output'.
 */
void run(GpuTest& t, const ShaderParams& params) {
    if (!hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters)) {
        t.skip("Missing unrestricted_pointer_parameters language feature");
    }
    if (params.requiresAliasing && !hasUnrestrictedAliasingLanguageFeature()) {
        t.skip("Missing unrestricted_aliasing language feature");
    }
    if (params.bufferView && !hasBufferViewLanguageFeature()) {
        t.skip("Missing buffer_view language feature");
    }

    const std::string varType = params.atomic       ? "AtomicData"
                                : params.bufferView ? "buffer<" + std::to_string(kDataSize * 4) + ">"
                                                    : "Data";
    std::string moduleVars;
    std::string functionVars;
    if (params.space == "function") {
        functionVars = "var A : " + varType + ";\n  var B : " + varType + ";";
    } else if (params.space == "private" || params.space == "workgroup") {
        moduleVars = "var<" + params.space + "> A : " + varType + ";\nvar<" + params.space + "> B : " + varType + ";";
    } else {
        moduleVars = "\n@group(0) @binding(2) var<storage, read_write> A : " + varType +
                     ";\n@group(0) @binding(3) var<storage, read_write> B : " + varType + ";";
    }

    std::string init = "A = input.a;\n  B = input.b;";
    std::string dump = "output.a = A;\n  output.b = B;";
    if (params.atomic) {
        init = "";
        dump = "";
    } else if (params.bufferView) {
        init = "*bufferView<Data>(&A, 0u) = input.a;\n  *bufferView<Data>(&B, 0u) = input.b;";
        dump = "output.a = *bufferView<Data>(&A, 0u);\n  output.b = *bufferView<Data>(&B, 0u);";
    }

    const std::string code = std::string("\n") + (params.requiresAliasing ? "// requires unrestricted_aliasing;" : "") +
                             "\n\n" + kTypeDecls + R"(

@group(0) @binding(0) var<storage, read> input : In;
@group(0) @binding(1) var<storage, read_write> output : Out;

)" + moduleVars + "\n\n" + params.helpers + R"(

@compute @workgroup_size(1)
fn main() {
  )" + functionVars + R"(
  )" + init + R"(
  )" + params.body + R"(
  )" + dump + R"(
}
)";

    WGPUComputePipeline pipeline = atomics::createComputePipelineAuto(t, code);

    std::vector<int32_t> inputData = initialMemory();
    inputData.insert(inputData.end(), kParams.begin(), kParams.end());
    const uint64_t inputSize = inputData.size() * sizeof(int32_t);
    WGPUBuffer inputBuffer =
        t.makeBufferWithContents(inputData.data(), static_cast<size_t>(inputSize), WGPUBufferUsage_Storage);
    const uint64_t outputSize = params.expected.size() * sizeof(int32_t);
    WGPUBufferDescriptor outputDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    outputDesc.size = outputSize;
    outputDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc;
    WGPUBuffer outputBuffer = t.createBufferTracked(outputDesc);

    std::vector<atomics::BufferBinding> entries = {{inputBuffer, inputSize}, {outputBuffer, outputSize}};
    if (params.space == "storage") {
        for (int binding = 2; binding <= 3; ++binding) {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = static_cast<uint64_t>(kDataSize) * 4;
            desc.usage = WGPUBufferUsage_Storage;
            entries.push_back({t.createBufferTracked(desc), desc.size});
        }
    }

    WGPUBindGroup bindGroup = atomics::makeAutoBindGroup(t, pipeline, entries);
    atomics::runComputePassX(t, pipeline, bindGroup, 1);

    t.expectGPUBufferValuesEqual(outputBuffer, params.expected.data(), static_cast<size_t>(outputSize));
}

/** A memory location within 'Data' or 'AtomicData' that pointers can be formed to. */
struct Target {
    std::string name;
    std::string wgsl;
    int offset = 0;
};

const Target& findTarget(const std::vector<Target>& targets, const std::string& name) {
    for (const Target& target : targets) {
        if (target.name == name) {
            return target;
        }
    }
    std::abort();
}

std::vector<Value> targetNames(const std::vector<Target>& targets) {
    std::vector<Value> names;
    for (const Target& target : targets) {
        names.emplace_back(target.name);
    }
    return names;
}

/** Scalar i32 memory locations within 'Data' that pointers can be formed to. */
const std::vector<Target>& scalarTargets() {
    static const std::vector<Target> targets = {
        {"scalar", "x", 0},
        {"array_element", "arr[1]", 3},
        {"struct_member", "s.b", 7},
    };
    return targets;
}

template <class Op>
const Op& findOp(const std::vector<Op>& ops, const std::string& name) {
    for (const Op& op : ops) {
        if (op.name == name) {
            return op;
        }
    }
    std::abort();
}

template <class Op>
std::vector<Value> opNames(const std::vector<Op>& ops) {
    std::vector<Value> names;
    for (const Op& op : ops) {
        names.emplace_back(op.name);
    }
    return names;
}

struct TwoPointerOp {
    std::string name;
    /** The body of 'fn f(pa : ptr<i32>, pb : ptr<i32>, va : i32, vb : i32, n : i32) -> i32'. */
    std::string wgsl;
    /** Simulates 'f'. */
    int64_t (*sim)(Memory& m, int pa, int pb);
};

const std::vector<TwoPointerOp>& twoPointerOps() {
    static const std::vector<TwoPointerOp> ops = {
        {"store_store_load",
         R"(
  *pa = va;
  *pb = vb;
  return *pa;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, kVA);
             m.st(pb, kVB);
             return m.ld(pa);
         }},
        {"store_load",
         R"(
  *pa = va;
  return *pb;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, kVA);
             return m.ld(pb);
         }},
        {"load_store_load",
         R"(
  let before = *pa;
  *pb = vb;
  return *pa - before;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             const int64_t before = m.ld(pa);
             m.st(pb, kVB);
             return m.ld(pa) - before;
         }},
        {"compound_assign",
         R"(
  *pa += *pb;
  *pa += *pb;
  return *pa;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, int64_t{m.ld(pa)} + m.ld(pb));
             m.st(pa, int64_t{m.ld(pa)} + m.ld(pb));
             return m.ld(pa);
         }},
        {"increment",
         R"(
  (*pa)++;
  (*pb)++;
  return *pa;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, int64_t{m.ld(pa)} + 1);
             m.st(pb, int64_t{m.ld(pb)} + 1);
             return m.ld(pa);
         }},
        {"swap",
         R"(
  let tmp = *pa;
  *pa = *pb;
  *pb = tmp;
  return *pa;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             const int32_t tmp = m.ld(pa);
             m.st(pa, m.ld(pb));
             m.st(pb, tmp);
             return m.ld(pa);
         }},
        // The load of '*pb' must not be hoisted out of the loop.
        {"loop_accumulate",
         R"(
  for (var i = 0; i < n; i++) {
    *pa += *pb;
  }
  return *pa;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             for (int i = 0; i < kN; ++i) {
                 m.st(pa, int64_t{m.ld(pa)} + m.ld(pb));
             }
             return m.ld(pa);
         }},
        // The load of '*pb' must not be hoisted out of the loop.
        {"loop_store",
         R"(
  var sum = 0;
  for (var i = 0; i < n; i++) {
    *pa = i;
    sum += *pb;
  }
  return sum;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             int64_t sum = 0;
             for (int i = 0; i < kN; ++i) {
                 m.st(pa, i);
                 sum += m.ld(pb);
             }
             return sum;
         }},
        {"nested_calls",
         R"(
  *pa = va;
  store_i32(pb, vb);
  return load_i32(pa);)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, kVA);
             m.st(pb, kVB);
             return m.ld(pa);
         }},
        // Pass the pointers through to another function in the opposite order.
        {"nested_calls_swapped",
         R"(
  return store_store_load(pb, pa, vb, va);)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pb, kVB);
             m.st(pa, kVA);
             return m.ld(pb);
         }},
        // The first store to '*pa' is only dead if '*pb' does not alias '*pa'.
        {"dead_store",
         R"(
  *pa = va;
  let tmp = *pb;
  *pa = vb;
  return tmp;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, kVA);
             const int32_t tmp = m.ld(pb);
             m.st(pa, kVB);
             return tmp;
         }},
        // As 'dead_store', but the intervening read happens in another function.
        {"dead_store_nested_call",
         R"(
  *pa = va;
  let tmp = load_i32(pb);
  *pa = vb;
  return tmp;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, kVA);
             const int32_t tmp = m.ld(pb);
             m.st(pa, kVB);
             return tmp;
         }},
        // Each store to '*pa' is overwritten by the next iteration, so only the last is live if '*pb'
        // does not alias '*pa'.
        {"dead_store_loop",
         R"(
  var sum = 0;
  for (var i = 0; i < n; i++) {
    *pa = va + i;
    sum += *pb;
    *pa = vb;
  }
  return sum;)",
         [](Memory& m, int pa, int pb) -> int64_t {
             int64_t sum = 0;
             for (int i = 0; i < kN; ++i) {
                 m.st(pa, kVA + i);
                 sum += m.ld(pb);
                 m.st(pa, kVB);
             }
             return sum;
         }},
    };
    return ops;
}

/** The helpers of the two_pointers tests, around the function 'f' with the body 'opWgsl'. */
std::string twoPointerHelpers(const std::string& pi32, const std::string& opWgsl) {
    return R"(
fn store_i32(p : )" + pi32 + R"(, v : i32) {
  *p = v;
}

fn load_i32(p : )" + pi32 + R"() -> i32 {
  return *p;
}

fn store_store_load(pa : )" + pi32 + ", pb : " + pi32 + R"(, va : i32, vb : i32) -> i32 {
  *pa = va;
  *pb = vb;
  return *pa;
}

fn f(pa : )" + pi32 + ", pb : " + pi32 + R"(, va : i32, vb : i32, n : i32) -> i32 {
  )" + opWgsl + R"(
})";
}

CTS_TEST(testGroup, "two_pointers")
    .desc(R"(Test that a function with two i32 pointer parameters behaves correctly when both pointers refer
to the same memory location.)")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", addressSpaces())
            .combine("target", targetNames(scalarTargets()))
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", opNames(twoPointerOps()));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string space = t.param<std::string>("address_space");
        const Target& target = findTarget(scalarTargets(), t.param<std::string>("target"));
        const bool aliased = t.param<bool>("aliased");
        const TwoPointerOp& op = findOp(twoPointerOps(), t.param<std::string>("op"));

        const int pa = 0 + target.offset;
        const int pb = (aliased ? 0 : kB) + target.offset;
        Memory m;
        m.r[0] = static_cast<int32_t>(op.sim(m, pa, pb));

        ShaderParams params;
        params.space = space;
        params.requiresAliasing = aliased;
        params.helpers = twoPointerHelpers(ptrType(space, "i32"), op.wgsl);
        params.body = "\n  output.r[0] = f(&A." + target.wgsl + ", &" + (aliased ? "A" : "B") + "." + target.wgsl +
                      ",\n                  input.p[0], input.p[1], input.p[2]);";
        params.expected = m.expected();
        run(t, params);
    });

CTS_TEST(testGroup, "two_pointers_dynamic_index")
    .desc(R"(Test that a function with two i32 pointer parameters behaves correctly when the pointers are
formed from runtime indices into the same array, and those indices may or may not be equal.

These cases always require the 'unrestricted_aliasing' language feature.)")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", addressSpaces())
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", opNames(twoPointerOps()));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string space = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");
        const TwoPointerOp& op = findOp(twoPointerOps(), t.param<std::string>("op"));

        const int kArr = 2;
        const int pa = kArr + kIdx;
        const int pb = kArr + kIdx + (aliased ? 0 : 1);
        Memory m;
        m.r[0] = static_cast<int32_t>(op.sim(m, pa, pb));

        ShaderParams params;
        params.space = space;
        params.requiresAliasing = true;
        params.helpers = twoPointerHelpers(ptrType(space, "i32"), op.wgsl);
        params.body = std::string("\n  let i = input.p[3];\n  let j = input.p[3] + ") + (aliased ? "0" : "1") +
                      ";\n  output.r[0] = f(&A.arr[i], &A.arr[j], input.p[0], input.p[1], input.p[2]);";
        params.expected = m.expected();
        run(t, params);
    });

/** A composite type within 'Data' that pointers can be formed to. */
struct Composite {
    std::string name;
    std::string type;
    std::string member;
    int offset = 0;
    bool isArray = false;

    std::string access(int i) const {
        if (isArray) {
            return "[" + std::to_string(i) + "]";
        }
        return std::string(".") + "abcd"[i];
    }
    std::string ctor(const std::string& args) const {
        return (isArray ? "array<i32, 4>(" : "S(") + args + ")";
    }
};

const std::vector<Composite>& composites() {
    static const std::vector<Composite> values = {
        {"array", "array<i32, 4>", "arr", 2, true},
        {"struct", "S", "s", 6, false},
    };
    return values;
}

struct CompositeAndElementOp {
    std::string name;
    /**
     * The body of 'fn f(pc : ptr<C>, pe : ptr<i32>, va : i32, vb : i32, idx : i32) -> i32', where
     * 'pc' points to a composite and 'pe' points to its element 1 (or the equivalent element of a
     * different variable).
     */
    std::string (*wgsl)(const Composite& c);
    /** Simulates 'f'. */
    int64_t (*sim)(Memory& m, int pc, int pe);
};

const std::vector<CompositeAndElementOp>& compositeAndElementOps() {
    static const std::vector<CompositeAndElementOp> ops = {
        {"element_then_whole",
         [](const Composite& c) {
             return "\n  *pe = va;\n  *pc = " + c.ctor("vb, vb + 1, vb + 2, vb + 3") + ";\n  return *pe;";
         },
         [](Memory& m, int pc, int pe) -> int64_t {
             m.st(pe, kVA);
             for (int i = 0; i < 4; ++i) {
                 m.st(pc + i, kVB + i);
             }
             return m.ld(pe);
         }},
        {"whole_then_element",
         [](const Composite& c) {
             return "\n  *pc = " + c.ctor("vb, vb + 1, vb + 2, vb + 3") + ";\n  *pe = va;\n  return (*pc)" +
                    c.access(1) + ";";
         },
         [](Memory& m, int pc, int pe) -> int64_t {
             for (int i = 0; i < 4; ++i) {
                 m.st(pc + i, kVB + i);
             }
             m.st(pe, kVA);
             return m.ld(pc + 1);
         }},
        {"element_then_load_whole",
         [](const Composite& c) { return "\n  *pe = va;\n  let v = *pc;\n  return v" + c.access(1) + ";"; },
         [](Memory& m, int pc, int pe) -> int64_t {
             m.st(pe, kVA);
             return m.ld(pc + 1);
         }},
        {"sub_element_write",
         [](const Composite& c) {
             return "\n  (*pc)" + (c.isArray ? std::string("[idx]") : c.access(1)) + " = va;\n  return *pe;";
         },
         [](Memory& m, int pc, int pe) -> int64_t {
             m.st(pc + kIdx, kVA);
             return m.ld(pe);
         }},
        // The copy 'v' must be taken before the store to '*pe'.
        {"load_whole_modify_store_whole",
         [](const Composite& c) {
             return "\n  var v = *pc;\n  *pe = va;\n  v" + c.access(0) + " = v" + c.access(1) +
                    ";\n  *pc = v;\n  return *pe;";
         },
         [](Memory& m, int pc, int pe) -> int64_t {
             std::array<int32_t, 4> v = {m.ld(pc), m.ld(pc + 1), m.ld(pc + 2), m.ld(pc + 3)};
             m.st(pe, kVA);
             v[0] = v[1];
             for (int i = 0; i < 4; ++i) {
                 m.st(pc + i, v[static_cast<size_t>(i)]);
             }
             return m.ld(pe);
         }},
    };
    return ops;
}

CTS_TEST(testGroup, "composite_and_element")
    .desc(R"(Test that a function taking a pointer to a composite and a pointer to an i32 behaves correctly
when the i32 pointer points to an element of the composite.)")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", addressSpaces())
            .combine("composite", opNames(composites()))
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", opNames(compositeAndElementOps()));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string space = t.param<std::string>("address_space");
        const Composite& c = findOp(composites(), t.param<std::string>("composite"));
        const bool aliased = t.param<bool>("aliased");
        const CompositeAndElementOp& op = findOp(compositeAndElementOps(), t.param<std::string>("op"));

        const int pc = c.offset;
        const int pe = (aliased ? 0 : kB) + c.offset + 1;
        Memory m;
        m.r[0] = static_cast<int32_t>(op.sim(m, pc, pe));

        ShaderParams params;
        params.space = space;
        params.requiresAliasing = aliased;
        params.helpers = "\nfn f(pc : " + ptrType(space, c.type) + ", pe : " + ptrType(space, "i32") +
                         ", va : i32, vb : i32, idx : i32) -> i32 {\n  " + op.wgsl(c) + "\n}";
        params.body = "\n  output.r[0] = f(&A." + c.member + ", &" + (aliased ? "A" : "B") + "." + c.member +
                      c.access(1) + ",\n                  input.p[0], input.p[1], input.p[3]);";
        params.expected = m.expected();
        run(t, params);
    });

struct CompositeCopyOp {
    std::string name;
    /** The body of 'fn f(pd : ptr<C>, ps : ptr<C>) -> i32'. */
    std::string (*wgsl)(const Composite& c);
    /** Simulates 'f'. */
    int64_t (*sim)(Memory& m, int pd, int ps);
};

const std::vector<CompositeCopyOp>& compositeCopyOps() {
    static const std::vector<CompositeCopyOp> ops = {
        // All loads of '*ps' happen before the store to '*pd'.
        {"reverse_construct",
         [](const Composite& c) {
             std::vector<std::string> args;
             for (int i = 3; i >= 0; --i) {
                 args.push_back("(*ps)" + c.access(i));
             }
             return "\n  *pd = " + c.ctor(joinStrings(args, ", ")) + ";\n  return (*pd)" + c.access(0) + ";";
         },
         [](Memory& m, int pd, int ps) -> int64_t {
             const std::array<int32_t, 4> v = {m.ld(ps + 3), m.ld(ps + 2), m.ld(ps + 1), m.ld(ps)};
             for (int i = 0; i < 4; ++i) {
                 m.st(pd + i, v[static_cast<size_t>(i)]);
             }
             return m.ld(pd);
         }},
        // Loads and stores are interleaved.
        {"memberwise_reverse",
         [](const Composite& c) {
             std::string out;
             for (int i = 0; i < 4; ++i) {
                 out += "\n  (*pd)" + c.access(i) + " = (*ps)" + c.access(3 - i) + ";";
             }
             return out + "\n  return (*pd)" + c.access(0) + ";";
         },
         [](Memory& m, int pd, int ps) -> int64_t {
             for (int i = 0; i < 4; ++i) {
                 m.st(pd + i, m.ld(ps + 3 - i));
             }
             return m.ld(pd);
         }},
        {"copy_then_modify",
         [](const Composite& c) {
             return "\n  *pd = *ps;\n  (*pd)" + c.access(0) + " += 1;\n  return (*ps)" + c.access(0) + ";";
         },
         [](Memory& m, int pd, int ps) -> int64_t {
             for (int i = 0; i < 4; ++i) {
                 m.st(pd + i, m.ld(ps + i));
             }
             m.st(pd, int64_t{m.ld(pd)} + 1);
             return m.ld(ps);
         }},
    };
    return ops;
}

CTS_TEST(testGroup, "composite_copy")
    .desc(R"(Test that a function that copies between two composite pointers behaves correctly when both
pointers refer to the same composite.)")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", addressSpaces())
            .combine("composite", opNames(composites()))
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", opNames(compositeCopyOps()));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string space = t.param<std::string>("address_space");
        const Composite& c = findOp(composites(), t.param<std::string>("composite"));
        const bool aliased = t.param<bool>("aliased");
        const CompositeCopyOp& op = findOp(compositeCopyOps(), t.param<std::string>("op"));
        const std::string pc = ptrType(space, c.type);

        const int pd = c.offset;
        const int ps = (aliased ? 0 : kB) + c.offset;
        Memory m;
        m.r[0] = static_cast<int32_t>(op.sim(m, pd, ps));

        ShaderParams params;
        params.space = space;
        params.requiresAliasing = aliased;
        params.helpers = "\nfn f(pd : " + pc + ", ps : " + pc + ") -> i32 {\n  " + op.wgsl(c) + "\n}";
        params.body = "\n  output.r[0] = f(&A." + c.member + ", &" + (aliased ? "A" : "B") + "." + c.member + ");";
        params.expected = m.expected();
        run(t, params);
    });

struct ModuleScopeOp {
    std::string name;
    /**
     * The body of 'fn f(p : ptr<i32>, va : i32, vb : i32, n : i32) -> i32', where 'global' is an
     * expression that directly accesses the module-scope variable 'A'.
     */
    std::string (*wgsl)(const std::string& global);
    /** Simulates 'f', where 'global' is the offset of the memory accessed by the 'global' expression. */
    int64_t (*sim)(Memory& m, int p, int global);
};

const std::vector<ModuleScopeOp>& moduleScopeOps() {
    static const std::vector<ModuleScopeOp> ops = {
        {"ptr_store_global_store_ptr_load",
         [](const std::string& global) { return "\n  *p = va;\n  " + global + " = vb;\n  return *p;"; },
         [](Memory& m, int p, int global) -> int64_t {
             m.st(p, kVA);
             m.st(global, kVB);
             return m.ld(p);
         }},
        {"global_store_ptr_store_global_load",
         [](const std::string& global) {
             return "\n  " + global + " = va;\n  *p = vb;\n  return " + global + ";";
         },
         [](Memory& m, int p, int global) -> int64_t {
             m.st(global, kVA);
             m.st(p, kVB);
             return m.ld(global);
         }},
        {"ptr_store_global_load",
         [](const std::string& global) { return "\n  *p = va;\n  return " + global + ";"; },
         [](Memory& m, int p, int global) -> int64_t {
             m.st(p, kVA);
             return m.ld(global);
         }},
        {"loop_accumulate",
         [](const std::string& global) {
             return "\n  for (var i = 0; i < n; i++) {\n    " + global + " += *p;\n  }\n  return " + global + ";";
         },
         [](Memory& m, int p, int global) -> int64_t {
             for (int i = 0; i < kN; ++i) {
                 m.st(global, int64_t{m.ld(global)} + m.ld(p));
             }
             return m.ld(global);
         }},
        {"whole_global_store",
         [](const std::string&) { return std::string("\n  *p = va;\n  A = input.b;\n  return *p;"); },
         [](Memory& m, int p, int) -> int64_t {
             m.st(p, kVA);
             const std::vector<int32_t> init = initialMemory();
             for (int i = 0; i < kDataSize; ++i) {
                 m.st(i, init[static_cast<size_t>(kB + i)]);
             }
             return m.ld(p);
         }},
    };
    return ops;
}

CTS_TEST(testGroup, "one_pointer_one_module_scope")
    .desc(R"(Test that a function with a pointer parameter behaves correctly when the pointer refers to a
module-scope variable that is also directly accessed by the function.)")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", moduleScopeAddressSpaces())
            .combine("target", targetNames(scalarTargets()))
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", opNames(moduleScopeOps()));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string space = t.param<std::string>("address_space");
        const Target& target = findTarget(scalarTargets(), t.param<std::string>("target"));
        const bool aliased = t.param<bool>("aliased");
        const ModuleScopeOp& op = findOp(moduleScopeOps(), t.param<std::string>("op"));

        const int p = (aliased ? 0 : kB) + target.offset;
        Memory m;
        m.r[0] = static_cast<int32_t>(op.sim(m, p, target.offset));

        ShaderParams params;
        params.space = space;
        params.requiresAliasing = aliased;
        params.helpers = "\nfn f(p : " + ptrType(space, "i32") + ", va : i32, vb : i32, n : i32) -> i32 {\n  " +
                         op.wgsl("A." + target.wgsl) + "\n}";
        params.body = std::string("\n  output.r[0] = f(&") + (aliased ? "A" : "B") + "." + target.wgsl +
                      ",\n                  input.p[0], input.p[1], input.p[2]);";
        params.expected = m.expected();
        run(t, params);
    });

struct AtomicOp {
    std::string name;
    /** The body of 'fn f(pa : ptr<atomic<i32>>, pb : ptr<atomic<i32>>, va : i32, vb : i32) -> i32'. */
    std::string wgsl;
    /** Simulates 'f'. */
    int64_t (*sim)(Memory& m, int pa, int pb);
};

const std::vector<AtomicOp>& atomicOps() {
    static const std::vector<AtomicOp> ops = {
        {"add_add_load",
         R"(
  atomicAdd(pa, va);
  atomicAdd(pb, vb);
  return atomicLoad(pa);)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, int64_t{m.ld(pa)} + kVA);
             m.st(pb, int64_t{m.ld(pb)} + kVB);
             return m.ld(pa);
         }},
        {"store_exchange",
         R"(
  atomicStore(pa, va);
  return atomicExchange(pb, vb);)",
         [](Memory& m, int pa, int pb) -> int64_t {
             m.st(pa, kVA);
             const int32_t old = m.ld(pb);
             m.st(pb, kVB);
             return old;
         }},
        {"exchange_load",
         R"(
  let old = atomicExchange(pa, va);
  return old + atomicLoad(pb);)",
         [](Memory& m, int pa, int pb) -> int64_t {
             const int64_t old = m.ld(pa);
             m.st(pa, kVA);
             return old + m.ld(pb);
         }},
    };
    return ops;
}

/** Atomic i32 memory locations within 'AtomicData' that pointers can be formed to. */
const std::vector<Target>& atomicTargets() {
    static const std::vector<Target> targets = {
        {"scalar", "x", 0},
        {"array_element", "arr[1]", 3},
    };
    return targets;
}

CTS_TEST(testGroup, "two_atomic_pointers")
    .desc(R"(Test that a function with two atomic pointer parameters behaves correctly when both pointers
refer to the same atomic.)")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", atomicAddressSpaces())
            .combine("target", targetNames(atomicTargets()))
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", opNames(atomicOps()));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string space = t.param<std::string>("address_space");
        const Target& target = findTarget(atomicTargets(), t.param<std::string>("target"));
        const bool aliased = t.param<bool>("aliased");
        const AtomicOp& op = findOp(atomicOps(), t.param<std::string>("op"));
        const std::string patomic = ptrType(space, "atomic<i32>");

        const int pa = target.offset;
        const int pb = (aliased ? 0 : kB) + target.offset;
        Memory m;
        m.r[0] = static_cast<int32_t>(op.sim(m, pa, pb));
        // 's' is not present in 'AtomicData', so is not written to the output.
        for (const int base : {0, kB}) {
            for (int i = kAtomicDataSize; i < kDataSize; ++i) {
                m.mem[static_cast<size_t>(base + i)] = 0;
            }
        }

        ShaderParams params;
        params.space = space;
        params.requiresAliasing = aliased;
        params.atomic = true;
        params.helpers = "\nfn f(pa : " + patomic + ", pb : " + patomic + ", va : i32, vb : i32) -> i32 {\n  " +
                         op.wgsl + "\n}";
        params.body = R"(
  atomicStore(&A.x, input.a.x);
  atomicStore(&A.y, input.a.y);
  atomicStore(&B.x, input.b.x);
  atomicStore(&B.y, input.b.y);
  for (var i = 0; i < 4; i++) {
    atomicStore(&A.arr[i], input.a.arr[i]);
    atomicStore(&B.arr[i], input.b.arr[i]);
  }

  output.r[0] = f(&A.)" + target.wgsl + ", &" + (aliased ? "A" : "B") + "." + target.wgsl + R"(,
                  input.p[0], input.p[1]);

  output.a.x = atomicLoad(&A.x);
  output.a.y = atomicLoad(&A.y);
  output.b.x = atomicLoad(&B.x);
  output.b.y = atomicLoad(&B.y);
  for (var i = 0; i < 4; i++) {
    output.a.arr[i] = atomicLoad(&A.arr[i]);
    output.b.arr[i] = atomicLoad(&B.arr[i]);
  })";
        params.expected = m.expected();
        run(t, params);
    });

struct LoopCarriedOp {
    std::string name;
    /**
     * The body of 'fn f(pd : ptr<array<i32, 32>>, ps : ptr<array<i32, 32>>, m : i32) -> i32'.
     * When 'pd' and 'ps' alias, each iteration depends on a value written by an earlier iteration,
     * so the loop must not be vectorized as if the arrays were disjoint.
     */
    std::string wgsl;
    /** Simulates 'f'. */
    int64_t (*sim)(Memory& m, int pd, int ps);
};

const std::vector<LoopCarriedOp>& loopCarriedOps() {
    static const std::vector<LoopCarriedOp> ops = {
        // Aliased: ps[0] is propagated to every element.
        {"shift_up",
         R"(
  for (var i = 0; i < m; i++) {
    (*pd)[i + 1] = (*ps)[i];
  }
  return (*pd)[m];)",
         [](Memory& mem, int pd, int ps) -> int64_t {
             for (int i = 0; i < kM; ++i) {
                 mem.st(pd + i + 1, mem.ld(ps + i));
             }
             return mem.ld(pd + kM);
         }},
        // Aliased: a dependence distance of 2, which is less than typical vector widths.
        {"shift_up_by_two",
         R"(
  for (var i = 0; i < m - 1; i++) {
    (*pd)[i + 2] = (*ps)[i] * 2;
  }
  return (*pd)[m];)",
         [](Memory& mem, int pd, int ps) -> int64_t {
             for (int i = 0; i < kM - 1; ++i) {
                 mem.st(pd + i + 2, int64_t{mem.ld(ps + i)} * 2);
             }
             return mem.ld(pd + kM);
         }},
        // Aliased: a prefix-sum style recurrence.
        {"running_sum",
         R"(
  for (var i = 0; i < m; i++) {
    (*pd)[i + 1] = (*ps)[i] + (*ps)[i + 1];
  }
  return (*pd)[m];)",
         [](Memory& mem, int pd, int ps) -> int64_t {
             for (int i = 0; i < kM; ++i) {
                 mem.st(pd + i + 1, int64_t{mem.ld(ps + i)} + mem.ld(ps + i + 1));
             }
             return mem.ld(pd + kM);
         }},
        // Aliased: the second half reads values already written by the first half, producing a
        // palindrome rather than a reversal.
        {"reverse",
         R"(
  for (var i = 0; i <= m; i++) {
    (*pd)[i] = (*ps)[m - i];
  }
  return (*pd)[0];)",
         [](Memory& mem, int pd, int ps) -> int64_t {
             for (int i = 0; i <= kM; ++i) {
                 mem.st(pd + i, mem.ld(ps + kM - i));
             }
             return mem.ld(pd);
         }},
    };
    return ops;
}

CTS_TEST(testGroup, "loop_carried_dependence")
    .desc(R"(Test that loops over two array pointers behave correctly when both pointers refer to the same
array, creating a loop-carried dependence. The loop count is a runtime value large enough to make
vectorization attractive.)")
    .params([](ParamsBuilder u) {
        return u.combine("address_space", addressSpaces())
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", opNames(loopCarriedOps()));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string space = t.param<std::string>("address_space");
        const bool aliased = t.param<bool>("aliased");
        const LoopCarriedOp& op = findOp(loopCarriedOps(), t.param<std::string>("op"));
        const std::string parr = ptrType(space, "array<i32, " + std::to_string(kBigSize) + ">");

        const int pd = kBig;
        const int ps = (aliased ? 0 : kB) + kBig;
        Memory m;
        m.r[0] = static_cast<int32_t>(op.sim(m, pd, ps));

        ShaderParams params;
        params.space = space;
        params.requiresAliasing = aliased;
        params.helpers = "\nfn f(pd : " + parr + ", ps : " + parr + ", m : i32) -> i32 {\n  " + op.wgsl + "\n}";
        params.body = std::string("\n  output.r[0] = f(&A.big, &") + (aliased ? "A" : "B") + ".big, input.p[4]);";
        params.expected = m.expected();
        run(t, params);
    });

CTS_TEST(testGroup, "buffer_view_mixed_types")
    .desc(R"(Test that a function taking two differently typed pointer parameters behaves correctly when
the pointers are views of overlapping bytes of a buffer, formed with bufferView or bufferArrayView.

The views are formed in the entry point and passed to the function. This is the pointer parameter
counterpart of the bufferView and bufferArrayView 'mixed_types_aliasing' tests.

Only the integer view is loaded from, so that float denormal flushing and NaN canonicalization
cannot affect the results.

 * 'pair' selects the types of the two views, covering combinations of scalars, vectors, and
   structures (with and without padding), including f16 types.
 * 'overlap' selects whether the views start at the same byte, partially overlap, or only overlap
   padding.
 * 'view' selects how the views are formed:
   - 'constant_offset': bufferView with a constant byte offset.
   - 'dynamic_offset': bufferView with a runtime byte offset.
   - 'dynamic_index': an element of bufferArrayView, with a runtime element index.)")
    .params([](ParamsBuilder u) {
        return u.combine("buffer", mixedTypeBuffers())
            .combine("pair", mixedTypePairNames())
            .combine("overlap", mixedTypeOverlaps())
            .filter(isValidMixedTypeOverlap)
            .combine("view", {"constant_offset", "dynamic_offset", "dynamic_index"})
            .combine("aliased", {true, false})
            .beginSubcases()
            .combine("op", mixedTypeOpNames());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool aliased = t.param<bool>("aliased");
        if (aliased && !hasUnrestrictedAliasingLanguageFeature()) {
            t.skip("Missing unrestricted_aliasing language feature");
        }
        const MixedTypePair& pair = mixedTypePair(t.param<std::string>("pair"));
        const MixedTypeViewWords words = *mixedTypeWords(*pair.intType, *pair.other, t.param<std::string>("overlap"));
        const std::string viewKind = t.param<std::string>("view");
        // The dynamic offset adds 'input.p[3] * 16' bytes, which preserves the alignment of all types.
        const int dynamicWords = 4 * kMixedTypeIdx;

        MixedTypeAliasingParams params;
        params.buffer = t.param<std::string>("buffer");
        params.intType = pair.intType;
        params.other = pair.other;
        params.intWord = words.intWord;
        params.otherWord = words.otherWord;
        params.view = [&](const MixedType& type, const std::string& buffer, int word) -> std::string {
            const std::string& ty = mixedTypeName(type);
            if (viewKind == "constant_offset") {
                return "bufferView<" + ty + ">(&" + buffer + ", " + std::to_string(word * 4) + "u)";
            }
            if (viewKind == "dynamic_offset") {
                return "bufferView<" + ty + ">(&" + buffer + ", " + std::to_string((word - dynamicWords) * 4) +
                       "u + u32(input.p[3]) * 16u)";
            }
            // The array starts one element before the target. All of the tested types have an
            // element stride equal to their size. Each view spans 32 bytes, which keeps all views
            // within the buffer.
            const int base = word * 4 - type.size * kMixedTypeIdx;
            return "&(*bufferArrayView<array<" + ty + ">>(&" + buffer + ", " + std::to_string(base) +
                   "u, 32u))[input.p[3]]";
        };
        params.aliased = aliased;
        params.op = &mixedTypeOp(t.param<std::string>("op"));
        params.pointerParams = true;
        params.directives = aliased ? "// requires unrestricted_aliasing;" : "";
        runMixedTypeAliasingTest(t, params);
    });

} // namespace
