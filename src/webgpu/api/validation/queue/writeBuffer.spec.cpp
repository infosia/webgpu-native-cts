// Ported from gpuweb/cts src/webgpu/api/validation/queue/writeBuffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// Note: The upstream 'ranges' test exercises TypedArray / ArrayBuffer element-based
// offset+size semantics that have no direct equivalent in the C WebGPU API.  The port
// tests the same underlying GPU validation rules (bufferOffset 4-byte alignment, copy
// size multiple-of-4, fits-in-destination) using byte-level parameters (elementSize 1,
// 2, 4, 8 as a scaling factor, covering each TypedArray element-size class).
// JS-side "shouldThrow('OperationError')" checks for source-data-out-of-range are
// omitted because the C API has no typed-array bounds — the caller owns the pointer.

#include <cstdint>
#include <cstring>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Test group — upstream fixture is AllFeaturesMaxLimitsGPUTest.
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,queue,writeBuffer",
    "Tests writeBuffer validation.");

// Helper: create a COPY_DST buffer of `size` bytes.
static WGPUBuffer makeCopyDstBuffer(AllFeaturesMaxLimitsGpuTest& t, uint64_t size) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = WGPUBufferUsage_CopyDst;
    return t.createBufferTracked(desc);
}

// ---------------------------------------------------------------------------
// buffer_state
// Tests calling writeBuffer with {valid, invalid, destroyed} buffer.
// ---------------------------------------------------------------------------
CTS_TEST(g, "buffer_state")
    .desc(
        "Test that the buffer used for GPUQueue.writeBuffer() must be valid. Tests calling "
        "writeBuffer with {valid, invalid, destroyed} buffer.")
    .params([](ParamsBuilder u) {
        return u.combine("bufferState", resourceStateValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ResourceState state = parseResourceState(t.param<std::string>("bufferState"));

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferWithState(state, desc);

        const bool valid = (state == ResourceState::Valid);
        std::vector<uint8_t> data(16, 0);

        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, 0, data.data(), data.size());
        }, !valid);
    });

// ---------------------------------------------------------------------------
// ranges
// Tests that the data ranges given to GPUQueue.writeBuffer() are properly
// validated.  The C API works entirely in bytes, so we parameterise over
// elementSize (1, 2, 4, 8) to cover all TypedArray element-size classes from
// the upstream test.  For each elementSize the same structural checks are run:
//   - bufferOffset must be 4-byte aligned
//   - copy size must be a multiple of 4 bytes
//   - bufferOffset + copySize must be <= buffer.size
//
// JS shouldThrow('OperationError') cases that test source-array bounds are
// omitted (see file header note).
// ---------------------------------------------------------------------------
CTS_TEST(g, "ranges")
    .desc(
        "Tests that the data ranges given to GPUQueue.writeBuffer() are properly validated. "
        "Covers bufferOffset alignment, copy-size alignment, and fits-in-destination rules. "
        "Parameterised over elementSize (1, 2, 4, 8) to mirror each TypedArray element-size "
        "class from the upstream test.")
    .params([](ParamsBuilder u) {
        // elementSize: each value corresponds to a TypedArray element-size class:
        //   1 -> Uint8Array / Int8Array / Uint8ClampedArray
        //   2 -> Uint16Array / Int16Array
        //   4 -> Uint32Array / Int32Array / Float32Array
        //   8 -> Float64Array / BigInt64Array / BigUint64Array
        return u.beginSubcases().combine(
            "elementSize", {Value(1), Value(2), Value(4), Value(8)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint64_t es = static_cast<uint64_t>(t.param<int>("elementSize"));

        // Upstream: bufferSize = 16 * elementSize
        const uint64_t bufferSize = 16 * es;
        WGPUBuffer buffer = makeCopyDstBuffer(t, bufferSize);

        // Upstream array sizes (in bytes):
        //   arraySm = 8 * es, arrayMd = 16 * es, arrayLg = 32 * es
        const uint64_t smBytes = 8 * es;
        const uint64_t mdBytes = 16 * es;
        const uint64_t lgBytes = 32 * es;

        // Backing data (zeroed).  We always use lgBytes as the max.
        std::vector<uint8_t> data(static_cast<size_t>(lgBytes), 0);
        const void* ptr = data.data();

        // -------------------------------------------------------------------
        // elementSize < 4 branch (es == 1 or es == 2)
        // -------------------------------------------------------------------
        if (es < 4) {
            const uint64_t array15Bytes = 15 * es;

            // Writing the full array15 buffer: size = 15*es bytes.
            // es=1: 15 bytes (not mult of 4) -> error
            // es=2: 30 bytes (not mult of 4) -> error
            t.expectValidationError([&] {
                wgpuQueueWriteBuffer(t.queue(), buffer, 0, ptr, static_cast<size_t>(array15Bytes));
            }, true);

            // Upstream: writeBuffer(buffer, 0, array15, 3)
            //   TypedArray: offset 3 elements => 3*es bytes, copySize = array15Bytes - 3*es = 12*es
            //   es=1: offset=0, src+3, copy=12 -> OK (4-byte aligned, fits in 16-byte buffer)
            //   es=2: offset=0, src+6, copy=24 -> fits in 32-byte buffer, OK
            // In C we replicate by calling with offset=0 and size=12*es.
            {
                const uint64_t copySize = 12 * es;
                wgpuQueueWriteBuffer(t.queue(), buffer, 0,
                    static_cast<const uint8_t*>(ptr) + 3 * es,
                    static_cast<size_t>(copySize));
            }

            // Upstream: writeBuffer(buffer, 0, arrayMd, 3) -> copy 13*es bytes -> not mult of 4
            // es=1: 13 bytes -> error; es=2: 26 bytes -> error
            {
                const uint64_t copySize = mdBytes - 3 * es;  // 13*es
                t.expectValidationError([&] {
                    wgpuQueueWriteBuffer(t.queue(), buffer, 0,
                        static_cast<const uint8_t*>(ptr) + 3 * es,
                        static_cast<size_t>(copySize));
                }, true);
            }

            // Upstream: writeBuffer(buffer, 0, arraySm, 0, 7) -> 7 elements = 7*es bytes
            // es=1: 7 bytes -> error; es=2: 14 bytes -> error
            {
                const uint64_t copySize = 7 * es;
                t.expectValidationError([&] {
                    wgpuQueueWriteBuffer(t.queue(), buffer, 0, ptr, static_cast<size_t>(copySize));
                }, true);
            }
        }

        // -------------------------------------------------------------------
        // Writing the full buffer without offsets.
        // arraySm (8*es bytes) fits in bufferSize (16*es); OK.
        // arrayMd (16*es bytes) fits exactly; OK.
        // arrayLg (32*es bytes) exceeds bufferSize; error.
        // -------------------------------------------------------------------
        wgpuQueueWriteBuffer(t.queue(), buffer, 0, ptr, static_cast<size_t>(smBytes));
        wgpuQueueWriteBuffer(t.queue(), buffer, 0, ptr, static_cast<size_t>(mdBytes));
        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, 0, ptr, static_cast<size_t>(lgBytes));
        }, true);

        // -------------------------------------------------------------------
        // Writing the full arraySm with a 4-byte aligned buffer offset of 8.
        // bufferOffset=8, copySize=8*es -> 8 + 8*es <= 16*es? Yes when es>=1.
        // Writing arrayMd (16*es) from bufferOffset=8: 8 + 16*es > 16*es -> error.
        // -------------------------------------------------------------------
        wgpuQueueWriteBuffer(t.queue(), buffer, 8, ptr, static_cast<size_t>(smBytes));
        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, 8, ptr, static_cast<size_t>(mdBytes));
        }, true);

        // -------------------------------------------------------------------
        // Writing the full arraySm with a non-4-byte-aligned buffer offset of 3.
        // bufferOffset=3 is not 4-byte aligned -> error.
        // -------------------------------------------------------------------
        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, 3, ptr, static_cast<size_t>(smBytes));
        }, true);

        // -------------------------------------------------------------------
        // Writing remainder of buffer from data offset.
        // Upstream: writeBuffer(buffer, 0, arraySm, 4) => skip first 4 elements,
        //   copy (8-4)*es = 4*es bytes from ptr+4*es; OK.
        // Upstream: writeBuffer(buffer, 0, arrayMd, 4) => copy (16-4)*es = 12*es bytes; OK (fits in 16*es buffer).
        // Upstream: writeBuffer(buffer, 0, arrayLg, 4) => copy (32-4)*es = 28*es bytes > 16*es -> error.
        // -------------------------------------------------------------------
        wgpuQueueWriteBuffer(t.queue(), buffer, 0,
            static_cast<const uint8_t*>(ptr) + 4 * es,
            static_cast<size_t>((8 - 4) * es));
        wgpuQueueWriteBuffer(t.queue(), buffer, 0,
            static_cast<const uint8_t*>(ptr) + 4 * es,
            static_cast<size_t>((16 - 4) * es));
        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, 0,
                static_cast<const uint8_t*>(ptr) + 4 * es,
                static_cast<size_t>((32 - 4) * es));
        }, true);

        // -------------------------------------------------------------------
        // Writing a larger array from an offset that lets it fit.
        // Upstream: writeBuffer(buffer, 0, arrayLg, 16) => copy (32-16)*es = 16*es bytes; fits exactly.
        // -------------------------------------------------------------------
        wgpuQueueWriteBuffer(t.queue(), buffer, 0,
            static_cast<const uint8_t*>(ptr) + 16 * es,
            static_cast<size_t>(16 * es));

        // -------------------------------------------------------------------
        // Writing with both a data offset and size.
        // Upstream: writeBuffer(buffer, 0, arraySm, 4, 4) => copy 4*es bytes from ptr+4*es; OK.
        // -------------------------------------------------------------------
        wgpuQueueWriteBuffer(t.queue(), buffer, 0,
            static_cast<const uint8_t*>(ptr) + 4 * es,
            static_cast<size_t>(4 * es));

        // -------------------------------------------------------------------
        // Writing zero bytes at the end of the buffer.
        // bufferOffset=bufferSize, copySize=0 -> OK (0 bytes, offset is at boundary).
        // -------------------------------------------------------------------
        wgpuQueueWriteBuffer(t.queue(), buffer, bufferSize, ptr, 0);

        // -------------------------------------------------------------------
        // Writing with a buffer offset out of range of buffer size.
        // bufferOffset=bufferSize+4, copySize=0 -> offset > bufferSize -> error.
        // -------------------------------------------------------------------
        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, bufferSize + 4, ptr, 0);
        }, true);

        // -------------------------------------------------------------------
        // Writing zero bytes from the end of the data region.
        // Upstream: writeBuffer(buffer, 0, arraySm, 8, 0) => from ptr+8*es, 0 bytes; OK.
        // -------------------------------------------------------------------
        wgpuQueueWriteBuffer(t.queue(), buffer, 0,
            static_cast<const uint8_t*>(ptr) + 8 * es,
            0);
    });

// ---------------------------------------------------------------------------
// usages
// Tests calling writeBuffer with buffer that is missing COPY_DST usage.
// ---------------------------------------------------------------------------
CTS_TEST(g, "usages")
    .desc(
        "Tests calling writeBuffer with the buffer missed COPY_DST usage. "
        "buffer {with, without} COPY_DST usage.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"usage", static_cast<int64_t>(WGPUBufferUsage_CopyDst)},         {"_valid", true}},
            ParamRecord{{"usage", static_cast<int64_t>(WGPUBufferUsage_Storage)},          {"_valid", false}},
            ParamRecord{{"usage", static_cast<int64_t>(WGPUBufferUsage_Storage | WGPUBufferUsage_CopySrc)}, {"_valid", false}},
            ParamRecord{{"usage", static_cast<int64_t>(WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst)}, {"_valid", true}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUBufferUsage usage = static_cast<WGPUBufferUsage>(t.param<int64_t>("usage"));
        const bool valid = t.param<bool>("_valid");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = usage;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        std::vector<uint8_t> data(16, 0);

        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, 0, data.data(), data.size());
        }, !valid);
    });

// ---------------------------------------------------------------------------
// buffer,device_mismatch
// Tests writeBuffer cannot be called with a buffer created from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "buffer,device_mismatch")
    .desc("Tests writeBuffer cannot be called with a buffer created from another device.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = mismatched
            ? t.createBufferOnMismatchedDevice(desc)
            : t.createBufferTracked(desc);

        std::vector<uint8_t> data(16, 0);

        t.expectValidationError([&] {
            wgpuQueueWriteBuffer(t.queue(), buffer, 0, data.data(), data.size());
        }, mismatched);
    });

} // namespace
