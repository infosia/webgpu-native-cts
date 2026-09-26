#pragma once

// Backend-neutral wrappers for the SetImmediates entry points.
//
// yawgpu and Dawn declare the standard webgpu.h signature
//   wgpuXxxSetImmediates(encoder, uint32_t offset, void const* data, size_t size).
// wgpu-native does not declare them in its webgpu.h; its extension header
// <wgpu.h> declares them with a different parameter order
//   wgpuXxxSetImmediates(encoder, uint32_t offset, uint32_t sizeBytes, void const* data).
// Ported tests call these cts:: wrappers (standard argument order) instead.

#include <cstddef>
#include <cstdint>

#include "cts/webgpu.h"

#if defined(CTS_BACKEND_WGPU)
#include <wgpu.h>
#endif

namespace cts {

inline void computePassSetImmediates(WGPUComputePassEncoder e, uint32_t offset, const void* data,
                                     size_t size) {
#if defined(CTS_BACKEND_WGPU)
    wgpuComputePassEncoderSetImmediates(e, offset, static_cast<uint32_t>(size), data);
#else
    wgpuComputePassEncoderSetImmediates(e, offset, data, size);
#endif
}

inline void renderPassSetImmediates(WGPURenderPassEncoder e, uint32_t offset, const void* data,
                                    size_t size) {
#if defined(CTS_BACKEND_WGPU)
    wgpuRenderPassEncoderSetImmediates(e, offset, static_cast<uint32_t>(size), data);
#else
    wgpuRenderPassEncoderSetImmediates(e, offset, data, size);
#endif
}

inline void renderBundleSetImmediates(WGPURenderBundleEncoder e, uint32_t offset, const void* data,
                                      size_t size) {
#if defined(CTS_BACKEND_WGPU)
    wgpuRenderBundleEncoderSetImmediates(e, offset, static_cast<uint32_t>(size), data);
#else
    wgpuRenderBundleEncoderSetImmediates(e, offset, data, size);
#endif
}

}  // namespace cts
