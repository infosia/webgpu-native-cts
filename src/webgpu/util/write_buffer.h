#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace cts {

enum class WriteBufferArrayType {
    U8,
    U16,
    U32,
    I8,
    I16,
    I32,
    F32,
    F64,
};

inline size_t bytesPerElement(WriteBufferArrayType type) {
    switch (type) {
        case WriteBufferArrayType::U8:
        case WriteBufferArrayType::I8:
            return 1;
        case WriteBufferArrayType::U16:
        case WriteBufferArrayType::I16:
            return 2;
        case WriteBufferArrayType::U32:
        case WriteBufferArrayType::I32:
        case WriteBufferArrayType::F32:
            return 4;
        case WriteBufferArrayType::F64:
            return 8;
    }
    return 1;
}

inline void appendLittleEndian(std::vector<uint8_t>& out, uint64_t value, size_t byteCount) {
    for (size_t i = 0; i < byteCount; ++i) {
        out.push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xff));
    }
}

inline std::vector<uint8_t> encodeWriteBufferData(const std::vector<int64_t>& data, WriteBufferArrayType type) {
    std::vector<uint8_t> bytes;
    bytes.reserve(data.size() * bytesPerElement(type));
    for (int64_t value : data) {
        switch (type) {
            case WriteBufferArrayType::U8:
            case WriteBufferArrayType::I8:
                appendLittleEndian(bytes, static_cast<uint64_t>(value), 1);
                break;
            case WriteBufferArrayType::U16:
            case WriteBufferArrayType::I16:
                appendLittleEndian(bytes, static_cast<uint64_t>(value), 2);
                break;
            case WriteBufferArrayType::U32:
            case WriteBufferArrayType::I32:
                appendLittleEndian(bytes, static_cast<uint64_t>(value), 4);
                break;
            case WriteBufferArrayType::F32: {
                const float f = static_cast<float>(value);
                uint32_t bits = 0;
                static_assert(sizeof(bits) == sizeof(f));
                std::memcpy(&bits, &f, sizeof(bits));
                appendLittleEndian(bytes, bits, 4);
                break;
            }
            case WriteBufferArrayType::F64: {
                const double d = static_cast<double>(value);
                uint64_t bits = 0;
                static_assert(sizeof(bits) == sizeof(d));
                std::memcpy(&bits, &d, sizeof(bits));
                appendLittleEndian(bytes, bits, 8);
                break;
            }
        }
    }
    return bytes;
}

inline size_t align4(size_t value) {
    return (value + 3u) & ~size_t{3u};
}

} // namespace cts
