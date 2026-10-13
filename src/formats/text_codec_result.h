#pragma once

#include <wormedit/core/result.h>

#include <cstddef>
#include <string>

namespace wormedit {

// Both codec backends retain the first failing byte and distinguish an unavailable converter.
struct text_codec_error_t {
    std::size_t error_offset{};
    bool unsupported{};

    auto operator==(const text_codec_error_t&) const -> bool = default;
};

using text_codec_result_t = result_t<std::string, text_codec_error_t>;

[[nodiscard]] inline auto has_unsupported_codec_error(const text_codec_result_t& result) -> bool {
    return !result && result.error().unsupported;
}

} // namespace wormedit
