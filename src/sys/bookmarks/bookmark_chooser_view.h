#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit { struct document_history_t; struct workspace_view_t; }

namespace wormedit::sys {
struct compose_scene_cache_t;

struct bookmark_chooser_indices_t {
    const chart_indices_t* committed{};
    // An empty supplied optional is a retained failed build, not a request to retry.
    const std::optional<timing_index_t>* bounded_clock{};
    // Main-loop construction; only history may fill the committed snapshot's slot.
    [[nodiscard]] static auto from(const document_history_t& history, const workspace_view_t& workspace, bool is_open,
                                   const compose_scene_cache_t& cache) -> bookmark_chooser_indices_t;
};

struct bookmark_chooser_row_t {
    object_id_t bookmark_id;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<local_position_t> beat_position;
    std::optional<std::int64_t> audio_milliseconds;
    std::string position_label;
    std::string name;
    std::string display_label;
    object_id_t previous_bookmark{};
    object_id_t next_bookmark{};
    bool navigation_available{};
};

struct bookmark_chooser_view_t {
    [[nodiscard]] auto is_success() const noexcept -> bool { return status.empty(); }

    chart_id_t chart;
    std::uint64_t document_revision{};
    std::size_t total_count{};
    std::size_t first_row{};
    std::size_t row_limit{};
    std::vector<bookmark_chooser_row_t> rows;
    std::string status;

    [[nodiscard]] static auto from(const song_document_t&, chart_id_t,
                                   std::size_t first_row, std::size_t row_limit, std::string_view query = {},
                                   const bookmark_chooser_indices_t& indices = {},
                                   bookmark_sort_t sort = bookmark_sort_t::custom, bool descending = false) -> bookmark_chooser_view_t;
};

} // namespace wormedit::sys
