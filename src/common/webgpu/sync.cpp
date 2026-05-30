#include "common/webgpu/sync.h"

#include <chrono>
#include <thread>

namespace cts {
namespace {

constexpr uint64_t kPollIntervalNs = 1'000'000;

std::string toString(WGPUStringView view) {
    if (view.data == nullptr) {
        return {};
    }

    if (view.length == WGPU_STRLEN) {
        return std::string(view.data);
    }

    return std::string(view.data, view.length);
}

struct RequestAdapterState {
    bool completed = false;
    WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Error;
    WGPUAdapter adapter = nullptr;
    std::string message;
};

struct RequestDeviceState {
    bool completed = false;
    WGPURequestDeviceStatus status = WGPURequestDeviceStatus_Error;
    WGPUDevice device = nullptr;
    std::string message;
};

struct PopErrorScopeState {
    bool completed = false;
    WGPUPopErrorScopeStatus status = WGPUPopErrorScopeStatus_Error;
    WGPUErrorType type = WGPUErrorType_NoError;
    std::string message;
};

void onRequestAdapter(WGPURequestAdapterStatus status,
                      WGPUAdapter adapter,
                      WGPUStringView message,
                      void* userdata1,
                      void*) {
    auto* state = static_cast<RequestAdapterState*>(userdata1);
    state->completed = true;
    state->status = status;
    state->adapter = adapter;
    state->message = toString(message);
}

void onRequestDevice(WGPURequestDeviceStatus status,
                     WGPUDevice device,
                     WGPUStringView message,
                     void* userdata1,
                     void*) {
    auto* state = static_cast<RequestDeviceState*>(userdata1);
    state->completed = true;
    state->status = status;
    state->device = device;
    state->message = toString(message);
}

void onPopErrorScope(WGPUPopErrorScopeStatus status,
                     WGPUErrorType type,
                     WGPUStringView message,
                     void* userdata1,
                     void*) {
    auto* state = static_cast<PopErrorScopeState*>(userdata1);
    state->completed = true;
    state->status = status;
    state->type = type;
    state->message = toString(message);
}

template <class Pred>
bool pumpUntil(WGPUInstance instance, uint64_t timeoutNs, Pred done) {
    const auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::nanoseconds(timeoutNs);
    while (!done() && std::chrono::steady_clock::now() - start < timeout) {
        wgpuInstanceProcessEvents(instance);
        std::this_thread::sleep_for(std::chrono::nanoseconds(kPollIntervalNs));
    }
    return done();
}

} // namespace

AdapterResult requestAdapterSync(WGPUInstance instance, const WGPURequestAdapterOptions* options) {
    RequestAdapterState state;
    WGPURequestAdapterCallbackInfo callbackInfo = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = onRequestAdapter;
    callbackInfo.userdata1 = &state;

    (void)wgpuInstanceRequestAdapter(instance, options, callbackInfo);

    if (!pumpUntil(instance, 5'000'000'000, [&] { return state.completed; })) {
        return AdapterResult{WGPURequestAdapterStatus_Error, nullptr, "requestAdapter timed out"};
    }

    return AdapterResult{state.status, state.adapter, state.message};
}

DeviceResult requestDeviceSync(WGPUInstance instance, WGPUAdapter adapter, const WGPUDeviceDescriptor* descriptor) {
    RequestDeviceState state;
    WGPURequestDeviceCallbackInfo callbackInfo = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = onRequestDevice;
    callbackInfo.userdata1 = &state;

    (void)wgpuAdapterRequestDevice(adapter, descriptor, callbackInfo);
    if (!pumpUntil(instance, 5'000'000'000, [&] { return state.completed; })) {
        return DeviceResult{WGPURequestDeviceStatus_Error, nullptr, "requestDevice timed out"};
    }
    return DeviceResult{state.status, state.device, state.message};
}

ScopeResult popErrorScopeSync(WGPUInstance instance, WGPUDevice device) {
    PopErrorScopeState state;
    WGPUPopErrorScopeCallbackInfo callbackInfo = WGPU_POP_ERROR_SCOPE_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = onPopErrorScope;
    callbackInfo.userdata1 = &state;

    (void)wgpuDevicePopErrorScope(device, callbackInfo);
    if (!pumpUntil(instance, 5'000'000'000, [&] { return state.completed; })) {
        return ScopeResult{WGPUPopErrorScopeStatus_Error, WGPUErrorType_NoError, "popErrorScope timed out"};
    }
    return ScopeResult{state.status, state.type, state.message};
}

} // namespace cts
