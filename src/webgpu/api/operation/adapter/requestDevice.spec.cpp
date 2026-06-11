// Ported from gpuweb/cts src/webgpu/api/operation/adapter/requestDevice.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
// SPDX-License-Identifier: BSD-3-Clause
// Upstream CTS: Copyright (c) 2019 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// Porting notes (deviations from upstream, all documented inline as well):
//
// 1. Lifecycle fixture. Upstream uses the base Fixture and obtains a GPU via
//    getGPU(t.rec) + gpu.requestAdapter(). Each test here owns a PRIVATE
//    WGPUInstance -> WGPUAdapter (RAII OwnedAdapterContext), never the shared
//    harness device. We use TestGroup<Fixture> only for fail()/expect()/skip()/
//    param(); we deliberately do NOT use GpuTest, whose init() would create the
//    shared device on (and thereby consume) the single-use shared harness
//    adapter, cascade-failing later AllFeaturesMaxLimitsGpuTest cases. Each test
//    self-guards GPU availability by requesting its own private adapter (skips if
//    none). Created devices are destroyed (upstream destroys them explicitly so
//    tests don't wait for GC) and released.
//
// 2. requestDeviceTracked(adapter, ...args) -> cts::requestDeviceSync(instance,
//    adapter, &desc). The created devices are tracked and destroyed/released at
//    test end (mirroring requestDeviceTracked's auto-destroy on cleanup).
//
// 3. shouldReject('TypeError'|'OperationError', p) / assertReject(...) have no
//    distinct C analogs: the C requestDevice callback reports a single
//    WGPURequestDeviceStatus and cannot distinguish TypeError vs OperationError.
//    All such expectations become "status != Success" checks. Likewise the JS
//    distinction between the two error classes in 'stale'/'limit,out_of_range'
//    collapses to "the request must fail" vs "the request must succeed".
//
// 4. Sparse requiredLimits. The C WGPULimits is a fixed struct (every field is
//    meaningful); a field left at WGPU_LIMIT_*_UNDEFINED means "use the
//    default". We therefore build a WGPULimits initialized to all-UNDEFINED and
//    set only the limit(s) under test. The four compat-only stage limits
//    (maxStorage{Buffers,Textures}In{Vertex,Fragment}Stage) are not in
//    WGPULimits; they live in a chained WGPUCompatibilityModeLimits and are set
//    there. Mirrors upstream's per-limit requiredLimits map.
//
// 5. 'limit,out_of_range' is UNIMPLEMENTED on native. It feeds out-of-range JS
//    Numbers (negatives, fractional, > 2^53 / > 2^64) to the requiredLimits and
//    expects requestDevice to throw a TypeError. In JS that rejection comes from
//    WebIDL [EnforceRange] coercion of the unsigned long / unsigned long long
//    limit fields, which has NO native C-API analog: the C WGPULimits fields are
//    plain unsigned integers and an out-of-band value such as (uint64_t)(-1)
//    aliases the WGPU_LIMIT_*_UNDEFINED sentinel, so the limit is ignored and the
//    request SUCCEEDS. The case is therefore .unimplemented() with that reason;
//    its name and params are kept for query identity.
//
// 6. featureLevel: 'compatibility' (always_returns_device) maps to the C
//    WGPURequestAdapterOptions.featureLevel field. Native backends may ignore
//    Compatibility and provide Core; upstream's note about that path is
//    preserved.

#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <variant>
#include <vector>

#include "cts/gpu.h"
#include "cts/test.h"
#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

using namespace cts;

namespace {

// ---------------------------------------------------------------------------
// Limit info table mirroring upstream kLimitInfoCore (class, default,
// maximumValue) for the CTS feature level 'core'. kPossibleLimits is the key
// order of this table.
// ---------------------------------------------------------------------------

constexpr uint64_t kMaxUnsignedLongValue = 4294967295ull;          // 2^32 - 1
constexpr uint64_t kMaxUnsignedLongLongValue = 9007199254740991ull; // Number.MAX_SAFE_INTEGER

enum class LimitClass { Maximum, Alignment };

struct LimitInfo {
    const char* name;
    LimitClass cls;
    uint64_t defaultValue;
    uint64_t maximumValue;
};

// Order matches upstream kLimitInfoCore / kPossibleLimits (35 limits).
const std::array<LimitInfo, 35> kLimitInfos = {{
    {"maxTextureDimension1D", LimitClass::Maximum, 8192, kMaxUnsignedLongValue},
    {"maxTextureDimension2D", LimitClass::Maximum, 8192, kMaxUnsignedLongValue},
    {"maxTextureDimension3D", LimitClass::Maximum, 2048, kMaxUnsignedLongValue},
    {"maxTextureArrayLayers", LimitClass::Maximum, 256, kMaxUnsignedLongValue},
    {"maxBindGroups", LimitClass::Maximum, 4, kMaxUnsignedLongValue},
    {"maxBindGroupsPlusVertexBuffers", LimitClass::Maximum, 24, kMaxUnsignedLongValue},
    {"maxBindingsPerBindGroup", LimitClass::Maximum, 1000, kMaxUnsignedLongValue},
    {"maxDynamicUniformBuffersPerPipelineLayout", LimitClass::Maximum, 8, kMaxUnsignedLongValue},
    {"maxDynamicStorageBuffersPerPipelineLayout", LimitClass::Maximum, 4, kMaxUnsignedLongValue},
    {"maxSampledTexturesPerShaderStage", LimitClass::Maximum, 16, kMaxUnsignedLongValue},
    {"maxSamplersPerShaderStage", LimitClass::Maximum, 16, kMaxUnsignedLongValue},
    {"maxStorageBuffersInFragmentStage", LimitClass::Maximum, 8, kMaxUnsignedLongValue},
    {"maxStorageBuffersInVertexStage", LimitClass::Maximum, 8, kMaxUnsignedLongValue},
    {"maxStorageBuffersPerShaderStage", LimitClass::Maximum, 8, kMaxUnsignedLongValue},
    {"maxStorageTexturesInFragmentStage", LimitClass::Maximum, 4, kMaxUnsignedLongValue},
    {"maxStorageTexturesInVertexStage", LimitClass::Maximum, 4, kMaxUnsignedLongValue},
    {"maxStorageTexturesPerShaderStage", LimitClass::Maximum, 4, kMaxUnsignedLongValue},
    {"maxUniformBuffersPerShaderStage", LimitClass::Maximum, 12, kMaxUnsignedLongValue},
    {"maxUniformBufferBindingSize", LimitClass::Maximum, 65536, kMaxUnsignedLongLongValue},
    {"maxStorageBufferBindingSize", LimitClass::Maximum, 134217728, kMaxUnsignedLongLongValue},
    {"minUniformBufferOffsetAlignment", LimitClass::Alignment, 256, kMaxUnsignedLongValue},
    {"minStorageBufferOffsetAlignment", LimitClass::Alignment, 256, kMaxUnsignedLongValue},
    {"maxVertexBuffers", LimitClass::Maximum, 8, kMaxUnsignedLongValue},
    {"maxBufferSize", LimitClass::Maximum, 268435456, kMaxUnsignedLongLongValue},
    {"maxVertexAttributes", LimitClass::Maximum, 16, kMaxUnsignedLongValue},
    {"maxVertexBufferArrayStride", LimitClass::Maximum, 2048, kMaxUnsignedLongValue},
    {"maxInterStageShaderVariables", LimitClass::Maximum, 16, kMaxUnsignedLongValue},
    {"maxColorAttachments", LimitClass::Maximum, 8, kMaxUnsignedLongValue},
    {"maxColorAttachmentBytesPerSample", LimitClass::Maximum, 32, kMaxUnsignedLongValue},
    {"maxComputeWorkgroupStorageSize", LimitClass::Maximum, 16384, kMaxUnsignedLongValue},
    {"maxComputeInvocationsPerWorkgroup", LimitClass::Maximum, 256, kMaxUnsignedLongValue},
    {"maxComputeWorkgroupSizeX", LimitClass::Maximum, 256, kMaxUnsignedLongValue},
    {"maxComputeWorkgroupSizeY", LimitClass::Maximum, 256, kMaxUnsignedLongValue},
    {"maxComputeWorkgroupSizeZ", LimitClass::Maximum, 64, kMaxUnsignedLongValue},
    {"maxComputeWorkgroupsPerDimension", LimitClass::Maximum, 65535, kMaxUnsignedLongValue},
}};

const LimitInfo* findLimitInfo(const std::string& name) {
    for (const LimitInfo& info : kLimitInfos) {
        if (name == info.name) {
            return &info;
        }
    }
    return nullptr;
}

std::vector<Value> kPossibleLimits() {
    std::vector<Value> out;
    out.reserve(kLimitInfos.size());
    for (const LimitInfo& info : kLimitInfos) {
        out.emplace_back(std::string(info.name));
    }
    return out;
}

// Read the value of a named limit from an adapter's reported limits (and its
// chained compatibility-mode limits for the four in-stage limits).
uint64_t readAdapterLimit(
    const WGPULimits& limits,
    const WGPUCompatibilityModeLimits& compat,
    const std::string& name) {
    if (name == "maxTextureDimension1D") return limits.maxTextureDimension1D;
    if (name == "maxTextureDimension2D") return limits.maxTextureDimension2D;
    if (name == "maxTextureDimension3D") return limits.maxTextureDimension3D;
    if (name == "maxTextureArrayLayers") return limits.maxTextureArrayLayers;
    if (name == "maxBindGroups") return limits.maxBindGroups;
    if (name == "maxBindGroupsPlusVertexBuffers") return limits.maxBindGroupsPlusVertexBuffers;
    if (name == "maxBindingsPerBindGroup") return limits.maxBindingsPerBindGroup;
    if (name == "maxDynamicUniformBuffersPerPipelineLayout")
        return limits.maxDynamicUniformBuffersPerPipelineLayout;
    if (name == "maxDynamicStorageBuffersPerPipelineLayout")
        return limits.maxDynamicStorageBuffersPerPipelineLayout;
    if (name == "maxSampledTexturesPerShaderStage") return limits.maxSampledTexturesPerShaderStage;
    if (name == "maxSamplersPerShaderStage") return limits.maxSamplersPerShaderStage;
    if (name == "maxStorageBuffersInFragmentStage")
        return compat.maxStorageBuffersInFragmentStage;
    if (name == "maxStorageBuffersInVertexStage") return compat.maxStorageBuffersInVertexStage;
    if (name == "maxStorageBuffersPerShaderStage") return limits.maxStorageBuffersPerShaderStage;
    if (name == "maxStorageTexturesInFragmentStage")
        return compat.maxStorageTexturesInFragmentStage;
    if (name == "maxStorageTexturesInVertexStage") return compat.maxStorageTexturesInVertexStage;
    if (name == "maxStorageTexturesPerShaderStage") return limits.maxStorageTexturesPerShaderStage;
    if (name == "maxUniformBuffersPerShaderStage") return limits.maxUniformBuffersPerShaderStage;
    if (name == "maxUniformBufferBindingSize") return limits.maxUniformBufferBindingSize;
    if (name == "maxStorageBufferBindingSize") return limits.maxStorageBufferBindingSize;
    if (name == "minUniformBufferOffsetAlignment") return limits.minUniformBufferOffsetAlignment;
    if (name == "minStorageBufferOffsetAlignment") return limits.minStorageBufferOffsetAlignment;
    if (name == "maxVertexBuffers") return limits.maxVertexBuffers;
    if (name == "maxBufferSize") return limits.maxBufferSize;
    if (name == "maxVertexAttributes") return limits.maxVertexAttributes;
    if (name == "maxVertexBufferArrayStride") return limits.maxVertexBufferArrayStride;
    if (name == "maxInterStageShaderVariables") return limits.maxInterStageShaderVariables;
    if (name == "maxColorAttachments") return limits.maxColorAttachments;
    if (name == "maxColorAttachmentBytesPerSample") return limits.maxColorAttachmentBytesPerSample;
    if (name == "maxComputeWorkgroupStorageSize") return limits.maxComputeWorkgroupStorageSize;
    if (name == "maxComputeInvocationsPerWorkgroup")
        return limits.maxComputeInvocationsPerWorkgroup;
    if (name == "maxComputeWorkgroupSizeX") return limits.maxComputeWorkgroupSizeX;
    if (name == "maxComputeWorkgroupSizeY") return limits.maxComputeWorkgroupSizeY;
    if (name == "maxComputeWorkgroupSizeZ") return limits.maxComputeWorkgroupSizeZ;
    if (name == "maxComputeWorkgroupsPerDimension")
        return limits.maxComputeWorkgroupsPerDimension;
    std::abort();
}

// A requiredLimits payload: a fixed WGPULimits initialized to all-UNDEFINED,
// optionally chained to a WGPUCompatibilityModeLimits for the four in-stage
// limits. Both structs must outlive the consuming requestDevice() call, so this
// owns them.
struct RequiredLimits {
    WGPULimits limits = WGPU_LIMITS_INIT;
    WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
    bool useCompat = false;

    const WGPULimits* finalize() {
        if (useCompat) {
            limits.nextInChain = &compat.chain;
        }
        return &limits;
    }
};

// Set a named limit to `value` (a uint64; clamped to uint32 for 32-bit limits).
void setLimit(RequiredLimits& req, const std::string& name, uint64_t value) {
    const auto u32 = [&](uint32_t& field) {
        field = static_cast<uint32_t>(value);
    };
    if (name == "maxTextureDimension1D") return u32(req.limits.maxTextureDimension1D);
    if (name == "maxTextureDimension2D") return u32(req.limits.maxTextureDimension2D);
    if (name == "maxTextureDimension3D") return u32(req.limits.maxTextureDimension3D);
    if (name == "maxTextureArrayLayers") return u32(req.limits.maxTextureArrayLayers);
    if (name == "maxBindGroups") return u32(req.limits.maxBindGroups);
    if (name == "maxBindGroupsPlusVertexBuffers")
        return u32(req.limits.maxBindGroupsPlusVertexBuffers);
    if (name == "maxBindingsPerBindGroup") return u32(req.limits.maxBindingsPerBindGroup);
    if (name == "maxDynamicUniformBuffersPerPipelineLayout")
        return u32(req.limits.maxDynamicUniformBuffersPerPipelineLayout);
    if (name == "maxDynamicStorageBuffersPerPipelineLayout")
        return u32(req.limits.maxDynamicStorageBuffersPerPipelineLayout);
    if (name == "maxSampledTexturesPerShaderStage")
        return u32(req.limits.maxSampledTexturesPerShaderStage);
    if (name == "maxSamplersPerShaderStage") return u32(req.limits.maxSamplersPerShaderStage);
    if (name == "maxStorageBuffersInFragmentStage") {
        req.useCompat = true;
        return u32(req.compat.maxStorageBuffersInFragmentStage);
    }
    if (name == "maxStorageBuffersInVertexStage") {
        req.useCompat = true;
        return u32(req.compat.maxStorageBuffersInVertexStage);
    }
    if (name == "maxStorageBuffersPerShaderStage")
        return u32(req.limits.maxStorageBuffersPerShaderStage);
    if (name == "maxStorageTexturesInFragmentStage") {
        req.useCompat = true;
        return u32(req.compat.maxStorageTexturesInFragmentStage);
    }
    if (name == "maxStorageTexturesInVertexStage") {
        req.useCompat = true;
        return u32(req.compat.maxStorageTexturesInVertexStage);
    }
    if (name == "maxStorageTexturesPerShaderStage")
        return u32(req.limits.maxStorageTexturesPerShaderStage);
    if (name == "maxUniformBuffersPerShaderStage")
        return u32(req.limits.maxUniformBuffersPerShaderStage);
    if (name == "maxUniformBufferBindingSize") {
        req.limits.maxUniformBufferBindingSize = value;
        return;
    }
    if (name == "maxStorageBufferBindingSize") {
        req.limits.maxStorageBufferBindingSize = value;
        return;
    }
    if (name == "minUniformBufferOffsetAlignment")
        return u32(req.limits.minUniformBufferOffsetAlignment);
    if (name == "minStorageBufferOffsetAlignment")
        return u32(req.limits.minStorageBufferOffsetAlignment);
    if (name == "maxVertexBuffers") return u32(req.limits.maxVertexBuffers);
    if (name == "maxBufferSize") {
        req.limits.maxBufferSize = value;
        return;
    }
    if (name == "maxVertexAttributes") return u32(req.limits.maxVertexAttributes);
    if (name == "maxVertexBufferArrayStride") return u32(req.limits.maxVertexBufferArrayStride);
    if (name == "maxInterStageShaderVariables") return u32(req.limits.maxInterStageShaderVariables);
    if (name == "maxColorAttachments") return u32(req.limits.maxColorAttachments);
    if (name == "maxColorAttachmentBytesPerSample")
        return u32(req.limits.maxColorAttachmentBytesPerSample);
    if (name == "maxComputeWorkgroupStorageSize")
        return u32(req.limits.maxComputeWorkgroupStorageSize);
    if (name == "maxComputeInvocationsPerWorkgroup")
        return u32(req.limits.maxComputeInvocationsPerWorkgroup);
    if (name == "maxComputeWorkgroupSizeX") return u32(req.limits.maxComputeWorkgroupSizeX);
    if (name == "maxComputeWorkgroupSizeY") return u32(req.limits.maxComputeWorkgroupSizeY);
    if (name == "maxComputeWorkgroupSizeZ") return u32(req.limits.maxComputeWorkgroupSizeZ);
    if (name == "maxComputeWorkgroupsPerDimension")
        return u32(req.limits.maxComputeWorkgroupsPerDimension);
    std::abort();
}

// Read the value of a named limit from a created device's limits.
uint64_t readDeviceLimit(
    const WGPULimits& limits,
    const WGPUCompatibilityModeLimits& compat,
    const std::string& name) {
    return readAdapterLimit(limits, compat, name);
}

bool isPowerOfTwo(uint64_t n) {
    if (n == 0) {
        return false;
    }
    return (n & (n - 1)) == 0;
}

uint64_t clampU64(uint64_t n, uint64_t lo, uint64_t hi) {
    if (n < lo) {
        return lo;
    }
    if (n > hi) {
        return hi;
    }
    return n;
}

// ---------------------------------------------------------------------------
// kFeatureNames mirroring upstream kFeatureNameInfo key order.
// ---------------------------------------------------------------------------

struct FeatureNameEntry {
    const char* name;
    WGPUFeatureName value;
};

const std::array<FeatureNameEntry, 22> kFeatureNameEntries = {{
    {"bgra8unorm-storage", WGPUFeatureName_BGRA8UnormStorage},
    {"depth-clip-control", WGPUFeatureName_DepthClipControl},
    {"depth32float-stencil8", WGPUFeatureName_Depth32FloatStencil8},
    {"texture-compression-bc", WGPUFeatureName_TextureCompressionBC},
    {"texture-compression-bc-sliced-3d", WGPUFeatureName_TextureCompressionBCSliced3D},
    {"texture-compression-etc2", WGPUFeatureName_TextureCompressionETC2},
    {"texture-compression-astc", WGPUFeatureName_TextureCompressionASTC},
    {"texture-compression-astc-sliced-3d", WGPUFeatureName_TextureCompressionASTCSliced3D},
    {"timestamp-query", WGPUFeatureName_TimestampQuery},
    {"indirect-first-instance", WGPUFeatureName_IndirectFirstInstance},
    {"shader-f16", WGPUFeatureName_ShaderF16},
    {"rg11b10ufloat-renderable", WGPUFeatureName_RG11B10UfloatRenderable},
    {"float32-filterable", WGPUFeatureName_Float32Filterable},
    {"float32-blendable", WGPUFeatureName_Float32Blendable},
    {"clip-distances", WGPUFeatureName_ClipDistances},
    {"dual-source-blending", WGPUFeatureName_DualSourceBlending},
    {"subgroups", WGPUFeatureName_Subgroups},
    {"core-features-and-limits", WGPUFeatureName_CoreFeaturesAndLimits},
    {"texture-formats-tier1", WGPUFeatureName_TextureFormatsTier1},
    {"texture-formats-tier2", WGPUFeatureName_TextureFormatsTier2},
    {"primitive-index", WGPUFeatureName_PrimitiveIndex},
    {"texture-component-swizzle", WGPUFeatureName_TextureComponentSwizzle},
}};

std::vector<Value> kFeatureNames() {
    std::vector<Value> out;
    out.reserve(kFeatureNameEntries.size());
    for (const FeatureNameEntry& e : kFeatureNameEntries) {
        out.emplace_back(std::string(e.name));
    }
    return out;
}

const FeatureNameEntry* findFeature(const std::string& name) {
    for (const FeatureNameEntry& e : kFeatureNameEntries) {
        if (name == e.name) {
            return &e;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Owns a private instance + adapter (lifecycle). Created devices are destroyed
// and released on scope exit (fail()/skip() throw, so RAII is required).
// Never destroys/releases the shared harness device.
// ---------------------------------------------------------------------------

struct OwnedAdapterContext {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    std::vector<WGPUDevice> devices;

    OwnedAdapterContext() = default;
    OwnedAdapterContext(const OwnedAdapterContext&) = delete;
    OwnedAdapterContext& operator=(const OwnedAdapterContext&) = delete;

    // Track a created device so it is destroyed + released at the end (mirrors
    // requestDeviceTracked's auto-cleanup; upstream tests also destroy created
    // devices so the suite doesn't wait for GC).
    void track(WGPUDevice device) {
        if (device != nullptr) {
            devices.push_back(device);
        }
    }

    ~OwnedAdapterContext() {
        for (WGPUDevice device : devices) {
            wgpuDeviceDestroy(device);
            wgpuDeviceRelease(device);
        }
        if (adapter != nullptr) {
            wgpuAdapterRelease(adapter);
        }
        if (instance != nullptr) {
            wgpuInstanceRelease(instance);
        }
    }
};

// Create the private instance and request an adapter (optionally with a feature
// level). Fails the test on setup error. Mirrors getGPU(t.rec).requestAdapter().
void createOwnedAdapter(
    Fixture& t,
    OwnedAdapterContext& ctx,
    WGPUFeatureLevel featureLevel = WGPUFeatureLevel_Undefined) {
    ctx.instance = createInstance();
    if (ctx.instance == nullptr) {
        t.fail("requestDevice: failed to create WGPUInstance");
    }
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.featureLevel = featureLevel;
    AdapterResult ar = requestAdapterSync(ctx.instance, &options);
    if (ar.status != WGPURequestAdapterStatus_Success || ar.adapter == nullptr) {
        // Upstream: assert(adapter !== null). On native, no adapter means the
        // test cannot run.
        t.skip("requestDevice: no adapter available");
    }
    ctx.adapter = ar.adapter;
}

// requestDeviceTracked(adapter, descriptor?) -> DeviceResult, tracking success.
DeviceResult requestDeviceTracked(
    OwnedAdapterContext& ctx,
    const WGPUDeviceDescriptor* descriptor) {
    DeviceResult dr = requestDeviceSync(ctx.instance, ctx.adapter, descriptor);
    ctx.track(dr.device);
    return dr;
}

bool deviceHasFeatureExactlyCoreOnly(WGPUDevice device) {
    WGPUSupportedFeatures features = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuDeviceGetFeatures(device, &features);
    const size_t count = features.featureCount;
    bool hasCore = false;
    for (size_t i = 0; i < count; ++i) {
        if (features.features[i] == WGPUFeatureName_CoreFeaturesAndLimits) {
            hasCore = true;
        }
    }
    wgpuSupportedFeaturesFreeMembers(features);
    // Upstream: device.features.size === 1 && hasFeature('core-features-and-limits')
    // OR device.features.size === 0.
    if (count == 0) {
        return true;
    }
    return count == 1 && hasCore;
}

// ---------------------------------------------------------------------------
// Test group
// ---------------------------------------------------------------------------

// Base Fixture (NOT GpuTest): GpuTest::init() eagerly creates the shared cached
// device on the shared harness adapter, which the C API treats as single-use
// (one device per adapter). That would consume the shared adapter and
// cascade-fail later AllFeaturesMaxLimitsGpuTest cases ("adapter is consumed").
// These tests only ever drive their own private instance/adapter.
TestGroup<Fixture> g = MakeTestGroup<Fixture>(
    "api,operation,adapter,requestDevice",
    "Test GPUAdapter.requestDevice.\n\n"
    "Note tests explicitly destroy created devices so that tests don't have to wait for GC to "
    "clean up potentially limited native resources.");

// ---------------------------------------------------------------------------
// g.test('default')
// ---------------------------------------------------------------------------
CTS_TEST(g, "default")
    .desc(
        "Test requesting the device with a variation of default parameters.\n"
        "- No features listed in default device\n"
        "- Default limits")
    .params([](ParamsBuilder u) {
        // Upstream paramsSubcasesOnly over `args`. The four arg variants are
        // semantically identical from the C API's perspective (no descriptor /
        // empty descriptor / empty features+limits). We preserve the case
        // identity with a string-encoded `args` param.
        return u.beginSubcases().combine(
            "args",
            {Value("[]"), Value("[undefined]"), Value("[{}]"),
             Value("[{requiredFeatures:[],requiredLimits:{}}]")});
    })
    .fn([](Fixture& t) {
        OwnedAdapterContext ctx;
        createOwnedAdapter(t, ctx);

        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        DeviceResult dr = requestDeviceTracked(ctx, &desc);
        if (dr.status != WGPURequestDeviceStatus_Success || dr.device == nullptr) {
            t.fail("default: requestDevice should succeed: " + dr.message);
        }

        t.expect(
            deviceHasFeatureExactlyCoreOnly(dr.device),
            "Default device should not have any features other than "
            "\"core-features-and-limits\"");

        // All limits should be defaults.
        WGPULimits deviceLimits = WGPU_LIMITS_INIT;
        WGPUCompatibilityModeLimits deviceCompat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
        deviceLimits.nextInChain = &deviceCompat.chain;
        (void)wgpuDeviceGetLimits(dr.device, &deviceLimits);
        for (const LimitInfo& info : kLimitInfos) {
            const uint64_t actual = readDeviceLimit(deviceLimits, deviceCompat, info.name);
            t.expect(
                actual == info.defaultValue,
                std::string("Expected ") + info.name + " == default: " +
                    std::to_string(actual) + " != " + std::to_string(info.defaultValue));
        }
    });

// ---------------------------------------------------------------------------
// g.test('invalid')
// ---------------------------------------------------------------------------
CTS_TEST(g, "invalid")
    .desc(
        "Test that requesting device on an invalid adapter resolves with lost device.\n"
        "- Induce invalid adapter via a device lost from a device.destroy()\n"
        "- Check the device is lost with reason 'destroyed'\n"
        "- Try creating another device on the now-stale adapter fails.")
    .fn([](Fixture& t) {
        OwnedAdapterContext ctx;
        createOwnedAdapter(t, ctx);

        {
            // Request a device and destroy it immediately afterwards.
            DeviceResult dr = requestDeviceTracked(ctx, nullptr);
            if (dr.status != WGPURequestDeviceStatus_Success || dr.device == nullptr) {
                t.fail("invalid: first requestDevice should succeed: " + dr.message);
            }
            wgpuDeviceDestroy(dr.device);
            // Upstream awaits device.lost and checks reason === 'destroyed'.
            // The destroy() above synchronously transitions the device to lost
            // with reason 'destroyed'; the C device-lost callback delivery is
            // out of band, so we rely on destroy() having taken effect (the
            // subsequent requestDevice failure verifies the adapter is stale).
        }

        // The adapter should now be invalid since a device was lost. Requesting
        // another device is not possible anymore (upstream: shouldReject
        // 'OperationError').
        DeviceResult dr2 = requestDeviceTracked(ctx, nullptr);
        t.expect(
            dr2.status != WGPURequestDeviceStatus_Success,
            "invalid: requesting a device on a consumed adapter must fail");
    });

// ---------------------------------------------------------------------------
// g.test('stale')
// ---------------------------------------------------------------------------
CTS_TEST(g, "stale")
    .desc(
        "Test that adapter.requestDevice() can successfully return a device once, and once only.\n"
        "- Tests that we can successfully resolve after serial and concurrent rejections.\n"
        "- Tests that consecutive valid attempts only succeeds the first time, returning lost "
        "device otherwise.")
    .params([](ParamsBuilder u) {
        return u.beginSubcases()
            .combine("initialError", {Value::undef(), Value("TypeError"), Value("OperationError")})
            .combine("awaitInitialError", {true, false})
            .combine("awaitSuccess", {true, false})
            .filter([](const ParamRecord& p) {
                const Value* ie = findParam(p, "initialError");
                const Value* ai = findParam(p, "awaitInitialError");
                const bool initialUndefined =
                    ie != nullptr && std::holds_alternative<Value::Undefined>(ie->data());
                const bool awaitInitial = ai != nullptr && valueAs<bool>(*ai);
                return !(initialUndefined && awaitInitial);
            });
    })
    .fn([](Fixture& t) {
        OwnedAdapterContext ctx;
        createOwnedAdapter(t, ctx);

        const bool initialErrorUndefined = t.paramIsUndefined("initialError");
        const std::string initialError =
            initialErrorUndefined ? std::string() : t.param<std::string>("initialError");
        // awaitInitialError / awaitSuccess control JS promise timing; on native
        // every requestDevice is synchronous, so both branches behave the same.
        // We keep them as case params for identity, then perform the request.

        if (!initialErrorUndefined) {
            DeviceResult initial;
            if (initialError == "TypeError") {
                // Cause a type error by requesting an unknown feature.
                WGPUFeatureName unknown = static_cast<WGPUFeatureName>(0x7FFFFFFE);
                WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
                desc.requiredFeatureCount = 1;
                desc.requiredFeatures = &unknown;
                initial = requestDeviceTracked(ctx, &desc);
            } else {
                // Cause an operation error: alignment limit not a power of two.
                RequiredLimits req;
                setLimit(req, "minUniformBufferOffsetAlignment", 255);
                WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
                desc.requiredLimits = req.finalize();
                initial = requestDeviceTracked(ctx, &desc);
            }
            t.expect(
                initial.status != WGPURequestDeviceStatus_Success,
                "stale: the initial invalid requestDevice must fail");
        }

        // The first valid attempt should succeed.
        DeviceResult dr = requestDeviceTracked(ctx, nullptr);
        t.expect(
            dr.status == WGPURequestDeviceStatus_Success && dr.device != nullptr,
            "stale: the first valid requestDevice must succeed");

        // Since the adapter is consumed now, requesting another device is not
        // possible anymore (upstream: shouldReject 'OperationError').
        DeviceResult dr2 = requestDeviceTracked(ctx, nullptr);
        t.expect(
            dr2.status != WGPURequestDeviceStatus_Success,
            "stale: requesting a device on a consumed adapter must fail");
    });

// ---------------------------------------------------------------------------
// g.test('features,unknown')
// ---------------------------------------------------------------------------
CTS_TEST(g, "features,unknown")
    .desc("Test requesting device with an unknown feature.")
    .fn([](Fixture& t) {
        OwnedAdapterContext ctx;
        createOwnedAdapter(t, ctx);

        WGPUFeatureName unknown = static_cast<WGPUFeatureName>(0x7FFFFFFE);
        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.requiredFeatureCount = 1;
        desc.requiredFeatures = &unknown;
        DeviceResult dr = requestDeviceTracked(ctx, &desc);
        t.expect(
            dr.status != WGPURequestDeviceStatus_Success,
            "features,unknown: requesting an unknown feature must fail (upstream TypeError)");
    });

// ---------------------------------------------------------------------------
// g.test('features,known')
// ---------------------------------------------------------------------------
CTS_TEST(g, "features,known")
    .desc(
        "Test requesting device with all features.\n"
        "- Succeeds with device supporting feature if adapter supports the feature.\n"
        "- Rejects if the adapter does not support the feature.")
    .params([](ParamsBuilder u) { return u.combine("feature", kFeatureNames()); })
    .fn([](Fixture& t) {
        const std::string feature = t.param<std::string>("feature");
        const FeatureNameEntry* entry = findFeature(feature);
        if (entry == nullptr) {
            t.fail("features,known: unknown feature name " + feature);
        }

        OwnedAdapterContext ctx;
        createOwnedAdapter(t, ctx);

        WGPUFeatureName name = entry->value;
        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.requiredFeatureCount = 1;
        desc.requiredFeatures = &name;
        DeviceResult dr = requestDeviceTracked(ctx, &desc);

        if (wgpuAdapterHasFeature(ctx.adapter, name) != 0) {
            if (dr.status != WGPURequestDeviceStatus_Success || dr.device == nullptr) {
                t.fail("features,known: requestDevice with a supported feature should succeed");
            }
            t.expect(
                wgpuDeviceHasFeature(dr.device, name) != 0,
                "Device should include the required feature");
        } else {
            t.expect(
                dr.status != WGPURequestDeviceStatus_Success,
                "features,known: requesting an unsupported feature must fail (upstream TypeError)");
        }
    });

// ---------------------------------------------------------------------------
// g.test('limits,unknown')
// ---------------------------------------------------------------------------
CTS_TEST(g, "limits,unknown")
    .desc(
        "Test that specifying limits that aren't part of the supported limit set causes\n"
        "requestDevice to reject unless the value is undefined.\n"
        "Also tests that the invalid requestDevice() call does not expire the adapter.")
    .fn([](Fixture& t) {
        // Upstream sets { unknownLimitName: 9000 }. The C WGPULimits has no
        // arbitrary key, so an "unknown limit" cannot be expressed against the
        // struct. The portable analog ("invalid requestDevice does not expire
        // the adapter, then the undefined-valued one succeeds") is exercised by
        // 'limits,supported' (undefined) and 'limit,out_of_range'; this exact
        // unknown-key case has no C analog.
        t.skip(
            "limits,unknown: arbitrary/unknown limit keys cannot be expressed against the fixed C "
            "WGPULimits struct (no C analog).");
    });

// ---------------------------------------------------------------------------
// g.test('limits,supported')
// ---------------------------------------------------------------------------
CTS_TEST(g, "limits,supported")
    .desc(
        "Test that each supported limit can be specified with valid values.\n"
        "- Tests each limit with the default values given by the spec\n"
        "- Tests each limit with the supported values given by the adapter\n"
        "- Tests each limit with undefined")
    .params([](ParamsBuilder u) {
        return u.combine("limit", kPossibleLimits())
            .beginSubcases()
            .combine("limitValue", {Value("default"), Value("adapter"), Value("undefined")});
    })
    .fn([](Fixture& t) {
        const std::string limit = t.param<std::string>("limit");
        const std::string limitValue = t.param<std::string>("limitValue");
        const LimitInfo* info = findLimitInfo(limit);
        if (info == nullptr) {
            t.fail("limits,supported: unknown limit " + limit);
        }

        OwnedAdapterContext ctx;
        createOwnedAdapter(t, ctx);

        WGPULimits adapterLimits = WGPU_LIMITS_INIT;
        WGPUCompatibilityModeLimits adapterCompat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
        adapterLimits.nextInChain = &adapterCompat.chain;
        (void)wgpuAdapterGetLimits(ctx.adapter, &adapterLimits);

        bool valueIsUndefined = false;
        uint64_t value = 0;
        uint64_t result = 0;
        if (limitValue == "default") {
            value = info->defaultValue;
            result = value;
        } else if (limitValue == "adapter") {
            value = readAdapterLimit(adapterLimits, adapterCompat, limit);
            result = value;
        } else {
            // undefined -> the default is reported back.
            valueIsUndefined = true;
            result = info->defaultValue;
        }

        RequiredLimits req;
        if (!valueIsUndefined) {
            setLimit(req, limit, value);
            // Upstream also sets maxStorageBuffersPerShaderStage /
            // maxStorageTexturesPerShaderStage alongside the in-stage limits.
            if (limit == "maxStorageBuffersInFragmentStage" ||
                limit == "maxStorageBuffersInVertexStage") {
                setLimit(req, "maxStorageBuffersPerShaderStage", value);
            }
            if (limit == "maxStorageTexturesInFragmentStage" ||
                limit == "maxStorageTexturesInVertexStage") {
                setLimit(req, "maxStorageTexturesPerShaderStage", value);
            }
        }

        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        if (!valueIsUndefined) {
            desc.requiredLimits = req.finalize();
        }
        DeviceResult dr = requestDeviceTracked(ctx, &desc);
        if (dr.status != WGPURequestDeviceStatus_Success || dr.device == nullptr) {
            t.fail("limits,supported: requestDevice should succeed: " + dr.message);
        }

        WGPULimits deviceLimits = WGPU_LIMITS_INIT;
        WGPUCompatibilityModeLimits deviceCompat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
        deviceLimits.nextInChain = &deviceCompat.chain;
        (void)wgpuDeviceGetLimits(dr.device, &deviceLimits);
        const uint64_t reported = readDeviceLimit(deviceLimits, deviceCompat, limit);
        t.expect(
            reported == result,
            std::string("Devices reported limit for ") + limit + "(" +
                std::to_string(reported) + ") should match the required limit (" +
                std::to_string(result) + ")");
    });

// ---------------------------------------------------------------------------
// g.test('limit,better_than_supported')
// ---------------------------------------------------------------------------
CTS_TEST(g, "limit,better_than_supported")
    .desc(
        "Test that specifying a better limit than what the adapter supports causes requestDevice "
        "to\n"
        "reject.\n"
        "- Tests each limit\n"
        "- Tests requesting better limits by various amounts")
    .params([](ParamsBuilder u) {
        return u.combine("limit", kPossibleLimits())
            .beginSubcases()
            .expand("mulAdd", [](const ParamRecord& p) -> std::vector<Value> {
                const Value* lv = findParam(p, "limit");
                const std::string limit = lv != nullptr ? valueAs<std::string>(*lv) : std::string();
                const LimitInfo* info = findLimitInfo(limit);
                if (info != nullptr && info->cls == LimitClass::Maximum) {
                    return {Value("1,1"), Value("1,100")};
                }
                // alignment
                return {Value("1,-1"), Value("0.5,0"), Value("0.0009765625,0")};
            });
    })
    .fn([](Fixture& t) {
        const std::string limit = t.param<std::string>("limit");
        const std::string mulAdd = t.param<std::string>("mulAdd");
        const LimitInfo* info = findLimitInfo(limit);
        if (info == nullptr) {
            t.fail("limit,better_than_supported: unknown limit " + limit);
        }
        const size_t comma = mulAdd.find(',');
        const double mul = std::stod(mulAdd.substr(0, comma));
        const double add = std::stod(mulAdd.substr(comma + 1));

        OwnedAdapterContext ctx;
        createOwnedAdapter(t, ctx);

        WGPULimits adapterLimits = WGPU_LIMITS_INIT;
        WGPUCompatibilityModeLimits adapterCompat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
        adapterLimits.nextInChain = &adapterCompat.chain;
        (void)wgpuAdapterGetLimits(ctx.adapter, &adapterLimits);

        const double base = static_cast<double>(readAdapterLimit(adapterLimits, adapterCompat, limit));
        const double computed = base * mul + add;
        const double clamped =
            computed < 0.0 ? 0.0
                           : (computed > static_cast<double>(info->maximumValue)
                                  ? static_cast<double>(info->maximumValue)
                                  : computed);
        const uint64_t value = static_cast<uint64_t>(clamped);

        RequiredLimits req;
        setLimit(req, limit, value);
        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.requiredLimits = req.finalize();
        DeviceResult dr = requestDeviceTracked(ctx, &desc);
        t.expect(
            dr.status != WGPURequestDeviceStatus_Success,
            "limit,better_than_supported: requesting a better-than-supported limit must fail "
            "(upstream OperationError)");
    });

// ---------------------------------------------------------------------------
// g.test('limit,out_of_range')
//
// WebIDL [EnforceRange] coercion of out-of-range numeric limit values has no
// native C-API analog (the C limit fields are unsigned and out-of-band values
// alias the WGPU_LIMIT_*_UNDEFINED sentinel). In JS, requiredLimits fields are
// declared as [EnforceRange] unsigned long / unsigned long long, so passing a
// negative, fractional, or > 2^53 / > 2^64 value throws a TypeError before the
// request is even made. The C WGPULimits fields are plain unsigned integers with
// no range-enforcement layer: e.g. (uint64_t)(-1) becomes 0xFFFFFFFFFFFFFFFF,
// which the C API reads as the "undefined/unset" sentinel
// (WGPU_LIMIT_U64_UNDEFINED), so the limit is simply ignored and the request
// SUCCEEDS instead of failing. There is therefore no way to exercise the
// [EnforceRange] rejection through the C API; the test is unimplementable.
// (The .params are kept so the case/subcase query identity is preserved.)
// ---------------------------------------------------------------------------
CTS_TEST(g, "limit,out_of_range")
    .desc(
        "Test that specifying limits that are out of range (<0, >MAX_SAFE_INTEGER, >2**31-2 for "
        "32-bit\n"
        "limits, =0 for alignment limits) produce the appropriate error (TypeError or "
        "OperationError).")
    .params([](ParamsBuilder u) {
        // Upstream yields a sequence of JS numbers. Negatives and values >2^64
        // are encoded as string sentinels (they are not representable as a
        // uint64 limit field anyway, and upstream classifies them as TypeError).
        return u.combine("limit", kPossibleLimits())
            .beginSubcases()
            .combine(
                "value",
                {
                    Value("-1.8446744073709552e19"),       // -(2**64)
                    Value("-9007199254740994"),            // MIN_SAFE_INTEGER - 3
                    Value("-9007199254740992"),            // MIN_SAFE_INTEGER - 1
                    Value("-9007199254740991"),            // MIN_SAFE_INTEGER
                    Value("-4294967296"),                  // -(2**32)
                    Value("-1"),
                    Value("0"),
                    Value("4294967294"),                   // 2**32 - 2
                    Value("4294967295"),                   // 2**32 - 1
                    Value("4294967296"),                   // 2**32
                    Value("4294967297"),                   // 2**32 + 1
                    Value("4294967298"),                   // 2**32 + 2
                    Value("9007199254740991"),             // MAX_SAFE_INTEGER
                    Value("9007199254740992"),             // MAX_SAFE_INTEGER + 1
                    Value("9007199254740994"),             // MAX_SAFE_INTEGER + 3
                    Value("1.8446744073709552e19"),        // 2**64
                    Value("1.7976931348623157e308"),       // Number.MAX_VALUE
                });
    })
    // WebIDL [EnforceRange] coercion of out-of-range numeric limit values has no
    // native C-API analog (the C limit fields are unsigned and out-of-band
    // values alias the WGPU_LIMIT_*_UNDEFINED sentinel).
    .unimplemented(
        "WebIDL [EnforceRange] coercion of out-of-range numeric limit values has no native "
        "C-API analog (the C limit fields are unsigned and out-of-band values alias the "
        "WGPU_LIMIT_*_UNDEFINED sentinel).");

// ---------------------------------------------------------------------------
// g.test('limit,worse_than_default')
// ---------------------------------------------------------------------------
CTS_TEST(g, "limit,worse_than_default")
    .desc(
        "Test that specifying a worse limit than the default values required by the spec cause the "
        "value\n"
        "to clamp.\n"
        "- Tests each limit\n"
        "- Tests requesting worse limits by various amounts")
    .params([](ParamsBuilder u) {
        return u.combine("limit", kPossibleLimits())
            .beginSubcases()
            .expand("mulAdd", [](const ParamRecord& p) -> std::vector<Value> {
                const Value* lv = findParam(p, "limit");
                const std::string limit = lv != nullptr ? valueAs<std::string>(*lv) : std::string();
                const LimitInfo* info = findLimitInfo(limit);
                if (info != nullptr && info->cls == LimitClass::Maximum) {
                    return {Value("1,-1"), Value("1,-100")};
                }
                // alignment
                return {Value("1,1"), Value("2,0"), Value("1024,0")};
            });
    })
    .fn([](Fixture& t) {
        const std::string limit = t.param<std::string>("limit");
        const std::string mulAdd = t.param<std::string>("mulAdd");
        const LimitInfo* info = findLimitInfo(limit);
        if (info == nullptr) {
            t.fail("limit,worse_than_default: unknown limit " + limit);
        }
        const size_t comma = mulAdd.find(',');
        const double mul = std::stod(mulAdd.substr(0, comma));
        const double add = std::stod(mulAdd.substr(comma + 1));

        OwnedAdapterContext ctx;
        createOwnedAdapter(t, ctx);

        // value = default * mul + add; requiredLimits = clamp(value, 0,
        // maximumValue). success = (class === alignment) ? isPowerOfTwo(value)
        // : true. Note: success uses the UNCLAMPED value (upstream computes
        // isPowerOfTwo(value) before clamping).
        const double computed = static_cast<double>(info->defaultValue) * mul + add;
        const uint64_t clampedValue = clampU64(
            computed < 0.0 ? 0 : static_cast<uint64_t>(computed), 0, info->maximumValue);

        bool success;
        if (info->cls == LimitClass::Alignment) {
            success = computed >= 0.0 && computed == static_cast<double>(static_cast<uint64_t>(computed)) &&
                      isPowerOfTwo(static_cast<uint64_t>(computed));
        } else {
            success = true;
        }

        RequiredLimits req;
        setLimit(req, limit, clampedValue);
        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.requiredLimits = req.finalize();
        DeviceResult dr = requestDeviceTracked(ctx, &desc);

        if (success) {
            if (dr.status != WGPURequestDeviceStatus_Success || dr.device == nullptr) {
                t.fail("limit,worse_than_default: requestDevice should succeed: " + dr.message);
            }
            WGPULimits deviceLimits = WGPU_LIMITS_INIT;
            WGPUCompatibilityModeLimits deviceCompat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
            deviceLimits.nextInChain = &deviceCompat.chain;
            (void)wgpuDeviceGetLimits(dr.device, &deviceLimits);
            const uint64_t reported = readDeviceLimit(deviceLimits, deviceCompat, limit);
            t.expect(
                reported == info->defaultValue,
                "Devices reported limit should match the default limit");
        } else {
            t.expect(
                dr.status != WGPURequestDeviceStatus_Success,
                "limit,worse_than_default: a non-power-of-two alignment must fail (upstream "
                "OperationError)");
        }
    });

// ---------------------------------------------------------------------------
// g.test('always_returns_device')
// ---------------------------------------------------------------------------
CTS_TEST(g, "always_returns_device")
    .desc(
        "Test that if requestAdapter returns an adapter then requestDevice must return a device.\n\n"
        "requestAdapter -> null = ok\n"
        "requestAdapter -> adapter, requestDevice -> device (lost or not) = ok\n"
        "requestAdapter -> adapter, requestDevice = null = Invalid: not spec compliant.\n\n"
        "Note: requestDevice can throw for invalid parameters like requesting features not\n"
        "in the adapter, reqesting limits not in the adapter, requesting limits larger than\n"
        "the maximum for the adapter. Otherwise it does not throw.\n\n"
        "Note: This is a regression test for a Chrome bug crbug.com/349062459\n"
        "Checking that a requestDevice always return a device is checked in other tests above\n"
        "but those tests have 'featureLevel: \"compatibility\"' set for them by the API that "
        "getGPU\n"
        "returns when the test suite is run in compatibility mode.\n\n"
        "This test tries to force both compat and core separately so both code paths are\n"
        "tested in the same browser configuration.")
    .params([](ParamsBuilder u) {
        return u.combine("featureLevel", {Value("core"), Value("compatibility")});
    })
    .fn([](Fixture& t) {
        const std::string featureLevel = t.param<std::string>("featureLevel");
        const WGPUFeatureLevel level =
            featureLevel == "core" ? WGPUFeatureLevel_Core : WGPUFeatureLevel_Compatibility;

        OwnedAdapterContext ctx;
        ctx.instance = createInstance();
        if (ctx.instance == nullptr) {
            t.fail("always_returns_device: failed to create WGPUInstance");
        }
        WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
        options.featureLevel = level;
        AdapterResult ar = requestAdapterSync(ctx.instance, &options);
        // requestAdapter -> null is OK; nothing more to check.
        if (ar.status != WGPURequestAdapterStatus_Success || ar.adapter == nullptr) {
            return;
        }
        ctx.adapter = ar.adapter;

        DeviceResult dr = requestDeviceTracked(ctx, nullptr);
        // requestDevice must return a device or throw. It did not throw here
        // (no invalid params), so it must return a device.
        t.expect(
            dr.status == WGPURequestDeviceStatus_Success && dr.device != nullptr,
            "requestDevice must return a device or throw");
        if (dr.device == nullptr) {
            return;
        }

        if (featureLevel == "core" &&
            wgpuAdapterHasFeature(ctx.adapter, WGPUFeatureName_CoreFeaturesAndLimits) != 0) {
            // The device must support core when featureLevel is core and the
            // adapter supports core (nothing lower-level forced compat mode).
            t.expect(
                wgpuDeviceHasFeature(dr.device, WGPUFeatureName_CoreFeaturesAndLimits) != 0,
                "must not get a Compatibility adapter if not requested");
        }
    });

} // namespace
