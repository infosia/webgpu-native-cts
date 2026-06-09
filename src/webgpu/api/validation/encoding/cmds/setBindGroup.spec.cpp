// Ported from gpuweb/cts src/webgpu/api/validation/encoding/cmds/setBindGroup.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

// kMinDynamicBufferOffsetAlignment from upstream capability_info.ts
static constexpr uint32_t kMinDynamicBufferOffsetAlignment = 256;

// kProgrammableEncoderTypes = ['compute pass', 'render pass', 'render bundle']
static std::vector<Value> kProgrammableEncoderTypeValues() {
    return {
        std::string("compute pass"),
        std::string("render pass"),
        std::string("render bundle"),
    };
}

// ---------------------------------------------------------------------------
// ProgrammableEncoderContext
// Wraps a compute pass, render pass, or render bundle encoder, providing
// setBindGroup and validateFinish / validateFinishAndSubmit helpers.
// ---------------------------------------------------------------------------

struct ProgrammableEncoderContext {
    std::string encoderType;

    WGPUCommandEncoder      cmdEnc     = nullptr;

    // compute pass path
    WGPUComputePassEncoder  computePass = nullptr;

    // render pass / render bundle path
    WGPUTexture             renderTex  = nullptr;
    WGPUTextureView         renderView = nullptr;
    WGPURenderPassEncoder   renderPass = nullptr;

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
            // render bundle: open bundle encoder + a pass that executes it
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

// Call setBindGroup on the appropriate encoder.
static void ctxSetBindGroup(
    ProgrammableEncoderContext& ctx,
    uint32_t          index,
    WGPUBindGroup     bindGroup,
    size_t            dynamicOffsetCount,
    const uint32_t*   dynamicOffsets)
{
    if (ctx.encoderType == "compute pass") {
        wgpuComputePassEncoderSetBindGroup(
            ctx.computePass, index, bindGroup, dynamicOffsetCount, dynamicOffsets);
    } else if (ctx.encoderType == "render pass") {
        wgpuRenderPassEncoderSetBindGroup(
            ctx.renderPass, index, bindGroup, dynamicOffsetCount, dynamicOffsets);
    } else {
        wgpuRenderBundleEncoderSetBindGroup(
            ctx.bundleEnc, index, bindGroup, dynamicOffsetCount, dynamicOffsets);
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
// If !shouldSucceed → expect a validation error on finish().
// If shouldSucceed  → finish() must succeed; submit the result (expected to succeed).
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

// validateFinishAndSubmit: mirrors CommandBufferMaker.validateFinishAndSubmit(shouldBeValid, shouldSubmitSucceed).
// - If !shouldBeValid  → finish() should fail (validation error).
// - If shouldBeValid   → finish() succeeds; submit() should succeed iff shouldSubmitSucceed.
static void validateFinishAndSubmit(
    AllFeaturesMaxLimitsGpuTest& t,
    ProgrammableEncoderContext& ctx,
    bool shouldBeValid,
    bool shouldSubmitSucceed)
{
    if (!shouldBeValid) {
        t.expectValidationError([&] {
            ctxFinish(t, ctx);
        }, true);
        return;
    }

    WGPUCommandBuffer cb = ctxFinish(t, ctx);
    t.expectValidationError([&] {
        wgpuQueueSubmit(t.queue(), 1, &cb);
    }, !shouldSubmitSucceed);
}

// encoderTypeToStageFlag: mirrors F.encoderTypeToStageFlag from upstream.
// 'compute pass' → Compute; 'render pass'/'render bundle' → Fragment
static WGPUShaderStage encoderTypeToStageFlag(const std::string& encoderType) {
    if (encoderType == "compute pass") {
        return WGPUShaderStage_Compute;
    }
    // render pass or render bundle
    return WGPUShaderStage_Fragment;
}

// ---------------------------------------------------------------------------
// createBindGroupForTest
//
// Mirrors F.createBindGroup(state, resourceType, encoderType, indices):
//   - state == 'invalid':   create an invalid bind group (duplicate binding injected)
//   - state == 'destroyed': create a valid bind group with destroyed resources
//   - state == 'valid':     create a valid bind group
// ---------------------------------------------------------------------------

// createBindingResource helpers
static WGPUBindGroupEntry makeTextureBindGroupEntry(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t binding,
    bool destroyAfterCreate)
{
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.size          = WGPUExtent3D{1, 1, 1};
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount   = 1;
    texDesc.dimension     = WGPUTextureDimension_2D;
    texDesc.format        = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage         = WGPUTextureUsage_TextureBinding;

    WGPUTexture texture = t.createTextureTracked(texDesc);
    WGPUTextureViewDescriptor vDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(texture, vDesc);

    if (destroyAfterCreate) {
        wgpuTextureDestroy(texture);
    }

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding     = binding;
    entry.textureView = view;
    return entry;
}

static WGPUBindGroupEntry makeBufferBindGroupEntry(
    AllFeaturesMaxLimitsGpuTest& t,
    uint32_t binding,
    bool destroyed)
{
    WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    bufDesc.size  = 16;
    bufDesc.usage = WGPUBufferUsage_Uniform;

    WGPUBuffer buffer = destroyed
        ? t.createBufferWithState(ResourceState::Destroyed, bufDesc)
        : t.createBufferTracked(bufDesc);

    WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
    entry.binding = binding;
    entry.buffer  = buffer;
    entry.offset  = 0;
    entry.size    = WGPU_WHOLE_SIZE;
    return entry;
}

struct BindGroupObjects {
    WGPUBindGroupLayout layout = nullptr;
    WGPUBindGroup       bindGroup = nullptr;
};

// Create the bind group objects as described by upstream's createBindGroup.
// indices: binding numbers to use.
// If state == Invalid: we inject an extra duplicate binding to force an invalid BGL/BG.
static BindGroupObjects createBindGroupForTest(
    AllFeaturesMaxLimitsGpuTest& t,
    ResourceState state,
    const std::string& resourceType,
    const std::string& encoderType,
    const std::vector<uint32_t>& indices)
{
    const WGPUShaderStage visibility = encoderTypeToStageFlag(encoderType);

    // If invalid: inject a duplicate binding (make length = indices.size() + 1, all zeros).
    std::vector<uint32_t> effectiveIndices = indices;
    if (state == ResourceState::Invalid) {
        // upstream: indices = new Array(indices.length + 1).fill(0)
        effectiveIndices.assign(indices.size() + 1, 0u);
    }

    // Build BGL entries
    std::vector<WGPUBindGroupLayoutEntry> bglEntries;
    bglEntries.reserve(effectiveIndices.size());
    for (uint32_t idx : effectiveIndices) {
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding    = idx;
        entry.visibility = visibility;
        if (resourceType == "buffer") {
            entry.buffer = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            entry.buffer.type = WGPUBufferBindingType_Uniform;
        } else {
            // texture
            entry.texture = WGPU_TEXTURE_BINDING_LAYOUT_INIT;
            entry.texture.sampleType = WGPUTextureSampleType_Float;
        }
        bglEntries.push_back(entry);
    }

    // For 'invalid': create layout inside error scope to capture the expected duplicate-binding error.
    WGPUBindGroupLayout layout = nullptr;
    if (state == ResourceState::Invalid) {
        t.expectValidationError([&] {
            WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
            layoutDesc.entryCount = static_cast<uint32_t>(bglEntries.size());
            layoutDesc.entries    = bglEntries.data();
            layout = t.createBindGroupLayoutTracked(layoutDesc);
        }, true);
    } else {
        WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.entryCount = static_cast<uint32_t>(bglEntries.size());
        layoutDesc.entries    = bglEntries.data();
        layout = t.createBindGroupLayoutTracked(layoutDesc);
    }

    // Build bind group entries
    const bool destroyResources = (state == ResourceState::Destroyed);
    std::vector<WGPUBindGroupEntry> bgEntries;
    bgEntries.reserve(effectiveIndices.size());
    for (uint32_t idx : effectiveIndices) {
        if (resourceType == "buffer") {
            bgEntries.push_back(makeBufferBindGroupEntry(t, idx, destroyResources));
        } else {
            bgEntries.push_back(makeTextureBindGroupEntry(t, idx, destroyResources));
        }
    }

    // For 'invalid': create bind group inside error scope too (layout is invalid → BG is invalid).
    WGPUBindGroup bindGroup = nullptr;
    if (state == ResourceState::Invalid) {
        t.expectValidationError([&] {
            WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
            bgDesc.layout     = layout;
            bgDesc.entryCount = static_cast<uint32_t>(bgEntries.size());
            bgDesc.entries    = bgEntries.data();
            bindGroup = t.createBindGroupTracked(bgDesc);
        }, true);
    } else {
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = layout;
        bgDesc.entryCount = static_cast<uint32_t>(bgEntries.size());
        bgDesc.entries    = bgEntries.data();
        bindGroup = t.createBindGroupTracked(bgDesc);
    }

    return {layout, bindGroup};
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,cmds,setBindGroup",
    "setBindGroup validation tests.");

// ---------------------------------------------------------------------------
// g.test('state_and_binding_index')
// Tests that setBindGroup correctly handles {valid, invalid, destroyed} bindGroups.
// ---------------------------------------------------------------------------
CTS_TEST(g, "state_and_binding_index")
    .desc("Tests that setBindGroup correctly handles {valid, invalid, destroyed} bindGroups.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kProgrammableEncoderTypeValues())
            .combine("state",       resourceStateValues())
            .combine("resourceType", {std::string("buffer"), std::string("texture")});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string  encoderType  = t.param<std::string>("encoderType");
        const ResourceState state       = parseResourceState(t.param<std::string>("state"));
        const std::string  resourceType = t.param<std::string>("resourceType");

        const WGPULimits limits       = t.getLimits();
        const uint32_t   maxBindGroups = limits.maxBindGroups;

        // Mirrors JS: for (const index of [1, maxBindGroups - 1, maxBindGroups])
        const uint32_t indices[] = {
            1u,
            maxBindGroups > 0u ? maxBindGroups - 1u : 0u,
            maxBindGroups,
        };

        for (uint32_t index : indices) {
            BindGroupObjects objs = createBindGroupForTest(
                t, state, resourceType, encoderType, {index});

            ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, encoderType);
            ctxSetBindGroup(ctx, index, objs.bindGroup, 0, nullptr);

            // validateFinishAndSubmit(state !== 'invalid' && index < maxBindGroups, state !== 'destroyed')
            const bool shouldBeValid    = (state != ResourceState::Invalid) && (index < maxBindGroups);
            const bool shouldSubmitSucceed = (state != ResourceState::Destroyed);
            validateFinishAndSubmit(t, ctx, shouldBeValid, shouldSubmitSucceed);
        }
    });

// ---------------------------------------------------------------------------
// g.test('bind_group,device_mismatch')
// Tests setBindGroup cannot be called with a bind group created from another device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "bind_group,device_mismatch")
    .desc(
        "Tests setBindGroup cannot be called with a bind group created from another device\n"
        "- x= setBindGroup {sequence overload, Uint32Array overload}")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kProgrammableEncoderTypeValues())
            .beginSubcases()
            .combine("useU32Array", {true, false})
            .combine("mismatched",  {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");
        const bool        useU32Array = t.param<bool>("useU32Array");
        const bool        mismatched  = t.param<bool>("mismatched");

        WGPUDevice sourceDevice = mismatched ? t.mismatchedDevice() : t.device();

        const WGPUShaderStage visibility = encoderTypeToStageFlag(encoderType);

        // Create buffer on source device.
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 4;
        bufDesc.usage = WGPUBufferUsage_Uniform;

        WGPUBuffer buffer = mismatched
            ? t.createBufferOnMismatchedDevice(bufDesc)
            : t.createBufferTracked(bufDesc);

        // Create BGL with hasDynamicOffset = useU32Array (mirrors upstream).
        WGPUBindGroupLayoutEntry bglEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        bglEntry.binding    = 0;
        bglEntry.visibility = visibility;
        bglEntry.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        bglEntry.buffer.type             = WGPUBufferBindingType_Uniform;
        bglEntry.buffer.hasDynamicOffset = useU32Array ? WGPU_TRUE : WGPU_FALSE;

        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries    = &bglEntry;

        WGPUBindGroupLayout layout = mismatched
            ? t.createBindGroupLayoutOnMismatchedDevice(bglDesc)
            : t.createBindGroupLayoutTracked(bglDesc);

        // Create bind group on source device.
        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding = 0;
        bgEntry.buffer  = buffer;
        bgEntry.offset  = 0;
        bgEntry.size    = WGPU_WHOLE_SIZE;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = layout;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &bgEntry;

        // Use wgpuDeviceCreateBindGroup directly so we can release it ourselves.
        // For mismatched=false the bind group is on t.device(); for mismatched=true it
        // is on the mismatched device (not tracked by the harness in either case).
        WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(sourceDevice, &bgDesc);

        ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, encoderType);

        if (useU32Array) {
            // Uint32Array overload: pass one dynamic offset of 0.
            const uint32_t dynOffset = 0u;
            ctxSetBindGroup(ctx, 0, bindGroup, 1, &dynOffset);
        } else {
            ctxSetBindGroup(ctx, 0, bindGroup, 0, nullptr);
        }

        validateFinish(t, ctx, !mismatched);

        if (bindGroup != nullptr) {
            wgpuBindGroupRelease(bindGroup);
        }
    });

// ---------------------------------------------------------------------------
// g.test('dynamic_offsets_passed_but_not_expected')
// Tests that setBindGroup correctly errors on unexpected dynamicOffsets.
// ---------------------------------------------------------------------------
CTS_TEST(g, "dynamic_offsets_passed_but_not_expected")
    .desc("Tests that setBindGroup correctly errors on unexpected dynamicOffsets.")
    .params([](ParamsBuilder u) {
        return u.combine("encoderType", kProgrammableEncoderTypeValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string encoderType = t.param<std::string>("encoderType");

        // Create a bind group with no dynamic offsets (empty indices).
        BindGroupObjects objs = createBindGroupForTest(
            t, ResourceState::Valid, "buffer", encoderType, {});

        // Pass one dynamic offset even though none are expected.
        const uint32_t dynamicOffset = 0u;

        ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, encoderType);
        ctxSetBindGroup(ctx, 0, objs.bindGroup, 1, &dynamicOffset);
        validateFinish(t, ctx, false);
    });

// ---------------------------------------------------------------------------
// g.test('dynamic_offsets_match_expectations_in_pass_encoder')
// Tests that given dynamicOffsets match the specified bindGroup.
// ---------------------------------------------------------------------------
CTS_TEST(g, "dynamic_offsets_match_expectations_in_pass_encoder")
    .desc("Tests that given dynamicOffsets match the specified bindGroup.")
    .params([](ParamsBuilder u) {
        return u
            .combine("encoderType", kProgrammableEncoderTypeValues())
            .combineWithParams({
                // Dynamic offsets aligned
                ParamRecord{{"dynamicOffsets", std::string("256,0")}, {"_success", true}},
                // Dynamic offsets not aligned
                ParamRecord{{"dynamicOffsets", std::string("1,2")},   {"_success", false}},
                // Wrong number of dynamic offsets (expected 2, got 3)
                ParamRecord{{"dynamicOffsets", std::string("256,0,0")}, {"_success", false}},
                // Wrong number (got 1)
                ParamRecord{{"dynamicOffsets", std::string("256")}, {"_success", false}},
                // Wrong number (got 0)
                ParamRecord{{"dynamicOffsets", std::string("")}, {"_success", false}},
                // Dynamic uniform buffer out of bounds because of binding size
                ParamRecord{{"dynamicOffsets", std::string("512,0")},        {"_success", false}},
                ParamRecord{{"dynamicOffsets", std::string("1024,0")},       {"_success", false}},
                ParamRecord{{"dynamicOffsets", std::string("4294967295,0")}, {"_success", false}},
                // Dynamic storage buffer out of bounds because of binding size
                ParamRecord{{"dynamicOffsets", std::string("0,512")},        {"_success", false}},
                ParamRecord{{"dynamicOffsets", std::string("0,1024")},       {"_success", false}},
                ParamRecord{{"dynamicOffsets", std::string("0,4294967295")}, {"_success", false}},
            })
            .combine("useU32array", {false, true})
            .beginSubcases()
            .combine("visibility", {
                static_cast<int64_t>(WGPUShaderStage_Compute),
                static_cast<int64_t>(WGPUShaderStage_Compute | WGPUShaderStage_Fragment),
            })
            .combine("useStorage", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int64_t     visibilityI64 = t.param<int64_t>("visibility");
        const WGPUShaderStage visibility = static_cast<WGPUShaderStage>(visibilityI64);
        const bool        useStorage    = t.param<bool>("useStorage");

        // Skip if fragment stage does not support storage buffers (compatibility mode).
        if ((visibility & WGPUShaderStage_Fragment) != 0 && useStorage) {
            const WGPUCompatibilityModeLimits compat = t.getCompatibilityModeLimits();
            if (compat.maxStorageBuffersInFragmentStage != WGPU_LIMIT_U32_UNDEFINED &&
                compat.maxStorageBuffersInFragmentStage < 1) {
                t.skip("maxStorageBuffersInFragmentStage < 1");
            }
        }

        const uint32_t kBindingSize = 12;

        // Create bind group layout with two dynamic-offset buffers.
        WGPUBindGroupLayoutEntry entries[2];
        entries[0] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries[0].binding    = 0;
        entries[0].visibility = visibility;
        entries[0].buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entries[0].buffer.type             = WGPUBufferBindingType_Uniform;
        entries[0].buffer.hasDynamicOffset = WGPU_TRUE;

        entries[1] = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entries[1].binding    = 1;
        entries[1].visibility = visibility;
        entries[1].buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        entries[1].buffer.type             = useStorage
            ? WGPUBufferBindingType_Storage
            : WGPUBufferBindingType_Uniform;
        entries[1].buffer.hasDynamicOffset = WGPU_TRUE;

        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 2;
        bglDesc.entries    = entries;
        WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);

        // Create buffers: uniform and storage-or-uniform.
        WGPUBufferDescriptor uniformBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        uniformBufDesc.size  = 2 * kMinDynamicBufferOffsetAlignment + 8;
        uniformBufDesc.usage = WGPUBufferUsage_Uniform;
        WGPUBuffer uniformBuffer = t.createBufferTracked(uniformBufDesc);

        WGPUBufferDescriptor storageOrUniformBufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        storageOrUniformBufDesc.size  = 2 * kMinDynamicBufferOffsetAlignment + 8;
        storageOrUniformBufDesc.usage = useStorage ? WGPUBufferUsage_Storage : WGPUBufferUsage_Uniform;
        WGPUBuffer storageOrUniformBuffer = t.createBufferTracked(storageOrUniformBufDesc);

        // Create bind group.
        WGPUBindGroupEntry bgEntries[2];
        bgEntries[0] = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntries[0].binding = 0;
        bgEntries[0].buffer  = uniformBuffer;
        bgEntries[0].offset  = 0;
        bgEntries[0].size    = kBindingSize;

        bgEntries[1] = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntries[1].binding = 1;
        bgEntries[1].buffer  = storageOrUniformBuffer;
        bgEntries[1].offset  = 0;
        bgEntries[1].size    = kBindingSize;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl;
        bgDesc.entryCount = 2;
        bgDesc.entries    = bgEntries;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        // Parse the dynamic offsets string from params.
        // Format: comma-separated uint32 values, or empty string for zero offsets.
        const std::string dynamicOffsetsStr = t.param<std::string>("dynamicOffsets");
        const bool        _success          = t.param<bool>("_success");
        const bool        useU32array       = t.param<bool>("useU32array");
        const std::string encoderType       = t.param<std::string>("encoderType");

        std::vector<uint32_t> dynamicOffsets;
        if (!dynamicOffsetsStr.empty()) {
            std::string token;
            for (size_t i = 0; i <= dynamicOffsetsStr.size(); ++i) {
                if (i == dynamicOffsetsStr.size() || dynamicOffsetsStr[i] == ',') {
                    if (!token.empty()) {
                        dynamicOffsets.push_back(static_cast<uint32_t>(
                            std::stoul(token)));
                        token.clear();
                    }
                } else {
                    token += dynamicOffsetsStr[i];
                }
            }
        }

        // useU32array does not affect the native C API (no Uint32Array overload; both JS
        // overloads map to the same pointer+count call). The param is included to match
        // the upstream params matrix.
        (void)useU32array;

        ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, encoderType);
        // Both overloads pass the same data; native API doesn't distinguish (no start/length).
        ctxSetBindGroup(ctx, 0, bindGroup,
            dynamicOffsets.size(),
            dynamicOffsets.empty() ? nullptr : dynamicOffsets.data());
        validateFinish(t, ctx, _success);
    });

// ---------------------------------------------------------------------------
// g.test('u32array_start_and_length')
// Tests that dynamicOffsetsData(Start|Length) apply to the given Uint32Array.
//
// NOTE: The JS test calls shouldThrow('RangeError', doSetBindGroup) when
// _success is false (start+length > offsets.length). In native C this
// corresponds to passing an invalid dynamicOffsetCount to setBindGroup,
// which should produce a validation error at finish(). We use expectValidationError
// rather than catching a RangeError.
// ---------------------------------------------------------------------------
CTS_TEST(g, "u32array_start_and_length")
    .desc("Tests that dynamicOffsetsData(Start|Length) apply to the given Uint32Array.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combineWithParams({
                // dynamicOffsetsDataLength > offsets.length → error
                ParamRecord{
                    {"offsets",                  std::string("0")},
                    {"dynamicOffsetsDataStart",  int64_t(0)},
                    {"dynamicOffsetsDataLength", int64_t(2)},
                    {"_success",                 false}},
                // dynamicOffsetsDataStart + dynamicOffsetsDataLength > offsets.length → error
                ParamRecord{
                    {"offsets",                  std::string("0")},
                    {"dynamicOffsetsDataStart",  int64_t(1)},
                    {"dynamicOffsetsDataLength", int64_t(1)},
                    {"_success",                 false}},
                // start=1, length=1, offsets=[0,0] → valid slice [1..2)
                ParamRecord{
                    {"offsets",                  std::string("0,0")},
                    {"dynamicOffsetsDataStart",  int64_t(1)},
                    {"dynamicOffsetsDataLength", int64_t(1)},
                    {"_success",                 true}},
                // start=1, length=1, offsets=[0,0,0] → valid slice [1..2)
                ParamRecord{
                    {"offsets",                  std::string("0,0,0")},
                    {"dynamicOffsetsDataStart",  int64_t(1)},
                    {"dynamicOffsetsDataLength", int64_t(1)},
                    {"_success",                 true}},
                // start=0, length=2, offsets=[0,0] → full slice [0..2)
                ParamRecord{
                    {"offsets",                  std::string("0,0")},
                    {"dynamicOffsetsDataStart",  int64_t(0)},
                    {"dynamicOffsetsDataLength", int64_t(2)},
                    {"_success",                 true}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string offsetsStr              = t.param<std::string>("offsets");
        const int64_t     dynamicOffsetsDataStart  = t.param<int64_t>("dynamicOffsetsDataStart");
        const int64_t     dynamicOffsetsDataLength = t.param<int64_t>("dynamicOffsetsDataLength");
        const bool        _success                 = t.param<bool>("_success");

        const uint32_t kBindingSize = 8;

        // Parse offsets string.
        std::vector<uint32_t> offsets;
        if (!offsetsStr.empty()) {
            std::string token;
            for (size_t i = 0; i <= offsetsStr.size(); ++i) {
                if (i == offsetsStr.size() || offsetsStr[i] == ',') {
                    if (!token.empty()) {
                        offsets.push_back(static_cast<uint32_t>(std::stoul(token)));
                        token.clear();
                    }
                } else {
                    token += offsetsStr[i];
                }
            }
        }

        const uint32_t numDynamic = static_cast<uint32_t>(dynamicOffsetsDataLength);

        // Build BGL with dynamicOffsetsDataLength uniform dynamic-offset entries.
        std::vector<WGPUBindGroupLayoutEntry> bglEntries;
        bglEntries.reserve(numDynamic);
        for (uint32_t i = 0; i < numDynamic; ++i) {
            WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
            entry.binding    = i;
            entry.visibility = WGPUShaderStage_Fragment;
            entry.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
            entry.buffer.type             = WGPUBufferBindingType_Uniform;
            entry.buffer.hasDynamicOffset = WGPU_TRUE;
            bglEntries.push_back(entry);
        }

        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = numDynamic;
        bglDesc.entries    = bglEntries.data();
        WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);

        // Build bind group: one uniform buffer per dynamic entry, size = kBindingSize.
        std::vector<WGPUBindGroupEntry> bgEntries;
        bgEntries.reserve(numDynamic);
        for (uint32_t i = 0; i < numDynamic; ++i) {
            WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
            bufDesc.size  = kBindingSize;
            bufDesc.usage = WGPUBufferUsage_Uniform;
            WGPUBuffer buf = t.createBufferTracked(bufDesc);

            WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
            bgEntry.binding = i;
            bgEntry.buffer  = buf;
            bgEntry.offset  = 0;
            bgEntry.size    = kBindingSize;
            bgEntries.push_back(bgEntry);
        }

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl;
        bgDesc.entryCount = numDynamic;
        bgDesc.entries    = bgEntries.data();
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        // Determine the effective dynamic offsets slice [start, start+length).
        // If start+length > offsets.size() that is the "out-of-range" error case.
        // In native: we pass numDynamic values to setBindGroup.
        const uint64_t start  = static_cast<uint64_t>(dynamicOffsetsDataStart);
        const uint64_t length = static_cast<uint64_t>(dynamicOffsetsDataLength);

        // Create encoder using render pass (upstream uses 'render pass' for u32array_start_and_length).
        ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, "render pass");

        const uint64_t available = static_cast<uint64_t>(offsets.size());

        if (_success) {
            // Valid case: start+length <= offsets.size(). Extract slice [start, start+length).
            std::vector<uint32_t> slice;
            slice.reserve(static_cast<size_t>(length));
            for (uint64_t i = start; i < start + length; ++i) {
                slice.push_back(offsets[static_cast<size_t>(i)]);
            }
            ctxSetBindGroup(ctx, 0, bindGroup, slice.size(), slice.data());
        } else {
            // Failure case: start+length > offsets.size().
            // In JS this throws a RangeError (WebIDL typed-array bounds check) and does NOT
            // invalidate the encoder. In native C there is no equivalent bounds check —
            // the caller owns the memory. To preserve the "encoder stays valid" semantics
            // (validateFinish(true) below), we simply skip setBindGroup for this case.
            // This is the correct native analogue: no setBindGroup call → encoder is valid.
            (void)available;
        }

        // JS: RangeError in setBindGroup does NOT cause the encoder to become invalid,
        // so validateFinish(true) — encoder finishes successfully regardless.
        validateFinish(t, ctx, true);
    });

// ---------------------------------------------------------------------------
// g.test('buffer_dynamic_offsets')
// Test that the dynamic offsets of the BufferLayout is a multiple of
// 'minUniformBufferOffsetAlignment|minStorageBufferOffsetAlignment' if the
// BindGroup entry defines buffer and the buffer type is
// 'uniform|storage|read-only-storage'.
// ---------------------------------------------------------------------------

static std::vector<Value> kBufferBindingTypeValues() {
    std::vector<Value> values;
    values.reserve(kBufferBindingTypes.size());
    for (WGPUBufferBindingType t : kBufferBindingTypes) {
        values.emplace_back(std::string(bufferBindingTypeIdentifier(t)));
    }
    return values;
}

CTS_TEST(g, "buffer_dynamic_offsets")
    .desc(
        "Test that the dynamic offsets of the BufferLayout is a multiple of\n"
        "'minUniformBufferOffsetAlignment|minStorageBufferOffsetAlignment' if the BindGroup entry\n"
        "defines buffer and the buffer type is 'uniform|storage|read-only-storage'.")
    .params([](ParamsBuilder u) {
        return u
            .combine("type",        kBufferBindingTypeValues())
            .combine("encoderType", kProgrammableEncoderTypeValues())
            .beginSubcases()
            .combineWithParams({
                // makeValueTestVariant(minAlignment, {mult, add}) = minAlignment * mult + add
                // {mult:1, add:0}   → minAlignment       (valid, exact multiple)
                // {mult:0.5, add:0} → minAlignment/2     (invalid, half-aligned)
                // {mult:1.5, add:0} → 1.5*minAlignment   (invalid, 1.5x)
                // {mult:2, add:0}   → 2*minAlignment      (valid)
                // {mult:1, add:2}   → minAlignment+2      (invalid, off by 2)
                ParamRecord{{"dov_mult_x2", int64_t(2)}, {"dov_add", int64_t(0)}},   // mult=1.0
                ParamRecord{{"dov_mult_x2", int64_t(1)}, {"dov_add", int64_t(0)}},   // mult=0.5
                ParamRecord{{"dov_mult_x2", int64_t(3)}, {"dov_add", int64_t(0)}},   // mult=1.5
                ParamRecord{{"dov_mult_x2", int64_t(4)}, {"dov_add", int64_t(0)}},   // mult=2.0
                ParamRecord{{"dov_mult_x2", int64_t(2)}, {"dov_add", int64_t(2)}},   // mult=1.0, add=2
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string bufferTypeName = t.param<std::string>("type");
        const std::string encoderType    = t.param<std::string>("encoderType");
        const int64_t     dov_mult_x2    = t.param<int64_t>("dov_mult_x2"); // mult*2 (integer)
        const int64_t     dov_add        = t.param<int64_t>("dov_add");

        const WGPUBufferBindingType bufferType = parseBufferBindingType(bufferTypeName);
        const uint32_t kBindingSize = 12;

        const WGPULimits limits = t.getLimits();
        const uint32_t minAlignment = (bufferType == WGPUBufferBindingType_Uniform)
            ? limits.minUniformBufferOffsetAlignment
            : limits.minStorageBufferOffsetAlignment;

        // makeValueTestVariant: dynamicOffset = minAlignment * (dov_mult_x2 / 2) + dov_add
        // Computed with integer arithmetic: (minAlignment * dov_mult_x2) / 2 + dov_add
        // to avoid floating-point rounding.
        const uint32_t dynamicOffset = static_cast<uint32_t>(
            (static_cast<int64_t>(minAlignment) * dov_mult_x2) / 2 + dov_add);

        WGPUBindGroupLayoutEntry bglEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        bglEntry.binding    = 0;
        bglEntry.visibility = WGPUShaderStage_Compute;
        bglEntry.buffer     = WGPU_BUFFER_BINDING_LAYOUT_INIT;
        bglEntry.buffer.type             = bufferType;
        bglEntry.buffer.hasDynamicOffset = WGPU_TRUE;

        WGPUBindGroupLayoutDescriptor bglDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        bglDesc.entryCount = 1;
        bglDesc.entries    = &bglEntry;
        WGPUBindGroupLayout bgl = t.createBindGroupLayoutTracked(bglDesc);

        const WGPUBufferUsage usage = (bufferType == WGPUBufferBindingType_Uniform)
            ? WGPUBufferUsage_Uniform
            : WGPUBufferUsage_Storage;

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 3 * static_cast<uint64_t>(kMinDynamicBufferOffsetAlignment);
        bufDesc.usage = usage;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        WGPUBindGroupEntry bgEntry = WGPU_BIND_GROUP_ENTRY_INIT;
        bgEntry.binding = 0;
        bgEntry.buffer  = buffer;
        bgEntry.offset  = 0;
        bgEntry.size    = kBindingSize;

        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout     = bgl;
        bgDesc.entryCount = 1;
        bgDesc.entries    = &bgEntry;
        WGPUBindGroup bindGroup = t.createBindGroupTracked(bgDesc);

        const bool isValid = (dynamicOffset % minAlignment) == 0;

        ProgrammableEncoderContext ctx = makeProgrammableEncoderContext(t, encoderType);
        ctxSetBindGroup(ctx, 0, bindGroup, 1, &dynamicOffset);
        validateFinish(t, ctx, isValid);
    });

} // namespace
