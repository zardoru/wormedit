#pragma once

#include <wormedit/core/document.h>

#include <cstdint>
#include <optional>

namespace wormedit {

// A timing index resolves its profile once at construction. Native/editor
// charts use the canonical delay/event/stop vocabulary; imported BMSON 1.0
// charts retain the format's Note/BGA -> BPM -> STOP ordering at one pulse.
enum class timing_discontinuity_profile_t : std::uint8_t {
    canonical,
    bmson_1_0,
};

enum class preview_discontinuity_kind_t : std::uint8_t {
    delay,
    playable,
    bgm,
    bga,
    metronome,
    bpm,
    stop,
};

[[nodiscard]] auto valid_timing_discontinuity_profile(
    timing_discontinuity_profile_t profile) noexcept -> bool;

[[nodiscard]] auto resolve_timing_discontinuity_profile(
    format_family_t source_family,
    std::optional<timing_discontinuity_profile_t> requested = std::nullopt) noexcept
    -> timing_discontinuity_profile_t;

// Smaller values occur first when several events map to one sample frame.
[[nodiscard]] auto timing_discontinuity_order(timing_discontinuity_profile_t profile,
                                              preview_discontinuity_kind_t kind) noexcept
    -> std::uint8_t;

} // namespace wormedit
