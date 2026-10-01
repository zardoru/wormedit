#include "ui/nuklear/compose_state.h"

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

void worm_ui_nuklear_advance_resource_picker(std::uint64_t placed_resource_id,
                                              std::uint64_t next_resource_id) {
    auto& state = worm_ui_nuklear_compose_state();
    if (!state.auto_increment_on_place || placed_resource_id == 0 ||
        state.armed_resource_id != placed_resource_id) {
        return;
    }
    if (next_resource_id == 0) {
        state.auto_increment_on_place = false;
        state.resource_pointer_mode = worm_ui_nuklear_compose_resource_pointer_mode_t::off;
    } else {
        state.armed_resource_id = next_resource_id;
    }
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
