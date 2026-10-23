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
    std::size_t maximum_chart_objects{default_maximum_chart_objects};
};

struct timing_pause_value_t {
    signed_rational_t duration{};
    pause_unit_t source_unit{pause_unit_t::seconds};
    pause_kind_t kind{pause_kind_t::stop};

    [[nodiscard]] static auto from(const pause_event_t& event) -> timing_pause_value_t {
        return {.duration = event.duration, .source_unit = event.source_unit, .kind = event.kind};
    }
    auto operator==(const timing_pause_value_t&) const -> bool = default;
};

using pause_edit_value_t = positioned_edit_value_t<timing_pause_value_t>;
template<> struct chart_edit_native_collection_t<pause_edit_value_t> {
    static constexpr auto member = &chart_document_t::pauses;
};


struct timing_warp_value_t {
    local_position_t end;
    warp_provenance_t source_encoding{warp_provenance_t::native};
    // Existing timing previews preserve provenance; raw updates can select replacement.
    bool is_source_encoding_replaced{};

    [[nodiscard]] static auto from(const warp_event_t& event) -> timing_warp_value_t {
        return {.end = event.span.end, .source_encoding = event.source_encoding};
    }
    [[nodiscard]] static auto from(local_position_t end, std::optional<warp_provenance_t> encoding)
        -> timing_warp_value_t {
        return {.end = end,
                .source_encoding = encoding.value_or(warp_provenance_t::native),
                .is_source_encoding_replaced = encoding.has_value()};
    }
    auto operator==(const timing_warp_value_t&) const -> bool = default;
};

using warp_edit_value_t = positioned_edit_value_t<timing_warp_value_t>;
template<> struct chart_edit_native_collection_t<warp_edit_value_t> {
    static constexpr auto member = &chart_document_t::warps;
};


using pause_warp_edit_action_t = std::variant<wormedit::insert_edit_t<wormedit::pause_edit_value_t>,
                                              wormedit::update_edit_t<wormedit::pause_edit_value_t>,
                                              wormedit::delete_edit_t<wormedit::pause_edit_value_t>,
                                              wormedit::insert_edit_t<wormedit::warp_edit_value_t>,
                                              wormedit::update_edit_t<wormedit::warp_edit_value_t>,
                                              wormedit::delete_edit_t<wormedit::warp_edit_value_t>>;

// Mutates one pause or warp through shared document history. Positions and
// durations remain exact, same-row timing events are legal, and inserts use
// chart-wide object IDs with source ordering allocated independently per
// event family. BMS pause import baselines are preserved on update and removed
// with their event on delete.
[[nodiscard]] auto
apply_pause_warp_edit(document_history_t& history,
                      const pause_warp_edit_action_t& action, pause_warp_edit_limits_t limits = {})
    -> document_command_result_t;

// Uses a ready command transaction supplied by a captured edit proposal.
[[nodiscard]] auto apply_pause_warp_edit(document_command_transaction_t& transaction,
    const pause_warp_edit_action_t& action, pause_warp_edit_limits_t limits = {})
    -> document_command_result_t;

[[nodiscard]] auto stage_pause_warp_edit(document_command_transaction_t&,
    const pause_warp_edit_action_t&, pause_warp_edit_limits_t limits = {}) -> attempt_t<document_edit_effect_t>;
} // namespace wormedit
