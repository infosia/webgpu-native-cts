#include "common/webgpu/backend.h"

#include <cstdlib>
#include <cstring>

namespace {

WGPUBackendType defaultAdapterBackendType() {
#if defined(__APPLE__)
    return WGPUBackendType_Metal;
#else
    return WGPUBackendType_Vulkan;
#endif
}

WGPUBackendType configuredAdapterBackendType() {
    WGPUBackendType backendType = defaultAdapterBackendType();

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    const char* sel = std::getenv("CTS_DAWN_BACKEND");
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    if (sel != nullptr) {
        if (std::strcmp(sel, "vulkan") == 0) {
            backendType = WGPUBackendType_Vulkan;
        } else if (std::strcmp(sel, "d3d12") == 0) {
            backendType = WGPUBackendType_D3D12;
        } else if (std::strcmp(sel, "d3d11") == 0) {
            backendType = WGPUBackendType_D3D11;
        } else if (std::strcmp(sel, "metal") == 0) {
            backendType = WGPUBackendType_Metal;
        } else if (std::strcmp(sel, "opengl") == 0) {
            backendType = WGPUBackendType_OpenGL;
        } else if (std::strcmp(sel, "opengles") == 0) {
            backendType = WGPUBackendType_OpenGLES;
        } else if (std::strcmp(sel, "null") == 0) {
            backendType = WGPUBackendType_Null;
        }
        // Unknown / empty values leave the platform default unchanged.
    }

    return backendType;
}

WGPURequestAdapterOptions makeAdapterOptions() {
    WGPURequestAdapterOptions options = WGPU_REQUEST_ADAPTER_OPTIONS_INIT;
    options.backendType = configuredAdapterBackendType();
    return options;
}

} // namespace

namespace cts {

WGPUInstance createInstance() {
    WGPUInstanceDescriptor descriptor = WGPU_INSTANCE_DESCRIPTOR_INIT;
    return wgpuCreateInstance(&descriptor);
}

const char* backendName() {
    return "dawn";
}

const WGPURequestAdapterOptions* adapterOptions() {
    static WGPURequestAdapterOptions options = makeAdapterOptions();
    return &options;
}

} // namespace cts
