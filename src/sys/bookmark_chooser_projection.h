#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

struct bookmark_chooser_projection_t {
    chart_id_t chart;
    std::uint64_t document_revision{};
    std::size_t total_count{};
    std::size_t first_row{};
    std::size_t row_limit{};
    std::vector<bookmark_chooser_row_t> rows;
    std::string status;

    [[nodiscard]] explicit operator bool() const noexcept { return status.empty(); }
};

// Projects a globally ordered page; paging is applied only after every bookmark
// has been resolved and sorted. A zero row_limit is rejected.
[[nodiscard]] auto project_bookmark_chooser(const song_document_t& document,
                                            chart_id_t chart, std::size_t first_row,
                                            std::size_t row_limit)
    -> bookmark_chooser_projection_t;

} // namespace wormedit::sys
