#pragma once

#include "cts/webgpu.h"

namespace cts {

WGPUInstance createInstance();
const char* backendName();
const WGPURequestAdapterOptions* adapterOptions();

} // namespace cts
