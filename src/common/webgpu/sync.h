#pragma once

#include <cstdint>
#include <string>

#include "cts/webgpu.h"

namespace cts {

WGPUWaitStatus waitFuture(WGPUInstance instance, WGPUFuture future, uint64_t timeoutNs);

struct AdapterResult {
    WGPURequestAdapterStatus status;
    WGPUAdapter adapter;
    std::string message;
};

AdapterResult requestAdapterSync(WGPUInstance instance, const WGPURequestAdapterOptions* options);

struct DeviceResult {
    WGPURequestDeviceStatus status;
    WGPUDevice device;
    std::string message;
};

DeviceResult requestDeviceSync(WGPUInstance instance, WGPUAdapter adapter, const WGPUDeviceDescriptor* descriptor);

struct ScopeResult {
    WGPUPopErrorScopeStatus status;
    WGPUErrorType type;
    std::string message;
};

ScopeResult popErrorScopeSync(WGPUInstance instance, WGPUDevice device);

} // namespace cts
