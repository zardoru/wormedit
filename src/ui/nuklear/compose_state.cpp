#include "ui/nuklear/compose_state.h"
#include "ui/nuklear/arrow_glyph.h"
#include "ui/nuklear/theme.h"

#include <algorithm>
#include <cmath>
#include <charconv>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <numeric>
#include <string_view>
#include <utility>

namespace {
template<class Note>
auto compose_layer_fill(const Note& note) -> nk_color {
    if (note.semantic_color_visible)
        return nk_rgba(note.semantic_red, note.semantic_green, note.semantic_blue,
                       note.semantic_alpha);
    return wormedit::ui::theme::compose(wormedit::ui::theme::ComposeColor::semantic_layer_none);
}
auto compose_snap_fill(std::uint8_t snap_class) -> nk_color {
    const auto color = wormedit::ui::arrow_snap_color(snap_class);
    return nk_rgb(color.red, color.green, color.blue);
}
}

auto compose_note_is_transient(const WormUiComposeView& view,
                               const WormUiComposeNoteView& note) -> bool {
    return note.proposal_ghost || note.proposal_origin || note.paint_preview ||
        note.paint_erase_preview || note.paint_invalid_preview ||
        (view.hover_note_visible && note.note_id == view.hover_note_id);
}

auto compose_arrow_layer_outline_visible(worm_ui_nuklear_compose_color_view_t color_view,
    const WormUiComposeView& view, const WormUiComposeNoteView& note) -> bool {
    return color_view == worm_ui_nuklear_compose_color_view_t::layer &&
           note.semantic_color_visible && !compose_note_is_transient(view, note);
}

auto compose_note_base_fill(worm_ui_nuklear_compose_color_view_t color_view,
                            const WormUiComposeNoteView& note) -> nk_color {
    if (color_view == worm_ui_nuklear_compose_color_view_t::layer)
        return compose_layer_fill(note);
    if (color_view == worm_ui_nuklear_compose_color_view_t::snap ||
        (note.glyph >= 2 && note.glyph <= 5)) return compose_snap_fill(note.snap_class);
    const auto key = wormedit::ui::theme::compose_key_color(note.key_color, false);
    return key.has_color ? key.note : nk_rgb(133, 182, 222);
}

auto compose_note_base_fill(worm_ui_nuklear_compose_color_view_t color_view,
                            const WormUiComposeHoldView& hold) -> nk_color {
    if (color_view == worm_ui_nuklear_compose_color_view_t::layer)
        return compose_layer_fill(hold);
    if (color_view == worm_ui_nuklear_compose_color_view_t::snap)
        return compose_snap_fill(hold.snap_class);
    const auto key = wormedit::ui::theme::compose_key_color(hold.key_color, false);
    if (!key.has_color) return nk_rgb(48, 111, 151);
    return nk_rgb(key.note.r * 3 / 4, key.note.g * 3 / 4, key.note.b * 3 / 4);
}

auto worm_ui_nuklear_normalize_compose_wheel(double x, double y, double mouse_x,
    double mouse_y, std::uint16_t modifiers, bool flipped)
    -> std::optional<worm_ui_nuklear_compose_wheel_event_t> {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(mouse_x) ||
        !std::isfinite(mouse_y)) return std::nullopt;
    const auto sign = flipped ? 1.0 : -1.0;
    return worm_ui_nuklear_compose_wheel_event_t{
        .delta_x = x * sign, .delta_y = y * sign,
        .mouse_x = mouse_x, .mouse_y = mouse_y, .modifiers = modifiers,
        .coarse_steps = true};
}

auto worm_ui_nuklear_compose_canvas_input_available(const WormUiComposeView* view,
    bool window_focused, bool text_owned, bool ime_owned) -> bool {
    return view && view->visible && view->scene_available && window_focused &&
        !view->input_occluded && !text_owned && !ime_owned;
}

auto worm_ui_nuklear_begin_shift_range(worm_ui_nuklear_compose_state_t& state,
    std::uint8_t shift_keys) -> bool {
    if (shift_keys == 0 || state.space_range_hold_active) return false;
    state.shift_range_keys = shift_keys;
    state.space_range_hold_active = true;
    state.space_range_hold_moved = false;
    return true;
}

auto worm_ui_nuklear_release_shift_range(worm_ui_nuklear_compose_state_t& state,
    std::uint8_t released_key) -> bool {
    if (!state.space_range_hold_active || released_key == 0) return false;
    state.shift_range_keys &= static_cast<std::uint8_t>(~released_key);
    return state.shift_range_keys == 0;
}

namespace {

struct compose_file_dialog_state_t {
    std::mutex mutex;
    std::uint64_t generation{};
    bool open{};
    std::optional<worm_ui_nuklear_compose_file_dialog_result_t> result;
};

auto compose_file_dialog_state() -> compose_file_dialog_state_t& {
    static compose_file_dialog_state_t state;
    return state;
}

struct inline_timing_rational_t {
    std::int64_t numerator{};
    std::uint64_t denominator{1};
};

auto trim_timing_text(std::string_view text) -> std::string_view {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
        text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
        text.remove_suffix(1);
    return text;
}

auto parse_timing_unsigned(std::string_view text) -> std::optional<std::uint64_t> {
    if (text.empty()) return std::nullopt;
    std::uint64_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        return std::nullopt;
    return value;
}

auto signed_timing_magnitude(std::uint64_t magnitude, bool negative)
    -> std::optional<std::int64_t> {
    constexpr auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (magnitude > maximum + static_cast<std::uint64_t>(negative)) return std::nullopt;
    if (negative && magnitude == maximum + 1) return std::numeric_limits<std::int64_t>::min();
    const auto value = static_cast<std::int64_t>(magnitude);
    return negative ? -value : value;
}

auto parse_timing_fraction(std::string_view text, bool negative)
    -> std::optional<inline_timing_rational_t> {
    const auto slash = text.find('/');
    if (slash == std::string_view::npos || text.find('/', slash + 1) != std::string_view::npos)
        return std::nullopt;
    const auto magnitude = parse_timing_unsigned(trim_timing_text(text.substr(0, slash)));
    const auto denominator = parse_timing_unsigned(trim_timing_text(text.substr(slash + 1)));
    if (!magnitude || !denominator || *denominator == 0) return std::nullopt;
    const auto divisor = std::gcd(*magnitude, *denominator);
    const auto numerator = signed_timing_magnitude(*magnitude / divisor, negative);
    if (!numerator) return std::nullopt;
    return inline_timing_rational_t{*numerator, *denominator / divisor};
}

auto parse_timing_decimal(std::string_view text, bool negative)
    -> std::optional<inline_timing_rational_t> {
    if (text.empty()) return std::nullopt;
    const auto point = text.find('.');
    if (point != std::string_view::npos && text.find('.', point + 1) != std::string_view::npos)
        return std::nullopt;
    std::uint64_t magnitude{};
    std::uint64_t denominator{1};
    bool saw_digit{};
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto digit = text[i];
        if (digit == '.') continue;
        if (digit < '0' || digit > '9') return std::nullopt;
        saw_digit = true;
        const auto value = static_cast<std::uint64_t>(digit - '0');
        if (magnitude > (std::numeric_limits<std::uint64_t>::max() - value) / 10)
            return std::nullopt;
        magnitude = magnitude * 10 + value;
        if (point != std::string_view::npos && i > point) {
            if (denominator > std::numeric_limits<std::uint64_t>::max() / 10)
                return std::nullopt;
            denominator *= 10;
        }
    }
    if (!saw_digit) return std::nullopt;
    const auto divisor = std::gcd(magnitude, denominator);
    const auto numerator = signed_timing_magnitude(magnitude / divisor, negative);
    if (!numerator) return std::nullopt;
    return inline_timing_rational_t{*numerator, denominator / divisor};
}

auto parse_timing_scalar(std::string_view text) -> std::optional<inline_timing_rational_t> {
    text = trim_timing_text(text);
    bool negative{};
    if (!text.empty() && (text.front() == '+' || text.front() == '-')) {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    if (text.find('/') != std::string_view::npos)
        return parse_timing_fraction(text, negative);
    return parse_timing_decimal(text, negative);
}

template <std::size_t N>
void copy_timing_text(char (&destination)[N], const char* source) {
    std::snprintf(destination, N, "%s", source);
}

auto timing_stage_primary_text(const WormUiTimingView& view) -> const char* {
    if (view.stage_lane == WORM_UI_TIMING_LANE_WARP)
        return view.stage_secondary_numerator_text;
    return view.stage_primary_numerator_text;
}

auto inline_timing_number_allowed(WormUiTimingLaneKind lane, inline_timing_rational_t value)
    -> bool {
    switch (lane) {
    case WORM_UI_TIMING_LANE_BPM: return value.numerator > 0;
    case WORM_UI_TIMING_LANE_SPEED:
    case WORM_UI_TIMING_LANE_STOP:
    case WORM_UI_TIMING_LANE_DELAY: return value.numerator >= 0;
    case WORM_UI_TIMING_LANE_WARP: return value.numerator > 0 && value.denominator == 1;
    case WORM_UI_TIMING_LANE_ANCHOR: return value.numerator >= 0 && value.denominator == 1;
    case WORM_UI_TIMING_LANE_SCROLL: return true;
    case WORM_UI_TIMING_LANE_KIAI: return false;
    case WORM_UI_TIMING_LANE_COUNT: return false;
    }
    return false;
}

void set_inline_timing_number(WormUiWorkspaceAction& action, WormUiTimingLaneKind lane,
                              inline_timing_rational_t value) {
    char numerator[64]{};
    char denominator[64]{};
    std::snprintf(numerator, sizeof(numerator), "%lld",
                  static_cast<long long>(value.numerator));
    std::snprintf(denominator, sizeof(denominator), "%llu",
                  static_cast<unsigned long long>(value.denominator));
    if (lane == WORM_UI_TIMING_LANE_WARP) {
        copy_timing_text(action.timing_secondary_numerator_text, numerator);
        copy_timing_text(action.timing_secondary_denominator_text, denominator);
        action.timing_secondary_value = static_cast<double>(value.numerator);
    } else {
        copy_timing_text(action.timing_primary_numerator_text, numerator);
        copy_timing_text(action.timing_primary_denominator_text, denominator);
        action.timing_value = static_cast<double>(value.numerator) / value.denominator;
    }
}

} // namespace

auto worm_ui_nuklear_compose_file_dialog_begin() -> std::optional<std::uint64_t> {
    auto& dialog = compose_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    if (dialog.open || dialog.generation == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }
    ++dialog.generation;
    dialog.open = true;
    dialog.result.reset();
    return dialog.generation;
}

void worm_ui_nuklear_compose_file_dialog_complete(
    std::uint64_t generation, std::string path, std::string error, bool canceled) {
    auto& dialog = compose_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    if (!dialog.open || generation != dialog.generation) {
        return;
    }
    dialog.open = false;
    dialog.result = {.path = std::move(path),
                     .error = std::move(error),
                     .canceled = canceled};
}

auto worm_ui_nuklear_compose_file_dialog_take()
    -> std::optional<worm_ui_nuklear_compose_file_dialog_result_t> {
    auto& dialog = compose_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    auto result = std::move(dialog.result);
    dialog.result.reset();
    return result;
}

auto worm_ui_nuklear_compose_file_dialog_open() -> bool {
    auto& dialog = compose_file_dialog_state();
    std::lock_guard lock{dialog.mutex};
    return dialog.open;
}

auto worm_ui_nuklear_compose_state() noexcept -> worm_ui_nuklear_compose_state_t& {
    static worm_ui_nuklear_compose_state_t state;
    return state;
}

auto worm_ui_nuklear_update_bgm_column_count_input_owner(
    worm_ui_nuklear_compose_state_t& state, std::uint64_t chart_id,
    std::uint64_t revision, bool editable, bool field_clicked, bool edit_active,
    bool edit_finished) -> bool {
    auto& edit = state.bgm_column_count_edit;
    if (!editable || chart_id == 0) {
        edit = {};
        return false;
    }
    if (!edit.initialized || edit.chart_id != chart_id || edit.revision != revision) {
        edit = {};
        edit.chart_id = chart_id;
        edit.revision = revision;
        edit.initialized = true;
    }
    if (edit.release_pending) {
        edit.input_owned = false;
        edit.release_pending = false;
    }
    if (edit_finished) {
        edit.input_owned = edit.input_owned || field_clicked || edit_active;
        edit.release_pending = edit.input_owned;
    } else if (field_clicked || edit_active) {
        edit.input_owned = true;
        edit.release_pending = false;
    } else if (edit.input_owned) {
        // Losing Nuklear's active flag can precede queued key dispatch too.
        // Keep ownership for this frame and release it on the next update.
        edit.release_pending = true;
    }
    return edit.input_owned;
}

void worm_ui_nuklear_retire_bgm_column_count_input_owner(
    worm_ui_nuklear_compose_state_t& state) {
    auto& edit = state.bgm_column_count_edit;
    if (!edit.release_pending) return;
    edit.input_owned = false;
    edit.release_pending = false;
}

void worm_ui_nuklear_advance_resource_picker(std::uint64_t placed_resource_id,
                                              std::uint64_t next_resource_id) {
    auto& state = worm_ui_nuklear_compose_state();
    if (!state.auto_increment_on_place || placed_resource_id == 0 ||
        state.armed_resource_id != placed_resource_id) {
        return;
    }
    if (state.armed_from_sounds_dock) {
        next_resource_id = next_visible_keysound(state.keysound_advance_rows, placed_resource_id);
        if (next_resource_id) {
            const std::array next{next_resource_id};
            worm_ui_keysound_clear(state.keysound_selection);
            worm_ui_keysound_select_all(state.keysound_selection, next);
            state.keysound_reveal_primary = true;
        }
    }
    if (next_resource_id == 0) {
        state.auto_increment_on_place = false;
        state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    } else {
        state.armed_resource_id = next_resource_id;
    }
}

auto worm_ui_nuklear_sound_list_contains(float x, float y) -> bool {
    const auto& state = worm_ui_nuklear_compose_state();
    return state.sound_list_valid && x >= state.sound_list_x &&
           x < state.sound_list_x + state.sound_list_width && y >= state.sound_list_y &&
           y < state.sound_list_y + state.sound_list_height;
}

void worm_ui_nuklear_reset_compose_state() {
    auto& dialog = compose_file_dialog_state();
    {
        std::lock_guard lock{dialog.mutex};
        if (dialog.generation != std::numeric_limits<std::uint64_t>::max()) {
            ++dialog.generation;
        }
        dialog.open = false;
        dialog.result.reset();
    }
    worm_ui_nuklear_compose_state() = {};
}

auto worm_ui_nuklear_begin_inline_offset(worm_ui_nuklear_compose_state_t& state,
                                         const WormUiComposeView& view) -> bool {
    if (!view.audio_offset_editable || view.input_occluded || !view.chart_id ||
        !view.waveform_audio_offset_denominator ||
        state.inline_timing_open || state.go_to_measure_open) return false;
    state.inline_offset_open = true;
    state.inline_offset_focus_pending = true;
    state.inline_offset_cancel_pending = false;
    state.inline_offset_input_consumed = true;
    state.inline_offset_chart_id = view.chart_id;
    state.inline_offset_revision = view.document_revision;
    state.inline_offset_error.fill(0);
    std::snprintf(state.inline_offset_text.data(), state.inline_offset_text.size(), "%lld/%llu",
        static_cast<long long>(view.waveform_audio_offset_numerator),
        static_cast<unsigned long long>(view.waveform_audio_offset_denominator));
    return true;
}

void worm_ui_nuklear_cancel_inline_offset(worm_ui_nuklear_compose_state_t& state) {
    state.inline_offset_input_consumed = true;
    state.inline_offset_open = false;
    state.inline_offset_focus_pending = false;
    state.inline_offset_cancel_pending = false;
}

void worm_ui_nuklear_update_inline_offset(worm_ui_nuklear_compose_state_t& state,
    const WormUiComposeView& view, WormUiWorkspaceAction& action, bool committed, bool deactivated) {
    if (!state.inline_offset_open) return;
    state.inline_offset_input_consumed = true;
    if (state.inline_offset_cancel_pending || !view.audio_offset_editable ||
        state.inline_offset_chart_id != view.chart_id || state.inline_offset_revision != view.document_revision) {
        worm_ui_nuklear_cancel_inline_offset(state);
        return;
    }
    if (committed) {
        const auto value = parse_timing_scalar(state.inline_offset_text.data());
        if (!value) {
            std::snprintf(state.inline_offset_error.data(), state.inline_offset_error.size(),
                          "Enter valid offset seconds (signed decimal or fraction)");
            state.inline_offset_focus_pending = true;
            return;
        }
        if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
        action.compose_action = WORM_UI_COMPOSE_ACTION_APPLY_AUDIO_OFFSET;
        action.compose_input_chart_id = state.inline_offset_chart_id;
        action.compose_input_document_revision = state.inline_offset_revision;
        action.compose_audio_offset_numerator = value->numerator;
        action.compose_audio_offset_denominator = value->denominator;
        worm_ui_nuklear_cancel_inline_offset(state);
    } else if (deactivated) worm_ui_nuklear_cancel_inline_offset(state);
}

void worm_ui_nuklear_inline_timing_initial_text(const WormUiTimingView& view, char* destination,
                                                std::size_t capacity) {
    if (!destination || capacity == 0) return;
    destination[0] = '\0';
    if (!view.stage_active || view.stage_lane == WORM_UI_TIMING_LANE_KIAI) return;
    if (view.stage_lane == WORM_UI_TIMING_LANE_BPM && view.stage_primary_decimal_exact) {
        std::snprintf(destination, capacity, "%s", view.stage_primary_decimal_text);
        return;
    }
    const auto* numerator = timing_stage_primary_text(view);
    const auto* denominator = view.stage_lane == WORM_UI_TIMING_LANE_WARP
                                  ? view.stage_secondary_denominator_text
                                  : view.stage_primary_denominator_text;
    if (view.stage_lane == WORM_UI_TIMING_LANE_ANCHOR ||
        view.stage_lane == WORM_UI_TIMING_LANE_WARP || std::strcmp(denominator, "1") == 0)
        std::snprintf(destination, capacity, "%s", numerator);
    else
        std::snprintf(destination, capacity, "%s/%s", numerator, denominator);
}

auto worm_ui_nuklear_inline_timing_review_new_stage(
    worm_ui_nuklear_compose_state_t& state, const WormUiTimingView& view,
    const WormUiWorkspaceAction& action, float anchor_x, float anchor_y) -> bool {
    if (!view.stage_active) {
        state.inline_timing_stage_seen = false;
        return false;
    }
    const bool same_stage = state.inline_timing_stage_seen &&
        state.inline_timing_seen_chart_id == view.chart_id &&
        state.inline_timing_seen_revision == view.document_revision &&
        state.inline_timing_seen_object_id == view.stage_existing_object_id &&
        state.inline_timing_seen_lane == view.stage_lane &&
        state.inline_timing_seen_stage_y == view.stage_marker_y_pixels;
    if (same_stage) return false;
    if (state.inline_timing_open || state.inline_timing_popup_close_pending ||
        action.timing_stage_action != WORM_UI_TIMING_STAGE_NONE)
        return false;
    state.inline_timing_stage_seen = true;
    state.inline_timing_seen_chart_id = view.chart_id;
    state.inline_timing_seen_revision = view.document_revision;
    state.inline_timing_seen_object_id = view.stage_existing_object_id;
    state.inline_timing_seen_lane = view.stage_lane;
    state.inline_timing_seen_stage_y = view.stage_marker_y_pixels;
    const bool editable_new_value = view.stage_existing_object_id == 0 &&
        (view.stage_lane == WORM_UI_TIMING_LANE_BPM || view.stage_lane == WORM_UI_TIMING_LANE_SCROLL);
    if (!editable_new_value || state.timing_stage_drag_active)
        return false;

    state.inline_timing_open = true;
    state.inline_timing_focus_pending = true;
    state.inline_timing_chart_id = view.chart_id;
    state.inline_timing_revision = view.document_revision;
    state.inline_timing_object_id = view.stage_existing_object_id;
    state.inline_timing_lane = view.stage_lane;
    state.inline_timing_anchor_screen_x = anchor_x;
    state.inline_timing_anchor_screen_y = anchor_y;
    state.inline_timing_text.fill('\0');
    state.inline_timing_error.fill('\0');
    return true;
}

auto worm_ui_nuklear_inline_timing_confirm(const WormUiTimingView& view, const char* text,
                                           WormUiWorkspaceAction& action) -> bool {
    if (!view.stage_active || !text) return false;
    const auto input = trim_timing_text(text);
    if (input.size() >= sizeof(action.timing_bpm_decimal_text)) return false;
    const auto anchor_frame = view.stage_lane == WORM_UI_TIMING_LANE_ANCHOR
                                  ? parse_timing_unsigned(input)
                                  : std::nullopt;
    const auto value = view.stage_lane == WORM_UI_TIMING_LANE_ANCHOR
                           ? std::optional<inline_timing_rational_t>{}
                           : parse_timing_scalar(input);
    if (view.stage_lane == WORM_UI_TIMING_LANE_ANCHOR ? !anchor_frame
        : !value || !inline_timing_number_allowed(view.stage_lane, *value)) return false;
    const auto bpm_decimal = view.stage_lane == WORM_UI_TIMING_LANE_BPM &&
                             input.find('/') == std::string_view::npos;
    auto result = action;
    result.timing_stage_action = WORM_UI_TIMING_STAGE_CONFIRM;
    result.timing_chart_id = view.chart_id;
    result.timing_document_revision = view.document_revision;
    result.timing_object_id = view.stage_existing_object_id;
    result.timing_lane = view.stage_lane;
    result.timing_value = view.stage_value;
    result.timing_secondary_value = view.stage_secondary_value;
    result.timing_transition_is_seconds = view.stage_transition_is_seconds;
    result.timing_toggle_primary = view.stage_toggle_primary;
    result.timing_pause_unit = view.stage_pause_unit;
    copy_timing_text(result.timing_primary_numerator_text, view.stage_primary_numerator_text);
    copy_timing_text(result.timing_primary_denominator_text, view.stage_primary_denominator_text);
    copy_timing_text(result.timing_secondary_numerator_text, view.stage_secondary_numerator_text);
    copy_timing_text(result.timing_secondary_denominator_text,
                     view.stage_secondary_denominator_text);
    copy_timing_text(result.timing_bpm_decimal_text, view.stage_primary_decimal_text);
    result.timing_bpm_decimal_edited = false;
    result.timing_bpm_step = 0;
    if (anchor_frame) {
        std::snprintf(result.timing_primary_numerator_text,
                      sizeof(result.timing_primary_numerator_text), "%llu",
                      static_cast<unsigned long long>(*anchor_frame));
        copy_timing_text(result.timing_primary_denominator_text, "1");
        result.timing_value = static_cast<double>(*anchor_frame);
    } else {
        set_inline_timing_number(result, view.stage_lane, *value);
    }
    if (bpm_decimal) {
        std::snprintf(result.timing_bpm_decimal_text, sizeof(result.timing_bpm_decimal_text),
                      "%.*s", static_cast<int>(input.size()), input.data());
        result.timing_bpm_decimal_edited = true;
    }
    action = result;
    return true;
}

void worm_ui_nuklear_inline_timing_cancel(worm_ui_nuklear_compose_state_t& state,
                                         WormUiWorkspaceAction& action) {
    state.inline_timing_input_consumed = true;
    state.inline_timing_open = false;
    state.inline_timing_focus_pending = false;
    state.inline_timing_popup_close_pending = true;
    action.timing_stage_action = WORM_UI_TIMING_STAGE_CANCEL;
}

void worm_ui_nuklear_inline_timing_update(worm_ui_nuklear_compose_state_t& state,
                                         const WormUiTimingView& view,
                                         WormUiWorkspaceAction& action,
                                         bool committed, bool deactivated) {
    state.inline_timing_input_consumed = true;
    if (committed) {
        if (worm_ui_nuklear_inline_timing_confirm(view, state.inline_timing_text.data(), action)) {
            state.inline_timing_open = false;
            state.inline_timing_focus_pending = false;
        } else {
            std::snprintf(state.inline_timing_error.data(), state.inline_timing_error.size(),
                          "Enter a valid timing value");
            state.inline_timing_focus_pending = true;
        }
    } else if (deactivated) {
        worm_ui_nuklear_inline_timing_cancel(state, action);
    }
}

auto worm_ui_nuklear_overview_fraction(double x, double left, double width)
    -> std::optional<double> {
    if (!std::isfinite(x) || !std::isfinite(left) || !std::isfinite(width) || width <= 0.0)
        return std::nullopt;
    return std::clamp((x - left) / width, 0.0, 1.0);
}

auto worm_ui_nuklear_overview_frame_delta(double x, double anchor, double width,
                                         std::uint64_t total_frames) -> std::int64_t {
    if (!std::isfinite(x) || !std::isfinite(anchor) || !std::isfinite(width) || width <= 0.0)
        return 0;
    const auto frames = (static_cast<long double>(x) - anchor) / width * total_frames;
    const auto minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    const auto maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
    if (frames <= minimum) return std::numeric_limits<std::int64_t>::min();
    if (frames >= maximum) return std::numeric_limits<std::int64_t>::max();
    return static_cast<std::int64_t>(std::round(frames));
}

auto worm_ui_nuklear_overview_capture_matches(
    const worm_ui_nuklear_overview_capture_t& capture, const WormUiComposeView& view,
    double left, double width, bool available, bool window_focused) -> bool {
    return available && window_focused && view.scene_available &&
        view.chart_id == capture.chart_id && view.document_revision == capture.revision &&
        view.selected_audio_asset_id == capture.asset_id &&
        view.waveform_overview_total_frames == capture.total_frames &&
        view.waveform_overview_sample_rate == capture.sample_rate &&
        left == capture.left && width == capture.width && width > 0.0 &&
        capture.total_frames != 0 && capture.sample_rate != 0;
}

auto worm_ui_nuklear_overview_source_fraction(const WormUiComposeView& view)
    -> std::optional<double> {
    if (!view.source_playhead_available || view.source_playhead_asset_id == 0 ||
        view.source_playhead_asset_id != view.selected_audio_asset_id ||
        view.source_playhead_chart_id != view.chart_id ||
        view.source_playhead_document_revision != view.document_revision ||
        view.source_playhead_sample_rate == 0 || view.waveform_overview_sample_rate == 0 ||
        view.waveform_overview_total_frames == 0)
        return std::nullopt;
    const auto fraction = static_cast<long double>(view.source_playhead_frame) *
        view.waveform_overview_sample_rate /
        (static_cast<long double>(view.source_playhead_sample_rate) *
            view.waveform_overview_total_frames);
    if (fraction < 0.0L || fraction >= 1.0L) return std::nullopt;
    return std::min(static_cast<double>(fraction), std::nextafter(1.0, 0.0));
}

auto worm_ui_nuklear_compose_measure_number(const WormUiComposeView& view, const char* text)
    -> std::optional<std::uint64_t> {
    if (!text || !text[0]) return std::nullopt;
    std::size_t length = 0;
    while (length < 32 && text[length] != '\0') ++length;
    if (length >= 32) return std::nullopt;
    std::uint64_t number{};
    const auto parsed = std::from_chars(text, text + length, number);
    if (parsed.ec != std::errc{} || parsed.ptr != text + length) return std::nullopt;
    if (view.bms_measure_labels)
        return number <= 999 ? std::optional{number} : std::nullopt;
    return number != 0 && number <= view.compose_measure_count ? std::optional{number}
                                                               : std::nullopt;
}

// Crossing four pixels begins view scrubbing; release emits one transport click.
auto worm_ui_nuklear_overview_gesture(worm_ui_nuklear_overview_gesture_t& gesture,
                                     double x, bool pressed, bool down, bool valid)
    -> worm_ui_nuklear_overview_event_t {
    using event = worm_ui_nuklear_overview_event_t;
    if (!valid || !std::isfinite(x)) { gesture = {}; return event::none; }
    if (pressed) gesture = {.active = true, .anchor = x};
    if (!gesture.active) return event::none;
    if (!down) { gesture.active = false; return event::click; }
    if (std::abs(x - gesture.anchor) >= 4.0) gesture.dragged = true;
    return gesture.dragged ? event::scroll : event::none;
}

void worm_ui_nuklear_pinch_input(worm_ui_nuklear_pinch_gesture_t& gesture,
    worm_ui_nuklear_pinch_phase_t phase, double factor, double mouse_x,
    double mouse_y, std::uint64_t project_id, std::uint64_t chart_id) {
    using phase_t = worm_ui_nuklear_pinch_phase_t;
    if (phase == phase_t::cancel) { gesture = {}; return; }
    if (phase == phase_t::end) { gesture.active = false; return; }
    if (phase == phase_t::begin) {
        if (!std::isfinite(mouse_x) || !std::isfinite(mouse_y) ||
            project_id == 0 || chart_id == 0) { gesture = {}; return; }
        if (gesture.project_id != project_id || gesture.chart_id != chart_id)
            gesture = {};
        gesture.active = true;
        gesture.mouse_x = mouse_x;
        gesture.mouse_y = mouse_y;
        gesture.project_id = project_id;
        gesture.chart_id = chart_id;
        return;
    }
    if (!gesture.active) return;
    if (!std::isfinite(factor) || factor <= 0.0 ||
        gesture.project_id != project_id || gesture.chart_id != chart_id ||
        gesture.count == gesture.pending.size()) { gesture = {}; return; }
    gesture.pending[gesture.count++] = {factor, gesture.mouse_x, gesture.mouse_y};
}
