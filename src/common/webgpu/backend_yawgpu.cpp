#include "common/webgpu/backend.h"

#include <cstdlib>
#include <cstring>

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
    // Runtime override of the yawgpu instance backend (default: platform choice above).
    // e.g. CTS_YAWGPU_BACKEND=vulkan to drive yawgpu's Vulkan HAL on macOS via MoltenVK.
    // The selected backend must be compiled into the linked libyawgpu.a (yawgpu --features <backend>);
    // if it is not, yawgpu returns a NULL instance with a clear message and the run fails fast.
    if (const char* sel = std::getenv("CTS_YAWGPU_BACKEND")) {
        if (std::strcmp(sel, "metal") == 0) {
            backendSelect.backend = YAWGPU_INSTANCE_BACKEND_METAL;
        } else if (std::strcmp(sel, "vulkan") == 0) {
            backendSelect.backend = YAWGPU_INSTANCE_BACKEND_VULKAN;
        } else if (std::strcmp(sel, "gles") == 0) {
            backendSelect.backend = YAWGPU_INSTANCE_BACKEND_GLES;
        }
        // Unknown / empty values leave the platform default unchanged.
    }

    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    descriptor.nextInChain = &backendSelect.chain;
    return wgpuCreateInstance(&descriptor);
}

const char* backendName() {
    return "yawgpu";
}

} // namespace cts
