// Ported from gpuweb/cts src/webgpu/api/validation/encoding/encoder_state.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <string>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Helpers mirroring the upstream F helper class
// ---------------------------------------------------------------------------

// beginRenderPass: mirrors F.beginRenderPass(commandEncoder, view).
static WGPURenderPassEncoder beginRenderPass(
    AllFeaturesMaxLimitsGpuTest& /*t*/,
    WGPUCommandEncoder encoder,
    WGPUTextureView view)
{
    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view       = view;
    colorAttachment.loadOp     = WGPULoadOp_Clear;
    colorAttachment.storeOp    = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor desc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    desc.colorAttachmentCount = 1;
    desc.colorAttachments     = &colorAttachment;

    return wgpuCommandEncoderBeginRenderPass(encoder, &desc);
}

// createAttachmentTextureView: mirrors F.createAttachmentTextureView().
static WGPUTextureView createAttachmentTextureView(AllFeaturesMaxLimitsGpuTest& t) {
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.format            = WGPUTextureFormat_RGBA8Unorm;
    texDesc.size              = WGPUExtent3D{1, 1, 1};
    texDesc.usage             = WGPUTextureUsage_RenderAttachment;
    texDesc.dimension         = WGPUTextureDimension_2D;
    texDesc.mipLevelCount     = 1;
    texDesc.sampleCount       = 1;

    WGPUTexture texture = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    return t.createViewTracked(texture, viewDesc);
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,encoder_state",
    "TODO: createCommandEncoder / pass-encoder state validation tests.");

// ---------------------------------------------------------------------------
// pass_end_invalid_order
// ---------------------------------------------------------------------------
CTS_TEST(g, "pass_end_invalid_order")
    .desc(
        "Test that beginning a {compute,render} pass before ending the previous "
        "{compute,render} pass causes an error.")
    .unimplemented("native eager-error model: beginning a pass while another is open errors "
                   "eagerly in the C API (vs JS's deferred model), so the valid/invalid subcase "
                   "split differs from upstream; needs dedicated per-subcase semantics work");

// ---------------------------------------------------------------------------
// call_after_successful_finish
// Test that encoding a command after a successful finish generates a
// validation error.
// ---------------------------------------------------------------------------
CTS_TEST(g, "call_after_successful_finish")
    .desc("Test that encoding command after a successful finish generates a validation error.")
    .params([](ParamsBuilder u) {
        return u
            .combine("callCmd", {
                std::string("beginComputePass"),
                std::string("beginRenderPass"),
                std::string("finishAndSubmitFirst"),
                std::string("finishAndSubmitSecond"),
                std::string("insertDebugMarker"),
            })
            .beginSubcases()
            .combine("prePassType", {std::string("compute"), std::string("render"), std::string("no-op")})
            .combine("IsEncoderFinished", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string callCmd          = t.param<std::string>("callCmd");
        const std::string prePassType      = t.param<std::string>("prePassType");
        const bool        IsEncoderFinished = t.param<bool>("IsEncoderFinished");

        WGPUTextureView    view    = createAttachmentTextureView(t);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        // Optionally run a pre-pass before finishing.
        if (prePassType != "no-op") {
            if (prePassType == "compute") {
                WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            } else {
                WGPURenderPassEncoder pass = beginRenderPass(t, encoder, view);
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }
        }

        // Optionally finish the encoder before the call-under-test.
        WGPUCommandBuffer buffer = nullptr;
        if (IsEncoderFinished) {
            // First finish: valid. Track the resulting command buffer.
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            buffer = wgpuCommandEncoderFinish(encoder, &cbDesc);
            // buffer is tracked implicitly (we'll release at the end if we submit).
        }

        if (callCmd == "beginComputePass") {
            WGPUComputePassEncoder pass = nullptr;
            t.expectValidationError([&] {
                pass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            }, IsEncoderFinished);
            t.expectValidationError([&] {
                wgpuComputePassEncoderEnd(pass);
            }, IsEncoderFinished);
            if (pass != nullptr) wgpuComputePassEncoderRelease(pass);
            if (buffer != nullptr) {
                WGPUCommandBuffer cmds[1] = {buffer};
                wgpuQueueSubmit(t.queue(), 1, cmds);
                wgpuCommandBufferRelease(buffer);
                buffer = nullptr;
            }
        } else if (callCmd == "beginRenderPass") {
            WGPURenderPassEncoder pass = nullptr;
            t.expectValidationError([&] {
                pass = beginRenderPass(t, encoder, view);
            }, IsEncoderFinished);
            t.expectValidationError([&] {
                wgpuRenderPassEncoderEnd(pass);
            }, IsEncoderFinished);
            if (pass != nullptr) wgpuRenderPassEncoderRelease(pass);
            if (buffer != nullptr) {
                WGPUCommandBuffer cmds[1] = {buffer};
                wgpuQueueSubmit(t.queue(), 1, cmds);
                wgpuCommandBufferRelease(buffer);
                buffer = nullptr;
            }
        } else if (callCmd == "finishAndSubmitFirst") {
            // Attempt to finish the encoder a second time (errors iff already finished).
            t.expectValidationError([&] {
                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
                if (cb != nullptr) {
                    wgpuCommandBufferRelease(cb);
                }
            }, IsEncoderFinished);
            if (buffer != nullptr) {
                WGPUCommandBuffer cmds[1] = {buffer};
                wgpuQueueSubmit(t.queue(), 1, cmds);
                wgpuCommandBufferRelease(buffer);
                buffer = nullptr;
            }
        } else if (callCmd == "finishAndSubmitSecond") {
            WGPUCommandBuffer secondBuffer = nullptr;
            t.expectValidationError([&] {
                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                secondBuffer = wgpuCommandEncoderFinish(encoder, &cbDesc);
            }, IsEncoderFinished);
            t.expectValidationError([&] {
                WGPUCommandBuffer cmds[1] = {secondBuffer};
                wgpuQueueSubmit(t.queue(), 1, cmds);
            }, IsEncoderFinished);
            if (secondBuffer != nullptr) {
                wgpuCommandBufferRelease(secondBuffer);
                secondBuffer = nullptr;
            }
            // Note: buffer (the first successful finish result, if IsEncoderFinished) is
            // not submitted in this branch; it will be released below.
            if (buffer != nullptr) {
                wgpuCommandBufferRelease(buffer);
                buffer = nullptr;
            }
        } else if (callCmd == "insertDebugMarker") {
            t.expectValidationError([&] {
                WGPUStringView label = WGPUStringView{"", 0};
                wgpuCommandEncoderInsertDebugMarker(encoder, label);
            }, IsEncoderFinished);
            if (buffer != nullptr) {
                WGPUCommandBuffer cmds[1] = {buffer};
                wgpuQueueSubmit(t.queue(), 1, cmds);
                wgpuCommandBufferRelease(buffer);
                buffer = nullptr;
            }
        }

        // If encoder was not finished and the callCmd is not a finish variant,
        // finish the encoder now (cleanup).
        if (!IsEncoderFinished && callCmd.rfind("finish", 0) != 0) {
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
            if (cb != nullptr) {
                wgpuCommandBufferRelease(cb);
            }
        }

        if (buffer != nullptr) {
            wgpuCommandBufferRelease(buffer);
        }
        // view is tracked by createViewTracked; no manual release needed.
    });

// ---------------------------------------------------------------------------
// pass_end_none
// Test that finishing an encoder without ending a child {compute,render} pass
// generates a validation error.
// ---------------------------------------------------------------------------
CTS_TEST(g, "pass_end_none")
    .desc(
        "Test that finishing an encoder without ending a child {compute,render} pass "
        "generates a validation error.")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combine("passType", {std::string("compute"), std::string("render")})
            .combine("endCount", {0, 1});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string passType = t.param<std::string>("passType");
        const int         endCount = t.param<int>("endCount");

        WGPUTextureView    view    = createAttachmentTextureView(t);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        WGPURenderPassEncoder  renderPass  = nullptr;
        WGPUComputePassEncoder computePass = nullptr;
        if (passType == "compute") {
            computePass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        } else {
            renderPass = beginRenderPass(t, encoder, view);
        }

        for (int i = 0; i < endCount; ++i) {
            if (passType == "compute") {
                wgpuComputePassEncoderEnd(computePass);
            } else {
                wgpuRenderPassEncoderEnd(renderPass);
            }
        }

        t.expectValidationError([&] {
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
            if (cb != nullptr) {
                wgpuCommandBufferRelease(cb);
            }
        }, endCount == 0);

        if (computePass != nullptr) wgpuComputePassEncoderRelease(computePass);
        if (renderPass  != nullptr) wgpuRenderPassEncoderRelease(renderPass);
        // view is tracked by createViewTracked; no manual release needed.
    });

// ---------------------------------------------------------------------------
// pass_end_twice,basic
// Test that ending a {compute,render} pass twice generates a validation error.
// The parent encoder (command encoder) can be either locked or open.
// ---------------------------------------------------------------------------
CTS_TEST(g, "pass_end_twice,basic")
    .desc(
        "Test that ending a {compute,render} pass twice generates a validation error. "
        "The parent encoder (command encoder) can be either locked or open.")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combine("passType", {std::string("compute"), std::string("render")})
            .combine("endTwice", {false, true})
            // false means "do not open another pass for the second end"
            .combine("secondEndInAnotherPass", {
                std::string("false"),
                std::string("compute"),
                std::string("render"),
            })
            .filter([](const ParamRecord& p) {
                const bool endTwice = valueAs<bool>(*findParam(p, "endTwice"));
                const std::string secondEndInAnotherPass =
                    valueAs<std::string>(*findParam(p, "secondEndInAnotherPass"));
                // Upstream filter: p.endTwice || !p.secondEndInAnotherPass
                // (where !p.secondEndInAnotherPass means the value is false/falsy)
                return endTwice || (secondEndInAnotherPass == "false");
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string passType               = t.param<std::string>("passType");
        const bool        endTwice               = t.param<bool>("endTwice");
        const std::string secondEndInAnotherPass = t.param<std::string>("secondEndInAnotherPass");

        WGPUTextureView    view    = createAttachmentTextureView(t);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        WGPURenderPassEncoder  renderPass  = nullptr;
        WGPUComputePassEncoder computePass = nullptr;
        if (passType == "compute") {
            computePass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        } else {
            renderPass = beginRenderPass(t, encoder, view);
        }

        // First end (always valid).
        if (passType == "compute") {
            wgpuComputePassEncoderEnd(computePass);
        } else {
            wgpuRenderPassEncoderEnd(renderPass);
        }

        if (secondEndInAnotherPass != "false") {
            // Open another pass, then do the second end (of the first pass) inside it.
            // The encoder is now locked (another pass is open), so the second end
            // should generate a validation error.
            WGPURenderPassEncoder  pass1Render  = nullptr;
            WGPUComputePassEncoder pass1Compute = nullptr;
            if (secondEndInAnotherPass == "compute") {
                pass1Compute = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
            } else {
                pass1Render = beginRenderPass(t, encoder, view);
            }

            // End the original pass a second time — should always error.
            t.expectValidationError([&] {
                if (passType == "compute") {
                    wgpuComputePassEncoderEnd(computePass);
                } else {
                    wgpuRenderPassEncoderEnd(renderPass);
                }
            }, true);

            // End the second pass.
            if (secondEndInAnotherPass == "compute") {
                wgpuComputePassEncoderEnd(pass1Compute);
            } else {
                wgpuRenderPassEncoderEnd(pass1Render);
            }

            if (pass1Compute != nullptr) wgpuComputePassEncoderRelease(pass1Compute);
            if (pass1Render  != nullptr) wgpuRenderPassEncoderRelease(pass1Render);
        } else {
            // No other pass open; optionally call end a second time.
            if (endTwice) {
                t.expectValidationError([&] {
                    if (passType == "compute") {
                        wgpuComputePassEncoderEnd(computePass);
                    } else {
                        wgpuRenderPassEncoderEnd(renderPass);
                    }
                }, true);
            }
        }

        // Finish the encoder (should succeed: the first pass ended correctly,
        // and the second end, if any, only generated an error that invalidates the pass).
        t.finishTracked(encoder);

        if (computePass != nullptr) wgpuComputePassEncoderRelease(computePass);
        if (renderPass  != nullptr) wgpuRenderPassEncoderRelease(renderPass);
        // view is tracked by createViewTracked; no manual release needed.
    });

// ---------------------------------------------------------------------------
// pass_end_twice,render_pass_invalid
// Test that ending a render pass twice generates a validation error even if
// the pass is invalid.
// ---------------------------------------------------------------------------
CTS_TEST(g, "pass_end_twice,render_pass_invalid")
    .desc(
        "Test that ending a render pass twice generates a validation error "
        "even if the pass is invalid.")
    .params([](ParamsBuilder u) {
        return u
            .beginSubcases()
            .combine("endTwice", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool endTwice = t.param<bool>("endTwice");

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        // Pass encoder creation will fail because both color and depth/stencil
        // attachments are empty (mirrors upstream: beginRenderPass({ colorAttachments: [] })).
        WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
        passDesc.colorAttachmentCount = 0;
        passDesc.colorAttachments     = nullptr;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        wgpuRenderPassEncoderEnd(pass);

        if (endTwice) {
            t.expectValidationError([&] {
                wgpuRenderPassEncoderEnd(pass);
            }, true);
        }

        // encoder.finish() should error (the invalid render pass invalidated the encoder).
        t.expectValidationError([&] {
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
            if (cb != nullptr) {
                wgpuCommandBufferRelease(cb);
            }
        }, true);

        if (pass != nullptr) wgpuRenderPassEncoderRelease(pass);
    });

// ---------------------------------------------------------------------------
// pass_begin_invalid_encoder
// Test that {compute,render} passes can still be opened on an invalid encoder.
// ---------------------------------------------------------------------------
CTS_TEST(g, "pass_begin_invalid_encoder")
    .desc(
        "Test that {compute,render} passes can still be opened on an invalid encoder.")
    .params([](ParamsBuilder u) {
        return u
            .combine("pass0Type", {std::string("compute"), std::string("render")})
            .combine("pass1Type", {std::string("compute"), std::string("render")})
            .beginSubcases()
            .combine("firstPassInvalid", {false, true});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string pass0Type        = t.param<std::string>("pass0Type");
        const std::string pass1Type        = t.param<std::string>("pass1Type");
        const bool        firstPassInvalid = t.param<bool>("firstPassInvalid");

        WGPUTextureView    view    = createAttachmentTextureView(t);
        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();

        WGPURenderPassEncoder  firstRenderPass  = nullptr;
        WGPUComputePassEncoder firstComputePass = nullptr;
        if (pass0Type == "compute") {
            firstComputePass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        } else {
            firstRenderPass = beginRenderPass(t, encoder, view);
        }

        if (firstPassInvalid) {
            // Popping an empty debug group stack invalidates the pass.
            if (pass0Type == "compute") {
                wgpuComputePassEncoderPopDebugGroup(firstComputePass);
            } else {
                wgpuRenderPassEncoderPopDebugGroup(firstRenderPass);
            }
        }

        // Ending an invalid pass invalidates the encoder.
        if (pass0Type == "compute") {
            wgpuComputePassEncoderEnd(firstComputePass);
        } else {
            wgpuRenderPassEncoderEnd(firstRenderPass);
        }

        // Passes can still be opened on an invalid encoder.
        WGPURenderPassEncoder  secondRenderPass  = nullptr;
        WGPUComputePassEncoder secondComputePass = nullptr;
        if (pass1Type == "compute") {
            secondComputePass = wgpuCommandEncoderBeginComputePass(encoder, nullptr);
        } else {
            secondRenderPass = beginRenderPass(t, encoder, view);
        }

        if (pass1Type == "compute") {
            wgpuComputePassEncoderEnd(secondComputePass);
        } else {
            wgpuRenderPassEncoderEnd(secondRenderPass);
        }

        t.expectValidationError([&] {
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer cb = wgpuCommandEncoderFinish(encoder, &cbDesc);
            if (cb != nullptr) {
                wgpuCommandBufferRelease(cb);
            }
        }, firstPassInvalid);

        if (firstComputePass  != nullptr) wgpuComputePassEncoderRelease(firstComputePass);
        if (firstRenderPass   != nullptr) wgpuRenderPassEncoderRelease(firstRenderPass);
        if (secondComputePass != nullptr) wgpuComputePassEncoderRelease(secondComputePass);
        if (secondRenderPass  != nullptr) wgpuRenderPassEncoderRelease(secondRenderPass);
        // view is tracked by createViewTracked; no manual release needed.
    });

} // namespace
