#pragma once

#include <string_view>

namespace wormedit {

// Case-insensitive natural ordering with a deterministic raw-byte tie-break.
[[nodiscard]] auto natural_name_less(std::string_view left, std::string_view right) noexcept
    -> bool;

} // namespace wormedit
