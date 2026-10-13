#pragma once

#include <wormedit/core/command.h>

#include <cstddef>
#include <cstdint>
#include <variant>

namespace wormedit {

struct timing_anchor_edit_limits_t {
    std::size_t maximum_timing_anchors{100'000};
    std::size_t maximum_chart_objects{default_maximum_chart_objects};
};

struct insert_timing_anchor_action_t {
    chart_id_t chart;
    local_position_t position;
    std::uint64_t source_sample_frame{};
    bool locked{false};
};

struct update_timing_anchor_action_t {
    chart_id_t chart;
    object_id_t anchor;
    local_position_t position;
    std::uint64_t source_sample_frame{};
    bool locked{false};
};

struct delete_timing_anchor_action_t {
    chart_id_t chart;
    object_id_t anchor;
};

using timing_anchor_edit_action_t =
    std::variant<insert_timing_anchor_action_t, update_timing_anchor_action_t,
                 delete_timing_anchor_action_t>;

// Mutates one timing anchor through shared document history. Exact local
// positions, source sample frames, and lock state are retained verbatim.
// Inserts allocate a fresh chart-wide object ID. Coincident positions and
// sample frames are legal; anchors have no timing-order semantics.
[[nodiscard]] auto apply_timing_anchor_edit(document_history_t& history,
                                            std::uint64_t expected_revision,
                                            const timing_anchor_edit_action_t& action,
                                            timing_anchor_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
