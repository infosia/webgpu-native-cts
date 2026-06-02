// Ported from gpuweb/cts src/webgpu/api/operation/queue/writeBuffer.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/util/write_buffer.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,operation,queue,writeBuffer",
    "Operation tests for GPUQueue.writeBuffer().");

struct WriteSpec {
    uint64_t bufferOffset = 0;
    std::vector<int64_t> data;
    WriteBufferArrayType arrayType = WriteBufferArrayType::U8;
    bool useArrayBuffer = false;
    int dataOffset = -1;
    int dataSize = -1;
};

std::vector<int64_t> testData() {
    std::vector<int64_t> data;
    data.reserve(16);
    for (int64_t i = 0; i < 16; ++i) {
        data.push_back(i);
    }
    return data;
}

std::vector<Value> arrayTypeValues() {
    return {
        Value(static_cast<int64_t>(WriteBufferArrayType::U8)),
        Value(static_cast<int64_t>(WriteBufferArrayType::U16)),
        Value(static_cast<int64_t>(WriteBufferArrayType::U32)),
        Value(static_cast<int64_t>(WriteBufferArrayType::I8)),
        Value(static_cast<int64_t>(WriteBufferArrayType::I16)),
        Value(static_cast<int64_t>(WriteBufferArrayType::I32)),
        Value(static_cast<int64_t>(WriteBufferArrayType::F32)),
        Value(static_cast<int64_t>(WriteBufferArrayType::F64)),
    };
}

size_t writeOffsetUnit(const WriteSpec& write) {
    return write.useArrayBuffer ? 1 : bytesPerElement(write.arrayType);
}

size_t writeDataOffsetBytes(const WriteSpec& write) {
    return static_cast<size_t>(write.dataOffset < 0 ? 0 : write.dataOffset) * writeOffsetUnit(write);
}

size_t writeBytesWritten(const WriteSpec& write, size_t encodedSize) {
    const size_t offsetBytes = writeDataOffsetBytes(write);
    size_t bytesWritten = encodedSize - offsetBytes;
    if (write.dataSize >= 0) {
        bytesWritten = std::min(bytesWritten, static_cast<size_t>(write.dataSize) * writeOffsetUnit(write));
    }
    return bytesWritten;
}

size_t calculateRequiredBufferSize(const std::vector<WriteSpec>& writes) {
    size_t bufferSize = 0;
    for (const WriteSpec& write : writes) {
        const std::vector<uint8_t> srcBytes = encodeWriteBufferData(write.data, write.arrayType);
        const size_t bytesWritten = writeBytesWritten(write, srcBytes.size());
        bufferSize = std::max(bufferSize, static_cast<size_t>(write.bufferOffset) + bytesWritten);
    }
    return align4(bufferSize);
}

void testWriteBuffer(GpuTest& t, const std::vector<WriteSpec>& writes) {
    const size_t bufferSize = calculateRequiredBufferSize(writes);
    std::vector<uint8_t> expected(bufferSize, 0xff);
    WGPUBuffer buffer = t.makeBufferWithContents(
        expected.data(),
        expected.size(),
        WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst);

    for (const WriteSpec& write : writes) {
        const std::vector<uint8_t> srcBytes = encodeWriteBufferData(write.data, write.arrayType);
        const size_t offsetBytes = writeDataOffsetBytes(write);
        const size_t bytesWritten = writeBytesWritten(write, srcBytes.size());
        const uint8_t* src = srcBytes.data() + offsetBytes;
        t.queueWriteBuffer(buffer, write.bufferOffset, src, bytesWritten);
        std::copy(src, src + bytesWritten, expected.begin() + static_cast<std::ptrdiff_t>(write.bufferOffset));
    }

    t.expectGPUBufferValuesEqual(buffer, expected.data(), expected.size());
}

std::vector<WriteSpec> writeSubcase(int index) {
    const std::vector<int64_t> data = testData();
    switch (index) {
        case 0:
            return {
                WriteSpec{0, data, WriteBufferArrayType::U32, false, 2, 2},
                WriteSpec{2 * 4, data, WriteBufferArrayType::U32, false, 0, 2},
            };
        case 1:
            return {
                WriteSpec{0, {0, 1, 2, 3}, WriteBufferArrayType::U8, false},
                WriteSpec{4, {4, 5, 6, 7}, WriteBufferArrayType::U8, false},
            };
        case 2:
            return {
                WriteSpec{0, data, WriteBufferArrayType::U8, false},
                WriteSpec{4, {0}, WriteBufferArrayType::U32, false},
            };
        case 3:
            return {
                WriteSpec{0, data, WriteBufferArrayType::U32, true, 2, 4 * 4},
                WriteSpec{4, {0x04030201}, WriteBufferArrayType::U32, true},
            };
        case 4:
            return {
                WriteSpec{0, data, WriteBufferArrayType::U8, false},
                WriteSpec{0, {}, WriteBufferArrayType::U8, false},
            };
        case 5:
            return {WriteSpec{0, {}, WriteBufferArrayType::U8, false}};
        case 6: {
            std::vector<int64_t> unaligned = {0x77};
            unaligned.insert(unaligned.end(), data.begin(), data.end());
            return {WriteSpec{0, unaligned, WriteBufferArrayType::U8, false, 1}};
        }
        case 7:
            return {
                WriteSpec{0, {0x05050505, 0x05050505, 0x05050505, 0x05050505, 0x05050505}, WriteBufferArrayType::U32, false},
                WriteSpec{0, {0x04040404, 0x04040404, 0x04040404, 0x04040404}, WriteBufferArrayType::U32, false},
                WriteSpec{0, {0x03030303, 0x03030303, 0x03030303}, WriteBufferArrayType::U32, false},
                WriteSpec{0, {0x02020202, 0x02020202}, WriteBufferArrayType::U32, false},
                WriteSpec{0, {0x01010101}, WriteBufferArrayType::U32, false},
            };
        default:
            std::abort();
    }
}

CTS_TEST(g, "array_types")
    .desc("Tests that writeBuffer correctly handles different typed arrays and ArrayBuffer.")
    .params([](ParamsBuilder u) {
        return u.combine("arrayType", arrayTypeValues())
            .combine("useArrayBuffer", {false, true});
    })
    .fn([](GpuTest& t) {
        const auto arrayType = static_cast<WriteBufferArrayType>(t.param<int64_t>("arrayType"));
        testWriteBuffer(t, {
            WriteSpec{0, testData(), arrayType, t.param<bool>("useArrayBuffer"), 1, 8},
        });
    });

CTS_TEST(g, "multiple_writes_at_different_offsets_and_sizes")
    .desc("Tests writeBuffer with multiple offsets, sizes, empty writes, and overlapping writes.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("subcase", {0, 1, 2, 3, 4, 5, 6, 7});
    })
    .fn([](GpuTest& t) {
        testWriteBuffer(t, writeSubcase(t.param<int>("subcase")));
    });

} // namespace
