#include "ui/nuklear/compose_measure_resize_input.h"

#include <wormedit/core/rational.h>

#include "ui/nuklear/measure_resize.h"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>

namespace {
constexpr float hit_band_screen_pixels = 6.0f;

struct line_hit_t {
    std::uint64_t measure_id{};
    std::size_t row{};
    double y_pixels{};
    double direction{1.0};
};

// Which way time grows on screen. Downscroll projects later rows higher, so y falls as time
// grows. Taken from the view rather than from a neighboring bar line, which a deep zoom can
// push off screen.
auto time_direction(const WormUiComposeView& view) -> double { return view.downscroll ? -1.0 : 1.0; }

// The measure whose end line lies under the press, if any: end lines are numbered measure + 2.
auto bar_line_at(const ComposeBeatFitInput& input, bool move_line) -> std::optional<line_hit_t> {
    const auto& view = *input.view;
    const auto band = hit_band_screen_pixels * view.viewport_height_pixels / input.viewport.h;
    std::optional<line_hit_t> best;
    double best_distance = band;
    for (const auto& line : std::span(view.grid_lines, view.grid_line_count)) {
        if (line.measure_number < 2 || line.measure_number - 2 >= view.measure_row_count) continue;
        const auto distance = std::abs(line.y_pixels - input.pointer_started_y);
        if (distance > best_distance) continue;
        // Moving only the line needs the next measure on the list as well.
        if (move_line && line.measure_number - 1 >= view.measure_row_count) continue;
        const auto row = static_cast<std::size_t>(line.measure_number - 2);
        best = line_hit_t{view.measure_rows[row].measure_id, row, line.y_pixels, time_direction(view)};
        best_distance = distance;
    }
    return best;
}

auto start_allowed(const ComposeBeatFitInput& input) -> bool {
    const auto& view = *input.view;
    constexpr std::uint16_t other_modifiers = SDL_KMOD_CTRL | SDL_KMOD_GUI;
    return view.scene_available && view.measure_row_count != 0 && view.pixels_per_standard_measure > 0.0 &&
           input.window_focused && input.pointer_started_in_view && input.left_down &&
           input.shortcut_event_count == 0 && (input.pointer_modifiers & SDL_KMOD_ALT) != 0 &&
           (input.pointer_modifiers & other_modifiers) == 0 && !state.measure_resize.measure_resize_blocked_until_release;
}

void begin(const ComposeBeatFitInput& input, const line_hit_t& hit, bool move_line) {
    const auto& view = *input.view;
    state.measure_resize.measure_resize_move_line = move_line;
    if (move_line) {
        state.measure_resize.measure_resize_next_numerator = view.measure_rows[hit.row + 1].length_numerator;
        state.measure_resize.measure_resize_next_denominator = view.measure_rows[hit.row + 1].length_denominator;
    }
    const auto& row = view.measure_rows[hit.row];
    state.measure_resize.measure_resize_active = true;
    state.measure_resize.measure_resize_valid = false;
    state.measure_resize.measure_resize_measure_id = hit.measure_id;
    state.measure_resize.measure_resize_chart_id = view.chart_id;
    state.measure_resize.measure_resize_revision = view.document_revision;
    state.measure_resize.measure_resize_old_numerator = row.length_numerator;
    state.measure_resize.measure_resize_old_denominator = row.length_denominator;
    state.measure_resize.measure_resize_anchor_y_pixels = input.pointer_started_y;
    state.measure_resize.measure_resize_line_y_pixels = hit.y_pixels;
    state.measure_resize.measure_resize_direction = hit.direction;
    state.measure_resize.measure_resize_ghost_y_pixels = hit.y_pixels;
    state.measure_resize.measure_resize_anchor_screen_y = input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y;
    state.measure_resize.measure_resize_label[0] = '\0';
}

void preview(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    const auto delta = state.measure_resize.measure_resize_direction *
                       (std::clamp(input.pointer_y, 0.0, view.viewport_height_pixels) -
                        state.measure_resize.measure_resize_anchor_y_pixels) / view.pixels_per_standard_measure;
    const auto snap = view.edit_snap_enabled ? view.area_snap_denominator : view.area_rows_per_measure;
    const auto old_length = wormedit::to_double(wormedit::rational_t{
        state.measure_resize.measure_resize_old_numerator, state.measure_resize.measure_resize_old_denominator});
    double new_length = 0.0;
    std::string label;
    if (state.measure_resize.measure_resize_move_line) {
        const auto moved = worm_ui_move_bar_line(
            state.measure_resize.measure_resize_old_numerator, state.measure_resize.measure_resize_old_denominator,
            state.measure_resize.measure_resize_next_numerator, state.measure_resize.measure_resize_next_denominator, delta, snap);
        state.measure_resize.measure_resize_valid = moved.valid;
        state.measure_resize.measure_resize_numerator = moved.first_numerator;
        state.measure_resize.measure_resize_denominator = moved.first_denominator;
        state.measure_resize.measure_resize_second_numerator = moved.second_numerator;
        state.measure_resize.measure_resize_second_denominator = moved.second_denominator;
        new_length = moved.first_length_whole_notes;
        label = moved.label;
    } else {
        const auto result = worm_ui_resize_measure(state.measure_resize.measure_resize_old_numerator,
                                                   state.measure_resize.measure_resize_old_denominator, delta, snap);
        state.measure_resize.measure_resize_valid = result.valid;
        state.measure_resize.measure_resize_numerator = result.numerator;
        state.measure_resize.measure_resize_denominator = result.denominator;
        new_length = result.length_whole_notes;
        label = result.label;
    }
    state.measure_resize.measure_resize_ghost_y_pixels =
        state.measure_resize.measure_resize_line_y_pixels +
        state.measure_resize.measure_resize_direction * (new_length - old_length) * view.pixels_per_standard_measure;
    std::snprintf(state.measure_resize.measure_resize_label.data(), state.measure_resize.measure_resize_label.size(), "%s",
                  label.c_str());
}

void commit(const ComposeBeatFitInput& input) {
    if (!state.measure_resize.measure_resize_valid || input.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    const auto& view = *input.view;
    const auto original = wormedit::reduce_fraction(wormedit::rational_t{
        state.measure_resize.measure_resize_old_numerator, state.measure_resize.measure_resize_old_denominator});
    const auto resized = wormedit::reduce_fraction(wormedit::rational_t{
        state.measure_resize.measure_resize_numerator, state.measure_resize.measure_resize_denominator});
    if (original == resized) return;
    state.measures_dock.measures_staged_ids.assign(1, state.measure_resize.measure_resize_measure_id);
    auto& action = *input.action;
    action.compose_action = state.measure_resize.measure_resize_move_line ? WORM_UI_COMPOSE_ACTION_MOVE_BAR_LINE
                                                           : WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER;
    action.compose_second_meter_numerator = state.measure_resize.measure_resize_second_numerator;
    action.compose_second_meter_denominator = state.measure_resize.measure_resize_second_denominator;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
    action.compose_measure_ids = state.measures_dock.measures_staged_ids.data();
    action.compose_measure_id_count = state.measures_dock.measures_staged_ids.size();
    std::snprintf(action.timing_meter_numerator_text, sizeof(action.timing_meter_numerator_text), "%u",
                  state.measure_resize.measure_resize_numerator);
    std::snprintf(action.timing_meter_denominator_text, sizeof(action.timing_meter_denominator_text), "%u",
                  state.measure_resize.measure_resize_denominator);
}

void update_active(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    const auto matches = input.window_focused && view.scene_available &&
                         view.chart_id == state.measure_resize.measure_resize_chart_id &&
                         view.document_revision == state.measure_resize.measure_resize_revision &&
                         view.pixels_per_standard_measure > 0.0;
    if (!matches || (!input.left_down && !input.left_released)) {
        cancel_compose_measure_resize();
        return;
    }
    preview(input);
    if (!input.left_released) return;
    const auto moved = std::abs(input.context->input.mouse.pos.y - state.measure_resize.measure_resize_anchor_screen_y) >= 4.0f;
    if (moved) commit(input);
    state.measure_resize.measure_resize_active = false;
}
} // namespace

void cancel_compose_measure_resize() {
    if (!state.measure_resize.measure_resize_active) return;
    state.measure_resize.measure_resize_active = false;
    state.measure_resize.measure_resize_blocked_until_release = true;
}

bool process_compose_measure_resize_pointer(const ComposeBeatFitInput& input) {
    if (!input.left_down) state.measure_resize.measure_resize_blocked_until_release = false;
    if (state.measure_resize.measure_resize_active) {
        update_active(input);
        return true;
    }
    if (!start_allowed(input)) return false;
    const auto move_line = (input.pointer_modifiers & SDL_KMOD_SHIFT) != 0;
    const auto hit = bar_line_at(input, move_line);
    if (!hit) return false;
    begin(input, *hit, move_line);
    return true;
}
