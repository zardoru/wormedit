#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::sys {

struct bookmark_chooser_row_t {
    object_id_t bookmark_id;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<local_position_t> beat_position;
    std::optional<std::int64_t> audio_milliseconds;
    std::string position_label;
    std::string name;
    std::string display_label;
    bool navigation_available{};
};

struct bookmark_chooser_view_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    std::size_t total_count{};
    std::size_t first_row{};
    std::size_t row_limit{};
    std::vector<bookmark_chooser_row_t> rows;
    std::string status;

    [[nodiscard]] static auto from(const song_document_t&, chart_id_t,
                                   std::size_t first_row, std::size_t row_limit, std::string_view query = {}) -> result_t<bookmark_chooser_view_t, bookmark_chooser_view_t>;
};
using bookmark_chooser_view_outcome_t =
    result_t<bookmark_chooser_view_t, bookmark_chooser_view_t>;

} // namespace wormedit::sys
