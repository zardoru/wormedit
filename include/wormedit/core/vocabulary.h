#pragma once

// Core vocabulary both sides of the frontend boundary may use (code-style.md, "Frontend
// boundary"): enums, strong IDs, rationals and small pure queries over them. Documents, history,
// indices and editor state stay out, so the frontend and C++ view types can include this alone.

#include <wormedit/core/id.h>

#include <cstddef>
#include <cstdint>
#include <optional>

namespace wormedit {

// Instrument color class; append only because native projects store the integer value.
enum class lane_key_color_t {
    generic, white, black, scratch, foot_pedal, free_zone,
    pms_white, pms_yellow, pms_green, pms_blue, pms_red,
};

struct signed_rational_t {
    std::int64_t numerator{};
    std::uint64_t denominator{1};

    auto operator==(const signed_rational_t&) const -> bool = default;
};

enum class note_kind_t {
    tap,
    hold,
    roll,
    invisible,
    mine,
    lift,
    fake,
};

enum class visual_event_kind_t {
    background,
    poor,
    layer,
    video,
    storyboard,
    layer2,
};

// How the user classifies a sound in the Sounds tab. Unspecified keeps the
// kind implied by the resource roles; it never affects exported files.
enum class sound_kind_t : std::uint8_t {
    unspecified,
    stem,
    keysound,
    sample,
};

// What moving a bar line does to the content after it. Bar-line edits are presentational by
// default, so everything keeps its song time; propagation is an explicit choice (open question 19).
enum class bar_line_time_t { keep_song_time, propagate };

enum class editor_selection_object_kind_t {
    note,
    bgm_event,
    bpm_event,
    stop_event,
    delay_event,
    warp_event,
    scroll_event,
    speed_event,
    kiai_event,
    timing_anchor,
    bga_event,
    bookmark,
    section,
    // The bar line that starts a measure, keyed by that measure's ID. The first measure has none.
    bar_line,
};

// Four editor lanes: background (including video), layer, layer 2, poor.
inline constexpr std::uint8_t editor_bga_lane_mask_all = 15;
[[nodiscard]] auto editor_bga_lane_index(visual_event_kind_t kind)
    -> std::optional<std::size_t>;

} // namespace wormedit
