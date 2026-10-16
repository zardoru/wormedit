#pragma once

#include <cstddef>
#include <optional>

namespace wormedit {

struct text_source_location_t {
    std::size_t source_order{};
    std::optional<std::size_t> byte_offset;

    auto operator==(const text_source_location_t&) const -> bool = default;
};

} // namespace wormedit
