// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/extension/clip_distances.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2024 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//   - Upstream beforeAllSubcases selects a device WITH the clip-distances feature
//     only when requireExtension; success is then (requireExtension && enableExtension).
//     The device-feature dimension is what `requireExtension` actually controls.
//     Our private context requests ALL adapter features, so on a clip-distances-capable
//     adapter the feature is always present; the spec-faithful success condition on
//     this device is therefore (enableExtension && device-has-clip-distances). When
//     enableExtension is true but the device lacks the feature, the shader's
//     `enable clip_distances;` triggers the auto-skip (a skip, not a fail). When
//     enableExtension is false the builtin is used without its `enable` directive,
//     which must fail. The `requireExtension` param is preserved for query identity.

#include <string>

#include "cts/test.h"
#include "webgpu/shader/validation/shader_validation_test.h"

using namespace cts;
using cts::shader_validation::UniqueFeaturesAndLimitsShaderValidationTest;

namespace {

TestGroup<UniqueFeaturesAndLimitsShaderValidationTest> g =
    MakeTestGroup<UniqueFeaturesAndLimitsShaderValidationTest>(
        "shader,validation,extension,clip_distances",
        "Validation tests for the clip_distances extension");

CTS_TEST(g, "use_clip_distances_requires_extension_enabled")
    .desc(R"(Checks that the clip_distances built-in variable is only allowed with the WGSL extension
     clip_distances enabled in shader and the WebGPU extension clip-distances supported on the
     device.)")
    .params([](ParamsBuilder u) {
        return u.combine("requireExtension", {Value(true), Value(false)})
                .combine("enableExtension", {Value(true), Value(false)});
    })
    .fn([](UniqueFeaturesAndLimitsShaderValidationTest& t) {
        const bool requireExtension = t.param<bool>("requireExtension");
        (void)requireExtension; // preserved for query identity; device always all-features
        const bool enableExtension = t.param<bool>("enableExtension");

        // On the all-features device, the clip_distances builtin is valid iff the
        // WGSL `enable clip_distances;` directive is present AND the device has the
        // feature. (When the directive is present but the feature is absent, the
        // auto-skip in expectCompileResult skips the case before reaching here.)
        const bool deviceHasFeature = t.deviceHasFeature(WGPUFeatureName_ClipDistances);
        const bool expectedSuccess = enableExtension && deviceHasFeature;

        const std::string enableLine = enableExtension ? "enable clip_distances;" : "";
        const std::string code =
            "\n        " + enableLine +
            "\n        struct VertexOut {"
            "\n          @builtin(clip_distances) my_clip_distances : array<f32, 1>,"
            "\n          @builtin(position) my_position : vec4f,"
            "\n        }"
            "\n        @vertex fn main() -> VertexOut {"
            "\n          var output : VertexOut;"
            "\n          output.my_clip_distances[0] = 1.0;"
            "\n          output.my_position = vec4f(0.0, 0.0, 0.0, 1.0);"
            "\n          return output;"
            "\n        }"
            "\n    ";

        t.expectCompileResult(expectedSuccess, code);
    });

} // namespace
