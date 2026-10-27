#pragma once

#include <cstdint>
#include <wormedit/core/vocabulary.h>

namespace wormedit::ui {

// The Compose canvas serves two modes. Every difference between them lives in
// the profile table below; call sites ask a named predicate and never compare
// the mode directly, so the specialization stays bounded and testable.
enum class compose_mode_t : std::uint8_t {
    full,
    timing_focus,
};

struct compose_mode_profile_t {
    // Notes keep drawing in every mode; focus mode only mutes them.
    bool notes_dimmed;
    // Placing, painting, and resource-assigning notes with the pointer or lane keys.
    bool notes_editable;
    bool note_palette_visible;
    // Ordinary timing lanes stay visible; optional sample columns keep their visibility choices.
    bool timing_lanes_forced_visible;
    // The inspector column; timing focus works from the canvas and toolbar alone.
    bool inspector_available;
    // Gestures on a timing lane.
    bool ctrl_click_inserts_matching_bpm;
    bool alt_click_inserts_anchor;
    bool export_cuts_visible;
};

inline constexpr compose_mode_profile_t compose_mode_full_profile{
    .notes_dimmed = false,
    .notes_editable = true,
    .note_palette_visible = true,
    .timing_lanes_forced_visible = false,
    .inspector_available = true,
    .ctrl_click_inserts_matching_bpm = false,
    .alt_click_inserts_anchor = false,
    .export_cuts_visible = false,
};

inline constexpr compose_mode_profile_t compose_mode_timing_focus_profile{
    .notes_dimmed = true,
    .notes_editable = false,
    .note_palette_visible = false,
    .timing_lanes_forced_visible = true,
    .inspector_available = false,
    .ctrl_click_inserts_matching_bpm = true,
    .alt_click_inserts_anchor = true,
    .export_cuts_visible = true,
};

[[nodiscard]] constexpr auto compose_mode_profile(compose_mode_t mode) noexcept
    -> const compose_mode_profile_t& {
    return mode == compose_mode_t::timing_focus ? compose_mode_timing_focus_profile
                                                : compose_mode_full_profile;
}

[[nodiscard]] constexpr auto compose_notes_dimmed(compose_mode_t mode) noexcept -> bool {
    return compose_mode_profile(mode).notes_dimmed;
}
[[nodiscard]] constexpr auto compose_notes_editable(compose_mode_t mode) noexcept -> bool {
    return compose_mode_profile(mode).notes_editable;
}
[[nodiscard]] constexpr auto compose_note_palette_visible(compose_mode_t mode) noexcept -> bool {
    return compose_mode_profile(mode).note_palette_visible;
}
[[nodiscard]] constexpr auto compose_timing_lanes_forced_visible(compose_mode_t mode) noexcept
    -> bool {
    return compose_mode_profile(mode).timing_lanes_forced_visible;
}
[[nodiscard]] constexpr auto compose_inspector_available(compose_mode_t mode) noexcept -> bool {
    return compose_mode_profile(mode).inspector_available;
}
[[nodiscard]] constexpr auto compose_ctrl_click_inserts_matching_bpm(compose_mode_t mode) noexcept
    -> bool {
    return compose_mode_profile(mode).ctrl_click_inserts_matching_bpm;
}
[[nodiscard]] constexpr auto compose_alt_click_inserts_anchor(compose_mode_t mode) noexcept
    -> bool {
    return compose_mode_profile(mode).alt_click_inserts_anchor;
}

[[nodiscard]] constexpr auto compose_export_cuts_visible(compose_mode_t mode) noexcept -> bool {
    return compose_mode_profile(mode).export_cuts_visible;
}

// Lane mask in force for drawing and hit testing.
[[nodiscard]] constexpr auto compose_effective_timing_lane_mask(compose_mode_t mode,
                                                                std::uint16_t user_mask) noexcept
    -> std::uint16_t {
    return compose_timing_lanes_forced_visible(mode) ? (user_mask | wormedit::default_timing_lane_mask) : user_mask;
}

} // namespace wormedit::ui
