// Ported from gpuweb/cts src/webgpu/api/operation/buffers/map_oom.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2024 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//
// "shouldThrow('RangeError', ...)" — in JS, the RangeError is thrown synchronously by trying to
// allocate a ~9 PB ArrayBuffer for the initial mapping, before any WebGPU validation runs. This
// is pure JS semantics with no C analog: native implementations do not necessarily surface any
// error for this createBuffer at all (Dawn raises none; Dawn and yawgpu agree there is no
// guaranteed validation error). The port therefore creates the buffer WITHOUT expecting a
// specific error type — any error of any filter is tolerated and drained via error scopes so the
// harness's uncaptured-error hook never flags it — and then asserts the observable consequence of
// the JS RangeError: the mapping cannot be materialized, i.e. wgpuBufferGetMappedRange returns
// nullptr. The creation runs on a privately owned instance/device (same pattern as
// api/validation/error_scope.spec.cpp) so the scopes can be popped synchronously without touching
// the shared harness device's error-scope stack.
//
// The post-unmap ArrayBuffer.byteLength===0 assertion has no C analog (no ArrayBuffer handle to
// observe); this sub-assertion is omitted and documented here.

#include <array>
#include <cstdint>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

// Privately owned instance/adapter/device (+ one buffer) for the oom=true
// path, so any error raised by the huge createBuffer can be drained through
// error scopes popped on our own instance (the shared harness device's
// instance is not accessible to spec files). Released in reverse order on
// scope exit (expect()/fail() throw, so RAII is required).
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

void createOwnedDevice(GpuTest& t, OwnedDeviceContext& ctx) {
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

// kMaxSafeMultipleOf8: the upstream constant from webgpu/util/math.ts.
// Number.MAX_SAFE_INTEGER - 7 == 9007199254740984, which is a multiple of 8.
// Any real device's maxBufferSize is far below this value, so a mapping of
// this size can never be materialized (see the RangeError porting note above).
// Defined in capability_info.h as kMaxSafeMultipleOf8 = 9007199254740984ULL.

std::vector<Value> bufferUsageValues() {
    std::vector<Value> values;
    values.reserve(kBufferUsages.size());
    for (WGPUBufferUsage usage : kBufferUsages) {
        values.emplace_back(static_cast<int64_t>(usage));
    }
    return values;
}

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,buffers,map_oom",
    "Test out-of-memory conditions creating large mappable/mappedAtCreation buffers.");

// Upstream param builder (kUnitCaseParamsBuilder):
//   .combine('oom', [false, true])
//   .expand('size', ({ oom }) => oom ? [kMaxSafeMultipleOf8] : [16])
//   .beginSubcases()
//   .combine('usage', kBufferUsages)
//
// Query identity: webgpu:api,operation,buffers,map_oom:mappedAtCreation:oom=<bool>;size=<n>;usage=<n>
CTS_TEST(g, "mappedAtCreation")
    .desc(
        "Test creating a very large buffer mappedAtCreation buffer should throw a RangeError only\n"
        "because such a large allocation cannot be created when we initialize an active buffer mapping.\n")
    .params([](ParamsBuilder u) {
        return u
            .combine("oom", {false, true})
            .expand("size", [](const ParamRecord& p) -> std::vector<Value> {
                const bool oom = valueAs<bool>(*findParam(p, "oom"));
                if (oom) {
                    // kMaxSafeMultipleOf8: 9007199254740984 bytes (~9 PB) — always exceeds
                    // maxBufferSize so creation with mappedAtCreation:true fails.
                    return {Value(static_cast<int64_t>(kMaxSafeMultipleOf8))};
                } else {
                    return {Value(int64_t{16})};
                }
            })
            .beginSubcases()
            .combine("usage", bufferUsageValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool oom = t.param<bool>("oom");
        // size fits in uint64_t; kMaxSafeMultipleOf8 is 9007199254740984 < UINT64_MAX.
        const uint64_t size = static_cast<uint64_t>(t.param<int64_t>("size"));
        const WGPUBufferUsage usage = t.param<WGPUBufferUsage>("usage");

        WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
        desc.size = size;
        desc.usage = usage;
        desc.mappedAtCreation = WGPU_TRUE;

        if (oom) {
            // JS: t.shouldThrow('RangeError', f)
            //
            // RangeError adaptation: in JS the RangeError comes from failing to allocate the
            // ~9 PB mapping ArrayBuffer — pure JS semantics that precede WebGPU validation.
            // The C API has no analogous synchronous throw, and native implementations raise
            // no guaranteed error here (Dawn raises none). So: create the buffer WITHOUT
            // expecting any specific error, tolerate-and-drain any error of any type through
            // error scopes (one per filter, popped without inspecting the result) so the
            // harness never sees an unexpected uncaptured/scoped error, then assert the
            // observable equivalent of the RangeError: the mapping cannot be materialized,
            // i.e. getMappedRange returns nullptr.
            OwnedDeviceContext ctx;
            createOwnedDevice(t, ctx);

            const std::array<WGPUErrorFilter, 3> kAllFilters = {
                WGPUErrorFilter_Validation,
                WGPUErrorFilter_OutOfMemory,
                WGPUErrorFilter_Internal,
            };
            for (WGPUErrorFilter filter : kAllFilters) {
                wgpuDevicePushErrorScope(ctx.device, filter);
            }

            // Note: yawgpu currently process-aborts inside this createBuffer (a real yawgpu
            // finding); the call must stay reachable so the crash remains observable.
            ctx.buffer = wgpuDeviceCreateBuffer(ctx.device, &desc);

            // Drain the scopes; whether each carries an error (and of which type) is
            // intentionally not asserted.
            for (size_t i = 0; i < kAllFilters.size(); ++i) {
                (void)popErrorScopeSync(ctx.instance, ctx.device);
            }

            // Even if a (possibly error) buffer handle is returned with
            // mappedAtCreation=true, getMappedRange must return nullptr because the backing
            // allocation cannot satisfy this size.
            if (ctx.buffer != nullptr) {
                void* mapped = wgpuBufferGetMappedRange(ctx.buffer, 0, WGPU_WHOLE_MAP_SIZE);
                t.expect(mapped == nullptr,
                    "getMappedRange must return nullptr for an OOM mappedAtCreation buffer");
            }
        } else {
            // JS: buffer = f(); mapping = buffer.getMappedRange();
            //     t.expect(mapping.byteLength === size, 'Mapping should be successful');
            //     buffer.unmap();
            //     t.expect(mapping.byteLength === 0, 'Mapping should be detached');
            //
            // C port: verify getMappedRange returns non-null; verify unmap does not crash.
            // The post-unmap byteLength===0 assertion has no C analog (no ArrayBuffer handle)
            // and is omitted; documented in the file header.
            WGPUBuffer buffer = t.createBufferTracked(desc);
            void* mapped = wgpuBufferGetMappedRange(buffer, 0, WGPU_WHOLE_MAP_SIZE);
            t.expect(mapped != nullptr, "getMappedRange must return non-null for a successfully created mappedAtCreation buffer");
            wgpuBufferUnmap(buffer);
        }
    });

} // namespace
