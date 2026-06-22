#include "cts/gpu.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <utility>
#include <vector>

#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"
#include "webgpu/util/texel_view.h"
#include "webgpu/util/texture_ok.h"
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
    WGPUAdapter adapterAllFeatures = nullptr;
    WGPUDevice deviceAllFeatures = nullptr;
    WGPUQueue queueAllFeatures = nullptr;
    bool allFeaturesDeviceUsedFallback = false;
    bool deviceLost = false;

    ~DeviceCache() {
        if (queueAllFeatures != nullptr) {
            wgpuQueueRelease(queueAllFeatures);
        }
        if (deviceAllFeatures != nullptr) {
            wgpuDeviceRelease(deviceAllFeatures);
        }
        if (adapterAllFeatures != nullptr) {
            wgpuAdapterRelease(adapterAllFeatures);
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

// phaseH3-A: detect genuine device loss so the cache can self-heal. Destroyed /
// CallbackCancelled fire during our own teardown and must NOT trigger a recycle.
void onDeviceLost(WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView, void*, void*) {
    if (reason == WGPUDeviceLostReason_Destroyed
        || reason == WGPUDeviceLostReason_CallbackCancelled) {
        return;
    }
    cache().deviceLost = true;
}

struct QueueWorkDoneState {
    bool completed = false;
    WGPUQueueWorkDoneStatus status = WGPUQueueWorkDoneStatus_Error;
    std::string message;
};

struct ManyQueueWorkDoneState {
    uint32_t completed = 0;
    uint32_t lastResolved = 0;
    bool orderError = false;
    WGPUQueueWorkDoneStatus firstErrorStatus = WGPUQueueWorkDoneStatus_Success;
    std::string firstErrorMessage;
};

void onQueueWorkDone(WGPUQueueWorkDoneStatus status, WGPUStringView message, void* userdata1, void*) {
    auto* state = static_cast<QueueWorkDoneState*>(userdata1);
    state->completed = true;
    state->status = status;
    state->message = toString(message);
}

void onManyQueueWorkDone(WGPUQueueWorkDoneStatus status, WGPUStringView message, void* userdata1, void* userdata2) {
    auto* state = static_cast<ManyQueueWorkDoneState*>(userdata1);
    const uint32_t index = *static_cast<uint32_t*>(userdata2);
    if (status != WGPUQueueWorkDoneStatus_Success && state->firstErrorStatus == WGPUQueueWorkDoneStatus_Success) {
        state->firstErrorStatus = status;
        state->firstErrorMessage = toString(message);
    }
    if (index != state->lastResolved) {
        state->orderError = true;
    }
    state->lastResolved = index + 1;
    ++state->completed;
}

void ensureInstance(DeviceCache& c) {
    if (c.instance != nullptr) {
        return;
    }
    c.instance = createInstance();
    if (c.instance == nullptr) {
        throw TestFailed("failed to create WebGPU instance");
    }
}

void ensureAdapter(DeviceCache& c) {
    if (c.adapter != nullptr) {
        return;
    }

    ensureInstance(c);
    AdapterResult adapter = requestAdapterSync(c.instance, nullptr);
    if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
        throw TestFailed("failed to request adapter: " + adapter.message);
    }
    c.adapter = adapter.adapter;
}

// The native adapter is consumed by requestDevice (Dawn marks it "consumed"),
// so the all-features/max-limits device needs its OWN adapter rather than
// sharing the one used for the default device.
void ensureAdapterAllFeatures(DeviceCache& c) {
    if (c.adapterAllFeatures != nullptr) {
        return;
    }

    ensureInstance(c);
    AdapterResult adapter = requestAdapterSync(c.instance, nullptr);
    if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
        throw TestFailed("failed to request all-features/max-limits adapter: " + adapter.message);
    }
    c.adapterAllFeatures = adapter.adapter;
}

WGPUDevice getDevice() {
    DeviceCache& c = cache();
    if (c.device != nullptr) {
        return c.device;
    }

    ensureAdapter(c);

    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.callback = onDeviceLost;
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

    ensureAdapterAllFeatures(c);

    WGPULimits limits = WGPU_LIMITS_INIT;
    if (wgpuAdapterGetLimits(c.adapterAllFeatures, &limits) != WGPUStatus_Success) {
        throw TestFailed("failed to get adapter limits");
    }

    WGPUSupportedFeatures supportedFeatures = WGPU_SUPPORTED_FEATURES_INIT;
    wgpuAdapterGetFeatures(c.adapterAllFeatures, &supportedFeatures);

    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.requiredFeatureCount = supportedFeatures.featureCount;
    descriptor.requiredFeatures = supportedFeatures.features;
    descriptor.requiredLimits = &limits;
    descriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    descriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    descriptor.deviceLostCallbackInfo.callback = onDeviceLost;
    DeviceResult device = requestDeviceSync(c.instance, c.adapterAllFeatures, &descriptor);
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
            if (wgpuAdapterHasFeature(c.adapterAllFeatures, feature)) {
                fallbackFeatures.push_back(feature);
            }
        }

        WGPUDeviceDescriptor fallbackDescriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
        fallbackDescriptor.requiredFeatureCount = fallbackFeatures.size();
        fallbackDescriptor.requiredFeatures = fallbackFeatures.data();
        fallbackDescriptor.requiredLimits = &limits;
        fallbackDescriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;
        fallbackDescriptor.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
        fallbackDescriptor.deviceLostCallbackInfo.callback = onDeviceLost;
        device = requestDeviceSync(c.instance, c.adapterAllFeatures, &fallbackDescriptor);
        c.allFeaturesDeviceUsedFallback = true;
    }

    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        throw TestFailed("failed to request all-features/max-limits device: " + device.message);
    }
    c.deviceAllFeatures = device.device;
    c.queueAllFeatures = wgpuDeviceGetQueue(c.deviceAllFeatures);
    return c.deviceAllFeatures;
}

// phaseH3-B: periodic / on-loss device recycle to avoid whole-suite
// "GPU-state-degradation collateral". Cases run serially per worker process, so
// recycling at the case boundary is concurrency-safe.
unsigned long g_caseCounter = 0;

unsigned long deviceRecycleInterval() {
    static const unsigned long interval = [] {
        // MSVC deprecates std::getenv (C4996) and the project builds /W4 /WX; the returned pointer is
        // read immediately and only parsed, so suppress the warning narrowly here (same pattern as
        // backend_yawgpu.cpp) rather than weaken /WX globally. Semantics are identical on every platform.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
        const char* value = std::getenv("CTS_DEVICE_RECYCLE_INTERVAL");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
        if (value == nullptr || *value == '\0') {
            return 500UL;
        }
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end == value) {
            return 500UL;
        }
        return parsed;
    }();
    return interval;
}

// Release the cached instance/adapter/device handles (same order as the
// destructor), null the fields, and reset the recycle-relevant flags so the next
// device()/queue() lazily rebuilds from a fresh instance+adapter. The adapter is
// consumed by requestDevice, so a rebuild MUST start from a fresh instance.
void teardownDevices(DeviceCache& c) {
    if (c.queueAllFeatures != nullptr) {
        wgpuQueueRelease(c.queueAllFeatures);
        c.queueAllFeatures = nullptr;
    }
    if (c.deviceAllFeatures != nullptr) {
        wgpuDeviceRelease(c.deviceAllFeatures);
        c.deviceAllFeatures = nullptr;
    }
    if (c.adapterAllFeatures != nullptr) {
        wgpuAdapterRelease(c.adapterAllFeatures);
        c.adapterAllFeatures = nullptr;
    }
    if (c.queue != nullptr) {
        wgpuQueueRelease(c.queue);
        c.queue = nullptr;
    }
    if (c.device != nullptr) {
        wgpuDeviceRelease(c.device);
        c.device = nullptr;
    }
    if (c.adapter != nullptr) {
        wgpuAdapterRelease(c.adapter);
        c.adapter = nullptr;
    }
    if (c.instance != nullptr) {
        wgpuInstanceRelease(c.instance);
        c.instance = nullptr;
    }
    c.deviceLost = false;
    c.allFeaturesDeviceUsedFallback = false;
}

void recycleDevicesIfNeeded() {
    DeviceCache& c = cache();
    if (c.deviceLost) {
        teardownDevices(c);
        return;
    }
    const unsigned long interval = deviceRecycleInterval();
    if (interval != 0 && g_caseCounter != 0 && (g_caseCounter % interval == 0)) {
        teardownDevices(c);
    }
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
    if (fixture != nullptr) {
        // phaseH3-B: at the start of each case, before the fixture touches the
        // device, heal a lost device / periodically recycle. setCurrentTest(nullptr)
        // (case end) must not recycle or count.
        recycleDevicesIfNeeded();
        ++g_caseCounter;
    }
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
    for (WGPUBindGroup bindGroup : bindGroups_) {
        wgpuBindGroupRelease(bindGroup);
    }
    bindGroups_.clear();
    for (WGPURenderPipeline pipeline : renderPipelines_) {
        wgpuRenderPipelineRelease(pipeline);
    }
    renderPipelines_.clear();
    for (WGPUComputePipeline pipeline : computePipelines_) {
        wgpuComputePipelineRelease(pipeline);
    }
    computePipelines_.clear();
    for (WGPUShaderModule shaderModule : shaderModules_) {
        wgpuShaderModuleRelease(shaderModule);
    }
    shaderModules_.clear();
    for (WGPUPipelineLayout pipelineLayout : pipelineLayouts_) {
        wgpuPipelineLayoutRelease(pipelineLayout);
    }
    pipelineLayouts_.clear();
    for (WGPUBindGroupLayout bindGroupLayout : bindGroupLayouts_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout);
    }
    bindGroupLayouts_.clear();
    for (WGPUBindGroupLayout bindGroupLayout : mismatchedDeviceBindGroupLayouts_) {
        wgpuBindGroupLayoutRelease(bindGroupLayout);
    }
    mismatchedDeviceBindGroupLayouts_.clear();
    for (WGPUBuffer buffer : buffers_) {
        wgpuBufferRelease(buffer);
    }
    buffers_.clear();
    for (WGPUBuffer buffer : mismatchedDeviceBuffers_) {
        wgpuBufferRelease(buffer);
    }
    mismatchedDeviceBuffers_.clear();
    if (mismatchedDevice_ != nullptr) {
        wgpuDeviceRelease(mismatchedDevice_);
        mismatchedDevice_ = nullptr;
    }
    if (mismatchedAdapter_ != nullptr) {
        wgpuAdapterRelease(mismatchedAdapter_);
        mismatchedAdapter_ = nullptr;
    }
}

WGPUDevice GpuTest::device() const {
    return getDevice();
}

WGPUDevice GpuTest::mismatchedDevice() {
    if (mismatchedDevice_ != nullptr) {
        return mismatchedDevice_;
    }

    DeviceCache& c = cache();
    ensureAdapter(c);
    if (mismatchedAdapter_ == nullptr) {
        AdapterResult adapter = requestAdapterSync(c.instance, nullptr);
        if (adapter.status != WGPURequestAdapterStatus_Success || adapter.adapter == nullptr) {
            fail("failed to request mismatched adapter: " + adapter.message);
        }
        mismatchedAdapter_ = adapter.adapter;
    }

    WGPUDeviceDescriptor descriptor = WGPU_DEVICE_DESCRIPTOR_INIT;
    descriptor.uncapturedErrorCallbackInfo.callback = onUncapturedError;
    DeviceResult device = requestDeviceSync(c.instance, mismatchedAdapter_, &descriptor);
    if (device.status != WGPURequestDeviceStatus_Success || device.device == nullptr) {
        fail("failed to request mismatched device: " + device.message);
    }
    mismatchedDevice_ = device.device;
    return mismatchedDevice_;
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

WGPUCompatibilityModeLimits GpuTest::getCompatibilityModeLimits() const {
    WGPUCompatibilityModeLimits compat = WGPU_COMPATIBILITY_MODE_LIMITS_INIT;
    WGPULimits limits = WGPU_LIMITS_INIT;
    limits.nextInChain = &compat.chain;
    if (wgpuDeviceGetLimits(device(), &limits) != WGPUStatus_Success) {
        fail("failed to get device limits");
    }
    return compat;
}

WGPUBuffer GpuTest::makeBufferWithContents(const void* data, size_t size, WGPUBufferUsage usage) {
    WGPUBufferDescriptor desc = WGPU_BUFFER_DESCRIPTOR_INIT;
    desc.size = size;
    desc.usage = usage;
    desc.mappedAtCreation = WGPU_TRUE;
    WGPUBuffer buffer = createBufferTracked(desc);

    void* mapped = wgpuBufferGetMappedRange(buffer, 0, size);
    if (size > 0 && mapped == nullptr) {
        fail("failed to get mapped range for initial buffer contents");
    }
    if (size > 0) {
        std::memcpy(mapped, data, size);
    }
    wgpuBufferUnmap(buffer);
    return buffer;
}

void GpuTest::expectGPUBufferValuesEqual(
    WGPUBuffer src,
    const void* expected,
    size_t size,
    uint64_t srcByteOffset) {
    WGPUBufferDescriptor stagingDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    stagingDesc.size = size;
    stagingDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer staging = createBufferTracked(stagingDesc);

    WGPUCommandEncoder encoder = createCommandEncoderTracked();
    wgpuCommandEncoderCopyBufferToBuffer(encoder, src, srcByteOffset, staging, 0, size);
    WGPUCommandBuffer commandBuffer = finishTracked(encoder);
    wgpuQueueSubmit(queue(), 1, &commandBuffer);

    expectMapAsync(staging, WGPUMapMode_Read, true, 0, size);
    const void* actual = wgpuBufferGetConstMappedRange(staging, 0, size);
    if (size > 0 && actual == nullptr) {
        wgpuBufferUnmap(staging);
        fail("failed to get mapped range for GPU buffer readback");
    }

    const auto* actualBytes = static_cast<const uint8_t*>(actual);
    const auto* expectedBytes = static_cast<const uint8_t*>(expected);
    size_t mismatchIndex = size;
    uint8_t mismatchExpected = 0;
    uint8_t mismatchActual = 0;
    for (size_t i = 0; i < size; ++i) {
        if (actualBytes[i] != expectedBytes[i]) {
            mismatchIndex = i;
            mismatchExpected = expectedBytes[i];
            mismatchActual = actualBytes[i];
            break;
        }
    }
    wgpuBufferUnmap(staging);

    if (mismatchIndex != size) {
        std::ostringstream message;
        message << "GPU buffer mismatch at byte " << mismatchIndex
                << ": expected " << static_cast<int>(mismatchExpected)
                << ", got " << static_cast<int>(mismatchActual);
        fail(message.str());
    }
}

void GpuTest::expectGPUBufferValuesPassCheck(
    WGPUBuffer src,
    const std::function<std::optional<std::string>(const uint8_t* actual, size_t len)>& check,
    uint64_t srcByteOffset,
    size_t byteLength) {
    WGPUBufferDescriptor stagingDesc = WGPU_BUFFER_DESCRIPTOR_INIT;
    stagingDesc.size = byteLength;
    stagingDesc.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
    WGPUBuffer staging = createBufferTracked(stagingDesc);

    WGPUCommandEncoder encoder = createCommandEncoderTracked();
    wgpuCommandEncoderCopyBufferToBuffer(encoder, src, srcByteOffset, staging, 0, byteLength);
    WGPUCommandBuffer commandBuffer = finishTracked(encoder);
    wgpuQueueSubmit(queue(), 1, &commandBuffer);

    expectMapAsync(staging, WGPUMapMode_Read, true, 0, byteLength);
    const void* actual = wgpuBufferGetConstMappedRange(staging, 0, byteLength);
    if (byteLength > 0 && actual == nullptr) {
        wgpuBufferUnmap(staging);
        fail("failed to get mapped range for GPU buffer readback");
    }

    const auto* actualBytes = static_cast<const uint8_t*>(actual);
    const std::optional<std::string> message = check(actualBytes, byteLength);
    wgpuBufferUnmap(staging);
    if (message) {
        fail(*message);
    }
}

void GpuTest::expectGPUBufferValuesEqualWhenInterpretedAsTextureFormat(
    const uint8_t* expected,
    size_t expectedLen,
    WGPUBuffer buffer,
    WGPUTextureFormat format,
    WGPUExtent3D size,
    TexelCopyBufferLayout dataLayout) {
    expectGPUBufferValuesPassCheck(buffer, [&](const uint8_t* actual, size_t actualLen) -> std::optional<std::string> {
        TexelViewConfig config;
        config.bytesPerRow = dataLayout.bytesPerRow;
        config.rowsPerImage = dataLayout.rowsPerImage;
        config.subrectOrigin = WGPUOrigin3D{0, 0, 0};
        config.subrectSize = size;
        const TexelView actualView = TexelView::fromTextureDataByReference(format, actual, actualLen, config);
        const TexelView expectedView = TexelView::fromTextureDataByReference(format, expected, expectedLen, config);
        std::optional<std::string> failedPixels = findFailedPixels(
            format,
            WGPUOrigin3D{0, 0, 0},
            size,
            actualView,
            expectedView,
            0.0);
        if (failedPixels) {
            return failedPixels;
        }

        const TextureBlockInfo block = getBlockInfoForTextureFormat(format);
        for (uint32_t z = 0; z < size.depthOrArrayLayers; ++z) {
            for (uint32_t y = 0; y < size.height; ++y) {
                const uint64_t rowStart = static_cast<uint64_t>(z) * dataLayout.rowsPerImage * dataLayout.bytesPerRow
                    + static_cast<uint64_t>(y) * dataLayout.bytesPerRow;
                const uint64_t texelEnd = rowStart + static_cast<uint64_t>(size.width) * block.bytesPerBlock;
                const uint64_t rowEnd = rowStart + dataLayout.bytesPerRow;
                for (uint64_t i = texelEnd; i < rowEnd && i < actualLen && i < expectedLen; ++i) {
                    if (actual[i] != expected[i]) {
                        std::ostringstream message;
                        message << "texture padding mismatch at byte " << i
                                << ": expected " << static_cast<int>(expected[i])
                                << ", got " << static_cast<int>(actual[i]);
                        return message.str();
                    }
                }
            }
        }
        return std::nullopt;
    }, dataLayout.offset, expectedLen);
}

void GpuTest::queueWriteBuffer(WGPUBuffer buffer, uint64_t bufferOffset, const void* data, size_t size) {
    wgpuQueueWriteBuffer(queue(), buffer, bufferOffset, data, size);
}

void GpuTest::queueWriteTexture(
    WGPUTexture dst,
    WGPUExtent3D copySize,
    const WGPUTexelCopyBufferLayout& layout,
    const void* data,
    size_t size,
    uint32_t mipLevel,
    WGPUOrigin3D origin) {
    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = dst;
    destination.mipLevel = mipLevel;
    destination.origin = origin;
    destination.aspect = WGPUTextureAspect_All;
    wgpuQueueWriteTexture(queue(), &destination, data, size, &layout, &copySize);
}

void GpuTest::copyBufferToTexture(
    WGPUCommandEncoder encoder,
    WGPUBuffer src,
    uint32_t bytesPerRow,
    WGPUTexture dst,
    WGPUExtent3D size) {
    WGPUTexelCopyBufferInfo source = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    source.buffer = src;
    source.layout.offset = 0;
    source.layout.bytesPerRow = bytesPerRow;
    source.layout.rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = dst;
    destination.mipLevel = 0;
    destination.origin = WGPUOrigin3D{0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    wgpuCommandEncoderCopyBufferToTexture(encoder, &source, &destination, &size);
}

void GpuTest::copyTextureToBuffer(
    WGPUCommandEncoder encoder,
    WGPUTexture src,
    WGPUBuffer dst,
    uint32_t bytesPerRow,
    WGPUExtent3D size) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = src;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyBufferInfo destination = WGPU_TEXEL_COPY_BUFFER_INFO_INIT;
    destination.buffer = dst;
    destination.layout.offset = 0;
    destination.layout.bytesPerRow = bytesPerRow;
    destination.layout.rowsPerImage = WGPU_COPY_STRIDE_UNDEFINED;

    wgpuCommandEncoderCopyTextureToBuffer(encoder, &source, &destination, &size);
}

void GpuTest::copyTextureToTexture(
    WGPUCommandEncoder encoder,
    WGPUTexture src,
    WGPUTexture dst,
    WGPUExtent3D size) {
    WGPUTexelCopyTextureInfo source = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    source.texture = src;
    source.mipLevel = 0;
    source.origin = WGPUOrigin3D{0, 0, 0};
    source.aspect = WGPUTextureAspect_All;

    WGPUTexelCopyTextureInfo destination = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
    destination.texture = dst;
    destination.mipLevel = 0;
    destination.origin = WGPUOrigin3D{0, 0, 0};
    destination.aspect = WGPUTextureAspect_All;

    wgpuCommandEncoderCopyTextureToTexture(encoder, &source, &destination, &size);
}

void GpuTest::onSubmittedWorkDoneSync() {
    QueueWorkDoneState state;
    WGPUQueueWorkDoneCallbackInfo callbackInfo = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = onQueueWorkDone;
    callbackInfo.userdata1 = &state;

    (void)wgpuQueueOnSubmittedWorkDone(queue(), callbackInfo);
    if (!processEventsUntil(cache().instance, [&] { return state.completed; })) {
        fail("onSubmittedWorkDone timed out");
    }
    if (state.status != WGPUQueueWorkDoneStatus_Success) {
        fail("onSubmittedWorkDone failed: " + state.message);
    }
}

void GpuTest::onSubmittedWorkDoneMany(uint32_t n, bool checkOrder) {
    ManyQueueWorkDoneState state;
    std::vector<uint32_t> indices;
    indices.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        indices.push_back(i);
        WGPUQueueWorkDoneCallbackInfo callbackInfo = WGPU_QUEUE_WORK_DONE_CALLBACK_INFO_INIT;
        callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
        callbackInfo.callback = onManyQueueWorkDone;
        callbackInfo.userdata1 = &state;
        callbackInfo.userdata2 = &indices.back();
        (void)wgpuQueueOnSubmittedWorkDone(queue(), callbackInfo);
    }

    if (!processEventsUntil(cache().instance, [&] { return state.completed == n; })) {
        fail("onSubmittedWorkDone timed out");
    }
    if (state.firstErrorStatus != WGPUQueueWorkDoneStatus_Success) {
        fail("onSubmittedWorkDone failed: " + state.firstErrorMessage);
    }
    if (checkOrder && state.orderError) {
        fail("onSubmittedWorkDone callbacks resolved out of order");
    }
}

WGPUBuffer GpuTest::createBufferTracked(const WGPUBufferDescriptor& desc) {
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(device(), &desc);
    if (buffer != nullptr) {
        buffers_.push_back(buffer);
    }
    return buffer;
}

WGPUBuffer GpuTest::createBufferOnMismatchedDevice(const WGPUBufferDescriptor& desc) {
    WGPUBuffer buffer = wgpuDeviceCreateBuffer(mismatchedDevice(), &desc);
    if (buffer != nullptr) {
        mismatchedDeviceBuffers_.push_back(buffer);
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

WGPUShaderModule GpuTest::createShaderModuleTracked(std::string_view wgsl) {
    WGPUShaderSourceWGSL source = WGPU_SHADER_SOURCE_WGSL_INIT;
    source.code = WGPUStringView{wgsl.data(), wgsl.size()};

    WGPUShaderModuleDescriptor desc = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
    desc.nextInChain = &source.chain;

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device(), &desc);
    if (shaderModule != nullptr) {
        shaderModules_.push_back(shaderModule);
    }
    return shaderModule;
}

WGPUBindGroupLayout GpuTest::createBindGroupLayoutTracked(const WGPUBindGroupLayoutDescriptor& desc) {
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(device(), &desc);
    if (layout != nullptr) {
        bindGroupLayouts_.push_back(layout);
    }
    return layout;
}

WGPUBindGroupLayout GpuTest::createBindGroupLayoutOnMismatchedDevice(const WGPUBindGroupLayoutDescriptor& desc) {
    WGPUBindGroupLayout layout = wgpuDeviceCreateBindGroupLayout(mismatchedDevice(), &desc);
    if (layout != nullptr) {
        mismatchedDeviceBindGroupLayouts_.push_back(layout);
    }
    return layout;
}

WGPUBindGroup GpuTest::createBindGroupTracked(const WGPUBindGroupDescriptor& desc) {
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device(), &desc);
    if (bindGroup != nullptr) {
        bindGroups_.push_back(bindGroup);
    }
    return bindGroup;
}

WGPUPipelineLayout GpuTest::createPipelineLayoutTracked(const WGPUPipelineLayoutDescriptor& desc) {
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(device(), &desc);
    if (layout != nullptr) {
        pipelineLayouts_.push_back(layout);
    }
    return layout;
}

WGPURenderPipeline GpuTest::createRenderPipelineTracked(const WGPURenderPipelineDescriptor& desc) {
    WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device(), &desc);
    if (pipeline != nullptr) {
        renderPipelines_.push_back(pipeline);
    }
    return pipeline;
}

WGPUComputePipeline GpuTest::createComputePipelineTracked(const WGPUComputePipelineDescriptor& desc) {
    WGPUComputePipeline pipeline = wgpuDeviceCreateComputePipeline(device(), &desc);
    if (pipeline != nullptr) {
        computePipelines_.push_back(pipeline);
    }
    return pipeline;
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

bool GpuTest::isTextureFormatUsableAsReadOnlyStorageTexture(WGPUTextureFormat format) {
    if (format == WGPUTextureFormat_BGRA8Unorm) {
        return false;
    }
    return isTextureFormatUsableAsWriteOnlyStorageTexture(format);
}

bool GpuTest::isTextureFormatUsableAsReadWriteStorageTexture(WGPUTextureFormat format) {
    return textureFormatInList(format, kReadWriteStorageTextureFormats)
        || (wgpuDeviceHasFeature(device(), WGPUFeatureName_TextureFormatsTier2)
            && textureFormatInList(format, kTextureFormatsTier2EnablesStorageReadWrite));
}

bool GpuTest::isTextureFormatUsableWithStorageAccessMode(
    WGPUTextureFormat format,
    WGPUStorageTextureAccess access) {
    switch (access) {
        case WGPUStorageTextureAccess_ReadOnly:
            return isTextureFormatUsableAsReadOnlyStorageTexture(format);
        case WGPUStorageTextureAccess_WriteOnly:
            return isTextureFormatUsableAsWriteOnlyStorageTexture(format);
        case WGPUStorageTextureAccess_ReadWrite:
            return isTextureFormatUsableAsReadWriteStorageTexture(format);
        default:
            return false;
    }
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
