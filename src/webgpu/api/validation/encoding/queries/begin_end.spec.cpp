// Ported from gpuweb/cts src/webgpu/api/validation/encoding/queries/begin_end.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Helpers mirroring common.ts: createQuerySetWithType / beginRenderPassWithQuerySet
// ---------------------------------------------------------------------------

// createQuerySetWithType: creates a WGPUQuerySet with the given type and count.
// Caller must release the returned object.
static WGPUQuerySet createQuerySetWithType(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUQueryType type,
    uint32_t count)
{
    WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
    desc.type  = type;
    desc.count = count;
    return wgpuDeviceCreateQuerySet(t.device(), &desc);
}

// beginRenderPassWithQuerySet: begins a render pass using a 16x16 rgba8unorm
// color attachment texture and optionally an occlusionQuerySet.
// Caller must call wgpuRenderPassEncoderEnd / release on the returned encoder.
static WGPURenderPassEncoder beginRenderPassWithQuerySet(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUCommandEncoder encoder,
    WGPUQuerySet occlusionQuerySet)
{
    WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
    texDesc.format    = WGPUTextureFormat_RGBA8Unorm;
    texDesc.size      = WGPUExtent3D{16, 16, 1};
    texDesc.usage     = WGPUTextureUsage_RenderAttachment;
    texDesc.dimension = WGPUTextureDimension_2D;
    WGPUTexture tex = t.createTextureTracked(texDesc);

    WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
    WGPUTextureView view = t.createViewTracked(tex, viewDesc);

    WGPURenderPassColorAttachment colorAttachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
    colorAttachment.view       = view;
    colorAttachment.loadOp     = WGPULoadOp_Clear;
    colorAttachment.storeOp    = WGPUStoreOp_Store;
    colorAttachment.clearValue = WGPUColor{1.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments     = &colorAttachment;
    passDesc.occlusionQuerySet    = occlusionQuerySet;

    return wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,queries,begin_end",
    "Validation for encoding begin/endable queries.");

// ---------------------------------------------------------------------------
// occlusion_query,begin_end_balance
//
// Tests that begin/end occlusion queries mismatch on render pass:
// - begin n queries, then end m queries, for various n and m.
//
// Errors defer to encoder finish() (occlusion query begin/end are encoder
// commands; validation is deferred until finish).
// ---------------------------------------------------------------------------
CTS_TEST(g, "occlusion_query,begin_end_balance")
    .desc(
        "Tests that begin/end occlusion queries mismatch on render pass:\n"
        "- begin n queries, then end m queries, for various n and m.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combineWithParams({
            ParamRecord{{"begin", int64_t(0)}, {"end", int64_t(1)}},
            ParamRecord{{"begin", int64_t(1)}, {"end", int64_t(0)}},
            ParamRecord{{"begin", int64_t(1)}, {"end", int64_t(1)}}, // control case
            ParamRecord{{"begin", int64_t(1)}, {"end", int64_t(2)}},
            ParamRecord{{"begin", int64_t(2)}, {"end", int64_t(1)}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t begin = static_cast<uint32_t>(t.param<int64_t>("begin"));
        const uint32_t end   = static_cast<uint32_t>(t.param<int64_t>("end"));

        WGPUQuerySet occlusionQuerySet = createQuerySetWithType(t, WGPUQueryType_Occlusion, 2);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPassWithQuerySet(t, encoder, occlusionQuerySet);

        for (uint32_t i = 0; i < begin; ++i) {
            wgpuRenderPassEncoderBeginOcclusionQuery(pass, i);
        }
        for (uint32_t j = 0; j < end; ++j) {
            wgpuRenderPassEncoderEndOcclusionQuery(pass);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // Mismatch between begin and end counts is a finish-time error.
        const bool shouldError = (begin != end);
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, shouldError);

        wgpuQuerySetRelease(occlusionQuerySet);
    });

// ---------------------------------------------------------------------------
// occlusion_query,begin_end_invalid_nesting
//
// Tests the invalid nesting of begin/end occlusion queries:
// - begin index 0, end, begin index 0, end (control case - valid)
// - begin index 0, begin index 0, end, end  (invalid)
// - begin index 0, begin index 1, end, end  (invalid)
//
// Errors defer to encoder finish().
// ---------------------------------------------------------------------------
CTS_TEST(g, "occlusion_query,begin_end_invalid_nesting")
    .desc(
        "Tests the invalid nesting of begin/end occlusion queries:\n"
        "- begin index 0, end, begin index 0, end (control case)\n"
        "- begin index 0, begin index 0, end, end\n"
        "- begin index 0, begin index 1, end, end")
    .params([](ParamsBuilder u) {
        // Each call sequence is encoded as a vector of integers where -1 means "end".
        return u.beginSubcases().combineWithParams({
            // calls: [0, end, 1, end] — valid (control case)
            ParamRecord{{"calls", std::string("0,end,1,end")}, {"_valid", true}},
            // calls: [0, 0, end, end] — invalid nesting
            ParamRecord{{"calls", std::string("0,0,end,end")}, {"_valid", false}},
            // calls: [0, 1, end, end] — invalid nesting
            ParamRecord{{"calls", std::string("0,1,end,end")}, {"_valid", false}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string callsStr = t.param<std::string>("calls");
        const bool        valid    = t.param<bool>("_valid");

        WGPUQuerySet occlusionQuerySet = createQuerySetWithType(t, WGPUQueryType_Occlusion, 2);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPassWithQuerySet(t, encoder, occlusionQuerySet);

        // Parse call sequence: comma-separated tokens, "end" means endOcclusionQuery,
        // otherwise an integer index.
        std::vector<std::string> tokens;
        {
            std::string s = callsStr;
            size_t pos = 0;
            while (pos < s.size()) {
                size_t comma = s.find(',', pos);
                if (comma == std::string::npos) {
                    tokens.push_back(s.substr(pos));
                    break;
                }
                tokens.push_back(s.substr(pos, comma - pos));
                pos = comma + 1;
            }
        }
        for (const std::string& tok : tokens) {
            if (tok == "end") {
                wgpuRenderPassEncoderEndOcclusionQuery(pass);
            } else {
                uint32_t index = static_cast<uint32_t>(std::stoi(tok));
                wgpuRenderPassEncoderBeginOcclusionQuery(pass, index);
            }
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // Validation is deferred to finish().
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, !valid);

        wgpuQuerySetRelease(occlusionQuerySet);
    });

// ---------------------------------------------------------------------------
// occlusion_query,disjoint_queries_with_same_query_index
//
// Tests that two disjoint occlusion queries cannot be begun with the same
// query index on the same render pass; using different render passes is valid.
//
// Errors defer to encoder finish().
// ---------------------------------------------------------------------------
CTS_TEST(g, "occlusion_query,disjoint_queries_with_same_query_index")
    .desc(
        "Tests that two disjoint occlusion queries cannot be begun with same query index on same render pass:\n"
        "- begin index 0, end, begin index 0, end\n"
        "- call on {same (invalid), different (control case)} render pass")
    .params([](ParamsBuilder u) {
        return u.beginSubcases().combine("isOnSameRenderPass", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool isOnSameRenderPass = t.param<bool>("isOnSameRenderPass");

        WGPUQuerySet querySet = createQuerySetWithType(t, WGPUQueryType_Occlusion, 1);

        WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
        WGPURenderPassEncoder pass = beginRenderPassWithQuerySet(t, encoder, querySet);

        wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
        wgpuRenderPassEncoderEndOcclusionQuery(pass);

        if (isOnSameRenderPass) {
            // Re-use query index 0 on the same render pass — invalid.
            wgpuRenderPassEncoderBeginOcclusionQuery(pass, 0);
            wgpuRenderPassEncoderEndOcclusionQuery(pass);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
        } else {
            // Use a second render pass with the same query index — valid.
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);

            WGPURenderPassEncoder otherPass = beginRenderPassWithQuerySet(t, encoder, querySet);
            wgpuRenderPassEncoderBeginOcclusionQuery(otherPass, 0);
            wgpuRenderPassEncoderEndOcclusionQuery(otherPass);
            wgpuRenderPassEncoderEnd(otherPass);
            wgpuRenderPassEncoderRelease(otherPass);
        }

        // Validation is deferred to finish().
        t.expectValidationError([&] {
            t.finishTracked(encoder);
        }, isOnSameRenderPass);

        wgpuQuerySetRelease(querySet);
    });

// ---------------------------------------------------------------------------
// nesting
//
// Tests that whether it's allowed to nest various types of queries:
// - call {occlusion, timestamp} query in same type or other type.
//
// NOTE: This test is marked .unimplemented() in the upstream TypeScript source
// and has no .fn() body there either. Matching upstream behaviour exactly.
// ---------------------------------------------------------------------------
CTS_TEST(g, "nesting")
    .desc(
        "Tests that whether it's allowed to nest various types of queries:\n"
        "- call {occlusion, timestamp} query in same type or other type.")
    .unimplemented("upstream test is unimplemented (.unimplemented() in the TypeScript source)");

} // namespace
