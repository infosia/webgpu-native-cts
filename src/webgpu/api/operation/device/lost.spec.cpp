// Ported from gpuweb/cts src/webgpu/api/operation/device/lost.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes (deviations from upstream):
//
// 1. Fixture / device ownership. Upstream DeviceLostTests extends Fixture and
//    requests its own adapter/device per test (never the shared GPUTest
//    device), because these tests destroy the device. We mirror that with a
//    private WGPUInstance -> adapter -> device (OwnedDeviceContext, the same
//    pattern as api/operation/buffers/map.spec.cpp and
//    api/validation/error_scope.spec.cpp), with RAII cleanup, and we NEVER
//    touch the shared harness device. The base fixture is the plain Fixture (not
//    GpuTest, whose init() would consume the shared adapter); we do not call any
//    harness readback helper on the private device.
//
// 2. device.lost model. The JS `device.lost` promise has no direct C analog.
//    In webgpu.h the device-lost notification is delivered through the
//    WGPUDeviceLostCallbackInfo set on the WGPUDeviceDescriptor at device
//    creation. We register that callback (AllowProcessEvents) and drive it with
//    processEventsUntil on our private instance. The lost "reason" maps to
//    WGPUDeviceLostReason (Destroyed for an explicit destroy()).
//
// 3. not_lost_on_gc is a JS garbage-collection test (attemptGarbageCollection,
//    asserting the lost promise stays unsettled). GC behaviour is a
//    web-platform concern with no native C analog, so it is an unimplemented
//    stub with a reason (test name kept for query identity).
//
// 4. same_object verifies JS object identity (the `device.lost` property
//    returns the SAME Promise object and the SAME GPUDeviceLostInfo result
//    object on each access, including after destroy). The C API exposes no
//    promise/result object to compare for identity (the callback is a one-shot
//    notification), so this is JS/web-platform-specific with no C analog: an
//    unimplemented stub with a reason (test name kept for query identity).

#include <string>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Private instance/adapter/device, released in reverse order on scope exit.
// fail()/skip() throw, so RAII is required.
// ---------------------------------------------------------------------------
struct OwnedDeviceContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;

    OwnedDeviceContext() = default;
    OwnedDeviceContext(const OwnedDeviceContext&) = delete;
    OwnedDeviceContext& operator=(const OwnedDeviceContext&) = delete;

    ~OwnedDeviceContext() {
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

// State shared between the device-lost callback and the test body.
struct LostState {
    bool fired = false;
    WGPUDeviceLostReason reason = WGPUDeviceLostReason_Unknown;
};

void onDeviceLost(
    WGPUDevice const*,
    WGPUDeviceLostReason reason,
    WGPUStringView,
    void* userdata1,
    void*) {
    auto* state = static_cast<LostState*>(userdata1);
    state->fired = true;
    state->reason = reason;
}

// Create a private instance/adapter and a device whose device-lost callback
// writes into `state`. Fails the test via Fixture::fail() on any setup error.
void createOwnedDevice(Fixture& t, OwnedDeviceContext& ctx, LostState& state) {
    ctx.instance = createInstance();
    if (ctx.instance == nullptr) {
        t.fail("device/lost: failed to create WGPUInstance");
    }

    AdapterResult adapter = requestAdapterSync(ctx.instance, adapterOptions());
    if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
        t.fail("device/lost: failed to request adapter: " + adapter.message);
    }
    ctx.adapter = adapter.adapter;

    WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
    desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    desc.deviceLostCallbackInfo.callback = onDeviceLost;
    desc.deviceLostCallbackInfo.userdata1 = &state;

    DeviceResult device = requestDeviceSync(ctx.instance, ctx.adapter, &desc);
    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        t.fail("device/lost: failed to request device: " + device.message);
    }
    ctx.device = device.device;
}

// Base Fixture (NOT GpuTest): GpuTest::init() eagerly creates the shared cached
// device on the shared harness adapter, which the C API treats as single-use
// (one device per adapter). That would consume the shared adapter and
// cascade-fail later AllFeaturesMaxLimitsGpuTest cases ("adapter is consumed").
// These tests only ever use their own private instance/adapter/device.
TestGroup<Fixture> g = MakeTestGroup<Fixture>(
    "api,operation,device,lost",
    "Tests for GPUDevice.lost.");

// ---------------------------------------------------------------------------
// g.test('not_lost_on_gc')
// JS-only: relies on attemptGarbageCollection and asserting the `device.lost`
// promise stays unsettled when the device is garbage collected. Garbage
// collection is a web-platform concept with no native C analog.
// ---------------------------------------------------------------------------
CTS_TEST(g, "not_lost_on_gc")
    .desc(
        "'lost' is never resolved by GPUDevice being garbage collected (with "
        "attemptGarbageCollection).")
    .unimplemented();

// ---------------------------------------------------------------------------
// g.test('lost_on_destroy')
// Portable: destroy a private device and expect the device-lost notification
// to fire with reason 'destroyed'.
// ---------------------------------------------------------------------------
CTS_TEST(g, "lost_on_destroy")
    .desc("'lost' is resolved, with reason='destroyed', on GPUDevice.destroy().")
    .fn([](Fixture& t) {
        LostState state;
        OwnedDeviceContext ctx;
        createOwnedDevice(t, ctx, state);

        // Upstream t.expectDeviceDestroyed(device); device.destroy();
        wgpuDeviceDestroy(ctx.device);

        // Drive the private instance until the device-lost callback fires.
        if (!processEventsUntil(ctx.instance, [&state] { return state.fired; })) {
            t.fail("device was not lost");
        }
        t.expect(
            state.reason == WGPUDeviceLostReason_Destroyed,
            "device was lost from destroy");
    });

// ---------------------------------------------------------------------------
// g.test('same_object')
// JS-only: verifies object identity of the `device.lost` Promise and the
// GPUDeviceLostInfo result object across repeated accesses (before and after
// destroy and resolution). The C API delivers device loss as a one-shot
// callback with no promise/result object to compare for identity, so there is
// no C analog.
// ---------------------------------------------------------------------------
CTS_TEST(g, "same_object")
    .desc(
        "'lost' provides the same Promise and GPUDeviceLostInfo objects each time it's "
        "accessed.")
    .unimplemented();

} // namespace
