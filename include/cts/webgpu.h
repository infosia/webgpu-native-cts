#pragma once

// Backend-neutral include shim for the WebGPU C header: Dawn ships it as
// <webgpu/webgpu.h>, while the wgpu-native / yawgpu webgpu-headers layout uses
// <webgpu-headers/webgpu.h>. Include this instead of either path directly.

#if defined(CTS_BACKEND_DAWN)
#include <webgpu/webgpu.h>
#else
#include <webgpu-headers/webgpu.h>
#endif
