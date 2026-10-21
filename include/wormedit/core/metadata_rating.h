#pragma once

#include <wormedit/core/vocabulary.h>

#include <optional>
#include <string_view>

namespace wormedit {

// Exact base-10 signed 64-bit numerator and positive 64-bit denominator.
// Preserves the supplied rational without reducing it.
[[nodiscard]] auto parse_metadata_rating(std::string_view numerator, std::string_view denominator)
    -> std::optional<signed_rational_t>;

} // namespace wormedit
