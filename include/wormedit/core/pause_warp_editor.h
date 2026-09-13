#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <variant>

namespace wormedit {

struct pause_warp_edit_limits_t {
    std::size_t maximum_pause_events{100'000};
    std::size_t maximum_warp_events{100'000};
    std::size_t maximum_chart_objects{1'000'000};
};

struct insert_pause_event_action_t {
    chart_id_t chart;
    local_position_t position;
    pause_kind_t kind{pause_kind_t::stop};
    signed_rational_t duration;
    pause_unit_t source_unit{pause_unit_t::seconds};
};

struct update_pause_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    pause_kind_t kind{pause_kind_t::stop};
    signed_rational_t duration;
    pause_unit_t source_unit{pause_unit_t::seconds};
};

struct delete_pause_event_action_t {
    chart_id_t chart;
    object_id_t event;
};

struct insert_warp_event_action_t {
    chart_id_t chart;
    position_span_t span;
    warp_provenance_t source_encoding{warp_provenance_t::native};
};

struct update_warp_event_action_t {
    chart_id_t chart;
    object_id_t event;
    position_span_t span;
    std::optional<warp_provenance_t> source_encoding;
};

struct delete_warp_event_action_t {
    chart_id_t chart;
    object_id_t event;
};

using pause_warp_edit_action_t =
    std::variant<insert_pause_event_action_t, update_pause_event_action_t,
                 delete_pause_event_action_t, insert_warp_event_action_t,
                 update_warp_event_action_t, delete_warp_event_action_t>;

// Mutates one pause or warp through shared document history. Positions and
// durations remain exact, same-row timing events are legal, and inserts use
// chart-wide object IDs with source ordering allocated independently per
// event family. BMS pause import baselines are preserved on update and removed
// with their event on delete.
[[nodiscard]] auto
apply_pause_warp_edit(document_history_t& history, std::uint64_t expected_revision,
                      const pause_warp_edit_action_t& action, pause_warp_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
