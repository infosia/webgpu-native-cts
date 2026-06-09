// Ported from gpuweb/cts src/webgpu/api/validation/error_scope.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

// ---------------------------------------------------------------------------
// Upstream note: "these must create their own device, not use GPUTest (that
// one already has error scopes on it)."
//
// We replicate this by creating a fresh WGPUInstance / WGPUAdapter /
// WGPUDevice inside each test body via helpers below.  That also gives us a
// local WGPUInstance we can pass to popErrorScopeSync without needing to
// access the harness-internal cache().instance singleton.
// ---------------------------------------------------------------------------

namespace {

// ---------------------------------------------------------------------------
// Filter / type constants mirroring the upstream TypeScript arrays
//
// kGeneratableErrorScopeFilters = { "out-of-memory", "validation" }
// kErrorScopeFilters            = { "internal", "out-of-memory", "validation" }
// ---------------------------------------------------------------------------

static const std::array<std::string, 2> kGeneratableErrorScopeFilters = {
    "out-of-memory",
    "validation",
};

static const std::array<std::string, 3> kErrorScopeFilters = {
    "internal",
    "out-of-memory",
    "validation",
};

// Map a filter name to the WGPUErrorFilter enum value.
static WGPUErrorFilter filterFromString(const std::string& name) {
    if (name == "validation")    return WGPUErrorFilter_Validation;
    if (name == "out-of-memory") return WGPUErrorFilter_OutOfMemory;
    if (name == "internal")      return WGPUErrorFilter_Internal;
    std::abort();
}

// Map a filter name to the WGPUErrorType that a captured error will carry.
static WGPUErrorType errorTypeFromFilter(const std::string& name) {
    if (name == "validation")    return WGPUErrorType_Validation;
    if (name == "out-of-memory") return WGPUErrorType_OutOfMemory;
    if (name == "internal")      return WGPUErrorType_Internal;
    std::abort();
}

// ---------------------------------------------------------------------------
// Lightweight device context: fresh instance + adapter + device, with the
// adapter's max texture limits requested (needed to reliably produce OOM).
// Caller is responsible for releasing all three after use.
// ---------------------------------------------------------------------------

struct DeviceContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter  adapter  = nullptr;
    WGPUDevice   device   = nullptr;

    // Factory: fails via GpuTest::fail() on any setup error.
    static DeviceContext create(GpuTest& t) {
        DeviceContext ctx;
        ctx.instance = createInstance();
        if (ctx.instance == nullptr) {
            t.fail("error_scope: failed to create WGPUInstance");
        }

        AdapterResult ar = requestAdapterSync(ctx.instance, nullptr);
        if (ar.status != WGPURequestAdapterStatus_Success || ar.adapter == nullptr) {
            wgpuInstanceRelease(ctx.instance);
            t.fail("error_scope: failed to request adapter: " + ar.message);
        }
        ctx.adapter = ar.adapter;

        // Request the adapter's max texture-dimension limits so that
        // createTexture with maxTextureDimension2D squared × maxTextureArrayLayers
        // is valid and reliably triggers an OOM (upstream ErrorTest does the same).
        // We request all adapter limits (not just the two texture ones) to avoid
        // creating a device with invalid 0-valued fields in the requiredLimits struct.
        WGPULimits adapterLimits = WGPU_LIMITS_INIT;
        bool gotLimits = (wgpuAdapterGetLimits(ctx.adapter, &adapterLimits) == WGPUStatus_Success);

        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        if (gotLimits) {
            desc.requiredLimits = &adapterLimits;
        }

        DeviceResult dr = requestDeviceSync(ctx.instance, ctx.adapter, &desc);
        if (dr.status != WGPURequestDeviceStatus_Success || dr.device == nullptr) {
            wgpuAdapterRelease(ctx.adapter);
            wgpuInstanceRelease(ctx.instance);
            t.fail("error_scope: failed to request device: " + dr.message);
        }
        ctx.device = dr.device;
        return ctx;
    }

    void release() {
        if (device   != nullptr) { wgpuDeviceRelease(device);   device   = nullptr; }
        if (adapter  != nullptr) { wgpuAdapterRelease(adapter);  adapter  = nullptr; }
        if (instance != nullptr) { wgpuInstanceRelease(instance); instance = nullptr; }
    }
};

// ---------------------------------------------------------------------------
// generateError — mirrors ErrorTest.generateError() in the upstream.
//
//  "validation"    → createBuffer with invalid usage (0xffff)
//  "out-of-memory" → createTexture at maximum possible dimensions
//
// The error is produced eagerly (object-creation error) so it lands in
// whatever error scope is currently on top of the stack.
// ---------------------------------------------------------------------------

static void generateError(
    GpuTest& t,
    WGPUDevice device,
    WGPUQueue queue,
    const std::string& filterName,
    const WGPULimits& limits)
{
    if (filterName == "validation") {
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 1024;
        bufDesc.usage = static_cast<WGPUBufferUsage>(0xffff); // invalid usage
        WGPUBuffer buf = wgpuDeviceCreateBuffer(device, &bufDesc);
        if (buf != nullptr) { wgpuBufferRelease(buf); }
    } else if (filterName == "out-of-memory") {
        // One of the largest formats (rgba32float).  With maxTextureDimension2D
        // and maxTextureArrayLayers, this texture would be enormous (≥ 256 GiB).
        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.format = WGPUTextureFormat_RGBA32Float;
        texDesc.usage  = WGPUTextureUsage_CopyDst;
        texDesc.size.width             = limits.maxTextureDimension2D;
        texDesc.size.height            = limits.maxTextureDimension2D;
        texDesc.size.depthOrArrayLayers = limits.maxTextureArrayLayers;
        WGPUTexture tex = wgpuDeviceCreateTexture(device, &texDesc);
        if (tex != nullptr) { wgpuTextureRelease(tex); }
    } else {
        t.fail("generateError: unsupported filter '" + filterName + "'");
    }

    // Flush (workaround for implementations that need explicit submission to
    // propagate errors, mirroring the upstream's device.queue.submit([])).
    wgpuQueueSubmit(queue, 0, nullptr);
}

// ---------------------------------------------------------------------------
// Synchronously pop one error scope and return the ScopeResult.
// ---------------------------------------------------------------------------

static ScopeResult popSync(WGPUInstance instance, WGPUDevice device) {
    return popErrorScopeSync(instance, device);
}

// ---------------------------------------------------------------------------
// Pop `count` error scopes and expect all of them to have no error captured
// (WGPUErrorType_NoError, status Success).  Mirrors chunkedPopManyErrorScopes.
// ---------------------------------------------------------------------------

static void popManyExpectNull(
    GpuTest& t,
    WGPUInstance instance,
    WGPUDevice device,
    int count)
{
    for (int i = 0; i < count; ++i) {
        ScopeResult result = popSync(instance, device);
        if (result.status != WGPUPopErrorScopeStatus_Success) {
            t.fail("popManyExpectNull: unexpected status on pop " + std::to_string(i));
        }
        if (result.type != WGPUErrorType_NoError) {
            t.fail("popManyExpectNull: unexpected error on pop " + std::to_string(i)
                   + ": " + result.message);
        }
    }
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<GpuTest> g = MakeTestGroup<GpuTest>(
    "api,validation,error_scope",
    "Error scope validation tests.\n\n"
    "Note these must create their own device, not use GPUTest "
    "(that one already has error scopes on it).\n\n"
    "TODO: (POSTV1) Test error scopes of different threads and make sure "
    "they go to the right place.\n"
    "TODO: (POSTV1) Test that unhandled errors go the right device, and "
    "nowhere if the device was dropped.");

// ---------------------------------------------------------------------------
// g.test('simple')
// ---------------------------------------------------------------------------
CTS_TEST(g, "simple")
    .desc(
        "Tests that error scopes catches their expected errors, firing an uncaptured error event "
        "otherwise.\n\n"
        "- Same error and error filter (popErrorScope should return the error)\n"
        "- Different error from filter (uncaptured error should result)")
    .params([](ParamsBuilder u) {
        std::vector<Value> generatable;
        for (const auto& s : kGeneratableErrorScopeFilters) { generatable.emplace_back(s); }
        std::vector<Value> all;
        for (const auto& s : kErrorScopeFilters) { all.emplace_back(s); }
        return u.combine("errorType", generatable).combine("errorFilter", all);
    })
    .fn([](GpuTest& t) {
        const std::string errorType   = t.param<std::string>("errorType");
        const std::string errorFilter = t.param<std::string>("errorFilter");

        if (errorType != errorFilter) {
            // -------------------------------------------------------------------
            // Different error case: the error bubbles past the scope as an
            // uncaptured error.
            //
            // In JS this is verified with expectUncapturedError() which listens
            // for the 'uncapturederror' DOM event.  In native C, the uncaptured
            // error fires the WGPUUncapturedErrorCallback set on the device.
            //
            // We create a separate device with a custom callback so we can
            // capture and inspect the uncaptured error without interfering with
            // the harness's own error-tracking mechanism.
            // -------------------------------------------------------------------

            // State shared between the callback and the test body.
            struct UncapturedState {
                bool        fired     = false;
                WGPUErrorType type    = WGPUErrorType_NoError;
            } uncapturedState;

            auto onUncaptured = [](
                WGPUDevice const*, WGPUErrorType type, WGPUStringView,
                void* userdata1, void*) {
                auto* state = static_cast<UncapturedState*>(userdata1);
                state->fired = true;
                state->type  = type;
            };

            // Create a fresh instance + adapter so we own the instance and can
            // call processEventsUntil / popErrorScopeSync with it.
            WGPUInstance inst2 = createInstance();
            if (inst2 == nullptr) {
                t.fail("simple/different: failed to create WGPUInstance");
            }
            AdapterResult ar2 = requestAdapterSync(inst2, nullptr);
            if (ar2.status != WGPURequestAdapterStatus_Success || ar2.adapter == nullptr) {
                wgpuInstanceRelease(inst2);
                t.fail("simple/different: failed to request adapter: " + ar2.message);
            }

            WGPULimits limitsForOom = WGPU_LIMITS_INIT;
            bool gotLimits2 = (wgpuAdapterGetLimits(ar2.adapter, &limitsForOom) == WGPUStatus_Success);

            WGPUDeviceDescriptor desc2 = WGPU_DEVICE_DESCRIPTOR_INIT;
            desc2.uncapturedErrorCallbackInfo.callback  = onUncaptured;
            desc2.uncapturedErrorCallbackInfo.userdata1 = &uncapturedState;
            if (gotLimits2) {
                desc2.requiredLimits = &limitsForOom;
            }

            DeviceResult dr2 = requestDeviceSync(inst2, ar2.adapter, &desc2);
            if (dr2.status != WGPURequestDeviceStatus_Success || dr2.device == nullptr) {
                wgpuAdapterRelease(ar2.adapter);
                wgpuInstanceRelease(inst2);
                t.fail("simple/different: failed to create device: " + dr2.message);
            }
            WGPUDevice   dev2   = dr2.device;
            WGPUQueue    queue2 = wgpuDeviceGetQueue(dev2);
            WGPULimits   limits = gotLimits2 ? limitsForOom : WGPU_LIMITS_INIT;

            // Push scope with 'errorFilter' — won't match 'errorType'.
            wgpuDevicePushErrorScope(dev2, filterFromString(errorFilter));

            // Generate the error; since the filter doesn't match it will escape
            // the scope and fire the uncaptured-error callback.
            generateError(t, dev2, queue2, errorType, limits);

            // Drain events so the uncaptured callback (and the pop callback)
            // have a chance to fire.
            processEventsUntil(inst2,
                [&uncapturedState]{ return uncapturedState.fired; });

            // Pop the scope — must return null (no error captured).
            ScopeResult popResult = popSync(inst2, dev2);
            t.expect(popResult.status == WGPUPopErrorScopeStatus_Success,
                     "simple/different: popErrorScope status should be Success");
            t.expect(popResult.type == WGPUErrorType_NoError,
                     "simple/different: scope should have captured no error");

            // Verify that the uncaptured error did fire with the right type.
            t.expect(uncapturedState.fired,
                     "simple/different: expected an uncaptured error, got none");
            t.expect(uncapturedState.type == errorTypeFromFilter(errorType),
                     "simple/different: uncaptured error type mismatch");

            wgpuQueueRelease(queue2);
            wgpuDeviceRelease(dev2);
            wgpuAdapterRelease(ar2.adapter);
            wgpuInstanceRelease(inst2);

        } else {
            // -------------------------------------------------------------------
            // Same-filter case: the scope captures the error.
            // -------------------------------------------------------------------
            DeviceContext ctx = DeviceContext::create(t);
            WGPULimits limits = WGPU_LIMITS_INIT;
            (void)wgpuDeviceGetLimits(ctx.device, &limits);
            WGPUQueue queue = wgpuDeviceGetQueue(ctx.device);

            wgpuDevicePushErrorScope(ctx.device, filterFromString(errorFilter));
            generateError(t, ctx.device, queue, errorType, limits);
            ScopeResult result = popSync(ctx.instance, ctx.device);

            t.expect(result.status == WGPUPopErrorScopeStatus_Success,
                     "simple/same: popErrorScope status should be Success");
            t.expect(result.type == errorTypeFromFilter(errorFilter),
                     "simple/same: captured error type mismatch — expected "
                     + errorFilter + ", got type=" + std::to_string(static_cast<int>(result.type)));

            wgpuQueueRelease(queue);
            ctx.release();
        }
    });

// ---------------------------------------------------------------------------
// g.test('empty')
// ---------------------------------------------------------------------------
CTS_TEST(g, "empty")
    .desc("Tests that popping an empty error scope stack should reject.")
    .fn([](GpuTest& t) {
        DeviceContext ctx = DeviceContext::create(t);

        // Pop with no prior push — stack is empty.
        ScopeResult result = popSync(ctx.instance, ctx.device);

        // In JS this is a Promise that rejects with OperationError.
        // In the C API the callback receives a non-Success status.
        t.expect(result.status != WGPUPopErrorScopeStatus_Success,
                 "empty: expected non-Success status when popping empty stack");

        ctx.release();
    });

// ---------------------------------------------------------------------------
// g.test('parent_scope')
// ---------------------------------------------------------------------------
CTS_TEST(g, "parent_scope")
    .desc(
        "Tests that an error bubbles to the correct parent scope.\n\n"
        "- Different error types as the parent scope\n"
        "- Different depths of non-capturing filters for the generated error")
    .params([](ParamsBuilder u) {
        std::vector<Value> generatable;
        for (const auto& s : kGeneratableErrorScopeFilters) { generatable.emplace_back(s); }
        return u.combine("errorFilter", generatable)
                .beginSubcases()
                .combine("stackDepth", {Value(1), Value(10), Value(100), Value(1000)});
    })
    .fn([](GpuTest& t) {
        const std::string errorFilter = t.param<std::string>("errorFilter");
        const int         stackDepth  = t.param<int>("stackDepth");

        DeviceContext ctx = DeviceContext::create(t);
        WGPULimits limits = WGPU_LIMITS_INIT;
        (void)wgpuDeviceGetLimits(ctx.device, &limits);
        WGPUQueue queue = wgpuDeviceGetQueue(ctx.device);

        // Push the parent scope that will catch the error.
        wgpuDevicePushErrorScope(ctx.device, filterFromString(errorFilter));

        // Build the list of filters that do NOT match 'errorFilter'.
        std::vector<std::string> unmatchedFilters;
        for (const auto& f : kErrorScopeFilters) {
            if (f != errorFilter) { unmatchedFilters.push_back(f); }
        }

        // Push 'stackDepth' non-matching scopes on top.
        for (int i = 0; i < stackDepth; ++i) {
            wgpuDevicePushErrorScope(
                ctx.device,
                filterFromString(unmatchedFilters[static_cast<size_t>(i) % unmatchedFilters.size()]));
        }

        // Generate the error.
        generateError(t, ctx.device, queue, errorFilter, limits);

        // Pop all non-matching scopes — none should have caught the error.
        popManyExpectNull(t, ctx.instance, ctx.device, stackDepth);

        // Finally pop the parent scope — it should have caught the error.
        ScopeResult result = popSync(ctx.instance, ctx.device);
        t.expect(result.status == WGPUPopErrorScopeStatus_Success,
                 "parent_scope: parent pop status should be Success");
        t.expect(result.type == errorTypeFromFilter(errorFilter),
                 "parent_scope: parent scope did not catch the expected error type");

        wgpuQueueRelease(queue);
        ctx.release();
    });

// ---------------------------------------------------------------------------
// g.test('current_scope')
// ---------------------------------------------------------------------------
CTS_TEST(g, "current_scope")
    .desc(
        "Tests that an error does not bubble to parent scopes when local scope matches.\n\n"
        "- Different error types as the current scope\n"
        "- Different depths of non-capturing filters for the generated error")
    .params([](ParamsBuilder u) {
        std::vector<Value> generatable;
        for (const auto& s : kGeneratableErrorScopeFilters) { generatable.emplace_back(s); }
        return u.combine("errorFilter", generatable)
                .beginSubcases()
                // stackDepth=100000 is preserved from upstream; each synchronous pop
                // completes a CPU-side round-trip so it may run slowly on some backends.
                .combine("stackDepth", {Value(1), Value(10), Value(100), Value(1000), Value(100000)});
    })
    .fn([](GpuTest& t) {
        const std::string errorFilter = t.param<std::string>("errorFilter");
        const int         stackDepth  = t.param<int>("stackDepth");

        DeviceContext ctx = DeviceContext::create(t);
        WGPULimits limits = WGPU_LIMITS_INIT;
        (void)wgpuDeviceGetLimits(ctx.device, &limits);
        WGPUQueue queue = wgpuDeviceGetQueue(ctx.device);

        // Push 'stackDepth' arbitrary scopes (cycling through all filter types).
        for (int i = 0; i < stackDepth; ++i) {
            wgpuDevicePushErrorScope(
                ctx.device,
                filterFromString(kErrorScopeFilters[static_cast<size_t>(i) % kErrorScopeFilters.size()]));
        }

        // Push the matching scope — it should catch the error immediately.
        wgpuDevicePushErrorScope(ctx.device, filterFromString(errorFilter));
        generateError(t, ctx.device, queue, errorFilter, limits);
        ScopeResult result = popSync(ctx.instance, ctx.device);
        t.expect(result.status == WGPUPopErrorScopeStatus_Success,
                 "current_scope: pop status should be Success");
        t.expect(result.type == errorTypeFromFilter(errorFilter),
                 "current_scope: scope did not catch the expected error type");

        // Remaining parent scopes should not have caught anything.
        popManyExpectNull(t, ctx.instance, ctx.device, stackDepth);

        wgpuQueueRelease(queue);
        ctx.release();
    });

// ---------------------------------------------------------------------------
// g.test('balanced_siblings')
// ---------------------------------------------------------------------------
CTS_TEST(g, "balanced_siblings")
    .desc(
        "Tests that sibling error scopes need to be balanced.\n\n"
        "- Different error types as the current scope\n"
        "- Different number of sibling errors")
    .params([](ParamsBuilder u) {
        std::vector<Value> all;
        for (const auto& s : kErrorScopeFilters) { all.emplace_back(s); }
        return u.combine("errorFilter", all)
                .beginSubcases()
                .combine("numErrors", {Value(1), Value(10), Value(100), Value(1000)});
    })
    .fn([](GpuTest& t) {
        const std::string errorFilter = t.param<std::string>("errorFilter");
        const int         numErrors   = t.param<int>("numErrors");

        DeviceContext ctx = DeviceContext::create(t);

        // Push 'numErrors' scopes then immediately pop each one.
        // No errors are generated, so all pops should return null.
        for (int i = 0; i < numErrors; ++i) {
            wgpuDevicePushErrorScope(ctx.device, filterFromString(errorFilter));
        }

        // Pop all numErrors scopes — each should return no error.
        popManyExpectNull(t, ctx.instance, ctx.device, numErrors);

        // Trying to pop an additional non-existing scope should not succeed.
        // (Mirrors: t.shouldReject('OperationError', promise, ...))
        ScopeResult extraPop = popSync(ctx.instance, ctx.device);
        t.expect(extraPop.status != WGPUPopErrorScopeStatus_Success,
                 "balanced_siblings: extra pop on empty stack should fail");

        ctx.release();
    });

// ---------------------------------------------------------------------------
// g.test('balanced_nesting')
// ---------------------------------------------------------------------------
CTS_TEST(g, "balanced_nesting")
    .desc(
        "Tests that nested error scopes need to be balanced.\n\n"
        "- Different error types as the current scope\n"
        "- Different number of nested errors")
    .params([](ParamsBuilder u) {
        std::vector<Value> all;
        for (const auto& s : kErrorScopeFilters) { all.emplace_back(s); }
        return u.combine("errorFilter", all)
                .beginSubcases()
                .combine("numErrors", {Value(1), Value(10), Value(100), Value(1000)});
    })
    .fn([](GpuTest& t) {
        const std::string errorFilter = t.param<std::string>("errorFilter");
        const int         numErrors   = t.param<int>("numErrors");

        DeviceContext ctx = DeviceContext::create(t);

        // Push numErrors scopes (nested).
        for (int i = 0; i < numErrors; ++i) {
            wgpuDevicePushErrorScope(ctx.device, filterFromString(errorFilter));
        }

        // Pop all numErrors scopes in order — none should have caught any error.
        popManyExpectNull(t, ctx.instance, ctx.device, numErrors);

        // Trying to pop an additional non-existing scope should not succeed.
        ScopeResult extraPop = popSync(ctx.instance, ctx.device);
        t.expect(extraPop.status != WGPUPopErrorScopeStatus_Success,
                 "balanced_nesting: extra pop on empty stack should fail");

        ctx.release();
    });

} // namespace
