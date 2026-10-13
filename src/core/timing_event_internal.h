#pragma once
#include <wormedit/core/document.h>
#include <algorithm>
#include <limits>
#include <optional>
#include <vector>

namespace wormedit {
inline auto valid_pause_unit(pause_unit_t unit) -> bool {
    switch (unit) {
    case pause_unit_t::seconds:
    case pause_unit_t::milliseconds:
    case pause_unit_t::bms_stop_ticks:
    case pause_unit_t::beats:
        return true;
    }
    return false;
}

inline auto valid_warp_encoding(warp_provenance_t encoding) -> bool {
    switch (encoding) {
    case warp_provenance_t::native:
    case warp_provenance_t::negative_bpm:
    case warp_provenance_t::negative_stop:
        return true;
    }
    return false;
}

template <typename event_t>
auto next_source_order(const std::vector<event_t>& events) -> std::optional<std::size_t> {
    if (events.empty()) {
        return 0;
    }
    auto maximum = std::size_t{};
    for (const auto& event : events) {
        maximum = std::max(maximum, event.provenance.source_order);
    }
    if (maximum == std::numeric_limits<std::size_t>::max()) {
        return std::nullopt;
    }
    return maximum + 1;
}

} // namespace wormedit
