#pragma once

// Mirrors upstream t.hasLanguageFeature() / t.skipIfLanguageFeatureNotSupported(). WGSL language
// features are an instance-level property and test bodies do not see the harness's shared
// instance, so a one-shot probe instance answers the query (once per feature and process).
//
// A feature whose enumerator is missing from a backend's webgpu.h is reported as unsupported there.

#include <map>

#include "common/webgpu/backend.h"
#include "cts/webgpu.h"

namespace cts {

inline bool hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName feature) {
    static std::map<WGPUWGSLLanguageFeatureName, bool> cache;
    const auto cached = cache.find(feature);
    if (cached != cache.end()) {
        return cached->second;
    }
    bool supported = false;
    WGPUInstance probeInstance = createInstance();
    if (probeInstance != nullptr) {
        supported = wgpuInstanceHasWGSLLanguageFeature(probeInstance, feature) != WGPU_FALSE;
        wgpuInstanceRelease(probeInstance);
    }
    cache.emplace(feature, supported);
    return supported;
}

/// The 'pointer_composite_access' language feature.
inline bool hasPointerCompositeAccessLanguageFeature() {
    return hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_PointerCompositeAccess);
}

/// The 'swizzle_assignment' language feature. Not in wgpu-native's webgpu.h.
inline bool hasSwizzleAssignmentLanguageFeature() {
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
    return hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_SwizzleAssignment);
#else
    return false;
#endif
}

/// The 'buffer_view' language feature. Not in wgpu-native's webgpu.h.
inline bool hasBufferViewLanguageFeature() {
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
    return hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_BufferView);
#else
    return false;
#endif
}

/// The 'fragment_depth' language feature. Not in wgpu-native's webgpu.h.
inline bool hasFragmentDepthLanguageFeature() {
#if defined(CTS_BACKEND_DAWN) || defined(CTS_BACKEND_YAWGPU)
    return hasWGSLLanguageFeature(WGPUWGSLLanguageFeatureName_FragmentDepth);
#else
    return false;
#endif
}

} // namespace cts
