// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/compute_pass.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Helpers: no-op compute pipeline and error compute pipeline
// Mirrors vtu.createNoOpComputePipeline / vtu.createErrorComputePipeline
// ---------------------------------------------------------------------------

static WGPUStringView makeStringView(const char* str) {
    return WGPUStringView{str, WGPU_STRLEN};
}

// createNoOpComputePipeline: creates a valid compute pipeline with a @compute @workgroup_size(1) shader.
// layout may be null (→ 'auto') or an explicit pipeline layout.
static WGPUComputePipeline createNoOpComputePipeline(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUPipelineLayout layout = nullptr)
{
    WGPUShaderModule shaderModule = t.createShaderModuleTracked(
        "@compute @workgroup_size(1) fn main() {}");

    WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
    desc.layout             = layout; // null → 'auto'
    desc.compute.module     = shaderModule;
    desc.compute.entryPoint = makeStringView("main");
    return t.createComputePipelineTracked(desc);
}

// createErrorComputePipeline: creates an invalid compute pipeline by using empty WGSL.
static WGPUComputePipeline createErrorComputePipeline(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUComputePipeline pipeline = nullptr;
    t.expectValidationError([&] {
        WGPUShaderModule shaderModule = t.createShaderModuleTracked("");

        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout             = nullptr;
        desc.compute.module     = shaderModule;
        desc.compute.entryPoint = makeStringView("");
        pipeline = t.createComputePipelineTracked(desc);
    }, true);
    return pipeline;
}

// ---------------------------------------------------------------------------
// Helper: createIndirectBuffer
// Mirrors F.createIndirectBuffer(state, data) from upstream.
// ---------------------------------------------------------------------------

// createIndirectBuffer: creates a buffer with INDIRECT | COPY_DST usage (or invalid usage),
// optionally writes data, and optionally destroys it.
static WGPUBuffer createIndirectBuffer(
    AllFeaturesMaxLimitsGpuTest& t,
    ResourceState state,
    const uint32_t* data,
    size_t dataCount)
{
    const uint64_t byteLen = static_cast<uint64_t>(dataCount) * sizeof(uint32_t);

    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size  = byteLen;
    desc.usage = WGPUBufferUsage_Indirect | WGPUBufferUsage_CopyDst;

    if (state == ResourceState::Invalid) {
        desc.usage = 0xffff; // Invalid GPUBufferUsage
        // Create buffer inside error scope to capture the validation error.
        WGPUBuffer buf = nullptr;
        t.expectValidationError([&] {
            buf = t.createBufferTracked(desc);
        }, true);
        return buf;
    }

    WGPUBuffer buffer = t.createBufferTracked(desc);

    if (state == ResourceState::Valid) {
        t.queueWriteBuffer(buffer, 0, data, byteLen);
    }

    if (state == ResourceState::Destroyed) {
        wgpuBufferDestroy(buffer);
    }

    return buffer;
}

// ---------------------------------------------------------------------------
// Compute pass encoding context helpers
// Mirrors the JS createEncoder('compute pass') pattern.
// ---------------------------------------------------------------------------

struct ComputePassContext {
    WGPUCommandEncoder      cmdEnc   = nullptr;
    WGPUComputePassEncoder  passEnc  = nullptr;
};

static ComputePassContext makeComputePassContext(AllFeaturesMaxLimitsGpuTest& t) {
    ComputePassContext ctx;
    ctx.cmdEnc = t.createCommandEncoderTracked();
    WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
    ctx.passEnc = wgpuCommandEncoderBeginComputePass(ctx.cmdEnc, &passDesc);
    return ctx;
}

// Finish the compute pass and return a WGPUCommandBuffer.
static WGPUCommandBuffer computePassFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    ComputePassContext& ctx)
{
    wgpuComputePassEncoderEnd(ctx.passEnc);
    wgpuComputePassEncoderRelease(ctx.passEnc);
    ctx.passEnc = nullptr;
    return t.finishTracked(ctx.cmdEnc);
}

// validateFinish(shouldSucceed):
//   If !shouldSucceed → expect a validation error on finish().
//   If shouldSucceed  → finish() must succeed; submit the result (expected to succeed).
static void validateFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    ComputePassContext& ctx,
    bool shouldSucceed)
{
    if (!shouldSucceed) {
        t.expectValidationError([&] {
            computePassFinish(t, ctx);
        }, true);
        return;
    }

    WGPUCommandBuffer cb = computePassFinish(t, ctx);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, false);
}

// validateFinishAndSubmit(shouldBeValid, submitShouldSucceedIfValid):
//   Mirrors CommandBufferMaker.validateFinishAndSubmit.
//   - If !shouldBeValid → finish() should fail (validation error).
//   - If shouldBeValid  → finish() succeeds; submit() should succeed iff submitShouldSucceedIfValid.
static void validateFinishAndSubmit(
    AllFeaturesMaxLimitsGpuTest& t,
    ComputePassContext& ctx,
    bool shouldBeValid,
    bool submitShouldSucceedIfValid)
{
    if (!shouldBeValid) {
        t.expectValidationError([&] {
            computePassFinish(t, ctx);
        }, true);
        return;
    }

    WGPUCommandBuffer cb = computePassFinish(t, ctx);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, !submitShouldSucceedIfValid);
}

// validateFinishAndSubmitGivenState(state):
//   Mirrors CommandBufferMaker.validateFinishAndSubmitGivenState.
//   = validateFinishAndSubmit(state != Invalid, state != Destroyed)
static void validateFinishAndSubmitGivenState(
    AllFeaturesMaxLimitsGpuTest& t,
    ComputePassContext& ctx,
    ResourceState state)
{
    validateFinishAndSubmit(
        t, ctx,
        state != ResourceState::Invalid,
        state != ResourceState::Destroyed);
}

// makeValueTestVariant: mirrors makeValueTestVariant(base, {mult, add}) = base * mult + add.
static uint32_t makeValueTestVariant(uint32_t base, uint32_t mult, uint32_t add) {
    // Note: this intentionally wraps for large values like 0xffffffff (add=0xffffffff, mult=0)
    return base * mult + add;
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,compute_pass",
    "API validation test for compute pass\n"
    "\n"
    "Does **not** test usage scopes (resource_usages/) or programmable pass stuff (programmable_pass).");

// ---------------------------------------------------------------------------
// g.test('set_pipeline')
// setPipeline should generate an error iff using an 'invalid' pipeline.
// ---------------------------------------------------------------------------
CTS_TEST(g, "set_pipeline")
    .desc("setPipeline should generate an error iff using an 'invalid' pipeline.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("state", {std::string("valid"), std::string("invalid")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string state = t.param<std::string>("state");

        WGPUComputePipeline pipeline = (state == "valid")
            ? createNoOpComputePipeline(t)
            : createErrorComputePipeline(t);

        ComputePassContext ctx = makeComputePassContext(t);
        wgpuComputePassEncoderSetPipeline(ctx.passEnc, pipeline);

        const ResourceState resourceState = parseResourceState(state);
        validateFinishAndSubmitGivenState(t, ctx, resourceState);
    });

// ---------------------------------------------------------------------------
// g.test('pipeline,device_mismatch')
// Tests setPipeline cannot be called with a compute pipeline created from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "pipeline,device_mismatch")
    .desc("Tests setPipeline cannot be called with a compute pipeline created from another device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");

        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();

        // Create shader module and compute pipeline on the source device.
        const char* code = "@compute @workgroup_size(1) fn main() {}";
        WGPUShaderSourceWGSL wgslSource = WGPU_SHADER_SOURCE_WGSL_INIT;
        wgslSource.code = makeStringView(code);
        WGPUShaderModuleDescriptor smDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        smDesc.nextInChain = &wgslSource.chain;

        WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(sourceDevice, &smDesc);

        WGPUComputePipelineDescriptor pipelineDesc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pipelineDesc.layout             = nullptr; // 'auto'
        pipelineDesc.compute.module     = shaderModule;
        pipelineDesc.compute.entryPoint = makeStringView("main");

        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(sourceDevice, &pipelineDesc);
        wgpuShaderModuleRelease(shaderModule);

        ComputePassContext ctx = makeComputePassContext(t);
        wgpuComputePassEncoderSetPipeline(ctx.passEnc, pipeline);
        validateFinish(t, ctx, !mismatched);

        wgpuComputePipelineRelease(pipeline);
    });

// ---------------------------------------------------------------------------
// g.test('dispatch_sizes')
// Test 'direct' and 'indirect' dispatch with various sizes.
// ---------------------------------------------------------------------------

// Encode the largeDimValueVariant struct as two separate int64_t params (mult, add).
// Upstream variants:
//   {mult:0, add:0}, {mult:0, add:1}, {mult:1, add:0}, {mult:1, add:1},
//   {mult:0, add:0x7fffffff}, {mult:0, add:0xffffffff}
CTS_TEST(g, "dispatch_sizes")
    .desc(
        "Test 'direct' and 'indirect' dispatch with various sizes.\n"
        "\n"
        "Only direct dispatches can produce validation errors.\n"
        "Workgroup sizes:\n"
        "  - valid: { zero, one, just under limit }\n"
        "  - invalid: { just over limit, way over limit }\n"
        "\n"
        "TODO: Verify that the invalid cases don't execute any invocations at all.")
    .params([](ParamsBuilder u) {
        return u
            .combine("dispatchType", {std::string("direct"), std::string("indirect")})
            .combineWithParams({
                ParamRecord{{"lv_mult", int64_t(0)}, {"lv_add", int64_t(0)}},
                ParamRecord{{"lv_mult", int64_t(0)}, {"lv_add", int64_t(1)}},
                ParamRecord{{"lv_mult", int64_t(1)}, {"lv_add", int64_t(0)}},
                ParamRecord{{"lv_mult", int64_t(1)}, {"lv_add", int64_t(1)}},
                ParamRecord{{"lv_mult", int64_t(0)}, {"lv_add", int64_t(0x7fffffff)}},
                ParamRecord{{"lv_mult", int64_t(0)}, {"lv_add", int64_t(0xffffffff)}},
            })
            .beginSubcases()
            .combine("largeDimIndex", {int64_t(0), int64_t(1), int64_t(2)})
            .combine("smallDimValue", {int64_t(0), int64_t(1)});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string dispatchType  = t.param<std::string>("dispatchType");
        const int64_t     lv_mult       = t.param<int64_t>("lv_mult");
        const int64_t     lv_add        = t.param<int64_t>("lv_add");
        const int64_t     largeDimIndex = t.param<int64_t>("largeDimIndex");
        const int64_t     smallDimValue = t.param<int64_t>("smallDimValue");

        const WGPULimits  limits       = t.getLimits();
        const uint32_t    maxDispatch  = limits.maxComputeWorkgroupsPerDimension;

        // makeValueTestVariant(maxDispatch, {mult, add}) = maxDispatch * mult + add
        const uint32_t largeDimValue = makeValueTestVariant(
            maxDispatch,
            static_cast<uint32_t>(lv_mult),
            static_cast<uint32_t>(lv_add));

        WGPUComputePipeline pipeline = createNoOpComputePipeline(t);

        uint32_t workSizes[3];
        workSizes[0] = static_cast<uint32_t>(smallDimValue);
        workSizes[1] = static_cast<uint32_t>(smallDimValue);
        workSizes[2] = static_cast<uint32_t>(smallDimValue);
        workSizes[static_cast<size_t>(largeDimIndex)] = largeDimValue;

        ComputePassContext ctx = makeComputePassContext(t);
        wgpuComputePassEncoderSetPipeline(ctx.passEnc, pipeline);

        if (dispatchType == "direct") {
            wgpuComputePassEncoderDispatchWorkgroups(
                ctx.passEnc, workSizes[0], workSizes[1], workSizes[2]);
        } else {
            // indirect: create a valid indirect buffer with the work sizes
            WGPUBuffer indirectBuffer = createIndirectBuffer(
                t, ResourceState::Valid, workSizes, 3);
            wgpuComputePassEncoderDispatchWorkgroupsIndirect(
                ctx.passEnc, indirectBuffer, 0);
        }

        const bool shouldError =
            dispatchType == "direct" &&
            (workSizes[0] > maxDispatch ||
             workSizes[1] > maxDispatch ||
             workSizes[2] > maxDispatch);

        // validateFinishAndSubmit(!shouldError, true)
        validateFinishAndSubmit(t, ctx, !shouldError, true);
    });

// ---------------------------------------------------------------------------
// g.test('indirect_dispatch_buffer_state')
// Test dispatchWorkgroupsIndirect validation with various buffer states and offsets.
// ---------------------------------------------------------------------------

// kBufferData = new Uint32Array(6).fill(1)
static const uint32_t kBufferData[6] = {1, 1, 1, 1, 1, 1};
static const uint64_t kBufferDataByteLength = 6 * sizeof(uint32_t); // = 24

CTS_TEST(g, "indirect_dispatch_buffer_state")
    .desc(
        "Test dispatchWorkgroupsIndirect validation by submitting various dispatches with a no-op pipeline\n"
        "and an indirectBuffer with 6 elements.\n"
        "- indirectBuffer: {'valid', 'invalid', 'destroyed'}\n"
        "- indirectOffset:\n"
        "  - valid, within the buffer: {beginning, middle, end} of the buffer\n"
        "  - invalid, non-multiple of 4\n"
        "  - invalid, the last element is outside the buffer")
    .params([](ParamsBuilder u) {
        // kBufferData.byteLength = 24; Uint32Array.BYTES_PER_ELEMENT = 4
        // Valid offsets: 0, 4, 12 (= 24 - 3*4)
        // Invalid offset, non-multiple of 4: 1
        // Invalid offset, last element outside buffer: 16 (= 24 - 2*4)
        return u.beginSubcases()
            .combine("state", resourceStateValues())
            .combine("offset", {
                int64_t(0),   // valid: beginning
                int64_t(4),   // valid: Uint32Array.BYTES_PER_ELEMENT = 4
                int64_t(12),  // valid: kBufferData.byteLength - 3 * BYTES_PER_ELEMENT = 24 - 12 = 12
                int64_t(1),   // invalid: non-multiple of 4
                int64_t(16),  // invalid: kBufferData.byteLength - 2 * BYTES_PER_ELEMENT = 24 - 8 = 16
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const ResourceState state  = parseResourceState(t.param<std::string>("state"));
        const uint64_t      offset = t.param<uint64_t>("offset");

        WGPUComputePipeline pipeline = createNoOpComputePipeline(t);
        WGPUBuffer buffer = createIndirectBuffer(t, state, kBufferData, 6);

        ComputePassContext ctx = makeComputePassContext(t);
        wgpuComputePassEncoderSetPipeline(ctx.passEnc, pipeline);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(ctx.passEnc, buffer, offset);

        // finishShouldError: invalid buffer, or non-multiple-of-4 offset, or offset+12 > 24
        const bool finishShouldError =
            (state == ResourceState::Invalid) ||
            (offset % 4 != 0) ||
            (offset + 3 * sizeof(uint32_t) > kBufferDataByteLength);

        // validateFinishAndSubmit(!finishShouldError, state != 'destroyed')
        validateFinishAndSubmit(t, ctx,
            !finishShouldError,
            state != ResourceState::Destroyed);
    });

// ---------------------------------------------------------------------------
// g.test('indirect_dispatch_buffer,device_mismatch')
// Tests dispatchWorkgroupsIndirect cannot be called with a buffer from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "indirect_dispatch_buffer,device_mismatch")
    .desc(
        "Tests dispatchWorkgroupsIndirect cannot be called with an indirect buffer created from another device")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool mismatched = t.param<bool>("mismatched");

        WGPUComputePipeline pipeline = createNoOpComputePipeline(t);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 16;
        bufDesc.usage = WGPUBufferUsage_Indirect;

        WGPUBuffer buffer = mismatched
            ? t.createBufferOnMismatchedDevice(bufDesc)
            : t.createBufferTracked(bufDesc);

        ComputePassContext ctx = makeComputePassContext(t);
        wgpuComputePassEncoderSetPipeline(ctx.passEnc, pipeline);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(ctx.passEnc, buffer, 0);
        validateFinish(t, ctx, !mismatched);
    });

// ---------------------------------------------------------------------------
// g.test('indirect_dispatch_buffer,usage')
// Tests dispatchWorkgroupsIndirect generates a validation error if the buffer usage
// does not contain INDIRECT usage.
// ---------------------------------------------------------------------------

static std::vector<Value> bufferUsageValues() {
    std::vector<Value> values;
    values.reserve(kBufferUsages.size());
    for (WGPUBufferUsage usage : kBufferUsages) {
        values.emplace_back(static_cast<int64_t>(usage));
    }
    return values;
}

CTS_TEST(g, "indirect_dispatch_buffer,usage")
    .desc(
        "Tests dispatchWorkgroupsIndirect generates a validation error if the buffer usage does not\n"
        "contain INDIRECT usage.")
    .params([](ParamsBuilder u) {
        // Upstream: combine bufferUsage0 x bufferUsage1, then filter out combinations that
        // include MAP_READ or MAP_WRITE.
        return u.beginSubcases()
            .combine("bufferUsage0", bufferUsageValues())
            .combine("bufferUsage1", bufferUsageValues())
            .filter([](const ParamRecord& p) -> bool {
                const Value* u0 = findParam(p, "bufferUsage0");
                const Value* u1 = findParam(p, "bufferUsage1");
                if (!u0 || !u1) return false;
                const int64_t usage0 = valueAs<int64_t>(*u0);
                const int64_t usage1 = valueAs<int64_t>(*u1);
                const int64_t combined = usage0 | usage1;
                // Filter OUT if MAP_READ or MAP_WRITE is present
                const int64_t mapReadWrite =
                    static_cast<int64_t>(WGPUBufferUsage_MapRead) |
                    static_cast<int64_t>(WGPUBufferUsage_MapWrite);
                return (combined & mapReadWrite) == 0;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUBufferUsage bufferUsage0 = t.param<WGPUBufferUsage>("bufferUsage0");
        const WGPUBufferUsage bufferUsage1 = t.param<WGPUBufferUsage>("bufferUsage1");

        const WGPUBufferUsage bufferUsage = bufferUsage0 | bufferUsage1;

        // Create pipeline with explicit layout (empty bind group layouts)
        WGPUPipelineLayoutDescriptor layoutDesc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.bindGroupLayoutCount = 0;
        layoutDesc.bindGroupLayouts     = nullptr;
        WGPUPipelineLayout layout = t.createPipelineLayoutTracked(layoutDesc);

        WGPUComputePipeline pipeline = createNoOpComputePipeline(t, layout);

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 16;
        bufDesc.usage = bufferUsage;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        const bool success = (bufferUsage & WGPUBufferUsage_Indirect) != 0;

        ComputePassContext ctx = makeComputePassContext(t);
        wgpuComputePassEncoderSetPipeline(ctx.passEnc, pipeline);
        wgpuComputePassEncoderDispatchWorkgroupsIndirect(ctx.passEnc, buffer, 0);
        validateFinish(t, ctx, success);
    });

} // namespace
