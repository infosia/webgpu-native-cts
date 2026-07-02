// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/setImmediates.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2026 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 webgpu-native-cts contributors, BSD-3-Clause.
//
// setImmediates validation tests.
//
// PORTING NOTES:
//  - The upstream fixture's init() skips the whole group when
//    !supportsImmediateData(gpu). We mirror that per-test via
//    skipIfImmediateUnsupported() (maxImmediateSize == 0 or undefined), since the
//    harness has no per-fixture async init hook here.
//  - Upstream drives three encoder.setImmediates(rangeOffset, data, dataOffset, size)
//    overload arguments. The C ABI is
//    wgpuXxxSetImmediates(encoder, uint32_t offset, void const* data, size_t size) --
//    no separate dataOffset argument; a JS `data.subarray(dataOffset, dataOffset+size)`
//    is represented natively as `data.data() + dataOffset*elementSize` with `size`
//    bytes.
//  - Several upstream subcases assert `t.shouldThrow('RangeError', ...)` for a
//    WebIDL/typed-array-view *bounds* violation (dataOffset/size exceeding the
//    backing TypedArray's element count). That is a synchronous JS-level check with
//    no C analog -- the C caller owns the pointer and never constructs an
//    out-of-bounds view. Per the established convention in this repo
//    (src/webgpu/api/validation/encoding/cmds/setBindGroup.spec.cpp
//    "u32array_start_and_length", src/webgpu/api/validation/queue/writeBuffer.spec.cpp
//    "ranges"), those subcases skip the native call entirely and reproduce only the
//    *other* observable outcome upstream checks afterward (or, when upstream doesn't
//    check anything afterward for that branch, we don't either). This is noted per
//    test below.
//  - "TODO(#4297): enable Float16Array" from upstream is preserved verbatim: Float16Array
//    stays excluded from arrayType via the same filter upstream uses.

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,setImmediates",
    "setImmediates validation tests.\nTODO(#4297): enable Float16Array");

// ---------------------------------------------------------------------------
// Shared param-value helpers
// ---------------------------------------------------------------------------

// kProgrammableEncoderTypes = ['compute pass', 'render pass', 'render bundle']
static std::vector<Value> kProgrammableEncoderTypeValues() {
    return {
        std::string("compute pass"),
        std::string("render pass"),
        std::string("render bundle"),
    };
}

// kTypedArrayBufferViewKeys, in upstream common/util/util.ts key order.
static std::vector<Value> kTypedArrayBufferViewKeyValues() {
    return {
        std::string("Uint8Array"),   std::string("Uint8ClampedArray"),
        std::string("Uint16Array"),  std::string("Uint32Array"),
        std::string("Int8Array"),    std::string("Int16Array"),
        std::string("Int32Array"),   std::string("Float16Array"),
        std::string("Float32Array"), std::string("Float64Array"),
        std::string("BigInt64Array"), std::string("BigUint64Array"),
    };
}

// BYTES_PER_ELEMENT per typed array (upstream key order).
static int bytesPerElementForTypedArray(const std::string& key) {
    if (key == "Uint8Array" || key == "Uint8ClampedArray" || key == "Int8Array") {
        return 1;
    }
    if (key == "Uint16Array" || key == "Int16Array" || key == "Float16Array") {
        return 2;
    }
    if (key == "Uint32Array" || key == "Int32Array" || key == "Float32Array") {
        return 4;
    }
    // Float64Array, BigInt64Array, BigUint64Array.
    return 8;
}

// Mirrors ImmediateDataTest/SetImmediatesTest.init()'s
// `if (!supportsImmediateData(gpu)) this.skip(...)`.
static void skipIfImmediateUnsupported(AllFeaturesMaxLimitsGpuTest& t) {
    const WGPULimits limits = t.getLimits();
    const uint32_t maxImmediateSize = limits.maxImmediateSize;
    if (maxImmediateSize == 0 || maxImmediateSize == WGPU_LIMIT_U32_UNDEFINED) {
        t.skip("setImmediates not supported (maxImmediateSize is 0 or undefined)");
    }
}

// ---------------------------------------------------------------------------
// ProgrammableEncoderContext
// Wraps a compute pass, render pass, or render bundle encoder, providing
// setImmediates and a validateFinish helper (mirrors CommandBufferMaker).
// ---------------------------------------------------------------------------

struct ProgrammableEncoderContext {
    std::string encoderType;

    WGPUCommandEncoder cmdEnc = nullptr;

    // compute pass path
    WGPUComputePassEncoder computePass = nullptr;

    // render pass / render bundle path
    WGPUTexture           renderTex  = nullptr;
    WGPUTextureView       renderView = nullptr;
    WGPURenderPassEncoder renderPass = nullptr;

    // render bundle path
    WGPURenderBundleEncoder bundleEnc  = nullptr;
    WGPURenderPassEncoder   bundlePass = nullptr;
};

// Create a 16x16 RGBA8Unorm render-attachment texture.
static WGPUTexture createSmallRenderTarget(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    desc.size          = WGPUExtent3D{16, 16, 1};
    desc.mipLevelCount = 1;
    desc.sampleCount   = 1;
    desc.dimension     = WGPUTextureDimension_2D;
    desc.format        = WGPUTextureFormat_RGBA8Unorm;
    desc.usage         = WGPUTextureUsage_RenderAttachment;
    return t.createTextureTracked(desc);
}

// Begin a minimal single-color-attachment render pass.
static WGPURenderPassEncoder beginSimpleRenderPass(
    WGPUCommandEncoder cmdEnc,
    WGPUTextureView    view)
{
    WGPURenderPassColorAttachment colorAttach = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttach.view       = view;
    colorAttach.loadOp     = WGPULoadOp_Clear;
    colorAttach.storeOp    = WGPUStoreOp_Store;
    colorAttach.clearValue = WGPUColor{0.0, 0.0, 0.0, 0.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttach;
    return wgpuCommandEncoderBeginRenderPass(cmdEnc, &passDesc);
}

static ProgrammableEncoderContext makeProgrammableEncoderContext(
    AllFeaturesMaxLimitsGpuTest& t,
    const std::string& encoderType)
{
    ProgrammableEncoderContext ctx;
    ctx.encoderType = encoderType;
    ctx.cmdEnc      = t.createCommandEncoderTracked();

    if (encoderType == "compute pass") {
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        ctx.computePass = wgpuCommandEncoderBeginComputePass(ctx.cmdEnc, &passDesc);
    } else {
        // render pass or render bundle: need a render target
        ctx.renderTex  = createSmallRenderTarget(t);
        WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
        ctx.renderView = t.createViewTracked(ctx.renderTex, vDesc);

        if (encoderType == "render pass") {
            ctx.renderPass = beginSimpleRenderPass(ctx.cmdEnc, ctx.renderView);
        } else {
            // render bundle: open bundle encoder + a pass that will execute it
            WGPUTextureFormat colorFmt = WGPUTextureFormat_RGBA8Unorm;
            WGPURenderBundleEncoderDescriptor bDesc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            bDesc.colorFormatCount = 1;
            bDesc.colorFormats     = &colorFmt;
            bDesc.sampleCount      = 1;
            ctx.bundleEnc  = wgpuDeviceCreateRenderBundleEncoder(t.device(), &bDesc);
            ctx.bundlePass = beginSimpleRenderPass(ctx.cmdEnc, ctx.renderView);
        }
    }
    return ctx;
}

// Call setImmediates on the appropriate encoder.
static void ctxSetImmediates(
    ProgrammableEncoderContext& ctx,
    uint32_t    offset,
    const void* data,
    size_t      size)
{
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderSetImmediates(ctx.computePass, offset, data, size);
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetImmediates(ctx.renderPass, offset, data, size);
    } else {
        wgpuRenderBundleEncoderSetImmediates(ctx.bundleEnc, offset, data, size);
    }
}

// Finish the context: end pass/bundle, return WGPUCommandBuffer.
static WGPUCommandBuffer ctxFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    ProgrammableEncoderContext& ctx)
{
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderEnd(ctx.computePass);
        wgpuComputePassEncoderRelease(ctx.computePass);
        ctx.computePass = nullptr;
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderEnd(ctx.renderPass);
        ctx.renderPass = nullptr;
    } else {
        // render bundle
        WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(ctx.bundleEnc, nullptr);
        ctx.bundleEnc = nullptr;

        wgpuRenderPassEncoderExecuteBundles(ctx.bundlePass, 1, &bundle);
        wgpuRenderBundleRelease(bundle);

        wgpuRenderPassEncoderEnd(ctx.bundlePass);
        ctx.bundlePass = nullptr;
    }
    return t.finishTracked(ctx.cmdEnc);
}

// validateFinish: mirrors CommandBufferMaker.validateFinish(shouldSucceed).
// If !shouldSucceed -> expect a validation error on finish().
// If shouldSucceed  -> finish() must succeed; submit the result (expected to succeed).
static void validateFinish(
    AllFeaturesMaxLimitsGpuTest& t,
    ProgrammableEncoderContext& ctx,
    bool shouldSucceed)
{
    if (!shouldSucceed) {
        t.expectValidationError([&] {
            ctxFinish(t, ctx);
        }, true);
        return;
    }

    WGPUCommandBuffer cb = ctxFinish(t, ctx);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, false);
}

// ---------------------------------------------------------------------------
// g.test('alignment')
// Tests that rangeOffset and contentSize must align to 4 bytes.
// ---------------------------------------------------------------------------
CTS_TEST(g, "alignment")
    .desc("Tests that rangeOffset and contentSize must align to 4 bytes.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypeValues())
            .combine("arrayType", kTypedArrayBufferViewKeyValues())
            .filter([](const ParamRecord& p) {
                return valueAs<std::string>(*findParam(p, "arrayType")) != "Float16Array";
            })
            .combineWithParams({
                // control case: rangeOffset 4 is aligned, contentByteSize 8 is aligned.
                ParamRecord{{"rangeOffset", int64_t(4)}, {"contentByteSize", int64_t(8)}},
                // rangeOffset 6 is unaligned (6 % 4 !== 0).
                ParamRecord{{"rangeOffset", int64_t(6)}, {"contentByteSize", int64_t(8)}},
                // contentByteSize 10 is unaligned (10 % 4 !== 0). Skipped for types with
                // element size > 2 (e.g. Uint32, Uint64) because they cannot form a 10-byte
                // array -- see the filter below.
                ParamRecord{{"rangeOffset", int64_t(4)}, {"contentByteSize", int64_t(10)}},
            })
            .filter([](const ParamRecord& p) {
                const std::string arrayType = valueAs<std::string>(*findParam(p, "arrayType"));
                const int64_t contentByteSize = valueAs<int64_t>(*findParam(p, "contentByteSize"));
                return contentByteSize % bytesPerElementForTypedArray(arrayType) == 0;
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipIfImmediateUnsupported(t);

        const std::string encoderType     = t.param<std::string>("encoderType");
        const std::string arrayType       = t.param<std::string>("arrayType");
        const int64_t     rangeOffset     = t.param<int64_t>("rangeOffset");
        const int64_t     contentByteSize = t.param<int64_t>("contentByteSize");

        const bool isRangeOffsetAligned = (rangeOffset % 4) == 0;
        const bool isContentSizeAligned = (contentByteSize % 4) == 0;

        ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, encoderType);

        if (isContentSizeAligned) {
            std::vector<uint8_t> data(static_cast<size_t>(contentByteSize), 0);
            ctxSetImmediates(ctx, static_cast<uint32_t>(rangeOffset), data.data(), data.size());
        } else {
            // Upstream: shouldThrow('RangeError', ...) -- contentByteSize not a multiple of
            // 4 is a synchronous WebIDL-level check with no C analog (see file header). We
            // skip the call, leaving the encoder untouched, which reproduces upstream's
            // "nothing recorded" observable behavior for this branch.
        }

        // validateFinish(isRangeOffsetAligned) is unconditional in upstream: when the
        // content-size branch above threw (skipped the call here), nothing was recorded and
        // finish() depends only on whatever rangeOffset alignment would have produced; when
        // the call was recorded, finish() depends on rangeOffset alignment as validated by
        // the recorded command.
        validateFinish(t, ctx, isRangeOffsetAligned);
    });

// ---------------------------------------------------------------------------
// g.test('overflow')
// Tests that rangeOffset + contentSize or dataOffset + size is handled correctly if it
// exceeds limits.
// ---------------------------------------------------------------------------
CTS_TEST(g, "overflow")
    .desc(
        "Tests that rangeOffset + contentSize or dataOffset + size is handled correctly if it "
        "exceeds limits.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypeValues())
            .combine("arrayType", kTypedArrayBufferViewKeyValues())
            .filter([](const ParamRecord& p) {
                return valueAs<std::string>(*findParam(p, "arrayType")) != "Float16Array";
            })
            .combineWithParams({
                // control case
                ParamRecord{
                    {"rangeOffset", int64_t(0)}, {"dataOffset", int64_t(0)},
                    {"elementCount", int64_t(4)}, {"_expectedError", Value::undef()}},
                // elementCount 0
                ParamRecord{
                    {"rangeOffset", int64_t(0)}, {"dataOffset", int64_t(0)},
                    {"elementCount", int64_t(0)}, {"_expectedError", Value::undef()}},
                // rangeOffset + contentSize overflows
                ParamRecord{
                    {"rangeOffset", int64_t(2147483640) /* 2**31 - 8 */}, {"dataOffset", int64_t(0)},
                    {"elementCount", int64_t(4)}, {"_expectedError", std::string("validation")}},
                ParamRecord{
                    {"rangeOffset", int64_t(4294967288) /* 2**32 - 8 */}, {"dataOffset", int64_t(0)},
                    {"elementCount", int64_t(4)}, {"_expectedError", std::string("validation")}},
                // dataOffset + size overflows
                ParamRecord{
                    {"rangeOffset", int64_t(0)}, {"dataOffset", int64_t(2147483647) /* 2**31 - 1 */},
                    {"elementCount", int64_t(4)}, {"_expectedError", std::string("RangeError")}},
                ParamRecord{
                    {"rangeOffset", int64_t(0)}, {"dataOffset", int64_t(4294967295) /* 2**32 - 1 */},
                    {"elementCount", int64_t(4)}, {"_expectedError", std::string("RangeError")}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipIfImmediateUnsupported(t);

        const std::string encoderType  = t.param<std::string>("encoderType");
        const std::string arrayType    = t.param<std::string>("arrayType");
        const int64_t     rangeOffset  = t.param<int64_t>("rangeOffset");
        const int64_t     dataOffset   = t.param<int64_t>("dataOffset");
        const int64_t     elementCount = t.param<int64_t>("elementCount");

        const bool isRangeErrorCase =
            t.paramIsString("_expectedError") && t.param<std::string>("_expectedError") == "RangeError";
        const bool isValidationErrorCase =
            t.paramIsString("_expectedError") && t.param<std::string>("_expectedError") == "validation";

        const int      elementSize     = bytesPerElementForTypedArray(arrayType);
        const uint64_t contentByteSize = static_cast<uint64_t>(elementCount) * static_cast<uint64_t>(elementSize);

        ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, encoderType);

        if (isRangeErrorCase) {
            // Upstream: `data = new arrayBufferType(elementCount)` (length == elementCount),
            // then `encoder.setImmediates(rangeOffset, data, dataOffset, elementCount)` with a
            // huge dataOffset (2**31-1 / 2**32-1). dataOffset + elementCount always exceeds
            // data.length -> synchronous RangeError from the WebIDL typed-array-view bounds
            // check, before anything is recorded. No C analog (see file header); we do not
            // call setImmediates and, matching upstream (validateFinish is only invoked in the
            // `else` branch), we do not check finish() either.
            return;
        }

        // Non-RangeError branch: dataOffset is always 0 in these subcases; honor it generally
        // (in bytes) for robustness even though it never contributes here.
        std::vector<uint8_t> data(static_cast<size_t>(contentByteSize), 0);
        const uint8_t* dataStart = data.data() + static_cast<size_t>(dataOffset) * static_cast<size_t>(elementSize);
        ctxSetImmediates(ctx, static_cast<uint32_t>(rangeOffset), dataStart, static_cast<size_t>(contentByteSize));
        validateFinish(t, ctx, !isValidationErrorCase);
    });

// ---------------------------------------------------------------------------
// g.test('out_of_bounds')
// Tests that rangeOffset + contentSize is greater than maxImmediateSize (Validation Error)
// and contentSize is larger than data size (RangeError).
// ---------------------------------------------------------------------------
CTS_TEST(g, "out_of_bounds")
    .desc(
        "Tests that rangeOffset + contentSize is greater than maxImmediateSize (Validation "
        "Error) and contentSize is larger than data size (RangeError).")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypeValues())
            .combine("arrayType", kTypedArrayBufferViewKeyValues())
            .filter([](const ParamRecord& p) {
                return valueAs<std::string>(*findParam(p, "arrayType")) != "Float16Array";
            })
            .combineWithParams({
                // control case
                ParamRecord{{"rangeOffsetDelta", int64_t(0)}, {"dataLengthDelta", int64_t(0)}},
                // rangeOffset + contentSize > maxImmediateSize
                ParamRecord{{"rangeOffsetDelta", int64_t(4)}, {"dataLengthDelta", int64_t(0)}},
                // dataOffset + size > data.length
                ParamRecord{{"rangeOffsetDelta", int64_t(0)}, {"dataLengthDelta", int64_t(-1)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        skipIfImmediateUnsupported(t);

        const std::string encoderType     = t.param<std::string>("encoderType");
        const std::string arrayType       = t.param<std::string>("arrayType");
        const int64_t     rangeOffsetDelta = t.param<int64_t>("rangeOffsetDelta");
        const int64_t     dataLengthDelta  = t.param<int64_t>("dataLengthDelta");

        const int elementSize = bytesPerElementForTypedArray(arrayType);

        const uint32_t maxImmediateSize = t.getLimits().maxImmediateSize;

        // We want contentByteSize to be aligned to 4 bytes to avoid alignment errors.
        // We use 8 bytes to cover all types including BigUint64 (8 bytes).
        const int64_t elementCount    = elementSize >= 8 ? 1 : 8 / elementSize;
        const int64_t contentByteSize = elementCount * elementSize;

        const int64_t rangeOffset = static_cast<int64_t>(maxImmediateSize) - contentByteSize + rangeOffsetDelta;
        const int64_t dataLength  = elementCount + dataLengthDelta;

        ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, encoderType);

        const bool rangeOverLimit = (rangeOffset + contentByteSize) > static_cast<int64_t>(maxImmediateSize);
        const bool dataOverLimit  = elementCount > dataLength;

        if (dataOverLimit) {
            // Upstream: shouldThrow('RangeError', ...) -- the backing TypedArray (dataLength
            // elements) is shorter than the elementCount requested by setImmediates. Typed-array
            // bounds check -> no C analog (see file header). Skip the call and do not check
            // finish(), matching upstream (validateFinish is only called `if (!dataOverLimit)`).
            return;
        }

        std::vector<uint8_t> data(static_cast<size_t>(contentByteSize), 0);
        ctxSetImmediates(ctx, static_cast<uint32_t>(rangeOffset), data.data(), data.size());
        validateFinish(t, ctx, !rangeOverLimit);
    });

} // namespace
