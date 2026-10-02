#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>

enum class worm_ui_nuklear_timing_recipe_range_drag_mode_t : std::uint8_t {
    new_range,
    begin_handle,
    end_handle,
    whole_span,
};

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
    std::array<char, 32> meter_measure{'1'};
    std::array<char, 32> meter_numerator{'4'};
    std::array<char, 32> meter_denominator{'4'};
    bool recipe_buffers_active{};
    std::uint64_t recipe_buffers_chart{};
    std::uint64_t recipe_buffers_revision{};
    std::uint64_t recipe_buffers_id{};
    WormUiTimingRecipeForm recipe_form{};
    bool recipe_range_drag_active{};
    worm_ui_nuklear_timing_recipe_range_drag_mode_t recipe_range_drag_mode{
        worm_ui_nuklear_timing_recipe_range_drag_mode_t::new_range};
    std::uint64_t recipe_drag_chart{};
    std::uint64_t recipe_drag_revision{};
    double recipe_drag_anchor_fraction{};
    double recipe_drag_current_fraction{};
    double recipe_drag_original_begin{};
    double recipe_drag_original_end{};
    float recipe_drag_anchor_screen_y{};
    bool recipe_drag_moved{};
    bool recipe_cancel_pending{};
    bool marker_drag_active{};
    bool marker_drag_moved{};
    bool marker_drag_cancel_pending{};
    std::uint64_t marker_drag_chart{};
    std::uint64_t marker_drag_revision{};
    std::uint64_t marker_drag_object{};
    WormUiTimingLaneKind marker_drag_lane{WORM_UI_TIMING_LANE_BPM};
    float marker_drag_anchor_screen_y{};
};

[[nodiscard]] auto worm_ui_nuklear_timing_state() noexcept -> worm_ui_nuklear_timing_state_t&;

void worm_ui_nuklear_reset_timing_state();

// Signature suggestions for a typed measure length (numerator/denominator text). Mirrors
// `wormedit::suggest_meters` (denominators 4, 8, 16 in order, exact lengths only) without
// crossing the core boundary; applying one never changes the length.
struct worm_ui_nuklear_meter_suggestion_t {
    std::uint32_t numerator{};
    std::uint32_t denominator{};
};

struct worm_ui_nuklear_meter_suggestions_t {
    std::array<worm_ui_nuklear_meter_suggestion_t, 3> items{};
    std::size_t count{};
};

[[nodiscard]] auto worm_ui_nuklear_suggest_meters(const char* numerator_text,
                                                  const char* denominator_text)
    -> worm_ui_nuklear_meter_suggestions_t;
