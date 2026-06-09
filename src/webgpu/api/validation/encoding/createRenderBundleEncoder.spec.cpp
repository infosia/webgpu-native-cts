// Ported from gpuweb/cts src/webgpu/api/validation/encoding/createRenderBundleEncoder.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/capability_info.h"
#include "webgpu/texture_format.h"
#include "webgpu/util/enum_strings.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Color-render byte-cost / alignment table (from upstream format_info.ts)
// Used by computeBytesPerSampleFromFormats and getColorRenderByteCost.
// Only covers the kPossibleColorRenderableTextureFormats set
// (kRegularTextureFormats filtered by colorRender != undefined in upstream).
// ---------------------------------------------------------------------------

struct ColorRenderInfo {
    WGPUTextureFormat format;
    uint32_t          byteCost;
    uint32_t          alignment;
};

static constexpr std::array<ColorRenderInfo, 42> kColorRenderInfoTable = {{
    // 8-bit regular
    { WGPUTextureFormat_R8Unorm,         1, 1 },
    { WGPUTextureFormat_R8Uint,          1, 1 },
    { WGPUTextureFormat_R8Sint,          1, 1 },
    { WGPUTextureFormat_RG8Unorm,        2, 1 },
    { WGPUTextureFormat_RG8Uint,         2, 1 },
    { WGPUTextureFormat_RG8Sint,         2, 1 },
    { WGPUTextureFormat_RGBA8Unorm,      8, 1 },
    { WGPUTextureFormat_RGBA8UnormSrgb,  8, 1 },
    { WGPUTextureFormat_RGBA8Uint,       4, 1 },
    { WGPUTextureFormat_RGBA8Sint,       4, 1 },
    { WGPUTextureFormat_BGRA8Unorm,      8, 1 },
    { WGPUTextureFormat_BGRA8UnormSrgb,  8, 1 },
    // 16-bit (unorm/snorm require texture-formats-tier1)
    { WGPUTextureFormat_R16Unorm,        2, 2 },
    { WGPUTextureFormat_R16Snorm,        2, 2 },
    { WGPUTextureFormat_R16Uint,         2, 2 },
    { WGPUTextureFormat_R16Sint,         2, 2 },
    { WGPUTextureFormat_R16Float,        2, 2 },
    { WGPUTextureFormat_RG16Unorm,       4, 2 },
    { WGPUTextureFormat_RG16Snorm,       4, 2 },
    { WGPUTextureFormat_RG16Uint,        4, 2 },
    { WGPUTextureFormat_RG16Sint,        4, 2 },
    { WGPUTextureFormat_RG16Float,       4, 2 },
    { WGPUTextureFormat_RGBA16Unorm,     8, 4 },
    { WGPUTextureFormat_RGBA16Snorm,     8, 2 },
    { WGPUTextureFormat_RGBA16Uint,      8, 2 },
    { WGPUTextureFormat_RGBA16Sint,      8, 2 },
    { WGPUTextureFormat_RGBA16Float,     8, 2 },
    // 32-bit
    { WGPUTextureFormat_R32Uint,         4, 4 },
    { WGPUTextureFormat_R32Sint,         4, 4 },
    { WGPUTextureFormat_R32Float,        4, 4 },
    { WGPUTextureFormat_RG32Uint,        8, 4 },
    { WGPUTextureFormat_RG32Sint,        8, 4 },
    { WGPUTextureFormat_RG32Float,       8, 4 },
    { WGPUTextureFormat_RGBA32Uint,     16, 4 },
    { WGPUTextureFormat_RGBA32Sint,     16, 4 },
    { WGPUTextureFormat_RGBA32Float,    16, 4 },
    // packed / mixed
    { WGPUTextureFormat_RGB10A2Uint,     8, 4 },
    { WGPUTextureFormat_RGB10A2Unorm,    8, 4 },
    // rg11b10ufloat: requires rg11b10ufloat-renderable (always has colorRender info in upstream)
    { WGPUTextureFormat_RG11B10Ufloat,   8, 4 },
    // Tier1 snorm 8-bit: NOT in upstream kPossibleColorRenderableTextureFormats
    // (upstream kTextureFormatInfo does not define colorRender for them), but included
    // here for the byteCost table completeness (used by shouldError logic when
    // isTextureFormatColorRenderable returns true under Tier1).
    { WGPUTextureFormat_R8Snorm,         1, 1 },
    { WGPUTextureFormat_RG8Snorm,        2, 1 },
    { WGPUTextureFormat_RGBA8Snorm,      4, 1 },
    // rgb9e5ufloat has no colorRender in upstream — omitted.
}};

// kPossibleColorRenderableTextureFormats:
// Matches upstream kRegularTextureFormats.filter(f => kTextureFormatInfo[f].colorRender).
// The 8-bit snorm formats do NOT appear in the upstream list.
static constexpr std::array<WGPUTextureFormat, 39> kPossibleColorRenderableTextureFormats = {{
    WGPUTextureFormat_R8Unorm,
    WGPUTextureFormat_R8Uint,
    WGPUTextureFormat_R8Sint,
    WGPUTextureFormat_RG8Unorm,
    WGPUTextureFormat_RG8Uint,
    WGPUTextureFormat_RG8Sint,
    WGPUTextureFormat_RGBA8Unorm,
    WGPUTextureFormat_RGBA8UnormSrgb,
    WGPUTextureFormat_RGBA8Uint,
    WGPUTextureFormat_RGBA8Sint,
    WGPUTextureFormat_BGRA8Unorm,
    WGPUTextureFormat_BGRA8UnormSrgb,
    WGPUTextureFormat_R16Unorm,
    WGPUTextureFormat_R16Snorm,
    WGPUTextureFormat_R16Uint,
    WGPUTextureFormat_R16Sint,
    WGPUTextureFormat_R16Float,
    WGPUTextureFormat_RG16Unorm,
    WGPUTextureFormat_RG16Snorm,
    WGPUTextureFormat_RG16Uint,
    WGPUTextureFormat_RG16Sint,
    WGPUTextureFormat_RG16Float,
    WGPUTextureFormat_RGBA16Unorm,
    WGPUTextureFormat_RGBA16Snorm,
    WGPUTextureFormat_RGBA16Uint,
    WGPUTextureFormat_RGBA16Sint,
    WGPUTextureFormat_RGBA16Float,
    WGPUTextureFormat_R32Uint,
    WGPUTextureFormat_R32Sint,
    WGPUTextureFormat_R32Float,
    WGPUTextureFormat_RG32Uint,
    WGPUTextureFormat_RG32Sint,
    WGPUTextureFormat_RG32Float,
    WGPUTextureFormat_RGBA32Uint,
    WGPUTextureFormat_RGBA32Sint,
    WGPUTextureFormat_RGBA32Float,
    WGPUTextureFormat_RGB10A2Uint,
    WGPUTextureFormat_RGB10A2Unorm,
    WGPUTextureFormat_RG11B10Ufloat,
}};

// Returns the ColorRenderInfo for a given format, or nullptr if not found.
static const ColorRenderInfo* findColorRenderInfo(WGPUTextureFormat format) {
    for (const ColorRenderInfo& info : kColorRenderInfoTable) {
        if (info.format == format) {
            return &info;
        }
    }
    return nullptr;
}

// Equivalent to upstream getColorRenderByteCost().
static uint32_t getColorRenderByteCost(WGPUTextureFormat format) {
    const ColorRenderInfo* info = findColorRenderInfo(format);
    if (info == nullptr) {
        // Should never be called for non-color-renderable formats.
        return 0;
    }
    return info->byteCost;
}

// Equivalent to upstream computeBytesPerSampleFromFormats().
// Applies alignment then adds byteCost for each format.
static uint32_t computeBytesPerSampleFromFormats(const std::vector<WGPUTextureFormat>& formats) {
    uint32_t bytesPerSample = 0;
    for (WGPUTextureFormat format : formats) {
        const ColorRenderInfo* info = findColorRenderInfo(format);
        if (info == nullptr) {
            continue; // non-renderable format, skip
        }
        // align up
        const uint32_t alignment = info->alignment;
        bytesPerSample = (bytesPerSample + alignment - 1) & ~(alignment - 1);
        bytesPerSample += info->byteCost;
    }
    return bytesPerSample;
}

// Build param values for kPossibleColorRenderableTextureFormats
static std::vector<Value> possibleColorRenderableFormatValues() {
    return formatIdentifierValues(kPossibleColorRenderableTextureFormats);
}

// Build param values for kAllTextureFormats
static std::vector<Value> allTextureFormatValues() {
    return formatIdentifierValues(kAllTextureFormats);
}

// Build param values for kDepthStencilFormats
static std::vector<Value> depthStencilFormatValues() {
    return formatIdentifierValues(kDepthStencilFormats);
}

// The upstream uses AllFeaturesMaxLimitsGPUTest; we use AllFeaturesMaxLimitsGpuTest.
// kMaxColorAttachments = getDefaultLimits('core').maxColorAttachments.default = 8.

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,encoding,createRenderBundleEncoder",
    "createRenderBundleEncoder validation tests.");

// ---------------------------------------------------------------------------
// attachment_state,limits,maxColorAttachments
// Tests that attachment state must have <= device.limits.maxColorAttachments.
// ---------------------------------------------------------------------------
CTS_TEST(g, "attachment_state,limits,maxColorAttachments")
    .desc("Tests that attachment state must have <= device.limits.maxColorAttachments.")
    .params([](ParamsBuilder u) {
        // colorFormatCount in range [1, kMaxColorAttachments] (= range(kMaxColorAttachments, i => i + 1))
        return u.beginSubcases().combine("colorFormatCount", {
            Value(1), Value(2), Value(3), Value(4),
            Value(5), Value(6), Value(7), Value(8),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const uint32_t colorFormatCount = static_cast<uint32_t>(t.param<int>("colorFormatCount"));
        const WGPULimits limits = t.getLimits();
        const uint32_t maxColorAttachments = limits.maxColorAttachments;

        if (colorFormatCount > maxColorAttachments) {
            t.skip("colorFormatCount > maxColorAttachments: " + std::to_string(maxColorAttachments));
        }

        // r8unorm repeated colorFormatCount times
        std::vector<WGPUTextureFormat> colorFormats(colorFormatCount, WGPUTextureFormat_R8Unorm);

        const bool shouldError = (colorFormatCount > maxColorAttachments);

        t.expectValidationError([&] {
            WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            desc.colorFormatCount = colorFormats.size();
            desc.colorFormats     = colorFormats.data();
            WGPURenderBundleEncoder enc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
            if (enc) {
                wgpuRenderBundleEncoderRelease(enc);
            }
        }, shouldError);
    });

// ---------------------------------------------------------------------------
// attachment_state,limits,maxColorAttachmentBytesPerSample,aligned
// Tests total color attachment bytes per sample <= maxColorAttachmentBytesPerSample
// when using the same format for all attachments.
// ---------------------------------------------------------------------------
CTS_TEST(g, "attachment_state,limits,maxColorAttachmentBytesPerSample,aligned")
    .desc(
        "Tests that the total color attachment bytes per sample <= "
        "device.limits.maxColorAttachmentBytesPerSample when using the same format (aligned) for "
        "multiple attachments.")
    .params([](ParamsBuilder u) {
        return u
            .combine("format", possibleColorRenderableFormatValues())
            .beginSubcases()
            .combine("colorFormatCount", {
                Value(1), Value(2), Value(3), Value(4),
                Value(5), Value(6), Value(7), Value(8),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const uint32_t colorFormatCount = static_cast<uint32_t>(t.param<int>("colorFormatCount"));

        t.skipIfTextureFormatNotSupported(format);

        const WGPULimits limits = t.getLimits();
        const uint32_t maxColorAttachments = limits.maxColorAttachments;

        if (colorFormatCount > maxColorAttachments) {
            t.skip(
                std::to_string(colorFormatCount) + " > maxColorAttachments: " +
                std::to_string(maxColorAttachments));
        }

        const bool colorRenderable = t.isTextureFormatColorRenderable(format);
        const uint32_t byteCost = getColorRenderByteCost(format) * colorFormatCount;
        const uint32_t maxBytesPerSample = limits.maxColorAttachmentBytesPerSample;

        const bool shouldError = !colorRenderable || (byteCost > maxBytesPerSample);

        std::vector<WGPUTextureFormat> colorFormats(colorFormatCount, format);

        t.expectValidationError([&] {
            WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            desc.colorFormatCount = colorFormats.size();
            desc.colorFormats     = colorFormats.data();
            WGPURenderBundleEncoder enc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
            if (enc) {
                wgpuRenderBundleEncoderRelease(enc);
            }
        }, shouldError);
    });

// ---------------------------------------------------------------------------
// attachment_state,limits,maxColorAttachmentBytesPerSample,unaligned
// Tests total color attachment bytes per sample <= maxColorAttachmentBytesPerSample
// for specific sets of unaligned formats.
// ---------------------------------------------------------------------------
CTS_TEST(g, "attachment_state,limits,maxColorAttachmentBytesPerSample,unaligned")
    .desc(
        "Tests that the total color attachment bytes per sample <= "
        "device.limits.maxColorAttachmentBytesPerSample when using various sets of (potentially) "
        "unaligned formats.")
    .params([](ParamsBuilder u) {
        // Two hardcoded format combinations from the upstream spec comment:
        // Case 0: [r8unorm, r32float, rgba8unorm, rgba32float, r8unorm] → alignment causes 4+4+8+16+1 > 32
        // Case 1: [r32float, rgba8unorm, rgba32float, r8unorm, r8unorm] → 4+8+16+1+1 < 32
        return u.combineWithParams({
            {{"formats", Value(0)}},
            {{"formats", Value(1)}},
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int formatsIndex = t.param<int>("formats");

        // Reproduce the two upstream format arrays
        static const std::array<WGPUTextureFormat, 5> kFormats0 = {{
            WGPUTextureFormat_R8Unorm,
            WGPUTextureFormat_R32Float,
            WGPUTextureFormat_RGBA8Unorm,
            WGPUTextureFormat_RGBA32Float,
            WGPUTextureFormat_R8Unorm,
        }};
        static const std::array<WGPUTextureFormat, 5> kFormats1 = {{
            WGPUTextureFormat_R32Float,
            WGPUTextureFormat_RGBA8Unorm,
            WGPUTextureFormat_RGBA32Float,
            WGPUTextureFormat_R8Unorm,
            WGPUTextureFormat_R8Unorm,
        }};

        const WGPUTextureFormat* rawFormats = (formatsIndex == 0) ? kFormats0.data() : kFormats1.data();
        const std::vector<WGPUTextureFormat> formats(rawFormats, rawFormats + 5);

        const WGPULimits limits = t.getLimits();

        if (static_cast<uint32_t>(formats.size()) > limits.maxColorAttachments) {
            t.skip(
                "numColorAttachments: " + std::to_string(formats.size()) +
                " > maxColorAttachments: " + std::to_string(limits.maxColorAttachments));
        }

        const uint32_t bytesPerSample = computeBytesPerSampleFromFormats(formats);
        const bool shouldError = (bytesPerSample > limits.maxColorAttachmentBytesPerSample);

        t.expectValidationError([&] {
            WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            desc.colorFormatCount = formats.size();
            desc.colorFormats     = formats.data();
            WGPURenderBundleEncoder enc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
            if (enc) {
                wgpuRenderBundleEncoderRelease(enc);
            }
        }, shouldError);
    });

// ---------------------------------------------------------------------------
// attachment_state,empty_color_formats
// Tests that if no colorFormats are given, a depthStencilFormat must be specified.
// ---------------------------------------------------------------------------
CTS_TEST(g, "attachment_state,empty_color_formats")
    .desc("Tests that if no colorFormats are given, a depthStencilFormat must be specified.")
    .params([](ParamsBuilder u) {
        // depthStencilFormat: undefined | 'depth24plus-stencil8'
        return u.beginSubcases().combine("depthStencilFormat", {
            Value::undef(),
            Value(std::string("depth24plus-stencil8")),
        });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const bool hasDepthStencil = !t.paramIsUndefined("depthStencilFormat");
        const WGPUTextureFormat dsFormat = hasDepthStencil
            ? parseTextureFormat(t.param<std::string>("depthStencilFormat"))
            : WGPUTextureFormat_Undefined;

        // Error if no colorFormats AND no depthStencilFormat
        const bool shouldError = !hasDepthStencil;

        t.expectValidationError([&] {
            WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            desc.colorFormatCount     = 0;
            desc.colorFormats         = nullptr;
            desc.depthStencilFormat   = dsFormat;
            WGPURenderBundleEncoder enc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
            if (enc) {
                wgpuRenderBundleEncoderRelease(enc);
            }
        }, shouldError);
    });

// ---------------------------------------------------------------------------
// valid_texture_formats
// Tests that createRenderBundleEncoder only accepts valid formats for its attachments.
// ---------------------------------------------------------------------------
CTS_TEST(g, "valid_texture_formats")
    .desc(
        "Tests that createRenderBundleEncoder only accepts valid formats for its attachments.\n"
        "  - colorFormats\n"
        "  - depthStencilFormat")
    .params([](ParamsBuilder u) {
        return u
            .combine("format", allTextureFormatValues())
            .beginSubcases()
            .combine("attachment", {
                Value(std::string("color")),
                Value(std::string("depthStencil")),
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat format = parseTextureFormat(t.param<std::string>("format"));
        const std::string attachment = t.param<std::string>("attachment");

        t.skipIfTextureFormatNotSupported(format);

        const bool colorRenderable = t.isTextureFormatColorRenderable(format);
        const bool depthStencil = isDepthOrStencilTextureFormat(format);

        if (attachment == "color") {
            t.expectValidationError([&] {
                WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                desc.colorFormatCount = 1;
                desc.colorFormats     = &format;
                WGPURenderBundleEncoder enc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
                if (enc) {
                    wgpuRenderBundleEncoderRelease(enc);
                }
            }, !colorRenderable);
        } else {
            // "depthStencil"
            t.expectValidationError([&] {
                WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
                desc.colorFormatCount     = 0;
                desc.colorFormats         = nullptr;
                desc.depthStencilFormat   = format;
                WGPURenderBundleEncoder enc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
                if (enc) {
                    wgpuRenderBundleEncoderRelease(enc);
                }
            }, !depthStencil);
        }
    });

// ---------------------------------------------------------------------------
// depth_stencil_readonly
// Tests that all combinations of depthStencilFormat + depthReadOnly + stencilReadOnly are allowed.
// ---------------------------------------------------------------------------
CTS_TEST(g, "depth_stencil_readonly")
    .desc(
        "Test that allow combinations of depth-stencil format, depthReadOnly and stencilReadOnly "
        "are allowed.")
    .params([](ParamsBuilder u) {
        return u
            .combine("depthStencilFormat", depthStencilFormatValues())
            .beginSubcases()
            .combine("depthReadOnly",  { Value(false), Value(true) })
            .combine("stencilReadOnly", { Value(false), Value(true) });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const WGPUTextureFormat dsFormat = parseTextureFormat(
            t.param<std::string>("depthStencilFormat"));
        const bool depthReadOnly   = t.param<bool>("depthReadOnly");
        const bool stencilReadOnly = t.param<bool>("stencilReadOnly");

        t.skipIfTextureFormatNotSupported(dsFormat);

        // The upstream test expects no error for any combination of
        // depthReadOnly/stencilReadOnly — it simply calls createRenderBundleEncoder
        // without wrapping in expectValidationError.
        WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
        desc.colorFormatCount   = 0;
        desc.colorFormats       = nullptr;
        desc.depthStencilFormat = dsFormat;
        desc.depthReadOnly      = depthReadOnly  ? WGPU_TRUE : WGPU_FALSE;
        desc.stencilReadOnly    = stencilReadOnly ? WGPU_TRUE : WGPU_FALSE;

        WGPURenderBundleEncoder enc = wgpuDeviceCreateRenderBundleEncoder(t.device(), &desc);
        // All combinations should succeed; if enc is null there was an unexpected error.
        t.expect(enc != nullptr, "createRenderBundleEncoder unexpectedly returned null");
        if (enc) {
            wgpuRenderBundleEncoderRelease(enc);
        }
    });

} // namespace
