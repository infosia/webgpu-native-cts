#include "cts/gpu.h"

#include <array>
#include <utility>
#include <vector>

#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"
#include "webgpu/texture_format.h"

namespace cts {
namespace {

Fixture* g_currentTest = nullptr;

std::string toString(WGPUStringView view) {
    if (view.data == nullptr) {
        return {};
    }
    if (view.length == WGPU_STRLEN) {
        return std::string(view.data);
    }
    return std::string(view.data, view.length);
}

struct DeviceCache {
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUDevice deviceAllFeatures = nullptr;
    WGPUQueue queueAllFeatures = nullptr;
    bool allFeaturesDeviceUsedFallback = false;

    ~DeviceCache() {
        if (queueAllFeatures != nullptr) {
            wgpuQueueRelease(queueAllFeatures);
        }
        if (deviceAllFeatures != nullptr) {
            wgpuDeviceRelease(deviceAllFeatures);
        }
        if (queue != nullptr) {
            wgpuQueueRelease(queue);
        }
        if (device != nullptr) {
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

DeviceCache& cache() {
    static DeviceCache cache;
    return cache;
}

void onUncapturedError(WGPUDevice const*, WGPUErrorType, WGPUStringView message, void*, void*) {
    if (g_currentTest != nullptr) {
        g_currentTest->recordUncapturedError("uncaptured error: " + toString(message));
    }
}

void ensureAdapter(DeviceCache& c) {
    if (c.adapter != nullptr) {
        return;
    }

    c.instance = createInstance();
    if (c.instance == nullptr) {
        throw TestFailed("failed to create WebGPU instance");
    }
    AdapterResult adapter = requestAdapterSync(c.instance, nullptr);
    if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
        throw TestFailed("failed to request adapter: " + adapter.message);
    }
    c.adapter = adapter.adapter;
}

WGPUDevice getDevice() {
    DeviceCache& c = cache();
    if (c.device != nullptr) {
        return c.device;
    }

    ensureAdapter(c);

    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    DeviceResult device = requestDeviceSync(c.instance, c.adapter, &descriptor);
    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        throw TestFailed("failed to request device: " + device.message);
    }
    c.device = device.device;
    c.queue = wgpuDeviceGetQueue(c.device);
    return c.device;
}

WGPUDevice getAllFeaturesMaxLimitsDevice() {
    DeviceCache& c = cache();
    if (c.deviceAllFeatures != nullptr) {
        return c.deviceAllFeatures;
    }

    ensureAdapter(c);

    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(c.adapter, &limits) != WGPUStatus_Success) {
        throw TestFailed("failed to get adapter limits");
    }

    WGPUSupportedFeatures supportedFeatures = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(c.adapter, &supportedFeatures);

    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = supportedFeatures.featureCount;
    descriptor.requiredFeatures = supportedFeatures.features;
    descriptor.requiredLimits = &limits;
    descriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    DeviceResult device = requestDeviceSync(c.instance, c.adapter, &descriptor);
    wgpuSupportedFeaturesFreeMembers(supportedFeatures);

    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        static constexpr std::array<WGPUFeatureName, 8> kTextureFeatures = {
            WGPUFeatureName_TextureCompressionBC,
            WGPUFeatureName_TextureCompressionETC2,
            WGPUFeatureName_TextureCompressionASTC,
            WGPUFeatureName_TextureCompressionBCSliced3D,
            WGPUFeatureName_TextureCompressionASTCSliced3D,
            WGPUFeatureName_TextureFormatsTier1,
            WGPUFeatureName_Depth32FloatStencil8,
            WGPUFeatureName_RG11B10UfloatRenderable,
        };
        std::vector<WGPUFeatureName> fallbackFeatures;
        fallbackFeatures.reserve(kTextureFeatures.size());
        for (WGPUFeatureName feature : kTextureFeatures) {
            if (wgpuAdapterHasFeature(c.adapter, feature)) {
                fallbackFeatures.push_back(feature);
            }
        }

        WGPUDeviceDescriptor fallbackDescriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
        fallbackDescriptor.requiredFeatureCount = fallbackFeatures.size();
        fallbackDescriptor.requiredFeatures = fallbackFeatures.data();
        fallbackDescriptor.requiredLimits = &limits;
        fallbackDescriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;
        device = requestDeviceSync(c.instance, c.adapter, &fallbackDescriptor);
        c.allFeaturesDeviceUsedFallback = true;
    }

    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        throw TestFailed("failed to request all-features/max-limits device: " + device.message);
    }
    c.deviceAllFeatures = device.device;
    c.queueAllFeatures = wgpuDeviceGetQueue(c.deviceAllFeatures);
    return c.deviceAllFeatures;
}

} // namespace

SkipTestCase::SkipTestCase(const std::string& message) : std::runtime_error(message) {}
TestFailed::TestFailed(const std::string& message) : std::runtime_error(message) {}

void Fixture::init() {}
void Fixture::finalize() {}

void Fixture::setParams(ParamRecord params) {
    params_ = std::move(params);
}

const ParamRecord& Fixture::params() const {
    return params_;
}

bool Fixture::hasParam(std::string_view key) const {
    return findParam(params_, key) != nullptr;
}

bool Fixture::paramIsUndefined(std::string_view key) const {
    const Value* value = findParam(params_, key);
    return value != nullptr && std::holds_alternative<Value::Undefined>(value->data());
}

bool Fixture::paramIsString(std::string_view key) const {
    const Value* value = findParam(params_, key);
    return value != nullptr && std::holds_alternative<std::string>(value->data());
}

void Fixture::expect(bool condition, const std::string& message) {
    if (!condition) {
        fail(message.empty() ? "expectation failed" : message);
    }
}

void Fixture::fail(const std::string& message) const {
    throw TestFailed(message);
}

void Fixture::skip(const std::string& message) const {
    throw SkipTestCase(message);
}

void Fixture::warn(const std::string& message) {
    warnings_.push_back(message);
}

const std::vector<std::string>& Fixture::warnings() const {
    return warnings_;
}

void Fixture::recordUncapturedError(std::string message) {
    uncapturedError_ = std::move(message);
}

bool Fixture::hasUncapturedError() const {
    return !uncapturedError_.empty();
}

const std::string& Fixture::uncapturedError() const {
    return uncapturedError_;
}

void setCurrentTest(Fixture* fixture) {
    g_currentTest = fixture;
}

Registry& Registry::instance() {
    static Registry registry;
    return registry;
}

SpecFile& Registry::addFile(std::string path, std::string desc) {
    files_.push_back(SpecFile{std::move(path), std::move(desc), {}});
    return files_.back();
}

const std::vector<SpecFile>& Registry::files() const {
    return files_;
}

void GpuTest::init() {
    (void)device();
}

void GpuTest::finalize() {
    for (WGPUCommandBuffer commandBuffer : commandBuffers_) {
        wgpuCommandBufferRelease(commandBuffer);
    }
    commandBuffers_.clear();
    for (WGPUCommandEncoder encoder : encoders_) {
        wgpuCommandEncoderRelease(encoder);
    }
    encoders_.clear();
    for (WGPUSampler sampler : samplers_) {
        wgpuSamplerRelease(sampler);
    }
    samplers_.clear();
    for (WGPUTextureView textureView : textureViews_) {
        wgpuTextureViewRelease(textureView);
    }
    textureViews_.clear();
    for (WGPUTexture texture : textures_) {
        wgpuTextureRelease(texture);
    }
    textures_.clear();
    for (WGPUPipelineLayout pipelineLayout : pipelineLayouts_) {
        wgpuPipelineLayoutRelease(pipelineLayout);
    }
    pipelineLayouts_.clear();
    for (WGPUBindGroupLayout bindGroupLayout : bindGroupLayouts_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout);
    }
    bindGroupLayouts_.clear();
    for (WGPUBuffer buffer : buffers_) {
        wgpuBufferRelease(buffer);
    }
    buffers_.clear();
}

WGPUDevice GpuTest::device() const {
    return getDevice();
}

WGPUQueue GpuTest::queue() const {
    (void)getDevice();
    return cache().queue;
}

WGPULimits GpuTest::getLimits() const {
    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuDeviceGetLimits(device(), &limits) != WGPUStatus_Success) {
        fail("failed to get device limits");
    }
    return limits;
}

WGPUBuffer GpuTest::createBufferTracked(const WGPUBufferDescriptor& desc) {
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device(), &desc);
    if (buffer != nullptr) {
        buffers_.push_back(buffer);
    }
    return buffer;
}

WGPUBuffer GpuTest::createBufferWithState(ResourceState state, const WGPUBufferDescriptor& desc) {
    if (state == ResourceState::Valid) {
        return createBufferTracked(desc);
    }

    if (state == ResourceState::Invalid) {
        WGPUBufferDescriptor invalidDesc = desc;
        invalidDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_MapWrite;

        wgpuDevicePushErrorScope(device(), WGPUErrorFilter_Validation);
        WGPUBuffer buffer = createBufferTracked(invalidDesc);
        ScopeResult result = popErrorScopeSync(cache().instance, device());
        if (result.status != WGPUPopErrorScopeStatus_Success) {
            fail("popErrorScope failed: " + result.message);
        }
        return buffer;
    }

    WGPUBuffer buffer = createBufferTracked(desc);
    wgpuBufferDestroy(buffer);
    return buffer;
}

WGPUBuffer GpuTest::getErrorBuffer() {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = 4;
    desc.usage = WGPUBufferUsage_Vertex;
    return createBufferWithState(ResourceState::Invalid, desc);
}

WGPUSampler GpuTest::createSamplerTracked(const WGPUSamplerDescriptor& desc) {
    WGPUSampler sampler = wgpuDeviceCreateSampler(device(), &desc);
    if (sampler != nullptr) {
        samplers_.push_back(sampler);
    }
    return sampler;
}

WGPUTexture GpuTest::createTextureTracked(const WGPUTextureDescriptor& desc) {
    WGPUTexture texture = wgpuDeviceCreateTexture(device(), &desc);
    if (texture != nullptr) {
        textures_.push_back(texture);
    }
    return texture;
}

WGPUTexture GpuTest::createTextureWithState(ResourceState state, const WGPUTextureDescriptor& desc) {
    if (state == ResourceState::Valid) {
        return createTextureTracked(desc);
    }

    if (state == ResourceState::Invalid) {
        WGPUTextureDescriptor invalidDesc = desc;
        invalidDesc.usage = WGPUTextureUsage_None;

        wgpuDevicePushErrorScope(device(), WGPUErrorFilter_Validation);
        WGPUTexture texture = createTextureTracked(invalidDesc);
        ScopeResult result = popErrorScopeSync(cache().instance, device());
        if (result.status != WGPUPopErrorScopeStatus_Success) {
            fail("popErrorScope failed: " + result.message);
        }
        return texture;
    }

    WGPUTexture texture = createTextureTracked(desc);
    wgpuTextureDestroy(texture);
    return texture;
}

WGPUTextureView GpuTest::createViewTracked(WGPUTexture texture, const WGPUTextureViewDescriptor& desc) {
    WGPUTextureView textureView = wgpuTextureCreateView(texture, &desc);
    if (textureView != nullptr) {
        textureViews_.push_back(textureView);
    }
    return textureView;
}

WGPUBindGroupLayout GpuTest::createBindGroupLayoutTracked(const WGPUBindGroupLayoutDescriptor& desc) {
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device(), &desc);
    if (layout != nullptr) {
        bindGroupLayouts_.push_back(layout);
    }
    return layout;
}

WGPUPipelineLayout GpuTest::createPipelineLayoutTracked(const WGPUPipelineLayoutDescriptor& desc) {
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device(), &desc);
    if (layout != nullptr) {
        pipelineLayouts_.push_back(layout);
    }
    return layout;
}

WGPUCommandEncoder GpuTest::createCommandEncoderTracked() {
    WGPUCommandEncoderDescriptor desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device(), &desc);
    if (encoder != nullptr) {
        encoders_.push_back(encoder);
    }
    return encoder;
}

WGPUCommandBuffer GpuTest::finishTracked(WGPUCommandEncoder encoder) {
    WGPUCommandBufferDescriptor desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    WGPUCommandBuffer commandBuffer = wgpuCommandEncoderFinish(encoder, &desc);
    if (commandBuffer != nullptr) {
        commandBuffers_.push_back(commandBuffer);
    }
    return commandBuffer;
}

void GpuTest::expectValidationError(const std::function<void()>& body, bool shouldError) {
    wgpuDevicePushErrorScope(device(), WGPUErrorFilter_Validation);
    body();
    ScopeResult result = popErrorScopeSync(cache().instance, device());
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

void GpuTest::expectMapAsync(WGPUBuffer buffer, WGPUMapMode mode, bool expectSuccess, size_t offset, size_t size) {
    if (expectSuccess) {
        const WGPUMapAsyncStatus status = bufferMapSync(cache().instance, buffer, mode, offset, size);
        if (status != WGPUMapAsyncStatus_Success) {
            fail("expected mapAsync success");
        }
        return;
    }

    WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Success;
    wgpuDevicePushErrorScope(device(), WGPUErrorFilter_Validation);
    status = bufferMapSync(cache().instance, buffer, mode, offset, size);
    ScopeResult result = popErrorScopeSync(cache().instance, device());
    if (result.status != WGPUPopErrorScopeStatus_Success) {
        fail("popErrorScope failed: " + result.message);
    }
    if (status == WGPUMapAsyncStatus_Success) {
        fail("expected mapAsync failure");
    }
}

void GpuTest::skipIfTransientAttachmentNotSupported() {
    // TRANSIENT_ATTACHMENT is a non-standard native extension, outside the conformance scope.
    // Upstream skips these cases in standard environments; do the same here.
    skip("TRANSIENT_ATTACHMENT is not supported");
}

void GpuTest::skipIfTextureFormatNotSupported(WGPUTextureFormat format) {
    const TextureFormatInfo& info = textureFormatInfo(format);
    if (info.hasRequiredFeature && !wgpuDeviceHasFeature(device(), info.requiredFeature)) {
        skip("texture format requires an unsupported feature");
    }
}

void GpuTest::skipIfTextureFormatAndDimensionNotCompatible(WGPUTextureFormat format, WGPUTextureDimension dimension) {
    if (!textureDimensionAndFormatCompatibleForDevice(dimension, format)) {
        skip("format does not support dimension");
    }
}

void GpuTest::skipIfTextureViewDimensionNotSupported(WGPUTextureViewDimension) {
    // Compatibility mode may restrict cube-array views upstream; this port never runs compatibility mode.
}

void GpuTest::skipIfTextureFormatNotUsableAsRenderAttachment(WGPUTextureFormat format) {
    if (!isTextureFormatUsableAsRenderAttachment(format)) {
        skip("texture format is not usable as render attachment");
    }
}

void GpuTest::skipIfTextureFormatDoesNotSupportUsage(WGPUTextureUsage usage, WGPUTextureFormat format) {
    if (usage & WGPUTextureUsage_RenderAttachment) {
        skipIfTextureFormatNotUsableAsRenderAttachment(format);
    }
    if ((usage & WGPUTextureUsage_StorageBinding)
        && !isTextureFormatUsableAsWriteOnlyStorageTexture(format)) {
        skip("texture format is not usable as write-only storage texture");
    }
}

bool GpuTest::textureDimensionAndFormatCompatibleForDevice(WGPUTextureDimension dimension, WGPUTextureFormat format) {
    if (dimension == WGPUTextureDimension_3D
        && ((isBCTextureFormat(format) && wgpuDeviceHasFeature(device(), WGPUFeatureName_TextureCompressionBCSliced3D))
            || (isASTCTextureFormat(format)
                && wgpuDeviceHasFeature(device(), WGPUFeatureName_TextureCompressionASTCSliced3D)))) {
        return true;
    }
    if (dimension == WGPUTextureDimension_Undefined || dimension == WGPUTextureDimension_2D) {
        return true;
    }
    const TextureFormatInfo& info = textureFormatInfo(format);
    return !(info.blockWidth > 1 || info.hasDepth || info.hasStencil);
}

bool GpuTest::isTextureFormatColorRenderable(WGPUTextureFormat format) {
    if (format == WGPUTextureFormat_RG11B10Ufloat) {
        return wgpuDeviceHasFeature(device(), WGPUFeatureName_RG11B10UfloatRenderable);
    }
    if (isTier1BlendableMultisampleTextureFormat(format)) {
        return wgpuDeviceHasFeature(device(), WGPUFeatureName_TextureFormatsTier1);
    }
    return textureFormatInList(format, kColorRenderableTextureFormats);
}

bool GpuTest::isTextureFormatUsableAsRenderAttachment(WGPUTextureFormat format) {
    if (format == WGPUTextureFormat_RG11B10Ufloat) {
        return wgpuDeviceHasFeature(device(), WGPUFeatureName_RG11B10UfloatRenderable);
    }
    if (isTier1BlendableMultisampleTextureFormat(format)) {
        return wgpuDeviceHasFeature(device(), WGPUFeatureName_TextureFormatsTier1);
    }
    return textureFormatInList(format, kColorRenderableTextureFormats)
        || isDepthOrStencilTextureFormat(format);
}

bool GpuTest::isTextureFormatUsableAsWriteOnlyStorageTexture(WGPUTextureFormat format) {
    if (format == WGPUTextureFormat_BGRA8Unorm
        && wgpuDeviceHasFeature(device(), WGPUFeatureName_BGRA8UnormStorage)) {
        return true;
    }
    if (textureFormatInList(format, kTextureFormatsTier1EnablesStorageReadOnlyWriteOnly)
        && wgpuDeviceHasFeature(device(), WGPUFeatureName_TextureFormatsTier1)) {
        return true;
    }
    return textureFormatInList(format, kStorageTextureFormats);
}

bool GpuTest::isTextureFormatMultisampled(WGPUTextureFormat format) {
    if (format == WGPUTextureFormat_RG11B10Ufloat) {
        return wgpuDeviceHasFeature(device(), WGPUFeatureName_RG11B10UfloatRenderable);
    }
    if (isTier1BlendableMultisampleTextureFormat(format)) {
        return wgpuDeviceHasFeature(device(), WGPUFeatureName_TextureFormatsTier1);
    }
    return textureFormatInfo(format).multisample;
}

void AllFeaturesMaxLimitsGpuTest::init() {
    (void)device();
}

WGPUDevice AllFeaturesMaxLimitsGpuTest::device() const {
    return getAllFeaturesMaxLimitsDevice();
}

WGPUQueue AllFeaturesMaxLimitsGpuTest::queue() const {
    (void)getAllFeaturesMaxLimitsDevice();
    return cache().queueAllFeatures;
}

} // namespace cts
