// Helpers for gpuweb/cts src/webgpu/api/validation/capability_checks/features/*.spec.ts @ b507bd117e53db86f2fb52d0d858d3ae7d684a85
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"
#include "cts/gpu.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

namespace cts::capability_features {

inline WGPUStringView sv(const char* s) {
    return WGPUStringView{s, WGPU_STRLEN};
}

inline WGPUStringView sv(std::string_view s) {
    return WGPUStringView{s.data(), s.size()};
}

inline std::vector<Value> formatValues(std::span<const WGPUTextureFormat> formats) {
    std::vector<Value> values;
    values.reserve(formats.size());
    for (WGPUTextureFormat format : formats) {
        values.emplace_back(std::string(textureFormatIdentifier(format)));
    }
    return values;
}

template <size_t N>
inline std::vector<Value> formatValues(const std::array<WGPUTextureFormat, N>& formats) {
    return formatValues(std::span<const WGPUTextureFormat>(formats.data(), formats.size()));
}

inline bool hasFeature(WGPUAdapter adapter, WGPUFeatureName feature) {
    return wgpuAdapterHasFeature(adapter, feature) != WGPU_FALSE;
}

inline bool hasFeature(WGPUDevice device, WGPUFeatureName feature) {
    return wgpuDeviceHasFeature(device, feature) != WGPU_FALSE;
}

class FeatureGpuTest : public GpuTest {
  public:
    void init() override {
        // UniqueFeaturesOrLimitsGPUTest selects the required device per case.
        // Do not call GpuTest::init(), because it eagerly calls device().
    }

    WGPUDevice device() const override {
        FeatureGpuTest* self = const_cast<FeatureGpuTest*>(this);
        if (self->device_ == nullptr) {
            self->selectDeviceOrSkipTestCase({});
        }
        return self->device_;
    }

    WGPUQueue queue() const override {
        FeatureGpuTest* self = const_cast<FeatureGpuTest*>(this);
        if (self->queue_ == nullptr) {
            self->queue_ = wgpuDeviceGetQueue(self->device());
        }
        return self->queue_;
    }

    WGPUAdapter adapter() {
        ensureInstanceAndAdapter();
        return adapter_;
    }

    void selectDeviceOrSkipTestCase(std::initializer_list<WGPUFeatureName> requiredFeatures) {
        std::vector<WGPUFeatureName> features(requiredFeatures.begin(), requiredFeatures.end());
        selectDeviceOrSkipTestCase(features);
    }

    void selectDeviceOrSkipTestCase(const std::vector<WGPUFeatureName>& requiredFeatures) {
        ensureInstanceAndAdapter();
        const std::vector<WGPUFeatureName> features = normalizeFeatures(requiredFeatures);
        for (WGPUFeatureName feature : features) {
            if (!hasFeature(adapter_, feature)) {
                skip("required feature is not supported by this adapter");
            }
        }
        if (device_ != nullptr && currentFeatures_ == features) {
            return;
        }
        releaseSelectedDevice();

        WGPUDeviceDescriptor desc = WGPU_DEVICE_DESCRIPTOR_INIT;
        desc.requiredFeatureCount = features.size();
        desc.requiredFeatures = features.empty() ? nullptr : features.data();

        DeviceResult result = requestDeviceSync(instance_, adapter_, &desc);
        if (result.status != WGPURequestDeviceStatus_Success || result.device == nullptr) {
            skip("failed to request feature-selected device: " + result.message);
        }
        device_ = result.device;
        currentFeatures_ = features;
    }

    void selectDeviceForTextureFormatOrSkipTestCase(WGPUTextureFormat format) {
        const TextureFormatInfo& info = textureFormatInfo(format);
        if (info.hasRequiredFeature) {
            selectDeviceOrSkipTestCase({info.requiredFeature});
            return;
        }
        selectDeviceOrSkipTestCase({});
    }

    void selectDeviceForTextureFormatsOrSkipTestCase(std::span<const WGPUTextureFormat> formats) {
        std::vector<WGPUFeatureName> features;
        for (WGPUTextureFormat format : formats) {
            const TextureFormatInfo& info = textureFormatInfo(format);
            if (!info.hasRequiredFeature) {
                continue;
            }
            bool seen = false;
            for (WGPUFeatureName feature : features) {
                if (feature == info.requiredFeature) {
                    seen = true;
                    break;
                }
            }
            if (!seen) {
                features.push_back(info.requiredFeature);
            }
        }
        selectDeviceOrSkipTestCase(features);
    }

    void expectValidationErrorOnSelectedDevice(const std::function<void()>& body, bool shouldError) {
        wgpuDevicePushErrorScope(device(), WGPUErrorFilter_Validation);
        body();
        ScopeResult result = popErrorScopeSync(instance_, device());
        if (result.status != WGPUPopErrorScopeStatus_Success) {
            fail("popErrorScope failed: " + result.message);
        }
        const bool hadError = result.type != WGPUErrorType_NoError;
        if (shouldError && !hadError) {
            fail("expected validation error, got none");
        }
        if (!shouldError && hadError) {
            fail("unexpected validation error: " + result.message);
        }
    }

    void finalize() override {
        GpuTest::finalize();
        releaseSelectedDevice();
        if (adapter_ != nullptr) {
            wgpuAdapterRelease(adapter_);
            adapter_ = nullptr;
        }
        if (instance_ != nullptr) {
            wgpuInstanceRelease(instance_);
            instance_ = nullptr;
        }
    }

  private:
    static std::vector<WGPUFeatureName> normalizeFeatures(const std::vector<WGPUFeatureName>& features) {
        std::vector<WGPUFeatureName> normalized = features;
        std::sort(normalized.begin(), normalized.end());
        normalized.erase(std::unique(normalized.begin(), normalized.end()), normalized.end());
        return normalized;
    }

    void releaseSelectedDevice() {
        if (queue_ != nullptr) {
            wgpuQueueRelease(queue_);
            queue_ = nullptr;
        }
        if (device_ != nullptr) {
            wgpuDeviceRelease(device_);
            device_ = nullptr;
        }
        currentFeatures_.clear();
    }

    void ensureInstanceAndAdapter() {
        if (adapter_ != nullptr) {
            return;
        }
        instance_ = createInstance();
        if (instance_ == nullptr) {
            fail("failed to create WGPUInstance");
        }
        AdapterResult adapter = requestAdapterSync(instance_, adapterOptions());
        if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
            fail("failed to request adapter: " + adapter.message);
        }
        adapter_ = adapter.adapter;
    }

    WGPUInstance instance_ = nullptr;
    WGPUAdapter adapter_ = nullptr;
    WGPUDevice device_ = nullptr;
    mutable WGPUQueue queue_ = nullptr;
    std::vector<WGPUFeatureName> currentFeatures_;
};

inline void expectValidationError(FeatureGpuTest& t, const std::function<void()>& body, bool shouldError) {
    t.expectValidationErrorOnSelectedDevice(body, shouldError);
}

inline void doCreateRenderPipelineTest(
    FeatureGpuTest& t,
    bool,
    bool success,
    const WGPURenderPipelineDescriptor& desc) {
    // isAsync=true maps to the same synchronous native create path.
    expectValidationError(t, [&] { t.createRenderPipelineTracked(desc); }, !success);
}

inline void doCreateComputePipelineTest(
    FeatureGpuTest& t,
    bool,
    bool success,
    const WGPUComputePipelineDescriptor& desc) {
    // isAsync=true maps to the same synchronous native create path.
    expectValidationError(t, [&] { t.createComputePipelineTracked(desc); }, !success);
}

} // namespace cts::capability_features
