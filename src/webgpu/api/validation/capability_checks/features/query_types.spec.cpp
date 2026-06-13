// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/features/query_types.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause

#include <string>

#include "cts/test.h"
#include "feature_test_helpers.h"

using namespace cts;
using namespace cts::capability_features;

namespace {

TestGroup<FeatureGpuTest> testGroup = MakeTestGroup<FeatureGpuTest>(
    "api,validation,capability_checks,features,query_types",
    "Tests for capability checking for features enabling optional query types.");

CTS_TEST(testGroup, "createQuerySet")
    .desc("Tests that timestamp query set creation is gated by timestamp-query.")
    .params([](ParamsBuilder u) {
        return u.combine("type", {std::string("occlusion"), std::string("timestamp")})
            .combine("featureContainsTimestampQuery", {false, true});
    })
    .fn([](FeatureGpuTest& t) {
        const std::string type = t.param<std::string>("type");
        const bool feature = t.param<bool>("featureContainsTimestampQuery");
        if (feature) {
            t.selectDeviceOrSkipTestCase({WGPUFeatureName_TimestampQuery});
        } else {
            t.selectDeviceOrSkipTestCase({});
        }

        WGPUQuerySetDescriptor desc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        desc.type = type == "timestamp" ? WGPUQueryType_Timestamp : WGPUQueryType_Occlusion;
        desc.count = 1;
        const bool shouldError = type == "timestamp" && !feature;
        expectValidationError(t, [&] {
            WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &desc);
            if (querySet != nullptr) {
                wgpuQuerySetRelease(querySet);
            }
        }, shouldError);
    });

CTS_TEST(testGroup, "timestamp")
    .desc("Tests that writing timestamps is gated by timestamp-query.")
    .params([](ParamsBuilder u) {
        return u.combine("featureContainsTimestampQuery", {false, true});
    })
    .fn([](FeatureGpuTest& t) {
        const bool feature = t.param<bool>("featureContainsTimestampQuery");
        if (feature) {
            t.selectDeviceOrSkipTestCase({WGPUFeatureName_TimestampQuery});
        } else {
            t.selectDeviceOrSkipTestCase({});
        }

        WGPUQuerySetDescriptor queryDesc = WGPU_QUERY_SET_DESCRIPTOR_INIT;
        queryDesc.type = feature ? WGPUQueryType_Timestamp : WGPUQueryType_Occlusion;
        queryDesc.count = 2;
        WGPUQuerySet querySet = wgpuDeviceCreateQuerySet(t.device(), &queryDesc);

        WGPUPassTimestampWrites writes = WGPU_PASS_TIMESTAMP_WRITES_INIT;
        writes.querySet = querySet;
        writes.beginningOfPassWriteIndex = 0;
        writes.endOfPassWriteIndex = 1;

        {
            WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
            passDesc.timestampWrites = &writes;
            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
            if (pass != nullptr) {
                wgpuComputePassEncoderEnd(pass);
                wgpuComputePassEncoderRelease(pass);
            }
            expectValidationError(t, [&] { t.finishTracked(encoder); }, !feature);
        }

        {
            WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            textureDesc.size = {16, 16, 1};
            textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
            textureDesc.usage = WGPUTextureUsage_RenderAttachment;
            WGPUTexture texture = t.createTextureTracked(textureDesc);
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPUTextureView view = t.createViewTracked(texture, viewDesc);

            WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            color.view = view;
            color.loadOp = WGPULoadOp_Clear;
            color.storeOp = WGPUStoreOp_Discard;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &color;
            passDesc.timestampWrites = &writes;

            WGPUCommandEncoder encoder = t.createCommandEncoderTracked();
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            if (pass != nullptr) {
                wgpuRenderPassEncoderEnd(pass);
                wgpuRenderPassEncoderRelease(pass);
            }
            expectValidationError(t, [&] { t.finishTracked(encoder); }, !feature);
        }

        if (querySet != nullptr) {
            wgpuQuerySetRelease(querySet);
        }
    });

} // namespace
