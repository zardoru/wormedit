#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace wormedit {

struct marker_edit_limits_t {
    std::size_t maximum_bookmarks{100'000};
    std::size_t maximum_sections{100'000};
    std::size_t maximum_kiai_events{100'000};
    std::size_t maximum_label_bytes{4U * 1024U};
    std::size_t maximum_total_label_bytes{1U * 1024U * 1024U};
    std::size_t maximum_chart_objects{1'000'000};
};

struct insert_bookmark_action_t {
    chart_id_t chart;
    std::string label;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<local_position_t> position;
    std::optional<std::int64_t> audio_milliseconds;
};

struct update_bookmark_action_t {
    chart_id_t chart;
    object_id_t bookmark;
    std::string label;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<local_position_t> position;
    std::optional<std::int64_t> audio_milliseconds;
};

struct delete_bookmark_action_t {
    chart_id_t chart;
    object_id_t bookmark;
};

struct insert_section_action_t {
    chart_id_t chart;
    std::string label;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<position_span_t> beat_span;
    std::optional<std::pair<std::int64_t, std::int64_t>> audio_millisecond_span;
};

struct update_section_action_t {
    chart_id_t chart;
    object_id_t section;
    std::string label;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<position_span_t> beat_span;
    std::optional<std::pair<std::int64_t, std::int64_t>> audio_millisecond_span;
};

struct delete_section_action_t {
    chart_id_t chart;
    object_id_t section;
};

struct insert_kiai_event_action_t {
    chart_id_t chart;
    local_position_t position;
    bool enabled{};
    std::uint32_t retained_effect_bits{};
};

struct update_kiai_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    bool enabled{};
};

struct delete_kiai_event_action_t {
    chart_id_t chart;
    object_id_t event;
};

using marker_edit_action_t =
    std::variant<insert_bookmark_action_t, update_bookmark_action_t, delete_bookmark_action_t,
                 insert_section_action_t, update_section_action_t, delete_section_action_t,
                 insert_kiai_event_action_t, update_kiai_event_action_t,
                 delete_kiai_event_action_t>;

// Applies one marker, section, or kiai-state edit through shared document
// history. Anchor fields must agree exactly with their selected value. New
// kiai events are appended in deterministic source order; bookmarks/sections
// are appended to their authored suffix. Updates retain stable identity and
// source provenance (including kiai effect bits). A semantic no-op does not
// create a revision or history entry.
[[nodiscard]] auto apply_marker_edit(document_history_t& history, std::uint64_t expected_revision,
                                     const marker_edit_action_t& action,
                                     marker_edit_limits_t limits = {}) -> document_command_result_t;

} // namespace wormedit
