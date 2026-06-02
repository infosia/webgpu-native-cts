#include <cstdlib>
#include <iostream>
#include <string>

#include "cts/test.h"
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
    std::cout << "Usage: cts [--help] [--version] [--list|--list-cases] [--isolate] [--crash-list <file>] [--emit-crash-list <file>] [--run-case <case>] [--expectations <file>] <query>...\n"
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
    }

    if (argc > 1) {
        cts::RunOptions options;
        options.executablePath = argv[0];
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--list") {
                options.list = true;
            } else if (arg == "--list-cases") {
                options.listCases = true;
            } else if (arg == "--isolate") {
                options.isolate = true;
            } else if (arg == "--run-case") {
                if (i + 1 >= argc) {
                    std::cerr << "missing value for --run-case\n";
                    return EXIT_FAILURE;
                }
                options.runCaseQuery = argv[++i];
            } else if (arg == "--expectations") {
                if (i + 1 >= argc) {
                    std::cerr << "missing value for --expectations\n";
                    return EXIT_FAILURE;
                }
                options.expectationsPath = argv[++i];
            } else if (arg == "--crash-list") {
                if (i + 1 >= argc) {
                    std::cerr << "missing value for --crash-list\n";
                    return EXIT_FAILURE;
                }
                options.crashListPath = argv[++i];
            } else if (arg == "--emit-crash-list") {
                if (i + 1 >= argc) {
                    std::cerr << "missing value for --emit-crash-list\n";
                    return EXIT_FAILURE;
                }
                options.emitCrashListPath = argv[++i];
            } else if (arg == "--yawgpu-backend" || arg == "--future-timeout-ms") {
                if (i + 1 >= argc) {
                    std::cerr << "missing value for " << arg << "\n";
                    return EXIT_FAILURE;
                }
                options.forwardedArgs.push_back(arg);
                options.forwardedArgs.push_back(argv[++i]);
            } else if (arg.starts_with("--")) {
                std::cerr << "unknown option: " << arg << "\n";
                printUsage();
                return EXIT_FAILURE;
            } else {
                options.queries.push_back(arg);
            }
        }
        if (options.runCaseQuery.empty() && options.queries.empty()) {
            std::cerr << "missing query\n";
            return EXIT_FAILURE;
        }
        return cts::runQueries(options);
    }
    return runAdapterEnumeration();
}
