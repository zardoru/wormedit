#pragma once

#include <wormedit/core/document.h>

#include <cstddef>
#include <cstdint>
#include <optional>

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

enum class marker_navigation_status_t : std::uint8_t {
    found,
    no_result,
    invalid_request,
};

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
};

struct marker_navigation_result_t {
    marker_navigation_status_t status{marker_navigation_status_t::no_result};
    std::uint64_t document_revision{};
    chart_id_t chart;
    std::optional<marker_navigation_item_t> item;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == marker_navigation_status_t::found && item.has_value() &&
               diagnostics.empty();
    }
};

// Beat cursors and beat markers are compared as exact absolute editor rows
// through the selected chart's measure index, so variable measure lengths are
// respected. Navigation is strict: previous/next excludes an item exactly at
// the cursor. At equal positions, stable chart-wide object IDs determine order;
// next returns the lowest ID and previous the highest ID.
[[nodiscard]] auto
navigate_markers(const song_document_t& song, std::uint64_t expected_revision, chart_id_t chart,
                 local_position_t beat_cursor, marker_navigation_direction_t direction,
                 marker_navigation_filter_t filter = {}, marker_navigation_limits_t limits = {})
    -> marker_navigation_result_t;

// Audio-time cursors compare only audio-anchored bookmarks and section
// boundaries, using signed integer milliseconds without a timing approximation.
[[nodiscard]] auto
navigate_markers(const song_document_t& song, std::uint64_t expected_revision, chart_id_t chart,
                 std::int64_t audio_millisecond_cursor, marker_navigation_direction_t direction,
                 marker_navigation_filter_t filter = {}, marker_navigation_limits_t limits = {})
    -> marker_navigation_result_t;

enum class section_selection_status_t : std::uint8_t {
    resolved,
    timing_projection_required,
    invalid_request,
};

struct section_selection_result_t {
    section_selection_status_t status{section_selection_status_t::invalid_request};
    std::uint64_t document_revision{};
    chart_id_t chart;
    object_id_t section;
    std::optional<position_span_t> beat_span;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == section_selection_status_t::resolved && beat_span.has_value() &&
               diagnostics.empty();
    }
};

// Resolves a beat-anchored section to its exact authored selection span. An
// audio-time section returns timing_projection_required; converting its bounds
// to chart positions requires a timing projection supplied by the caller.
[[nodiscard]] auto resolve_section_selection(const song_document_t& song,
                                             std::uint64_t expected_revision, chart_id_t chart,
                                             object_id_t section,
                                             marker_navigation_limits_t limits = {})
    -> section_selection_result_t;

} // namespace wormedit
