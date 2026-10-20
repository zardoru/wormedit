#pragma once

#include <wormedit/core/catalogs.h>
#include <wormedit/core/interaction_types.h>
#include <wormedit/core/timing_lane_controller.h>

namespace wormedit {

inline constexpr std::size_t editor_selection_object_kind_count = static_cast<std::size_t>(editor_selection_object_kind_t::bar_line) + 1;
struct timing_lane_descriptor_t {
    timing_lane_kind_t kind;
    std::string_view short_label;
    std::string_view label;
    editor_selection_object_kind_t selection_kind;
};
inline constexpr std::array timing_lanes{
    timing_lane_descriptor_t{timing_lane_kind_t::bpm, "BPM", "BPM · tempo", editor_selection_object_kind_t::bpm_event},
    timing_lane_descriptor_t{timing_lane_kind_t::scroll, "SV", "SV · scroll multiplier", editor_selection_object_kind_t::scroll_event},
    timing_lane_descriptor_t{timing_lane_kind_t::speed, "SPD", "SPEEDS · speed transitions", editor_selection_object_kind_t::speed_event},
    timing_lane_descriptor_t{timing_lane_kind_t::stop, "STP", "Stop · pause after notes", editor_selection_object_kind_t::stop_event},
    timing_lane_descriptor_t{timing_lane_kind_t::delay, "DLY", "Delay · pause before notes", editor_selection_object_kind_t::delay_event},
    timing_lane_descriptor_t{timing_lane_kind_t::warp, "WRP", "Warp · skip chart rows", editor_selection_object_kind_t::warp_event},
    timing_lane_descriptor_t{timing_lane_kind_t::kiai, "KIA", "Kiai · highlight sections", editor_selection_object_kind_t::kiai_event},
    timing_lane_descriptor_t{timing_lane_kind_t::anchor, "ANC", "Anchor · timing references", editor_selection_object_kind_t::timing_anchor},
};
static_assert(is_catalog_exhaustive(timing_lanes, timing_lane_kind_count));
static_assert(editor_selection_object_kind_count == timing_lane_kind_count + 6);

constexpr auto timing_lane_for_selection(editor_selection_object_kind_t kind) -> std::optional<timing_lane_kind_t> {
    using selection_t = editor_selection_object_kind_t;
    // Explicit non-timing cases ensure that new selection kinds require a decision.
    switch (kind) {
    case selection_t::note:
    case selection_t::bgm_event:
    case selection_t::bga_event:
    case selection_t::bookmark:
    case selection_t::section:
    case selection_t::bar_line: return std::nullopt;
    case selection_t::bpm_event:
    case selection_t::scroll_event:
    case selection_t::speed_event:
    case selection_t::stop_event:
    case selection_t::delay_event:
    case selection_t::warp_event:
    case selection_t::kiai_event:
    case selection_t::timing_anchor:
        for (const auto& lane : timing_lanes)
            if (lane.selection_kind == kind) return lane.kind;
        return std::nullopt;
    }
    return std::nullopt;
}
constexpr auto selection_kind_for_timing_lane(timing_lane_kind_t kind) -> std::optional<editor_selection_object_kind_t> {
    switch (kind) {
    case timing_lane_kind_t::bpm:
    case timing_lane_kind_t::scroll:
    case timing_lane_kind_t::speed:
    case timing_lane_kind_t::stop:
    case timing_lane_kind_t::delay:
    case timing_lane_kind_t::warp:
    case timing_lane_kind_t::kiai:
    case timing_lane_kind_t::anchor:
        return timing_lanes[static_cast<std::size_t>(kind)].selection_kind;
    }
    return std::nullopt;
}

} // namespace wormedit
