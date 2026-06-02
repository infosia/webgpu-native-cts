#include <cstdint>
#include <optional>
#include <string_view>

#include "cts/format_sample.h"
#include "webgpu/texture_format.h"

namespace {

const bool kRegistered = [] {
    cts::setFormatSampleHook([](std::string_view key, int64_t value) -> std::optional<bool> {
        if (key == "format" || key == "textureFormat" || key == "viewFormat") {
            return cts::isRepresentativeTextureFormat(static_cast<WGPUTextureFormat>(value));
        }
        return std::nullopt;
    });
    return true;
}();

} // namespace
