#pragma once

#include <wormedit/core/document.h>
#include <wormedit/core/operation_state.h>
#include <wormedit/core/result.h>
#include <wormedit/core/timing.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

namespace wormedit {

enum class marker_navigation_direction_t : std::uint8_t {
    previous,
    next,
};

enum class marker_navigation_kind_t : std::uint8_t {
    bookmark,
    section_begin,
    section_end,
};

enum class marker_navigation_phase_t : std::uint8_t { found, no_result };
static_assert(sizeof(operation_state_t<marker_navigation_phase_t>) <= 2 * sizeof(std::uint8_t));
static_assert(std::is_trivially_copyable_v<operation_state_t<marker_navigation_phase_t>>);

struct marker_navigation_filter_t {
    bool include_bookmarks{true};
    bool include_section_begins{true};
    bool include_section_ends{true};
};

struct marker_navigation_limits_t {
    std::size_t maximum_marker_objects{200'000};
};

struct marker_navigation_item_t {
    object_id_t object;
    marker_navigation_kind_t kind{marker_navigation_kind_t::bookmark};
    // The stable chart-wide ID is used as the source-order tie breaker because
    // bookmarks and sections do not carry a separate provenance order.
    std::uint64_t source_order{};
    std::optional<local_position_t> beat_position;
    std::optional<std::int64_t> audio_milliseconds;
    auto operator==(const marker_navigation_item_t&) const -> bool = default;
};

// Beat cursors and beat markers are compared as exact absolute editor rows
// through the selected chart's measure index, so variable measure lengths are
// respected. Navigation is strict: previous/next excludes an item exactly at
// the cursor. At equal positions, stable chart-wide object IDs determine order;
// next returns the lowest ID and previous the highest ID.
//
// When a timing index is passed, audio-time bookmarks and section boundaries are
// also candidates: each millisecond value is converted to an absolute row by
// inverse timing lookup (warp side = entry) and floored to the row that contains
// it; the item then carries both beat_position and audio_milliseconds. A marker
// outside the timing range is skipped and reported as a warning diagnostic, and
// an unusable timing index falls back to beat markers with one warning. The
// result keeps status found; callers choose whether warning diagnostics permit navigation.
// With no timing index, behavior is unchanged and audio markers are ignored.
struct marker_navigation_match_t {
    [[nodiscard]] static auto
    from(const song_document_t& song, std::uint64_t expected_revision, chart_id_t chart,
         local_position_t beat_cursor, marker_navigation_direction_t direction,
         marker_navigation_filter_t filter = {}, marker_navigation_limits_t limits = {},
         const timing_index_t* timing = nullptr) -> diagnosed_t<marker_navigation_match_t>;
    [[nodiscard]] static auto
    from(const song_document_t& song, std::uint64_t expected_revision, chart_id_t chart,
         std::int64_t audio_millisecond_cursor, marker_navigation_direction_t direction,
         marker_navigation_filter_t filter = {}, marker_navigation_limits_t limits = {})
        -> diagnosed_t<marker_navigation_match_t>;
    operation_state_t<marker_navigation_phase_t> status{marker_navigation_phase_t::no_result};
    std::uint64_t document_revision{};
    chart_id_t chart;
    std::optional<marker_navigation_item_t> item;
    [[nodiscard]] auto is_found() const noexcept -> bool {
        return status.is_in_phase(marker_navigation_phase_t::found) && item.has_value();
    }
    auto operator==(const marker_navigation_match_t&) const -> bool = default;
};
using marker_navigation_outcome_t = diagnosed_t<marker_navigation_match_t>;

// True when the chart has a bookmark or section available for navigation.
[[nodiscard]] auto has_marker_navigation_targets(const chart_document_t& chart) -> bool;

enum class section_selection_phase_t : std::uint8_t { resolved, timing_projection_required };
static_assert(sizeof(operation_state_t<section_selection_phase_t>) <= 2 * sizeof(std::uint8_t));
static_assert(std::is_trivially_copyable_v<operation_state_t<section_selection_phase_t>>);

// Resolves beat sections to their authored selection spans. Audio-time sections require
// timing projection by the caller.
struct section_selection_t {
    [[nodiscard]] static auto from(const song_document_t& song, std::uint64_t expected_revision,
                                   chart_id_t chart, object_id_t section,
                                   marker_navigation_limits_t limits = {})
        -> diagnosed_t<section_selection_t>;
    operation_state_t<section_selection_phase_t> status{failure_code_t::invalid_request};
    std::uint64_t document_revision{};
    chart_id_t chart;
    object_id_t section;
    std::optional<position_span_t> beat_span;
    [[nodiscard]] auto is_resolved() const noexcept -> bool {
        return status.is_in_phase(section_selection_phase_t::resolved) && beat_span.has_value();
    }
    auto operator==(const section_selection_t&) const -> bool = default;
};
using section_selection_outcome_t = diagnosed_t<section_selection_t>;

} // namespace wormedit
