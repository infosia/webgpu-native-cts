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
    // MSVC deprecates std::getenv (C4996) and the project builds /W4 /WX; the returned pointer is
    // read immediately and only compared, so suppress the warning narrowly here rather than weaken
    // /WX globally. Lifetime/semantics are identical to a plain std::getenv on every platform.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* sel = std::getenv("CTS_YAWGPU_BACKEND");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (sel != nullptr) {
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

const WGPURequestAdapterOptions* adapterOptions() {
    return nullptr;
}

} // namespace cts
