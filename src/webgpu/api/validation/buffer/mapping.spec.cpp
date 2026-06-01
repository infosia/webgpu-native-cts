// Ported from gpuweb/cts src/webgpu/api/validation/buffer/mapping.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// C getMappedRange returns a raw pointer. Invalid calls are observed as nullptr returns, not
// validation errors, and JS ArrayBuffer byteLength/detach assertions have no C API equivalent.

#include <array>
#include <cstddef>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,buffer,mapping",
    "Validation tests for GPUBuffer.mapAsync.");

constexpr std::array<WGPUMapMode, 2> kMapModeOptions = {
    WGPUMapMode_Read,
    WGPUMapMode_Write,
};
constexpr uint64_t kOffsetAlignment = 8;
constexpr uint64_t kSizeAlignment = 4;

std::vector<Value> mapModeValues() {
    std::vector<Value> values;
    values.reserve(kMapModeOptions.size());
    for (WGPUMapMode mode : kMapModeOptions) {
        values.emplace_back(static_cast<int64_t>(mode));
    }
    return values;
}

std::vector<Value> bufferUsageValues() {
    std::vector<Value> values;
    values.reserve(kBufferUsages.size());
    for (WGPUBufferUsage usage : kBufferUsages) {
        values.emplace_back(static_cast<int64_t>(usage));
    }
    return values;
}

WGPUBuffer createMappableBuffer(GpuTest& t, WGPUMapMode mode, uint64_t size) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = mode == WGPUMapMode_Read ? WGPUBufferUsage_MapRead : WGPUBufferUsage_MapWrite;
    return t.createBufferTracked(desc);
}

WGPUBuffer createMappedAtCreationBuffer(GpuTest& t, WGPUBufferUsage usage, uint64_t size) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    desc.mappedAtCreation = WGPU_TRUE;
    return t.createBufferTracked(desc);
}

void expectGetMappedRange(
    GpuTest& t,
    bool success,
    WGPUMapMode mode,
    WGPUBuffer buffer,
    size_t offset = 0,
    size_t size = WGPU_WHOLE_MAP_SIZE) {
    const void* range = nullptr;
    if (mode == WGPUMapMode_Read) {
        range = wgpuBufferGetConstMappedRange(buffer, offset, size);
    } else if (mode == WGPUMapMode_Write) {
        range = wgpuBufferGetMappedRange(buffer, offset, size);
    } else {
        t.fail("unexpected map mode");
    }

    if (success && range == nullptr) {
        t.fail("expected mapped range");
    }
    if (!success && range != nullptr) {
        t.fail("expected null mapped range");
    }
}

bool paramIsUndefinedOrMissing(GpuTest& t, std::string_view key) {
    return !t.hasParam(key) || t.paramIsUndefined(key);
}

size_t paramSizeOr(GpuTest& t, std::string_view key, size_t fallback) {
    return paramIsUndefinedOrMissing(t, key) ? fallback : static_cast<size_t>(t.param<int>(key));
}

void expectMapAsyncAndUnmap(
    GpuTest& t,
    WGPUBuffer buffer,
    WGPUMapMode mode,
    bool expectSuccess,
    size_t offset = 0,
    size_t size = WGPU_WHOLE_MAP_SIZE) {
    t.expectMapAsync(buffer, mode, expectSuccess, offset, size);
    if (expectSuccess) {
        wgpuBufferUnmap(buffer);
    }
}

CTS_TEST(g, "mapAsync,usage")
    .desc("Test the usage validation for mapAsync.")
    .params([](ParamsBuilder u) {
        std::vector<Value> usages;
        usages.reserve(kBufferUsages.size());
        for (WGPUBufferUsage usage : kBufferUsages) {
            usages.emplace_back(static_cast<int64_t>(usage));
        }
        return u.beginSubcases()
            .combineWithParams({
                ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_Read)},
                            {"validUsage", static_cast<int64_t>(WGPUBufferUsage_MapRead)}},
                ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_Write)},
                            {"validUsage", static_cast<int64_t>(WGPUBufferUsage_MapWrite)}},
                ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_None)}, {"validUsage", 0}},
            })
            .combine("usage", std::move(usages));
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const WGPUBufferUsage validUsage = t.param<WGPUBufferUsage>("validUsage");
        const WGPUBufferUsage usage = t.param<WGPUBufferUsage>("usage");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = usage;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        const bool expectSuccess = mapMode != WGPUMapMode_None && usage == validUsage;
        t.expectMapAsync(buffer, mapMode, expectSuccess);
        if (expectSuccess) {
            wgpuBufferUnmap(buffer);
        }
    });

CTS_TEST(g, "mapAsync,invalidBuffer")
    .desc("Test that mapAsync is an error when called on an invalid buffer.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", {
            static_cast<int64_t>(WGPUMapMode_Read),
            static_cast<int64_t>(WGPUMapMode_Write),
        });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = t.getErrorBuffer();
        t.expectMapAsync(buffer, mapMode, false);
    });

CTS_TEST(g, "mapAsync,state,destroyed")
    .desc("Test that mapAsync is an error when called on a destroyed buffer.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", {
            static_cast<int64_t>(WGPUMapMode_Read),
            static_cast<int64_t>(WGPUMapMode_Write),
        });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);

        wgpuBufferDestroy(buffer);
        t.expectMapAsync(buffer, mapMode, false);
    });

CTS_TEST(g, "mapAsync,state,mappedAtCreation")
    .desc("Test that mapAsync is an error on a mapped-at-creation buffer, but succeeds after unmapping it.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_Read)},
                        {"validUsage", static_cast<int64_t>(WGPUBufferUsage_MapRead)}},
            ParamRecord{{"mapMode", static_cast<int64_t>(WGPUMapMode_Write)},
                        {"validUsage", static_cast<int64_t>(WGPUBufferUsage_MapWrite)}},
        });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const WGPUBufferUsage validUsage = t.param<WGPUBufferUsage>("validUsage");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = validUsage;
        desc.mappedAtCreation = WGPU_TRUE;
        WGPUBuffer buffer = t.createBufferTracked(desc);

        t.expectMapAsync(buffer, mapMode, false);
        wgpuBufferUnmap(buffer);
        t.expectMapAsync(buffer, mapMode, true);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "mapAsync,state,mapped")
    .desc("Test that mapAsync is an error on a mapped buffer, but succeeds after unmapping it.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", {
            static_cast<int64_t>(WGPUMapMode_Read),
            static_cast<int64_t>(WGPUMapMode_Write),
        });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);

        t.expectMapAsync(buffer, mapMode, true);
        t.expectMapAsync(buffer, mapMode, false);
        wgpuBufferUnmap(buffer);
        t.expectMapAsync(buffer, mapMode, true);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "mapAsync,state,mappingPending")
    .desc("Test that mapAsync is rejected when called on a buffer that is being mapped.")
    .unimplemented("JS microtask/pending-map timing");

CTS_TEST(g, "mapAsync,sizeUnspecifiedOOB")
    .desc("Test mapAsync offset bounds when size is unspecified.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mapMode", mapModeValues())
            .combineWithParams({
                ParamRecord{{"bufferSize", 0}, {"offset", 0}},
                ParamRecord{{"bufferSize", 0}, {"offset", 1}},
                ParamRecord{{"bufferSize", 0}, {"offset", static_cast<int64_t>(kOffsetAlignment)}},
                ParamRecord{{"bufferSize", 16}, {"offset", 0}},
                ParamRecord{{"bufferSize", 16}, {"offset", static_cast<int64_t>(kOffsetAlignment)}},
                ParamRecord{{"bufferSize", 16}, {"offset", 16}},
                ParamRecord{{"bufferSize", 16}, {"offset", 17}},
                ParamRecord{{"bufferSize", 16}, {"offset", static_cast<int64_t>(16 + kOffsetAlignment)}},
            });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const size_t bufferSize = static_cast<size_t>(t.param<int>("bufferSize"));
        const size_t offset = static_cast<size_t>(t.param<int>("offset"));
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, bufferSize);
        expectMapAsyncAndUnmap(t, buffer, mapMode, offset <= bufferSize, offset);
    });

CTS_TEST(g, "mapAsync,offsetAndSizeAlignment")
    .desc("Test mapAsync offset and size alignment.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mapMode", mapModeValues())
            .combine("offset", {Value(0), Value(static_cast<int64_t>(kOffsetAlignment)),
                                Value(static_cast<int64_t>(kOffsetAlignment / 2))})
            .combine("size", {Value(0), Value(static_cast<int64_t>(kSizeAlignment)),
                              Value(static_cast<int64_t>(kSizeAlignment / 2))});
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const size_t offset = static_cast<size_t>(t.param<int>("offset"));
        const size_t size = static_cast<size_t>(t.param<int>("size"));
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);
        const bool success = offset % kOffsetAlignment == 0 && size % kSizeAlignment == 0;
        expectMapAsyncAndUnmap(t, buffer, mapMode, success, offset, size);
    });

CTS_TEST(g, "mapAsync,offsetAndSizeOOB")
    .desc("Test mapAsync offset plus size bounds.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mapMode", mapModeValues())
            .combineWithParams({
                ParamRecord{{"bufferSize", 0}, {"offset", 0}, {"size", 0}},
                ParamRecord{{"bufferSize", 0}, {"offset", 0}, {"size", 4}},
                ParamRecord{{"bufferSize", 0}, {"offset", 8}, {"size", 0}},
                ParamRecord{{"bufferSize", 16}, {"offset", 0}, {"size", 16}},
                ParamRecord{{"bufferSize", 16}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 16}},
                ParamRecord{{"bufferSize", 16}, {"offset", 16}, {"size", 0}},
                ParamRecord{{"bufferSize", 16}, {"offset", 16}, {"size", static_cast<int64_t>(kSizeAlignment)}},
                ParamRecord{{"bufferSize", 16}, {"offset", 8}, {"size", 0}},
                ParamRecord{{"bufferSize", 16}, {"offset", 8}, {"size", 8}},
                ParamRecord{{"bufferSize", 16}, {"offset", 8}, {"size", static_cast<int64_t>(8 + kSizeAlignment)}},
                ParamRecord{{"bufferSize", 1024}, {"offset", 0}, {"size", 1024}},
                ParamRecord{{"bufferSize", 1024}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 1024}},
                ParamRecord{{"bufferSize", 1024}, {"offset", 1024}, {"size", 0}},
                ParamRecord{{"bufferSize", 1024}, {"offset", 1024}, {"size", static_cast<int64_t>(kSizeAlignment)}},
                ParamRecord{{"bufferSize", 1024}, {"offset", 512}, {"size", 0}},
                ParamRecord{{"bufferSize", 1024}, {"offset", 512}, {"size", 512}},
                ParamRecord{{"bufferSize", 1024}, {"offset", 512}, {"size", static_cast<int64_t>(512 + kSizeAlignment)}},
            });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const size_t bufferSize = static_cast<size_t>(t.param<int>("bufferSize"));
        const size_t offset = static_cast<size_t>(t.param<int>("offset"));
        const size_t size = static_cast<size_t>(t.param<int>("size"));
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, bufferSize);
        expectMapAsyncAndUnmap(t, buffer, mapMode, offset + size <= bufferSize, offset, size);
    });

CTS_TEST(g, "mapAsync,earlyRejection")
    .desc("Test early mapAsync rejection timing.")
    .unimplemented("JS microtask/promise early-rejection timing has no synchronous C API analog");

CTS_TEST(g, "mapAsync,abort_over_invalid_error")
    .desc("Test AbortError precedence over validation errors.")
    .unimplemented("AbortError-vs-OperationError promise precedence has no synchronous C API analog");

CTS_TEST(g, "getMappedRange,state,mapped")
    .desc("Test getMappedRange in the mapped state.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", mapModeValues());
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);
        t.expectMapAsync(buffer, mapMode, true);
        expectGetMappedRange(t, true, mapMode, buffer);
        t.expectMapAsync(buffer, mapMode, false);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "getMappedRange,state,mappedAtCreation")
    .desc("Test getMappedRange in the mapped-at-creation state.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("bufferUsage", bufferUsageValues())
            .combine("mapMode", mapModeValues());
    })
    .fn([](GpuTest& t) {
        const WGPUBufferUsage usage = t.param<WGPUBufferUsage>("bufferUsage");
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappedAtCreationBuffer(t, usage, 16);
        expectGetMappedRange(t, true, mapMode, buffer);
        t.expectMapAsync(buffer, mapMode, false);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "getMappedRange,state,invalid_mappedAtCreation")
    .desc("Test getMappedRange on an invalid mapped-at-creation buffer.")
    .fn([](GpuTest& t) {
        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = 16;
        desc.usage = 0xFFFFFFFF;
        desc.mappedAtCreation = WGPU_TRUE;
        WGPUBuffer buffer = nullptr;
        t.expectValidationError([&] {
            buffer = t.createBufferTracked(desc);
        }, true);
        if (buffer == nullptr) {
            t.fail("invalid mapped-at-creation buffer was null");
        }
        expectGetMappedRange(t, true, WGPUMapMode_Read, buffer);
    });

CTS_TEST(g, "getMappedRange,state,mappedAgain")
    .desc("Test getMappedRange after a duplicate mapAsync fails.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", mapModeValues());
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);
        t.expectMapAsync(buffer, mapMode, true);
        t.expectMapAsync(buffer, mapMode, false);
        expectGetMappedRange(t, true, mapMode, buffer);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "getMappedRange,state,unmapped")
    .desc("Test getMappedRange in the unmapped state.")
    .fn([](GpuTest& t) {
        {
            WGPUBuffer buffer = createMappableBuffer(t, WGPUMapMode_Read, 16);
            expectGetMappedRange(t, false, WGPUMapMode_Read, buffer);
        }
        {
            WGPUBuffer buffer = createMappableBuffer(t, WGPUMapMode_Read, 16);
            t.expectMapAsync(buffer, WGPUMapMode_Read, true);
            wgpuBufferUnmap(buffer);
            expectGetMappedRange(t, false, WGPUMapMode_Read, buffer);
        }
        {
            WGPUBuffer buffer = createMappedAtCreationBuffer(t, WGPUBufferUsage_MapRead, 16);
            wgpuBufferUnmap(buffer);
            expectGetMappedRange(t, false, WGPUMapMode_Read, buffer);
        }
    });

CTS_TEST(g, "getMappedRange,state,destroyed")
    .desc("Test getMappedRange in the destroyed state.")
    .fn([](GpuTest& t) {
        {
            WGPUBuffer buffer = createMappableBuffer(t, WGPUMapMode_Read, 16);
            wgpuBufferDestroy(buffer);
            expectGetMappedRange(t, false, WGPUMapMode_Read, buffer);
        }
        {
            WGPUBuffer buffer = createMappableBuffer(t, WGPUMapMode_Read, 16);
            t.expectMapAsync(buffer, WGPUMapMode_Read, true);
            wgpuBufferDestroy(buffer);
            expectGetMappedRange(t, false, WGPUMapMode_Read, buffer);
        }
        {
            WGPUBuffer buffer = createMappedAtCreationBuffer(t, WGPUBufferUsage_MapRead, 16);
            wgpuBufferDestroy(buffer);
            expectGetMappedRange(t, false, WGPUMapMode_Read, buffer);
        }
    });

CTS_TEST(g, "getMappedRange,state,mappingPending")
    .desc("Test getMappedRange in the mapping-pending state.")
    .unimplemented("JS promise pending-map state has no synchronous C API analog");

CTS_TEST(g, "getMappedRange,subrange,mapped")
    .desc("Test getMappedRange subranges after remapping.")
    .params([](ParamsBuilder u) {
        return u.combine("mapMode", mapModeValues());
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);
        t.expectMapAsync(buffer, mapMode, true);
        expectGetMappedRange(t, true, mapMode, buffer);
        wgpuBufferUnmap(buffer);
        t.expectMapAsync(buffer, mapMode, true, 8);
        expectGetMappedRange(t, true, mapMode, buffer, 8);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "getMappedRange,subrange,mappedAtCreation")
    .desc("Test getMappedRange subranges after remapping a mapped-at-creation buffer.")
    .fn([](GpuTest& t) {
        WGPUBuffer buffer = createMappedAtCreationBuffer(t, WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead, 16);
        expectGetMappedRange(t, true, WGPUMapMode_Read, buffer);
        wgpuBufferUnmap(buffer);
        t.expectMapAsync(buffer, WGPUMapMode_Read, true, 8);
        expectGetMappedRange(t, true, WGPUMapMode_Read, buffer, 8);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "getMappedRange,offsetAndSizeAlignment,mapped")
    .desc("Test getMappedRange offset and size alignment on mapAsync mappings.")
    .params([](ParamsBuilder u) {
        return u.combine("mapMode", mapModeValues())
            .beginSubcases()
            .combine("mapOffset", {Value(0), Value(static_cast<int64_t>(kOffsetAlignment))})
            .combine("offset", {Value(0), Value(static_cast<int64_t>(kOffsetAlignment)),
                                Value(static_cast<int64_t>(kOffsetAlignment / 2))})
            .combine("size", {Value(0), Value(static_cast<int64_t>(kSizeAlignment)),
                              Value(static_cast<int64_t>(kSizeAlignment / 2))});
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const size_t mapOffset = static_cast<size_t>(t.param<int>("mapOffset"));
        const size_t offset = static_cast<size_t>(t.param<int>("offset"));
        const size_t size = static_cast<size_t>(t.param<int>("size"));
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 32);
        t.expectMapAsync(buffer, mapMode, true, mapOffset);
        const bool success = offset % kOffsetAlignment == 0 && size % kSizeAlignment == 0;
        expectGetMappedRange(t, success, mapMode, buffer, offset + mapOffset, size);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "getMappedRange,offsetAndSizeAlignment,mappedAtCreation")
    .desc("Test getMappedRange offset and size alignment on mapped-at-creation buffers.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("offset", {Value(0), Value(static_cast<int64_t>(kOffsetAlignment)),
                                Value(static_cast<int64_t>(kOffsetAlignment / 2))})
            .combine("size", {Value(0), Value(static_cast<int64_t>(kSizeAlignment)),
                              Value(static_cast<int64_t>(kSizeAlignment / 2))});
    })
    .fn([](GpuTest& t) {
        const size_t offset = static_cast<size_t>(t.param<int>("offset"));
        const size_t size = static_cast<size_t>(t.param<int>("size"));
        WGPUBuffer buffer = createMappedAtCreationBuffer(t, WGPUBufferUsage_CopyDst, 16);
        const bool success = offset % kOffsetAlignment == 0 && size % kSizeAlignment == 0;
        expectGetMappedRange(t, success, WGPUMapMode_Read, buffer, offset, size);
    });

CTS_TEST(g, "getMappedRange,sizeAndOffsetOOB,mappedAtCreation")
    .desc("Test getMappedRange bounds on mapped-at-creation buffers.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"bufferSize", 0}, {"offset", Value::undef()}, {"size", Value::undef()}},
            ParamRecord{{"bufferSize", 0}, {"offset", Value::undef()}, {"size", 0}},
            ParamRecord{{"bufferSize", 0}, {"offset", Value::undef()}, {"size", static_cast<int64_t>(kSizeAlignment)}},
            ParamRecord{{"bufferSize", 0}, {"offset", 0}, {"size", Value::undef()}},
            ParamRecord{{"bufferSize", 0}, {"offset", 0}, {"size", 0}},
            ParamRecord{{"bufferSize", 0}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", Value::undef()}},
            ParamRecord{{"bufferSize", 0}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 0}},
            ParamRecord{{"bufferSize", 80}, {"offset", Value::undef()}, {"size", 80}},
            ParamRecord{{"bufferSize", 80}, {"offset", Value::undef()}, {"size", static_cast<int64_t>(80 + kSizeAlignment)}},
            ParamRecord{{"bufferSize", 80}, {"offset", Value::undef()}, {"size", Value::undef()}},
            ParamRecord{{"bufferSize", 80}, {"offset", 0}, {"size", Value::undef()}},
            ParamRecord{{"bufferSize", 80}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", Value::undef()}},
            ParamRecord{{"bufferSize", 80}, {"offset", 80}, {"size", Value::undef()}},
            ParamRecord{{"bufferSize", 80}, {"offset", static_cast<int64_t>(80 + kOffsetAlignment)}, {"size", Value::undef()}},
            ParamRecord{{"bufferSize", 80}, {"offset", 0}, {"size", 80}},
            ParamRecord{{"bufferSize", 80}, {"offset", 0}, {"size", static_cast<int64_t>(80 + kSizeAlignment)}},
            ParamRecord{{"bufferSize", 80}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 80}},
            ParamRecord{{"bufferSize", 80}, {"offset", 40}, {"size", 40}},
            ParamRecord{{"bufferSize", 80}, {"offset", static_cast<int64_t>(40 + kOffsetAlignment)}, {"size", 40}},
            ParamRecord{{"bufferSize", 80}, {"offset", 40}, {"size", static_cast<int64_t>(40 + kSizeAlignment)}},
        });
    })
    .fn([](GpuTest& t) {
        const size_t bufferSize = static_cast<size_t>(t.param<int>("bufferSize"));
        const size_t offset = paramSizeOr(t, "offset", 0);
        const size_t defaultSize = offset <= bufferSize ? bufferSize - offset : 0;
        const size_t size = paramSizeOr(t, "size", defaultSize);
        WGPUBuffer buffer = createMappedAtCreationBuffer(t, WGPUBufferUsage_CopyDst, bufferSize);
        const bool success = offset <= bufferSize && offset + size <= bufferSize;
        expectGetMappedRange(t, success, WGPUMapMode_Read, buffer, offset, paramIsUndefinedOrMissing(t, "size") ? WGPU_WHOLE_MAP_SIZE : size);
    });

CTS_TEST(g, "getMappedRange,sizeAndOffsetOOB,mapped")
    .desc("Test getMappedRange bounds on mapAsync mappings.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mapMode", mapModeValues())
            .combineWithParams({
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", Value::undef()}, {"offset", Value::undef()}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", Value::undef()}, {"offset", Value::undef()}, {"size", 0}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", Value::undef()}, {"offset", Value::undef()}, {"size", static_cast<int64_t>(kSizeAlignment)}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", Value::undef()}, {"offset", 0}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", Value::undef()}, {"offset", 0}, {"size", 0}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", Value::undef()}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", Value::undef()}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 0}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", 0}, {"offset", Value::undef()}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", 0}, {"offset", 0}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", 0}, {"offset", 0}, {"size", 0}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", 0}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 0}, {"mapOffset", 0}, {"mapSize", 0}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 0}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", Value::undef()}, {"mapSize", Value::undef()}, {"offset", 0}, {"size", 80}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", Value::undef()}, {"mapSize", Value::undef()}, {"offset", 0}, {"size", static_cast<int64_t>(80 + kSizeAlignment)}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", Value::undef()}, {"mapSize", Value::undef()}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 80}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", Value::undef()}, {"offset", 24}, {"size", 56}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", Value::undef()}, {"offset", 0}, {"size", static_cast<int64_t>(56 + kSizeAlignment)}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", Value::undef()}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 56}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 0}, {"mapSize", 80}, {"offset", 0}, {"size", 80}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 0}, {"mapSize", 80}, {"offset", static_cast<int64_t>(kOffsetAlignment)}, {"size", 80}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 0}, {"mapSize", 80}, {"offset", 0}, {"size", static_cast<int64_t>(80 + kSizeAlignment)}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 0}, {"mapSize", 80}, {"offset", 40}, {"size", 40}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 0}, {"mapSize", 80}, {"offset", static_cast<int64_t>(40 + kOffsetAlignment)}, {"size", 40}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 0}, {"mapSize", 80}, {"offset", 40}, {"size", static_cast<int64_t>(40 + kSizeAlignment)}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", 40}, {"offset", 24}, {"size", 40}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", 40}, {"offset", static_cast<int64_t>(24 - kOffsetAlignment)}, {"size", 40}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", 40}, {"offset", static_cast<int64_t>(24 + kOffsetAlignment)}, {"size", 40}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", 40}, {"offset", 24}, {"size", static_cast<int64_t>(40 + kSizeAlignment)}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", 40}, {"offset", Value::undef()}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", 40}, {"offset", 0}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", 40}, {"offset", 24}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", Value::undef()}, {"offset", 24}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 24}, {"mapSize", Value::undef()}, {"offset", 80}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 0}, {"mapSize", 64}, {"offset", Value::undef()}, {"size", Value::undef()}},
                ParamRecord{{"bufferSize", 80}, {"mapOffset", 0}, {"mapSize", 64}, {"offset", Value::undef()}, {"size", 64}},
            });
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        const size_t bufferSize = static_cast<size_t>(t.param<int>("bufferSize"));
        const size_t mapOffset = paramSizeOr(t, "mapOffset", 0);
        const size_t mapSize = paramSizeOr(t, "mapSize", bufferSize - mapOffset);
        const size_t offset = paramSizeOr(t, "offset", 0);
        const size_t defaultSize = offset <= bufferSize ? bufferSize - offset : 0;
        const size_t size = paramSizeOr(t, "size", defaultSize);
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, bufferSize);
        t.expectMapAsync(
            buffer,
            mapMode,
            true,
            mapOffset,
            paramIsUndefinedOrMissing(t, "mapSize") ? WGPU_WHOLE_MAP_SIZE : mapSize);

        const bool success =
            offset >= mapOffset &&
            offset <= bufferSize &&
            offset + size <= mapOffset + mapSize;
        expectGetMappedRange(
            t,
            success,
            mapMode,
            buffer,
            offset,
            paramIsUndefinedOrMissing(t, "size") ? WGPU_WHOLE_MAP_SIZE : size);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "getMappedRange,disjointRanges")
    .desc("Test getMappedRange disjoint ranges.")
    .unimplemented("C API does not enforce overlapping getMappedRange disjoint-range rejection");

CTS_TEST(g, "getMappedRange,disjointRanges_many")
    .desc("Test many getMappedRange disjoint ranges.")
    .unimplemented("C API does not enforce overlapping getMappedRange disjoint-range rejection");

CTS_TEST(g, "unmap,state,unmapped")
    .desc("Test unmap in the unmapped state.")
    .fn([](GpuTest& t) {
        {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = 16;
            desc.usage = WGPUBufferUsage_MapRead;
            WGPUBuffer buffer = t.createBufferTracked(desc);
            wgpuBufferUnmap(buffer);
        }
        {
            WGPUBuffer buffer = createMappableBuffer(t, WGPUMapMode_Read, 16);
            t.expectMapAsync(buffer, WGPUMapMode_Read, true);
            wgpuBufferUnmap(buffer);
            wgpuBufferUnmap(buffer);
        }
        {
            WGPUBuffer buffer = createMappedAtCreationBuffer(t, WGPUBufferUsage_MapRead, 16);
            wgpuBufferUnmap(buffer);
            wgpuBufferUnmap(buffer);
        }
    });

CTS_TEST(g, "unmap,state,destroyed")
    .desc("Test unmap in the destroyed state.")
    .fn([](GpuTest& t) {
        {
            WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
            desc.size = 16;
            desc.usage = WGPUBufferUsage_MapRead;
            WGPUBuffer buffer = t.createBufferTracked(desc);
            wgpuBufferDestroy(buffer);
            wgpuBufferUnmap(buffer);
        }
        {
            WGPUBuffer buffer = createMappableBuffer(t, WGPUMapMode_Read, 16);
            t.expectMapAsync(buffer, WGPUMapMode_Read, true);
            wgpuBufferDestroy(buffer);
            wgpuBufferUnmap(buffer);
        }
        {
            WGPUBuffer buffer = createMappedAtCreationBuffer(t, WGPUBufferUsage_MapRead, 16);
            wgpuBufferDestroy(buffer);
            wgpuBufferUnmap(buffer);
        }
    });

CTS_TEST(g, "unmap,state,mappedAtCreation")
    .desc("Test unmap on mapped-at-creation buffers.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("bufferUsage", bufferUsageValues());
    })
    .fn([](GpuTest& t) {
        const WGPUBufferUsage usage = t.param<WGPUBufferUsage>("bufferUsage");
        WGPUBuffer buffer = createMappedAtCreationBuffer(t, usage, 16);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "unmap,state,mapped")
    .desc("Test unmap on mapped buffers.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("mapMode", mapModeValues());
    })
    .fn([](GpuTest& t) {
        const WGPUMapMode mapMode = t.param<WGPUMapMode>("mapMode");
        WGPUBuffer buffer = createMappableBuffer(t, mapMode, 16);
        t.expectMapAsync(buffer, mapMode, true);
        wgpuBufferUnmap(buffer);
    });

CTS_TEST(g, "unmap,state,mappingPending")
    .desc("Test unmap in the mapping-pending state.")
    .unimplemented("JS promise pending-map state has no synchronous C API analog");

CTS_TEST(g, "gc_behavior,mappedAtCreation")
    .desc("Test JS garbage-collection behavior for mappedAtCreation.")
    .unimplemented("N/A: JS GC and ArrayBuffer detach semantics have no C API analog");

CTS_TEST(g, "gc_behavior,mapAsync")
    .desc("Test JS garbage-collection behavior for mapAsync.")
    .unimplemented("N/A: JS GC and ArrayBuffer detach semantics have no C API analog");

} // namespace
