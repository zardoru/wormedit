#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/kiai_spans.h>

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
    std::size_t maximum_chart_objects{default_maximum_chart_objects};
};

struct bookmark_edit_value_t {
    std::string label;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<local_position_t> position;
    std::optional<std::int64_t> audio_milliseconds;
    auto operator==(const bookmark_edit_value_t&) const -> bool = default;
};

struct section_edit_value_t {
    std::string label;
    marker_anchor_t anchor{marker_anchor_t::beat};
    std::optional<position_span_t> beat_span;
    std::optional<std::pair<std::int64_t, std::int64_t>> audio_millisecond_span;
    auto operator==(const section_edit_value_t&) const -> bool = default;
};

struct timing_kiai_value_t {
    bool enabled{true};
    std::uint32_t retained_effect_bits{};

    [[nodiscard]] static auto with_defaults(chart_edit_value_default_policy_t policy)
        -> timing_kiai_value_t {
        timing_kiai_value_t value;
        if (policy == chart_edit_value_default_policy_t::command)
            value.enabled = false;
        return value;
    }

    [[nodiscard]] static auto from(const kiai_event_t& event) -> timing_kiai_value_t {
        return {.enabled = event.enabled, .retained_effect_bits = event.retained_effect_bits};
    }
    auto operator==(const timing_kiai_value_t&) const -> bool = default;
};

using kiai_edit_value_t = positioned_edit_value_t<timing_kiai_value_t>;

// Creates, resizes, moves or deletes one whole Kiai area, merging overlaps (see
// apply_kiai_span_edit). One undoable command.
struct edit_kiai_span_action_t {
    chart_id_t chart;
    kiai_span_edit_t edit;
};

using marker_edit_action_t =
    std::variant<wormedit::insert_edit_t<wormedit::bookmark_edit_value_t>,
                 wormedit::update_edit_t<wormedit::bookmark_edit_value_t>,
                 wormedit::delete_edit_t<wormedit::bookmark_edit_value_t>,
                 wormedit::insert_edit_t<wormedit::section_edit_value_t>,
                 wormedit::update_edit_t<wormedit::section_edit_value_t>,
                 wormedit::delete_edit_t<wormedit::section_edit_value_t>,
                 wormedit::insert_edit_t<wormedit::kiai_edit_value_t>,
                 wormedit::update_edit_t<wormedit::kiai_edit_value_t>,
                 wormedit::delete_edit_t<wormedit::kiai_edit_value_t>, edit_kiai_span_action_t>;

// Applies one marker, section, or kiai-state edit through shared document
// history. Anchor fields must agree exactly with their selected value. New
// kiai events are appended in deterministic source order; bookmarks/sections
// are appended to their authored suffix. Updates retain stable identity and
// source provenance (including kiai effect bits). A semantic no-op does not
// create a revision or history entry.
[[nodiscard]] auto apply_marker_edit(document_history_t& history,
                                     const marker_edit_action_t& action,
                                     marker_edit_limits_t limits = {}) -> document_command_result_t;

// Uses a ready command transaction supplied by a captured edit proposal.
[[nodiscard]] auto apply_marker_edit(document_command_transaction_t& transaction,
    const marker_edit_action_t& action, marker_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
