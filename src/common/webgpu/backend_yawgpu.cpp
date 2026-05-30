#include "common/webgpu/backend.h"

#include <webgpu-headers/yawgpu.h>

namespace cts {

WGPUInstance createInstance() {
    YaWGPUInstanceBackendSelect backendSelect = {};
    backendSelect.chain.next = nullptr;
    backendSelect.chain.sType = YAWGPU_STYPE_INSTANCE_BACKEND_SELECT;
    backendSelect.backend = YAWGPU_INSTANCE_BACKEND_METAL;

    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &backendSelect.chain;
    return wgpuCreateInstance(&descriptor);
}

const char* backendName() {
    return "yawgpu";
}

bool backendSupportsTimeoutWaitAny() {
    return false;
}

} // namespace cts
