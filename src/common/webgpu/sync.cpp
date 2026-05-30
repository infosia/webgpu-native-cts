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

} // namespace

WGPUWaitStatus waitFuture(WGPUInstance instance, WGPUFuture future, uint64_t timeoutNs) {
    (void)future;

    const auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::nanoseconds(timeoutNs);

    do {
        wgpuInstanceProcessEvents(instance);
        std::this_thread::sleep_for(std::chrono::nanoseconds(kPollIntervalNs));
    } while (std::chrono::steady_clock::now() - start < timeout);

    return WGPUWaitStatus_TimedOut;
}

AdapterResult requestAdapterSync(WGPUInstance instance, const WGPURequestAdapterOptions* options) {
    RequestAdapterState state;
    WGPURequestAdapterCallbackInfo callbackInfo = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
    callbackInfo.mode = WGPUCallbackMode_AllowProcessEvents;
    callbackInfo.callback = onRequestAdapter;
    callbackInfo.userdata1 = &state;

    (void)wgpuInstanceRequestAdapter(instance, options, callbackInfo);

    constexpr uint64_t timeoutNs = 5'000'000'000;
    const auto start = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::nanoseconds(timeoutNs);

    while (!state.completed && std::chrono::steady_clock::now() - start < timeout) {
        wgpuInstanceProcessEvents(instance);
        std::this_thread::sleep_for(std::chrono::nanoseconds(kPollIntervalNs));
    }

    if (!state.completed) {
        return AdapterResult{WGPURequestAdapterStatus_Error, nullptr, "requestAdapter timed out"};
    }

    return AdapterResult{state.status, state.adapter, state.message};
}

} // namespace cts
