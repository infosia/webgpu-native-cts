// Ported from gpuweb/cts src/webgpu/api/operation/vertex_state/correctness.spec.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa
//
// Parameter identity: ValueTestVariant and array params are not representable as a `Value`. The
// ones that are subcase params are encoded as strings ("mult,add" for variants, comma-separated
// step modes for stepModes). They do not take part in the case query.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,vertex_state,correctness",
    "TODO: Test more corner case values for Float16 / Float32 (INF, NaN, ...) and reduce the\n"
    "float tolerance.");

// ------------------------------------------------------------------
// kVertexFormatInfo
// ------------------------------------------------------------------
enum class VertexFormatType { Uint, Sint, Unorm, Snorm, Float };

struct VertexFormatInfo {
    const char* name;
    WGPUVertexFormat format;
    // Bytes per component; 0 for upstream's 'packed'.
    uint32_t bytesPerComponent;
    VertexFormatType type;
    uint32_t componentCount;
    uint32_t byteSize;
};

// Stands in for an enumerator that this backend's webgpu.h does not have.
constexpr WGPUVertexFormat kVertexFormatNotInHeader = WGPUVertexFormat_Force32;

constexpr std::array<VertexFormatInfo, 42> kVertexFormats = {{
    {"uint8", WGPUVertexFormat_Uint8, 1, VertexFormatType::Uint, 1, 1},
    {"uint8x2", WGPUVertexFormat_Uint8x2, 1, VertexFormatType::Uint, 2, 2},
    {"uint8x4", WGPUVertexFormat_Uint8x4, 1, VertexFormatType::Uint, 4, 4},
    {"sint8", WGPUVertexFormat_Sint8, 1, VertexFormatType::Sint, 1, 1},
    {"sint8x2", WGPUVertexFormat_Sint8x2, 1, VertexFormatType::Sint, 2, 2},
    {"sint8x4", WGPUVertexFormat_Sint8x4, 1, VertexFormatType::Sint, 4, 4},
    {"unorm8", WGPUVertexFormat_Unorm8, 1, VertexFormatType::Unorm, 1, 1},
    {"unorm8x2", WGPUVertexFormat_Unorm8x2, 1, VertexFormatType::Unorm, 2, 2},
    {"unorm8x4", WGPUVertexFormat_Unorm8x4, 1, VertexFormatType::Unorm, 4, 4},
    {"snorm8", WGPUVertexFormat_Snorm8, 1, VertexFormatType::Snorm, 1, 1},
    {"snorm8x2", WGPUVertexFormat_Snorm8x2, 1, VertexFormatType::Snorm, 2, 2},
    {"snorm8x4", WGPUVertexFormat_Snorm8x4, 1, VertexFormatType::Snorm, 4, 4},
    {"uint16", WGPUVertexFormat_Uint16, 2, VertexFormatType::Uint, 1, 2},
    {"uint16x2", WGPUVertexFormat_Uint16x2, 2, VertexFormatType::Uint, 2, 4},
    {"uint16x4", WGPUVertexFormat_Uint16x4, 2, VertexFormatType::Uint, 4, 8},
    {"sint16", WGPUVertexFormat_Sint16, 2, VertexFormatType::Sint, 1, 2},
    {"sint16x2", WGPUVertexFormat_Sint16x2, 2, VertexFormatType::Sint, 2, 4},
    {"sint16x4", WGPUVertexFormat_Sint16x4, 2, VertexFormatType::Sint, 4, 8},
    {"unorm16", WGPUVertexFormat_Unorm16, 2, VertexFormatType::Unorm, 1, 2},
    {"unorm16x2", WGPUVertexFormat_Unorm16x2, 2, VertexFormatType::Unorm, 2, 4},
    {"unorm16x4", WGPUVertexFormat_Unorm16x4, 2, VertexFormatType::Unorm, 4, 8},
    {"snorm16", WGPUVertexFormat_Snorm16, 2, VertexFormatType::Snorm, 1, 2},
    {"snorm16x2", WGPUVertexFormat_Snorm16x2, 2, VertexFormatType::Snorm, 2, 4},
    {"snorm16x4", WGPUVertexFormat_Snorm16x4, 2, VertexFormatType::Snorm, 4, 8},
    {"float16", WGPUVertexFormat_Float16, 2, VertexFormatType::Float, 1, 2},
    {"float16x2", WGPUVertexFormat_Float16x2, 2, VertexFormatType::Float, 2, 4},
    {"float16x4", WGPUVertexFormat_Float16x4, 2, VertexFormatType::Float, 4, 8},
    {"float32", WGPUVertexFormat_Float32, 4, VertexFormatType::Float, 1, 4},
    {"float32x2", WGPUVertexFormat_Float32x2, 4, VertexFormatType::Float, 2, 8},
    {"float32x3", WGPUVertexFormat_Float32x3, 4, VertexFormatType::Float, 3, 12},
    {"float32x4", WGPUVertexFormat_Float32x4, 4, VertexFormatType::Float, 4, 16},
    {"uint32", WGPUVertexFormat_Uint32, 4, VertexFormatType::Uint, 1, 4},
    {"uint32x2", WGPUVertexFormat_Uint32x2, 4, VertexFormatType::Uint, 2, 8},
    {"uint32x3", WGPUVertexFormat_Uint32x3, 4, VertexFormatType::Uint, 3, 12},
    {"uint32x4", WGPUVertexFormat_Uint32x4, 4, VertexFormatType::Uint, 4, 16},
    {"sint32", WGPUVertexFormat_Sint32, 4, VertexFormatType::Sint, 1, 4},
    {"sint32x2", WGPUVertexFormat_Sint32x2, 4, VertexFormatType::Sint, 2, 8},
    {"sint32x3", WGPUVertexFormat_Sint32x3, 4, VertexFormatType::Sint, 3, 12},
    {"sint32x4", WGPUVertexFormat_Sint32x4, 4, VertexFormatType::Sint, 4, 16},
    {"unorm10-10-10-2", WGPUVertexFormat_Unorm10_10_10_2, 0, VertexFormatType::Unorm, 4, 4},
    {"unorm8x4-bgra", WGPUVertexFormat_Unorm8x4BGRA, 0, VertexFormatType::Unorm, 4, 4},
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
    {"snorm10-10-10-2", WGPUVertexFormat_Snorm10_10_10_2, 0, VertexFormatType::Snorm, 4, 4},
#else
    {"snorm10-10-10-2", kVertexFormatNotInHeader, 0, VertexFormatType::Snorm, 4, 4},
#endif
}};

std::vector<Value> vertexFormatValues() {
    std::vector<Value> values;
    values.reserve(kVertexFormats.size());
    for (const VertexFormatInfo& info : kVertexFormats) {
        values.emplace_back(std::string(info.name));
    }
    return values;
}

const VertexFormatInfo& vertexFormatInfo(std::string_view name) {
    for (const VertexFormatInfo& info : kVertexFormats) {
        if (name == info.name) {
            return info;
        }
    }
    std::abort();
}

const VertexFormatInfo& vertexFormatInfoFromParams(const ParamRecord& params) {
    return vertexFormatInfo(valueAs<std::string>(*findParam(params, "format")));
}

const VertexFormatInfo& formatParam(AllFeaturesMaxLimitsGpuTest& t) {
    const VertexFormatInfo& info = vertexFormatInfo(t.param<std::string>("format"));
    if (info.format == kVertexFormatNotInHeader) {
        t.skip("the vertex format is not in this backend's webgpu.h");
    }
    return info;
}

// ------------------------------------------------------------------
// ValueTestVariant
// ------------------------------------------------------------------
struct ValueTestVariant {
    double mult;
    int64_t add;
};

std::string encodeVariant(ValueTestVariant variant) {
    std::ostringstream encoded;
    encoded << variant.mult << "," << variant.add;
    return encoded.str();
}

ValueTestVariant decodeVariant(const std::string& encoded) {
    const size_t comma = encoded.find(',');
    return {std::stod(encoded.substr(0, comma)), std::stoll(encoded.substr(comma + 1))};
}

std::vector<Value> variantValues(std::initializer_list<ValueTestVariant> variants) {
    std::vector<Value> values;
    for (const ValueTestVariant& variant : variants) {
        const Value value(encodeVariant(variant));
        const bool duplicate = std::any_of(values.begin(), values.end(), [&](const Value& other) {
            return valueAs<std::string>(other) == valueAs<std::string>(value);
        });
        if (!duplicate) {
            values.push_back(value);
        }
    }
    return values;
}

double makeValueTestVariant(double base, ValueTestVariant variant) {
    return base * variant.mult + static_cast<double>(variant.add);
}

uint32_t makeLimitVariant(uint32_t limit, const std::string& encodedVariant) {
    return static_cast<uint32_t>(makeValueTestVariant(limit, decodeVariant(encodedVariant)));
}

uint64_t alignTo(uint64_t value, uint64_t alignment) {
    return (value + alignment - 1) / alignment * alignment;
}

// ------------------------------------------------------------------
// Vertex state description (VertexLayoutState<V, A & TestData>)
// ------------------------------------------------------------------
struct TestData {
    std::string shaderBaseType;
    std::optional<double> floatTolerance;
    // The number of vertex components in the vertexData (expectedData might contain more because
    // it is padded to 4 components).
    uint32_t testComponentCount = 0;
    // The data that will be in the uniform buffer and used to check the vertex inputs.
    std::vector<uint8_t> expectedData;
    // The data that will be in the vertex buffer.
    std::vector<uint8_t> vertexData;
};

struct VertexAttrib {
    uint32_t shaderLocation = 0;
    const VertexFormatInfo* format = nullptr;
    uint64_t offset = 0;
    // 0 stands for undefined: the format's component count.
    uint32_t shaderComponentCount = 0;
    TestData testData;
};

struct VertexBufferDesc {
    uint32_t slot = 0;
    uint64_t arrayStride = 0;
    WGPUVertexStepMode stepMode = WGPUVertexStepMode_Vertex;
    uint64_t vbOffset = 0;
    std::vector<VertexAttrib> attributes;
};

using VertexLayoutState = std::vector<VertexBufferDesc>;

struct BoundVertexBuffer {
    uint32_t slot;
    WGPUBuffer buffer;
    uint64_t vbOffset;
};

// ------------------------------------------------------------------
// Conversions
// ------------------------------------------------------------------
template <class T>
void appendValue(std::vector<uint8_t>& out, T value) {
    const size_t offset = out.size();
    out.resize(offset + sizeof(T));
    std::memcpy(out.data() + offset, &value, sizeof(T));
}

std::vector<uint8_t> toFloat32Bytes(const std::vector<double>& values) {
    std::vector<uint8_t> bytes;
    bytes.reserve(values.size() * sizeof(float));
    for (double value : values) {
        appendValue(bytes, static_cast<float>(value));
    }
    return bytes;
}

// float32ToFloat16Bits: on underflow rounds to (signed) zero, discards the lower mantissa bits.
uint16_t float32ToFloat16Bits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    const uint32_t sign = bits >> 31;
    if (value == 0.0f) {
        return static_cast<uint16_t>(sign << 15);
    }
    const int32_t newBiasedExp = static_cast<int32_t>((bits >> 23) & 0xff) - 127 + 15;
    if (newBiasedExp <= 0) {
        return static_cast<uint16_t>(sign << 15);
    }
    const uint32_t newMantissa = (bits & 0x7fffff) >> 13;
    return static_cast<uint16_t>((sign << 15) | (static_cast<uint32_t>(newBiasedExp) << 10) | newMantissa);
}

double normalizedIntegerAsFloat(int64_t integer, uint32_t bits, bool isSigned) {
    if (isSigned) {
        const int64_t max = (int64_t(1) << (bits - 1)) - 1;
        if (integer == -max - 1) {
            integer = -max;
        }
        return static_cast<double>(integer) / static_cast<double>(max);
    }
    const int64_t max = (int64_t(1) << bits) - 1;
    return static_cast<double>(integer) / static_cast<double>(max);
}

uint32_t makeRgb10a2(const std::array<uint32_t, 4>& rgba) {
    return rgba[0] | (rgba[1] << 10) | (rgba[2] << 20) | (rgba[3] << 30);
}

double normalizeRgb10a2(double value, size_t index) {
    const double normalizationFactor = index % 4 == 3 ? 3.0 : 1023.0;
    return value / normalizationFactor;
}

uint32_t makeRgb10a2Signed(const std::array<int32_t, 4>& rgba) {
    const uint32_t redBits = static_cast<uint32_t>(rgba[0]) & 0x3ff;
    const uint32_t greenBits = static_cast<uint32_t>(rgba[1]) & 0x3ff;
    const uint32_t blueBits = static_cast<uint32_t>(rgba[2]) & 0x3ff;
    const uint32_t alphaBits = static_cast<uint32_t>(rgba[3]) & 0x3;
    return redBits | (greenBits << 10) | (blueBits << 20) | (alphaBits << 30);
}

double normalizeRgb10a2Signed(double value, size_t index) {
    const double maxVal = index % 4 == 3 ? 1.0 : 511.0;
    return std::max(value / maxVal, -1.0);
}

std::vector<uint8_t> packIntegers(const std::vector<int64_t>& data, uint32_t bitSize) {
    std::vector<uint8_t> bytes;
    for (int64_t value : data) {
        if (bitSize == 32) {
            appendValue(bytes, static_cast<uint32_t>(value));
        } else if (bitSize == 16) {
            appendValue(bytes, static_cast<uint16_t>(value));
        } else {
            appendValue(bytes, static_cast<uint8_t>(value));
        }
    }
    return bytes;
}

// Generate TestData for the format with interesting test values.
// Note that the test data always starts with an interesting value, so that using the first
// test value in a test is still meaningful.
TestData generateTestData(const VertexFormatInfo& formatInfo) {
    const uint32_t bitSize = formatInfo.bytesPerComponent * 8;
    TestData result;

    switch (formatInfo.type) {
        case VertexFormatType::Float: {
            // -0.0 and +0.0 have different bit patterns, but compare as equal.
            const std::vector<double> data = {42.42, 0.0, -0.0, 1.0, -1.0, 1000, -18.7, 25.17};
            result.expectedData = toFloat32Bytes(data);
            if (bitSize == 32) {
                result.vertexData = result.expectedData;
            } else {
                for (double value : data) {
                    appendValue(result.vertexData, float32ToFloat16Bits(static_cast<float>(value)));
                }
            }
            result.shaderBaseType = "f32";
            result.testComponentCount = static_cast<uint32_t>(data.size());
            result.floatTolerance = 0.05;
            return result;
        }

        case VertexFormatType::Sint: {
            const int64_t quarter = int64_t(1) << (bitSize - 2);
            const int64_t half = int64_t(1) << (bitSize - 1);
            const std::vector<int64_t> data = {
                42,
                0, 1, 2, 3, 4, 5,
                -1, -2, -3, -4, -5,
                quarter,
                half - 1, // max value
                -quarter,
                -half, // min value
            };
            for (int64_t value : data) {
                appendValue(result.expectedData, static_cast<int32_t>(value));
            }
            result.vertexData = packIntegers(data, bitSize);
            result.shaderBaseType = "i32";
            result.testComponentCount = static_cast<uint32_t>(data.size());
            return result;
        }

        case VertexFormatType::Uint: {
            const int64_t half = int64_t(1) << (bitSize - 1);
            const std::vector<int64_t> data = {
                42,
                0, 1, 2, 3, 4, 5,
                half,
                half * 2 - 1, // max value
            };
            for (int64_t value : data) {
                appendValue(result.expectedData, static_cast<uint32_t>(value));
            }
            result.vertexData = packIntegers(data, bitSize);
            result.shaderBaseType = "u32";
            result.testComponentCount = static_cast<uint32_t>(data.size());
            return result;
        }

        case VertexFormatType::Snorm: {
            result.shaderBaseType = "f32";
            if (formatInfo.bytesPerComponent == 0) {
                // snorm10-10-10-2
                const std::array<std::array<int32_t, 4>, 4> data = {{
                    {0, 0, 0, 0},
                    {511, 511, 511, 1},
                    {-512, -512, -512, -2},
                    {243, -123, 342, -1},
                }};
                std::vector<double> flatExpected;
                for (const std::array<int32_t, 4>& texel : data) {
                    appendValue(result.vertexData, makeRgb10a2Signed(texel));
                    for (int32_t component : texel) {
                        flatExpected.push_back(
                            normalizeRgb10a2Signed(component, flatExpected.size()));
                    }
                }
                result.expectedData = toFloat32Bytes(flatExpected);
                result.testComponentCount = static_cast<uint32_t>(flatExpected.size());
                result.floatTolerance = 0.1 / 511;
                return result;
            }
            const int64_t quarter = int64_t(1) << (bitSize - 2);
            const int64_t half = int64_t(1) << (bitSize - 1);
            const std::vector<int64_t> data = {
                42,
                0, 1, 2, 3, 4, 5,
                -1, -2, -3, -4, -5,
                quarter,
                half - 1, // max value
                -quarter,
                -half, // min value
            };
            std::vector<double> expected;
            for (int64_t value : data) {
                expected.push_back(normalizedIntegerAsFloat(value, bitSize, true));
            }
            result.expectedData = toFloat32Bytes(expected);
            result.vertexData = packIntegers(data, bitSize);
            result.testComponentCount = static_cast<uint32_t>(data.size());
            result.floatTolerance = 0.1 * normalizedIntegerAsFloat(1, bitSize, true);
            return result;
        }

        case VertexFormatType::Unorm: {
            result.shaderBaseType = "f32";
            if (formatInfo.bytesPerComponent == 0) {
                if (formatInfo.format == WGPUVertexFormat_Unorm10_10_10_2) {
                    const std::array<std::array<uint32_t, 4>, 3> data = {{
                        {0, 0, 0, 0},
                        {1023, 1023, 1023, 3},
                        {243, 567, 765, 2},
                    }};
                    std::vector<double> flatExpected;
                    for (const std::array<uint32_t, 4>& texel : data) {
                        appendValue(result.vertexData, makeRgb10a2(texel));
                        for (uint32_t component : texel) {
                            flatExpected.push_back(normalizeRgb10a2(component, flatExpected.size()));
                        }
                    }
                    result.expectedData = toFloat32Bytes(flatExpected);
                    result.testComponentCount = static_cast<uint32_t>(flatExpected.size());
                    result.floatTolerance = 0.1 / 1023;
                    return result;
                }

                // unorm8x4-bgra
                const std::vector<int64_t> data = {42, 0, 1, 2, 3, 4, 128, 255};
                std::vector<double> expected;
                for (int64_t value : data) {
                    expected.push_back(normalizedIntegerAsFloat(value, 8, false));
                }
                for (size_t i = 0; i + 2 < expected.size(); i += 4) {
                    std::swap(expected[i + 0], expected[i + 2]);
                }
                result.vertexData = packIntegers(data, 8);
                result.expectedData = toFloat32Bytes(expected);
                result.testComponentCount = static_cast<uint32_t>(data.size());
                result.floatTolerance = 0.1 / 255;
                return result;
            }

            const int64_t half = int64_t(1) << (bitSize - 1);
            const std::vector<int64_t> data = {
                42,
                0, 1, 2, 3, 4, 5,
                half,
                half * 2 - 1, // max value
            };
            std::vector<double> expected;
            for (int64_t value : data) {
                expected.push_back(normalizedIntegerAsFloat(value, bitSize, false));
            }
            result.expectedData = toFloat32Bytes(expected);
            result.vertexData = packIntegers(data, bitSize);
            result.testComponentCount = static_cast<uint32_t>(data.size());
            result.floatTolerance = 0.1 * normalizedIntegerAsFloat(1, bitSize, false);
            return result;
        }
    }
    std::abort();
}

// The TestData generated for a format might not contain enough data for all the vertices we are
// going to draw, so we expand them by adding additional copies of the vertexData as needed.
// expectedData is a bit different because it also needs to be unpacked to have `componentCount`
// components every 4 components (because the shader uses vec4 for the expected data).
TestData expandTestData(const TestData& data, uint32_t maxCount, uint32_t componentCount) {
    const size_t vertexComponentSize = data.vertexData.size() / data.testComponentCount;
    const size_t expectedComponentSize = data.expectedData.size() / data.testComponentCount;

    std::vector<uint8_t> expandedVertexData(maxCount * componentCount * vertexComponentSize, 0);
    std::vector<uint8_t> expandedExpectedData(4 * static_cast<size_t>(maxCount) * expectedComponentSize, 0);

    for (size_t index = 0; index < maxCount; index++) {
        for (size_t component = 0; component < componentCount; component++) {
            const size_t targetVertexOffset = (index * componentCount + component) * vertexComponentSize;
            const size_t sourceVertexOffset = targetVertexOffset % data.vertexData.size();
            std::memcpy(expandedVertexData.data() + targetVertexOffset,
                        data.vertexData.data() + sourceVertexOffset,
                        vertexComponentSize);

            const size_t targetExpectedOffset = (index * 4 + component) * expectedComponentSize;
            const size_t sourceExpectedOffset =
                ((index * componentCount + component) * expectedComponentSize) % data.expectedData.size();
            std::memcpy(expandedExpectedData.data() + targetExpectedOffset,
                        data.expectedData.data() + sourceExpectedOffset,
                        expectedComponentSize);
        }
    }

    TestData expanded;
    expanded.shaderBaseType = data.shaderBaseType;
    expanded.testComponentCount = maxCount * componentCount;
    expanded.floatTolerance = data.floatTolerance;
    expanded.expectedData = std::move(expandedExpectedData);
    expanded.vertexData = std::move(expandedVertexData);
    return expanded;
}

// Copies `size` bytes from `source` to `target` starting at `offset` each `targetStride`.
// (the data in `source` is assumed packed)
void interleaveVertexDataInto(std::vector<uint8_t>& target,
                              const std::vector<uint8_t>& source,
                              uint64_t targetStride,
                              uint64_t offset,
                              uint64_t size) {
    for (uint64_t srcStart = 0, dstStart = offset; srcStart < source.size();
         srcStart += size, dstStart += targetStride) {
        std::memcpy(target.data() + dstStart, source.data() + srcStart, size);
    }
}

std::string numberLiteral(double value) {
    std::ostringstream literal;
    literal << std::setprecision(17) << value;
    return literal.str();
}

// Generate for VS + FS (entrypoints vsMain / fsMain) that for each attribute will check that its
// value corresponds to what's expected (as provided by a uniform buffer per attribute) and then
// renders each vertex at position (vertexIndex, instanceindex) with either 1 (success) or
// a negative number corresponding to the check number (in case you need to debug a failure).
std::string makeTestWGSL(const VertexLayoutState& buffers, uint32_t vertexCount, uint32_t instanceCount) {
    std::ostringstream vsInputs;
    std::ostringstream vsChecks;
    std::ostringstream providedDataDefs;

    for (const VertexBufferDesc& b : buffers) {
        for (const VertexAttrib& a : b.attributes) {
            const VertexFormatInfo& format = *a.format;
            const uint32_t shaderComponentCount =
                a.shaderComponentCount != 0 ? a.shaderComponentCount : format.componentCount;
            const uint32_t i = a.shaderLocation;
            const std::string& baseType = a.testData.shaderBaseType;

            // shaderType is either a scalar type like f32 or a vecN<scalarType>
            std::string shaderType = baseType;
            if (shaderComponentCount != 1) {
                shaderType = "vec" + std::to_string(shaderComponentCount) + "<" + shaderType + ">";
            }

            uint32_t maxCount = vertexCount;
            std::string indexBuiltin = "input.vertexIndex";
            if (b.stepMode == WGPUVertexStepMode_Instance) {
                maxCount = instanceCount;
                indexBuiltin = "input.instanceIndex";
            }

            vsInputs << "  @location(" << i << ") attrib" << i << " : " << shaderType << ",\n";
            providedDataDefs << "  data" << i << ": array<vec4<" << baseType << ">, " << maxCount << ">,\n";

            // Generate the all the checks for the attributes.
            for (uint32_t component = 0; component < shaderComponentCount; component++) {
                // Components are filled with (0, 0, 0, 1) if they aren't provided data from the
                // pipeline.
                if (component >= format.componentCount) {
                    const char* expected = component == 3 ? "1" : "0";
                    vsChecks << "  check(input.attrib" << i << "[" << component << "] == " << baseType
                             << "(" << expected << "));\n";
                    continue;
                }

                // Check each component individually, with special handling of tolerance for floats.
                const std::string attribComponent =
                    shaderComponentCount == 1
                        ? "input.attrib" + std::to_string(i)
                        : "input.attrib" + std::to_string(i) + "[" + std::to_string(component) + "]";
                const std::string providedData = "providedData.data" + std::to_string(i) + "[" + indexBuiltin
                    + "][" + std::to_string(component) + "]";
                if (format.type == VertexFormatType::Uint || format.type == VertexFormatType::Sint) {
                    vsChecks << "  check(" << attribComponent << " == " << providedData << ");\n";
                } else {
                    vsChecks << "  check(floatsSimilar(" << attribComponent << ", " << providedData << ", f32("
                             << numberLiteral(a.testData.floatTolerance.value_or(0)) << ")));\n";
                }
            }
        }
    }

    std::ostringstream code;
    code << R"(
struct Inputs {
)" << vsInputs.str() << R"(
  @builtin(vertex_index) vertexIndex: u32,
  @builtin(instance_index) instanceIndex: u32,
};

struct ProvidedData {
)" << providedDataDefs.str() << R"(
};

@group(0) @binding(0) var<uniform> providedData: ProvidedData;

var<private> vsResult : i32 = 1;
var<private> checkIndex : i32 = 0;
fn check(success : bool) {
  if (!success) {
    vsResult = -checkIndex;
  }
  checkIndex = checkIndex + 1;
}

fn floatsSimilar(a : f32, b : f32, tolerance : f32) -> bool {
  // Note: -0.0 and 0.0 have different bit patterns, but compare as equal.
  return abs(a - b) < tolerance;
}

fn doTest(input : Inputs) {
)" << vsChecks.str() << R"(
}

struct VSOutputs {
  @location(0) @interpolate(flat, either) result : i32,
  @builtin(position) position : vec4<f32>,
};

@vertex fn vsMain(input : Inputs) -> VSOutputs {
  doTest(input);

  // Place that point at pixel (vertexIndex, instanceIndex) in a framebuffer of size
  // (vertexCount , instanceCount).
  var output : VSOutputs;
  output.position = vec4<f32>(
    ((f32(input.vertexIndex) + 0.5) / )" << vertexCount << R"(.0 * 2.0) - 1.0,
    ((f32(input.instanceIndex) + 0.5) / )" << instanceCount << R"(.0 * 2.0) - 1.0,
    0.0, 1.0
  );
  output.result = vsResult;
  return output;
}

@fragment fn fsMain(@location(0) @interpolate(flat, either) result : i32)
  -> @location(0) i32 {
  return result;
}
    )";
    return code.str();
}

WGPURenderPipeline makeTestPipeline(AllFeaturesMaxLimitsGpuTest& t,
                                    const VertexLayoutState& buffers,
                                    uint32_t vertexCount,
                                    uint32_t instanceCount) {
    WGPUShaderModule module = t.createShaderModuleTracked(makeTestWGSL(buffers, vertexCount, instanceCount));

    uint32_t bufferCount = 0;
    for (const VertexBufferDesc& b : buffers) {
        bufferCount = std::max(bufferCount, b.slot + 1);
    }

    // Slots without a buffer stay holes (`bufferLayouts[b.slot] = b` in upstream).
    std::vector<WGPUVertexBufferLayout> bufferLayouts(bufferCount, WGPU_VERTEX_BUFFER_LAYOUT_INIT);
    std::vector<std::vector<WGPUVertexAttribute>> attributes(buffers.size());
    for (size_t i = 0; i < buffers.size(); ++i) {
        const VertexBufferDesc& b = buffers[i];
        for (const VertexAttrib& a : b.attributes) {
            WGPUVertexAttribute attribute = WGPU_VERTEX_ATTRIBUTE_INIT;
            attribute.format = a.format->format;
            attribute.offset = a.offset;
            attribute.shaderLocation = a.shaderLocation;
            attributes[i].push_back(attribute);
        }
        WGPUVertexBufferLayout& layout = bufferLayouts[b.slot];
        layout.stepMode = b.stepMode;
        layout.arrayStride = b.arrayStride;
        layout.attributeCount = attributes[i].size();
        layout.attributes = attributes[i].data();
    }

    WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
    target.format = WGPUTextureFormat_R32Sint;
    target.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
    fragment.module = module;
    fragment.entryPoint = WGPUStringView{"fsMain", WGPU_STRLEN};
    fragment.targetCount = 1;
    fragment.targets = &target;

    WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
    desc.vertex.module = module;
    desc.vertex.entryPoint = WGPUStringView{"vsMain", WGPU_STRLEN};
    desc.vertex.bufferCount = bufferLayouts.size();
    desc.vertex.buffers = bufferLayouts.data();
    desc.primitive.topology = WGPUPrimitiveTopology_PointList;
    desc.multisample.count = 1;
    desc.fragment = &fragment;
    return t.createRenderPipelineTracked(desc);
}

// Runs the render pass drawing points in a vertexCount*instanceCount rectangle, then check each
// of produced a value of 1 which means that the tests in the shader passed.
void submitRenderPass(AllFeaturesMaxLimitsGpuTest& t,
                      WGPURenderPipeline pipeline,
                      const std::vector<BoundVertexBuffer>& buffers,
                      WGPUBindGroup expectedData,
                      uint32_t vertexCount,
                      uint32_t instanceCount) {
    constexpr uint32_t kBytesPerTexel = 4;
    constexpr uint64_t kBytesPerRowAlignment = 256;

    WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    textureDesc.format = WGPUTextureFormat_R32Sint;
    textureDesc.size = WGPUExtent3D{vertexCount, instanceCount, 1};
    textureDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    WGPUTexture testTexture = t.createTextureTracked(textureDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view = t.createViewTracked(testTexture, viewDesc);
    colorAttachment.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
    WGPURenderPassEncoder pass = t.beginRenderPassTracked(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, expectedData, 0, nullptr);
    for (const BoundVertexBuffer& buffer : buffers) {
        wgpuRenderPassEncoderSetVertexBuffer(pass, buffer.slot, buffer.buffer, buffer.vbOffset, WGPU_WHOLE_SIZE);
    }
    wgpuRenderPassEncoderDraw(pass, vertexCount, instanceCount, 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    const uint32_t bytesPerRow = static_cast<uint32_t>(alignTo(vertexCount * kBytesPerTexel, kBytesPerRowAlignment));
    const uint64_t byteLength = static_cast<uint64_t>(bytesPerRow) * (instanceCount - 1)
        + static_cast<uint64_t>(vertexCount) * kBytesPerTexel;
    WGPUBufferDescriptor readbackDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    readbackDesc.size = byteLength;
    readbackDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    WGPUBuffer readback = t.createBufferTracked(readbackDesc);
    t.copyTextureToBuffer(encoder, testTexture, readback, bytesPerRow, textureDesc.size);

    WGPUCommandBuffer commandBuffer = t.finishTracked(encoder);
    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);

    // expectSingleColor(testTexture, 'r32sint', { size, exp: { R: 1 } })
    t.expectGPUBufferValuesPassCheck(
        readback,
        [&](const uint8_t* actual, size_t) -> std::optional<std::string> {
            for (uint32_t y = 0; y < instanceCount; ++y) {
                for (uint32_t x = 0; x < vertexCount; ++x) {
                    int32_t value = 0;
                    std::memcpy(&value, actual + static_cast<size_t>(y) * bytesPerRow + static_cast<size_t>(x) * kBytesPerTexel,
                                sizeof(value));
                    if (value != 1) {
                        std::ostringstream message;
                        message << "expected R=1 at (" << x << ", " << y << "), got " << value;
                        return message.str();
                    }
                }
            }
            return std::nullopt;
        },
        0,
        byteLength);
}

// Gather the test data and some additional test state for attribs.
VertexLayoutState createTestAndPipelineData(const VertexLayoutState& state,
                                            uint32_t vertexCount,
                                            uint32_t instanceCount) {
    VertexLayoutState result = state;
    for (VertexBufferDesc& buffer : result) {
        const uint32_t maxCount = buffer.stepMode == WGPUVertexStepMode_Instance ? instanceCount : vertexCount;
        for (VertexAttrib& attrib : buffer.attributes) {
            attrib.testData = expandTestData(generateTestData(*attrib.format), maxCount, attrib.format->componentCount);
        }
    }
    return result;
}

// Create the bindgroup for the expected test data
WGPUBindGroup createExpectedBG(AllFeaturesMaxLimitsGpuTest& t,
                               const VertexLayoutState& state,
                               WGPURenderPipeline pipeline) {
    // Concat expectedData into one buffer
    std::vector<uint8_t> allExpectedData;
    for (const VertexBufferDesc& buffer : state) {
        for (const VertexAttrib& attrib : buffer.attributes) {
            allExpectedData.insert(
                allExpectedData.end(), attrib.testData.expectedData.begin(), attrib.testData.expectedData.end());
        }
    }
    WGPUBuffer expectedDataBuffer =
        t.makeBufferWithContents(allExpectedData.data(), allExpectedData.size(), WGPUBufferUsage_Uniform);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = 0;
    entry.buffer = expectedDataBuffer;
    entry.size = WGPU_WHOLE_SIZE;

    WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
    desc.layout = wgpuRenderPipelineGetBindGroupLayout(pipeline, 0);
    desc.entryCount = 1;
    desc.entries = &entry;
    WGPUBindGroup bindGroup = t.createBindGroupTracked(desc);
    wgpuBindGroupLayoutRelease(desc.layout);
    return bindGroup;
}

// Create the vertex buffers
std::vector<BoundVertexBuffer> createVertexBuffers(AllFeaturesMaxLimitsGpuTest& t,
                                                   const VertexLayoutState& state,
                                                   uint32_t vertexCount,
                                                   uint32_t instanceCount) {
    std::vector<BoundVertexBuffer> vertexBuffers;
    for (const VertexBufferDesc& buffer : state) {
        const uint32_t maxCount = buffer.stepMode == WGPUVertexStepMode_Instance ? instanceCount : vertexCount;

        // Fill the vertex data with garbage so that we don't get `0` (which could be a test value)
        // if the vertex shader loads the vertex data incorrectly.
        std::vector<uint8_t> vertexData(alignTo(buffer.arrayStride * maxCount + buffer.vbOffset, 4), 0xc4);

        for (const VertexAttrib& attrib : buffer.attributes) {
            interleaveVertexDataInto(vertexData,
                                     attrib.testData.vertexData,
                                     buffer.arrayStride,
                                     buffer.vbOffset + attrib.offset,
                                     attrib.format->byteSize);
        }

        vertexBuffers.push_back(BoundVertexBuffer{
            buffer.slot,
            t.makeBufferWithContents(vertexData.data(), vertexData.size(), WGPUBufferUsage_Vertex),
            buffer.vbOffset,
        });
    }
    return vertexBuffers;
}

// Default to using 20 vertices and 20 instances so that we cover each of the test data at least
// once (at the time of writing the largest testData has 16 values).
void runTest(AllFeaturesMaxLimitsGpuTest& t,
             const VertexLayoutState& buffers,
             uint32_t vertexCount = 20,
             uint32_t instanceCount = 20) {
    const VertexLayoutState testData = createTestAndPipelineData(buffers, vertexCount, instanceCount);
    WGPURenderPipeline pipeline = makeTestPipeline(t, testData, vertexCount, instanceCount);
    WGPUBindGroup expectedDataBG = createExpectedBG(t, testData, pipeline);
    const std::vector<BoundVertexBuffer> vertexBuffers = createVertexBuffers(t, testData, vertexCount, instanceCount);
    submitRenderPass(t, pipeline, vertexBuffers, expectedDataBG, vertexCount, instanceCount);
}

VertexAttrib makeAttrib(const VertexFormatInfo& format, uint64_t offset, uint32_t shaderLocation) {
    VertexAttrib attrib;
    attrib.shaderLocation = shaderLocation;
    attrib.format = &format;
    attrib.offset = offset;
    return attrib;
}

VertexBufferDesc makeBuffer(uint32_t slot,
                            uint64_t arrayStride,
                            WGPUVertexStepMode stepMode,
                            std::vector<VertexAttrib> attributes,
                            uint64_t vbOffset = 0) {
    VertexBufferDesc buffer;
    buffer.slot = slot;
    buffer.arrayStride = arrayStride;
    buffer.stepMode = stepMode;
    buffer.vbOffset = vbOffset;
    buffer.attributes = std::move(attributes);
    return buffer;
}

CTS_TEST(g, "vertex_format_to_shader_format_conversion")
    .desc(
        "Test that the raw data passed in vertex buffers is correctly converted to the input type in "
        "the shader. Test for:\n"
        "- all formats\n"
        "- 1 to 4 components in the shader's input type (unused components are filled with 0 and "
        "except the 4th with 1)\n"
        "- various locations\n"
        "- various slots")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .combine("shaderComponentCount", {Value(1), Value(2), Value(3), Value(4)})
            .beginSubcases()
            .combine("slotVariant", variantValues({{0, 0}, {0, 1}, {1, -1}}))
            .combine("shaderLocationVariant", variantValues({{0, 0}, {0, 1}, {1, -1}}));
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& format = formatParam(t);
        const WGPULimits limits = t.getLimits();
        const uint32_t slot = makeLimitVariant(limits.maxVertexBuffers, t.param<std::string>("slotVariant"));
        const uint32_t shaderLocation =
            makeLimitVariant(limits.maxVertexAttributes, t.param<std::string>("shaderLocationVariant"));

        VertexAttrib attrib = makeAttrib(format, 0, shaderLocation);
        attrib.shaderComponentCount = static_cast<uint32_t>(t.param<int>("shaderComponentCount"));
        runTest(t, {makeBuffer(slot, 16, WGPUVertexStepMode_Vertex, {attrib})});
    });

CTS_TEST(g, "setVertexBuffer_offset_and_attribute_offset")
    .desc(
        "Test that the vertex buffer offset and attribute offset in the vertex state are applied "
        "correctly. Test for:\n"
        "- all formats\n"
        "- various setVertexBuffer offsets\n"
        "- various attribute offsets in a fixed arrayStride")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .beginSubcases()
            .combine("vbOffset", {Value(0), Value(4), Value(400), Value(1004)})
            .combine("arrayStride", {Value(128)})
            .expand("offset", [](const ParamRecord& p) {
                const int64_t formatSize = vertexFormatInfoFromParams(p).byteSize;
                const int64_t arrayStride = valueAs<int64_t>(*findParam(p, "arrayStride"));
                std::vector<int64_t> offsets;
                for (int64_t offset :
                     {int64_t(0),
                      int64_t(4),
                      int64_t(8),
                      formatSize,
                      formatSize * 2,
                      arrayStride / 2,
                      arrayStride - formatSize - 4,
                      arrayStride - formatSize - 8,
                      arrayStride - formatSize - formatSize,
                      arrayStride - formatSize - formatSize * 2,
                      arrayStride - formatSize}) {
                    if (std::find(offsets.begin(), offsets.end(), offset) == offsets.end()) {
                        offsets.push_back(offset);
                    }
                }
                std::vector<Value> values;
                for (int64_t offset : offsets) {
                    values.emplace_back(offset);
                }
                return values;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& format = formatParam(t);
        const uint64_t vbOffset = t.param<uint64_t>("vbOffset");
        const uint64_t arrayStride = t.param<uint64_t>("arrayStride");
        const uint64_t offset = t.param<uint64_t>("offset");
        runTest(t, {makeBuffer(0, arrayStride, WGPUVertexStepMode_Vertex, {makeAttrib(format, offset, 0)}, vbOffset)});
    });

CTS_TEST(g, "non_zero_array_stride_and_attribute_offset")
    .desc(
        "Test that the array stride and attribute offset in the vertex state are applied correctly. "
        "Test for:\n"
        "- all formats\n"
        "- various array strides\n"
        "- various attribute offsets in a fixed arrayStride")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .beginSubcases()
            .expand("arrayStrideVariant", [](const ParamRecord& p) {
                const int64_t aligned = static_cast<int64_t>(alignTo(vertexFormatInfoFromParams(p).byteSize, 4));
                return variantValues({{0, aligned}, {0, aligned + 4}, {1, 0}});
            })
            .expand("offsetVariant", [](const ParamRecord& p) {
                const int64_t formatSize = vertexFormatInfoFromParams(p).byteSize;
                std::vector<Value> values;
                const auto yield = [&values](double mult, int64_t add) {
                    values.emplace_back(encodeVariant({mult, add}));
                };
                yield(0, 0);
                yield(0, 4);
                if (formatSize != 4) {
                    yield(0, formatSize);
                }
                yield(0.5, 0);
                yield(1, -formatSize * 2);
                if (formatSize != 4) {
                    yield(1, -formatSize - 4);
                }
                yield(1, -formatSize);
                return values;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& format = formatParam(t);
        const uint32_t arrayStride = makeLimitVariant(
            t.getLimits().maxVertexBufferArrayStride, t.param<std::string>("arrayStrideVariant"));
        const double formatSize = format.byteSize;
        const double makeMultipleOf = std::min(formatSize, 4.0);
        const double clamped = std::clamp(
            makeValueTestVariant(arrayStride, decodeVariant(t.param<std::string>("offsetVariant"))),
            0.0,
            static_cast<double>(arrayStride) - formatSize);
        const uint64_t offset = static_cast<uint64_t>(std::floor(clamped / makeMultipleOf) * makeMultipleOf);

        runTest(t, {makeBuffer(0, arrayStride, WGPUVertexStepMode_Vertex, {makeAttrib(format, offset, 0)})});
    });

CTS_TEST(g, "buffers_with_varying_step_mode")
    .desc(
        "Test buffers with varying step modes in the same vertex state.\n"
        "- Various combination of step modes")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("stepModes",
                                         {Value(std::string("instance")),
                                          Value(std::string("vertex,vertex,instance")),
                                          Value(std::string("instance,vertex,instance")),
                                          Value(std::string("vertex,instance,vertex,vertex"))});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string stepModes = t.param<std::string>("stepModes");
        const VertexFormatInfo& float32 = vertexFormatInfo("float32");

        VertexLayoutState state;
        std::stringstream stream(stepModes);
        std::string stepMode;
        for (uint32_t i = 0; std::getline(stream, stepMode, ','); ++i) {
            state.push_back(makeBuffer(i,
                                       4,
                                       stepMode == "instance" ? WGPUVertexStepMode_Instance : WGPUVertexStepMode_Vertex,
                                       {makeAttrib(float32, 0, i)}));
        }
        runTest(t, state);
    });

CTS_TEST(g, "vertex_buffer_used_multiple_times_overlapped")
    .desc(
        "Test using the same vertex buffer in for multiple \"vertex buffers\", with data from each "
        "buffer overlapping.\n"
        "- For each vertex format.\n"
        "- For various numbers of vertex buffers [2, 3, max]")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .beginSubcases()
            .combine("vbCountVariant", variantValues({{0, 2}, {0, 3}, {1, 0}}))
            .combine("additionalVBOffset", {Value(0), Value(4), Value(120)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& format = formatParam(t);
        const uint32_t vbCount =
            makeLimitVariant(t.getLimits().maxVertexBuffers, t.param<std::string>("vbCountVariant"));
        const uint64_t additionalVBOffset = t.param<uint64_t>("additionalVBOffset");
        constexpr uint32_t kVertexCount = 20;
        constexpr uint32_t kInstanceCount = 1;
        // We need to align so the offset for non-0 setVertexBuffer don't fail validation.
        const uint64_t alignedFormatByteSize = alignTo(format.byteSize, 4);

        // In this test we want to test using the same vertex buffer for multiple different attributes.
        // For example if vbCount is 3, we will create a vertex buffer containing the following data:
        //    a0, a1, a2, a3, ..., a<baseDataVertexCount>
        // We also create the expected data for the vertex fetching from that buffer so we can modify it
        // below.
        const uint32_t baseDataVertexCount = kVertexCount + vbCount - 1;
        const VertexLayoutState baseData = createTestAndPipelineData(
            {makeBuffer(0, alignedFormatByteSize, WGPUVertexStepMode_Vertex, {makeAttrib(format, 0, 0)}, additionalVBOffset)},
            baseDataVertexCount,
            kInstanceCount);
        WGPUBuffer vertexBuffer = createVertexBuffers(t, baseData, baseDataVertexCount, kInstanceCount)[0].buffer;

        // We are going to bind the vertex buffer multiple times, each time at a different offset that's
        // a multiple of the data size. So what should be fetched by the vertex shader is:
        //    - attrib0: a0, a1, ..., a19
        //    - attrib1: a1, a2, ..., a20
        //    - attrib2: a2, a3, ..., a21
        //    etc.
        // We re-create the test data by:
        //   1) creating multiple "vertex buffers" that all point at the GPUBuffer above but at
        //      different offsets.
        //   2) selecting what parts of the expectedData each attribute will see in the expectedData for
        //      the full vertex buffer.
        const TestData& baseTestData = baseData[0].attributes[0].testData;
        t.expect(baseTestData.testComponentCount == format.componentCount * baseDataVertexCount,
                 "unexpected base test data size");
        const size_t expectedDataBytesPerVertex = baseTestData.expectedData.size() / baseDataVertexCount;

        VertexLayoutState testData;
        std::vector<BoundVertexBuffer> vertexBuffers;
        for (uint32_t i = 0; i < vbCount; i++) {
            vertexBuffers.push_back(BoundVertexBuffer{i, vertexBuffer, additionalVBOffset + i * alignedFormatByteSize});

            VertexAttrib attrib = makeAttrib(format, 0, i);
            attrib.testData.shaderBaseType = baseTestData.shaderBaseType;
            attrib.testData.floatTolerance = baseTestData.floatTolerance;
            // Select vertices [i, i + kVertexCount]
            attrib.testData.testComponentCount = kVertexCount * format.componentCount;
            attrib.testData.expectedData.assign(
                baseTestData.expectedData.begin() + static_cast<std::ptrdiff_t>(expectedDataBytesPerVertex * i),
                baseTestData.expectedData.begin() + static_cast<std::ptrdiff_t>(expectedDataBytesPerVertex * (kVertexCount + i)));
            testData.push_back(makeBuffer(i, alignedFormatByteSize, WGPUVertexStepMode_Vertex, {attrib}));
        }

        // Run the test with the modified test data.
        WGPURenderPipeline pipeline = makeTestPipeline(t, testData, kVertexCount, kInstanceCount);
        WGPUBindGroup expectedDataBG = createExpectedBG(t, testData, pipeline);
        submitRenderPass(t, pipeline, vertexBuffers, expectedDataBG, kVertexCount, kInstanceCount);
    });

CTS_TEST(g, "vertex_buffer_used_multiple_times_interleaved")
    .desc(
        "Test using the same vertex buffer in for multiple \"vertex buffers\", with data from each "
        "buffer interleaved.\n"
        "- For each vertex format.\n"
        "- For various numbers of vertex buffers [2, 3, max]")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .beginSubcases()
            .combine("vbCountVariant", variantValues({{0, 2}, {0, 3}, {1, 0}}))
            .combine("additionalVBOffset", {Value(0), Value(4), Value(120)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& format = formatParam(t);
        const uint32_t vbCount =
            makeLimitVariant(t.getLimits().maxVertexBuffers, t.param<std::string>("vbCountVariant"));
        const uint64_t additionalVBOffset = t.param<uint64_t>("additionalVBOffset");
        constexpr uint32_t kVertexCount = 20;
        constexpr uint32_t kInstanceCount = 1;
        // We need to align so the offset for non-0 setVertexBuffer don't fail validation.
        const uint64_t alignedFormatByteSize = alignTo(format.byteSize, 4);

        // Create data for a single vertex buffer with many attributes, that will be split between
        // many vertex buffers set at different offsets.

        // In this test we want to test using the same vertex buffer for multiple different attributes.
        // For example if vbCount is 3, we will create a vertex buffer containing the following data:
        //    a0, a0, a0, a1, a1, a1, ...
        // To do that we create a single vertex buffer with `vbCount` attributes that all have the same
        // format.
        std::vector<VertexAttrib> attribs;
        for (uint32_t i = 0; i < vbCount; i++) {
            attribs.push_back(makeAttrib(format, i * alignedFormatByteSize, i));
        }
        const VertexLayoutState baseData = createTestAndPipelineData(
            {makeBuffer(0, alignedFormatByteSize * vbCount, WGPUVertexStepMode_Vertex, attribs, additionalVBOffset)},
            kVertexCount,
            kInstanceCount);
        WGPUBuffer vertexBuffer = createVertexBuffers(t, baseData, kVertexCount, kInstanceCount)[0].buffer;

        // Then we recreate test data by:
        //   1) creating multiple "vertex buffers" that all point at the GPUBuffer above but at
        //      different offsets.
        //   2) have multiple vertex buffer, each with one attributes that will expect a0, a1, ...
        VertexLayoutState testData;
        std::vector<BoundVertexBuffer> vertexBuffers;
        for (uint32_t i = 0; i < vbCount; i++) {
            vertexBuffers.push_back(BoundVertexBuffer{i, vertexBuffer, additionalVBOffset + i * alignedFormatByteSize});
            VertexBufferDesc buffer = baseData[0];
            buffer.slot = i;
            buffer.attributes = {baseData[0].attributes[i]};
            buffer.attributes[0].offset = 0;
            testData.push_back(std::move(buffer));
        }

        // Run the test with the modified test data.
        WGPURenderPipeline pipeline = makeTestPipeline(t, testData, kVertexCount, kInstanceCount);
        WGPUBindGroup expectedDataBG = createExpectedBG(t, testData, pipeline);
        submitRenderPass(t, pipeline, vertexBuffers, expectedDataBG, kVertexCount, kInstanceCount);
    });

CTS_TEST(g, "max_buffers_and_attribs")
    .desc(
        "Test a vertex state that loads as many attributes and buffers as possible.\n"
        "- For each format.")
    .params([](ParamsBuilder u) { return u.combine("format", vertexFormatValues()); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& format = formatParam(t);
        const WGPULimits limits = t.getLimits();
        const uint32_t maxVertexBuffers = limits.maxVertexBuffers;
        const uint32_t maxVertexAttributes = limits.maxVertexAttributes;
        const uint32_t attributesPerBuffer = (maxVertexAttributes + maxVertexBuffers - 1) / maxVertexBuffers;
        uint32_t attributesEmitted = 0;

        VertexLayoutState state;
        for (uint32_t i = 0; i < maxVertexBuffers; i++) {
            std::vector<VertexAttrib> attributes;
            for (uint32_t j = 0; j < attributesPerBuffer && attributesEmitted < maxVertexAttributes; j++) {
                attributes.push_back(makeAttrib(format, 0, attributesEmitted));
                attributesEmitted++;
            }
            state.push_back(makeBuffer(i, 32, WGPUVertexStepMode_Vertex, std::move(attributes)));
        }
        runTest(t, state);
    });

CTS_TEST(g, "array_stride_zero")
    .desc(
        "Test that arrayStride 0 correctly uses the same data for all vertex/instances, while another "
        "test vertex buffer with arrayStride != 0 gets different data.\n"
        "- Test for all formats\n"
        "- Test for both step modes")
    .params([](ParamsBuilder u) {
        return u.combine("format", vertexFormatValues())
            .beginSubcases()
            .combine("stepMode", {Value(std::string("vertex")), Value(std::string("instance"))})
            .expand("offsetVariant", [](const ParamRecord& p) {
                const int64_t formatSize = vertexFormatInfoFromParams(p).byteSize;
                return variantValues({
                    {0, 0},
                    {0, 4},
                    {0, 8},
                    {0, formatSize},
                    {0, formatSize * 2},
                    {0.5, 0},
                    {1, -formatSize - 4},
                    {1, -formatSize - 8},
                    {1, -formatSize},
                    {1, -formatSize * 2},
                });
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& format = formatParam(t);
        const WGPUVertexStepMode stepMode = t.param<std::string>("stepMode") == "instance"
            ? WGPUVertexStepMode_Instance
            : WGPUVertexStepMode_Vertex;
        const uint32_t makeMultipleOf = std::min<uint32_t>(format.byteSize, 4);
        const uint64_t offset =
            makeLimitVariant(t.getLimits().maxVertexBufferArrayStride, t.param<std::string>("offsetVariant"))
            / makeMultipleOf * makeMultipleOf;
        constexpr uint32_t kCount = 10;

        // Create the stride 0 part of the test, first by faking a single vertex being drawn and
        // then expanding the data to cover kCount vertex / instances
        // (vbOffset is used to push data in the vertex buffer)
        VertexBufferDesc stride0TestData = createTestAndPipelineData(
            {makeBuffer(0, 2048, stepMode, {makeAttrib(format, 0, 0)}, offset)}, 1, 1)[0];
        BoundVertexBuffer stride0VertexBuffer = createVertexBuffers(t, {stride0TestData}, kCount, kCount)[0];

        // Expand the stride0 test data to have kCount values for expectedData.
        const std::vector<uint8_t> originalData = stride0TestData.attributes[0].testData.expectedData;
        std::vector<uint8_t> expandedData;
        expandedData.reserve(kCount * originalData.size());
        for (uint32_t i = 0; i < kCount; i++) {
            expandedData.insert(expandedData.end(), originalData.begin(), originalData.end());
        }

        // Fixup stride0TestData to use arrayStride 0.
        stride0TestData.attributes[0].offset = offset;
        stride0TestData.attributes[0].testData.expectedData = expandedData;
        stride0TestData.attributes[0].testData.testComponentCount *= kCount;
        stride0TestData.arrayStride = 0;
        stride0VertexBuffer.vbOffset = 0;

        // Create the part of the state that will be varying for each vertex / instance
        const VertexBufferDesc varyingTestData =
            createTestAndPipelineData({makeBuffer(1, 32, stepMode, {makeAttrib(format, 0, 1)})}, kCount, kCount)[0];
        const BoundVertexBuffer varyingVertexBuffer = createVertexBuffers(t, {varyingTestData}, kCount, kCount)[0];

        // Run the test with the merged test state.
        const VertexLayoutState state = {stride0TestData, varyingTestData};
        const std::vector<BoundVertexBuffer> vertexBuffers = {stride0VertexBuffer, varyingVertexBuffer};

        WGPURenderPipeline pipeline = makeTestPipeline(t, state, kCount, kCount);
        WGPUBindGroup expectedDataBG = createExpectedBG(t, state, pipeline);
        submitRenderPass(t, pipeline, vertexBuffers, expectedDataBG, kCount, kCount);
    });

CTS_TEST(g, "discontiguous_location_and_attribs")
    .desc("Test that using far away slots / shaderLocations works as expected")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPULimits limits = t.getLimits();
        const VertexFormatInfo& uint8x2 = vertexFormatInfo("uint8x2");
        const VertexFormatInfo& uint32x4 = vertexFormatInfo("uint32x4");
        runTest(t,
                {
                    makeBuffer(limits.maxVertexBuffers - 1,
                               4,
                               WGPUVertexStepMode_Vertex,
                               {makeAttrib(uint8x2, 2, 0), makeAttrib(uint8x2, 0, 8)}),
                    makeBuffer(1,
                               16,
                               WGPUVertexStepMode_Instance,
                               {makeAttrib(uint32x4, 0, limits.maxVertexAttributes - 1)},
                               1000),
                });
    });

CTS_TEST(g, "overlapping_attributes")
    .desc(
        "Test that overlapping attributes in the same vertex buffer works\n"
        "- Test for all formats")
    .params([](ParamsBuilder u) { return u.combine("format", vertexFormatValues()); })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const VertexFormatInfo& format = formatParam(t);
        const uint32_t maxVertexAttributes = t.getLimits().maxVertexAttributes;
        std::vector<VertexAttrib> attributes;
        for (uint32_t i = 0; i < maxVertexAttributes; i++) {
            attributes.push_back(makeAttrib(format, 0, i));
        }
        runTest(t, {makeBuffer(0, 32, WGPUVertexStepMode_Vertex, std::move(attributes))});
    });

} // namespace
