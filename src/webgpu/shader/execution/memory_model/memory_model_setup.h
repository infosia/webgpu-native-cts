// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/execution/memory_model/memory_model_setup.ts
//   @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) the webgpu-native-cts contributors, BSD-3-Clause.
//
// Header-only port of the memory model stress harness (MemoryModelTester),
// the shader-building helpers (buildTestShader / buildResultShader), and the
// per-spec-file parameter presets used by the upstream memory_model/*.spec.ts
// files. Everything lives in namespace cts::memory_model.
//
// Documented simplifications relative to upstream:
//  - Upstream populates the shuffledWorkgroups / scratchMemoryLocations /
//    stressParams source buffers via mapAsync(MAP_WRITE). The C harness does
//    not expose the WGPUInstance needed to pump map callbacks, so those source
//    buffers use COPY_DST and are populated with wgpuQueueWriteBuffer from
//    persistent host-side shadow arrays. The shadow arrays persist across
//    iterations, which exactly replicates the persistence of upstream's mapped
//    buffer contents (notably for setScratchLocations, which only overwrites a
//    subset of entries each iteration).
//  - Upstream's SubBufferWithSource is folded into BufferWithSource with an
//    `offset` field (offset 0 for full-buffer copies).
//  - In upstream setScratchLocations, the chunking branch computes fractional
//    JS typed-array indices; JS silently drops stores at non-integral indices.
//    That behavior is replicated explicitly (see setScratchLocations).
//  - run() checks the testResults buffer synchronously each iteration instead
//    of deferring readbacks to test finalization. Pass criteria are identical:
//    the weak-behavior counter must be 0.

#ifndef WEBGPU_SHADER_EXECUTION_MEMORY_MODEL_MEMORY_MODEL_SETUP_H_
#define WEBGPU_SHADER_EXECUTION_MEMORY_MODEL_MEMORY_MODEL_SETUP_H_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <numeric>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

namespace cts {
namespace memory_model {

/* All buffer sizes are counted in units of 4-byte words. */

/**
 * The value type loaded and stored from memory (the WGSL 'store type' for the
 * locations being accessed). The GPU buffers are sized assuming this type is
 * at most 4 bytes.
 *
 * 'u32' is the default case; it can be atomically loaded and stored.
 * 'f16' is interesting because it is not 32-bits, and can't be the store type
 * for atomic accesses.
 */
enum class AccessValueType {
    F16,
    U32,
};

inline constexpr std::array<AccessValueType, 2> kAccessValueTypes = {
    AccessValueType::F16,
    AccessValueType::U32,
};

inline const char* accessValueTypeId(AccessValueType type) {
    switch (type) {
        case AccessValueType::F16:
            return "f16";
        case AccessValueType::U32:
            return "u32";
    }
    std::abort();
}

inline AccessValueType accessValueTypeFromId(std::string_view id) {
    if (id == "f16") {
        return AccessValueType::F16;
    }
    if (id == "u32") {
        return AccessValueType::U32;
    }
    std::abort();
}

/** The width used for textures (default compat limit in WebGPU). */
inline constexpr uint32_t kWidth = 4096;

/**
 * Seed-able deterministic pseudo random generator, port of upstream
 * src/webgpu/util/prng.ts (TinyMT). The generated sequence is bit-exact with
 * upstream for a given seed, so the stress schedules match upstream runs.
 */
class PRNG {
  public:
    explicit PRNG(uint32_t seed) {
        state_[0] = seed;
        state_[1] = kMat1;
        state_[2] = kMat2;
        state_[3] = kTMat;
        for (uint32_t i = 1; i < kMinLoop; i++) {
            const uint32_t prev = state_[(i - 1u) & 3u];
            state_[i & 3u] ^= i + 1812433253u * (prev ^ (prev >> 30));
        }
        for (uint32_t i = 0; i < kPreLoop; i++) {
            next();
        }
    }

    /** Returns a 32-bit unsigned integer value and advances the state. */
    uint32_t randomU32() {
        next();
        return temper();
    }

    /** Returns a value in the range [0, 1) and advances the state. */
    double random() {
        next();
        return static_cast<double>(temper()) / 4294967296.0;
    }

    /** Returns a uniformly selected integer in [0, n-1]. n must be >= 1. */
    uint32_t uniformInt(uint64_t n) {
        const uint64_t upperBound = 0x100000000ull;
        const uint64_t keepZoneSize = upperBound - (upperBound % n);
        uint64_t candidate = 0;
        do {
            candidate = randomU32();
        } while (candidate >= keepZoneSize);
        return static_cast<uint32_t>(candidate % n);
    }

  private:
    static constexpr uint32_t kMat1 = 0x8f7011ee;
    static constexpr uint32_t kMat2 = 0xfc78ff1f;
    static constexpr uint32_t kTMat = 0x3793fdff;
    static constexpr uint32_t kMask = 0x7fffffff;
    static constexpr uint32_t kMinLoop = 8;
    static constexpr uint32_t kPreLoop = 8;
    static constexpr uint32_t kSH0 = 1;
    static constexpr uint32_t kSH1 = 10;
    static constexpr uint32_t kSH8 = 8;

    void next() {
        uint32_t x = (state_[0] & kMask) ^ state_[1] ^ state_[2];
        uint32_t y = state_[3];
        x ^= x << kSH0;
        y ^= (y >> kSH0) ^ x;
        state_[0] = state_[1];
        state_[1] = state_[2];
        state_[2] = x ^ (y << kSH1);
        state_[3] = y;
        if ((y & 1u) != 0u) {
            state_[1] ^= kMat1;
            state_[2] ^= kMat2;
        }
    }

    uint32_t temper() const {
        uint32_t t0 = state_[3];
        const uint32_t t1 = state_[0] + (state_[2] >> kSH8);
        t0 ^= t1;
        if ((t1 & 1u) != 0u) {
            t0 ^= kTMat;
        }
        return t0;
    }

    uint32_t state_[4] = {};
};

/* Parameter values are set heuristically, typically by a time-intensive search. */
struct MemoryModelTestParams {
    /** Number of invocations per workgroup. The workgroups are 1-dimensional. */
    uint32_t workgroupSize;
    /** The number of workgroups to assign to running the test. */
    uint32_t testingWorkgroups;
    /**
     * Run no more than this many workgroups. Must be >= the number of testing workgroups.
     * Non-testing workgroups are used to stress other memory locations.
     */
    uint32_t maxWorkgroups;
    /** The percentage of iterations to shuffle the workgroup ids. */
    uint32_t shufflePct;
    /** The percentage of iterations to run the bounded spin-loop barrier. */
    uint32_t barrierPct;
    /** The percentage of iterations to run memory stress using non-testing workgroups. */
    uint32_t memStressPct;
    /** The number of iterations to run the memory stress pattern. */
    uint32_t memStressIterations;
    /** The percentage of iterations the first instruction in the stress pattern should be a store. */
    uint32_t memStressStoreFirstPct;
    /** The percentage of iterations the second instruction in the stress pattern should be a store. */
    uint32_t memStressStoreSecondPct;
    /** The percentage of iterations for testing threads to run stress before running the test. */
    uint32_t preStressPct;
    /** Same as for memStressIterations. */
    uint32_t preStressIterations;
    /** The percentage of iterations the first instruction in the pre-stress pattern should be a store. */
    uint32_t preStressStoreFirstPct;
    /** The percentage of iterations the second instruction in the pre-stress pattern should be a store. */
    uint32_t preStressStoreSecondPct;
    /** The size of the scratch memory region, used for stressing threads. */
    uint32_t scratchMemorySize;
    /** The size of each block of memory stressing threads access. */
    uint32_t stressLineSize;
    /** The number of blocks of memory to assign stressing threads to. */
    uint32_t stressTargetLines;
    /**
     * How non-testing threads are assigned to stressing locations. 100 means all iterations
     * use a round robin approach, 0 means all use a chunking approach.
     */
    uint32_t stressStrategyBalancePct;
    /**
     * Used to permute thread ids within a workgroup, so more random pairings are created
     * between threads coordinating on a test.
     */
    uint32_t permuteFirst;
    /**
     * Used to create distance between memory locations used in a test.
     * Set this to 1 for memory that should be aliased.
     */
    uint32_t permuteSecond;
    /** The distance (in number of 4 byte intervals) between any two memory locations used for testing. */
    uint32_t memStride;
    /**
     * For tests that access one memory location, but use dynamic addresses to avoid compiler
     * optimization, aliased memory should be set to true.
     */
    bool aliasedMemory;
    /** The number of possible behaviors that a test can have. */
    uint32_t numBehaviors;
};

// ---------------------------------------------------------------------------
// Parameter presets. Upstream defines one `memoryModelTestParams` const per
// spec file ("a reasonable parameter set, determined heuristically"); they are
// centralized here so the ported spec files share them. Copy and override
// fields to mirror upstream's `{ ...memoryModelTestParams, key: value }`.
// ---------------------------------------------------------------------------

/** Preset from upstream atomicity.spec.ts. */
inline constexpr MemoryModelTestParams kAtomicityTestParams = {
    /* workgroupSize */ 256,
    /* testingWorkgroups */ 512,
    /* maxWorkgroups */ 1024,
    /* shufflePct */ 100,
    /* barrierPct */ 100,
    /* memStressPct */ 100,
    /* memStressIterations */ 1024,
    /* memStressStoreFirstPct */ 50,
    /* memStressStoreSecondPct */ 50,
    /* preStressPct */ 100,
    /* preStressIterations */ 1024,
    /* preStressStoreFirstPct */ 50,
    /* preStressStoreSecondPct */ 50,
    /* scratchMemorySize */ 2048,
    /* stressLineSize */ 64,
    /* stressTargetLines */ 2,
    /* stressStrategyBalancePct */ 50,
    /* permuteFirst */ 109,
    /* permuteSecond */ 419,
    /* memStride */ 4,
    /* aliasedMemory */ false,
    /* numBehaviors */ 4,
};

/** Preset from upstream barrier.spec.ts (same as atomicity, but two behaviors). */
inline constexpr MemoryModelTestParams kBarrierTestParams = {
    /* workgroupSize */ 256,
    /* testingWorkgroups */ 512,
    /* maxWorkgroups */ 1024,
    /* shufflePct */ 100,
    /* barrierPct */ 100,
    /* memStressPct */ 100,
    /* memStressIterations */ 1024,
    /* memStressStoreFirstPct */ 50,
    /* memStressStoreSecondPct */ 50,
    /* preStressPct */ 100,
    /* preStressIterations */ 1024,
    /* preStressStoreFirstPct */ 50,
    /* preStressStoreSecondPct */ 50,
    /* scratchMemorySize */ 2048,
    /* stressLineSize */ 64,
    /* stressTargetLines */ 2,
    /* stressStrategyBalancePct */ 50,
    /* permuteFirst */ 109,
    /* permuteSecond */ 419,
    /* memStride */ 4,
    /* aliasedMemory */ false,
    /* numBehaviors */ 2,
};

/** Preset from upstream coherence.spec.ts. */
inline constexpr MemoryModelTestParams kCoherenceTestParams = {
    /* workgroupSize */ 256,
    /* testingWorkgroups */ 39,
    /* maxWorkgroups */ 952,
    /* shufflePct */ 0,
    /* barrierPct */ 0,
    /* memStressPct */ 0,
    /* memStressIterations */ 1024,
    /* memStressStoreFirstPct */ 50,
    /* memStressStoreSecondPct */ 50,
    /* preStressPct */ 0,
    /* preStressIterations */ 1024,
    /* preStressStoreFirstPct */ 50,
    /* preStressStoreSecondPct */ 50,
    /* scratchMemorySize */ 2048,
    /* stressLineSize */ 64,
    /* stressTargetLines */ 2,
    /* stressStrategyBalancePct */ 50,
    /* permuteFirst */ 109,
    /* permuteSecond */ 1,
    /* memStride */ 1,
    /* aliasedMemory */ true,
    /* numBehaviors */ 4,
};

/** Preset from upstream weak.spec.ts. */
inline constexpr MemoryModelTestParams kWeakTestParams = {
    /* workgroupSize */ 256,
    /* testingWorkgroups */ 739,
    /* maxWorkgroups */ 885,
    /* shufflePct */ 0,
    /* barrierPct */ 0,
    /* memStressPct */ 0,
    /* memStressIterations */ 1024,
    /* memStressStoreFirstPct */ 50,
    /* memStressStoreSecondPct */ 50,
    /* preStressPct */ 100,
    /* preStressIterations */ 33,
    /* preStressStoreFirstPct */ 0,
    /* preStressStoreSecondPct */ 100,
    /* scratchMemorySize */ 1408,
    /* stressLineSize */ 4,
    /* stressTargetLines */ 11,
    /* stressStrategyBalancePct */ 0,
    /* permuteFirst */ 109,
    /* permuteSecond */ 419,
    /* memStride */ 2,
    /* aliasedMemory */ false,
    /* numBehaviors */ 4,
};

/**
 * The number of memory locations accessed by a test. Currently, only tests
 * with up to 2 memory locations are supported.
 */
inline constexpr uint32_t kNumMemLocations = 2;

/**
 * The number of read outputs per test that need to be analyzed in the result
 * aggregation shader. Currently, only tests with up to 2 read outputs are supported.
 */
inline constexpr uint32_t kNumReadOutputs = 2;

/** Represents a device buffer plus a utility source buffer used to reset its contents. */
struct BufferWithSource {
    /** Buffer used by shader code (for combo sub-ranges, the shared combo buffer). */
    WGPUBuffer deviceBuf = nullptr;
    /** Buffer populated from the host side; data is copied to the device buffer each iteration. */
    WGPUBuffer srcBuf = nullptr;
    /** Size in bytes of this (portion of the) buffer. */
    uint64_t size = 0;
    /** Offset in bytes of this portion within deviceBuf (0 for whole-buffer entries). */
    uint64_t offset = 0;
};

/** Represents a device texture and a utility buffer for resetting memory. */
struct TextureWithSource {
    /** Texture used by shader code. */
    WGPUTexture deviceTex = nullptr;
    /** Buffer populated from the host side; data is copied to the texture for use by the shader. */
    WGPUBuffer srcBuf = nullptr;
    /** Size in bytes of the texture data. */
    uint64_t size = 0;
};

/** Specifies the buffers used during a memory model test. */
struct MemoryModelBuffers {
    /** This is the memory region that testing threads read from and write to. */
    BufferWithSource testLocations;
    /** This buffer collects the results of reads for analysis in the result aggregation shader. */
    BufferWithSource readResults;
    /** This buffer is the aggregated results of every testing thread; checked for test success/failure. */
    BufferWithSource testResults;
    /** This buffer stores the shuffled workgroup ids for use during testing. Read-only in the shader. */
    BufferWithSource shuffledWorkgroups;
    /** This is the bounded spin-loop barrier, used to temporally align testing threads. */
    BufferWithSource barrier;
    /** Memory region for stressing threads to read to and write from. */
    BufferWithSource scratchpad;
    /** The memory locations in the scratch region that stressing threads access. */
    BufferWithSource scratchMemoryLocations;
    /** Parameters that are used by the shader to calculate memory locations and perform stress. */
    BufferWithSource stressParams;
};

struct MemoryModelTextures {
    TextureWithSource testLocations;
};

/** The number of stress params in the stress params buffer. */
inline constexpr uint32_t kNumStressParams = 12;
inline constexpr uint32_t kBarrierParamIndex = 0;
inline constexpr uint32_t kMemStressIndex = 1;
inline constexpr uint32_t kMemStressIterationsIndex = 2;
inline constexpr uint32_t kMemStressPatternIndex = 3;
inline constexpr uint32_t kPreStressIndex = 4;
inline constexpr uint32_t kPreStressIterationsIndex = 5;
inline constexpr uint32_t kPreStressPatternIndex = 6;
inline constexpr uint32_t kPermuteFirstIndex = 7;
inline constexpr uint32_t kPermuteSecondIndex = 8;
inline constexpr uint32_t kTestingWorkgroupsIndex = 9;
inline constexpr uint32_t kMemStrideIndex = 10;
inline constexpr uint32_t kMemLocationOffsetIndex = 11;

/**
 * All memory used in these tests consists of four byte words, so this value is
 * used to correctly set the byte size of buffers that are read to/written from
 * during tests and for storing test results.
 */
inline constexpr uint32_t kBytesPerWord = 4;

namespace detail {

inline WGPUStringView stringView(std::string_view value) {
    WGPUStringView view;
    view.data = value.data();
    view.length = value.length();
    return view;
}

/** Rounds `n` up to the nearest multiple of `alignment`. */
inline constexpr uint64_t alignUp(uint64_t n, uint64_t alignment) {
    return ((n + alignment - 1) / alignment) * alignment;
}

/** Joins string pieces with '\n', mirroring upstream's `[..].join('\n')`. */
inline std::string joinShader(std::initializer_list<std::string_view> pieces) {
    std::string result;
    bool first = true;
    for (std::string_view piece : pieces) {
        if (!first) {
            result += "\n";
        }
        first = false;
        result += piece;
    }
    return result;
}

} // namespace detail

/**
 * Returns the shader preamble based on the access value type:
 *  - enable directives, if necessary
 *  - the type alias for AccessValueType
 */
inline std::string shaderPreamble(AccessValueType accessValueType, const std::string& constants) {
    if (accessValueType == AccessValueType::F16) {
        return "enable f16;\nalias AccessValueTy = f16;\n" + constants + "\n";
    }
    return "alias AccessValueTy = u32;\n" + constants + "\n";
}

/**
 * Implements setup code necessary to run a memory model test. A test consists of two parts:
 *  1.) A test shader that runs a specified memory model litmus test and attempts to reveal a
 *      weak (disallowed) behavior. At a high level, a test shader consists of a set of testing
 *      workgroups where every invocation executes the litmus test on a set of test locations,
 *      and a set of stressing workgroups where every invocation accesses a specified memory
 *      location in a random pattern.
 *  2.) A result shader that takes the output of the test shader (the memory locations accessed
 *      during the test and the results of any reads made during the test) and aggregates the
 *      results based on the possible behaviors of the test.
 *
 * See upstream memory_model_setup.ts for the full description of the buffer variables.
 */
class MemoryModelTester {
  public:
    /** Sets up a memory model test by initializing buffers and pipeline layouts. */
    MemoryModelTester(
        GpuTest& t,
        const MemoryModelTestParams& params,
        std::string testShader,
        std::string resultShader,
        AccessValueType accessValueType = AccessValueType::U32,
        bool useTexture = false)
        : test_(t), params_(params), prng_(1), useTexture_(useTexture) {
        const WGPULimits limits = test_.getLimits();
        const uint32_t workgroupXSize = std::min(params_.workgroupSize, limits.maxComputeWorkgroupSizeX);
        workgroupXSize_ = workgroupXSize;

        std::ostringstream constantsStream;
        constantsStream
            << "\n  const kNumBarriers = 1u;  // MAINTENANCE_TODO: make barrier not an array\n"
            << "  const kMaxWorkgroups = " << params_.maxWorkgroups << "u;\n"
            << "  const kScratchMemorySize = " << params_.scratchMemorySize << "u;\n"
            << "  const kWorkgroupXSize = " << workgroupXSize << "u;\n";
        const std::string constants = constantsStream.str();
        testShader = shaderPreamble(accessValueType, constants) + testShader;
        resultShader = shaderPreamble(accessValueType, constants) + resultShader;

        // set up buffers
        const uint32_t testingThreads = workgroupXSize * params_.testingWorkgroups;
        const uint64_t testLocationsSize =
            static_cast<uint64_t>(testingThreads) * kNumMemLocations * params_.memStride * kBytesPerWord;
        buffers_.testLocations.deviceBuf =
            createDeviceBuffer("testLocationsBuffer", testLocationsSize,
                               WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage);
        buffers_.testLocations.srcBuf =
            createDeviceBuffer("testLocationsSrcBuf", testLocationsSize, WGPUBufferUsage_CopySrc);
        buffers_.testLocations.size = testLocationsSize;

        const uint64_t readResultsSize =
            static_cast<uint64_t>(testingThreads) * kNumReadOutputs * kBytesPerWord;
        buffers_.readResults.deviceBuf =
            createDeviceBuffer("readResultsBuffer", readResultsSize,
                               WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage);
        buffers_.readResults.srcBuf =
            createDeviceBuffer("readResultsSrcBuf", readResultsSize, WGPUBufferUsage_CopySrc);
        buffers_.readResults.size = readResultsSize;

        const uint64_t testResultsSize = static_cast<uint64_t>(params_.numBehaviors) * kBytesPerWord;
        buffers_.testResults.deviceBuf =
            createDeviceBuffer("testResultsBuffer", testResultsSize,
                               WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc);
        buffers_.testResults.srcBuf =
            createDeviceBuffer("testResultsSrcBuffer", testResultsSize, WGPUBufferUsage_CopySrc);
        buffers_.testResults.size = testResultsSize;

        const uint64_t shuffledWorkgroupsSize =
            static_cast<uint64_t>(params_.maxWorkgroups) * kBytesPerWord;
        buffers_.shuffledWorkgroups.deviceBuf =
            createDeviceBuffer("shuffledWorkgroupsBuffer", shuffledWorkgroupsSize,
                               WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage);
        // Upstream: COPY_SRC | MAP_WRITE; ported to COPY_SRC | COPY_DST (queueWriteBuffer).
        buffers_.shuffledWorkgroups.srcBuf =
            createDeviceBuffer("shuffledWorkgroupsSrcBuf", shuffledWorkgroupsSize,
                               WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        buffers_.shuffledWorkgroups.size = shuffledWorkgroupsSize;
        shuffledWorkgroupsHost_.assign(params_.maxWorkgroups, 0u);

        if (useTexture_) {
            const uint64_t numTexels = testLocationsSize / kBytesPerWord;
            const uint32_t width = kWidth;
            const uint32_t height = static_cast<uint32_t>(numTexels / width);
            WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            texDesc.format = WGPUTextureFormat_R32Uint;
            texDesc.dimension = WGPUTextureDimension_2D;
            texDesc.size = WGPUExtent3D{width, height, 1};
            texDesc.usage = WGPUTextureUsage_CopyDst | WGPUTextureUsage_StorageBinding;
            textures_.testLocations.deviceTex = test_.createTextureTracked(texDesc);
            textures_.testLocations.srcBuf = buffers_.testLocations.srcBuf;
            textures_.testLocations.size = testLocationsSize;
        }

        // Combine 3 arrays into 1 buffer as we need to keep the number of storage buffers
        // to 4 for compat.
        const uint64_t falseSharingAvoidanceQuantum = 4096;
        const uint64_t barrierSize = detail::alignUp(kBytesPerWord, falseSharingAvoidanceQuantum);
        const uint64_t scratchpadSize = detail::alignUp(
            static_cast<uint64_t>(params_.scratchMemorySize) * kBytesPerWord,
            falseSharingAvoidanceQuantum);
        const uint64_t scratchMemoryLocationsSize = detail::alignUp(
            static_cast<uint64_t>(params_.maxWorkgroups) * kBytesPerWord,
            falseSharingAvoidanceQuantum);
        const uint64_t comboSize = barrierSize + scratchpadSize + scratchMemoryLocationsSize;

        WGPUBuffer comboBuffer = createDeviceBuffer(
            "comboBuffer", comboSize, WGPUBufferUsage_CopyDst | WGPUBufferUsage_Storage);
        comboBufferSize_ = comboSize;

        buffers_.barrier.deviceBuf = comboBuffer;
        buffers_.barrier.srcBuf =
            createDeviceBuffer("barrierSrcBuf", barrierSize, WGPUBufferUsage_CopySrc);
        buffers_.barrier.size = barrierSize;
        buffers_.barrier.offset = 0;

        buffers_.scratchpad.deviceBuf = comboBuffer;
        buffers_.scratchpad.srcBuf =
            createDeviceBuffer("scratchpadSrcBuf", scratchpadSize, WGPUBufferUsage_CopySrc);
        buffers_.scratchpad.size = scratchpadSize;
        buffers_.scratchpad.offset = barrierSize;

        buffers_.scratchMemoryLocations.deviceBuf = comboBuffer;
        // Upstream: COPY_SRC | MAP_WRITE; ported to COPY_SRC | COPY_DST (queueWriteBuffer).
        buffers_.scratchMemoryLocations.srcBuf =
            createDeviceBuffer("scratchMemoryLocationsSrcBuf", scratchMemoryLocationsSize,
                               WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        buffers_.scratchMemoryLocations.size = scratchMemoryLocationsSize;
        buffers_.scratchMemoryLocations.offset = barrierSize + scratchpadSize;
        // Persistent host shadow: upstream re-maps the same buffer each iteration, so values
        // written in earlier iterations persist when not overwritten.
        scratchLocationsHost_.assign(
            static_cast<size_t>(scratchMemoryLocationsSize / kBytesPerWord), 0u);

        const uint64_t stressParamsSize = static_cast<uint64_t>(kNumStressParams) * kBytesPerWord;
        buffers_.stressParams.deviceBuf =
            createDeviceBuffer("stressParamsBuffer", stressParamsSize,
                               WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform);
        // Upstream: COPY_SRC | MAP_WRITE; ported to COPY_SRC | COPY_DST (queueWriteBuffer).
        buffers_.stressParams.srcBuf =
            createDeviceBuffer("stressParamsSrcBuf", stressParamsSize,
                               WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);
        buffers_.stressParams.size = stressParamsSize;
        stressParamsHost_.assign(kNumStressParams, 0u);

        // set up pipeline layouts
        std::array<WGPUBindGroupLayoutEntry, 5> testLayoutEntries;
        for (uint32_t i = 0; i < testLayoutEntries.size(); i++) {
            testLayoutEntries[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            testLayoutEntries[i].binding = i;
            testLayoutEntries[i].visibility = WGPUShaderStage_Compute;
            testLayoutEntries[i].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            testLayoutEntries[i].buffer.type = WGPUBufferBindingType_Storage;
        }
        testLayoutEntries[2].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
        testLayoutEntries[4].buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor testLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        testLayoutDesc.label = detail::stringView("testLayout");
        testLayoutDesc.entryCount = testLayoutEntries.size();
        testLayoutDesc.entries = testLayoutEntries.data();
        WGPUBindGroupLayout testLayout = test_.createBindGroupLayoutTracked(testLayoutDesc);

        std::vector<WGPUBindGroupLayout> layouts = {testLayout};
        if (useTexture_) {
            WGPUBindGroupLayoutEntry textureLayoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            textureLayoutEntry.binding = 0;
            textureLayoutEntry.visibility = WGPUShaderStage_Compute;
            textureLayoutEntry.storageTexture = WGPU_STORAGE_TEXTURE_BINDING_LAYOUT_INIT;
            textureLayoutEntry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
            textureLayoutEntry.storageTexture.format = WGPUTextureFormat_R32Uint;
            textureLayoutEntry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;

            WGPUBindGroupLayoutDescriptor textureLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            textureLayoutDesc.label = detail::stringView("textureLayout");
            textureLayoutDesc.entryCount = 1;
            textureLayoutDesc.entries = &textureLayoutEntry;
            WGPUBindGroupLayout textureLayout = test_.createBindGroupLayoutTracked(textureLayoutDesc);
            layouts.push_back(textureLayout);

            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPUTextureView texView = test_.createViewTracked(textures_.testLocations.deviceTex, viewDesc);

            WGPUBindGroupEntry textureBindGroupEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            textureBindGroupEntry.binding = 0;
            textureBindGroupEntry.textureView = texView;

            WGPUBindGroupDescriptor textureBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            textureBindGroupDesc.label = detail::stringView("textureBindGroup");
            textureBindGroupDesc.layout = textureLayout;
            textureBindGroupDesc.entryCount = 1;
            textureBindGroupDesc.entries = &textureBindGroupEntry;
            textureBindGroup_ = test_.createBindGroupTracked(textureBindGroupDesc);
        }

        WGPUPipelineLayoutDescriptor testPipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        testPipelineLayoutDesc.bindGroupLayoutCount = layouts.size();
        testPipelineLayoutDesc.bindGroupLayouts = layouts.data();
        WGPUPipelineLayout testPipelineLayout = test_.createPipelineLayoutTracked(testPipelineLayoutDesc);

        WGPUShaderModule testModule = test_.createShaderModuleTracked(testShader);
        WGPUComputePipelineDescriptor testPipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        testPipelineDesc.label = detail::stringView("testPipeline");
        testPipelineDesc.layout = testPipelineLayout;
        testPipelineDesc.compute.module = testModule;
        testPipelineDesc.compute.entryPoint = detail::stringView("main");
        testPipeline_ = test_.createComputePipelineTracked(testPipelineDesc);

        std::array<WGPUBindGroupEntry, 5> testBindGroupEntries;
        for (uint32_t i = 0; i < testBindGroupEntries.size(); i++) {
            testBindGroupEntries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            testBindGroupEntries[i].binding = i;
            testBindGroupEntries[i].offset = 0;
        }
        testBindGroupEntries[0].buffer = buffers_.testLocations.deviceBuf;
        testBindGroupEntries[0].size = buffers_.testLocations.size;
        testBindGroupEntries[1].buffer = buffers_.readResults.deviceBuf;
        testBindGroupEntries[1].size = buffers_.readResults.size;
        testBindGroupEntries[2].buffer = buffers_.shuffledWorkgroups.deviceBuf;
        testBindGroupEntries[2].size = buffers_.shuffledWorkgroups.size;
        testBindGroupEntries[3].buffer = comboBuffer;
        testBindGroupEntries[3].size = comboSize;
        testBindGroupEntries[4].buffer = buffers_.stressParams.deviceBuf;
        testBindGroupEntries[4].size = buffers_.stressParams.size;

        WGPUBindGroupDescriptor testBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        testBindGroupDesc.label = detail::stringView("testBindGroup");
        testBindGroupDesc.layout = testLayout;
        testBindGroupDesc.entryCount = testBindGroupEntries.size();
        testBindGroupDesc.entries = testBindGroupEntries.data();
        testBindGroup_ = test_.createBindGroupTracked(testBindGroupDesc);

        std::array<WGPUBindGroupLayoutEntry, 4> resultLayoutEntries;
        for (uint32_t i = 0; i < resultLayoutEntries.size(); i++) {
            resultLayoutEntries[i] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            resultLayoutEntries[i].binding = i;
            resultLayoutEntries[i].visibility = WGPUShaderStage_Compute;
            resultLayoutEntries[i].buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            resultLayoutEntries[i].buffer.type = WGPUBufferBindingType_Storage;
        }
        resultLayoutEntries[3].buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor resultLayoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        resultLayoutDesc.label = detail::stringView("resultLayout");
        resultLayoutDesc.entryCount = resultLayoutEntries.size();
        resultLayoutDesc.entries = resultLayoutEntries.data();
        WGPUBindGroupLayout resultLayout = test_.createBindGroupLayoutTracked(resultLayoutDesc);

        WGPUPipelineLayoutDescriptor resultPipelineLayoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        resultPipelineLayoutDesc.bindGroupLayoutCount = 1;
        resultPipelineLayoutDesc.bindGroupLayouts = &resultLayout;
        WGPUPipelineLayout resultPipelineLayout =
            test_.createPipelineLayoutTracked(resultPipelineLayoutDesc);

        WGPUShaderModule resultModule = test_.createShaderModuleTracked(resultShader);
        WGPUComputePipelineDescriptor resultPipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        resultPipelineDesc.label = detail::stringView("resultPipeline");
        resultPipelineDesc.layout = resultPipelineLayout;
        resultPipelineDesc.compute.module = resultModule;
        resultPipelineDesc.compute.entryPoint = detail::stringView("main");
        resultPipeline_ = test_.createComputePipelineTracked(resultPipelineDesc);

        std::array<WGPUBindGroupEntry, 4> resultBindGroupEntries;
        for (uint32_t i = 0; i < resultBindGroupEntries.size(); i++) {
            resultBindGroupEntries[i] = WGPU_BIND_GROUP_ENTRY_INIT;
            resultBindGroupEntries[i].binding = i;
            resultBindGroupEntries[i].offset = 0;
        }
        resultBindGroupEntries[0].buffer = buffers_.testLocations.deviceBuf;
        resultBindGroupEntries[0].size = buffers_.testLocations.size;
        resultBindGroupEntries[1].buffer = buffers_.readResults.deviceBuf;
        resultBindGroupEntries[1].size = buffers_.readResults.size;
        resultBindGroupEntries[2].buffer = buffers_.testResults.deviceBuf;
        resultBindGroupEntries[2].size = buffers_.testResults.size;
        resultBindGroupEntries[3].buffer = buffers_.stressParams.deviceBuf;
        resultBindGroupEntries[3].size = buffers_.stressParams.size;

        WGPUBindGroupDescriptor resultBindGroupDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        resultBindGroupDesc.label = detail::stringView("resultBindGroup");
        resultBindGroupDesc.layout = resultLayout;
        resultBindGroupDesc.entryCount = resultBindGroupEntries.size();
        resultBindGroupDesc.entries = resultBindGroupEntries.data();
        resultBindGroup_ = test_.createBindGroupTracked(resultBindGroupDesc);
    }

    /**
     * Run the test for the specified number of iterations. Checks the testResults buffer on the
     * weakIndex; if this value is not 0 then the test has failed. The number of iterations is
     * chosen per test so that the full set of tests meets some time budget while still being
     * reasonably effective at uncovering issues.
     */
    void run(uint32_t iterations, uint32_t weakIndex) {
        for (uint32_t i = 0; i < iterations; i++) {
            const uint32_t numWorkgroups =
                getRandomInRange(params_.testingWorkgroups, params_.maxWorkgroups);
            setShuffledWorkgroups(numWorkgroups);
            setScratchLocations(numWorkgroups);
            setStressParams();

            WGPUCommandEncoder encoder = test_.createCommandEncoderTracked();
            copyBufferToBuffer(encoder, buffers_.testLocations);
            copyBufferToBuffer(encoder, buffers_.readResults);
            copyBufferToBuffer(encoder, buffers_.testResults);
            copyBufferToBuffer(encoder, buffers_.barrier);
            copyBufferToBuffer(encoder, buffers_.shuffledWorkgroups);
            copyBufferToBuffer(encoder, buffers_.scratchpad);
            copyBufferToBuffer(encoder, buffers_.scratchMemoryLocations);
            copyBufferToBuffer(encoder, buffers_.stressParams);
            if (useTexture_) {
                copyBufferToTexture(encoder, textures_.testLocations);
            }

            WGPUComputePassDescriptor testPassDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            WGPUComputePassEncoder testPass = wgpuCommandEncoderBeginComputePass(encoder, &testPassDesc);
            wgpuComputePassEncoderSetPipeline(testPass, testPipeline_);
            wgpuComputePassEncoderSetBindGroup(testPass, 0, testBindGroup_, 0, nullptr);
            if (useTexture_) {
                wgpuComputePassEncoderSetBindGroup(testPass, 1, textureBindGroup_, 0, nullptr);
            }
            wgpuComputePassEncoderDispatchWorkgroups(testPass, numWorkgroups, 1, 1);
            wgpuComputePassEncoderEnd(testPass);

            WGPUComputePassDescriptor resultPassDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            WGPUComputePassEncoder resultPass =
                wgpuCommandEncoderBeginComputePass(encoder, &resultPassDesc);
            wgpuComputePassEncoderSetPipeline(resultPass, resultPipeline_);
            wgpuComputePassEncoderSetBindGroup(resultPass, 0, resultBindGroup_, 0, nullptr);
            wgpuComputePassEncoderDispatchWorkgroups(resultPass, params_.testingWorkgroups, 1, 1);
            wgpuComputePassEncoderEnd(resultPass);

            WGPUCommandBuffer commandBuffer = test_.finishTracked(encoder);
            wgpuQueueSubmit(test_.queue(), 1, &commandBuffer);

            test_.expectGPUBufferValuesPassCheck(
                buffers_.testResults.deviceBuf,
                checkWeakIndex(weakIndex),
                /*srcByteOffset=*/0,
                /*byteLength=*/static_cast<size_t>(params_.numBehaviors) * kBytesPerWord);
        }
    }

  protected:
    /**
     * Returns a check function for expectGPUBufferValuesPassCheck: the test fails iff the value
     * at the weak index is non-zero, which means a behavior disallowed by the memory model was
     * observed. All other behavior counters may hold any value.
     */
    std::function<std::optional<std::string>(const uint8_t*, size_t)> checkWeakIndex(
        uint32_t weakIndex) const {
        const uint32_t numBehaviors = params_.numBehaviors;
        return [weakIndex, numBehaviors](
                   const uint8_t* actual, size_t len) -> std::optional<std::string> {
            std::vector<uint32_t> words(numBehaviors, 0u);
            const size_t bytes = std::min(len, static_cast<size_t>(numBehaviors) * kBytesPerWord);
            std::memcpy(words.data(), actual, bytes);
            if (weakIndex < numBehaviors && words[weakIndex] > 0u) {
                std::ostringstream message;
                message << "memory model test failed: testResults[" << weakIndex << "] == "
                        << words[weakIndex] << ", expected == 0 (disallowed weak behavior "
                        << "observed). behaviors: [";
                for (uint32_t i = 0; i < numBehaviors; i++) {
                    if (i != 0) {
                        message << ", ";
                    }
                    message << words[i];
                }
                message << "]";
                return message.str();
            }
            return std::nullopt;
        };
    }

    /** Utility method that simplifies copying source buffers to device buffers. */
    void copyBufferToBuffer(WGPUCommandEncoder encoder, const BufferWithSource& buffer) {
        wgpuCommandEncoderCopyBufferToBuffer(
            encoder, buffer.srcBuf, 0, buffer.deviceBuf, buffer.offset, buffer.size);
    }

    /** Utility method that simplifies copying source buffers to device textures. */
    void copyBufferToTexture(WGPUCommandEncoder encoder, const TextureWithSource& texture) {
        const uint64_t numTexels = texture.size / kBytesPerWord; // always uses r32uint format.
        const uint32_t height = static_cast<uint32_t>(numTexels / kWidth);

        WGPUTexelCopyBufferInfo source = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        source.buffer = texture.srcBuf;
        source.layout.offset = 0;
        source.layout.bytesPerRow = kWidth * kBytesPerWord;
        source.layout.rowsPerImage = height;

        WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
        destination.texture = texture.deviceTex;

        const WGPUExtent3D copySize = WGPUExtent3D{kWidth, height, 1};
        wgpuCommandEncoderCopyBufferToTexture(encoder, &source, &destination, &copySize);
    }

    /** Returns a random integer in the range [0, max). */
    uint32_t getRandomInt(uint32_t max) {
        return prng_.randomU32() % max;
    }

    /** Returns a random number in the range [min, max). */
    uint32_t getRandomInRange(uint32_t min, uint32_t max) {
        if (min == max) {
            return min;
        }
        return min + getRandomInt(max - min);
    }

    /** Permutes the array using a simple Fisher-Yates shuffle (matches upstream's RNG usage). */
    void shuffleArray(std::vector<uint32_t>& a) {
        // Upstream iterates down to and including i == 0, consuming a random value there too.
        for (size_t i = a.size(); i-- > 0;) {
            const uint32_t toSwap = getRandomInt(static_cast<uint32_t>(i) + 1u);
            std::swap(a[toSwap], a[i]);
        }
    }

    /**
     * Shuffles the order of workgroup ids, so that threads operating on the same memory location
     * are not always in consecutive workgroups.
     */
    void setShuffledWorkgroups(uint32_t numWorkgroups) {
        for (uint32_t i = 0; i < numWorkgroups; i++) {
            shuffledWorkgroupsHost_[i] = i;
        }
        if (getRandomInt(100) < params_.shufflePct) {
            for (uint32_t i = numWorkgroups - 1; i > 0; i--) {
                const uint32_t x = getRandomInt(i + 1);
                std::swap(shuffledWorkgroupsHost_[i], shuffledWorkgroupsHost_[x]);
            }
        }
        test_.queueWriteBuffer(
            buffers_.shuffledWorkgroups.srcBuf, 0, shuffledWorkgroupsHost_.data(),
            shuffledWorkgroupsHost_.size() * sizeof(uint32_t));
    }

    /**
     * Sets the memory locations that stressing workgroups will access. Uses either a chunking or
     * round robin assignment strategy.
     */
    void setScratchLocations(uint32_t numWorkgroups) {
        std::vector<uint32_t>& scratchLocations = scratchLocationsHost_;
        const uint32_t scratchNumRegions = params_.scratchMemorySize / params_.stressLineSize;
        std::vector<uint32_t> scratchRegions(scratchNumRegions);
        std::iota(scratchRegions.begin(), scratchRegions.end(), 0u);
        shuffleArray(scratchRegions);
        for (uint32_t i = 0; i < params_.stressTargetLines; i++) {
            const uint32_t region = scratchRegions[i];
            const uint32_t locInRegion = getRandomInt(params_.stressLineSize);
            const uint32_t loc = region * params_.stressLineSize + locInRegion;
            if (getRandomInt(100) < params_.stressStrategyBalancePct) {
                // In the round-robin case, the current scratch location is striped across all
                // workgroups.
                for (uint32_t j = i; j < numWorkgroups; j += params_.stressTargetLines) {
                    scratchLocations[j] = loc;
                }
            } else {
                // In the chunking case, the current scratch location is assigned to a block of
                // workgroups. The final scratch location may be assigned to more workgroups, if
                // the number of scratch locations does not cleanly divide the number of
                // workgroups.
                //
                // Upstream computes `numWorkgroups / stressTargetLines` in floating point and
                // indexes a Uint32Array with `i * workgroupsPerLocation + j`; JS silently drops
                // stores at non-integral typed-array indices. Replicate that behavior exactly.
                const double workgroupsPerLocation =
                    static_cast<double>(numWorkgroups) / static_cast<double>(params_.stressTargetLines);
                for (uint32_t j = 0; static_cast<double>(j) < workgroupsPerLocation; j++) {
                    const double index =
                        static_cast<double>(i) * workgroupsPerLocation + static_cast<double>(j);
                    if (index == std::floor(index) &&
                        static_cast<size_t>(index) < scratchLocations.size()) {
                        scratchLocations[static_cast<size_t>(index)] = loc;
                    }
                }
                if (i == params_.stressTargetLines - 1 &&
                    numWorkgroups % params_.stressTargetLines != 0) {
                    for (uint32_t j = 0; j < numWorkgroups % params_.stressTargetLines; j++) {
                        scratchLocations[numWorkgroups - j - 1] = loc;
                    }
                }
            }
        }
        test_.queueWriteBuffer(
            buffers_.scratchMemoryLocations.srcBuf, 0, scratchLocations.data(),
            scratchLocations.size() * sizeof(uint32_t));
    }

    /** Sets the parameters that are used by the shader to calculate memory locations and perform stress. */
    void setStressParams() {
        std::vector<uint32_t>& stressParams = stressParamsHost_;
        stressParams[kBarrierParamIndex] = (getRandomInt(100) < params_.barrierPct) ? 1u : 0u;
        stressParams[kMemStressIndex] = (getRandomInt(100) < params_.memStressPct) ? 1u : 0u;
        stressParams[kMemStressIterationsIndex] = params_.memStressIterations;
        const bool memStressStoreFirst = getRandomInt(100) < params_.memStressStoreFirstPct;
        const bool memStressStoreSecond = getRandomInt(100) < params_.memStressStoreSecondPct;
        uint32_t memStressPattern = 3;
        if (memStressStoreFirst && memStressStoreSecond) {
            memStressPattern = 0;
        } else if (memStressStoreFirst && !memStressStoreSecond) {
            memStressPattern = 1;
        } else if (!memStressStoreFirst && memStressStoreSecond) {
            memStressPattern = 2;
        }
        stressParams[kMemStressPatternIndex] = memStressPattern;
        stressParams[kPreStressIndex] = (getRandomInt(100) < params_.preStressPct) ? 1u : 0u;
        stressParams[kPreStressIterationsIndex] = params_.preStressIterations;
        const bool preStressStoreFirst = getRandomInt(100) < params_.preStressStoreFirstPct;
        const bool preStressStoreSecond = getRandomInt(100) < params_.preStressStoreSecondPct;
        uint32_t preStressPattern = 3;
        if (preStressStoreFirst && preStressStoreSecond) {
            preStressPattern = 0;
        } else if (preStressStoreFirst && !preStressStoreSecond) {
            preStressPattern = 1;
        } else if (!preStressStoreFirst && preStressStoreSecond) {
            preStressPattern = 2;
        }
        stressParams[kPreStressPatternIndex] = preStressPattern;
        stressParams[kPermuteFirstIndex] = params_.permuteFirst;
        stressParams[kPermuteSecondIndex] = params_.permuteSecond;
        stressParams[kTestingWorkgroupsIndex] = params_.testingWorkgroups;
        stressParams[kMemStrideIndex] = params_.memStride;
        stressParams[kMemLocationOffsetIndex] = params_.aliasedMemory ? 0u : params_.memStride;
        test_.queueWriteBuffer(
            buffers_.stressParams.srcBuf, 0, stressParams.data(),
            stressParams.size() * sizeof(uint32_t));
    }

    WGPUBuffer createDeviceBuffer(std::string_view label, uint64_t size, WGPUBufferUsage usage) {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.label = detail::stringView(label);
        desc.size = size;
        desc.usage = usage;
        return test_.createBufferTracked(desc);
    }

    GpuTest& test_;
    MemoryModelTestParams params_;
    PRNG prng_;
    bool useTexture_ = false;
    uint32_t workgroupXSize_ = 0;
    MemoryModelBuffers buffers_;
    MemoryModelTextures textures_;
    uint64_t comboBufferSize_ = 0;
    std::vector<uint32_t> shuffledWorkgroupsHost_;
    std::vector<uint32_t> scratchLocationsHost_;
    std::vector<uint32_t> stressParamsHost_;
    WGPUComputePipeline testPipeline_ = nullptr;
    WGPUBindGroup testBindGroup_ = nullptr;
    WGPUBindGroup textureBindGroup_ = nullptr;
    WGPUComputePipeline resultPipeline_ = nullptr;
    WGPUBindGroup resultBindGroup_ = nullptr;
};

// ---------------------------------------------------------------------------
// Shader building blocks (verbatim WGSL from upstream).
// ---------------------------------------------------------------------------

namespace detail {

/** Defines common data structures used in memory model test shaders. */
inline constexpr const char kShaderMemStructures[] = R"wgsl(
  struct Memory {
    value: array<AccessValueTy>
  };

  struct AtomicMemory {
    value: array<atomic<u32>>
  };

  struct IndexMemory {
    value: array<u32>,
  };

  struct AtomicMemoryBarrier {
    value: array<atomic<u32>, kNumBarriers>
  };

  struct IndexMemoryScratchpad {
    value: array<u32, kMaxWorkgroups>,
  };

  struct IndexMemoryScratchLocations {
    value: array<u32, kScratchMemorySize>,
  };

  struct ReadResult {
    r0: atomic<u32>,
    r1: atomic<u32>,
  };

  struct ReadResults {
    value: array<ReadResult>,
  };

  // These arrays are combine into 1 buffer because compat mode only supports 4 storage buffers by default.
  struct CombinedData {
    barrier: AtomicMemoryBarrier,
    scratchpad: IndexMemoryScratchpad,
    scratch_locations: IndexMemoryScratchLocations,
  };

  struct StressParamsMemory {
    do_barrier: u32,
    mem_stress: u32,
    mem_stress_iterations: u32,
    mem_stress_pattern: u32,
    pre_stress: u32,
    pre_stress_iterations: u32,
    pre_stress_pattern: u32,
    permute_first: u32,
    permute_second: u32,
    testing_workgroups: u32,
    mem_stride: u32,
    location_offset: u32,
  };
)wgsl";

/**
 * Structure to hold the counts of occurrences of the possible behaviors of a two-thread,
 * four-instruction test.
 * "seq0" means the first invocation's instructions are observed to have occurred before the
 * second invocation's instructions.
 * "seq1" means the second invocation's instructions are observed to have occurred before the
 * first invocation's instructions.
 * "interleaved" means there was an observation of some interleaving of instructions between the
 * two invocations.
 * "weak" means there was an observation of some ordering of instructions that is inconsistent
 * with the WebGPU memory model.
 */
inline constexpr const char kFourBehaviorTestResultStructure[] = R"wgsl(
  struct TestResults {
    seq0: atomic<u32>,
    seq1: atomic<u32>,
    interleaved: atomic<u32>,
    weak: atomic<u32>,
  };
)wgsl";

/**
 * Defines the possible behaviors of a two instruction test. Used to test the behavior of
 * non-atomic memory with barriers and one-thread coherence tests.
 * "seq" means that the expected, sequential behavior occurred.
 * "weak" means that an unexpected, inconsistent behavior occurred.
 */
inline constexpr const char kTwoBehaviorTestResultStructure[] = R"wgsl(
  struct TestResults {
    seq: atomic<u32>,
    weak: atomic<u32>,
  };
)wgsl";

/** Common bindings used in the test shader phase of a test. */
inline constexpr const char kCommonTestShaderBindings[] = R"wgsl(
  @group(0) @binding(1) var<storage, read_write> results : ReadResults;
  @group(0) @binding(2) var<storage, read> shuffled_workgroups : IndexMemory;
  @group(0) @binding(3) var<storage, read_write> combo : CombinedData;
  @group(0) @binding(4) var<uniform> stress_params : StressParamsMemory;
)wgsl";

/** The test-locations binding for a test on atomic memory. */
inline constexpr const char kAtomicTestLocationsBinding[] = R"wgsl(
  @group(0) @binding(0) var<storage, read_write> test_locations : AtomicMemory;
)wgsl";

/** The test-locations binding for a test on non-atomic memory. */
inline constexpr const char kNonAtomicTestLocationsBinding[] = R"wgsl(
  @group(0) @binding(0) var<storage, read_write> test_locations : Memory;
)wgsl";

/** The extra binding for non-atomic texture tests. */
inline constexpr const char kTextureBindings[] = R"wgsl(
@group(1) @binding(0) var texture_locations : texture_storage_2d<r32uint, read_write>;
)wgsl";

/** Bindings used in the result aggregation phase of the test. */
inline constexpr const char kResultShaderBindings[] = R"wgsl(
  @group(0) @binding(0) var<storage, read_write> test_locations : Memory;
  @group(0) @binding(1) var<storage, read_write> read_results : ReadResults;
  @group(0) @binding(2) var<storage, read_write> test_results : TestResults;
  @group(0) @binding(3) var<uniform> stress_params : StressParamsMemory;
)wgsl";

/**
 * For tests that operate on workgroup memory, include this definition. 3584 memory locations is
 * large enough to accommodate the maximum memory size needed per workgroup for testing, which is
 * 256 invocations per workgroup x 2 memory locations x 7 (memStride, or max stride between
 * successive memory locations). Should change to a pipeline overridable constant when possible.
 */
inline constexpr const char kAtomicWorkgroupMemory[] = R"wgsl(
  var<workgroup> wg_test_locations: array<atomic<u32>, 3584>;
)wgsl";

/**
 * For tests that operate on non-atomic workgroup memory, include this definition. 3584 memory
 * locations is large enough to accommodate the maximum memory size needed per workgroup for
 * testing.
 */
inline constexpr const char kNonAtomicWorkgroupMemory[] = R"wgsl(
  var<workgroup> wg_test_locations: array<AccessValueTy, 3584>;
)wgsl";

/**
 * Functions used to calculate memory locations for each invocation, for both testing and result
 * aggregation. The permute function ensures a random permutation based on multiplying and modding
 * by coprime numbers. The stripe workgroup function ensures that invocations coordinating on a
 * test are spread out across different workgroups.
 */
inline constexpr const char kMemoryLocationFunctions[] = R"wgsl(
  fn permute_id(id: u32, factor: u32, mask: u32) -> u32 {
    return (id * factor) % mask;
  }

  fn stripe_workgroup(workgroup_id: u32, local_id: u32) -> u32 {
    return (workgroup_id + 1u + local_id % (stress_params.testing_workgroups - 1u)) % stress_params.testing_workgroups;
  }
)wgsl";

/** Function to convert an index into an equivalent 2D coordinate for the texture. */
inline std::string textureFunctions() {
    return "\n  const kWidth = " + std::to_string(kWidth) + ";\n" +
           R"wgsl(  fn indexToCoord(idx : u32) -> vec2u {
    return vec2u(idx % kWidth, idx / kWidth);
  }
)wgsl";
}

/** Functions that help add stress to the test. */
inline constexpr const char kTestShaderFunctions[] = R"wgsl(
  //Force the invocations in the workgroup to wait for each other, but without the general memory ordering
  // effects of a control barrier. The barrier spins until either all invocations have incremented the atomic
  // variable or 1024 loops have occurred. 1024 was chosen because it gives more time for invocations to enter
  // the barrier but does not overly reduce testing throughput.
  fn spin(limit: u32) {
    var i : u32 = 0u;
    var bar_val : u32 = atomicAdd(&combo.barrier.value[0], 1u);
    loop {
      if (i == 1024u || bar_val >= limit) {
        break;
      }
      bar_val = atomicAdd(&combo.barrier.value[0], 0u);
      i = i + 1u;
    }
  }

  // Perform iterations of stress, depending on the specified pattern. Pattern 0 is store-store, pattern 1 is store-load,
  // pattern 2 is load-store, and pattern 3 is load-load. The extra if condition (if tmpX > 100000u), is used to avoid
  // the compiler optimizing out unused loads, where 100,000 is larger than the maximum number of stress iterations used
  // in any test.
  fn do_stress(iterations: u32, pattern: u32, workgroup_id: u32) {
    let addr = combo.scratch_locations.value[workgroup_id];
    switch(pattern) {
      case 0u: {
        for(var i: u32 = 0u; i < iterations; i = i + 1u) {
          combo.scratchpad.value[addr] = i;
          combo.scratchpad.value[addr] = i + 1u;
        }
      }
      case 1u: {
        for(var i: u32 = 0u; i < iterations; i = i + 1u) {
          combo.scratchpad.value[addr] = i;
          let tmp1: u32 = combo.scratchpad.value[addr];
          if (tmp1 > 100000u) {
            combo.scratchpad.value[addr] = i;
            break;
          }
        }
      }
      case 2u: {
        for(var i: u32 = 0u; i < iterations; i = i + 1u) {
          let tmp1: u32 = combo.scratchpad.value[addr];
          if (tmp1 > 100000u) {
            combo.scratchpad.value[addr] = i;
            break;
          }
          combo.scratchpad.value[addr] = i;
        }
      }
      case 3u: {
        for(var i: u32 = 0u; i < iterations; i = i + 1u) {
          let tmp1: u32 = combo.scratchpad.value[addr];
          if (tmp1 > 100000u) {
            combo.scratchpad.value[addr] = i;
            break;
          }
          let tmp2: u32 = combo.scratchpad.value[addr];
          if (tmp2 > 100000u) {
            combo.scratchpad.value[addr] = i;
            break;
          }
        }
      }
      default: {
      }
    }
  }
)wgsl";

/**
 * Entry point to both test and result shaders. One-dimensional workgroup size is hardcoded
 * until pipeline overridable constants are supported.
 */
inline constexpr const char kShaderEntryPoint[] = R"wgsl(
  // Change to pipeline overridable constant when possible.
  const workgroupXSize = kWorkgroupXSize;
  @compute @workgroup_size(workgroupXSize) fn main(
    @builtin(local_invocation_id) local_invocation_id : vec3<u32>,
    @builtin(workgroup_id) workgroup_id : vec3<u32>) {
)wgsl";

/** All test shaders first calculate the shuffled workgroup. */
inline constexpr const char kTestShaderCommonHeader[] = R"wgsl(
    let shuffled_workgroup = shuffled_workgroups.value[workgroup_id[0]];
    if (shuffled_workgroup < stress_params.testing_workgroups) {
)wgsl";

/**
 * All test shaders must calculate addresses for memory locations used in the test. Not all these
 * addresses are used in every test, but no test uses more than these addresses.
 */
inline constexpr const char kTestShaderCommonCalculations[] = R"wgsl(
  let x_0 = id_0 * stress_params.mem_stride * 2u;
  let y_0 = permute_id(id_0, stress_params.permute_second, total_ids) * stress_params.mem_stride * 2u + stress_params.location_offset;
  let x_1 = id_1 * stress_params.mem_stride * 2u;
  let y_1 = permute_id(id_1, stress_params.permute_second, total_ids) * stress_params.mem_stride * 2u + stress_params.location_offset;
  if (stress_params.pre_stress == 1u) {
    do_stress(stress_params.pre_stress_iterations, stress_params.pre_stress_pattern, shuffled_workgroup);
  }
)wgsl";

/**
 * An inter-workgroup test calculates two sets of memory locations that are guaranteed to be in
 * separate workgroups. If the bounded spin-loop barrier is called, it attempts to wait for all
 * invocations in all workgroups.
 */
inline constexpr const char kInterWorkgroupTestShaderIds[] = R"wgsl(
  let total_ids = workgroupXSize * stress_params.testing_workgroups;
  let id_0 = shuffled_workgroup * workgroupXSize + local_invocation_id[0];
  let new_workgroup = stripe_workgroup(shuffled_workgroup, local_invocation_id[0]);
  let id_1 = new_workgroup * workgroupXSize + permute_id(local_invocation_id[0], stress_params.permute_first, workgroupXSize);
)wgsl";

inline constexpr const char kInterWorkgroupTestShaderBarrier[] = R"wgsl(
  if (stress_params.do_barrier == 1u) {
    spin(workgroupXSize * stress_params.testing_workgroups);
  }
)wgsl";

/**
 * An intra-workgroup test calculates two sets of memory locations that are guaranteed to be in
 * the same workgroup. If the bounded spin-loop barrier is called, it attempts to wait for all
 * invocations in the same workgroup.
 */
inline constexpr const char kIntraWorkgroupTestShaderIds[] = R"wgsl(
  let total_ids = workgroupXSize;
  let id_0 = local_invocation_id[0];
  let id_1 = permute_id(local_invocation_id[0], stress_params.permute_first, workgroupXSize);
)wgsl";

inline constexpr const char kIntraWorkgroupTestShaderBarrier[] = R"wgsl(
  if (stress_params.do_barrier == 1u) {
    spin(workgroupXSize);
  }
)wgsl";

/**
 * Tests that operate on storage memory and communicate with invocations in the same workgroup
 * must offset their locations relative to global memory.
 */
inline constexpr const char kStorageIntraWorkgroupTestShaderCode[] = R"wgsl(
  let total_ids = workgroupXSize;
  let id_0 = local_invocation_id[0];
  let id_1 = permute_id(local_invocation_id[0], stress_params.permute_first, workgroupXSize);
  let x_0 = (shuffled_workgroup * workgroupXSize + id_0) * stress_params.mem_stride * 2u;
  let y_0 = (shuffled_workgroup * workgroupXSize + permute_id(id_0, stress_params.permute_second, total_ids)) * stress_params.mem_stride * 2u + stress_params.location_offset;
  let x_1 = (shuffled_workgroup * workgroupXSize + id_1) * stress_params.mem_stride * 2u;
  let y_1 = (shuffled_workgroup * workgroupXSize + permute_id(id_1, stress_params.permute_second, total_ids)) * stress_params.mem_stride * 2u + stress_params.location_offset;
  if (stress_params.pre_stress == 1u) {
    do_stress(stress_params.pre_stress_iterations, stress_params.pre_stress_pattern, shuffled_workgroup);
  }
  if (stress_params.do_barrier == 1u) {
    spin(workgroupXSize);
  }
)wgsl";

/** All test shaders may perform stress with non-testing threads. */
inline constexpr const char kTestShaderCommonFooter[] = R"wgsl(
    } else if (stress_params.mem_stress == 1u) {
      do_stress(stress_params.mem_stress_iterations, stress_params.mem_stress_pattern, shuffled_workgroup);
    }
  }
)wgsl";

/**
 * All result shaders must calculate memory locations used in the test. Not all these locations
 * are used in every result shader, but no result shader uses more than these locations.
 *
 * Each value read from test_locations is converted from AccessValueTy to u32 before storing it
 * in the read result. This assumes u32(AccessValueTy) is either an identity function u32(u32)
 * or a value-converting overload such as u32(f16).
 */
inline constexpr const char kResultShaderCommonCalculations[] = R"wgsl(
  let id_0 = workgroup_id[0] * workgroupXSize + local_invocation_id[0];
  let x_0 = id_0 * stress_params.mem_stride * 2u;
  let mem_x_0 = u32(test_locations.value[x_0]);
  let r0 = atomicLoad(&read_results.value[id_0].r0);
  let r1 = atomicLoad(&read_results.value[id_0].r1);
)wgsl";

/** Inter-workgroup-specific part of the result shader. */
inline constexpr const char kInterWorkgroupResultShaderCode[] = R"wgsl(
  let total_ids = workgroupXSize * stress_params.testing_workgroups;
  let y_0 = permute_id(id_0, stress_params.permute_second, total_ids) * stress_params.mem_stride * 2u + stress_params.location_offset;
  let mem_y_0 = u32(test_locations.value[y_0]);
)wgsl";

/** Intra-workgroup-specific part of the result shader. */
inline constexpr const char kIntraWorkgroupResultShaderCode[] = R"wgsl(
  let total_ids = workgroupXSize;
  let y_0 = (workgroup_id[0] * workgroupXSize + permute_id(local_invocation_id[0], stress_params.permute_second, total_ids)) * stress_params.mem_stride * 2u + stress_params.location_offset;
  let mem_y_0 = u32(test_locations.value[y_0]);
)wgsl";

/** Ending bracket for result shaders. */
inline constexpr const char kResultShaderCommonFooter[] = R"wgsl(
}
)wgsl";

} // namespace detail

/**
 * Defines the types of possible memory a test is operating on. Used as part of the process of
 * building shader code from its composite parts. The identifiers match the upstream string enum
 * values, so they can be used directly as test parameter values for query identity.
 */
enum class MemoryType {
    /** Atomic memory in the storage address space. */
    AtomicStorageClass,
    /** Non-atomic memory in the storage address space. */
    NonAtomicStorageClass,
    /** Atomic memory in the workgroup address space. */
    AtomicWorkgroupClass,
    /** Non-atomic memory in the workgroup address space. */
    NonAtomicWorkgroupClass,
    /** Non-atomic memory in a texture. */
    NonAtomicTextureClass,
};

inline const char* memoryTypeId(MemoryType type) {
    switch (type) {
        case MemoryType::AtomicStorageClass:
            return "atomic_storage";
        case MemoryType::NonAtomicStorageClass:
            return "non_atomic_storage";
        case MemoryType::AtomicWorkgroupClass:
            return "atomic_workgroup";
        case MemoryType::NonAtomicWorkgroupClass:
            return "non_atomic_workgroup";
        case MemoryType::NonAtomicTextureClass:
            return "non_atomic_texture";
    }
    std::abort();
}

inline MemoryType memoryTypeFromId(std::string_view id) {
    if (id == "atomic_storage") {
        return MemoryType::AtomicStorageClass;
    }
    if (id == "non_atomic_storage") {
        return MemoryType::NonAtomicStorageClass;
    }
    if (id == "atomic_workgroup") {
        return MemoryType::AtomicWorkgroupClass;
    }
    if (id == "non_atomic_workgroup") {
        return MemoryType::NonAtomicWorkgroupClass;
    }
    if (id == "non_atomic_texture") {
        return MemoryType::NonAtomicTextureClass;
    }
    std::abort();
}

/**
 * Defines the relative positions of two invocations coordinating on a test. The identifiers
 * match the upstream string enum values.
 */
enum class TestType {
    /** A test consists of two invocations in different workgroups. */
    InterWorkgroup,
    /** A test consists of two invocations in the same workgroup. */
    IntraWorkgroup,
};

inline const char* testTypeId(TestType type) {
    switch (type) {
        case TestType::InterWorkgroup:
            return "inter_workgroup";
        case TestType::IntraWorkgroup:
            return "intra_workgroup";
    }
    std::abort();
}

inline TestType testTypeFromId(std::string_view id) {
    if (id == "inter_workgroup") {
        return TestType::InterWorkgroup;
    }
    if (id == "intra_workgroup") {
        return TestType::IntraWorkgroup;
    }
    std::abort();
}

/** Defines the number of behaviors a test may have. */
enum class ResultType {
    TwoBehavior,
    FourBehavior,
};

/**
 * Given test code that performs the actual sequence of loads and stores, as well as a memory
 * type and test type, returns a complete test shader (minus the preamble that the
 * MemoryModelTester constructor prepends).
 */
inline std::string buildTestShader(
    const std::string& testCode,
    MemoryType memoryType,
    TestType testType) {
    using namespace detail;
    std::string memoryTypeCode;
    bool isGlobalSpace = false;
    switch (memoryType) {
        case MemoryType::AtomicStorageClass:
            memoryTypeCode = joinShader({kShaderMemStructures,
                                         joinShader({kAtomicTestLocationsBinding, kCommonTestShaderBindings}),
                                         kMemoryLocationFunctions, kTestShaderFunctions,
                                         kShaderEntryPoint, kTestShaderCommonHeader});
            isGlobalSpace = true;
            break;
        case MemoryType::NonAtomicStorageClass:
            memoryTypeCode = joinShader({kShaderMemStructures,
                                         joinShader({kNonAtomicTestLocationsBinding, kCommonTestShaderBindings}),
                                         kMemoryLocationFunctions, kTestShaderFunctions,
                                         kShaderEntryPoint, kTestShaderCommonHeader});
            isGlobalSpace = true;
            break;
        case MemoryType::AtomicWorkgroupClass:
            memoryTypeCode = joinShader({kShaderMemStructures,
                                         joinShader({kAtomicTestLocationsBinding, kCommonTestShaderBindings}),
                                         kAtomicWorkgroupMemory, kMemoryLocationFunctions,
                                         kTestShaderFunctions, kShaderEntryPoint,
                                         kTestShaderCommonHeader});
            break;
        case MemoryType::NonAtomicWorkgroupClass:
            memoryTypeCode = joinShader({kShaderMemStructures,
                                         joinShader({kNonAtomicTestLocationsBinding, kCommonTestShaderBindings}),
                                         kNonAtomicWorkgroupMemory, kMemoryLocationFunctions,
                                         kTestShaderFunctions, kShaderEntryPoint,
                                         kTestShaderCommonHeader});
            break;
        case MemoryType::NonAtomicTextureClass:
            memoryTypeCode = joinShader({kShaderMemStructures,
                                         joinShader({kNonAtomicTestLocationsBinding, kCommonTestShaderBindings}),
                                         kTextureBindings, kMemoryLocationFunctions,
                                         textureFunctions(), kTestShaderFunctions,
                                         kShaderEntryPoint, kTestShaderCommonHeader});
            isGlobalSpace = true;
            break;
    }
    std::string testTypeCode;
    switch (testType) {
        case TestType::InterWorkgroup:
            testTypeCode = joinShader({kInterWorkgroupTestShaderIds, kTestShaderCommonCalculations,
                                       kInterWorkgroupTestShaderBarrier});
            break;
        case TestType::IntraWorkgroup:
            if (isGlobalSpace) {
                testTypeCode = kStorageIntraWorkgroupTestShaderCode;
            } else {
                testTypeCode = joinShader({kIntraWorkgroupTestShaderIds, kTestShaderCommonCalculations,
                                           kIntraWorkgroupTestShaderBarrier});
            }
            break;
    }
    return detail::joinShader({memoryTypeCode, testTypeCode, testCode, kTestShaderCommonFooter});
}

/**
 * Given result code that aggregates the possible behaviors of a test across all instances, as
 * well as a test type and number of behaviors, returns a complete result shader (minus the
 * preamble that the MemoryModelTester constructor prepends).
 */
inline std::string buildResultShader(
    const std::string& resultCode,
    TestType testType,
    ResultType resultType) {
    using namespace detail;
    std::string resultStructure;
    switch (resultType) {
        case ResultType::TwoBehavior:
            resultStructure = kTwoBehaviorTestResultStructure;
            break;
        case ResultType::FourBehavior:
            resultStructure = kFourBehaviorTestResultStructure;
            break;
    }
    const std::string resultShaderCommonCode = joinShader(
        {kShaderMemStructures, kResultShaderBindings, kMemoryLocationFunctions, kShaderEntryPoint});
    std::string testTypeCode;
    switch (testType) {
        case TestType::InterWorkgroup:
            testTypeCode = joinShader({kResultShaderCommonCalculations, kInterWorkgroupResultShaderCode});
            break;
        case TestType::IntraWorkgroup:
            testTypeCode = joinShader({kResultShaderCommonCalculations, kIntraWorkgroupResultShaderCode});
            break;
    }
    return joinShader(
        {resultStructure, resultShaderCommonCode, testTypeCode, resultCode, kResultShaderCommonFooter});
}

} // namespace memory_model
} // namespace cts

#endif // WEBGPU_SHADER_EXECUTION_MEMORY_MODEL_MEMORY_MODEL_SETUP_H_
