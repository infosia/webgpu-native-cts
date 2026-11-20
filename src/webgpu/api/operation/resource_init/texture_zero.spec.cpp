// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/api/operation/resource_init/texture_zero.spec.ts
// @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
//
// The TextureZeroInitTest fixture, kTestParams and the checkers live in check_texture/*.h
// (ports of upstream's check_texture/*.ts).

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/api/operation/resource_init/check_texture/by_copy.h"
#include "webgpu/api/operation/resource_init/check_texture/by_ds_test.h"
#include "webgpu/api/operation/resource_init/check_texture/by_sampling.h"
#include "webgpu/api/operation/resource_init/check_texture/texture_zero_init_test.h"

using namespace cts;
using namespace cts::texture_zero;

namespace {

TestGroup<TextureZeroInitTest> g = MakeTestGroup<TextureZeroInitTest>(
    "api,operation,resource_init,texture_zero",
    R"(
Test uninitialized textures are initialized to zero when read.

TODO:
- test by sampling depth/stencil [1]
- test by copying out of stencil [2]
- test compressed texture formats [3]
)");

void checkContentsImpl(TextureZeroInitTest& t,
                       WGPUTexture texture,
                       InitializedState state,
                       const SubresourceRange& subresourceRange) {
    switch (t.p.readMethod) {
        case ReadMethod::Sample:
            checkContentsBySampling(t, texture, state, subresourceRange);
            return;
        case ReadMethod::CopyToBuffer:
            checkContentsByBufferCopy(t, texture, state, subresourceRange);
            return;
        case ReadMethod::CopyToTexture:
            checkContentsByTextureCopy(t, texture, state, subresourceRange);
            return;
        case ReadMethod::DepthTest:
            checkContentsByDepthTest(t, texture, state, subresourceRange);
            return;
        case ReadMethod::StencilTest:
            checkContentsByStencilTest(t, texture, state, subresourceRange);
            return;
        case ReadMethod::ColorBlending:
        case ReadMethod::Storage:
            t.skip("Not implemented");
    }
}

CTS_TEST(g, "uninitialized_texture_is_zero")
    .params([](ParamsBuilder u) { return makeTestParams(u); })
    .fn([](TextureZeroInitTest& t) {
        t.loadParams();
        const TextureZeroParams& params = t.p;
        t.skipIfTextureFormatNotSupportedForTest();
        t.skipIfTextureFormatAndDimensionNotCompatible(params.format, params.dimension);

        const WGPUTextureUsage usage = getRequiredTextureUsage(params.format, params.sampleCount,
                                                               params.uninitializeMethod, params.readMethod);

        WGPUTextureDescriptor desc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        desc.size = WGPUExtent3D{t.textureWidth(), t.textureHeight(), t.textureDepthOrArrayLayers()};
        desc.format = params.format;
        desc.dimension = params.dimension;
        desc.usage = usage;
        desc.mipLevelCount = params.mipLevelCount;
        desc.sampleCount = params.sampleCount;
        WGPUTexture texture = t.createTextureTracked(desc);

        if (params.canaryOnCreation) {
            // Initialize some subresources with canary values
            for (const SubresourceRange& subresourceRange : t.iterateInitializedSubresources()) {
                t.initializeTexture(texture, InitializedState::Canary, subresourceRange);
            }
        }

        switch (params.uninitializeMethod) {
            case UninitializeMethod::Creation:
                break;
            case UninitializeMethod::StoreOpClear:
                // Initialize the rest of the resources.
                for (const SubresourceRange& subresourceRange : t.iterateUninitializedSubresources()) {
                    t.initializeTexture(texture, InitializedState::Canary, subresourceRange);
                }
                // Then use a store op to discard their contents.
                for (const SubresourceRange& subresourceRange : t.iterateUninitializedSubresources()) {
                    t.discardTexture(texture, subresourceRange);
                }
                break;
        }

        // Check that all uninitialized resources are zero.
        for (const SubresourceRange& subresourceRange : t.iterateUninitializedSubresources()) {
            checkContentsImpl(t, texture, InitializedState::Zero, subresourceRange);
        }

        if (params.canaryOnCreation) {
            // Check the all other resources are unchanged.
            for (const SubresourceRange& subresourceRange : t.iterateInitializedSubresources()) {
                checkContentsImpl(t, texture, InitializedState::Canary, subresourceRange);
            }
        }
    });

}  // namespace
