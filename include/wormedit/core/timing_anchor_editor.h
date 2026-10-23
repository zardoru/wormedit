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

struct timing_anchor_value_t {
    std::uint64_t source_sample_frame{};
    bool locked{};

    [[nodiscard]] static auto from(const timing_anchor_t& event) -> timing_anchor_value_t {
        return {.source_sample_frame = event.source_sample_frame, .locked = event.locked};
    }
    auto operator==(const timing_anchor_value_t&) const -> bool = default;
};

using timing_anchor_edit_value_t = positioned_edit_value_t<timing_anchor_value_t>;
template<> struct chart_edit_native_collection_t<timing_anchor_edit_value_t> {
    static constexpr auto member = &chart_document_t::timing_anchors;
};


using timing_anchor_edit_action_t =
    std::variant<wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>,
                 wormedit::update_edit_t<wormedit::timing_anchor_edit_value_t>,
                 wormedit::delete_edit_t<wormedit::timing_anchor_edit_value_t>>;

// Mutates one timing anchor through shared document history. Exact local
// positions, source sample frames, and lock state are retained verbatim.
// Inserts allocate a fresh chart-wide object ID. Coincident positions and
// sample frames are legal; anchors have no timing-order semantics.
[[nodiscard]] auto apply_timing_anchor_edit(document_history_t& history,
                                            const timing_anchor_edit_action_t& action,
                                            timing_anchor_edit_limits_t limits = {})
    -> document_command_result_t;

// Uses a ready command transaction supplied by a captured edit proposal.
[[nodiscard]] auto apply_timing_anchor_edit(document_command_transaction_t& transaction,
    const timing_anchor_edit_action_t& action, timing_anchor_edit_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
