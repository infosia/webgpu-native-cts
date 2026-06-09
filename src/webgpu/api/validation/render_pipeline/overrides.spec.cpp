// Ported from gpuweb/cts src/webgpu/api/validation/render_pipeline/overrides.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// Note: isAsync=true sub-cases use the same synchronous validation path (harness has no async
// pipeline-creation wrapper). value,type_error tests treat NaN/Infinity as a validation error
// (no JS TypeError concept in the native C API).

#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"

using namespace cts;

namespace {

TestGroup<AllFeaturesMaxLimitsGpuTest> g = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,validation,render_pipeline,overrides",
    "Validation of pipeline overridable constants of createRenderPipeline.");

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

// Build a WGPUConstantEntry with an explicit-length key and a double value.
// The key is passed as string_view to support embedded null bytes or non-null-terminated strings.
WGPUConstantEntry makeConstantEntry(std::string_view key, double value) {
    WGPUConstantEntry entry = WGPU_CONSTANT_ENTRY_INIT;
    entry.key   = sv(key);
    entry.value = value;
    return entry;
}

static const char* kDefaultVertexShaderCode =
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

static const char* kDefaultFragmentShaderCode =
    "@fragment fn main() -> @location(0) vec4<f32> {\n"
    "  return vec4<f32>(0.0, 1.0, 0.0, 1.0);\n"
    "}\n";

// Build a render pipeline with:
//   - a vertex shader (custom code + constants)
//   - a fragment shader (custom code + constants)
//   - layout: auto (null layout → automatic)
//   - render target: rgba8unorm
// and call expectValidationError around createRenderPipeline.
void doCreateRenderPipelineTest(
    AllFeaturesMaxLimitsGpuTest& t,
    bool shouldError,
    std::string_view vertShaderCode,
    const std::vector<WGPUConstantEntry>& vertConstants,
    std::string_view fragShaderCode,
    const std::vector<WGPUConstantEntry>& fragConstants) {

    t.expectValidationError([&] {
        WGPUShaderModule vertModule = t.createShaderModuleTracked(vertShaderCode);
        WGPUShaderModule fragModule = t.createShaderModuleTracked(fragShaderCode);

        WGPUColorTargetState colorTarget = WGPU_COLOR_TARGET_STATE_INIT;
        colorTarget.format    = WGPUTextureFormat_RGBA8Unorm;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment.module        = fragModule;
        fragment.entryPoint    = sv("main");
        fragment.targetCount   = 1;
        fragment.targets       = &colorTarget;
        fragment.constantCount = fragConstants.size();
        fragment.constants     = fragConstants.empty() ? nullptr : fragConstants.data();

        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        // layout:auto — null after INIT selects automatic layout
        desc.vertex.module        = vertModule;
        desc.vertex.entryPoint    = sv("main");
        desc.vertex.constantCount = vertConstants.size();
        desc.vertex.constants     = vertConstants.empty() ? nullptr : vertConstants.data();
        desc.multisample.count    = 1;
        desc.fragment             = &fragment;

        t.createRenderPipelineTracked(desc);
    }, shouldError);
}

// Convenience overload: vertex with no overrides, custom fragment code and constants.
void doCreateRenderPipelineTestFragOnly(
    AllFeaturesMaxLimitsGpuTest& t,
    bool shouldError,
    std::string_view fragShaderCode,
    const std::vector<WGPUConstantEntry>& fragConstants) {
    doCreateRenderPipelineTest(
        t, shouldError,
        kDefaultVertexShaderCode, {},
        fragShaderCode, fragConstants);
}

// Convenience overload: custom vertex code + constants, default fragment.
void doCreateRenderPipelineTestVertOnly(
    AllFeaturesMaxLimitsGpuTest& t,
    bool shouldError,
    std::string_view vertShaderCode,
    const std::vector<WGPUConstantEntry>& vertConstants) {
    doCreateRenderPipelineTest(
        t, shouldError,
        vertShaderCode, vertConstants,
        kDefaultFragmentShaderCode, {});
}

// ---------------------------------------------------------------------------
// WGSL shaders used by multiple tests
// ---------------------------------------------------------------------------

// Vertex shader for identifier,vertex test.
// The séquençage override uses NFC encoding (é=\xc3\xa9, ç=\xc3\xa7) in the WGSL source.
// The subcase 11 key uses NFD encoding (e+\xcc\x81, c+\xcc\xa7), which does NOT match
// because WebGPU does not normalize unicode identifiers — so validation must fail.
static const char* kIdentifierVertexShader =
    "override x: f32 = 0.0;\n"
    "override y: f32 = 0.0;\n"
    "override \xe6\x95\xb0: f32 = 0.0;\n"              // 数 in UTF-8 (NFC)
    "override s" "\xc3" "\xa9" "quen" "\xc3" "\xa7" "age: f32 = 0.0;\n"  // séquençage NFC: é=\xc3\xa9, ç=\xc3\xa7
    "@id(1) override z: f32 = 0.0;\n"
    "@id(1000) override w: f32 = 1.0;\n"
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(x, y, z, w + \xe6\x95\xb0 + s" "\xc3" "\xa9" "quen" "\xc3" "\xa7" "age);\n"
    "}\n";

// Fragment shader for identifier,fragment test
// Note: the WGSL identifier is `sequencage` (no accents); the map key `séquençage` does NOT match.
static const char* kIdentifierFragmentShader =
    "override r: f32 = 0.0;\n"
    "override g: f32 = 0.0;\n"
    "override \xe6\x95\xb0: f32 = 0.0;\n"           // 数 in UTF-8
    "override sequencage: f32 = 0.0;\n"
    "@id(1) override b: f32 = 0.0;\n"
    "@id(1000) override a: f32 = 0.0;\n"
    "@fragment fn main() -> @location(0) vec4<f32> {\n"
    "    return vec4<f32>(r, g, b, a + \xe6\x95\xb0 + sequencage);\n"
    "}\n";

// Vertex shader for uninitialized,vertex test
static const char* kUninitializedVertexShader =
    "override x: f32;\n"
    "override y: f32 = 0.0;\n"
    "override z: f32;\n"
    "override w: f32 = 1.0;\n"
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  return vec4<f32>(x, y, z, w);\n"
    "}\n";

// Fragment shader for uninitialized,fragment test
static const char* kUninitializedFragmentShader =
    "override r: f32;\n"
    "override g: f32 = 0.0;\n"
    "override b: f32;\n"
    "override a: f32 = 0.0;\n"
    "@fragment fn main() -> @location(0) vec4<f32> {\n"
    "    return vec4<f32>(r, g, b, a);\n"
    "}\n";

// Vertex shader for value,type_error,vertex and value,validation_error,vertex tests
static const char* kValueVertexShader =
    "override cf: f32 = 0.0;\n"
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  _ = cf;\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// Vertex shader for value,validation_error,vertex test (bool + u32 + i32 + f32)
static const char* kValidationVertexShader =
    "override cb: bool = false;\n"
    "override cu: u32 = 0u;\n"
    "override ci: i32 = 0;\n"
    "override cf: f32 = 0.0;\n"
    "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
    "  _ = cb;\n"
    "  _ = cu;\n"
    "  _ = ci;\n"
    "  _ = cf;\n"
    "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
    "}\n";

// Fragment shader for value,type_error,fragment and value,validation_error,fragment tests
static const char* kValueFragmentShader =
    "override cf: f32 = 0.0;\n"
    "@fragment fn main() -> @location(0) vec4<f32> {\n"
    "    _ = cf;\n"
    "    return vec4<f32>(1.0, 1.0, 1.0, 1.0);\n"
    "}\n";

// Fragment shader for value,validation_error,fragment test (bool + u32 + i32 + f32)
static const char* kValidationFragmentShader =
    "override cb: bool = false;\n"
    "override cu: u32 = 0u;\n"
    "override ci: i32 = 0;\n"
    "override cf: f32 = 0.0;\n"
    "@fragment fn main() -> @location(0) vec4<f32> {\n"
    "    _ = cb;\n"
    "    _ = cu;\n"
    "    _ = ci;\n"
    "    _ = cf;\n"
    "    return vec4<f32>(1.0, 1.0, 1.0, 1.0);\n"
    "}\n";

// ---------------------------------------------------------------------------
// test: identifier,vertex
// ---------------------------------------------------------------------------
CTS_TEST(g, "identifier,vertex")
    .desc("Tests validation for overridable constants identifiers in vertex state.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                // subcaseIndex 0:  {} -> success
                ParamRecord{{"subcaseIndex", Value(0)}},
                // subcaseIndex 1:  {x:1, y:1} -> success
                ParamRecord{{"subcaseIndex", Value(1)}},
                // subcaseIndex 2:  {x:1, y:1, "1":1, "1000":1} -> success
                ParamRecord{{"subcaseIndex", Value(2)}},
                // subcaseIndex 3:  {"x\0":1, y:1} -> fail (embedded null)
                ParamRecord{{"subcaseIndex", Value(3)}},
                // subcaseIndex 4:  {xxx:1} -> fail (not declared)
                ParamRecord{{"subcaseIndex", Value(4)}},
                // subcaseIndex 5:  {"1":1} -> success (numeric id)
                ParamRecord{{"subcaseIndex", Value(5)}},
                // subcaseIndex 6:  {"2":1} -> fail (not declared)
                ParamRecord{{"subcaseIndex", Value(6)}},
                // subcaseIndex 7:  {z:1} -> fail (id specified for z)
                ParamRecord{{"subcaseIndex", Value(7)}},
                // subcaseIndex 8:  {w:1} -> fail (id specified for w)
                ParamRecord{{"subcaseIndex", Value(8)}},
                // subcaseIndex 9:  {"1":1, z:1} -> fail (id specified for z)
                ParamRecord{{"subcaseIndex", Value(9)}},
                // subcaseIndex 10: {数:1} -> success (non-ASCII)
                ParamRecord{{"subcaseIndex", Value(10)}},
                // subcaseIndex 11: {séquençage:0} -> fail (unicode normalization not applied)
                ParamRecord{{"subcaseIndex", Value(11)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int idx = t.param<int>("subcaseIndex");
        // (void) isAsync: no async pipeline path in harness
        (void)t.param<bool>("isAsync");

        // UTF-8 encoded strings for non-ASCII identifiers.
        // 数 = U+6570 = UTF-8 (NFC): \xe6\x95\xb0
        // x with embedded null: "x\0" = 2 bytes
        // séquençage subcase: the WGSL shader uses NFC form (é=\xc3\xa9, ç=\xc3\xa7).
        //   The key here uses the NFD decomposed form (e+combining-acute, c+combining-cedilla)
        //   which is byte-different from NFC, testing that WebGPU does NOT normalize unicode.
        //   NFD: e+U+0301(\xcc\x81), c+U+0327(\xcc\xa7)
        //   So NFD séquençage = "se\xcc\x81quen" + "c\xcc\xa7" + "age"
        static const std::string kNullX = std::string("x\0", 2); // NOLINT
        static const std::string kKanji = "\xe6\x95\xb0"; // 数 (NFC)
        static const std::string kSeq   = "se" "\xcc" "\x81" "quenc" "\xcc" "\xa7" "age"; // séquençage NFD

        struct IdentifierSubcaseData {
            std::vector<std::pair<std::string, double>> entries;
            bool success;
        };

        static const IdentifierSubcaseData kSubcases[] = {
            // 0: {} -> success
            { {}, true },
            // 1: {x:1, y:1} -> success
            { {{"x", 1.0}, {"y", 1.0}}, true },
            // 2: {x:1, y:1, "1":1, "1000":1} -> success
            { {{"x", 1.0}, {"y", 1.0}, {"1", 1.0}, {"1000", 1.0}}, true },
            // 3: {"x\0":1, y:1} -> fail (embedded null byte)
            { {{kNullX, 1.0}, {"y", 1.0}}, false },
            // 4: {xxx:1} -> fail (not declared)
            { {{"xxx", 1.0}}, false },
            // 5: {"1":1} -> success (numeric id for z)
            { {{"1", 1.0}}, true },
            // 6: {"2":1} -> fail (numeric id not declared)
            { {{"2", 1.0}}, false },
            // 7: {z:1} -> fail (pipeline constant id is specified for z)
            { {{"z", 1.0}}, false },
            // 8: {w:1} -> fail (pipeline constant id is specified for w)
            { {{"w", 1.0}}, false },
            // 9: {"1":1, z:1} -> fail (pipeline constant id is specified for z)
            { {{"1", 1.0}, {"z", 1.0}}, false },
            // 10: {数:1} -> success (non-ASCII identifier matches)
            { {{kKanji, 1.0}}, true },
            // 11: {séquençage:0} -> fail (séquençage != séquençage in shader: normalization not applied)
            { {{kSeq, 0.0}}, false },
        };

        const auto& sc = kSubcases[idx];
        std::vector<WGPUConstantEntry> entries;
        entries.reserve(sc.entries.size());
        for (const auto& [key, val] : sc.entries) {
            entries.push_back(makeConstantEntry(key, val));
        }

        doCreateRenderPipelineTestVertOnly(t, !sc.success, kIdentifierVertexShader, entries);
    });

// ---------------------------------------------------------------------------
// test: identifier,fragment
// ---------------------------------------------------------------------------
CTS_TEST(g, "identifier,fragment")
    .desc("Tests validation for overridable constants identifiers in fragment state.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                // subcaseIndex 0:  {} -> success
                ParamRecord{{"subcaseIndex", Value(0)}},
                // subcaseIndex 1:  {r:1, g:1} -> success
                ParamRecord{{"subcaseIndex", Value(1)}},
                // subcaseIndex 2:  {r:1, g:1, "1":1, "1000":1} -> success
                ParamRecord{{"subcaseIndex", Value(2)}},
                // subcaseIndex 3:  {"r\0":1} -> fail (embedded null)
                ParamRecord{{"subcaseIndex", Value(3)}},
                // subcaseIndex 4:  {xxx:1} -> fail (not declared)
                ParamRecord{{"subcaseIndex", Value(4)}},
                // subcaseIndex 5:  {"1":1} -> success (numeric id for b)
                ParamRecord{{"subcaseIndex", Value(5)}},
                // subcaseIndex 6:  {"2":1} -> fail (not declared)
                ParamRecord{{"subcaseIndex", Value(6)}},
                // subcaseIndex 7:  {b:1} -> fail (id specified for b)
                ParamRecord{{"subcaseIndex", Value(7)}},
                // subcaseIndex 8:  {a:1} -> fail (id specified for a)
                ParamRecord{{"subcaseIndex", Value(8)}},
                // subcaseIndex 9:  {"1":1, b:1} -> fail (id specified for b)
                ParamRecord{{"subcaseIndex", Value(9)}},
                // subcaseIndex 10: {数:1} -> success (non-ASCII)
                ParamRecord{{"subcaseIndex", Value(10)}},
                // subcaseIndex 11: {séquençage:0} -> fail (unicode normalization not applied)
                ParamRecord{{"subcaseIndex", Value(11)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        static const std::string kNullR = std::string("r\0", 2); // NOLINT
        static const std::string kKanji = "\xe6\x95\xb0"; // 数
        // séquençage: in the WGSL shader the identifier is `sequencage` (no accents),
        // so this key does not match → validation error.
        static const std::string kSeq   = "s" "\xc3" "\xa9" "quen" "\xc3" "\xa7" "age"; // séquençage

        struct FragIdentSubcaseData {
            std::vector<std::pair<std::string, double>> entries;
            bool success;
        };

        static const FragIdentSubcaseData kSubcases[] = {
            // 0: {} -> success
            { {}, true },
            // 1: {r:1, g:1} -> success
            { {{"r", 1.0}, {"g", 1.0}}, true },
            // 2: {r:1, g:1, "1":1, "1000":1} -> success
            { {{"r", 1.0}, {"g", 1.0}, {"1", 1.0}, {"1000", 1.0}}, true },
            // 3: {"r\0":1} -> fail (embedded null byte)
            { {{kNullR, 1.0}}, false },
            // 4: {xxx:1} -> fail (not declared)
            { {{"xxx", 1.0}}, false },
            // 5: {"1":1} -> success (numeric id for b)
            { {{"1", 1.0}}, true },
            // 6: {"2":1} -> fail (not declared)
            { {{"2", 1.0}}, false },
            // 7: {b:1} -> fail (pipeline constant id is specified for b)
            { {{"b", 1.0}}, false },
            // 8: {a:1} -> fail (pipeline constant id is specified for a)
            { {{"a", 1.0}}, false },
            // 9: {"1":1, b:1} -> fail (pipeline constant id is specified for b)
            { {{"1", 1.0}, {"b", 1.0}}, false },
            // 10: {数:1} -> success (non-ASCII identifier matches)
            { {{kKanji, 1.0}}, true },
            // 11: {séquençage:0} -> fail (unicode normalization not applied; shader uses sequencage)
            { {{kSeq, 0.0}}, false },
        };

        const auto& sc = kSubcases[idx];
        std::vector<WGPUConstantEntry> entries;
        entries.reserve(sc.entries.size());
        for (const auto& [key, val] : sc.entries) {
            entries.push_back(makeConstantEntry(key, val));
        }

        doCreateRenderPipelineTestFragOnly(t, !sc.success, kIdentifierFragmentShader, entries);
    });

// ---------------------------------------------------------------------------
// test: uninitialized,vertex
// ---------------------------------------------------------------------------
CTS_TEST(g, "uninitialized,vertex")
    .desc("Tests validation for uninitialized overridable constants in vertex state.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                // subcaseIndex 0: {} -> fail (x and z uninitialized)
                ParamRecord{{"subcaseIndex", Value(0)}},
                // subcaseIndex 1: {x:1, y:1} -> fail (z missing)
                ParamRecord{{"subcaseIndex", Value(1)}},
                // subcaseIndex 2: {x:1, z:1} -> success
                ParamRecord{{"subcaseIndex", Value(2)}},
                // subcaseIndex 3: {x:1, y:1, z:1, w:1} -> success
                ParamRecord{{"subcaseIndex", Value(3)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        struct UninitSubcaseData {
            std::vector<std::pair<std::string, double>> entries;
            bool success;
        };

        static const UninitSubcaseData kSubcases[] = {
            // 0: {} -> fail
            { {}, false },
            // 1: {x:1, y:1} -> fail (z missing)
            { {{"x", 1.0}, {"y", 1.0}}, false },
            // 2: {x:1, z:1} -> success
            { {{"x", 1.0}, {"z", 1.0}}, true },
            // 3: {x:1, y:1, z:1, w:1} -> success
            { {{"x", 1.0}, {"y", 1.0}, {"z", 1.0}, {"w", 1.0}}, true },
        };

        const auto& sc = kSubcases[idx];
        std::vector<WGPUConstantEntry> entries;
        entries.reserve(sc.entries.size());
        for (const auto& [key, val] : sc.entries) {
            entries.push_back(makeConstantEntry(key, val));
        }

        doCreateRenderPipelineTestVertOnly(t, !sc.success, kUninitializedVertexShader, entries);
    });

// ---------------------------------------------------------------------------
// test: uninitialized,fragment
// ---------------------------------------------------------------------------
CTS_TEST(g, "uninitialized,fragment")
    .desc("Tests validation for uninitialized overridable constants in fragment state.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                // subcaseIndex 0: {} -> fail (r and b uninitialized)
                ParamRecord{{"subcaseIndex", Value(0)}},
                // subcaseIndex 1: {r:1, g:1} -> fail (b missing)
                ParamRecord{{"subcaseIndex", Value(1)}},
                // subcaseIndex 2: {r:1, b:1} -> success
                ParamRecord{{"subcaseIndex", Value(2)}},
                // subcaseIndex 3: {r:1, g:1, b:1, a:1} -> success
                ParamRecord{{"subcaseIndex", Value(3)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        struct UninitFragSubcaseData {
            std::vector<std::pair<std::string, double>> entries;
            bool success;
        };

        static const UninitFragSubcaseData kSubcases[] = {
            // 0: {} -> fail
            { {}, false },
            // 1: {r:1, g:1} -> fail (b missing)
            { {{"r", 1.0}, {"g", 1.0}}, false },
            // 2: {r:1, b:1} -> success
            { {{"r", 1.0}, {"b", 1.0}}, true },
            // 3: {r:1, g:1, b:1, a:1} -> success
            { {{"r", 1.0}, {"g", 1.0}, {"b", 1.0}, {"a", 1.0}}, true },
        };

        const auto& sc = kSubcases[idx];
        std::vector<WGPUConstantEntry> entries;
        entries.reserve(sc.entries.size());
        for (const auto& [key, val] : sc.entries) {
            entries.push_back(makeConstantEntry(key, val));
        }

        doCreateRenderPipelineTestFragOnly(t, !sc.success, kUninitializedFragmentShader, entries);
    });

// ---------------------------------------------------------------------------
// test: value,type_error,vertex
// Tests that invalid constant values like NaN and Infinity cause a validation
// error. (In JS these cause a TypeError; in the native C API they produce a
// validation error at pipeline creation time.)
// ---------------------------------------------------------------------------
CTS_TEST(g, "value,type_error,vertex")
    .desc(
        "Tests that invalid constant values (NaN, Infinity) cause a validation error "
        "in the vertex stage. (Native C maps TypeError to a validation error.)")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                // subcaseIndex 0: {cf:1} -> success (control)
                ParamRecord{{"subcaseIndex", Value(0)}},
                // subcaseIndex 1: {cf:NaN} -> fail
                ParamRecord{{"subcaseIndex", Value(1)}},
                // subcaseIndex 2: {cf:+Inf} -> fail
                ParamRecord{{"subcaseIndex", Value(2)}},
                // subcaseIndex 3: {cf:-Inf} -> fail
                ParamRecord{{"subcaseIndex", Value(3)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        // Double values for the 4 subcases; avoid static local init ordering issues.
        double value = 0.0;
        bool success = false;
        switch (idx) {
            case 0: value = 1.0;                                           success = true;  break;
            case 1: value = std::numeric_limits<double>::quiet_NaN();      success = false; break;
            case 2: value = std::numeric_limits<double>::infinity();       success = false; break;
            case 3: value = -std::numeric_limits<double>::infinity();      success = false; break;
            default: t.fail("unexpected subcaseIndex"); return;
        }

        std::vector<WGPUConstantEntry> entries = {makeConstantEntry("cf", value)};
        doCreateRenderPipelineTestVertOnly(t, !success, kValueVertexShader, entries);
    });

// ---------------------------------------------------------------------------
// test: value,type_error,fragment
// ---------------------------------------------------------------------------
CTS_TEST(g, "value,type_error,fragment")
    .desc(
        "Tests that invalid constant values (NaN, Infinity) cause a validation error "
        "in the fragment stage.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"subcaseIndex", Value(0)}},
                ParamRecord{{"subcaseIndex", Value(1)}},
                ParamRecord{{"subcaseIndex", Value(2)}},
                ParamRecord{{"subcaseIndex", Value(3)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        double value = 0.0;
        bool success = false;
        switch (idx) {
            case 0: value = 1.0;                                           success = true;  break;
            case 1: value = std::numeric_limits<double>::quiet_NaN();      success = false; break;
            case 2: value = std::numeric_limits<double>::infinity();       success = false; break;
            case 3: value = -std::numeric_limits<double>::infinity();      success = false; break;
            default: t.fail("unexpected subcaseIndex"); return;
        }

        std::vector<WGPUConstantEntry> entries = {makeConstantEntry("cf", value)};
        doCreateRenderPipelineTestFragOnly(t, !success, kValueFragmentShader, entries);
    });

// ---------------------------------------------------------------------------
// test: value,validation_error,vertex
// Tests that out-of-range constant values cause a validation error.
// kValue boundary constants (computed from upstream kBit):
//   u32: min=0, max=4294967295
//   i32: negative.min=-2147483648, positive.max=2147483647
//   f32: positive.max=3.4028234663852886e+38, negative.min=-3.4028234663852886e+38
//   f32 first_non_castable = FLT_MAX/2 + 2^127 = 3.4028235677973366e+38
// ---------------------------------------------------------------------------
CTS_TEST(g, "value,validation_error,vertex")
    .desc(
        "Tests that unrepresentable constant values in the vertex stage cause a validation error.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"subcaseIndex", Value(0)}},
                ParamRecord{{"subcaseIndex", Value(1)}},
                ParamRecord{{"subcaseIndex", Value(2)}},
                ParamRecord{{"subcaseIndex", Value(3)}},
                ParamRecord{{"subcaseIndex", Value(4)}},
                ParamRecord{{"subcaseIndex", Value(5)}},
                ParamRecord{{"subcaseIndex", Value(6)}},
                ParamRecord{{"subcaseIndex", Value(7)}},
                ParamRecord{{"subcaseIndex", Value(8)}},
                ParamRecord{{"subcaseIndex", Value(9)}},
                ParamRecord{{"subcaseIndex", Value(10)}},
                ParamRecord{{"subcaseIndex", Value(11)}},
                ParamRecord{{"subcaseIndex", Value(12)}},
                ParamRecord{{"subcaseIndex", Value(13)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        // Boundary values (from kValue in upstream CTS)
        // u32: [0, 4294967295]
        // i32: [-2147483648, 2147483647]
        // f32: [-3.4028234663852886e+38, 3.4028234663852886e+38]
        // f32 first_non_castable_pipeline_override = FLT_MAX/2 + 2^127 = 3.4028235677973366e+38
        static const double kU32Min     = 0.0;
        static const double kU32MinM1   = -1.0;         // u32.min - 1
        static const double kU32Max     = 4294967295.0; // 0xffffffff
        static const double kU32MaxP1   = 4294967296.0; // 0xffffffff + 1
        static const double kI32NMin    = -2147483648.0;
        static const double kI32NMinM1  = -2147483649.0;
        static const double kI32PMax    = 2147483647.0;
        static const double kI32PMaxP1  = 2147483648.0;
        static const double kF32PMax    = 3.4028234663852886e+38;
        static const double kF32NMin    = -3.4028234663852886e+38;
        static const double kF32PFncp   = 3.4028235677973366e+38;  // first_non_castable
        static const double kF32NFncp   = -3.4028235677973366e+38; // negative.first_non_castable
        // Conversion to boolean can't fail — using extreme values
        static const double kDoubleMax  = std::numeric_limits<double>::max();

        struct ValErrSubcase {
            std::string key;
            double value;
            bool success;
        };

        static const ValErrSubcase kSubcases[] = {
            // 0:  cu = u32.min -> success
            { "cu", kU32Min,    true  },
            // 1:  cu = u32.min - 1 -> fail
            { "cu", kU32MinM1,  false },
            // 2:  cu = u32.max -> success
            { "cu", kU32Max,    true  },
            // 3:  cu = u32.max + 1 -> fail
            { "cu", kU32MaxP1,  false },
            // 4:  ci = i32.negative.min -> success
            { "ci", kI32NMin,   true  },
            // 5:  ci = i32.negative.min - 1 -> fail
            { "ci", kI32NMinM1, false },
            // 6:  ci = i32.positive.max -> success
            { "ci", kI32PMax,   true  },
            // 7:  ci = i32.positive.max + 1 -> fail
            { "ci", kI32PMaxP1, false },
            // 8:  cf = f32.negative.min -> success
            { "cf", kF32NMin,   true  },
            // 9:  cf = f32.negative.first_non_castable -> fail
            { "cf", kF32NFncp,  false },
            // 10: cf = f32.positive.max -> success
            { "cf", kF32PMax,   true  },
            // 11: cf = f32.positive.first_non_castable -> fail
            { "cf", kF32PFncp,  false },
            // 12: cb = Number.MAX_VALUE -> success (bool conversion can't fail)
            { "cb", kDoubleMax, true  },
            // 13: cb = i32.negative.min - 1 -> success (bool conversion can't fail)
            { "cb", kI32NMinM1, true  },
        };

        const auto& sc = kSubcases[idx];
        std::vector<WGPUConstantEntry> entries = {makeConstantEntry(sc.key, sc.value)};
        doCreateRenderPipelineTestVertOnly(t, !sc.success, kValidationVertexShader, entries);
    });

// ---------------------------------------------------------------------------
// test: value,validation_error,fragment
// ---------------------------------------------------------------------------
CTS_TEST(g, "value,validation_error,fragment")
    .desc(
        "Tests that unrepresentable constant values in the fragment stage cause a validation error.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"subcaseIndex", Value(0)}},
                ParamRecord{{"subcaseIndex", Value(1)}},
                ParamRecord{{"subcaseIndex", Value(2)}},
                ParamRecord{{"subcaseIndex", Value(3)}},
                ParamRecord{{"subcaseIndex", Value(4)}},
                ParamRecord{{"subcaseIndex", Value(5)}},
                ParamRecord{{"subcaseIndex", Value(6)}},
                ParamRecord{{"subcaseIndex", Value(7)}},
                ParamRecord{{"subcaseIndex", Value(8)}},
                ParamRecord{{"subcaseIndex", Value(9)}},
                ParamRecord{{"subcaseIndex", Value(10)}},
                ParamRecord{{"subcaseIndex", Value(11)}},
                ParamRecord{{"subcaseIndex", Value(12)}},
                ParamRecord{{"subcaseIndex", Value(13)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        static const double kU32Min     = 0.0;
        static const double kU32MinM1   = -1.0;
        static const double kU32Max     = 4294967295.0;
        static const double kU32MaxP1   = 4294967296.0;
        static const double kI32NMin    = -2147483648.0;
        static const double kI32NMinM1  = -2147483649.0;
        static const double kI32PMax    = 2147483647.0;
        static const double kI32PMaxP1  = 2147483648.0;
        static const double kF32PMax    = 3.4028234663852886e+38;
        static const double kF32NMin    = -3.4028234663852886e+38;
        static const double kF32PFncp   = 3.4028235677973366e+38;
        static const double kF32NFncp   = -3.4028235677973366e+38;
        static const double kDoubleMax  = std::numeric_limits<double>::max();

        struct ValErrFragSubcase {
            std::string key;
            double value;
            bool success;
        };

        static const ValErrFragSubcase kSubcases[] = {
            { "cu", kU32Min,    true  },
            { "cu", kU32MinM1,  false },
            { "cu", kU32Max,    true  },
            { "cu", kU32MaxP1,  false },
            { "ci", kI32NMin,   true  },
            { "ci", kI32NMinM1, false },
            { "ci", kI32PMax,   true  },
            { "ci", kI32PMaxP1, false },
            { "cf", kF32NMin,   true  },
            { "cf", kF32NFncp,  false },
            { "cf", kF32PMax,   true  },
            { "cf", kF32PFncp,  false },
            { "cb", kDoubleMax, true  },
            { "cb", kI32NMinM1, true  },
        };

        const auto& sc = kSubcases[idx];
        std::vector<WGPUConstantEntry> entries = {makeConstantEntry(sc.key, sc.value)};
        doCreateRenderPipelineTestFragOnly(t, !sc.success, kValidationFragmentShader, entries);
    });

// ---------------------------------------------------------------------------
// test: value,validation_error,f16,vertex
// Gated on WGPUFeatureName_ShaderF16.
// f16: positive.max = 65504.0, negative.min = -65504.0
// f16 first_non_castable = f16_max/2 + 2^15 = 65504/2 + 32768 = 32752 + 32768 = 65520
// f32.positive.max and f32.negative.min are also out-of-range for f16.
// ---------------------------------------------------------------------------
CTS_TEST(g, "value,validation_error,f16,vertex")
    .desc(
        "Tests that unrepresentable f16 constant values in the vertex stage cause a validation "
        "error. Requires shader-f16 feature.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"subcaseIndex", Value(0)}},
                ParamRecord{{"subcaseIndex", Value(1)}},
                ParamRecord{{"subcaseIndex", Value(2)}},
                ParamRecord{{"subcaseIndex", Value(3)}},
                ParamRecord{{"subcaseIndex", Value(4)}},
                ParamRecord{{"subcaseIndex", Value(5)}},
                ParamRecord{{"subcaseIndex", Value(6)}},
                ParamRecord{{"subcaseIndex", Value(7)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }

        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        // f16 boundaries
        static const double kF16PMax   = 65504.0;
        static const double kF16NMin   = -65504.0;
        static const double kF16PFncp  = 65520.0; // f16.positive.first_non_castable_pipeline_override
        static const double kF16NFncp  = -65520.0;
        // f32 values are out of f16 range
        static const double kF32PMax   = 3.4028234663852886e+38;
        static const double kF32NMin   = -3.4028234663852886e+38;
        static const double kF32PFncp  = 3.4028235677973366e+38;
        static const double kF32NFncp  = -3.4028235677973366e+38;

        struct F16VertSubcase {
            double value;
            bool success;
        };

        static const F16VertSubcase kSubcases[] = {
            { kF16NMin,  true  }, // 0: f16.negative.min -> success
            { kF16NFncp, false }, // 1: f16.negative.first_non_castable -> fail
            { kF16PMax,  true  }, // 2: f16.positive.max -> success
            { kF16PFncp, false }, // 3: f16.positive.first_non_castable -> fail
            { kF32NMin,  false }, // 4: f32.negative.min (out of f16 range) -> fail
            { kF32PMax,  false }, // 5: f32.positive.max (out of f16 range) -> fail
            { kF32NFncp, false }, // 6: f32.negative.first_non_castable -> fail
            { kF32PFncp, false }, // 7: f32.positive.first_non_castable -> fail
        };

        static const char* kF16VertexShader =
            "enable f16;\n"
            "\n"
            "override cf16: f16 = 0.0h;\n"
            "@vertex fn main() -> @builtin(position) vec4<f32> {\n"
            "  _ = cf16;\n"
            "  return vec4<f32>(0.0, 0.0, 0.0, 1.0);\n"
            "}\n";

        const auto& sc = kSubcases[idx];
        std::vector<WGPUConstantEntry> entries = {makeConstantEntry("cf16", sc.value)};
        doCreateRenderPipelineTestVertOnly(t, !sc.success, kF16VertexShader, entries);
    });

// ---------------------------------------------------------------------------
// test: value,validation_error,f16,fragment
// ---------------------------------------------------------------------------
CTS_TEST(g, "value,validation_error,f16,fragment")
    .desc(
        "Tests that unrepresentable f16 constant values in the fragment stage cause a validation "
        "error. Requires shader-f16 feature.")
    .params([](ParamsBuilder u) {
        return u
            .combine("isAsync", {Value(false), Value(true)})
            .beginSubcases()
            .combineWithParams({
                ParamRecord{{"subcaseIndex", Value(0)}},
                ParamRecord{{"subcaseIndex", Value(1)}},
                ParamRecord{{"subcaseIndex", Value(2)}},
                ParamRecord{{"subcaseIndex", Value(3)}},
                ParamRecord{{"subcaseIndex", Value(4)}},
                ParamRecord{{"subcaseIndex", Value(5)}},
                ParamRecord{{"subcaseIndex", Value(6)}},
                ParamRecord{{"subcaseIndex", Value(7)}},
            });
    })
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        if (!wgpuDeviceHasFeature(t.device(), WGPUFeatureName_ShaderF16)) {
            t.skip("shader-f16 feature not available");
        }

        const int idx = t.param<int>("subcaseIndex");
        (void)t.param<bool>("isAsync");

        static const double kF16PMax   = 65504.0;
        static const double kF16NMin   = -65504.0;
        static const double kF16PFncp  = 65520.0;
        static const double kF16NFncp  = -65520.0;
        static const double kF32PMax   = 3.4028234663852886e+38;
        static const double kF32NMin   = -3.4028234663852886e+38;
        static const double kF32PFncp  = 3.4028235677973366e+38;
        static const double kF32NFncp  = -3.4028235677973366e+38;

        struct F16FragSubcase {
            double value;
            bool success;
        };

        static const F16FragSubcase kSubcases[] = {
            { kF16NMin,  true  }, // 0: f16.negative.min -> success
            { kF16NFncp, false }, // 1: f16.negative.first_non_castable -> fail
            { kF16PMax,  true  }, // 2: f16.positive.max -> success
            { kF16PFncp, false }, // 3: f16.positive.first_non_castable -> fail
            { kF32NMin,  false }, // 4: f32.negative.min (out of f16 range) -> fail
            { kF32PMax,  false }, // 5: f32.positive.max (out of f16 range) -> fail
            { kF32NFncp, false }, // 6: f32.negative.first_non_castable -> fail
            { kF32PFncp, false }, // 7: f32.positive.first_non_castable -> fail
        };

        static const char* kF16FragmentShader =
            "enable f16;\n"
            "\n"
            "override cf16: f16 = 0.0h;\n"
            "@fragment fn main() -> @location(0) vec4<f32> {\n"
            "    _ = cf16;\n"
            "    return vec4<f32>(1.0, 1.0, 1.0, 1.0);\n"
            "}\n";

        const auto& sc = kSubcases[idx];
        std::vector<WGPUConstantEntry> entries = {makeConstantEntry("cf16", sc.value)};
        doCreateRenderPipelineTestFragOnly(t, !sc.success, kF16FragmentShader, entries);
    });

} // namespace
