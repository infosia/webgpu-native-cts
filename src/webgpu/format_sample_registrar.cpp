#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "cts/format_sample.h"
#include "cts/test.h"
#include "webgpu/texture_format.h"

namespace {

const bool kRegistered = [] {
    cts::setFormatSampleHook([](std::string_view key, const cts::Value& value) -> std::optional<bool> {
        if (key == "format" || key == "textureFormat" || key == "viewFormat"
            || key == "srcFormat" || key == "dstFormat") {
            if (const auto* identifier = std::get_if<std::string>(&value.data())) {
                return cts::isRepresentativeTextureFormat(cts::parseTextureFormat(*identifier));
            }
            if (const auto* numeric = std::get_if<int64_t>(&value.data())) {
                return cts::isRepresentativeTextureFormat(static_cast<WGPUTextureFormat>(*numeric));
            }
        }
        return std::nullopt;
    });
    return true;
}();

} // namespace
