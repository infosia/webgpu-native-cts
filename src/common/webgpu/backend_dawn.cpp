#include "common/webgpu/backend.h"

namespace cts {

WGPUInstance createInstance() {
    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    return wgpuCreateInstance(&descriptor);
}

const char* backendName() {
    return "dawn";
}

bool backendSupportsTimeoutWaitAny() {
    return true;
}

} // namespace cts
