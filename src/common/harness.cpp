#include "cts/gpu.h"

#include <utility>

#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

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

    ~DeviceCache() {
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

WGPUDevice getDevice() {
    DeviceCache& c = cache();
    if (c.device != nullptr) {
        return c.device;
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
    (void)getDevice();
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
    return cache().device;
}

WGPUQueue GpuTest::queue() const {
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

} // namespace cts
