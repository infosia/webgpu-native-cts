// Ported from gpuweb/cts src/webgpu/api/operation/buffers/map.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes (deviations from upstream, all documented inline as well):
//
// 1. Array-valued params: upstream subcases carry `range` (and `range1`/`range2`)
//    as JS arrays whose elements may be `undefined`, stringified by the upstream
//    query encoder as e.g. `range=[8,4]` / `range=[0,"_undef_"]`. The C++ harness
//    Value has no array type, so ranges are encoded as string params using the
//    same bracket syntax with `_undef_` for undefined elements (no inner quotes):
//    "[]", "[_undef_]", "[0,_undef_]", "[8,4]", ... Query identity is otherwise
//    preserved (same param names, same element values, same ordering).
// 2. JS ArrayBuffer.byteLength assertions have no C analog (getMappedRange
//    returns a raw pointer); they are replaced by null-pointer checks for
//    non-empty ranges.
// 3. JS TypedArray views (mapAsync,read,typedArrayAccess) are adapted to
//    byte-range comparisons over the same byte layout. Note upstream writes
//    Int32Array indices [2]/[3] on a 2-element view -- silently-dropped
//    out-of-bounds writes in JS -- so bytes 20..27 stay zero; mirrored here.
// 4. `buffer.mapState` maps to wgpuBufferGetMapState. The JS promise-timing
//    checks in mapAsync,mapState (state must be 'pending' before the promise
//    settles; unmap/destroy aborting an in-flight map) require control over
//    event delivery, so that test creates its own WGPUInstance/device (same
//    pattern as api/validation/error_scope.spec.cpp) and defers error-scope
//    pops / callback delivery to mirror the JS task ordering.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

// Upstream fixture: MappingTest extends AllFeaturesMaxLimitsGPUTest.
class MappingTest : public AllFeaturesMaxLimitsGpuTest {
  public:
    // checkMapWrite(buffer, offset, mappedContents, size):
    // check the mapped range is zero-initialized, write an i+1 pattern through
    // it, unmap, and verify via copy + map-read that the contents were written.
    void checkMapWrite(WGPUBuffer buffer, uint64_t offset, void* mappedContents, uint64_t size) {
        checkMapWriteZeroed(mappedContents, size);

        std::vector<uint32_t> expected(static_cast<size_t>(size / 4), 0);
        auto* mappedView = static_cast<uint32_t*>(mappedContents);
        for (size_t i = 0; i < expected.size(); ++i) {
            const uint32_t value = static_cast<uint32_t>(i + 1);
            mappedView[i] = value;
            expected[i] = value;
        }
        wgpuBufferUnmap(buffer);

        expectGPUBufferValuesEqual(buffer, expected.data(), static_cast<size_t>(size), offset);
    }

    void checkMapWriteZeroed(const void* mappedContents, uint64_t expectedSize) {
        // JS: this.expect(arrayBuffer.byteLength === expectedSize) -- no C
        // analog for a raw pointer; require a non-null pointer for non-empty
        // ranges instead.
        if (expectedSize == 0) {
            return;
        }
        if (mappedContents == nullptr) {
            fail("getMappedRange returned null for a non-empty range");
        }
        expectZero(static_cast<const uint8_t*>(mappedContents), expectedSize);
    }

    void expectZero(const uint8_t* actual, uint64_t size) {
        for (uint64_t i = 0; i < size; ++i) {
            if (actual[i] != 0) {
                fail("at [" + std::to_string(i) + "], expected zero, got " +
                     std::to_string(actual[i]));
            }
        }
    }
};

TestGroup<MappingTest> g = MakeTestGroup<MappingTest>(
    "api,operation,buffers,map",
    R"(
Test the operation of buffer mapping, specifically the data contents written via
map-write/mappedAtCreation, and the contents of buffers returned by getMappedRange on
buffers which are mapped-read/mapped-write/mappedAtCreation.

range: used for getMappedRange
mapRegion: used for mapAsync

mapRegionBoundModes is used to get mapRegion from range:
 - default-expand: expand mapRegion to buffer bound by setting offset/size to undefined
 - explicit-expand: expand mapRegion to buffer bound by explicitly calculating offset/size
 - minimal: make mapRegion to be the same as range which is the minimal range to make getMappedRange input valid
)");

// ---------------------------------------------------------------------------
// Range helpers (see porting note 1 for the string encoding).
// ---------------------------------------------------------------------------

// Upstream: range: readonly [number?, number?]
struct RangeSpec {
    std::optional<uint64_t> offset;
    std::optional<uint64_t> size;
};

std::optional<uint64_t> parseRangeToken(const std::string& token) {
    if (token == "_undef_") {
        return std::nullopt;
    }
    return static_cast<uint64_t>(std::stoull(token));
}

RangeSpec parseRangeSpec(const std::string& text) {
    RangeSpec spec;
    if (text.size() < 2 || text.front() != '[' || text.back() != ']') {
        std::abort();
    }
    const std::string inner = text.substr(1, text.size() - 2);
    if (inner.empty()) {
        return spec;
    }
    const size_t comma = inner.find(',');
    if (comma == std::string::npos) {
        spec.offset = parseRangeToken(inner);
    } else {
        spec.offset = parseRangeToken(inner.substr(0, comma));
        spec.size = parseRangeToken(inner.substr(comma + 1));
    }
    return spec;
}

// Upstream reifyMapRange: offset = range[0] ?? 0; size = range[1] ?? bufferSize - offset.
std::pair<uint64_t, uint64_t> reifyMapRange(uint64_t bufferSize, const RangeSpec& range) {
    const uint64_t offset = range.offset.value_or(0);
    return {offset, range.size.value_or(bufferSize - offset)};
}

// getMappedRange argument mapping: JS undefined offset -> 0, undefined size ->
// "rest of the buffer" (WGPU_WHOLE_MAP_SIZE defaults to buffer.size - offset,
// matching the JS default).
size_t rangeOffsetArg(const RangeSpec& range) {
    return static_cast<size_t>(range.offset.value_or(0));
}

size_t rangeSizeArg(const RangeSpec& range) {
    return range.size ? static_cast<size_t>(*range.size) : WGPU_WHOLE_MAP_SIZE;
}

std::vector<Value> mapRegionBoundModes() {
    return {Value("default-expand"), Value("explicit-expand"), Value("minimal")};
}

// Upstream getRegionForMap: produces the [offset, size] arguments for mapAsync.
// JS undefined offset -> 0; JS undefined size -> WGPU_WHOLE_MAP_SIZE.
struct MapRegion {
    size_t offset = 0;
    size_t size = WGPU_WHOLE_MAP_SIZE;
};

MapRegion getRegionForMap(
    uint64_t bufferSize,
    uint64_t rangeOffset,
    uint64_t rangeSize,
    const std::string& mapAsyncRegionLeft,
    const std::string& mapAsyncRegionRight) {
    const uint64_t regionLeft = mapAsyncRegionLeft == "minimal" ? rangeOffset : 0;
    const uint64_t regionRight =
        mapAsyncRegionRight == "minimal" ? rangeOffset + rangeSize : bufferSize;

    MapRegion region;
    region.offset = mapAsyncRegionLeft == "default-expand" ? 0 : static_cast<size_t>(regionLeft);
    region.size = mapAsyncRegionRight == "default-expand"
                      ? WGPU_WHOLE_MAP_SIZE
                      : static_cast<size_t>(regionRight - regionLeft);
    return region;
}

// Upstream kSubcases (size + range). See porting note 1 for the range encoding.
std::vector<ParamRecord> kSubcases() {
    return {
        ParamRecord{{"size", 0}, {"range", "[]"}},
        ParamRecord{{"size", 0}, {"range", "[_undef_]"}},
        ParamRecord{{"size", 0}, {"range", "[_undef_,_undef_]"}},
        ParamRecord{{"size", 0}, {"range", "[0]"}},
        ParamRecord{{"size", 0}, {"range", "[0,_undef_]"}},
        ParamRecord{{"size", 0}, {"range", "[0,0]"}},
        ParamRecord{{"size", 12}, {"range", "[]"}},
        ParamRecord{{"size", 12}, {"range", "[_undef_]"}},
        ParamRecord{{"size", 12}, {"range", "[_undef_,_undef_]"}},
        ParamRecord{{"size", 12}, {"range", "[0]"}},
        ParamRecord{{"size", 12}, {"range", "[0,_undef_]"}},
        ParamRecord{{"size", 12}, {"range", "[0,12]"}},
        ParamRecord{{"size", 12}, {"range", "[0,0]"}},
        ParamRecord{{"size", 12}, {"range", "[8]"}},
        ParamRecord{{"size", 12}, {"range", "[8,_undef_]"}},
        ParamRecord{{"size", 12}, {"range", "[8,4]"}},
        ParamRecord{{"size", 28}, {"range", "[8,8]"}},
        ParamRecord{{"size", 28}, {"range", "[8,12]"}},
        ParamRecord{{"size", 512 * 1024}, {"range", "[]"}},
    };
}

// Byte-wise equivalent of upstream checkElementsEqual on Uint8Array views.
void expectBytesEqual(
    MappingTest& t,
    const uint8_t* actual,
    const uint8_t* expected,
    uint64_t size,
    const std::string& label = "") {
    for (uint64_t i = 0; i < size; ++i) {
        if (actual[i] != expected[i]) {
            t.fail((label.empty() ? std::string() : label + ": ") + "mismatch at byte " +
                   std::to_string(i) + ": expected " + std::to_string(expected[i]) + ", got " +
                   std::to_string(actual[i]));
        }
    }
}

// ---------------------------------------------------------------------------
// mapState helpers (mappedAtCreation,mapState / mapAsync,mapState)
// ---------------------------------------------------------------------------

const char* mapStateName(WGPUBufferMapState state) {
    switch (state) {
        case WGPUBufferMapState_Unmapped:
            return "unmapped";
        case WGPUBufferMapState_Pending:
            return "pending";
        case WGPUBufferMapState_Mapped:
            return "mapped";
        default:
            return "unknown";
    }
}

void expectMapState(
    MappingTest& t,
    WGPUBuffer buffer,
    WGPUBufferMapState expected,
    const std::string& when) {
    const WGPUBufferMapState actual = wgpuBufferGetMapState(buffer);
    t.expect(
        actual == expected,
        "mapState must be '" + std::string(mapStateName(expected)) + "' " + when + ", got '" +
            std::string(mapStateName(actual)) + "'");
}

struct MapCallbackState {
    bool completed = false;
    WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Error;
};

void onMapAsyncDone(WGPUMapAsyncStatus status, WGPUStringView, void* userdata1, void*) {
    auto* state = static_cast<MapCallbackState*>(userdata1);
    state->completed = true;
    state->status = status;
}

struct PopScopeState {
    bool completed = false;
    WGPUPopErrorScopeStatus status = WGPUPopErrorScopeStatus_Error;
    WGPUErrorType type = WGPUErrorType_NoError;
};

void onPopScopeDone(
    WGPUPopErrorScopeStatus status,
    WGPUErrorType type,
    WGPUStringView,
    void* userdata1,
    void*) {
    auto* state = static_cast<PopScopeState*>(userdata1);
    state->completed = true;
    state->status = status;
    state->type = type;
}

// Owns a private instance/adapter/device (+ one buffer) so the test controls
// when async callbacks are delivered. Released in reverse order on scope exit
// (fail()/skip() throw, so RAII is required).
struct OwnedDeviceContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUBuffer buffer = nullptr;

    OwnedDeviceContext() = default;
    OwnedDeviceContext(const OwnedDeviceContext&) = delete;
    OwnedDeviceContext& operator=(const OwnedDeviceContext&) = delete;

    ~OwnedDeviceContext() {
        if (buffer != nullptr) {
            wgpuBufferRelease(buffer);
        }
        if (device != nullptr) {
            wgpuDeviceRelease(device);
        }
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
        }
        if (instance != nullptr) {
            wgpuInstanceRelease(instance);
        }
    }
};

void createOwnedDevice(MappingTest& t, OwnedDeviceContext& ctx) {
    ctx.instance = createInstance();
    if (ctx.instance == nullptr) {
        t.fail("failed to create WGPUInstance");
    }
    AdapterResult adapter = requestAdapterSync(ctx.instance, adapterOptions());
    if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
        t.fail("failed to request adapter: " + adapter.message);
    }
    ctx.adapter = adapter.adapter;

    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    DeviceResult device = requestDeviceSync(ctx.instance, ctx.adapter, &desc);
    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        t.fail("failed to request device: " + device.message);
    }
    ctx.device = device.device;
}

// Synchronous expectValidationError against an explicitly owned device.
void expectValidationErrorOnDevice(
    MappingTest& t,
    WGPUInstance instance,
    WGPUDevice device,
    const std::function<void()>& body,
    bool shouldError) {
    wgpuDevicePushErrorScope(device, WGPUErrorFilter_Validation);
    body();
    ScopeResult result = popErrorScopeSync(instance, device);
    if (result.status != WGPUPopErrorScopeStatus_Success) {
        t.fail("popErrorScope failed: " + result.message);
    }
    const bool hadError = result.type != WGPUErrorType_NoError;
    if (shouldError && !hadError) {
        t.fail("expected validation error, got none");
    }
    if (!shouldError && hadError) {
        t.fail("unexpected validation error: " + result.message);
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

CTS_TEST(g, "mapAsync,write")
    .desc(
        "Use map-write to write to various ranges of variously-sized buffers, then expectContents\n"
        "(which does copyBufferToBuffer + map-read) to ensure the contents were written.")
    .params([](ParamsBuilder u) {
        return u.combine("mapAsyncRegionLeft", mapRegionBoundModes())
            .combine("mapAsyncRegionRight", mapRegionBoundModes())
            .beginSubcases()
            .combineWithParams(kSubcases());
    })
    .fn([](MappingTest& t) {
        const uint64_t size = t.param<uint64_t>("size");
        const RangeSpec range = parseRangeSpec(t.param<std::string>("range"));
        const auto reified = reifyMapRange(size, range);
        const uint64_t rangeOffset = reified.first;
        const uint64_t rangeSize = reified.second;

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = size;
        desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        const MapRegion mapRegion = getRegionForMap(
            size,
            rangeOffset,
            rangeSize,
            t.param<std::string>("mapAsyncRegionLeft"),
            t.param<std::string>("mapAsyncRegionRight"));
        t.expectMapAsync(buffer, WGPUMapMode_Write, true, mapRegion.offset, mapRegion.size);
        void* mapped = wgpuBufferGetMappedRange(buffer, rangeOffsetArg(range), rangeSizeArg(range));
        t.checkMapWrite(buffer, rangeOffset, mapped, rangeSize);
    });

CTS_TEST(g, "mapAsync,write,unchanged_ranges_preserved")
    .desc(
        "Use mappedAtCreation or mapAsync to write to various ranges of variously-sized buffers, then\n"
        "use mapAsync to map a different range and zero it out. Finally use expectGPUBufferValuesEqual\n"
        "(which does copyBufferToBuffer + map-read) to verify that contents originally written outside the\n"
        "second mapped range were not altered.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mappedAtCreation", {false, true})
            .combineWithParams({
                ParamRecord{{"size", 12}, {"range1", "[]"}, {"range2", "[8]"}},
                ParamRecord{{"size", 12}, {"range1", "[]"}, {"range2", "[0,8]"}},
                ParamRecord{{"size", 12}, {"range1", "[0,8]"}, {"range2", "[8]"}},
                ParamRecord{{"size", 12}, {"range1", "[8]"}, {"range2", "[0,8]"}},
                ParamRecord{{"size", 28}, {"range1", "[]"}, {"range2", "[8,8]"}},
                ParamRecord{{"size", 28}, {"range1", "[8,16]"}, {"range2", "[16,8]"}},
                ParamRecord{{"size", 32}, {"range1", "[16,12]"}, {"range2", "[8,16]"}},
                ParamRecord{{"size", 32}, {"range1", "[8,8]"}, {"range2", "[24,4]"}},
            });
    })
    .fn([](MappingTest& t) {
        const uint64_t size = t.param<uint64_t>("size");
        const RangeSpec range1 = parseRangeSpec(t.param<std::string>("range1"));
        const RangeSpec range2 = parseRangeSpec(t.param<std::string>("range2"));
        const bool mappedAtCreation = t.param<bool>("mappedAtCreation");
        const auto reified1 = reifyMapRange(size, range1);
        const uint64_t rangeOffset1 = reified1.first;
        const uint64_t rangeSize1 = reified1.second;
        const auto reified2 = reifyMapRange(size, range2);
        const uint64_t rangeOffset2 = reified2.first;
        const uint64_t rangeSize2 = reified2.second;

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.mappedAtCreation = mappedAtCreation ? WGPU_TRUE : 0;
        desc.size = size;
        desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        // If the buffer is not mappedAtCreation map it now.
        if (!mappedAtCreation) {
            t.expectMapAsync(buffer, WGPUMapMode_Write, true);
        }

        // Set the initial contents of the buffer.
        void* init = wgpuBufferGetMappedRange(buffer, rangeOffsetArg(range1), rangeSizeArg(range1));
        if (rangeSize1 > 0 && init == nullptr) {
            t.fail("getMappedRange for the first range returned null");
        }

        // expectedBuffer mirrors upstream's full-buffer-sized expected ArrayBuffer.
        std::vector<uint8_t> expectedBuffer(static_cast<size_t>(size), 0);
        auto* data = static_cast<uint32_t*>(init);
        for (uint64_t i = 0; i < rangeSize1 / 4; ++i) {
            const uint32_t value = static_cast<uint32_t>(i + 1);
            data[i] = value;
            std::memcpy(expectedBuffer.data() + rangeOffset1 + i * 4, &value, sizeof(value));
        }
        wgpuBufferUnmap(buffer);

        // Write to a second range of the buffer.
        t.expectMapAsync(buffer, WGPUMapMode_Write, true, rangeOffsetArg(range2), rangeSizeArg(range2));
        void* init2 = wgpuBufferGetMappedRange(buffer, rangeOffsetArg(range2), rangeSizeArg(range2));
        if (rangeSize2 > 0 && init2 == nullptr) {
            t.fail("getMappedRange for the second range returned null");
        }

        auto* data2 = static_cast<uint32_t*>(init2);
        for (uint64_t i = 0; i < rangeSize2 / 4; ++i) {
            const uint32_t zero = 0;
            data2[i] = zero;
            std::memcpy(expectedBuffer.data() + rangeOffset2 + i * 4, &zero, sizeof(zero));
        }
        wgpuBufferUnmap(buffer);

        // Verify that the range of the buffer which was not overwritten was preserved.
        t.expectGPUBufferValuesEqual(
            buffer,
            expectedBuffer.data() + rangeOffset1,
            static_cast<size_t>(rangeSize1),
            rangeOffset1);
    });

CTS_TEST(g, "mapAsync,read")
    .desc(
        "Use mappedAtCreation to initialize various ranges of variously-sized buffers, then\n"
        "map-read and check the read-back result.")
    .params([](ParamsBuilder u) {
        return u.combine("mapAsyncRegionLeft", mapRegionBoundModes())
            .combine("mapAsyncRegionRight", mapRegionBoundModes())
            .beginSubcases()
            .combineWithParams(kSubcases());
    })
    .fn([](MappingTest& t) {
        const uint64_t size = t.param<uint64_t>("size");
        const RangeSpec range = parseRangeSpec(t.param<std::string>("range"));
        const auto reified = reifyMapRange(size, range);
        const uint64_t rangeOffset = reified.first;
        const uint64_t rangeSize = reified.second;

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.mappedAtCreation = WGPU_TRUE;
        desc.size = size;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        void* init = wgpuBufferGetMappedRange(buffer, rangeOffsetArg(range), rangeSizeArg(range));
        if (rangeSize > 0 && init == nullptr) {
            t.fail("getMappedRange for initialization returned null");
        }

        std::vector<uint8_t> expected(static_cast<size_t>(rangeSize), 0);
        auto* data = static_cast<uint32_t*>(init);
        for (uint64_t i = 0; i < rangeSize / 4; ++i) {
            const uint32_t value = static_cast<uint32_t>(i + 1);
            data[i] = value;
            std::memcpy(expected.data() + i * 4, &value, sizeof(value));
        }
        wgpuBufferUnmap(buffer);

        const MapRegion mapRegion = getRegionForMap(
            size,
            rangeOffset,
            rangeSize,
            t.param<std::string>("mapAsyncRegionLeft"),
            t.param<std::string>("mapAsyncRegionRight"));
        t.expectMapAsync(buffer, WGPUMapMode_Read, true, mapRegion.offset, mapRegion.size);
        const void* actual =
            wgpuBufferGetConstMappedRange(buffer, rangeOffsetArg(range), rangeSizeArg(range));
        if (rangeSize > 0 && actual == nullptr) {
            t.fail("getMappedRange after map-read returned null");
        }
        expectBytesEqual(t, static_cast<const uint8_t*>(actual), expected.data(), rangeSize);
    });

CTS_TEST(g, "mapAsync,read,typedArrayAccess")
    .desc("Use various TypedArray types to read back from a mapped buffer")
    .params([](ParamsBuilder u) {
        return u.combine("mapAsyncRegionLeft", mapRegionBoundModes())
            .combine("mapAsyncRegionRight", mapRegionBoundModes())
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"size", 80}, {"range", "[]"}},
                ParamRecord{{"size", 160}, {"range", "[]"}},
                ParamRecord{{"size", 160}, {"range", "[0,80]"}},
                ParamRecord{{"size", 160}, {"range", "[80]"}},
                ParamRecord{{"size", 160}, {"range", "[40,120]"}},
                ParamRecord{{"size", 160}, {"range", "[40]"}},
            });
    })
    .fn([](MappingTest& t) {
        const uint64_t size = t.param<uint64_t>("size");
        const RangeSpec range = parseRangeSpec(t.param<std::string>("range"));
        const auto reified = reifyMapRange(size, range);
        const uint64_t rangeOffset = reified.first;
        const uint64_t rangeSize = reified.second;

        // Fill an 80-byte pattern with a variety of values of different types,
        // mirroring the upstream TypedArray writes byte-for-byte.
        std::array<uint8_t, 80> expectedBytes{};
        // Uint8Array at byte 0 (2 elements).
        expectedBytes[0] = 1;
        expectedBytes[1] = 255;
        // Int8Array at byte 2 (2 elements).
        const int8_t int8Expected[2] = {-1, 127};
        std::memcpy(expectedBytes.data() + 2, int8Expected, sizeof(int8Expected));
        // Uint16Array at byte 4 (2 elements).
        const uint16_t uint16Expected[2] = {1, 65535};
        std::memcpy(expectedBytes.data() + 4, uint16Expected, sizeof(uint16Expected));
        // Int16Array at byte 8 (2 elements).
        const int16_t int16Expected[2] = {-1, 32767};
        std::memcpy(expectedBytes.data() + 8, int16Expected, sizeof(int16Expected));
        // Uint32Array at byte 12 (2 elements).
        const uint32_t uint32Expected[2] = {1, 4294967295u};
        std::memcpy(expectedBytes.data() + 12, uint32Expected, sizeof(uint32Expected));
        // Int32Array at byte 20 (2 elements). Upstream writes indices [2] and
        // [3] of this 2-element view; out-of-bounds TypedArray writes are
        // silently dropped in JS, so bytes 20..27 remain zero. Mirror that by
        // leaving them zero here.
        // Float32Array at byte 28 (3 elements).
        const float float32Expected[3] = {1.0f, -1.0f, 12345.6789f};
        std::memcpy(expectedBytes.data() + 28, float32Expected, sizeof(float32Expected));
        // Float64Array at byte 40 (5 elements). Number.MAX_VALUE ==
        // numeric_limits<double>::max(); Number.MIN_VALUE is the smallest
        // positive subnormal double == numeric_limits<double>::denorm_min().
        const double float64Expected[5] = {
            1.0,
            -1.0,
            12345.6789,
            std::numeric_limits<double>::max(),
            std::numeric_limits<double>::denorm_min(),
        };
        std::memcpy(expectedBytes.data() + 40, float64Expected, sizeof(float64Expected));

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.mappedAtCreation = WGPU_TRUE;
        desc.size = size;
        desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        void* init = wgpuBufferGetMappedRange(buffer, rangeOffsetArg(range), rangeSizeArg(range));
        if (init == nullptr) {
            t.fail("getMappedRange for initialization returned null");
        }
        // Copy the expected values into the mapped range (rangeSize >= 80 for
        // every subcase, mirroring upstream's memcpy of the whole pattern).
        std::memcpy(init, expectedBytes.data(), expectedBytes.size());
        wgpuBufferUnmap(buffer);

        const MapRegion mapRegion = getRegionForMap(
            size,
            rangeOffset,
            rangeSize,
            t.param<std::string>("mapAsyncRegionLeft"),
            t.param<std::string>("mapAsyncRegionRight"));
        t.expectMapAsync(buffer, WGPUMapMode_Read, true, mapRegion.offset, mapRegion.size);
        const void* mapped =
            wgpuBufferGetConstMappedRange(buffer, rangeOffsetArg(range), rangeSizeArg(range));
        if (mapped == nullptr) {
            t.fail("getMappedRange after map-read returned null");
        }

        // Compare the same byte segments the upstream TypedArray views cover.
        struct Segment {
            size_t offset;
            size_t size;
            const char* label;
        };
        const std::array<Segment, 8> segments = {{
            {0, 2, "Uint8Array"},
            {2, 2, "Int8Array"},
            {4, 4, "Uint16Array"},
            {8, 4, "Int16Array"},
            {12, 8, "Uint32Array"},
            {20, 8, "Int32Array"},
            {28, 12, "Float32Array"},
            {40, 40, "Float64Array"},
        }};
        const auto* actualBytes = static_cast<const uint8_t*>(mapped);
        for (const Segment& segment : segments) {
            expectBytesEqual(
                t,
                actualBytes + segment.offset,
                expectedBytes.data() + segment.offset,
                segment.size,
                segment.label);
        }
    });

CTS_TEST(g, "mappedAtCreation")
    .desc(
        "Use mappedAtCreation to write to various ranges of variously-sized buffers created either\n"
        "with or without the MAP_WRITE usage (since this could affect the mappedAtCreation upload path),\n"
        "then expectContents (which does copyBufferToBuffer + map-read) to ensure the contents were written.")
    .params([](ParamsBuilder u) {
        return u.combine("mappable", {false, true})
            .beginSubcases()
            .combineWithParams(kSubcases());
    })
    .fn([](MappingTest& t) {
        const uint64_t size = t.param<uint64_t>("size");
        const RangeSpec range = parseRangeSpec(t.param<std::string>("range"));
        const bool mappable = t.param<bool>("mappable");
        const uint64_t rangeSize = reifyMapRange(size, range).second;

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.mappedAtCreation = WGPU_TRUE;
        desc.size = size;
        desc.usage =
            WGPUBufferUsage_CopySrc | (mappable ? WGPUBufferUsage_MapWrite : WGPUBufferUsage_None);
        WGPUBuffer buffer = t.createBufferTracked(desc);

        void* mapped = wgpuBufferGetMappedRange(buffer, rangeOffsetArg(range), rangeSizeArg(range));
        t.checkMapWrite(buffer, range.offset.value_or(0), mapped, rangeSize);
    });

CTS_TEST(g, "remapped_for_write")
    .desc(
        "Use mappedAtCreation or mapAsync to write to various ranges of variously-sized buffers created\n"
        "with the MAP_WRITE usage, then mapAsync again and ensure that the previously written values are\n"
        "still present in the mapped buffer.")
    .params([](ParamsBuilder u) {
        return u.combine("mapAsyncRegionLeft", mapRegionBoundModes())
            .combine("mapAsyncRegionRight", mapRegionBoundModes())
            .beginSubcases()
            .combine("mappedAtCreation", {false, true})
            .combineWithParams(kSubcases());
    })
    .fn([](MappingTest& t) {
        const uint64_t size = t.param<uint64_t>("size");
        const RangeSpec range = parseRangeSpec(t.param<std::string>("range"));
        const bool mappedAtCreation = t.param<bool>("mappedAtCreation");
        const auto reified = reifyMapRange(size, range);
        const uint64_t rangeOffset = reified.first;
        const uint64_t rangeSize = reified.second;

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.mappedAtCreation = mappedAtCreation ? WGPU_TRUE : 0;
        desc.size = size;
        desc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        // If the buffer is not mappedAtCreation map it now.
        if (!mappedAtCreation) {
            t.expectMapAsync(buffer, WGPUMapMode_Write, true);
        }

        // Set the initial contents of the buffer.
        void* init = wgpuBufferGetMappedRange(buffer, rangeOffsetArg(range), rangeSizeArg(range));
        if (rangeSize > 0 && init == nullptr) {
            t.fail("getMappedRange for initialization returned null");
        }

        std::vector<uint8_t> expected(static_cast<size_t>(rangeSize), 0);
        auto* data = static_cast<uint32_t*>(init);
        for (uint64_t i = 0; i < rangeSize / 4; ++i) {
            const uint32_t value = static_cast<uint32_t>(i + 1);
            data[i] = value;
            std::memcpy(expected.data() + i * 4, &value, sizeof(value));
        }
        wgpuBufferUnmap(buffer);

        // Check that upon remapping for WRITE the values in the buffer are
        // still the same.
        const MapRegion mapRegion = getRegionForMap(
            size,
            rangeOffset,
            rangeSize,
            t.param<std::string>("mapAsyncRegionLeft"),
            t.param<std::string>("mapAsyncRegionRight"));
        t.expectMapAsync(buffer, WGPUMapMode_Write, true, mapRegion.offset, mapRegion.size);
        const void* actual =
            wgpuBufferGetMappedRange(buffer, rangeOffsetArg(range), rangeSizeArg(range));
        if (rangeSize > 0 && actual == nullptr) {
            t.fail("getMappedRange after remapping returned null");
        }
        expectBytesEqual(t, static_cast<const uint8_t*>(actual), expected.data(), rangeSize);
    });

CTS_TEST(g, "mappedAtCreation,mapState")
    .desc("Test that exposed map state of buffer created with mappedAtCreation has expected values.")
    .params([](ParamsBuilder u) {
        return u.combine("usageType", {"invalid", "read", "write"})
            .combine("afterUnmap", {false, true})
            .combine("afterDestroy", {false, true});
    })
    .fn([](MappingTest& t) {
        const std::string usageType = t.param<std::string>("usageType");
        const bool afterUnmap = t.param<bool>("afterUnmap");
        const bool afterDestroy = t.param<bool>("afterDestroy");
        const WGPUBufferUsage usage =
            usageType == "read"
                ? (WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead)
                : usageType == "write"
                      ? (WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite)
                      : WGPUBufferUsage_None;
        const bool validationError = usage == WGPUBufferUsage_None;

        WGPUBuffer buffer = nullptr;
        t.expectValidationError(
            [&] {
                WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
                desc.mappedAtCreation = WGPU_TRUE;
                desc.size = 8;
                desc.usage = usage;
                buffer = t.createBufferTracked(desc);
            },
            validationError);
        if (buffer == nullptr) {
            t.fail("createBuffer returned a null handle");
        }

        // mapState must be "mapped" regardless of validation error.
        expectMapState(t, buffer, WGPUBufferMapState_Mapped, "after mappedAtCreation");

        // getMappedRange must not change the map state.
        (void)wgpuBufferGetMappedRange(buffer, 0, 8);
        expectMapState(t, buffer, WGPUBufferMapState_Mapped, "after getMappedRange");

        if (afterUnmap) {
            wgpuBufferUnmap(buffer);
            expectMapState(t, buffer, WGPUBufferMapState_Unmapped, "after unmap");
        }

        if (afterDestroy) {
            wgpuBufferDestroy(buffer);
            expectMapState(t, buffer, WGPUBufferMapState_Unmapped, "after destroy");
        }
    });

CTS_TEST(g, "mapAsync,mapState")
    .desc("Test that exposed map state of buffer mapped with mapAsync has expected values.")
    .params([](ParamsBuilder u) {
        return u.combine("usageType", {"invalid", "read", "write"})
            .combine("mapModeType", {"READ", "WRITE"})
            .combine("beforeUnmap", {false, true})
            .combine("beforeDestroy", {false, true})
            .combine("afterUnmap", {false, true})
            .combine("afterDestroy", {false, true});
    })
    .fn([](MappingTest& t) {
        const std::string usageType = t.param<std::string>("usageType");
        const std::string mapModeType = t.param<std::string>("mapModeType");
        const bool beforeUnmap = t.param<bool>("beforeUnmap");
        const bool beforeDestroy = t.param<bool>("beforeDestroy");
        const bool afterUnmap = t.param<bool>("afterUnmap");
        const bool afterDestroy = t.param<bool>("afterDestroy");

        const WGPUBufferUsage usage =
            usageType == "read"
                ? (WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead)
                : usageType == "write"
                      ? (WGPUBufferUsage_CopySrc | WGPUBufferUsage_MapWrite)
                      : WGPUBufferUsage_None;
        const bool bufferCreationValidationError = usage == WGPUBufferUsage_None;
        const WGPUMapMode mapMode = mapModeType == "READ" ? WGPUMapMode_Read : WGPUMapMode_Write;

        // This test must observe the 'pending' state and unmap/destroy a
        // buffer while its map request is still in flight, which requires
        // control over async callback delivery. Use a privately owned
        // instance/device so the harness does not pump events underneath us
        // (porting note 4).
        OwnedDeviceContext ctx;
        createOwnedDevice(t, ctx);

        expectValidationErrorOnDevice(
            t,
            ctx.instance,
            ctx.device,
            [&] {
                WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
                desc.mappedAtCreation = 0;
                desc.size = 8;
                desc.usage = usage;
                ctx.buffer = wgpuDeviceCreateBuffer(ctx.device, &desc);
            },
            bufferCreationValidationError);
        if (ctx.buffer == nullptr) {
            t.fail("createBuffer returned a null handle");
        }

        expectMapState(t, ctx.buffer, WGPUBufferMapState_Unmapped, "after creation");

        {
            const bool mapAsyncValidationError =
                bufferCreationValidationError ||
                (mapMode == WGPUMapMode_Read && (usage & WGPUBufferUsage_MapRead) == 0) ||
                (mapMode == WGPUMapMode_Write && (usage & WGPUBufferUsage_MapWrite) == 0);

            // Upstream wraps the mapAsync call in expectValidationError and
            // checks 'pending' before the promise settles. The pop closes the
            // scope window immediately, but its result (like the map promise)
            // is only delivered later — mirror that by issuing the pop without
            // pumping events, and collecting both results after the
            // unmap/destroy-before-resolve steps.
            MapCallbackState mapResult;
            PopScopeState popResult;
            wgpuDevicePushErrorScope(ctx.device, WGPUErrorFilter_Validation);
            {
                WGPUBufferMapCallbackInfo callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
                callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
                callbackInfo.callback = onMapAsyncDone;
                callbackInfo.userdata1 = &mapResult;
                (void)wgpuBufferMapAsync(ctx.buffer, mapMode, 0, WGPU_WHOLE_MAP_SIZE, callbackInfo);
            }
            {
                WGPUPopErrorScopeCallbackInfo popInfo = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
                popInfo.mode = WGPUCallbackMode_AllowProcessEvents;
                popInfo.callback = onPopScopeDone;
                popInfo.userdata1 = &popResult;
                (void)wgpuDevicePopErrorScope(ctx.device, popInfo);
            }
            if (mapAsyncValidationError) {
                // Upstream JS pins mapState === 'pending' synchronously after
                // mapAsync() even when the map request will be rejected for
                // validation reasons. Native implementations (Dawn and yawgpu
                // agree) may reject such a mapAsync eagerly, so
                // wgpuBufferGetMapState can already report Unmapped here; the
                // C API does not pin 'pending'. Accept either state.
                const WGPUBufferMapState actual = wgpuBufferGetMapState(ctx.buffer);
                t.expect(
                    actual == WGPUBufferMapState_Pending ||
                        actual == WGPUBufferMapState_Unmapped,
                    "mapState must be 'pending' or 'unmapped' right after an invalid mapAsync, "
                    "got '" + std::string(mapStateName(actual)) + "'");
            } else {
                expectMapState(t, ctx.buffer, WGPUBufferMapState_Pending, "right after mapAsync");
            }

            if (beforeUnmap) {
                wgpuBufferUnmap(ctx.buffer);
                expectMapState(
                    t, ctx.buffer, WGPUBufferMapState_Unmapped, "after unmap before resolve");
            }
            if (beforeDestroy) {
                wgpuBufferDestroy(ctx.buffer);
                expectMapState(
                    t, ctx.buffer, WGPUBufferMapState_Unmapped, "after destroy before resolve");
            }

            // "await promise" — deliver the map callback (and the pop result).
            if (!processEventsUntil(ctx.instance, [&] {
                    return mapResult.completed && popResult.completed;
                })) {
                t.fail("mapAsync / popErrorScope callbacks did not fire");
            }

            if (popResult.status != WGPUPopErrorScopeStatus_Success) {
                t.fail("popErrorScope after mapAsync failed");
            }
            const bool hadError = popResult.type != WGPUErrorType_NoError;
            if (mapAsyncValidationError && !hadError) {
                t.fail("expected a validation error from mapAsync, got none");
            }
            if (!mapAsyncValidationError && hadError) {
                t.fail("unexpected validation error from mapAsync");
            }

            if (mapResult.status == WGPUMapAsyncStatus_Success) {
                expectMapState(t, ctx.buffer, WGPUBufferMapState_Mapped, "after mapAsync resolved");

                // getMappedRange must not change the map state. (JS
                // getMappedRange maps to the const variant for read mode.)
                if (mapMode == WGPUMapMode_Read) {
                    (void)wgpuBufferGetConstMappedRange(ctx.buffer, 0, 8);
                } else {
                    (void)wgpuBufferGetMappedRange(ctx.buffer, 0, 8);
                }
                expectMapState(t, ctx.buffer, WGPUBufferMapState_Mapped, "after getMappedRange");
            } else {
                // Unmapped before resolve, destroyed before resolve, or
                // mapAsync validation error all end up with rejection and
                // 'unmapped'.
                expectMapState(
                    t, ctx.buffer, WGPUBufferMapState_Unmapped, "after mapAsync rejected");
            }
        }

        // If the buffer is already mapped test mapAsync on the already mapped buffer.
        if (wgpuBufferGetMapState(ctx.buffer) == WGPUBufferMapState_Mapped) {
            // mapAsync on an already mapped buffer must be rejected with a
            // validation error and the map state must stay 'mapped'.
            MapCallbackState mapResult2;
            PopScopeState popResult2;
            wgpuDevicePushErrorScope(ctx.device, WGPUErrorFilter_Validation);
            {
                WGPUBufferMapCallbackInfo callbackInfo = WGPU_BUFFER_MAP_CALLBACK_INFO_INIT;
                callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
                callbackInfo.callback = onMapAsyncDone;
                callbackInfo.userdata1 = &mapResult2;
                (void)wgpuBufferMapAsync(
                    ctx.buffer, WGPUMapMode_Write, 0, WGPU_WHOLE_MAP_SIZE, callbackInfo);
            }
            {
                WGPUPopErrorScopeCallbackInfo popInfo = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
                popInfo.mode = WGPUCallbackMode_AllowProcessEvents;
                popInfo.callback = onPopScopeDone;
                popInfo.userdata1 = &popResult2;
                (void)wgpuDevicePopErrorScope(ctx.device, popInfo);
            }
            expectMapState(
                t, ctx.buffer, WGPUBufferMapState_Mapped, "right after mapAsync on a mapped buffer");

            if (!processEventsUntil(ctx.instance, [&] {
                    return mapResult2.completed && popResult2.completed;
                })) {
                t.fail("second mapAsync / popErrorScope callbacks did not fire");
            }
            if (popResult2.status != WGPUPopErrorScopeStatus_Success) {
                t.fail("popErrorScope after second mapAsync failed");
            }
            if (popResult2.type == WGPUErrorType_NoError) {
                t.fail("mapAsync on an already mapped buffer must produce a validation error");
            }
            if (mapResult2.status == WGPUMapAsyncStatus_Success) {
                t.fail("mapAsync on already mapped buffer must not succeed.");
            }
            expectMapState(
                t, ctx.buffer, WGPUBufferMapState_Mapped, "after rejected mapAsync on a mapped buffer");
        }

        if (afterUnmap) {
            wgpuBufferUnmap(ctx.buffer);
            expectMapState(t, ctx.buffer, WGPUBufferMapState_Unmapped, "after final unmap");
        }

        if (afterDestroy) {
            wgpuBufferDestroy(ctx.buffer);
            expectMapState(t, ctx.buffer, WGPUBufferMapState_Unmapped, "after final destroy");
        }
    });

} // namespace
