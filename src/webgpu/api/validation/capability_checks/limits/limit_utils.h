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
#include "webgpu/capability_info.h"
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
        AdapterResult adapterResult = requestAdapterSync(instance_, adapterOptions());
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

    struct BindingCommandContext {
        WGPUComputePassEncoder computePass = nullptr;
        WGPURenderPassEncoder renderPass = nullptr;
        WGPURenderBundleEncoder renderBundle = nullptr;
        WGPUBindGroup bindGroup = nullptr;

        void setBindGroup(uint32_t index, WGPUBindGroup group) const {
            if (computePass != nullptr) {
                wgpuComputePassEncoderSetBindGroup(computePass, index, group, 0, nullptr);
            } else if (renderPass != nullptr) {
                wgpuRenderPassEncoderSetBindGroup(renderPass, index, group, 0, nullptr);
            } else {
                wgpuRenderBundleEncoderSetBindGroup(renderBundle, index, group, 0, nullptr);
            }
        }

        void setVertexBuffer(uint32_t slot, WGPUBuffer buffer) const {
            if (renderPass != nullptr) {
                wgpuRenderPassEncoderSetVertexBuffer(renderPass, slot, buffer, 0, WGPU_WHOLE_SIZE);
            } else {
                wgpuRenderBundleEncoderSetVertexBuffer(renderBundle, slot, buffer, 0, WGPU_WHOLE_SIZE);
            }
        }

        void setPipeline(WGPURenderPipeline pipeline) const {
            if (renderPass != nullptr) {
                wgpuRenderPassEncoderSetPipeline(renderPass, pipeline);
            } else {
                wgpuRenderBundleEncoderSetPipeline(renderBundle, pipeline);
            }
        }

        void setIndexBuffer(WGPUBuffer buffer) const {
            if (renderPass != nullptr) {
                wgpuRenderPassEncoderSetIndexBuffer(renderPass, buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
            } else {
                wgpuRenderBundleEncoderSetIndexBuffer(renderBundle, buffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
            }
        }

        void draw(std::string_view drawType, WGPUBuffer indirectBuffer) const {
            if (renderPass != nullptr) {
                if (drawType == "draw") wgpuRenderPassEncoderDraw(renderPass, 0, 1, 0, 0);
                else if (drawType == "drawIndexed") wgpuRenderPassEncoderDrawIndexed(renderPass, 0, 1, 0, 0, 0);
                else if (drawType == "drawIndirect") wgpuRenderPassEncoderDrawIndirect(renderPass, indirectBuffer, 0);
                else if (drawType == "drawIndexedIndirect") wgpuRenderPassEncoderDrawIndexedIndirect(renderPass, indirectBuffer, 0);
                else std::abort();
            } else {
                if (drawType == "draw") wgpuRenderBundleEncoderDraw(renderBundle, 0, 1, 0, 0);
                else if (drawType == "drawIndexed") wgpuRenderBundleEncoderDrawIndexed(renderBundle, 0, 1, 0, 0, 0);
                else if (drawType == "drawIndirect") wgpuRenderBundleEncoderDrawIndirect(renderBundle, indirectBuffer, 0);
                else if (drawType == "drawIndexedIndirect") wgpuRenderBundleEncoderDrawIndexedIndirect(renderBundle, indirectBuffer, 0);
                else std::abort();
            }
        }
    };

    void testGPURenderAndBindingCommandsMixin(
        std::string_view encoderType,
        const std::function<void(const BindingCommandContext&)>& fn,
        bool shouldError,
        std::string_view msg = {}) {
        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = 16;
        bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_Vertex;
        WGPUBuffer buffer = createBufferTracked(bufferDesc);

        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Vertex;
        layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
        WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &layoutEntry;
        WGPUBindGroupLayout layout = createBindGroupLayoutTracked(layoutDesc);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = buffer;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = layout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        WGPUBindGroup bindGroup = createBindGroupTracked(bgDesc);

        if (encoderType == "render") {
            WGPUTextureDescriptor textureDesc = WGPU_TEXTURE_DESCRIPTOR_INIT;
            textureDesc.size = {1, 1, 1};
            textureDesc.format = WGPUTextureFormat_RGBA8Unorm;
            textureDesc.usage = WGPUTextureUsage_RenderAttachment;
            WGPUTexture texture = createTextureTracked(textureDesc);
            WGPUTextureViewDescriptor viewDesc = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
            WGPUTextureView view = createViewTracked(texture, viewDesc);
            WGPUCommandEncoder encoder = createCommandEncoderTracked();
            WGPURenderPassColorAttachment attachment = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
            attachment.view = view;
            attachment.loadOp = WGPULoadOp_Clear;
            attachment.storeOp = WGPUStoreOp_Store;
            WGPURenderPassDescriptor passDesc = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments = &attachment;
            WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
            BindingCommandContext ctx;
            ctx.renderPass = pass;
            ctx.bindGroup = bindGroup;
            fn(ctx);
            wgpuRenderPassEncoderEnd(pass);
            wgpuRenderPassEncoderRelease(pass);
            expectValidationErrorOnLimitDevice([&] { finishTracked(encoder); }, shouldError, msg);
            return;
        }

        if (encoderType == "renderBundle") {
            WGPUTextureFormat format = WGPUTextureFormat_RGBA8Unorm;
            WGPURenderBundleEncoderDescriptor desc = WGPU_RENDER_BUNDLE_ENCODER_DESCRIPTOR_INIT;
            desc.colorFormatCount = 1;
            desc.colorFormats = &format;
            WGPURenderBundleEncoder encoder = wgpuDeviceCreateRenderBundleEncoder(device(), &desc);
            BindingCommandContext ctx;
            ctx.renderBundle = encoder;
            ctx.bindGroup = bindGroup;
            fn(ctx);
            expectValidationErrorOnLimitDevice([&] {
                WGPURenderBundle bundle = wgpuRenderBundleEncoderFinish(encoder, nullptr);
                if (bundle != nullptr) wgpuRenderBundleRelease(bundle);
            }, shouldError, msg);
            wgpuRenderBundleEncoderRelease(encoder);
            return;
        }

        std::abort();
    }

    void testGPUBindingCommandsMixin(
        std::string_view encoderType,
        const std::function<void(const BindingCommandContext&)>& fn,
        bool shouldError,
        std::string_view msg = {}) {
        if (encoderType == "render" || encoderType == "renderBundle") {
            testGPURenderAndBindingCommandsMixin(encoderType, fn, shouldError, msg);
            return;
        }
        if (encoderType != "compute") {
            std::abort();
        }
        WGPUBufferDescriptor bufferDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
        bufferDesc.size = 16;
        bufferDesc.usage = WGPUBufferUsage_Uniform;
        WGPUBuffer buffer = createBufferTracked(bufferDesc);
        WGPUBindGroupLayoutEntry layoutEntry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        layoutEntry.binding = 0;
        layoutEntry.visibility = WGPUShaderStage_Compute;
        layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
        WGPUBindGroupLayoutDescriptor layoutDesc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
        layoutDesc.entryCount = 1;
        layoutDesc.entries = &layoutEntry;
        WGPUBindGroupLayout layout = createBindGroupLayoutTracked(layoutDesc);
        WGPUBindGroupEntry entry = WGPU_BIND_GROUP_ENTRY_INIT;
        entry.binding = 0;
        entry.buffer = buffer;
        WGPUBindGroupDescriptor bgDesc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
        bgDesc.layout = layout;
        bgDesc.entryCount = 1;
        bgDesc.entries = &entry;
        WGPUBindGroup bindGroup = createBindGroupTracked(bgDesc);
        WGPUCommandEncoder encoder = createCommandEncoderTracked();
        WGPUComputePassDescriptor passDesc = WGPU_COMPUTE_PASS_DESCRIPTOR_INIT;
        WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);
        BindingCommandContext ctx;
        ctx.computePass = pass;
        ctx.bindGroup = bindGroup;
        fn(ctx);
        wgpuComputePassEncoderEnd(pass);
        wgpuComputePassEncoderRelease(pass);
        expectValidationErrorOnLimitDevice([&] { finishTracked(encoder); }, shouldError, msg);
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

enum class ReorderOrder {
    Forward,
    Backward,
    ShiftByHalf,
};

inline std::vector<Value> kReorderOrderValues() {
    return {std::string("forward"), std::string("backward"), std::string("shiftByHalf")};
}

inline ReorderOrder parseReorderOrder(std::string_view order) {
    if (order == "forward") return ReorderOrder::Forward;
    if (order == "backward") return ReorderOrder::Backward;
    if (order == "shiftByHalf") return ReorderOrder::ShiftByHalf;
    std::abort();
}

template <typename T>
std::vector<T> reorder(ReorderOrder order, std::vector<T> values) {
    if (order == ReorderOrder::Forward) {
        return values;
    }
    if (order == ReorderOrder::Backward) {
        std::reverse(values.begin(), values.end());
        return values;
    }
    const size_t half = values.size() / 2;
    std::rotate(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(half), values.end());
    return values;
}

inline std::vector<Value> shaderStageCombinationWithStageValues() {
    std::vector<Value> values;
    for (WGPUShaderStage stage : kShaderStageCombinationsWithStage) {
        values.emplace_back(static_cast<uint64_t>(stage));
    }
    return values;
}

inline std::vector<Value> kBindGroupTestValues() {
    return {std::string("sameGroup"), std::string("differentGroups")};
}

inline std::vector<Value> kBindingCombinationValues() {
    return {std::string("vertex"),
            std::string("fragment"),
            std::string("vertexAndFragmentWithPossibleVertexStageOverflow"),
            std::string("vertexAndFragmentWithPossibleFragmentStageOverflow"),
            std::string("compute")};
}

inline std::string getPipelineTypeForBindingCombination(std::string_view bindingCombination) {
    if (bindingCombination == "vertex") return "createRenderPipeline";
    if (bindingCombination == "fragment" ||
        bindingCombination == "vertexAndFragmentWithPossibleVertexStageOverflow" ||
        bindingCombination == "vertexAndFragmentWithPossibleFragmentStageOverflow") {
        return "createRenderPipelineWithFragmentStage";
    }
    if (bindingCombination == "compute") return "createComputePipeline";
    std::abort();
}

inline WGPUShaderStage getStageVisibilityForBindingCombination(std::string_view bindingCombination) {
    if (bindingCombination == "vertex") return WGPUShaderStage_Vertex;
    if (bindingCombination == "fragment") return WGPUShaderStage_Fragment;
    if (bindingCombination == "vertexAndFragmentWithPossibleVertexStageOverflow" ||
        bindingCombination == "vertexAndFragmentWithPossibleFragmentStageOverflow") {
        return WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    }
    if (bindingCombination == "compute") return WGPUShaderStage_Compute;
    std::abort();
}

inline uint64_t getBindGroupIndex(std::string_view bindGroupTest, uint64_t numBindGroups, uint64_t i) {
    if (bindGroupTest == "sameGroup") return 0;
    if (bindGroupTest == "differentGroups") return i % numBindGroups;
    std::abort();
}

inline uint64_t getBindingIndex(std::string_view bindGroupTest, uint64_t numBindGroups, uint64_t i) {
    if (bindGroupTest == "sameGroup") return i;
    if (bindGroupTest == "differentGroups") return i / numBindGroups;
    std::abort();
}

inline std::string getWGSLBindings(
    ReorderOrder order,
    std::string_view bindGroupTest,
    const std::function<std::string(int, int)>& storageDefinitionWGSLSnippetFn,
    uint64_t numBindGroups,
    uint64_t numBindings,
    int id) {
    std::vector<std::string> lines;
    for (uint64_t i = 0; i < numBindings; ++i) {
        std::ostringstream line;
        line << "@group(" << getBindGroupIndex(bindGroupTest, numBindGroups, i) << ") @binding("
             << getBindingIndex(bindGroupTest, numBindGroups, i) << ") "
             << storageDefinitionWGSLSnippetFn(static_cast<int>(i), id) << ";";
        lines.push_back(line.str());
    }
    lines = reorder(order, std::move(lines));
    std::ostringstream code;
    for (const std::string& line : lines) {
        code << line << "\n";
    }
    return code.str();
}

inline std::string getPerStageWGSLForBindingCombinationImpl(
    std::string_view bindingCombination,
    ReorderOrder order,
    std::string_view bindGroupTest,
    const std::function<std::string(int, int)>& storageDefinitionWGSLSnippetFn,
    const std::function<std::string(uint64_t, int)>& bodyFn,
    uint64_t numBindGroups,
    uint64_t numBindings,
    std::string_view extraWGSL = {}) {
    std::ostringstream code;
    code << extraWGSL << "\n";
    if (bindingCombination == "vertex") {
        code << getWGSLBindings(order, bindGroupTest, storageDefinitionWGSLSnippetFn, numBindGroups, numBindings, 0)
             << "@vertex fn mainVS() -> @builtin(position) vec4f {\n"
             << bodyFn(numBindings, 0)
             << "  return vec4f(0);\n"
             << "}\n";
        return code.str();
    }
    if (bindingCombination == "fragment") {
        code << getWGSLBindings(order, bindGroupTest, storageDefinitionWGSLSnippetFn, numBindGroups, numBindings, 0)
             << "@vertex fn mainVS() -> @builtin(position) vec4f { return vec4f(0); }\n"
             << "@fragment fn mainFS() {\n"
             << bodyFn(numBindings, 0)
             << "}\n";
        return code.str();
    }
    if (bindingCombination == "vertexAndFragmentWithPossibleVertexStageOverflow") {
        code << getWGSLBindings(order, bindGroupTest, storageDefinitionWGSLSnippetFn, numBindGroups, numBindings, 0)
             << getWGSLBindings(order, bindGroupTest, storageDefinitionWGSLSnippetFn, numBindGroups, numBindings - 1, 1)
             << "@vertex fn mainVS() -> @builtin(position) vec4f {\n"
             << bodyFn(numBindings, 0)
             << "  return vec4f(0);\n"
             << "}\n"
             << "@fragment fn mainFS() {\n"
             << bodyFn(numBindings - 1, 1)
             << "}\n";
        return code.str();
    }
    if (bindingCombination == "vertexAndFragmentWithPossibleFragmentStageOverflow") {
        code << getWGSLBindings(order, bindGroupTest, storageDefinitionWGSLSnippetFn, numBindGroups, numBindings - 1, 0)
             << getWGSLBindings(order, bindGroupTest, storageDefinitionWGSLSnippetFn, numBindGroups, numBindings, 1)
             << "@vertex fn mainVS() -> @builtin(position) vec4f {\n"
             << bodyFn(numBindings - 1, 0)
             << "  return vec4f(0);\n"
             << "}\n"
             << "@fragment fn mainFS() {\n"
             << bodyFn(numBindings, 1)
             << "}\n";
        return code.str();
    }
    if (bindingCombination == "compute") {
        code << getWGSLBindings(order, bindGroupTest, storageDefinitionWGSLSnippetFn, numBindGroups, numBindings, 0)
             << "@compute @workgroup_size(1) fn main() {\n"
             << bodyFn(numBindings, 0)
             << "}\n";
        return code.str();
    }
    std::abort();
}

inline std::string getPerStageWGSLForBindingCombination(
    std::string_view bindingCombination,
    ReorderOrder order,
    std::string_view bindGroupTest,
    const std::function<std::string(int, int)>& storageDefinitionWGSLSnippetFn,
    const std::function<std::string(int, int)>& usageWGSLSnippetFn,
    uint64_t maxBindGroups,
    uint64_t numBindings,
    std::string_view extraWGSL = {}) {
    return getPerStageWGSLForBindingCombinationImpl(
        bindingCombination, order, bindGroupTest, storageDefinitionWGSLSnippetFn,
        [&](uint64_t count, int set) {
            std::ostringstream body;
            for (uint64_t i = 0; i < count; ++i) {
                body << "  " << usageWGSLSnippetFn(static_cast<int>(i), set) << "\n";
            }
            return body.str();
        },
        maxBindGroups, numBindings, extraWGSL);
}

inline std::string getPerStageWGSLForBindingCombinationStorageTextures(
    std::string_view bindingCombination,
    ReorderOrder order,
    std::string_view bindGroupTest,
    const std::function<std::string(int, int)>& storageDefinitionWGSLSnippetFn,
    const std::function<std::string(int, int)>& usageWGSLSnippetFn,
    uint64_t numBindGroups,
    uint64_t numBindings,
    std::string_view extraWGSL = {}) {
    return getPerStageWGSLForBindingCombination(
        bindingCombination, order, bindGroupTest, storageDefinitionWGSLSnippetFn, usageWGSLSnippetFn,
        numBindGroups, numBindings, extraWGSL);
}

enum class PerStageResourceKind {
    SampledTexture,
    Sampler,
    UniformBuffer,
    StorageBuffer,
    ReadOnlyStorageBuffer,
    StorageTextureReadOnly,
    StorageTextureReadWrite,
    StorageTextureWriteOnly,
};

inline PerStageResourceKind parseStorageBufferKind(std::string_view type) {
    if (type == "storage") return PerStageResourceKind::StorageBuffer;
    if (type == "read-only-storage") return PerStageResourceKind::ReadOnlyStorageBuffer;
    std::abort();
}

inline PerStageResourceKind parseStorageTextureKind(std::string_view access) {
    if (access == "read-only") return PerStageResourceKind::StorageTextureReadOnly;
    if (access == "read-write") return PerStageResourceKind::StorageTextureReadWrite;
    if (access == "write-only") return PerStageResourceKind::StorageTextureWriteOnly;
    std::abort();
}

inline std::vector<Value> storageBufferTypeValues() {
    return {std::string("storage"), std::string("read-only-storage")};
}

inline std::vector<Value> storageTextureAccessValues() {
    return {std::string("read-only"), std::string("read-write"), std::string("write-only")};
}

inline bool storageTextureAccessAllowedInVisibility(WGPUShaderStage visibility, std::string_view access) {
    return access == "read-only" || (visibility & WGPUShaderStage_Vertex) == 0;
}

inline WGPUStorageTextureAccess storageTextureAccess(std::string_view access) {
    if (access == "read-only") return WGPUStorageTextureAccess_ReadOnly;
    if (access == "read-write") return WGPUStorageTextureAccess_ReadWrite;
    if (access == "write-only") return WGPUStorageTextureAccess_WriteOnly;
    std::abort();
}

inline const char* storageTextureWGSLAccess(std::string_view access) {
    if (access == "read-only") return "read";
    if (access == "read-write") return "read_write";
    if (access == "write-only") return "write";
    std::abort();
}

inline WGPUBindGroupLayout createPerStageBindGroupLayout(
    LimitTest& t,
    WGPUShaderStage visibility,
    PerStageResourceKind kind,
    ReorderOrder order,
    uint64_t numBindings) {
    std::vector<WGPUBindGroupLayoutEntry> entries;
    for (uint64_t i = 0; i < numBindings; ++i) {
        WGPUBindGroupLayoutEntry entry = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
        entry.binding = static_cast<uint32_t>(i);
        entry.visibility = visibility;
        switch (kind) {
            case PerStageResourceKind::SampledTexture:
                entry.texture.sampleType = WGPUTextureSampleType_Float;
                entry.texture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case PerStageResourceKind::Sampler:
                entry.sampler.type = WGPUSamplerBindingType_Filtering;
                break;
            case PerStageResourceKind::UniformBuffer:
                entry.buffer.type = WGPUBufferBindingType_Uniform;
                break;
            case PerStageResourceKind::StorageBuffer:
                entry.buffer.type = WGPUBufferBindingType_Storage;
                break;
            case PerStageResourceKind::ReadOnlyStorageBuffer:
                entry.buffer.type = WGPUBufferBindingType_ReadOnlyStorage;
                break;
            case PerStageResourceKind::StorageTextureReadOnly:
                entry.storageTexture.access = WGPUStorageTextureAccess_ReadOnly;
                entry.storageTexture.format = WGPUTextureFormat_R32Float;
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case PerStageResourceKind::StorageTextureReadWrite:
                entry.storageTexture.access = WGPUStorageTextureAccess_ReadWrite;
                entry.storageTexture.format = WGPUTextureFormat_R32Float;
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                break;
            case PerStageResourceKind::StorageTextureWriteOnly:
                entry.storageTexture.access = WGPUStorageTextureAccess_WriteOnly;
                entry.storageTexture.format = WGPUTextureFormat_R32Float;
                entry.storageTexture.viewDimension = WGPUTextureViewDimension_2D;
                break;
        }
        entries.push_back(entry);
    }
    entries = reorder(order, std::move(entries));
    WGPUBindGroupLayoutDescriptor desc = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
    desc.entryCount = entries.size();
    desc.entries = entries.data();
    return t.createBindGroupLayoutTracked(desc);
}

inline void testPerStageCreatePipelineLayout(
    LimitTest& t,
    WGPUShaderStage visibility,
    PerStageResourceKind kind,
    ReorderOrder order,
    const SpecificLimitTestInputs& inputs) {
    if (inputs.actualLimit == 0) {
        t.skip("can not make a bindGroupLayout to test createPipelineLayout if the actual limit is 0");
    }
    WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
    const WGPULimits limits = queryLimits(inputs.device, &compat);
    const uint64_t maxBindingsPerBindGroup = std::min<uint64_t>(limits.maxBindingsPerBindGroup, inputs.actualLimit);
    const uint64_t numGroups = (inputs.testValue + maxBindingsPerBindGroup - 1) / maxBindingsPerBindGroup;
    t.expect(numGroups <= limits.maxBindGroups);
    std::vector<WGPUBindGroupLayout> layouts;
    for (uint64_t i = 0; i < numGroups; ++i) {
        const uint64_t remaining = inputs.testValue - i * maxBindingsPerBindGroup;
        const uint64_t inGroup = std::min(remaining, maxBindingsPerBindGroup);
        layouts.push_back(createPerStageBindGroupLayout(t, visibility, kind, order, inGroup));
    }
    WGPUPipelineLayoutDescriptor desc = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
    desc.bindGroupLayoutCount = layouts.size();
    desc.bindGroupLayouts = layouts.data();
    t.expectValidationErrorOnLimitDevice([&] { t.createPipelineLayoutTracked(desc); }, inputs.shouldError);
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

template <class F>
inline void testMaxStorageXXXInYYYStageDeviceCreationWithDependentLimit(
    TestGroup<F>& testGroup,
    std::string limit,
    std::string dependentLimitName) {
    makeTestBuilder(testGroup, "auto_upgrades_per_stage," + dependentLimitName)
        .desc("Test that required in-stage storage limit auto-upgrades the dependent per-stage limit.")
        .fn([limit, dependentLimitName](F& t) {
            const uint64_t maximumLimit = t.adapterLimit;
            const uint64_t dependentAdapterLimit = t.getAdapterLimit(dependentLimitName);
            t.expect(maximumLimit <= dependentAdapterLimit);
            std::optional<DeviceAndLimits> deviceAndLimits =
                t.requestDeviceWithLimits({numericLimitRequest(limit, maximumLimit)}, false);
            if (!deviceAndLimits.has_value()) {
                return;
            }
            WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
            const WGPULimits limits = queryLimits(deviceAndLimits->device, &compat);
            const uint64_t actualLimit = getLimitValueByName(limits, compat, limit);
            const uint64_t dependentLimit = getLimitValueByName(limits, compat, dependentLimitName);
            t.expect(dependentLimit >= actualLimit);
            wgpuDeviceDestroy(deviceAndLimits->device);
        });

    makeTestBuilder(testGroup, "auto_upgraded_from_per_stage," + dependentLimitName)
        .desc("Test that in-stage storage limit is automatically upgraded from the dependent per-stage limit.")
        .fn([limit, dependentLimitName](F& t) {
            const uint64_t dependentAdapterLimit = t.getAdapterLimit(dependentLimitName);
            std::optional<DeviceAndLimits> deviceAndLimits =
                t.requestDeviceWithLimits({numericLimitRequest(dependentLimitName, dependentAdapterLimit)}, false);
            if (!deviceAndLimits.has_value()) {
                return;
            }
            WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
            const WGPULimits limits = queryLimits(deviceAndLimits->device, &compat);
            const uint64_t actualLimit = getLimitValueByName(limits, compat, limit);
            // Native fixtures are non-compatibility mode here; compat would expect defaultLimit.
            t.expect(actualLimit == dependentAdapterLimit);
            wgpuDeviceDestroy(deviceAndLimits->device);
        });
}

} // namespace cts::capability_limits
