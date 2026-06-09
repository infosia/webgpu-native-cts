// Ported from gpuweb/cts src/webgpu/api/validation/queue/buffer_mapped.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
//
// Note: The upstream class F extends AllFeaturesMaxLimitsGPUTest and provides a
// `runBufferDependencyTest` helper that iterates a single callback over several
// buffer mapping states:
//   1. Unmapped                  -> should succeed
//   2. Pending (mapAsync called, not yet resolved) -> should fail
//   3. Mapped (no getMappedRange) -> should fail
//   4. Mapped with getMappedRange -> should fail
//   5. After unmap               -> should succeed
//   6. mappedAtCreation          -> should fail
//   7. After unmap of mappedAtCreation -> should succeed
//
// In the native C API there is no reliable cross-backend mechanism to observe
// the "pending" state: wgpuBufferMapAsync queues the map callback but may
// resolve it synchronously before any event-processing loop runs. The pending
// substate is therefore not tested here (same precedent as
// api,validation,buffer,mapping:mapAsync,state,mappingPending which is marked
// unimplemented with "JS microtask/pending-map timing"). All other states are
// fully ported.
//
// For writeBuffer the error fires eagerly at the wgpuQueueWriteBuffer call.
// For copy commands the error fires at wgpuQueueSubmit (submit-time validation).

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Test group — upstream fixture is AllFeaturesMaxLimitsGPUTest.
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,queue,buffer_mapped",
    "Validation tests for the map-state of mappable buffers used in submitted command buffers.");

// ---------------------------------------------------------------------------
// runBufferDependencyTest
//
// Mirrors the upstream F.runBufferDependencyTest.  The callback receives a
// WGPUBuffer and performs some operation on it.  The helper asserts:
//   - operation on an unmapped buffer       -> no error
//   - operation while buffer is mapped      -> validation error
//   - operation after unmap                 -> no error
//   - operation on mappedAtCreation buffer  -> validation error
//   - operation after unmap of above        -> no error
//
// The `shouldErrorFn` parameter determines whether the operation itself fires
// the error (writeBuffer, eager) or the error is fired inside the callback
// at submit time (copy commands).  Either way the callback is responsible
// for wrapping the error-producing call in expectValidationError when needed.
//
// The `errorOnMapped` parameter controls whether operating on a mapped buffer
// should produce a validation error.  For all operations tested here it is
// always true, but we keep it explicit for clarity.
// ---------------------------------------------------------------------------
static void runBufferDependencyTest(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBufferUsage usage,
    const std::function<void(WGPUBuffer, bool shouldError)>& callback)
{
    // usage encodes the map mode: MAP_READ -> READ, MAP_WRITE -> WRITE.
    const WGPUMapMode mapMode =
        (usage & WGPUBufferUsage_MapRead) ? WGPUMapMode_Read : WGPUMapMode_Write;

    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = 8;
    desc.usage = usage;
    desc.mappedAtCreation = WGPU_FALSE;

    // Create a mappable buffer and a control buffer that stays unmapped.
    WGPUBuffer mappableBuffer = t.createBufferTracked(desc);
    WGPUBuffer unmappedBuffer = t.createBufferTracked(desc);

    // --- State 1: unmapped --- should succeed.
    callback(mappableBuffer, false);

    // --- State 2: pending (skipped — see file header note) ---

    // Map the buffer synchronously via the harness helper.
    t.expectMapAsync(mappableBuffer, mapMode, true, 0, WGPU_WHOLE_MAP_SIZE);

    // --- State 3: mapped (no getMappedRange queried) --- should fail.
    callback(mappableBuffer, true);

    // Control: unmapped buffer should still succeed.
    callback(unmappedBuffer, false);

    // --- State 4: mapped with getMappedRange queried --- should fail.
    // (Both READ and WRITE branches of getMappedRange.)
    if (mapMode == WGPUMapMode_Read) {
        (void)wgpuBufferGetConstMappedRange(mappableBuffer, 0, WGPU_WHOLE_MAP_SIZE);
    } else {
        (void)wgpuBufferGetMappedRange(mappableBuffer, 0, WGPU_WHOLE_MAP_SIZE);
    }
    callback(mappableBuffer, true);

    // --- State 5: after unmap --- should succeed.
    wgpuBufferUnmap(mappableBuffer);
    callback(mappableBuffer, false);

    // --- State 6: mappedAtCreation buffer --- should fail.
    WGPUBufferDescriptor descMac = WGPU_BUFFER_DESCRIPTOR_INIT;
    descMac.size = 8;
    descMac.usage = usage;
    descMac.mappedAtCreation = WGPU_TRUE;
    WGPUBuffer mappedBuffer = t.createBufferTracked(descMac);

    callback(mappedBuffer, true);

    // Control: unmapped buffer should still succeed.
    callback(unmappedBuffer, false);

    // --- State 7: after unmap of mappedAtCreation buffer --- should succeed.
    wgpuBufferUnmap(mappedBuffer);
    callback(mappedBuffer, false);
}

// ---------------------------------------------------------------------------
// writeBuffer
// Test that an outstanding mapping will prevent writeBuffer calls.
// ---------------------------------------------------------------------------
CTS_TEST(g, "writeBuffer")
    .desc("Test that an outstanding mapping will prevent writeBuffer calls.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // 4-byte write data (writeBuffer requires 4-byte aligned size).
        const uint32_t kData = 42u;

        runBufferDependencyTest(
            t,
            WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            [&](WGPUBuffer buffer, bool shouldError) {
                // writeBuffer validates mapping state eagerly.
                t.expectValidationError([&] {
                    wgpuQueueWriteBuffer(t.queue(), buffer, 0, &kData, sizeof(kData));
                }, shouldError);
            });
    });

// ---------------------------------------------------------------------------
// copyBufferToBuffer
// Test that an outstanding mapping will prevent copyBufferToBuffer commands
// from submitting, both when the mapped buffer is used as source and as
// destination.
// ---------------------------------------------------------------------------
CTS_TEST(g, "copyBufferToBuffer")
    .desc(
        "Test that an outstanding mapping will prevent copyBufferToTexture commands from "
        "submitting, both when used as the source and destination.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // --- As source ---
        WGPUBufferDescriptor destDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        destDesc.size = 8;
        destDesc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer destBuffer = t.createBufferTracked(destDesc);

        runBufferDependencyTest(
            t,
            WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc,
            [&](WGPUBuffer buffer, bool shouldError) {
                WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
                WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);
                wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, 0, destBuffer, 0, 4);
                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
                wgpuCommandEncoderRelease(encoder);
                // Mapping state is checked at submit time.
                t.expectValidationError([&] {
                    wgpuQueueSubmit(t.queue(), 1, &cb);
                }, shouldError);
                if (cb != nullptr) { wgpuCommandBufferRelease(cb); }
            });

        // --- As destination ---
        WGPUBufferDescriptor srcDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        srcDesc.size = 8;
        srcDesc.usage = WGPUBufferUsage_CopySrc;
        WGPUBuffer srcBuffer = t.createBufferTracked(srcDesc);

        runBufferDependencyTest(
            t,
            WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            [&](WGPUBuffer buffer, bool shouldError) {
                WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
                WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);
                wgpuCommandEncoderCopyBufferToBuffer(encoder, srcBuffer, 0, buffer, 0, 4);
                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
                wgpuCommandEncoderRelease(encoder);
                // Mapping state is checked at submit time.
                t.expectValidationError([&] {
                    wgpuQueueSubmit(t.queue(), 1, &cb);
                }, shouldError);
                if (cb != nullptr) { wgpuCommandBufferRelease(cb); }
            });
    });

// ---------------------------------------------------------------------------
// copyBufferToTexture
// Test that an outstanding mapping will prevent copyBufferToTexture commands
// from submitting.
// ---------------------------------------------------------------------------
CTS_TEST(g, "copyBufferToTexture")
    .desc(
        "Test that an outstanding mapping will prevent copyBufferToTexture commands from "
        "submitting.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUExtent3D size = {1, 1, 1};

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = size;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        runBufferDependencyTest(
            t,
            WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc,
            [&](WGPUBuffer buffer, bool shouldError) {
                WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
                WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);

                WGPUTexelCopyBufferInfo src = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
                src.buffer = buffer;
                // bytesPerRow/rowsPerImage left as WGPU_COPY_STRIDE_UNDEFINED (INIT default).
                // For a 1x1 single-layer copy this is valid and avoids buffer-too-small errors.

                WGPUTexelCopyTextureInfo dst = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                dst.texture = texture;

                wgpuCommandEncoderCopyBufferToTexture(encoder, &src, &dst, &size);

                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
                wgpuCommandEncoderRelease(encoder);
                // Mapping state is checked at submit time.
                t.expectValidationError([&] {
                    wgpuQueueSubmit(t.queue(), 1, &cb);
                }, shouldError);
                if (cb != nullptr) { wgpuCommandBufferRelease(cb); }
            });
    });

// ---------------------------------------------------------------------------
// copyTextureToBuffer
// Test that an outstanding mapping will prevent copyTextureToBuffer commands
// from submitting.
// ---------------------------------------------------------------------------
CTS_TEST(g, "copyTextureToBuffer")
    .desc(
        "Test that an outstanding mapping will prevent copyTextureToBuffer commands from "
        "submitting.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        WGPUExtent3D size = {1, 1, 1};

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size = size;
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_CopySrc;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        runBufferDependencyTest(
            t,
            WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
            [&](WGPUBuffer buffer, bool shouldError) {
                WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
                WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);

                WGPUTexelCopyTextureInfo src = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
                src.texture = texture;

                WGPUTexelCopyBufferInfo dst = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
                dst.buffer = buffer;
                // bytesPerRow/rowsPerImage left as WGPU_COPY_STRIDE_UNDEFINED (INIT default).
                // For a 1x1 single-layer copy this is valid and avoids buffer-too-small errors.

                wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &size);

                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
                wgpuCommandEncoderRelease(encoder);
                // Mapping state is checked at submit time.
                t.expectValidationError([&] {
                    wgpuQueueSubmit(t.queue(), 1, &cb);
                }, shouldError);
                if (cb != nullptr) { wgpuCommandBufferRelease(cb); }
            });
    });

// ---------------------------------------------------------------------------
// map_command_recording_order
//
// Test that the order of mapping a buffer relative to when commands are
// recorded does not matter, as long as the buffer is unmapped when the
// commands are submitted.
//
// The upstream uses paramsSubcasesOnly with an `order` JS tuple.  Here we
// encode each order as a comma-separated string of step names and parse it
// at runtime.  The `map` step blocks until the async map completes (using the
// harness expectMapAsync helper).
//
// Valid step names: "record", "map", "unmap", "finish", "submit".
// ---------------------------------------------------------------------------
CTS_TEST(g, "map_command_recording_order")
    .desc(
        "Test that the order of mapping a buffer relative to when commands are recorded that "
        "use it does not matter, as long as the buffer is unmapped when the commands are "
        "submitted.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            // mappedAtCreation=false cases
            ParamRecord{{"order", std::string("record,map,unmap,finish,submit")},
                        {"mappedAtCreation", false}, {"_shouldError", false}},
            ParamRecord{{"order", std::string("record,map,finish,unmap,submit")},
                        {"mappedAtCreation", false}, {"_shouldError", false}},
            ParamRecord{{"order", std::string("record,finish,map,unmap,submit")},
                        {"mappedAtCreation", false}, {"_shouldError", false}},
            ParamRecord{{"order", std::string("map,record,unmap,finish,submit")},
                        {"mappedAtCreation", false}, {"_shouldError", false}},
            ParamRecord{{"order", std::string("map,record,finish,unmap,submit")},
                        {"mappedAtCreation", false}, {"_shouldError", false}},
            ParamRecord{{"order", std::string("map,record,finish,submit,unmap")},
                        {"mappedAtCreation", false}, {"_shouldError", true}},
            ParamRecord{{"order", std::string("record,map,finish,submit,unmap")},
                        {"mappedAtCreation", false}, {"_shouldError", true}},
            ParamRecord{{"order", std::string("record,finish,map,submit,unmap")},
                        {"mappedAtCreation", false}, {"_shouldError", true}},
            // mappedAtCreation=true cases
            ParamRecord{{"order", std::string("record,unmap,finish,submit")},
                        {"mappedAtCreation", true}, {"_shouldError", false}},
            ParamRecord{{"order", std::string("record,finish,unmap,submit")},
                        {"mappedAtCreation", true}, {"_shouldError", false}},
            ParamRecord{{"order", std::string("record,finish,submit,unmap")},
                        {"mappedAtCreation", true}, {"_shouldError", true}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string orderStr    = t.param<std::string>("order");
        const bool mappedAtCreation   = t.param<bool>("mappedAtCreation");
        const bool shouldError        = t.param<bool>("_shouldError");

        // Parse comma-separated order string into a vector of step names.
        std::vector<std::string> order;
        {
            size_t pos = 0;
            while (pos < orderStr.size()) {
                const size_t comma = orderStr.find(',', pos);
                if (comma == std::string::npos) {
                    order.push_back(orderStr.substr(pos));
                    break;
                }
                order.push_back(orderStr.substr(pos, comma - pos));
                pos = comma + 1;
            }
        }

        // Buffer used as COPY_SRC in the recorded command.
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size = 4;
        bufDesc.usage = WGPUBufferUsage_MapWrite | WGPUBufferUsage_CopySrc;
        bufDesc.mappedAtCreation = mappedAtCreation ? WGPU_TRUE : WGPU_FALSE;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        // Copy destination buffer.
        WGPUBufferDescriptor targetDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        targetDesc.size = 4;
        targetDesc.usage = WGPUBufferUsage_CopyDst;
        WGPUBuffer targetBuffer = t.createBufferTracked(targetDesc);

        // Encoder / command buffer state; only valid after "record" / "finish".
        WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);
        WGPUCommandBuffer commandBuffer = nullptr;

        for (const std::string& step : order) {
            if (step == "record") {
                wgpuCommandEncoderCopyBufferToBuffer(encoder, buffer, 0, targetBuffer, 0, 4);
            } else if (step == "map") {
                // Synchronously map the buffer using the harness helper.
                // We expect the map to succeed here.
                t.expectMapAsync(buffer, WGPUMapMode_Write, true, 0, WGPU_WHOLE_MAP_SIZE);
            } else if (step == "unmap") {
                wgpuBufferUnmap(buffer);
            } else if (step == "finish") {
                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                commandBuffer = wgpuCommandEncoderFinish(encoder, &cbDesc);
            } else if (step == "submit") {
                t.expectValidationError([&] {
                    wgpuQueueSubmit(t.queue(), 1, &commandBuffer);
                }, shouldError);
            }
        }

        wgpuCommandEncoderRelease(encoder);
        if (commandBuffer != nullptr) { wgpuCommandBufferRelease(commandBuffer); }
    });

} // namespace
