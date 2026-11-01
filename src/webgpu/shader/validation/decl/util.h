// SPDX-License-Identifier: BSD-3-Clause
// Ported from gpuweb/cts src/webgpu/shader/validation/decl/util.ts @ d116bcccd1833369fa3715a6df503c0dd9b0d7fa (see docs/UPSTREAM.md).
// Upstream CTS: Copyright (c) 2023 The WebGPU CTS Contributors, BSD-3-Clause.
// Port:         Copyright (c) 2026 Kota Iguchi, BSD-3-Clause.
//
// The address-space language-feature helpers of decl/util.ts, shared by several specs. The rest
// of util.ts is ported inline in decl/var.spec.cpp and types/pointer.spec.cpp. 'immediate' is the
// only address space with `wgslLanguageFeature` set in upstream's kAddressSpaceInfo.

#pragma once

#include <string>

#include "webgpu/shader/validation/shader_validation_test.h"

namespace cts::shader_validation {

inline std::string requiredLanguageFeatureHeader(const std::string& addressSpace) {
    return addressSpace == "immediate" ? "requires immediate_address_space;\n" : "";
}

inline void skipIfImmediateDataNotSupported(ShaderValidationTest& t) {
    t.skipIfLanguageFeatureNotSupported("immediate_address_space");
}

inline void skipIfAddressSpaceNotSupported(ShaderValidationTest& t,
                                           const std::string& addressSpace) {
    if (addressSpace == "immediate") {
        skipIfImmediateDataNotSupported(t);
    }
}

}  // namespace cts::shader_validation
