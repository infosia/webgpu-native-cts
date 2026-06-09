// Ported from gpuweb/cts src/webgpu/api/validation/image_copy/buffer_related.spec.ts
// @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/texture_layout.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Test group — upstream fixture is ImageCopyTest which extends
// AllFeaturesMaxLimitsGPUTest.
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,image_copy,buffer_related",
    "Validation tests for buffer related parameters for buffer <-> texture copies");

// ---------------------------------------------------------------------------
// Helper: testBuffer
//
// Mirrors ImageCopyTest::testBuffer() from image_copy.ts.
//
// Runs one copy operation (CopyB2T or CopyT2B) using the provided buffer and
// texture, expecting success or failure at finish() or submit() time.
//
// method:
//   "CopyB2T" — copyBufferToTexture encoder command
//   "CopyT2B" — copyTextureToBuffer encoder command
//
// submit:
//   false — expect the validation error (if any) at encoder.finish()
//   true  — encoding should succeed; expect the error (if any) at queue.submit()
//
// Notes on the native error model:
//   copyBufferToTexture / copyTextureToBuffer are ENCODER COMMANDS; their
//   validation errors DEFER to finish().  When submit==true the finish() must
//   succeed (no error scope there) and the error propagates to submit().
// ---------------------------------------------------------------------------
static void testBuffer(
    AllFeaturesMaxLimitsGpuTest& t,
    WGPUBuffer buffer,
    WGPUTexture texture,
    uint32_t bytesPerRow,      // WGPU_COPY_STRIDE_UNDEFINED when not specified
    WGPUExtent3D copySize,
    const std::string& method,
    bool success,
    bool submit)
{
    // Build the texel-copy-texture info (the texture side, no buffer here).
    WGPUTexelCopyTextureInfo texInfo = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    texInfo.texture  = texture;
    texInfo.mipLevel = 0;
    texInfo.origin   = WGPUOrigin3D{0, 0, 0};
    texInfo.aspect   = WGPUTextureAspect_All;

    // Build the texel-copy-buffer layout (the buffer side).
    WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
    layout.offset       = 0;
    layout.bytesPerRow  = bytesPerRow;
    layout.rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;

    if (method == "CopyB2T") {
        // Build the source (buffer side) descriptor — buffer is the test subject.
        WGPUTexelCopyBufferInfo srcInfo = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        srcInfo.buffer              = buffer;
        srcInfo.layout.offset       = layout.offset;
        srcInfo.layout.bytesPerRow  = layout.bytesPerRow;
        srcInfo.layout.rowsPerImage = layout.rowsPerImage;

        WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);

        // copyBufferToTexture is an encoder command — error defers to finish().
        wgpuCommandEncoderCopyBufferToTexture(encoder, &srcInfo, &texInfo, &copySize);

        if (submit) {
            // Encoding must succeed; error expected at submit time.
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
            t.expectValidationError([&] {
                WGPUCommandBuffer cmds[1] = { cmd };
                wgpuQueueSubmit(t.queue(), 1, cmds);
            }, !success);
            if (cmd) { wgpuCommandBufferRelease(cmd); }
        } else {
            // Error expected at finish().
            t.expectValidationError([&] {
                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
                if (cmd) { wgpuCommandBufferRelease(cmd); }
            }, !success);
        }

        wgpuCommandEncoderRelease(encoder);

    } else { // CopyT2B
        // Build the destination (buffer side) descriptor — buffer is the test subject.
        WGPUTexelCopyBufferInfo dstInfo = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
        dstInfo.buffer              = buffer;
        dstInfo.layout.offset       = layout.offset;
        dstInfo.layout.bytesPerRow  = layout.bytesPerRow;
        dstInfo.layout.rowsPerImage = layout.rowsPerImage;

        WGPUCommandEncoderDescriptor encDesc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(t.device(), &encDesc);

        // copyTextureToBuffer is an encoder command — error defers to finish().
        wgpuCommandEncoderCopyTextureToBuffer(encoder, &texInfo, &dstInfo, &copySize);

        if (submit) {
            // Encoding must succeed; error expected at submit time.
            WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
            WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
            t.expectValidationError([&] {
                WGPUCommandBuffer cmds[1] = { cmd };
                wgpuQueueSubmit(t.queue(), 1, cmds);
            }, !success);
            if (cmd) { wgpuCommandBufferRelease(cmd); }
        } else {
            // Error expected at finish().
            t.expectValidationError([&] {
                WGPUCommandBufferDescriptor cbDesc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
                WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cbDesc);
                if (cmd) { wgpuCommandBufferRelease(cmd); }
            }, !success);
        }

        wgpuCommandEncoderRelease(encoder);
    }
}

// ---------------------------------------------------------------------------
// Helper: formatCopyableWithMethod
//
// Returns true if the given format can be copied with the given method.
// Mirrors formatCopyableWithMethod() from image_copy.ts.
//
// For depth/stencil formats, at least one aspect must be copyable for that
// method direction.
// For color/compressed formats, all sized formats (bytesPerBlock > 0) are
// copyable in either direction.
// ---------------------------------------------------------------------------
static bool formatCopyableWithMethod(WGPUTextureFormat format, ImageCopyType method) {
    if (isDepthOrStencilTextureFormat(format)) {
        const auto aspects = depthStencilFormatCopyableAspects(method, format);
        return !aspects.empty();
    }
    // Color + compressed: any format with bytesPerBlock > 0 is "sized" and
    // copyable in both directions.
    const TextureBlockInfo info = getBlockInfoForTextureFormat(format);
    return info.bytesPerBlock > 0;
}

// ---------------------------------------------------------------------------
// Helper: kSizedTextureFormats
//
// Mirrors kSizedTextureFormats from format_info.ts — all texture formats
// that have a defined bytes-per-block (i.e. bytesPerBlock > 0).
// Excludes depth24plus (bytesPerBlock == 0).
// ---------------------------------------------------------------------------
static std::vector<WGPUTextureFormat> sizedTextureFormats() {
    std::vector<WGPUTextureFormat> result;
    result.reserve(kAllTextureFormats.size());
    for (WGPUTextureFormat fmt : kAllTextureFormats) {
        const TextureBlockInfo info = getBlockInfoForTextureFormat(fmt);
        if (info.bytesPerBlock > 0) {
            result.push_back(fmt);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// buffer_state
//
// Test that the buffer must be valid and not destroyed.
// - for CopyB2T and CopyT2B
// - for various buffer states (valid, invalid, destroyed)
//
// The upstream notes:
//   "Invalid buffer will fail finish, and destroyed buffer will fail submit"
// So: submit = (state != 'invalid'), success = (state == 'valid').
// ---------------------------------------------------------------------------
CTS_TEST(g, "buffer_state")
    .desc(
        "Test that the buffer must be valid and not destroyed.\n"
        "- for all buffer <-> texture copy methods\n"
        "- for various buffer states")
    .params([](ParamsBuilder u) {
        return u
            .combine("method", {Value("CopyB2T"), Value("CopyT2B")})
            .combine("state", resourceStateValues());
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string method = t.param<std::string>("method");
        const ResourceState state = parseResourceState(t.param<std::string>("state"));

        // A buffer that may be valid, invalid, or destroyed.
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 16;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferWithState(state, bufDesc);

        // Invalid buffer will fail at finish(); destroyed buffer at submit().
        const bool submit  = (state != ResourceState::Invalid);
        const bool success = (state == ResourceState::Valid);

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size.width            = 2;
        texDesc.size.height           = 2;
        texDesc.size.depthOrArrayLayers = 1;
        texDesc.format                = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        WGPUExtent3D copySize = {0, 0, 0};
        testBuffer(t, buffer, texture,
                   WGPU_COPY_STRIDE_UNDEFINED,
                   copySize,
                   method,
                   success,
                   submit);
    });

// ---------------------------------------------------------------------------
// buffer,device_mismatch
//
// Tests that image copies cannot be called with a buffer created from another
// device.
// ---------------------------------------------------------------------------
CTS_TEST(g, "buffer,device_mismatch")
    .desc("Tests the image copies cannot be called with a buffer created from another device")
    .params([](ParamsBuilder u) {
        return u
            .combine("method", {Value("CopyB2T"), Value("CopyT2B")})
            .beginSubcases()
            .combine("mismatched", {true, false});
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string method    = t.param<std::string>("method");
        const bool mismatched       = t.param<bool>("mismatched");

        // Create the buffer on the test device or the mismatched device.
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 16;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = mismatched
            ? t.createBufferOnMismatchedDevice(bufDesc)
            : t.createBufferTracked(bufDesc);

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size.width            = 2;
        texDesc.size.height           = 2;
        texDesc.size.depthOrArrayLayers = 1;
        texDesc.format                = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        const bool success = !mismatched;

        // Expect success in both finish and submit, or validation error at finish.
        WGPUExtent3D copySize = {0, 0, 0};
        testBuffer(t, buffer, texture,
                   WGPU_COPY_STRIDE_UNDEFINED,
                   copySize,
                   method,
                   success,
                   /*submit=*/success);
    });

// ---------------------------------------------------------------------------
// usage
//
// Test the buffer must have the appropriate COPY_SRC/COPY_DST usage.
// ---------------------------------------------------------------------------
CTS_TEST(g, "usage")
    .desc(
        "Test the buffer must have the appropriate COPY_SRC/COPY_DST usage.\n"
        "TODO update such that it tests\n"
        "- for all buffer source usages\n"
        "- for all buffer destination usages")
    .params([](ParamsBuilder u) {
        return u
            .combine("method", {Value("CopyB2T"), Value("CopyT2B")})
            .beginSubcases()
            .combine("usage", {
                Value(static_cast<int64_t>(WGPUBufferUsage_CopySrc | WGPUBufferUsage_Uniform)),
                Value(static_cast<int64_t>(WGPUBufferUsage_CopyDst | WGPUBufferUsage_Uniform)),
                Value(static_cast<int64_t>(WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst)),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string method = t.param<std::string>("method");
        const WGPUBufferUsage usage =
            static_cast<WGPUBufferUsage>(t.param<int64_t>("usage"));

        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 16;
        bufDesc.usage = usage;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        // CopyB2T needs COPY_SRC on the buffer; CopyT2B needs COPY_DST.
        const bool success = (method == "CopyB2T")
            ? ((usage & WGPUBufferUsage_CopySrc) != 0)
            : ((usage & WGPUBufferUsage_CopyDst) != 0);

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size.width            = 2;
        texDesc.size.height           = 2;
        texDesc.size.depthOrArrayLayers = 1;
        texDesc.format                = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        // Expect success in both finish and submit, or validation error at finish.
        WGPUExtent3D copySize = {0, 0, 0};
        testBuffer(t, buffer, texture,
                   WGPU_COPY_STRIDE_UNDEFINED,
                   copySize,
                   method,
                   success,
                   /*submit=*/success);
    });

// ---------------------------------------------------------------------------
// bytes_per_row_alignment
//
// Test that bytesPerRow must be a multiple of 256 for CopyB2T and CopyT2B
// if it is required.
// - for all copy methods between linear data and textures
// - for all texture dimensions
// - for all sized formats
// - for various bytesPerRow values (aligned to 256 or not)
// - for various numbers of block rows copied
//
// Upstream iterates over kImageCopyTypes (WriteTexture, CopyB2T, CopyT2B),
// kSizedTextureFormats, kTextureDimensions, and subcase values of
// bytesPerRow/copyHeightInBlocks.
// ---------------------------------------------------------------------------
CTS_TEST(g, "bytes_per_row_alignment")
    .desc(
        "Test that bytesPerRow must be a multiple of 256 for CopyB2T and CopyT2B if it is required.\n"
        "- for all copy methods between linear data and textures\n"
        "- for all texture dimensions\n"
        "- for all sized formats\n"
        "- for various bytesPerRow aligned to 256 or not\n"
        "- for various number of blocks rows copied")
    .params([](ParamsBuilder u) {
        // Build the outer (case) parameters: method x format x dimension,
        // filtered for compatibility.
        std::vector<ParamRecord> outerRecords;

        // Upstream kImageCopyTypes = ['WriteTexture', 'CopyB2T', 'CopyT2B']
        const std::array<std::pair<std::string, ImageCopyType>, 3> methods = {{
            {"WriteTexture", ImageCopyType::WriteTexture},
            {"CopyB2T",      ImageCopyType::CopyB2T},
            {"CopyT2B",      ImageCopyType::CopyT2B},
        }};

        const std::vector<WGPUTextureFormat> fmts = sizedTextureFormats();

        for (const auto& [methodStr, methodEnum] : methods) {
            for (WGPUTextureFormat fmt : fmts) {
                // formatCopyableWithMethod filter
                if (!formatCopyableWithMethod(fmt, methodEnum)) {
                    continue;
                }
                for (WGPUTextureDimension dim : kTextureDimensions) {
                    // textureFormatAndDimensionPossiblyCompatible filter
                    if (!textureFormatAndDimensionPossiblyCompatible(dim, fmt)) {
                        continue;
                    }
                    outerRecords.push_back({
                        {"method",    Value(methodStr)},
                        {"format",    Value(std::string(textureFormatIdentifier(fmt)))},
                        {"dimension", Value(static_cast<int64_t>(static_cast<int>(dim)))},
                    });
                }
            }
        }

        // Sub-cases: bytesPerRow x copyHeightInBlocks, filtered as upstream.
        // bytesPerRow: undefined (represented as -1), 0, 1, 255, 256, 257, 512
        // copyHeightInBlocks: 0, 1, 2, 3
        return u.combineWithParams(outerRecords)
            .beginSubcases()
            .combine("bytesPerRow", {
                Value(int64_t(-1)),   // -1 represents "undefined" / WGPU_COPY_STRIDE_UNDEFINED
                Value(int64_t(0)),
                Value(int64_t(1)),
                Value(int64_t(255)),
                Value(int64_t(256)),
                Value(int64_t(257)),
                Value(int64_t(512)),
            })
            .combine("copyHeightInBlocks", {
                Value(int64_t(0)),
                Value(int64_t(1)),
                Value(int64_t(2)),
                Value(int64_t(3)),
            })
            // Unless 1D and copyHeightInBlocks > 1
            .filter([](const ParamRecord& p) {
                const int64_t dim = [&] {
                    for (const auto& kv : p) {
                        if (kv.first == "dimension") {
                            return std::get<int64_t>(kv.second.data());
                        }
                    }
                    return int64_t(-1);
                }();
                const int64_t copyH = [&] {
                    for (const auto& kv : p) {
                        if (kv.first == "copyHeightInBlocks") {
                            return std::get<int64_t>(kv.second.data());
                        }
                    }
                    return int64_t(0);
                }();
                if (dim == static_cast<int64_t>(WGPUTextureDimension_1D) && copyH > 1) {
                    return false;
                }
                return true;
            })
            // bytesPerRow must be >= bytesPerBlock when specified and copyHeight > 0
            .filter([](const ParamRecord& p) {
                // Extract format string, bytesPerRow, copyHeightInBlocks
                std::string fmtStr;
                int64_t bpr = -1;
                int64_t copyH = 0;
                for (const auto& kv : p) {
                    if (kv.first == "format")            fmtStr = std::get<std::string>(kv.second.data());
                    else if (kv.first == "bytesPerRow")  bpr    = std::get<int64_t>(kv.second.data());
                    else if (kv.first == "copyHeightInBlocks") copyH = std::get<int64_t>(kv.second.data());
                }
                if (fmtStr.empty()) return true;

                const WGPUTextureFormat fmt = parseTextureFormat(fmtStr);
                const TextureBlockInfo info = getBlockInfoForTextureFormat(fmt);

                // bytesPerRow undefined: only valid when copyHeight <= 1
                if (bpr == -1) {
                    return copyH <= 1;
                }
                // bytesPerRow specified: must be >= bytesPerBlock when copying > 0 rows
                // (single block per row in this test)
                return bpr >= static_cast<int64_t>(info.bytesPerBlock);
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const std::string methodStr = t.param<std::string>("method");
        const std::string fmtStr    = t.param<std::string>("format");
        const int64_t dimInt        = t.param<int64_t>("dimension");
        const int64_t bprParam      = t.param<int64_t>("bytesPerRow");
        const int64_t copyH         = t.param<int64_t>("copyHeightInBlocks");

        const WGPUTextureFormat format    = parseTextureFormat(fmtStr);
        const WGPUTextureDimension dimension =
            static_cast<WGPUTextureDimension>(static_cast<int>(dimInt));

        // Upstream: t.skipIfTextureFormatNotSupported(format)
        t.skipIfTextureFormatNotSupported(format);
        // Upstream: t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension)
        t.skipIfTextureFormatAndDimensionNotCompatible(format, dimension);

        const TextureBlockInfo info = getBlockInfoForTextureFormat(format);

        // The actual bytesPerRow to use in the copy (UNDEFINED when bprParam == -1).
        const uint32_t bytesPerRow = (bprParam == -1)
            ? WGPU_COPY_STRIDE_UNDEFINED
            : static_cast<uint32_t>(bprParam);

        // _textureHeightInBlocks = copyHeightInBlocks == 0 ? 1 : copyHeightInBlocks
        const uint32_t textureHeightInBlocks =
            (copyH == 0) ? 1u : static_cast<uint32_t>(copyH);

        // Depth/stencil formats: copy must cover the whole subresource,
        // so we skip the case where copyHeightInBlocks != textureHeightInBlocks.
        // This is already filtered in the upstream (.unless), so this case
        // should not arise. Guard here for safety.
        if (isDepthOrStencilTextureFormat(format) &&
            static_cast<uint32_t>(copyH) != textureHeightInBlocks) {
            t.skip("depth/stencil copy must cover whole subresource");
        }

        // The buffer is larger than any single row to handle any bytesPerRow.
        WGPUBufferDescriptor bufDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufDesc.size  = 512u * 8u * 16u;
        bufDesc.usage = WGPUBufferUsage_CopySrc | WGPUBufferUsage_CopyDst;
        WGPUBuffer buffer = t.createBufferTracked(bufDesc);

        // Determine success:
        //   WriteTexture never requires bytesPerRow to be 256-byte aligned.
        //   If copyHeight <= 1 and bytesPerRow is undefined, it succeeds.
        //   If bytesPerRow is a positive multiple of 256, it succeeds.
        bool success = false;
        if (methodStr == "WriteTexture") {
            success = true;
        } else if (copyH <= 1 && bytesPerRow == WGPU_COPY_STRIDE_UNDEFINED) {
            success = true;
        } else if (bytesPerRow != WGPU_COPY_STRIDE_UNDEFINED &&
                   bytesPerRow > 0 &&
                   bytesPerRow % 256 == 0) {
            success = true;
        }

        // The copy below uses aspect 'all'. For a *combined* depth-stencil
        // format (one with BOTH a depth and a stencil aspect — depth24plus-
        // stencil8, depth32float-stencil8) a whole-format ('all') buffer copy is
        // never permitted; only a single aspect (depth-only / stencil-only) is
        // copyable. So for those formats the copy is a validation error
        // regardless of bytesPerRow alignment — override the prediction to
        // failure. Single-aspect depth/stencil formats (depth16unorm, depth32float,
        // stencil8) and color formats accept aspect 'all' and are unaffected.
        // (Matches the WebGPU spec / Dawn oracle.)
        if (isDepthTextureFormat(format) && isStencilTextureFormat(format)) {
            success = false;
        }

        // Create the texture (one block wide, textureHeightInBlocks * blockHeight tall).
        WGPUExtent3D texSize = WGPUExtent3D{
            info.blockWidth,
            textureHeightInBlocks * info.blockHeight,
            1,
        };
        // Clamp height to 1 for 1D textures.
        if (dimension == WGPUTextureDimension_1D) {
            texSize.height = 1;
        }

        WGPUTextureDescriptor texDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
        texDesc.size              = texSize;
        texDesc.dimension         = dimension;
        texDesc.format            = format;
        texDesc.usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
        WGPUTexture texture = t.createTextureTracked(texDesc);

        // Copy size: one block wide, copyHeightInBlocks * blockHeight tall.
        WGPUExtent3D copySize = WGPUExtent3D{
            info.blockWidth,
            static_cast<uint32_t>(copyH) * info.blockHeight,
            1,
        };
        // Clamp height to 0 for 1D textures when copyH is 0.
        if (dimension == WGPUTextureDimension_1D) {
            copySize.height = 1;
        }
        // When copyHeightInBlocks == 0, the copy is empty.
        if (copyH == 0) {
            copySize.width  = 0;
            copySize.height = 0;
        }

        if (methodStr == "WriteTexture") {
            // WriteTexture is a direct queue operation.
            WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
            destination.texture  = texture;
            destination.mipLevel = 0;
            destination.origin   = WGPUOrigin3D{0, 0, 0};
            destination.aspect   = WGPUTextureAspect_All;

            WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
            layout.offset       = 0;
            layout.bytesPerRow  = bytesPerRow;
            layout.rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;

            const uint64_t dataSize = static_cast<uint64_t>(512) * 8 * 16;
            std::vector<uint8_t> data(static_cast<size_t>(dataSize), 0);

            t.expectValidationError([&] {
                wgpuQueueWriteTexture(
                    t.queue(), &destination,
                    data.data(), data.size(),
                    &layout, &copySize);
            }, !success);
        } else {
            // CopyB2T or CopyT2B — errors defer to finish().
            testBuffer(t, buffer, texture,
                       bytesPerRow,
                       copySize,
                       methodStr,
                       success,
                       /*submit=*/success);
        }
    });

} // namespace
