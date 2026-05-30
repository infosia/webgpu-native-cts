#include <cstdlib>
#include <iostream>
#include <string>

#include "common/webgpu/backend.h"
#include "common/webgpu/sync.h"

namespace {

std::string toString(WGPUStringView view) {
    if (view.data == nullptr) {
        return {};
    }

    if (view.length == WGPU_STRLEN) {
        return std::string(view.data);
    }

    return std::string(view.data, view.length);
}

const char* backendTypeName(WGPUBackendType type) {
    switch (type) {
    case WGPUBackendType_Undefined:
        return "undefined";
    case WGPUBackendType_Null:
        return "null";
    case WGPUBackendType_WebGPU:
        return "webgpu";
    case WGPUBackendType_D3D11:
        return "d3d11";
    case WGPUBackendType_D3D12:
        return "d3d12";
    case WGPUBackendType_Metal:
        return "metal";
    case WGPUBackendType_Vulkan:
        return "vulkan";
    case WGPUBackendType_OpenGL:
        return "opengl";
    case WGPUBackendType_OpenGLES:
        return "opengles";
    case WGPUBackendType_Force32:
        return "force32";
    }
    return "unknown";
}

const char* adapterTypeName(WGPUAdapterType type) {
    switch (type) {
    case WGPUAdapterType_DiscreteGPU:
        return "discrete-gpu";
    case WGPUAdapterType_IntegratedGPU:
        return "integrated-gpu";
    case WGPUAdapterType_CPU:
        return "cpu";
    case WGPUAdapterType_Unknown:
        return "unknown";
    case WGPUAdapterType_Force32:
        return "force32";
    }
    return "unknown";
}

void printUsage() {
    std::cout << "Usage: cts [--help] [--version]\n"
              << "\n"
              << "Without arguments, creates a WebGPU instance, requests an adapter,\n"
              << "prints adapter information, and exits.\n";
}

void printVersion() {
    std::cout << "cts 0.0.0 (backend: " << cts::backendName() << ")\n";
}

int runAdapterEnumeration() {
    WGPUInstance instance = cts::createInstance();
    if (instance == nullptr) {
        std::cerr << "failed to create WebGPU instance\n";
        return EXIT_FAILURE;
    }

    cts::AdapterResult adapterResult = cts::requestAdapterSync(instance, nullptr);
    if (adapterResult.status != WGPURequestAdapterStatus_Success || adapterResult.adapter == nullptr) {
        std::cerr << "failed to request WebGPU adapter";
        if (!adapterResult.message.empty()) {
            std::cerr << ": " << adapterResult.message;
        }
        std::cerr << "\n";
        wgpuInstanceRelease(instance);
        return EXIT_FAILURE;
    }

    WGPUAdapterInfo info = WGPU_ADAPTER_INFO_INIT;
    WGPUStatus infoStatus = wgpuAdapterGetInfo(adapterResult.adapter, &info);
    if (infoStatus != WGPUStatus_Success) {
        std::cerr << "failed to query WebGPU adapter info\n";
        wgpuAdapterRelease(adapterResult.adapter);
        wgpuInstanceRelease(instance);
        return EXIT_FAILURE;
    }

    std::cout << "WebGPU adapter:\n"
              << "  backendType: " << backendTypeName(info.backendType) << "\n"
              << "  adapterType: " << adapterTypeName(info.adapterType) << "\n"
              << "  vendor: " << toString(info.vendor) << "\n"
              << "  device: " << toString(info.device) << "\n"
              << "  description: " << toString(info.description) << "\n";

    wgpuAdapterInfoFreeMembers(info);
    wgpuAdapterRelease(adapterResult.adapter);
    wgpuInstanceRelease(instance);
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 2) {
        printUsage();
        return EXIT_FAILURE;
    }

    if (argc == 2) {
        const std::string arg = argv[1];
        if (arg == "--help") {
            printUsage();
            return EXIT_SUCCESS;
        }
        if (arg == "--version") {
            printVersion();
            return EXIT_SUCCESS;
        }

        std::cerr << "unknown option: " << arg << "\n";
        printUsage();
        return EXIT_FAILURE;
    }

    return runAdapterEnumeration();
}
