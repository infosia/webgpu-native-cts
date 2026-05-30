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

} // namespace cts
