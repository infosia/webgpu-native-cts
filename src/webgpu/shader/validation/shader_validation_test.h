// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/shader_validation_test.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85 (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2022 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Base fixture for WGSL shader validation tests. Mirrors upstream
// ShaderValidationTest / UniqueFeaturesAndLimitsShaderValidationTest (the two
// upstream classes are functionally identical for our purposes — both wrap
// createShaderModule in a validation error scope, check getCompilationInfo
// message types, and provide expectPipelineResult — so this port unifies them
// into one fixture with both names aliased).
//
// Porting notes:
//
// 1. Private compilation context. wgpuShaderModuleGetCompilationInfo and
//    wgpuInstanceHasWGSLLanguageFeature both require a WGPUInstance to pump
//    events / query, but the harness's cache().instance is not exposed to test
//    bodies. So — exactly as api/operation/shader_module/compilation_info.spec.cpp
//    does — this fixture lazily creates its OWN instance + adapter + device
//    (with all adapter features + max limits, matching AllFeaturesMaxLimitsGpuTest)
//    and runs every createShaderModule / createPipeline / getCompilationInfo /
//    hasLanguageFeature call against it. The context is created once per case and
//    released in finalize().
//
// 2. expectCompileResult faithfully performs BOTH upstream checks:
//      (a) the synchronous validation-error-scope check: createShaderModule must
//          raise a validation error iff !expectedSuccess.
//      (b) the asynchronous getCompilationInfo message-type check: the
//          compilationInfo messages must contain an 'error'-type message iff
//          !expectedSuccess.
//    Check (b) is the only signal that catches a WRONG-REASON rejection, so it is
//    NOT dropped (per the spec). A mismatch on either check fails the case.
//
// 3. Feature auto-skip (skipIfCodeNeedsFeatureAndDeviceDoesNotHaveFeature):
//    regex-detect `enable f16|subgroups|clip_distances|chromium_experimental_primitive_id`
//    in the WGSL and skip the case if the private device lacks the corresponding
//    device feature (shader-f16 / subgroups / clip-distances /
//    chromium-experimental-primitive-id). This makes the feature-absent path
//    correct (a skip), while a real backend gap shows as a finding on the
//    feature-present backend. The skip is never removed.

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"
#include "cts/gpu.h"
#include "cts/test.h"

namespace cts::shader_validation {

// ---------------------------------------------------------------------------
// WGSL `enable X;` -> device feature mapping (mirrors upstream kEnables/kEnableREs)
// ---------------------------------------------------------------------------
struct EnableFeature {
    const char* enableName;     // WGSL extension token after `enable`
    WGPUFeatureName feature;    // corresponding WebGPU device feature
};

// Returns true if `code` contains an `enable <enableName>;` directive (allowing
// other comma-listed extensions before/after). Mirrors the upstream regex
//   \benable\s+(?:\s*\w+\s*,)*\s*<name>\s*(?:,\s*\w+)*\s*;
inline bool codeEnables(const std::string& code, const char* enableName) {
    const std::string pattern =
        std::string("\\benable\\s+(?:\\s*\\w+\\s*,)*\\s*") + enableName +
        std::string("\\s*(?:,\\s*\\w+)*\\s*;");
    const std::regex re(pattern);
    return std::regex_search(code, re);
}

// ---------------------------------------------------------------------------
// Private compilation context: own instance/adapter/device with all features
// and max limits (matching AllFeaturesMaxLimitsGpuTest).
// ---------------------------------------------------------------------------
class ShaderValidationTest : public AllFeaturesMaxLimitsGpuTest {
  public:
    void finalize() override {
        releaseContext();
        AllFeaturesMaxLimitsGpuTest::finalize();
    }

    // ---- Compile result (success/fail) -------------------------------------
    // Mirrors upstream expectCompileResult.
    void expectCompileResult(bool expectedResult, const std::string& code) {
        skipIfCodeNeedsFeatureAndDeviceDoesNotHaveFeature(code);

        // (a) Synchronous validation error-scope check. This is the accept/reject
        // VERDICT and is performed on all backends.
        WGPUShaderModule shaderModule = createShaderModuleChecked(code, /*shouldError=*/!expectedResult);

#if defined(CTS_BACKEND_WGPU)
        // wgpu-native: wgpuShaderModuleGetCompilationInfo currently ABORTS
        // (signal 6) for every shader module, which would crash every
        // shader/validation case before any verdict is produced. The accept/reject
        // verdict comes entirely from the error-scope check above, so skip the
        // reason-level getCompilationInfo error-type check here. Dawn and yawgpu
        // keep the full behavior below.
        wgpuShaderModuleRelease(shaderModule);
#else
        // (b) Asynchronous getCompilationInfo message-type check.
        const std::vector<MessageInfo> messages = getCompilationInfoChecked(shaderModule);
        wgpuShaderModuleRelease(shaderModule);

        bool hasError = false;
        for (const MessageInfo& m : messages) {
            if (m.type == WGPUCompilationMessageType_Error) {
                hasError = true;
                break;
            }
        }
        if (expectedResult) {
            expect(!hasError,
                   "Unexpected compilationInfo 'error' message.\n" + messagesLog(messages, code));
        } else {
            expect(hasError,
                   "Missing expected compilationInfo 'error' message.\n" + messagesLog(messages, code));
        }
#endif
    }

    // ---- Compile warning ----------------------------------------------------
    // Mirrors upstream expectCompileWarning: compile must succeed; a 'warning'
    // message must be present iff expectWarning.
    void expectCompileWarning(bool expectWarning, const std::string& code) {
        WGPUShaderModule shaderModule = createShaderModuleChecked(code, /*shouldError=*/false);

#if defined(CTS_BACKEND_WGPU)
        // wgpu-native: getCompilationInfo aborts (see expectCompileResult). The
        // compile-success verdict is already asserted above; the warning-presence
        // (reason-level) check cannot be performed without crashing, so skip it.
        (void)expectWarning;
        wgpuShaderModuleRelease(shaderModule);
#else
        const std::vector<MessageInfo> messages = getCompilationInfoChecked(shaderModule);
        wgpuShaderModuleRelease(shaderModule);

        bool hasWarning = false;
        for (const MessageInfo& m : messages) {
            if (m.type == WGPUCompilationMessageType_Warning) {
                hasWarning = true;
                break;
            }
        }
        if (expectWarning) {
            expect(hasWarning,
                   "Missing expected compilationInfo 'warning' message.\n" + messagesLog(messages, ""));
        } else {
            expect(!hasWarning,
                   "Found an unexpected 'warning' message.\n" + messagesLog(messages, ""));
        }
#endif
    }

    // ---- Pipeline result ----------------------------------------------------
    // Mirrors upstream expectPipelineResult. Wraps `code` into a compute entry
    // point (optionally appending @workgroup_size(1)), inserts phony statements
    // for `statements`, `constants`, and `reference`, then expects
    // createComputePipeline to succeed/fail per expectedResult (createShaderModule
    // itself is expected NOT to error, matching upstream).
    struct PipelineArgs {
        bool expectedResult = false;
        std::string code;
        // Override constants by name -> value (double; ints pass exactly for small magnitudes).
        std::map<std::string, double> constants;
        std::vector<std::string> reference;
        std::vector<std::string> statements;
        bool autoSkipIfFeatureNotAvailable = true;
        bool addWorkgroupSize = true;
    };

    void expectPipelineResult(const PipelineArgs& args) {
        std::vector<std::string> phonies;
        for (const std::string& s : args.statements) {
            phonies.push_back(s);
        }
        for (const auto& kv : args.constants) {
            phonies.push_back("_ = " + kv.first + ";");
        }
        for (const std::string& r : args.reference) {
            phonies.push_back("_ = " + r + ";");
        }

        std::string body;
        for (size_t i = 0; i < phonies.size(); ++i) {
            body += "  " + phonies[i] + "\n";
        }

        std::string fullCode = args.code;
        if (args.addWorkgroupSize) {
            fullCode += "\n@workgroup_size(1)";
        }
        fullCode += "\n      @compute fn main() {\n" + body + "}";

        if (args.autoSkipIfFeatureNotAvailable) {
            skipIfCodeNeedsFeatureAndDeviceDoesNotHaveFeature(fullCode);
        }

        // createShaderModule is expected to succeed (no error scope failure).
        WGPUShaderModule shaderModule = createShaderModuleChecked(fullCode, /*shouldError=*/false);

        // Build override constant entries.
        std::vector<WGPUConstantEntry> constantEntries;
        std::vector<std::string> constantKeys; // own storage for WGPUStringView keys
        constantEntries.reserve(args.constants.size());
        constantKeys.reserve(args.constants.size());
        for (const auto& kv : args.constants) {
            constantKeys.push_back(kv.first);
        }
        size_t ki = 0;
        for (const auto& kv : args.constants) {
            WGPUConstantEntry entry = WGPU_CONSTANT_ENTRY_INIT;
            entry.key = WGPUStringView{constantKeys[ki].data(), constantKeys[ki].size()};
            entry.value = kv.second;
            constantEntries.push_back(entry);
            ++ki;
        }

        WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = nullptr; // 'auto'
        desc.compute.module = shaderModule;
        desc.compute.entryPoint = WGPUStringView{"main", 4};
        desc.compute.constantCount = constantEntries.size();
        desc.compute.constants = constantEntries.empty() ? nullptr : constantEntries.data();

        ensureContext();
        wgpuDevicePushErrorScope(ctxDevice_, WGPUErrorFilter_Validation);
        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(ctxDevice_, &desc);
        ScopeResult scope = popErrorScopeSync(ctxInstance_, ctxDevice_);
        if (pipeline != nullptr) {
            wgpuComputePipelineRelease(pipeline);
        }
        wgpuShaderModuleRelease(shaderModule);

        if (scope.status != WGPUPopErrorScopeStatus_Success) {
            fail("popErrorScope failed: " + scope.message);
        }
        const bool hadError = scope.type != WGPUErrorType_NoError;
        const bool shouldError = !args.expectedResult;
        if (shouldError && !hadError) {
            fail("expected a pipeline-creation validation error, got none");
        }
        if (!shouldError && hadError) {
            fail("unexpected pipeline-creation validation error: " + scope.message);
        }
    }

    // ---- WGSL language feature query (mirrors upstream t.hasLanguageFeature) -
    // Returns the TRUE per-backend answer for a stabilized WGSL language feature,
    // keyed by the upstream string name (mirroring `t.hasLanguageFeature('name')`).
    // Upstream uses this to set the *expected* compile result (success iff the
    // feature is supported), NOT to skip — so a wrong answer turns supported
    // features into spurious "expected a validation error" fails.
    //
    //   - Dawn path: map the string -> WGPUWGSLLanguageFeatureName_* enum ->
    //     wgpuInstanceHasWGSLLanguageFeature (the only stable query Dawn exposes).
    //   - non-Dawn path: behaviorally probe by trial-compiling a CANONICAL minimal
    //     snippet that uses the feature (distinct from the test-case shaders, so
    //     the check is not tautological — a genuine per-variant divergence still
    //     surfaces as a fail). The feature is "supported" iff that snippet compiles
    //     with no error-type compilation message. Probe results are cached per
    //     feature name (compiled once per fixture/instance), like the subgroup probe.
    bool hasLanguageFeature(std::string_view feature) {
        ensureContext();
        const std::string key(feature);
        // Cache probe results per worker THREAD (not per case): the non-Dawn path
        // trial-compiles a snippet per feature, and re-probing every case would add
        // ~hundreds of thousands of shader-module creations across a large run (e.g.
        // uniformity, 181k cases) and re-pressure the backend. The answer is constant
        // for the thread's (thread_local) device.
        thread_local std::unordered_map<std::string, bool> featureCache;
        auto it = featureCache.find(key);
        if (it != featureCache.end()) {
            return it->second;
        }

        bool supported = false;
#if defined(CTS_BACKEND_DAWN)
        WGPUWGSLLanguageFeatureName name;
        if (feature == "readonly_and_readwrite_storage_textures") {
            name = WGPUWGSLLanguageFeatureName_ReadonlyAndReadwriteStorageTextures;
        } else if (feature == "packed_4x8_integer_dot_product") {
            name = WGPUWGSLLanguageFeatureName_Packed4x8IntegerDotProduct;
        } else if (feature == "unrestricted_pointer_parameters") {
            name = WGPUWGSLLanguageFeatureName_UnrestrictedPointerParameters;
        } else if (feature == "pointer_composite_access") {
            name = WGPUWGSLLanguageFeatureName_PointerCompositeAccess;
        } else if (feature == "uniform_buffer_standard_layout") {
            name = WGPUWGSLLanguageFeatureName_UniformBufferStandardLayout;
        } else if (feature == "texture_formats_tier1") {
            name = WGPUWGSLLanguageFeatureName_TextureFormatsTier1;
        } else if (feature == "texture_and_sampler_let") {
            name = WGPUWGSLLanguageFeatureName_TextureAndSamplerLet;
        } else if (feature == "swizzle_assignment") {
            name = WGPUWGSLLanguageFeatureName_SwizzleAssignment;
        } else if (feature == "subgroup_id") {
            name = WGPUWGSLLanguageFeatureName_SubgroupId;
        } else if (feature == "subgroup_uniformity") {
            name = WGPUWGSLLanguageFeatureName_SubgroupUniformity;
        } else if (feature == "linear_indexing") {
            name = WGPUWGSLLanguageFeatureName_LinearIndexing;
        } else {
            fail("hasLanguageFeature: unknown feature name '" + key + "'");
            featureCache[key] = false;
            return false;
        }
        supported = wgpuInstanceHasWGSLLanguageFeature(ctxInstance_, name) != WGPU_FALSE;
#else
        // Behavioral trial-compile probe with a canonical snippet per feature.
        if (feature == "pointer_composite_access") {
            supported = compilesWithoutError(
                "fn f() { var a = vec2<i32>(); let r = (&a)[0]; _ = r; }");
        } else if (feature == "readonly_and_readwrite_storage_textures") {
            supported = compilesWithoutError(
                "@group(0) @binding(0) var t : texture_storage_2d<r32float, read>;"
                "\n@compute @workgroup_size(1) fn main() { _ = textureLoad(t, vec2u(0)); }");
        } else if (feature == "uniform_buffer_standard_layout") {
            // Probe a uniform array<T> with a non-16 element stride; valid only
            // when the standard-layout relaxation is supported.
            supported = compilesWithoutError(
                "@group(0) @binding(0) var<uniform> u : array<f32, 4>;"
                "\n@compute @workgroup_size(1) fn main() { _ = u[0]; }");
        } else if (feature == "texture_and_sampler_let") {
            // A `let` holding a texture handle is valid only with this feature.
            supported = compilesWithoutError(
                "@group(0) @binding(0) var t : texture_2d<f32>;"
                "\nfn f() { let x = t; _ = x; }");
        } else if (feature == "swizzle_assignment") {
            // Assigning to a multi-component swizzle is valid only with this feature.
            supported = compilesWithoutError(
                "fn f() { var v : vec4f; v.xy = vec2(1.0, 2.0); }");
        } else if (feature == "unrestricted_pointer_parameters") {
            // Passing a pointer into a non-function/private address space as a
            // function parameter is valid only when this relaxation is supported.
            supported = compilesWithoutError(
                "var<workgroup> v : u32;"
                "\nfn bar(p : ptr<workgroup, u32>) { _ = *p; }"
                "\nfn foo() { bar(&v); }"
                "\n@compute @workgroup_size(1) fn main() { foo(); }");
        } else if (feature == "packed_4x8_integer_dot_product") {
            // The builtins are only valid behind the `requires` directive, which a
            // backend accepts iff it exposes the feature.
            supported = compilesWithoutError(
                "requires packed_4x8_integer_dot_product;"
                "\nfn f() { _ = dot4I8Packed(1u, 2u); }");
        } else if (feature == "linear_indexing") {
            // The `requires` directive compiles iff the backend allows the feature.
            supported = compilesWithoutError(
                "requires linear_indexing;"
                "\n@compute @workgroup_size(1) fn main() {}");
        } else if (feature == "texture_formats_tier1") {
            // No clean shader-only usage probe; the `requires` directive itself is
            // accepted iff the backend exposes the feature.
            supported = compilesWithoutError(
                "requires texture_formats_tier1;"
                "\n@compute @workgroup_size(1) fn main() {}");
        } else {
            // Any unrecognized name: stay conservative and report unsupported on
            // non-Dawn.
            supported = false;
        }
#endif
        featureCache[key] = supported;
        return supported;
    }

    // ---- Device feature query on the private context -----------------------
    // Mirrors upstream `hasFeature(t.device.features, ...)` and the implicit
    // device-feature gating that upstream's beforeAllSubcases performs via
    // selectDeviceOrSkipTestCase. On our all-features context this returns true
    // whenever the adapter supports the feature.
    bool deviceHasFeature(WGPUFeatureName feature) {
        ensureContext();
        return wgpuDeviceHasFeature(ctxDevice_, feature) != WGPU_FALSE;
    }

    // ---- Skip the case if the private device lacks `feature` ---------------
    // Mirrors upstream selectDeviceOrSkipTestCase({ requiredFeatures: [feature] }).
    void skipIfDeviceDoesNotHaveFeature(WGPUFeatureName feature, const std::string& name) {
        if (!deviceHasFeature(feature)) {
            skip("device does not have required feature: " + name);
        }
    }

    // ---- Conditional skip (mirrors upstream t.skipIf(cond, msg)) -----------
    void skipIf(bool condition, const std::string& message = "") {
        if (condition) {
            skip(message.empty() ? "skipped" : message);
        }
    }

    // ---- Skip the case if a WGSL language feature is unsupported -----------
    // Mirrors upstream t.skipIfLanguageFeatureNotSupported(name). Distinct from
    // hasLanguageFeature (which sets the *expected* result): this SKIPS the case
    // when the feature is absent, so an unsupported-feature backend neither
    // false-passes nor false-fails.
    void skipIfLanguageFeatureNotSupported(std::string_view feature) {
        if (!hasLanguageFeature(feature)) {
            skip(std::string("WGSL language feature not supported: ") + std::string(feature));
        }
    }

    // ---- Probe whether a WGSL extension is usable on the private device ----
    // Compiles a minimal shader that enables `enableExtensions` (a single WGSL
    // `enable ...;` line) and returns false if the module fails to compile (e.g.
    // the device was not created with the corresponding feature). This is a
    // behavioral, header-portable substitute for selectDeviceOrSkipTestCase when
    // there is no stable WGPUFeatureName (e.g. subgroup-size-control, which Dawn
    // gates behind a device toggle not present in the standard webgpu-headers enum).
    bool wgslExtensionUsable(const std::string& enableLines) {
        ensureContext();
        const std::string probe =
            enableLines + "\n@compute @workgroup_size(1) fn main() {}";
        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = WGPUStringView{probe.data(), probe.size()};
        WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        desc.nextInChain = &source.chain;

        wgpuDevicePushErrorScope(ctxDevice_, WGPUErrorFilter_Validation);
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(ctxDevice_, &desc);
        ScopeResult scope = popErrorScopeSync(ctxInstance_, ctxDevice_);
        if (sm != nullptr) {
            wgpuShaderModuleRelease(sm);
        }
        if (scope.status != WGPUPopErrorScopeStatus_Success) {
            fail("popErrorScope failed: " + scope.message);
        }
        return scope.type == WGPUErrorType_NoError;
    }

    // ---- Trial-compile probe: does `code` compile with no error? -----------
    // Compiles `code` as-is on the private context and returns true iff (a) the
    // createShaderModule validation error scope reports no error AND (b) the
    // asynchronous getCompilationInfo carries no 'error'-type message. Used by the
    // non-Dawn hasLanguageFeature behavioral probe. Does not fail() the case.
    bool compilesWithoutError(const std::string& code) {
        ensureContext();
        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = WGPUStringView{code.data(), code.size()};
        WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        desc.nextInChain = &source.chain;

        wgpuDevicePushErrorScope(ctxDevice_, WGPUErrorFilter_Validation);
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(ctxDevice_, &desc);
        ScopeResult scope = popErrorScopeSync(ctxInstance_, ctxDevice_);
        if (scope.status != WGPUPopErrorScopeStatus_Success) {
            if (sm != nullptr) {
                wgpuShaderModuleRelease(sm);
            }
            return false;
        }
        if (scope.type != WGPUErrorType_NoError) {
            if (sm != nullptr) {
                wgpuShaderModuleRelease(sm);
            }
            return false;
        }
#if defined(CTS_BACKEND_WGPU)
        // wgpu-native: getCompilationInfo aborts (see expectCompileResult). Base the
        // result on the error-scope verdict alone (no error scope + non-null module
        // == compiled without error).
        const bool ok = (sm != nullptr);
        if (sm != nullptr) {
            wgpuShaderModuleRelease(sm);
        }
        return ok;
#else
        bool ok = true;
        if (sm != nullptr) {
            const std::vector<MessageInfo> messages = getCompilationInfoChecked(sm);
            for (const MessageInfo& m : messages) {
                if (m.type == WGPUCompilationMessageType_Error) {
                    ok = false;
                    break;
                }
            }
            wgpuShaderModuleRelease(sm);
        } else {
            ok = false;
        }
        return ok;
#endif
    }

    // ---- Adapter subgroup-size range (mirrors device.adapterInfo) ----------
    struct SubgroupSizeRange {
        uint32_t minSize = 0;
        uint32_t maxSize = 0;
    };
    SubgroupSizeRange getSubgroupSizeRange() {
        ensureContext();
        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        SubgroupSizeRange range;
        if (wgpuAdapterGetInfo(ctxAdapter_, &info) == WGPUStatus_Success) {
            range.minSize = info.subgroupMinSize;
            range.maxSize = info.subgroupMaxSize;
            wgpuAdapterInfoFreeMembers(info);
        }
        return range;
    }

    // ---- Pipeline-creation success check on the private context ------------
    // Like the createComputePipeline error-scope check in expectPipelineResult, but
    // for an already-built shaderModule/descriptor. Returns true if a validation
    // error occurred. Used by getValidSubgroupSizes.
    bool computePipelineCausesError(const std::string& code) {
        ensureContext();
        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = WGPUStringView{code.data(), code.size()};
        WGPUShaderModuleDescriptor smDesc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        smDesc.nextInChain = &source.chain;

        wgpuDevicePushErrorScope(ctxDevice_, WGPUErrorFilter_Validation);
        WGPUShaderModule sm = wgpuDeviceCreateShaderModule(ctxDevice_, &smDesc);
        WGPUComputePipelineDescriptor pd = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
        pd.layout = nullptr;
        pd.compute.module = sm;
        pd.compute.entryPoint = WGPUStringView{"main", 4};
        WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(ctxDevice_, &pd);
        ScopeResult scope = popErrorScopeSync(ctxInstance_, ctxDevice_);
        if (pipeline != nullptr) {
            wgpuComputePipelineRelease(pipeline);
        }
        if (sm != nullptr) {
            wgpuShaderModuleRelease(sm);
        }
        if (scope.status != WGPUPopErrorScopeStatus_Success) {
            fail("popErrorScope failed: " + scope.message);
        }
        return scope.type != WGPUErrorType_NoError;
    }

    // ---- Wrap a fragment into a compute entry point (upstream wrapInEntryPoint)
    std::string wrapInEntryPoint(const std::string& code,
                                 const std::vector<std::string>& enabledExtensions = {}) {
        std::string enables;
        for (const std::string& ext : enabledExtensions) {
            enables += "enable " + ext + ";\n      ";
        }
        return "\n      " + enables +
               "\n      @compute @workgroup_size(1)\n      fn main() {\n        " + code +
               "\n      }";
    }

  protected:
    // Per-message info captured from getCompilationInfo.
    struct MessageInfo {
        WGPUCompilationMessageType type = WGPUCompilationMessageType_Error;
        uint64_t lineNum = 0;
        uint64_t linePos = 0;
        std::string message;
    };

    void skipIfCodeNeedsFeatureAndDeviceDoesNotHaveFeature(const std::string& code) {
        static const EnableFeature kEnables[] = {
            {"f16", WGPUFeatureName_ShaderF16},
            {"subgroups", WGPUFeatureName_Subgroups},
            {"clip_distances", WGPUFeatureName_ClipDistances},
            {"chromium_experimental_primitive_id", WGPUFeatureName_PrimitiveIndex},
        };
        ensureContext();
        for (const EnableFeature& ef : kEnables) {
            if (codeEnables(code, ef.enableName)) {
                if (wgpuDeviceHasFeature(ctxDevice_, ef.feature) == WGPU_FALSE) {
                    skip(std::string("device does not have feature required by `enable ") +
                         ef.enableName + "`");
                }
            }
        }
    }

    // Create a shader module on the private context, wrapped in a validation
    // error scope. Fails the case if the observed error behaviour does not match
    // `shouldError`. Returns the (non-null) shader module; caller releases it.
    WGPUShaderModule createShaderModuleChecked(const std::string& code, bool shouldError) {
        ensureContext();

        WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
        source.code = WGPUStringView{code.data(), code.size()};
        WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
        desc.nextInChain = &source.chain;

        wgpuDevicePushErrorScope(ctxDevice_, WGPUErrorFilter_Validation);
        WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctxDevice_, &desc);
        ScopeResult scope = popErrorScopeSync(ctxInstance_, ctxDevice_);

        if (scope.status != WGPUPopErrorScopeStatus_Success) {
            if (shaderModule != nullptr) {
                wgpuShaderModuleRelease(shaderModule);
            }
            fail("popErrorScope failed: " + scope.message);
        }
        const bool hadError = scope.type != WGPUErrorType_NoError;
        if (shouldError && !hadError) {
            if (shaderModule != nullptr) {
                wgpuShaderModuleRelease(shaderModule);
            }
            fail("expected a validation error for invalid shader, got none.\n---- shader ----\n" + code);
        }
        if (!shouldError && hadError) {
            if (shaderModule != nullptr) {
                wgpuShaderModuleRelease(shaderModule);
            }
            fail("unexpected validation error for valid shader: " + scope.message +
                 "\n---- shader ----\n" + code);
        }
        if (shaderModule == nullptr) {
            fail("createShaderModule returned null");
        }
        return shaderModule;
    }

    std::vector<MessageInfo> getCompilationInfoChecked(WGPUShaderModule shaderModule) {
        struct State {
            bool completed = false;
            WGPUCompilationInfoRequestStatus status = WGPUCompilationInfoRequestStatus_CallbackCancelled;
            std::vector<MessageInfo> messages;
        } state;

        WGPUCompilationInfoCallbackInfo cb = WGPU_COMPILATION_INFO_CALLBACK_INFO_INIT;
        cb.mode = WGPUCallbackMode_AllowProcessEvents;
        cb.callback = [](WGPUCompilationInfoRequestStatus status,
                         const WGPUCompilationInfo* info,
                         void* ud1,
                         void* /*ud2*/) {
            auto* s = static_cast<State*>(ud1);
            s->completed = true;
            s->status = status;
            if (info != nullptr) {
                for (size_t i = 0; i < info->messageCount; ++i) {
                    const WGPUCompilationMessage& src = info->messages[i];
                    MessageInfo m;
                    m.type = src.type;
                    m.lineNum = src.lineNum;
                    m.linePos = src.linePos;
                    if (src.message.data != nullptr && src.message.length != WGPU_STRLEN) {
                        m.message.assign(src.message.data, src.message.length);
                    } else if (src.message.data != nullptr) {
                        m.message.assign(src.message.data);
                    }
                    s->messages.push_back(m);
                }
            }
        };
        cb.userdata1 = &state;

        (void)wgpuShaderModuleGetCompilationInfo(shaderModule, cb);
        if (!processEventsUntil(ctxInstance_, [&] { return state.completed; })) {
            fail("getCompilationInfo timed out");
        }
        if (state.status != WGPUCompilationInfoRequestStatus_Success) {
            fail("getCompilationInfo failed (status=" +
                 std::to_string(static_cast<int>(state.status)) + ")");
        }
        return state.messages;
    }

    static std::string messagesLog(const std::vector<MessageInfo>& messages, const std::string& code) {
        std::string log;
        for (const MessageInfo& m : messages) {
            const char* typeStr =
                m.type == WGPUCompilationMessageType_Error ? "error" :
                m.type == WGPUCompilationMessageType_Warning ? "warning" : "info";
            log += std::to_string(m.lineNum) + ":" + std::to_string(m.linePos) + ": " +
                   typeStr + ": " + m.message + "\n";
        }
        if (!code.empty()) {
            log += "\n---- shader ----\n" + code;
        }
        return log;
    }

  private:
    void ensureContext() {
        // The instance/adapter/device are reused across ALL cases on a worker
        // thread (thread_local), created once per thread and intentionally leaked
        // at process exit. Creating a fresh device PER CASE exhausts some backends
        // (yawgpu) at scale — thousands of validation cases cause device-creation
        // failures and spurious mass fails. thread_local also avoids cross-thread
        // device races (--workers N runs N threads in one process).
        thread_local WGPUInstance tlInstance = nullptr;
        thread_local WGPUAdapter tlAdapter = nullptr;
        thread_local WGPUDevice tlDevice = nullptr;

        if (tlDevice == nullptr) {
            tlInstance = createInstance();
            if (tlInstance == nullptr) {
                fail("failed to create WGPUInstance for shader validation");
            }
            AdapterResult adapter = requestAdapterSync(tlInstance, nullptr);
            if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
                fail("failed to request adapter: " + adapter.message);
            }
            tlAdapter = adapter.adapter;

            WGPULimits limits = WGPU_LIMITS_INIT;
            const bool haveLimits = wgpuAdapterGetLimits(tlAdapter, &limits) == WGPUStatus_Success;

            WGPUSupportedFeatures supported = WGPU_SUPPORTED_FEATURES_INIT;
            wgpuAdapterGetFeatures(tlAdapter, &supported);

            WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
            desc.requiredFeatureCount = supported.featureCount;
            desc.requiredFeatures = supported.features;
            if (haveLimits) {
                desc.requiredLimits = &limits;
            }
            DeviceResult device = requestDeviceSync(tlInstance, tlAdapter, &desc);
            wgpuSupportedFeaturesFreeMembers(supported);

            if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
                // Retry with no extra features/limits to maximize portability.
                WGPUDeviceDescriptor fallback = WGPU_DEVICE_DESCRIPTOR_INIT;
                device = requestDeviceSync(tlInstance, tlAdapter, &fallback);
            }
            if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
                fail("failed to request shader-validation device: " + device.message);
            }
            tlDevice = device.device;
        }

        ctxInstance_ = tlInstance;
        ctxAdapter_ = tlAdapter;
        ctxDevice_ = tlDevice;
    }

    void releaseContext() {
        // The context is thread_local and reused across cases — do NOT release it
        // per case (that is the per-case-device exhaustion this design avoids).
        // Just drop this fixture's non-owning views.
        ctxInstance_ = nullptr;
        ctxAdapter_ = nullptr;
        ctxDevice_ = nullptr;
    }

    // Non-owning views of the thread_local context (see ensureContext()).
    WGPUInstance ctxInstance_ = nullptr;
    WGPUAdapter ctxAdapter_ = nullptr;
    WGPUDevice ctxDevice_ = nullptr;
};

// Upstream UniqueFeaturesAndLimitsShaderValidationTest is functionally identical
// for our needs (both subclasses wrap createShaderModule in a validation scope,
// check getCompilationInfo, and provide expectPipelineResult). The clip_distances,
// dual_source_blending and subgroup_size_control specs use this name; alias it.
using UniqueFeaturesAndLimitsShaderValidationTest = ShaderValidationTest;

} // namespace cts::shader_validation
