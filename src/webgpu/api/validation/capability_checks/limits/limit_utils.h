// Ported from gpuweb/cts src/webgpu/api/validation/capability_checks/limits/limit_utils.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"
#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

namespace cts::capability_limits {

inline WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

inline WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

constexpr uint64_t kMaxUnsignedLongValue = 4294967295ull;
constexpr uint64_t kMaxUnsignedLongLongValue = 9007199254740991ull;

enum class LimitClass {
    Maximum,
    Alignment,
};

struct LimitInfo {
    const char* name;
    LimitClass cls;
    uint64_t defaultValue;
    uint64_t maximumValue;
};

inline constexpr std::array<LimitInfo, 35> kLimitInfos = {{
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

inline const LimitInfo* findLimitInfo(std::string_view name) {
    for (const LimitInfo& info : kLimitInfos) {
        if (name == info.name) {
            return &info;
        }
    }
    return nullptr;
}

inline uint64_t getDefaultLimit(std::string_view name) {
    const LimitInfo* info = findLimitInfo(name);
    if (info == nullptr) {
        std::abort();
    }
    return info->defaultValue;
}

inline bool isMinimumLimit(std::string_view name) {
    return name == "minUniformBufferOffsetAlignment" || name == "minStorageBufferOffsetAlignment";
}

inline bool isUndefinedLimitValue(uint64_t value) {
    return value == WGPU_LIMIT_U32_UNDEFINED || value == WGPU_LIMIT_U64_UNDEFINED;
}

inline uint64_t getLimitValueByName(
    const WGPULimits& limits,
    const WGPUCompatibilityModeLimits& compat,
    std::string_view name) {
    if (name == "maxTextureDimension1D") return limits.maxTextureDimension1D;
    if (name == "maxTextureDimension2D") return limits.maxTextureDimension2D;
    if (name == "maxTextureDimension3D") return limits.maxTextureDimension3D;
    if (name == "maxTextureArrayLayers") return limits.maxTextureArrayLayers;
    if (name == "maxBindGroups") return limits.maxBindGroups;
    if (name == "maxBindGroupsPlusVertexBuffers") return limits.maxBindGroupsPlusVertexBuffers;
    if (name == "maxBindingsPerBindGroup") return limits.maxBindingsPerBindGroup;
    if (name == "maxDynamicUniformBuffersPerPipelineLayout") return limits.maxDynamicUniformBuffersPerPipelineLayout;
    if (name == "maxDynamicStorageBuffersPerPipelineLayout") return limits.maxDynamicStorageBuffersPerPipelineLayout;
    if (name == "maxSampledTexturesPerShaderStage") return limits.maxSampledTexturesPerShaderStage;
    if (name == "maxSamplersPerShaderStage") return limits.maxSamplersPerShaderStage;
    if (name == "maxStorageBuffersInFragmentStage") return compat.maxStorageBuffersInFragmentStage;
    if (name == "maxStorageBuffersInVertexStage") return compat.maxStorageBuffersInVertexStage;
    if (name == "maxStorageBuffersPerShaderStage") return limits.maxStorageBuffersPerShaderStage;
    if (name == "maxStorageTexturesInFragmentStage") return compat.maxStorageTexturesInFragmentStage;
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
    if (name == "maxComputeInvocationsPerWorkgroup") return limits.maxComputeInvocationsPerWorkgroup;
    if (name == "maxComputeWorkgroupSizeX") return limits.maxComputeWorkgroupSizeX;
    if (name == "maxComputeWorkgroupSizeY") return limits.maxComputeWorkgroupSizeY;
    if (name == "maxComputeWorkgroupSizeZ") return limits.maxComputeWorkgroupSizeZ;
    if (name == "maxComputeWorkgroupsPerDimension") return limits.maxComputeWorkgroupsPerDimension;
    std::abort();
}

struct RequiredLimits {
    WGPULimits limits = WGPU_LIMITS_INIT;
    WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
    bool useCompat = false;

    const WGPULimits* finalize() {
        limits.nextInChain = useCompat ? &compat.chain : nullptr;
        return &limits;
    }
};

inline void setRequiredLimitByName(RequiredLimits& required, std::string_view name, uint64_t value) {
    const auto setU32 = [&](uint32_t& field) {
        field = static_cast<uint32_t>(value);
    };
    if (name == "maxTextureDimension1D") return setU32(required.limits.maxTextureDimension1D);
    if (name == "maxTextureDimension2D") return setU32(required.limits.maxTextureDimension2D);
    if (name == "maxTextureDimension3D") return setU32(required.limits.maxTextureDimension3D);
    if (name == "maxTextureArrayLayers") return setU32(required.limits.maxTextureArrayLayers);
    if (name == "maxBindGroups") return setU32(required.limits.maxBindGroups);
    if (name == "maxBindGroupsPlusVertexBuffers") return setU32(required.limits.maxBindGroupsPlusVertexBuffers);
    if (name == "maxBindingsPerBindGroup") return setU32(required.limits.maxBindingsPerBindGroup);
    if (name == "maxDynamicUniformBuffersPerPipelineLayout") return setU32(required.limits.maxDynamicUniformBuffersPerPipelineLayout);
    if (name == "maxDynamicStorageBuffersPerPipelineLayout") return setU32(required.limits.maxDynamicStorageBuffersPerPipelineLayout);
    if (name == "maxSampledTexturesPerShaderStage") return setU32(required.limits.maxSampledTexturesPerShaderStage);
    if (name == "maxSamplersPerShaderStage") return setU32(required.limits.maxSamplersPerShaderStage);
    if (name == "maxStorageBuffersInFragmentStage") {
        required.useCompat = true;
        return setU32(required.compat.maxStorageBuffersInFragmentStage);
    }
    if (name == "maxStorageBuffersInVertexStage") {
        required.useCompat = true;
        return setU32(required.compat.maxStorageBuffersInVertexStage);
    }
    if (name == "maxStorageBuffersPerShaderStage") return setU32(required.limits.maxStorageBuffersPerShaderStage);
    if (name == "maxStorageTexturesInFragmentStage") {
        required.useCompat = true;
        return setU32(required.compat.maxStorageTexturesInFragmentStage);
    }
    if (name == "maxStorageTexturesInVertexStage") {
        required.useCompat = true;
        return setU32(required.compat.maxStorageTexturesInVertexStage);
    }
    if (name == "maxStorageTexturesPerShaderStage") return setU32(required.limits.maxStorageTexturesPerShaderStage);
    if (name == "maxUniformBuffersPerShaderStage") return setU32(required.limits.maxUniformBuffersPerShaderStage);
    if (name == "maxUniformBufferBindingSize") {
        required.limits.maxUniformBufferBindingSize = value;
        return;
    }
    if (name == "maxStorageBufferBindingSize") {
        required.limits.maxStorageBufferBindingSize = value;
        return;
    }
    if (name == "minUniformBufferOffsetAlignment") return setU32(required.limits.minUniformBufferOffsetAlignment);
    if (name == "minStorageBufferOffsetAlignment") return setU32(required.limits.minStorageBufferOffsetAlignment);
    if (name == "maxVertexBuffers") return setU32(required.limits.maxVertexBuffers);
    if (name == "maxBufferSize") {
        required.limits.maxBufferSize = value;
        return;
    }
    if (name == "maxVertexAttributes") return setU32(required.limits.maxVertexAttributes);
    if (name == "maxVertexBufferArrayStride") return setU32(required.limits.maxVertexBufferArrayStride);
    if (name == "maxInterStageShaderVariables") return setU32(required.limits.maxInterStageShaderVariables);
    if (name == "maxColorAttachments") return setU32(required.limits.maxColorAttachments);
    if (name == "maxColorAttachmentBytesPerSample") return setU32(required.limits.maxColorAttachmentBytesPerSample);
    if (name == "maxComputeWorkgroupStorageSize") return setU32(required.limits.maxComputeWorkgroupStorageSize);
    if (name == "maxComputeInvocationsPerWorkgroup") return setU32(required.limits.maxComputeInvocationsPerWorkgroup);
    if (name == "maxComputeWorkgroupSizeX") return setU32(required.limits.maxComputeWorkgroupSizeX);
    if (name == "maxComputeWorkgroupSizeY") return setU32(required.limits.maxComputeWorkgroupSizeY);
    if (name == "maxComputeWorkgroupSizeZ") return setU32(required.limits.maxComputeWorkgroupSizeZ);
    if (name == "maxComputeWorkgroupsPerDimension") return setU32(required.limits.maxComputeWorkgroupsPerDimension);
    std::abort();
}

inline WGPULimits queryLimits(WGPUAdapter adapter, WGPUCompatibilityModeLimits* compat) {
    WGPULimits limits = WGPU_LIMITS_INIT;
    limits.nextInChain = &compat->chain;
    if (wgpuAdapterGetLimits(adapter, &limits) != WGPUStatus_Success) {
        std::abort();
    }
    return limits;
}

inline WGPULimits queryLimits(WGPUDevice device, WGPUCompatibilityModeLimits* compat) {
    WGPULimits limits = WGPU_LIMITS_INIT;
    limits.nextInChain = &compat->chain;
    if (wgpuDeviceGetLimits(device, &limits) != WGPUStatus_Success) {
        std::abort();
    }
    return limits;
}

inline std::vector<Value> kMaximumLimitBaseParamValues(std::string_view name) {
    if (name == "limitTest") {
        return {std::string("atDefault"), std::string("underDefault"),
                std::string("betweenDefaultAndMaximum"), std::string("atMaximum"),
                std::string("overMaximum")};
    }
    if (name == "testValueName") {
        return {std::string("atLimit"), std::string("overLimit")};
    }
    std::abort();
}

inline ParamsBuilder kMaximumLimitBaseParams(ParamsBuilder u) {
    return u.combine("limitTest", kMaximumLimitBaseParamValues("limitTest"))
        .combine("testValueName", kMaximumLimitBaseParamValues("testValueName"));
}

inline ParamsBuilder kMinimumLimitBaseParams(ParamsBuilder u) {
    return u.combine("limitTest",
                     {std::string("atDefault"), std::string("overDefault"),
                      std::string("betweenDefaultAndMinimum"), std::string("atMinimum"),
                      std::string("underMinimum")})
        .combine("testValueName", {std::string("atLimit"), std::string("underLimit")});
}

inline uint64_t getLimitValue(uint64_t defaultLimit, uint64_t maximumLimit, std::string_view limitValueTest) {
    if (limitValueTest == "atDefault") return defaultLimit;
    if (limitValueTest == "underDefault") return defaultLimit - 1;
    if (limitValueTest == "betweenDefaultAndMaximum") return (defaultLimit + maximumLimit) / 2;
    if (limitValueTest == "atMaximum") return maximumLimit;
    if (limitValueTest == "overMaximum") return maximumLimit + 1;
    std::abort();
}

inline uint64_t getMaximumTestValue(uint64_t limit, std::string_view testValueName) {
    if (testValueName == "atLimit") return limit;
    if (testValueName == "overLimit") return limit + 1;
    std::abort();
}

enum class LimitRequestMode {
    DefaultLimit,
    AdapterLimit,
    Numeric,
};

struct LimitRequestEntry {
    std::string name;
    LimitRequestMode mode;
    uint64_t value;
};

inline LimitRequestEntry defaultLimitRequest(std::string name) {
    return LimitRequestEntry{std::move(name), LimitRequestMode::DefaultLimit, 0};
}

inline LimitRequestEntry adapterLimitRequest(std::string name) {
    return LimitRequestEntry{std::move(name), LimitRequestMode::AdapterLimit, 0};
}

inline LimitRequestEntry numericLimitRequest(std::string name, uint64_t value) {
    return LimitRequestEntry{std::move(name), LimitRequestMode::Numeric, value};
}

struct DeviceAndLimits {
    WGPUDevice device = nullptr;
    uint64_t defaultLimit = 0;
    uint64_t adapterLimit = 0;
    uint64_t requestedLimit = 0;
    uint64_t actualLimit = 0;
};

struct SpecificLimitTestInputs : DeviceAndLimits {
    uint64_t testValue = 0;
    bool shouldError = false;
};

struct MaximumLimitTestInputs : SpecificLimitTestInputs {
    std::string testValueName;
};

class LimitTest : public GpuTest {
  public:
    void init() override {
        instance_ = createInstance();
        if (instance_ == nullptr) {
            fail("failed to create WGPUInstance");
        }
        AdapterResult adapterResult = requestAdapterSync(instance_, nullptr);
        if (adapterResult.status != WGPURequestAdapterStatus_Success || adapterResult.adapter == nullptr) {
            fail("failed to request adapter: " + adapterResult.message);
        }
        adapter_ = adapterResult.adapter;
        const LimitInfo* info = findLimitInfo(limitName());
        if (info == nullptr) {
            skip(std::string(limitName()) + " is missing but optional for now");
        }
        defaultLimit = info->defaultValue;
        WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
        const WGPULimits limits = queryLimits(adapter_, &compat);
        adapterLimit = getLimitValueByName(limits, compat, limitName());
        if (isUndefinedLimitValue(adapterLimit)) {
            skip(std::string(limitName()) + " is missing but optional for now");
        }
    }

    void finalize() override {
        GpuTest::finalize();
        for (WGPUDevice deviceHandle : ownedDevices_) {
            if (deviceHandle != nullptr) {
                wgpuDeviceRelease(deviceHandle);
            }
        }
        ownedDevices_.clear();
        if (adapter_ != nullptr) {
            wgpuAdapterRelease(adapter_);
            adapter_ = nullptr;
        }
        if (instance_ != nullptr) {
            wgpuInstanceRelease(instance_);
            instance_ = nullptr;
        }
    }

    WGPUDevice device() const override {
        LimitTest* self = const_cast<LimitTest*>(this);
        if (self->currentDevice_ == nullptr) {
            self->fail("device is only valid in _testThenDestroyDevice callback");
        }
        return self->currentDevice_;
    }

    WGPUQueue queue() const override {
        LimitTest* self = const_cast<LimitTest*>(this);
        if (self->currentQueue_ == nullptr) {
            self->currentQueue_ = wgpuDeviceGetQueue(self->device());
        }
        return self->currentQueue_;
    }

    WGPUAdapter adapter() const {
        return adapter_;
    }

    virtual const char* limitName() const = 0;

    uint64_t getDefaultLimit(std::string_view name) const {
        return capability_limits::getDefaultLimit(name);
    }

    uint64_t getAdapterLimit(std::string_view name) const {
        WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
        const WGPULimits limits = queryLimits(adapter_, &compat);
        return getLimitValueByName(limits, compat, name);
    }

    std::optional<DeviceAndLimits> requestDeviceWithLimits(
        const std::vector<LimitRequestEntry>& limits,
        bool shouldReject,
        const std::vector<WGPUFeatureName>& features = {}) {
        RequiredLimits required;
        for (const LimitRequestEntry& entry : limits) {
            uint64_t value = entry.value;
            if (entry.mode == LimitRequestMode::DefaultLimit) {
                value = getDefaultLimit(entry.name);
            } else if (entry.mode == LimitRequestMode::AdapterLimit) {
                value = getAdapterLimit(entry.name);
            }
            setRequiredLimitByName(required, entry.name, value);
        }

        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.requiredLimits = required.finalize();
        desc.requiredFeatureCount = features.size();
        desc.requiredFeatures = features.empty() ? nullptr : features.data();
        DeviceResult result = requestDeviceSync(instance_, adapter_, &desc);
        const bool success = result.status == WGPURequestDeviceStatus_Success && result.device != nullptr;
        if (shouldReject) {
            if (success) {
                wgpuDeviceDestroy(result.device);
                wgpuDeviceRelease(result.device);
                fail("expected requestDevice failure, got a device");
            }
            return std::nullopt;
        }
        if (!success) {
            fail("requestDevice failed: " + result.message);
        }
        ownedDevices_.push_back(result.device);
        return DeviceAndLimits{result.device, defaultLimit, adapterLimit, 0, 0};
    }

    std::optional<DeviceAndLimits> getDeviceWithSpecificLimit(
        uint64_t requestedLimit,
        const std::vector<LimitRequestEntry>& extraLimits = {},
        const std::vector<WGPUFeatureName>& features = {}) {
        std::vector<LimitRequestEntry> requested;
        requested.push_back(numericLimitRequest(limitName(), requestedLimit));
        requested.insert(requested.end(), extraLimits.begin(), extraLimits.end());
        const bool shouldReject = isMinimumLimit(limitName())
            ? requestedLimit < adapterLimit
            : requestedLimit > adapterLimit;

        std::optional<DeviceAndLimits> result = requestDeviceWithLimits(requested, shouldReject, features);
        if (!result.has_value()) {
            return std::nullopt;
        }

        WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
        const WGPULimits limits = queryLimits(result->device, &compat);
        const uint64_t actualLimit = getLimitValueByName(limits, compat, limitName());
        if (isMinimumLimit(limitName())) {
            const uint64_t expected = requestedLimit <= defaultLimit ? requestedLimit : defaultLimit;
            expect(actualLimit == expected, "unexpected actual minimum limit");
        } else {
            const uint64_t expected = requestedLimit <= defaultLimit ? defaultLimit : requestedLimit;
            expect(actualLimit == expected, "unexpected actual maximum limit");
        }
        result->requestedLimit = requestedLimit;
        result->actualLimit = actualLimit;
        return result;
    }

    std::optional<DeviceAndLimits> getDeviceWithRequestedMaximumLimit(
        std::string_view limitTest,
        const std::vector<LimitRequestEntry>& extraLimits = {},
        const std::vector<WGPUFeatureName>& features = {}) {
        const uint64_t requestedLimit = getLimitValue(defaultLimit, adapterLimit, limitTest);
        if (limitTest == "underDefault" && requestedLimit > defaultLimit) {
            skip("requestedLimit for underDefault underflowed");
        }
        if (limitTest != "atDefault" && requestedLimit == defaultLimit) {
            skip("The limit value for this case is the same as the default.");
        }
        return getDeviceWithSpecificLimit(requestedLimit, extraLimits, features);
    }

    void testThenDestroyDevice(
        const DeviceAndLimits& deviceAndLimits,
        uint64_t testValue,
        const std::function<void(const SpecificLimitTestInputs&)>& fn) {
        if (currentDevice_ != nullptr) {
            fail("nested limit device test");
        }
        currentDevice_ = deviceAndLimits.device;
        const bool shouldError = isMinimumLimit(limitName())
            ? testValue < deviceAndLimits.actualLimit
            : testValue > deviceAndLimits.actualLimit;

        wgpuDevicePushErrorScope(currentDevice_, WGPUErrorFilter_Internal);
        wgpuDevicePushErrorScope(currentDevice_, WGPUErrorFilter_OutOfMemory);
        wgpuDevicePushErrorScope(currentDevice_, WGPUErrorFilter_Validation);

        SpecificLimitTestInputs inputs;
        static_cast<DeviceAndLimits&>(inputs) = deviceAndLimits;
        inputs.testValue = testValue;
        inputs.shouldError = shouldError;
        fn(inputs);

        ScopeResult validation = popErrorScopeSync(instance_, currentDevice_);
        ScopeResult oom = popErrorScopeSync(instance_, currentDevice_);
        ScopeResult internal = popErrorScopeSync(instance_, currentDevice_);
        if (validation.status != WGPUPopErrorScopeStatus_Success ||
            oom.status != WGPUPopErrorScopeStatus_Success ||
            internal.status != WGPUPopErrorScopeStatus_Success) {
            fail("popErrorScope failed while destroying limit test device");
        }
        if (validation.type != WGPUErrorType_NoError) {
            fail("unexpected validation error: " + validation.message);
        }
        if (oom.type != WGPUErrorType_NoError) {
            fail("unexpected out-of-memory error: " + oom.message);
        }
        if (internal.type != WGPUErrorType_NoError) {
            fail("unexpected internal error: " + internal.message);
        }

        wgpuDeviceDestroy(currentDevice_);
        if (currentQueue_ != nullptr) {
            wgpuQueueRelease(currentQueue_);
            currentQueue_ = nullptr;
        }
        currentDevice_ = nullptr;
    }

    void testDeviceWithSpecificLimits(
        uint64_t deviceLimitValue,
        uint64_t testValue,
        const std::function<void(const SpecificLimitTestInputs&)>& fn,
        const std::vector<LimitRequestEntry>& extraLimits = {},
        const std::vector<WGPUFeatureName>& features = {}) {
        std::optional<DeviceAndLimits> deviceAndLimits =
            getDeviceWithSpecificLimit(deviceLimitValue, extraLimits, features);
        if (!deviceAndLimits.has_value()) {
            return;
        }
        testThenDestroyDevice(*deviceAndLimits, testValue, fn);
    }

    void testDeviceWithRequestedMaximumLimits(
        std::string_view limitTest,
        std::string_view testValueName,
        const std::function<void(const MaximumLimitTestInputs&)>& fn,
        const std::vector<LimitRequestEntry>& extraLimits = {},
        const std::vector<WGPUFeatureName>& features = {}) {
        std::optional<DeviceAndLimits> deviceAndLimits =
            getDeviceWithRequestedMaximumLimit(limitTest, extraLimits, features);
        if (!deviceAndLimits.has_value()) {
            return;
        }
        const uint64_t testValue = getMaximumTestValue(deviceAndLimits->actualLimit, testValueName);
        testThenDestroyDevice(*deviceAndLimits, testValue, [&](const SpecificLimitTestInputs& inputs) {
            MaximumLimitTestInputs maximumInputs;
            static_cast<SpecificLimitTestInputs&>(maximumInputs) = inputs;
            maximumInputs.testValueName = std::string(testValueName);
            fn(maximumInputs);
        });
    }

    void expectValidationErrorOnLimitDevice(const std::function<void()>& body, bool shouldError, std::string_view msg = {}) {
        wgpuDevicePushErrorScope(device(), WGPUErrorFilter_Validation);
        body();
        ScopeResult result = popErrorScopeSync(instance_, device());
        if (result.status != WGPUPopErrorScopeStatus_Success) {
            fail("popErrorScope failed: " + result.message);
        }
        const bool hadError = result.type != WGPUErrorType_NoError;
        if (hadError != shouldError) {
            std::string detail = hadError ? result.message : std::string("no error when one was expected");
            if (!msg.empty()) {
                detail += ": ";
                detail += std::string(msg);
            }
            fail(detail);
        }
    }

    ScopeResult popErrorScopeOnLimitDevice() {
        return popErrorScopeSync(instance_, device());
    }

    void testForValidationErrorWithPossibleOutOfMemoryError(
        const std::function<void()>& body,
        bool shouldError,
        std::string_view msg = {}) {
        if (!shouldError) {
            wgpuDevicePushErrorScope(device(), WGPUErrorFilter_OutOfMemory);
            body();
            (void)popErrorScopeSync(instance_, device());
            return;
        }
        expectValidationErrorOnLimitDevice(body, true, msg);
    }

    WGPUShaderModule getModuleForWorkgroupSize(const std::array<uint64_t, 3>& size, std::string* codeOut = nullptr) {
        std::ostringstream code;
        code << "@group(0) @binding(0) var<storage, read_write> d: f32;\n"
             << "@compute @workgroup_size(" << size[0] << "," << size[1] << "," << size[2] << ") fn main() {\n"
             << "  d = 0;\n"
             << "}\n";
        const std::string codeString = code.str();
        if (codeOut != nullptr) {
            *codeOut = codeString;
        }
        return createShaderModuleTracked(codeString);
    }

    std::string getGroupIndexWGSLForPipelineType(std::string_view pipelineType, uint64_t groupIndex) {
        std::ostringstream code;
        code << "@group(" << groupIndex << ") @binding(0) var<uniform> v: f32;\n";
        if (pipelineType == "createComputePipeline") {
            code << "@compute @workgroup_size(1) fn main() { _ = v; }\n";
        } else {
            code << "@vertex fn mainVS() -> @builtin(position) vec4f { return vec4f(v); }\n";
            if (pipelineType == "createRenderPipelineWithFragmentStage") {
                code << "@fragment fn mainFS() -> @location(0) vec4f { return vec4f(1); }\n";
            }
        }
        return code.str();
    }

    std::string getBindingIndexWGSLForPipelineType(std::string_view pipelineType, uint64_t bindingIndex) {
        std::ostringstream code;
        code << "@group(0) @binding(" << bindingIndex << ") var<uniform> v: f32;\n";
        if (pipelineType == "createComputePipeline") {
            code << "@compute @workgroup_size(1) fn main() { _ = v; }\n";
        } else {
            code << "@vertex fn mainVS() -> @builtin(position) vec4f { return vec4f(v); }\n";
            if (pipelineType == "createRenderPipelineWithFragmentStage") {
                code << "@fragment fn mainFS() -> @location(0) vec4f { return vec4f(1); }\n";
            }
        }
        return code.str();
    }

    WGPURenderPipelineDescriptor createRenderPipelineDescriptor(
        WGPUShaderModule module,
        WGPUShaderModule fragmentModule,
        WGPUFragmentState* fragment,
        WGPUColorTargetState* target) {
        target->format = WGPUTextureFormat_RGBA8Unorm;
        target->writeMask = WGPUColorWriteMask_All;
        *fragment = WGPU_FRAGMENT_STATE_INIT;
        fragment->module = fragmentModule;
        fragment->entryPoint = sv("main");
        fragment->targetCount = 1;
        fragment->targets = target;
        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = nullptr;
        desc.vertex.module = module;
        desc.vertex.entryPoint = sv("mainVS");
        desc.fragment = fragment;
        return desc;
    }

    void testCreatePipeline(
        std::string_view pipelineType,
        bool,
        WGPUShaderModule module,
        bool shouldError,
        std::string_view msg = {}) {
        // Native has no async pipeline promise path; async=true uses the same synchronous create.
        if (pipelineType == "createComputePipeline") {
            WGPUComputePipelineDescriptor desc = WGPU_COMPUTE_PIPELINE_DESCRIPTOR_INIT;
            desc.layout = nullptr;
            desc.compute.module = module;
            desc.compute.entryPoint = sv("main");
            expectValidationErrorOnLimitDevice([&] {
                createComputePipelineTracked(desc);
            }, shouldError, msg);
            return;
        }

        WGPUShaderModule fallbackFragment = createShaderModuleTracked(
            "@fragment fn main() -> @location(0) vec4f { return vec4f(0); }\n");
        WGPUColorTargetState target = WGPU_COLOR_TARGET_STATE_INIT;
        WGPUFragmentState fragment = WGPU_FRAGMENT_STATE_INIT;
        WGPUDepthStencilState depthStencil = WGPU_DEPTH_STENCIL_STATE_INIT;
        WGPURenderPipelineDescriptor desc = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
        desc.layout = nullptr;
        desc.vertex.module = module;
        desc.vertex.entryPoint = sv("mainVS");
        if (pipelineType == "createRenderPipelineWithFragmentStage") {
            fragment.module = module;
            fragment.entryPoint = sv("mainFS");
            fragment.targetCount = 0;
            fragment.targets = nullptr;
            desc.fragment = &fragment;
            depthStencil.format = WGPUTextureFormat_Depth24PlusStencil8;
            depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
            depthStencil.depthCompare = WGPUCompareFunction_Always;
            desc.depthStencil = &depthStencil;
        } else {
            target.format = WGPUTextureFormat_RGBA8Unorm;
            target.writeMask = WGPUColorWriteMask_All;
            fragment.module = fallbackFragment;
            fragment.entryPoint = sv("main");
            fragment.targetCount = 1;
            fragment.targets = &target;
            desc.fragment = &fragment;
        }
        expectValidationErrorOnLimitDevice([&] {
            createRenderPipelineTracked(desc);
        }, shouldError, msg);
    }

    void testCreateRenderPipeline(const WGPURenderPipelineDescriptor& desc, bool, bool shouldError, std::string_view msg = {}) {
        // Native has no async pipeline promise path; async=true uses the same synchronous create.
        expectValidationErrorOnLimitDevice([&] {
            createRenderPipelineTracked(desc);
        }, shouldError, msg);
    }

    void testMaxComputeWorkgroupSize(
        std::string_view limitTest,
        std::string_view testValueName,
        bool async,
        char axis) {
        std::vector<LimitRequestEntry> extraLimits = {
            adapterLimitRequest("maxComputeInvocationsPerWorkgroup"),
        };
        testDeviceWithRequestedMaximumLimits(limitTest, testValueName, [&](const MaximumLimitTestInputs& inputs) {
            WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
            const WGPULimits limits = queryLimits(inputs.device, &compat);
            if (inputs.testValue > limits.maxComputeInvocationsPerWorkgroup) {
                return;
            }
            std::array<uint64_t, 3> size = {1, 1, 1};
            size[static_cast<size_t>(axis - 'X')] = inputs.testValue;
            std::string code;
            WGPUShaderModule module = getModuleForWorkgroupSize(size, &code);
            testCreatePipeline("createComputePipeline", async, module, inputs.shouldError, code);
        }, extraLimits);
    }

    void skipIfNotEnoughStorageBuffersInStage(WGPUShaderStage visibility, uint64_t numRequired) {
        WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
        const WGPULimits limits = queryLimits(device(), &compat);
        if (numRequired > limits.maxStorageBuffersPerShaderStage) {
            skip("maxStorageBuffersPerShaderStage is less than required storage buffers");
        }
        if ((visibility & WGPUShaderStage_Fragment) != 0 &&
            compat.maxStorageBuffersInFragmentStage != WGPU_LIMIT_U32_UNDEFINED &&
            limits.maxStorageBuffersPerShaderStage > compat.maxStorageBuffersInFragmentStage &&
            !(compat.maxStorageBuffersInFragmentStage >= numRequired)) {
            skip("maxStorageBuffersInFragmentStage is less than required storage buffers");
        }
        if ((visibility & WGPUShaderStage_Vertex) != 0 &&
            compat.maxStorageBuffersInVertexStage != WGPU_LIMIT_U32_UNDEFINED &&
            limits.maxStorageBuffersPerShaderStage > compat.maxStorageBuffersInVertexStage &&
            !(compat.maxStorageBuffersInVertexStage >= numRequired)) {
            skip("maxStorageBuffersInVertexStage is less than required storage buffers");
        }
    }

    std::string getPerStageWGSLForBindingCombination() {
        // V10b: binding-combination WGSL generators are not touched by the V10a core-path files.
        return {};
    }

    void testGPUBindingCommandsMixin() {
        // V10b: binding command mixin setup is not touched by the V10a core-path files.
    }

    void testGPURenderAndBindingCommandsMixin() {
        // V10b: render/binding command mixin setup is not touched by the V10a core-path files.
    }

    uint64_t defaultLimit = 0;
    uint64_t adapterLimit = 0;

  private:
    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice currentDevice_ = nullptr;
    mutable WGPUQueue currentQueue_ = nullptr;
    std::vector<WGPUDevice> ownedDevices_;
};

inline std::vector<Value> kCreatePipelineTypeValues() {
    return {std::string("createRenderPipeline"),
            std::string("createRenderPipelineWithFragmentStage"),
            std::string("createComputePipeline")};
}

inline void addMaximumLimitUpToDependentLimit(
    LimitTest& t,
    std::vector<LimitRequestEntry>& limits,
    std::string_view limit,
    std::string_view dependentLimitName,
    std::string_view dependentLimitTest) {
    const uint64_t limitMaximum = t.getAdapterLimit(limit);
    if (isUndefinedLimitValue(limitMaximum)) {
        return;
    }
    const uint64_t dependentMaximum = t.getAdapterLimit(dependentLimitName);
    const uint64_t testValue = getLimitValue(t.getDefaultLimit(dependentLimitName), dependentMaximum, dependentLimitTest);
    limits.push_back(numericLimitRequest(std::string(limit), std::min({testValue, dependentMaximum, limitMaximum})));
}

} // namespace cts::capability_limits
