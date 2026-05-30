#pragma once

#include "cts/webgpu.h"

namespace cts {

WGPUInstance createInstance();
const char* backendName();
bool backendSupportsTimeoutWaitAny();

} // namespace cts
