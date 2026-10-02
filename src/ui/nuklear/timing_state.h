#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>

struct worm_ui_nuklear_timing_state_t {
    bool stage_buffers_active{};
    std::uint64_t stage_buffers_chart{};
    std::uint64_t stage_buffers_revision{};
    std::uint64_t stage_buffers_object{};
    WormUiTimingLaneKind stage_buffers_lane{WORM_UI_TIMING_LANE_BPM};
    double stage_buffers_y{};
    std::array<char, 64> bpm_decimal{};
    bool bpm_decimal_edited{};
    bool bpm_advanced_fraction{};
    std::array<char, 64> primary_numerator{};
    std::array<char, 64> primary_denominator{};
    std::array<char, 64> view_primary_numerator{};
    std::array<char, 64> view_primary_denominator{};
    std::array<char, 64> secondary_numerator{};
    std::array<char, 64> secondary_denominator{};
};

[[nodiscard]] auto worm_ui_nuklear_timing_state() noexcept -> worm_ui_nuklear_timing_state_t&;

void worm_ui_nuklear_reset_timing_state();
