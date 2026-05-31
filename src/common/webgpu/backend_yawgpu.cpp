#include "common/webgpu/backend.h"

#include <webgpu-headers/yawgpu.h>

namespace cts {

WGPUInstance createInstance() {
    YaWGPUInstanceBackendSelect backendSelect = {};
    backendSelect.chain.next = nullptr;
    backendSelect.chain.sType = YAWGPU_STYPE_INSTANCE_BACKEND_SELECT;
#if defined(__APPLE__)
    backendSelect.backend = YAWGPU_INSTANCE_BACKEND_METAL;
#else
    backendSelect.backend = YAWGPU_INSTANCE_BACKEND_VULKAN;
#endif

    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &backendSelect.chain;
    return wgpuCreateInstance(&descriptor);
}

const char* backendName() {
    return "yawgpu";
}

} // namespace cts
