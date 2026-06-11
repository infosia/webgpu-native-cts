// Ported from gpuweb/cts src/webgpu/api/operation/uncapturederror.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2021 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes (deviations from upstream, documented inline as well):
//
//  1. Fixture: upstream uses ErrorTest (extends Fixture), which builds its own
//     device with max texture-dimension limits so that an out-of-memory error
//     can be reliably generated. We mirror this by creating a PRIVATE
//     WGPUInstance -> adapter -> device (OwnedDeviceContext, RAII cleanup) per
//     test, the same pattern as api/validation/error_scope.spec.cpp and
//     api/operation/buffers/map.spec.cpp. We never use the shared harness
//     device (it already has the harness's own uncaptured-error tracking) and
//     never use harness readback helpers on the private-instance device (they
//     pump cache().instance and would hang). The TestGroup fixture is the base
//     Fixture (NOT GpuTest, whose init() would create the shared device and
//     consume the single-use shared adapter, cascade-failing later
//     AllFeaturesMaxLimitsGpuTest cases), consistent with the sibling lifecycle
//     ports in api/operation/adapter/*.
//
//  2. iff_uncaptured: PORTABLE. Upstream uses expectUncapturedError(), which
//     attaches a listener either via device.onuncapturederror or
//     addEventListener('uncapturederror', ...). In the native C API there is a
//     single WGPUUncapturedErrorCallback set at device-creation time, so both
//     values of the upstream `useOnuncapturederror` param exercise the exact
//     same native mechanism. We keep both param values for query identity and
//     run the same body for each; the callback records whether it fired and the
//     WGPUErrorType, and we assert it fired with the type expected for the
//     filter (isInstanceOfError analog).
//
//  3. only_original_device_is_event_target / uncapturederror_from_non_-
//     originating_thread / onuncapturederror_order_wrt_addEventListener: these
//     are JS/web-platform tests (EventTarget identity across structured-clone
//     deserialization, cross-thread propagation, and addEventListener vs
//     onuncapturederror listener ordering). The native C API exposes neither
//     EventTarget semantics nor multiple ordered listeners (one callback per
//     device), so there is no C analog. They are kept as .unimplemented()
//     stubs with a reason (the first two are already .unimplemented() upstream;
//     the third has an upstream body but is non-portable). File is partial.

#include <cstdlib>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// kGeneratableErrorScopeFilters (capability_info.ts):
//   kErrorScopeFilterInfo = { internal: {generatable:false},
//                             out-of-memory: {generatable:true},
//                             validation: {generatable:true} }
//   => kGeneratableErrorScopeFilters = { "out-of-memory", "validation" }
// ---------------------------------------------------------------------------
const std::vector<std::string>& generatableErrorScopeFilters() {
    static const std::vector<std::string> kFilters = {"out-of-memory", "validation"};
    return kFilters;
}

// Map a filter name to the WGPUErrorType that the generated error carries.
WGPUErrorType errorTypeFromFilter(const std::string& name) {
    if (name == "validation") {
        return WGPUErrorType_Validation;
    }
    if (name == "out-of-memory") {
        return WGPUErrorType_OutOfMemory;
    }
    if (name == "internal") {
        return WGPUErrorType_Internal;
    }
    std::abort();
}

// ---------------------------------------------------------------------------
// Shared state observed by the device's uncaptured-error callback.
// ---------------------------------------------------------------------------
struct UncapturedState {
    bool fired = false;
    WGPUErrorType type = WGPUErrorType_NoError;
};

// ---------------------------------------------------------------------------
// Private device context: fresh instance + adapter + device, with an
// uncaptured-error callback wired to `state`, and the adapter's max texture
// limits requested so that an out-of-memory error can be generated reliably
// (mirrors ErrorTest.init() requesting maxTextureDimension2D /
// maxTextureArrayLayers). RAII cleanup via release(). The shared harness
// device is never used or destroyed.
// ---------------------------------------------------------------------------
struct DeviceContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPULimits limits = WGPU_LIMITS_INIT;

    static void onUncaptured(WGPUDevice const*, WGPUErrorType type, WGPUStringView,
                             void* userdata1, void*) {
        auto* state = static_cast<UncapturedState*>(userdata1);
        state->fired = true;
        state->type = type;
    }

    static DeviceContext create(Fixture& t, UncapturedState* state) {
        DeviceContext ctx;
        ctx.instance = createInstance();
        if (ctx.instance == nullptr) {
            t.fail("uncapturederror: failed to create WGPUInstance");
        }

        AdapterResult ar = requestAdapterSync(ctx.instance, nullptr);
        if (ar.status != WGPURequestAdapterStatus_Success || ar.adapter == nullptr) {
            wgpuInstanceRelease(ctx.instance);
            t.fail("uncapturederror: failed to request adapter: " + ar.message);
        }
        ctx.adapter = ar.adapter;

        // Request the adapter's full limits so createTexture at
        // maxTextureDimension2D^2 x maxTextureArrayLayers is valid and reliably
        // triggers an OOM (upstream ErrorTest does the equivalent).
        WGPULimits adapterLimits = WGPU_LIMITS_INIT;
        bool gotLimits = (wgpuAdapterGetLimits(ctx.adapter, &adapterLimits) == WGPUStatus_Success);

        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.uncapturedErrorCallbackInfo.callback = &DeviceContext::onUncaptured;
        desc.uncapturedErrorCallbackInfo.userdata1 = state;
        if (gotLimits) {
            desc.requiredLimits = &adapterLimits;
        }

        DeviceResult dr = requestDeviceSync(ctx.instance, ctx.adapter, &desc);
        if (dr.status != WGPURequestDeviceStatus_Success || dr.device == nullptr) {
            wgpuAdapterRelease(ctx.adapter);
            wgpuInstanceRelease(ctx.instance);
            t.fail("uncapturederror: failed to request device: " + dr.message);
        }
        ctx.device = dr.device;
        ctx.queue = wgpuDeviceGetQueue(ctx.device);

        ctx.limits = WGPU_LIMITS_INIT;
        if (gotLimits) {
            ctx.limits = adapterLimits;
        } else {
            (void)wgpuDeviceGetLimits(ctx.device, &ctx.limits);
        }
        return ctx;
    }

    void release() {
        if (queue != nullptr) {
            wgpuQueueRelease(queue);
            queue = nullptr;
        }
        if (device != nullptr) {
            wgpuDeviceRelease(device);
            device = nullptr;
        }
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
            adapter = nullptr;
        }
        if (instance != nullptr) {
            wgpuInstanceRelease(instance);
            instance = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// generateError — mirrors ErrorTest.generateError(filter).
//   "validation"    -> createBuffer with invalid usage (0xffff)
//   "out-of-memory" -> createTexture at maximum dimensions (>= 256 GiB)
// then submit([]) (flush workaround).
// ---------------------------------------------------------------------------
void generateError(Fixture& t, const DeviceContext& ctx, const std::string& filter) {
    if (filter == "validation") {
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = 1024;
        bufDesc.usage = static_cast<WGPUBufferUsage>(0xffff);  // invalid GPUBufferUsage
        WGPUBuffer buf = wgpuDeviceCreateBuffer(ctx.device, &bufDesc);
        if (buf != nullptr) {
            wgpuBufferRelease(buf);
        }
    } else if (filter == "out-of-memory") {
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.format = WGPUTextureFormat_RGBA32Float;
        texDesc.usage = WGPUTextureUsage_CopyDst;
        texDesc.size.width = ctx.limits.maxTextureDimension2D;
        texDesc.size.height = ctx.limits.maxTextureDimension2D;
        texDesc.size.depthOrArrayLayers = ctx.limits.maxTextureArrayLayers;
        WGPUTexture tex = wgpuDeviceCreateTexture(ctx.device, &texDesc);
        if (tex != nullptr) {
            wgpuTextureRelease(tex);
        }
    } else {
        t.fail("generateError: unsupported filter '" + filter + "'");
    }

    // Flush (mirror upstream device.queue.submit([])).
    wgpuQueueSubmit(ctx.queue, 0, nullptr);
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------
// Base Fixture (NOT GpuTest): GpuTest::init() eagerly creates the shared cached
// device on the shared harness adapter, which the C API treats as single-use
// (one device per adapter). That would consume the shared adapter and
// cascade-fail later AllFeaturesMaxLimitsGpuTest cases ("adapter is consumed").
// These tests only ever use their own private instance/adapter/device.
TestGroup<Fixture> g = MakeTestGroup<Fixture>(
    "api,operation,uncapturederror",
    "Tests for GPUDevice.onuncapturederror / addEventListener('uncapturederror')");

// ---------------------------------------------------------------------------
// g.test('iff_uncaptured')
// ---------------------------------------------------------------------------
CTS_TEST(g, "iff_uncaptured")
    .desc(
        "{validation, out-of-memory} error should fire uncapturederror iff not captured by a "
        "scope.")
    .params([](ParamsBuilder u) {
        std::vector<Value> filters;
        for (const auto& s : generatableErrorScopeFilters()) {
            filters.emplace_back(s);
        }
        return u.combine("useOnuncapturederror", {Value(false), Value(true)})
            .combine("errorType", filters);
    })
    .fn([](Fixture& t) {
        // useOnuncapturederror selects, in JS, between device.onuncapturederror
        // and addEventListener('uncapturederror'). Both map to the single native
        // WGPUUncapturedErrorCallback, so both param values exercise the same
        // mechanism; the param is preserved for query identity only.
        const std::string errorType = t.param<std::string>("errorType");

        UncapturedState state;
        DeviceContext ctx = DeviceContext::create(t, &state);

        // No error scope is pushed, so the generated error must propagate to the
        // uncaptured-error callback (the "iff not captured by a scope" path).
        generateError(t, ctx, errorType);

        // Drain events so the uncaptured-error callback has a chance to fire.
        processEventsUntil(ctx.instance, [&state] { return state.fired; });

        t.expect(state.fired,
                 std::string("iff_uncaptured: expected an uncaptured error for '") + errorType +
                     "', got none");
        // isInstanceOfError(errorType, event.error): the uncaptured error must be
        // of the type expected for the filter.
        t.expect(state.type == errorTypeFromFilter(errorType),
                 "iff_uncaptured: uncaptured error type mismatch");

        ctx.release();
    });

// ---------------------------------------------------------------------------
// g.test('only_original_device_is_event_target')
// Upstream is .unimplemented(). Additionally non-portable: the C API has no
// EventTarget / structured-clone-deserialized GPUDevice concept.
// ---------------------------------------------------------------------------
CTS_TEST(g, "only_original_device_is_event_target")
    .desc(
        "Original GPUDevice objects are EventTargets and have onuncapturederror, but\n"
        "deserialized GPUDevices do not.")
    .unimplemented();

// ---------------------------------------------------------------------------
// g.test('uncapturederror_from_non_originating_thread')
// Upstream is .unimplemented(). Additionally non-portable: cross-thread /
// structured-clone deserialization is a JS/web-platform concept with no C
// analog.
// ---------------------------------------------------------------------------
CTS_TEST(g, "uncapturederror_from_non_originating_thread")
    .desc(
        "Uncaptured errors on any thread should always propagate to the original GPUDevice object\n"
        "(since deserialized ones don't have EventTarget/onuncapturederror).")
    .unimplemented();

// ---------------------------------------------------------------------------
// g.test('onuncapturederror_order_wrt_addEventListener')
// Non-portable: tests the listener ordering between onuncapturederror and
// multiple addEventListener('uncapturederror') registrations, plus
// preventDefault(). The native C API exposes a single uncaptured-error callback
// per device, with no EventTarget, no multiple listeners, and no ordering
// semantics to assert. No C analog -> unimplemented with reason; the test name
// is kept for query identity.
// ---------------------------------------------------------------------------
CTS_TEST(g, "onuncapturederror_order_wrt_addEventListener")
    .desc(
        "Test that onuncapturederror and addEventListener work in the correct order.\n\n"
        "The spec says setting onuncapturederror adds a listener via addEventListener that\n"
        "calls the callback. Changing onuncapturederror changes the callback to the existing\n"
        "listener. Setting onuncapturederror to null removes the listener.")
    .unimplemented();

}  // namespace
