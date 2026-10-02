#pragma once

#include <cstdint>

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
    bool notes_editable;
    bool note_palette_visible;
    // Timing lanes ignore the per-lane visibility mask and all draw.
    bool timing_lanes_forced_visible;
    // Inspector: Compose-only sections versus the timing-centered set.
    bool note_inspector_sections;
    bool timing_inspector_sections;
    // Gestures on a timing lane.
    bool ctrl_click_inserts_matching_bpm;
    bool alt_click_inserts_anchor;
};

inline constexpr compose_mode_profile_t compose_mode_full_profile{
    .notes_dimmed = false,
    .notes_editable = true,
    .note_palette_visible = true,
    .timing_lanes_forced_visible = false,
    .note_inspector_sections = true,
    .timing_inspector_sections = true,
    .ctrl_click_inserts_matching_bpm = false,
    .alt_click_inserts_anchor = false,
};

inline constexpr compose_mode_profile_t compose_mode_timing_focus_profile{
    .notes_dimmed = true,
    .notes_editable = false,
    .note_palette_visible = false,
    .timing_lanes_forced_visible = true,
    .note_inspector_sections = false,
    .timing_inspector_sections = true,
    .ctrl_click_inserts_matching_bpm = true,
    .alt_click_inserts_anchor = true,
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
[[nodiscard]] constexpr auto compose_note_inspector_sections(compose_mode_t mode) noexcept -> bool {
    return compose_mode_profile(mode).note_inspector_sections;
}
[[nodiscard]] constexpr auto compose_timing_inspector_sections(compose_mode_t mode) noexcept
    -> bool {
    return compose_mode_profile(mode).timing_inspector_sections;
}
[[nodiscard]] constexpr auto compose_ctrl_click_inserts_matching_bpm(compose_mode_t mode) noexcept
    -> bool {
    return compose_mode_profile(mode).ctrl_click_inserts_matching_bpm;
}
[[nodiscard]] constexpr auto compose_alt_click_inserts_anchor(compose_mode_t mode) noexcept
    -> bool {
    return compose_mode_profile(mode).alt_click_inserts_anchor;
}

// Lane mask in force for drawing and hit testing.
[[nodiscard]] constexpr auto compose_effective_timing_lane_mask(compose_mode_t mode,
                                                                std::uint8_t user_mask) noexcept
    -> std::uint8_t {
    return compose_timing_lanes_forced_visible(mode) ? std::uint8_t{0xff} : user_mask;
}

} // namespace wormedit::ui
