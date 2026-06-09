// Ported from gpuweb/cts src/webgpu/api/validation/queue/submit.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Test group — uses AllFeaturesMaxLimitsGpuTest, matching upstream class F.
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,queue,submit",
    "Tests submit validation.");

// ---------------------------------------------------------------------------
// createCommandBuffer:
// Mirrors the upstream class F helper:
//   createCommandBuffer({ device?, valid? = true }) -> GPUCommandBuffer
//
// - If valid == true: create encoder, finish, return the command buffer.
// - If valid == false: create encoder, popDebugGroup (unmatched -> invalid
//   encoder in native), finish (errors on invalid encoder).  The resulting
//   command buffer handle is an error object (may be nullptr depending on
//   backend).
//
// NATIVE EAGER ERROR MODEL:
//   wgpuCommandEncoderPopDebugGroup with no matching push fires a validation
//   error eagerly.  We wrap the call inside an expectValidationError scope
//   (via wgpuDevicePushErrorScope / popErrorScopeSync) to suppress the
//   uncaptured error, and also wrap the finish() that follows.  In both
//   cases the errors happen at popDebugGroup and finish; the resulting
//   WGPUCommandBuffer is a null/error object that, when submitted, causes
//   another validation error.
// ---------------------------------------------------------------------------
static WGPUCommandBuffer createCommandBuffer(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUDevice device,
    bool valid)
{
    WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPUCommandBuffer cb = nullptr;

    if (!valid) {
        // Popping a debug group when none are pushed results in an invalid
        // command encoder (validation error fires at popDebugGroup in native).
        // Wrap in an error scope to prevent uncaptured-error surfacing.
        t.expectValidationError([&] {
            wgpuCommandEncoderPopDebugGroup(encoder);
        }, true);

        // finish() on an invalid encoder also produces a validation error and
        // returns a null/error command buffer.
        t.expectValidationError([&] {
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
        }, true);
    } else {
        WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
        cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
    }

    wgpuCommandEncoderRelease(encoder);
    return cb;
}

// Overload: create a valid command buffer on t.device().
static WGPUCommandBuffer createCommandBuffer(AllFeaturesMaxLimitsGpuTest& t) {
    return createCommandBuffer(t, t.device(), true);
}

// ---------------------------------------------------------------------------
// command_buffer,device_mismatch
// ---------------------------------------------------------------------------
CTS_TEST(g, "command_buffer,device_mismatch")
    .desc(
        "Tests submit cannot be called with command buffers created from another device.\n"
        "Test with two command buffers to make sure all command buffers can be validated:\n"
        "- cb0 and cb1 from same device\n"
        "- cb0 and cb1 from different device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"cb0Mismatched", false}, {"cb1Mismatched", false}},  // control case
            ParamRecord{{"cb0Mismatched", true },  {"cb1Mismatched", false}},
            ParamRecord{{"cb0Mismatched", false}, {"cb1Mismatched", true }},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool cb0Mismatched = t.param<bool>("cb0Mismatched");
        const bool cb1Mismatched = t.param<bool>("cb1Mismatched");
        const bool mismatched    = cb0Mismatched || cb1Mismatched;

        WGPUDevice dev0 = cb0Mismatched ? t.mismatchedDevice() : t.device();
        WGPUDevice dev1 = cb1Mismatched ? t.mismatchedDevice() : t.device();

        WGPUCommandBuffer cb0 = createCommandBuffer(t, dev0, true);
        WGPUCommandBuffer cb1 = createCommandBuffer(t, dev1, true);

        WGPUCommandBuffer cbs[2] = { cb0, cb1 };
        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 2, cbs);
        }, mismatched);

        if (cb0 != nullptr) { wgpuCommandBufferRelease(cb0); }
        if (cb1 != nullptr) { wgpuCommandBufferRelease(cb1); }
    });

// ---------------------------------------------------------------------------
// command_buffer,duplicate_buffers
// ---------------------------------------------------------------------------
CTS_TEST(g, "command_buffer,duplicate_buffers")
    .desc(
        "Tests submit cannot be called with the same command buffer listed multiple times.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUCommandBuffer cb = createCommandBuffer(t);

        WGPUCommandBuffer cbs[2] = { cb, cb };
        t.expectValidationError([&] {
            wgpuQueueSubmit(t.queue(), 2, cbs);
        }, true);

        if (cb != nullptr) { wgpuCommandBufferRelease(cb); }
    });

// ---------------------------------------------------------------------------
// command_buffer,submit_invalidates
// ---------------------------------------------------------------------------
CTS_TEST(g, "command_buffer,submit_invalidates")
    .desc(
        "Tests that calling submit invalidates the command buffers passed to it.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUCommandBuffer cb = createCommandBuffer(t);

        // Initial submit of a valid command buffer should pass.
        {
            WGPUCommandBuffer cbs[1] = { cb };
            wgpuQueueSubmit(t.queue(), 1, cbs);
        }

        // Subsequent submits of the same command buffer should fail.
        t.expectValidationError([&] {
            WGPUCommandBuffer cbs[1] = { cb };
            wgpuQueueSubmit(t.queue(), 1, cbs);
        }, true);

        if (cb != nullptr) { wgpuCommandBufferRelease(cb); }
    });

// ---------------------------------------------------------------------------
// command_buffer,invalid_submit_invalidates
// ---------------------------------------------------------------------------
CTS_TEST(g, "command_buffer,invalid_submit_invalidates")
    .desc(
        "Tests that calling submit invalidates all command buffers passed to it, even\n"
        "if they are part of an invalid submit.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUCommandBuffer cb1         = createCommandBuffer(t);
        WGPUCommandBuffer cb1_invalid = createCommandBuffer(t, t.device(), false);

        // Submit should fail because one of the command buffers is invalid.
        t.expectValidationError([&] {
            WGPUCommandBuffer cbs[2] = { cb1, cb1_invalid };
            wgpuQueueSubmit(t.queue(), 2, cbs);
        }, true);

        // Subsequent submits of the previously valid command buffer should fail.
        t.expectValidationError([&] {
            WGPUCommandBuffer cbs[1] = { cb1 };
            wgpuQueueSubmit(t.queue(), 1, cbs);
        }, true);

        // The order of the invalid and valid command buffers in the submit
        // array should not matter.
        WGPUCommandBuffer cb2         = createCommandBuffer(t);
        WGPUCommandBuffer cb2_invalid = createCommandBuffer(t, t.device(), false);

        t.expectValidationError([&] {
            WGPUCommandBuffer cbs[2] = { cb2_invalid, cb2 };
            wgpuQueueSubmit(t.queue(), 2, cbs);
        }, true);

        t.expectValidationError([&] {
            WGPUCommandBuffer cbs[1] = { cb2 };
            wgpuQueueSubmit(t.queue(), 1, cbs);
        }, true);

        if (cb1         != nullptr) { wgpuCommandBufferRelease(cb1); }
        if (cb1_invalid != nullptr) { wgpuCommandBufferRelease(cb1_invalid); }
        if (cb2         != nullptr) { wgpuCommandBufferRelease(cb2); }
        if (cb2_invalid != nullptr) { wgpuCommandBufferRelease(cb2_invalid); }
    });

} // namespace
