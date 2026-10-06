#include "ui/nuklear/compose_measure_resize_input.h"

#include "ui/nuklear/measure_resize.h"

#include <SDL3/SDL_keyboard.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>

namespace {
constexpr float hit_band_screen_pixels = 6.0f;

struct line_hit_t {
    std::uint64_t measure_id{};
    std::size_t row{};
    double y_pixels{};
    double direction{1.0};
};

auto find_line(const WormUiComposeView& view, std::uint32_t number) -> const WormUiComposeGridLineView* {
    const auto lines = std::span(view.grid_lines, view.grid_line_count);
    const auto found = std::ranges::find_if(lines, [number](const auto& line) {
        return line.measure_number == number;
    });
    return found == lines.end() ? nullptr : &*found;
}

// Which way time grows on screen, read off two neighboring bar lines.
auto line_direction(const WormUiComposeView& view, const WormUiComposeGridLineView& end_line)
    -> std::optional<double> {
    if (const auto* before = find_line(view, end_line.measure_number - 1))
        return end_line.y_pixels >= before->y_pixels ? 1.0 : -1.0;
    if (const auto* after = find_line(view, end_line.measure_number + 1))
        return after->y_pixels >= end_line.y_pixels ? 1.0 : -1.0;
    return std::nullopt;
}

// The measure whose end line lies under the press, if any: end lines are numbered measure + 2.
auto bar_line_at(const ComposeBeatFitInput& input) -> std::optional<line_hit_t> {
    const auto& view = *input.view;
    const auto band = hit_band_screen_pixels * view.viewport_height_pixels / input.viewport.h;
    std::optional<line_hit_t> best;
    double best_distance = band;
    for (const auto& line : std::span(view.grid_lines, view.grid_line_count)) {
        if (line.measure_number < 2 || line.measure_number - 2 >= view.measure_row_count) continue;
        const auto distance = std::abs(line.y_pixels - input.pointer_started_y);
        if (distance > best_distance) continue;
        const auto direction = line_direction(view, line);
        if (!direction) continue;
        const auto row = static_cast<std::size_t>(line.measure_number - 2);
        best = line_hit_t{view.measure_rows[row].measure_id, row, line.y_pixels, *direction};
        best_distance = distance;
    }
    return best;
}

auto start_allowed(const ComposeBeatFitInput& input) -> bool {
    const auto& view = *input.view;
    constexpr std::uint16_t other_modifiers = SDL_KMOD_CTRL | SDL_KMOD_SHIFT | SDL_KMOD_GUI;
    return view.scene_available && view.measure_row_count != 0 && view.pixels_per_standard_measure > 0.0 &&
           input.window_focused && input.pointer_started_in_view && input.left_down &&
           input.shortcut_event_count == 0 && (input.pointer_modifiers & SDL_KMOD_ALT) != 0 &&
           (input.pointer_modifiers & other_modifiers) == 0 && !state.measure_resize_blocked_until_release;
}

void begin(const ComposeBeatFitInput& input, const line_hit_t& hit) {
    const auto& view = *input.view;
    const auto& row = view.measure_rows[hit.row];
    state.measure_resize_active = true;
    state.measure_resize_valid = false;
    state.measure_resize_measure_id = hit.measure_id;
    state.measure_resize_chart_id = view.chart_id;
    state.measure_resize_revision = view.document_revision;
    state.measure_resize_old_numerator = row.length_numerator;
    state.measure_resize_old_denominator = row.length_denominator;
    state.measure_resize_anchor_y_pixels = input.pointer_started_y;
    state.measure_resize_line_y_pixels = hit.y_pixels;
    state.measure_resize_direction = hit.direction;
    state.measure_resize_ghost_y_pixels = hit.y_pixels;
    state.measure_resize_anchor_screen_y = input.context->input.mouse.buttons[NK_BUTTON_LEFT].clicked_pos.y;
    state.measure_resize_label[0] = '\0';
}

void preview(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    const auto delta = state.measure_resize_direction *
                       (std::clamp(input.pointer_y, 0.0, view.viewport_height_pixels) -
                        state.measure_resize_anchor_y_pixels) / view.pixels_per_standard_measure;
    const auto result = worm_ui_resize_measure(state.measure_resize_old_numerator,
                                               state.measure_resize_old_denominator, delta,
                                               view.edit_snap_enabled ? view.area_snap_denominator
                                                                      : view.area_rows_per_measure);
    const auto old_length = static_cast<double>(state.measure_resize_old_numerator) /
                            static_cast<double>(state.measure_resize_old_denominator);
    state.measure_resize_valid = result.valid;
    state.measure_resize_numerator = result.numerator;
    state.measure_resize_denominator = result.denominator;
    state.measure_resize_ghost_y_pixels =
        state.measure_resize_line_y_pixels +
        state.measure_resize_direction * (result.length_whole_notes - old_length) *
            view.pixels_per_standard_measure;
    std::snprintf(state.measure_resize_label.data(), state.measure_resize_label.size(), "%s",
                  result.label.c_str());
}

void commit(const ComposeBeatFitInput& input) {
    if (!state.measure_resize_valid || input.action->compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    const auto& view = *input.view;
    const auto unchanged = static_cast<std::uint64_t>(state.measure_resize_numerator) *
                                   state.measure_resize_old_denominator ==
                               state.measure_resize_old_numerator * state.measure_resize_denominator;
    if (unchanged) return;
    state.measures_staged_ids.assign(1, state.measure_resize_measure_id);
    auto& action = *input.action;
    action.compose_action = WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER;
    action.compose_input_chart_id = view.chart_id;
    action.compose_input_document_revision = view.document_revision;
    action.compose_measure_ids = state.measures_staged_ids.data();
    action.compose_measure_id_count = state.measures_staged_ids.size();
    std::snprintf(action.timing_meter_numerator_text, sizeof(action.timing_meter_numerator_text), "%u",
                  state.measure_resize_numerator);
    std::snprintf(action.timing_meter_denominator_text, sizeof(action.timing_meter_denominator_text), "%u",
                  state.measure_resize_denominator);
}

void update_active(const ComposeBeatFitInput& input) {
    const auto& view = *input.view;
    const auto matches = input.window_focused && view.scene_available &&
                         view.chart_id == state.measure_resize_chart_id &&
                         view.document_revision == state.measure_resize_revision &&
                         view.pixels_per_standard_measure > 0.0;
    if (!matches || (!input.left_down && !input.left_released)) {
        cancel_compose_measure_resize();
        return;
    }
    preview(input);
    if (!input.left_released) return;
    const auto moved = std::abs(input.context->input.mouse.pos.y - state.measure_resize_anchor_screen_y) >= 4.0f;
    if (moved) commit(input);
    state.measure_resize_active = false;
}
} // namespace

void cancel_compose_measure_resize() {
    if (!state.measure_resize_active) return;
    state.measure_resize_active = false;
    state.measure_resize_blocked_until_release = true;
}

bool process_compose_measure_resize_pointer(const ComposeBeatFitInput& input) {
    if (!input.left_down) state.measure_resize_blocked_until_release = false;
    if (state.measure_resize_active) {
        update_active(input);
        return true;
    }
    if (!start_allowed(input)) return false;
    const auto hit = bar_line_at(input);
    if (!hit) return false;
    begin(input, *hit);
    return true;
}
