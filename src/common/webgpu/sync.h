#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "cts/webgpu.h"

namespace cts {

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
WGPUMapAsyncStatus bufferMapSync(WGPUInstance instance, WGPUBuffer buffer, WGPUMapMode mode, size_t offset, size_t size);

} // namespace cts
