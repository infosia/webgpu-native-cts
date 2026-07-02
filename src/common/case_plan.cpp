#include "common/case_plan.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>
#include <utility>

#include "common/query.h"

namespace cts {
namespace {

constexpr std::array<char, 8> kCasePlanMagic = {'W', 'C', 'T', 'S', 'P', 'L', 'A', 'N'};
constexpr uint32_t kCasePlanVersion = 1;

enum class ValueTag : uint8_t {
    Int64 = 1,
    Double = 2,
    Bool = 3,
    String = 4,
    Undefined = 5,
};

std::string testKey(const std::string& file, const std::string& name) {
    std::string key = file;
    key.push_back('\0');
    key += name;
    return key;
}

void writeU8(std::ostream& out, uint8_t value) {
    const char byte = static_cast<char>(value);
    out.write(&byte, 1);
}

void writeU32(std::ostream& out, uint32_t value) {
    char bytes[4]{};
    for (size_t i = 0; i < 4; ++i) {
        bytes[i] = static_cast<char>((value >> (i * 8)) & 0xFF);
    }
    out.write(bytes, sizeof(bytes));
}

void writeU64(std::ostream& out, uint64_t value) {
    char bytes[8]{};
    for (size_t i = 0; i < 8; ++i) {
        bytes[i] = static_cast<char>((value >> (i * 8)) & 0xFF);
    }
    out.write(bytes, sizeof(bytes));
}

void writeString(std::ostream& out, const std::string& value) {
    writeU64(out, static_cast<uint64_t>(value.size()));
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
}

void writeValue(std::ostream& out, const Value& value) {
    std::visit(
        [&](const auto& stored) {
            using T = std::decay_t<decltype(stored)>;
            if constexpr (std::is_same_v<T, int64_t>) {
                uint64_t bits = 0;
                std::memcpy(&bits, &stored, sizeof(bits));
                writeU8(out, static_cast<uint8_t>(ValueTag::Int64));
                writeU64(out, bits);
            } else if constexpr (std::is_same_v<T, double>) {
                uint64_t bits = 0;
                std::memcpy(&bits, &stored, sizeof(bits));
                writeU8(out, static_cast<uint8_t>(ValueTag::Double));
                writeU64(out, bits);
            } else if constexpr (std::is_same_v<T, bool>) {
                writeU8(out, static_cast<uint8_t>(ValueTag::Bool));
                writeU8(out, stored ? 1 : 0);
            } else if constexpr (std::is_same_v<T, std::string>) {
                writeU8(out, static_cast<uint8_t>(ValueTag::String));
                writeString(out, stored);
            } else if constexpr (std::is_same_v<T, Value::Undefined>) {
                writeU8(out, static_cast<uint8_t>(ValueTag::Undefined));
            }
        },
        value.data());
}

void writeParamRecord(std::ostream& out, const ParamRecord& record) {
    writeU64(out, static_cast<uint64_t>(record.size()));
    for (const auto& [key, value] : record) {
        writeString(out, key);
        writeValue(out, value);
    }
}

void writeParamRecordVector(std::ostream& out, const std::vector<ParamRecord>& records) {
    writeU64(out, static_cast<uint64_t>(records.size()));
    for (const ParamRecord& record : records) {
        writeParamRecord(out, record);
    }
}

class CasePlanReader {
  public:
    CasePlanReader(const std::string& path, std::istream& in)
        : path_(path), in_(in) {}

    void setCaseContext(std::string file, std::string name) {
        file_ = std::move(file);
        name_ = std::move(name);
    }

    [[noreturn]] void fail(const std::string& message) const {
        std::string full = "corrupt case-plan file " + path_;
        if (!file_.empty() || !name_.empty()) {
            full += " entry " + file_ + ":" + name_;
        }
        full += ": " + message;
        throw std::runtime_error(full);
    }

    void readBytes(char* data, size_t size) {
        in_.read(data, static_cast<std::streamsize>(size));
        if (!in_) {
            fail("unexpected end of file");
        }
    }

    uint8_t readU8() {
        char byte = 0;
        readBytes(&byte, 1);
        return static_cast<uint8_t>(byte);
    }

    uint32_t readU32() {
        char bytes[4]{};
        readBytes(bytes, sizeof(bytes));
        uint32_t value = 0;
        for (size_t i = 0; i < 4; ++i) {
            value |= static_cast<uint32_t>(static_cast<unsigned char>(bytes[i])) << (i * 8);
        }
        return value;
    }

    uint64_t readU64() {
        char bytes[8]{};
        readBytes(bytes, sizeof(bytes));
        uint64_t value = 0;
        for (size_t i = 0; i < 8; ++i) {
            value |= static_cast<uint64_t>(static_cast<unsigned char>(bytes[i])) << (i * 8);
        }
        return value;
    }

    std::string readString() {
        const uint64_t size64 = readU64();
        if (size64 > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
            fail("string length does not fit in size_t");
        }
        std::string value(static_cast<size_t>(size64), '\0');
        if (!value.empty()) {
            readBytes(value.data(), value.size());
        }
        return value;
    }

  private:
    const std::string& path_;
    std::istream& in_;
    std::string file_;
    std::string name_;
};

size_t readCount(CasePlanReader& reader, const char* label) {
    const uint64_t count64 = reader.readU64();
    if (count64 > static_cast<uint64_t>((std::numeric_limits<size_t>::max)())) {
        reader.fail(std::string(label) + " count does not fit in size_t");
    }
    return static_cast<size_t>(count64);
}

Value readValue(CasePlanReader& reader) {
    const uint8_t rawTag = reader.readU8();
    const auto tag = static_cast<ValueTag>(rawTag);
    switch (tag) {
    case ValueTag::Int64: {
        const uint64_t bits = reader.readU64();
        int64_t value = 0;
        std::memcpy(&value, &bits, sizeof(value));
        return Value(value);
    }
    case ValueTag::Double: {
        const uint64_t bits = reader.readU64();
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof(value));
        return Value(value);
    }
    case ValueTag::Bool: {
        const uint8_t byte = reader.readU8();
        if (byte > 1) {
            reader.fail("invalid bool payload");
        }
        return Value(byte != 0);
    }
    case ValueTag::String:
        return Value(reader.readString());
    case ValueTag::Undefined:
        return Value::undef();
    }
    reader.fail("unknown value type tag " + std::to_string(rawTag));
}

ParamRecord readParamRecord(CasePlanReader& reader) {
    ParamRecord record;
    const size_t count = readCount(reader, "parameter");
    record.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        std::string key = reader.readString();
        Value value = readValue(reader);
        record.emplace_back(std::move(key), std::move(value));
    }
    return record;
}

std::vector<ParamRecord> readParamRecordVector(CasePlanReader& reader) {
    std::vector<ParamRecord> records;
    const size_t count = readCount(reader, "subcase");
    records.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        records.push_back(readParamRecord(reader));
    }
    return records;
}

std::unordered_map<std::string, const TestSpec*> buildTestLookup() {
    std::unordered_map<std::string, const TestSpec*> lookup;
    for (const SpecFile& file : Registry::instance().files()) {
        for (const TestSpec& test : file.tests) {
            lookup.emplace(testKey(file.path, test.name), &test);
        }
    }
    return lookup;
}

} // namespace

void serializeCasePlan(const std::string& path, const std::vector<CaseRun>& cases) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("failed to open case-plan file for writing: " + path);
    }

    out.write(kCasePlanMagic.data(), static_cast<std::streamsize>(kCasePlanMagic.size()));
    writeU32(out, kCasePlanVersion);
    writeU64(out, static_cast<uint64_t>(cases.size()));
    for (const CaseRun& run : cases) {
        if (run.test == nullptr) {
            throw std::runtime_error("cannot serialize case-plan entry with null test: " + run.file);
        }
        writeString(out, run.file);
        writeString(out, run.test->name);
        writeParamRecord(out, run.params);
        writeParamRecordVector(out, run.subcases);
    }
    out.flush();
    if (!out) {
        throw std::runtime_error("failed to write case-plan file: " + path);
    }
}

std::vector<CaseRun> loadCasePlan(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("failed to open case-plan file for reading: " + path);
    }

    CasePlanReader reader(path, in);
    std::array<char, kCasePlanMagic.size()> magic{};
    reader.readBytes(magic.data(), magic.size());
    if (magic != kCasePlanMagic) {
        reader.fail("magic mismatch");
    }
    const uint32_t version = reader.readU32();
    if (version != kCasePlanVersion) {
        reader.fail("unsupported version " + std::to_string(version));
    }

    const auto lookup = buildTestLookup();
    const size_t caseCount = readCount(reader, "case");
    std::vector<CaseRun> cases;
    cases.reserve(caseCount);
    for (size_t i = 0; i < caseCount; ++i) {
        std::string file = reader.readString();
        std::string name = reader.readString();
        reader.setCaseContext(file, name);
        ParamRecord params = readParamRecord(reader);
        std::vector<ParamRecord> subcases = readParamRecordVector(reader);

        const auto found = lookup.find(testKey(file, name));
        if (found == lookup.end()) {
            reader.fail("unknown test");
        }
        std::string query = caseQuery(file, name, params);
        cases.push_back(CaseRun{
            file,
            found->second,
            std::move(params),
            std::move(subcases),
            std::move(query),
        });
    }

    if (in.peek() != std::char_traits<char>::eof()) {
        reader.fail("trailing data");
    }
    if (in.bad()) {
        reader.fail("stream error after reading plan");
    }
    return cases;
}

} // namespace cts
