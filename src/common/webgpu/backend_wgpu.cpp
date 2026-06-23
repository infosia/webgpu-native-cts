#include "common/webgpu/backend.h"

namespace cts {

WGPUInstance createInstance() {
    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    return wgpuCreateInstance(&descriptor);
}

const char* backendName() {
    return "wgpu-native";
}

const WGPURequestAdapterOptions* adapterOptions() {
    return nullptr;
}

} // namespace cts
