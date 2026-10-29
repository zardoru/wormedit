#pragma once

#include <wormedit/core/vocabulary.h>

#include <cstdint>

namespace wormedit {

// A row within a stable chart measure identity.
struct local_position_t {
    measure_id_t measure;
    std::uint64_t row{};
    auto operator==(const local_position_t&) const -> bool = default;
};

struct position_span_t {
    local_position_t begin;
    local_position_t end;
    auto operator==(const position_span_t&) const -> bool = default;
};

} // namespace wormedit
