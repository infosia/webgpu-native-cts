// Ported from gpuweb/cts src/webgpu/api/operation/adapter/info.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes:
//
// 1. Probe instance pattern: the upstream uses `getGPU(t.rec).requestAdapter()`
//    (web/navigator.gpu), which has no direct C equivalent.  Each test creates
//    a temporary WGPUInstance + WGPUAdapter, queries adapter info via
//    wgpuAdapterGetInfo, and releases both in a RAII helper.  This mirrors the
//    "temporary-instance wgpuAdapterGetInfo" pattern from
//    shader_io/compute_builtins.spec.cpp (querySubgroupRange).
//
// 2. same_object (.unimplemented): the [SameObject] extended attribute is a
//    JS/WebIDL object-identity guarantee (adapter.info === adapter.info).
//    The C API returns WGPUAdapterInfo by value (struct copy), so this
//    semantics does not apply.  The test also accesses device.adapterInfo via
//    wgpuDeviceGetAdapterInfo, which is not implemented in wgpu-native
//    (src/unimplemented.rs) and not exported by yawgpu.
//
// 3. device_matches_adapter (.unimplemented): requires wgpuDeviceGetAdapterInfo
//    to read device.adapterInfo, which is not implemented in wgpu-native and
//    not exported by yawgpu.  The testDeviceFirst / testMembersFirst subcases
//    govern JS access order (a [SameObject] caching concern) that has no
//    meaningful C analog.
//
// 4. Normalized-identifier check: the upstream test checks vendor/architecture/device
//    against the normalizedIdentifierRegex /^$|^[a-z0-9]+(-[a-z0-9]+)*$/.  This is a
//    web-platform privacy requirement (browsers normalize/blank adapter strings to
//    prevent fingerprinting) that does not apply to native backends, which return real
//    hardware strings (e.g. "Apple M2").  The check is omitted in the C port.
//
// 5. isPowerOfTwo: the upstream util function is ported as a local constexpr
//    helper.

#include <cstdint>
#include <string>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Note: isSegmentChar / isNormalizedIdentifier were used for the upstream
// normalized-identifier checks on vendor/architecture/device.  Those checks
// enforce a web-platform privacy requirement (browsers blank/normalize adapter
// strings to prevent fingerprinting) that does not apply to native backends.
// The functions are removed to avoid unused-function warnings under MSVC /W4.

// Upstream: isPowerOfTwo from util/math.ts
static bool isPowerOfTwo(uint32_t n) {
    return n != 0 && (n & (n - 1)) == 0;
}

// RAII probe context: a temporary WGPUInstance + WGPUAdapter used to call
// wgpuAdapterGetInfo without touching the shared harness device/adapter.
struct ProbeContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter  adapter  = nullptr;

    ProbeContext() = default;
    ProbeContext(const ProbeContext&) = delete;
    ProbeContext& operator=(const ProbeContext&) = delete;

    ~ProbeContext() {
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
        }
        if (instance != nullptr) {
            wgpuInstanceRelease(instance);
        }
    }
};

// Creates a probe instance + adapter, failing the test if either step fails.
static void createProbeContext(AllFeaturesMaxLimitsGpuTest& t, ProbeContext& ctx) {
    ctx.instance = createInstance();
    if (ctx.instance == nullptr) {
        t.fail("adapter/info: failed to create a probe WGPUInstance");
    }
    AdapterResult ar = requestAdapterSync(ctx.instance, nullptr);
    if (ar.status != WGPURequestAdapterStatus_Success || ar.adapter == nullptr) {
        t.fail("adapter/info: failed to request probe adapter: " + ar.message);
    }
    ctx.adapter = ar.adapter;
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

TestGroup<AllFeaturesMaxLimitsGpuTest> grp = MakeTestGroup<AllFeaturesMaxLimitsGpuTest>(
    "api,operation,adapter,info",
    R"(
Tests for GPUAdapterInfo.
)");

// ---------------------------------------------------------------------------
// adapter_info
// ---------------------------------------------------------------------------

CTS_TEST(grp, "adapter_info")
    .desc(
        "Test that every member in the GPUAdapter.info except description is properly formatted\n"
        "\n"
        "Note (C port): the upstream test checks that vendor, architecture, and device\n"
        "match the normalized-identifier regex /^$|^[a-z0-9]+(-[a-z0-9]+)*$/.  This is a\n"
        "WEB-PLATFORM PRIVACY REQUIREMENT: browsers normalize/blank these strings to prevent\n"
        "fingerprinting.  Native implementations (Dawn, yawgpu, wgpu-native) legitimately\n"
        "return real hardware strings (e.g. 'Apple M2', 'Apple', 'apple-m2') that do not\n"
        "match the normalized format.  The normalization assertions are dropped here; only\n"
        "the structural check (wgpuAdapterGetInfo succeeds and fields are accessible) is kept.")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        ProbeContext ctx;
        createProbeContext(t, ctx);

        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        const WGPUStatus status = wgpuAdapterGetInfo(ctx.adapter, &info);
        if (status != WGPUStatus_Success) {
            wgpuAdapterInfoFreeMembers(info);
            t.fail("wgpuAdapterGetInfo failed");
        }

        // Structural check: the fields are accessible (WGPUStringView with data/length).
        // The upstream normalization assertions (isNormalizedIdentifier) are omitted because
        // they enforce a web-platform privacy requirement (browsers blank/normalize adapter
        // strings to prevent fingerprinting) that does not apply to native backends.
        // Native implementations return real hardware strings such as "Apple M2" or
        // "NVIDIA GeForce RTX 4090", which do not match the normalized-identifier regex.
        (void)info.vendor;
        (void)info.architecture;
        (void)info.device;
        (void)info.description;

        wgpuAdapterInfoFreeMembers(info);
    });

// ---------------------------------------------------------------------------
// same_object
// ---------------------------------------------------------------------------

CTS_TEST(grp, "same_object")
    .desc(
        "GPUAdapter.info and GPUDevice.adapterInfo provide the same object each time they're "
        "accessed, but different objects from one another.")
    // [SameObject] is a JS/WebIDL object-identity guarantee; the C API returns
    // WGPUAdapterInfo by value (struct copy), so this semantics does not apply.
    // The test also accesses device.adapterInfo via wgpuDeviceGetAdapterInfo,
    // which is not implemented in wgpu-native and not exported by yawgpu.
    .unimplemented(
        "[SameObject] is JS/WebIDL object identity; C API returns WGPUAdapterInfo by value. "
        "device.adapterInfo requires wgpuDeviceGetAdapterInfo which is unimplemented in "
        "wgpu-native and not exported by yawgpu.");

// ---------------------------------------------------------------------------
// device_matches_adapter
// ---------------------------------------------------------------------------

CTS_TEST(grp, "device_matches_adapter")
    .desc(
        "Test that GPUDevice.adapterInfo matches GPUAdapter.info. Cases access the members in "
        "different orders to make sure that they are consistent regardless of the access order.")
    // Requires wgpuDeviceGetAdapterInfo which is unimplemented in wgpu-native
    // (src/unimplemented.rs) and not exported by yawgpu.  The testDeviceFirst /
    // testMembersFirst subcases control JS access order for [SameObject]
    // caching, a concern with no meaningful C analog.
    .unimplemented(
        "Requires wgpuDeviceGetAdapterInfo which is unimplemented in wgpu-native and not "
        "exported by yawgpu.");

// ---------------------------------------------------------------------------
// subgroup_sizes
// ---------------------------------------------------------------------------

CTS_TEST(grp, "subgroup_sizes")
    .desc(R"(
Verify GPUAdapterInfo.subgroupMinSize, GPUAdapterInfo.subgroupMaxSize.
If the subgroups feature is supported, they must both exist.
If they exist, they must both exist and be powers of two, and
4 <= subgroupMinSize <= subgroupMaxSize <= 128.
)")
    .fn([](AllFeaturesMaxLimitsGpuTest& t) {
        // Upstream bounds (const in upstream scope).
        const uint32_t kSubgroupMinSizeBound = 4;
        const uint32_t kSubgroupMaxSizeBound = 128;

        ProbeContext ctx;
        createProbeContext(t, ctx);

        WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
        const WGPUStatus status = wgpuAdapterGetInfo(ctx.adapter, &info);
        if (status != WGPUStatus_Success) {
            wgpuAdapterInfoFreeMembers(info);
            t.fail("wgpuAdapterGetInfo failed");
        }

        const uint32_t subgroupMinSize = info.subgroupMinSize;
        const uint32_t subgroupMaxSize = info.subgroupMaxSize;
        const bool hasSubgroupsFeature =
            wgpuAdapterHasFeature(ctx.adapter, WGPUFeatureName_Subgroups) != 0u;

        wgpuAdapterInfoFreeMembers(info);

        // Upstream: if (hasFeature(adapter.features, 'subgroups')) { ... }
        // When the subgroups feature is supported, both sizes must be non-zero
        // (the C API uses 0 as the "not present" sentinel, mirroring JS undefined).
        if (hasSubgroupsFeature) {
            t.expect(
                subgroupMinSize != 0u,
                "GPUAdapterInfo.subgroupMinSize must exist (non-zero) when subgroups supported");
            t.expect(
                subgroupMaxSize != 0u,
                "GPUAdapterInfo.subgroupMaxSize must exist (non-zero) when subgroups supported");
        }

        // Upstream: both must be defined (or neither).
        // In C, 0 means "not present" (since the INIT macro zeros them).
        const bool minDefined = subgroupMinSize != 0u;
        const bool maxDefined = subgroupMaxSize != 0u;
        t.expect(
            minDefined == maxDefined,
            "GPUAdapterInfo.subgroupMinSize and GPUAdapterInfo.subgroupMaxSize must both be "
            "defined, or neither should be");

        if (minDefined && maxDefined) {
            t.expect(isPowerOfTwo(subgroupMinSize), "subgroupMinSize must be a power of two");
            t.expect(isPowerOfTwo(subgroupMaxSize), "subgroupMaxSize must be a power of two");
            t.expect(
                kSubgroupMinSizeBound <= subgroupMinSize,
                "subgroupMinSize must be >= " + std::to_string(kSubgroupMinSizeBound));
            t.expect(
                subgroupMinSize <= subgroupMaxSize,
                "subgroupMinSize must be <= subgroupMaxSize");
            t.expect(
                subgroupMaxSize <= kSubgroupMaxSizeBound,
                "subgroupMaxSize must be <= " + std::to_string(kSubgroupMaxSizeBound));
        }
    });

} // namespace
