#include "sys/compose/compose_bgm_bar.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <optional>

namespace wormedit::sys {
namespace {

auto measure_fraction(const wormedit::measure_index_entry_t& entry,
                      const wormedit::timing_index_t& timing, std::uint32_t sample_rate,
                      std::uint64_t total_frames) -> std::optional<double> {
    const auto time = timing.query_seconds(entry.absolute_begin_row,
                                          wormedit::timing_boundary_side_t::before_delays);
    if (!time || time->value.denominator == 0) return std::nullopt;
    const auto frames = static_cast<long double>(time->value.numerator) * sample_rate /
                        time->value.denominator;
    return static_cast<double>(frames / static_cast<long double>(total_frames));
}

} // namespace

auto bgm_bar_minimum_span(std::uint64_t total_frames) -> double {
    if (total_frames == 0) return 1.0;
    return std::min(1.0, static_cast<double>(bgm_bar_minimum_span_frames) /
                             static_cast<double>(total_frames));
}

auto bgm_bar_fit(bgm_bar_range_t range, double minimum_span) -> bgm_bar_range_t {
    if (!std::isfinite(range.begin) || !std::isfinite(range.end) || !std::isfinite(minimum_span))
        return {};
    minimum_span = std::clamp(minimum_span, 1e-9, 1.0);
    const auto span = std::clamp(range.end - range.begin, minimum_span, 1.0);
    const auto begin = std::clamp(range.begin, 0.0, 1.0 - span);
    return {begin, begin + span};
}

auto bgm_bar_center_on(bgm_bar_range_t range, double center, double minimum_span)
    -> bgm_bar_range_t {
    range = bgm_bar_fit(range, minimum_span);
    if (!std::isfinite(center)) return range;
    const auto span = range.end - range.begin;
    center = std::clamp(center, 0.0, 1.0);
    return bgm_bar_fit({center - span * 0.5, center + span * 0.5}, minimum_span);
}

auto bgm_bar_center_on_visible_span(bgm_bar_range_t range, double visible_begin,
                                    double visible_end, double minimum_span)
    -> bgm_bar_range_t {
    if (!std::isfinite(visible_begin) || !std::isfinite(visible_end) ||
        visible_begin < 0.0 || visible_end > 1.0 || visible_begin > visible_end)
        return bgm_bar_fit(range, minimum_span);
    return bgm_bar_center_on(range, std::midpoint(visible_begin, visible_end), minimum_span);
}

auto bgm_bar_pan_override_matches(const bgm_bar_pan_override_t& override,
    double visible_begin, double visible_end, song_id_t project, chart_id_t chart,
    std::uint64_t revision, asset_id_t asset) -> bool {
    return override.visible_begin_fraction == visible_begin &&
        override.visible_end_fraction == visible_end && override.project == project &&
        override.chart == chart && override.document_revision == revision && override.asset == asset;
}

auto bgm_bar_window_capture_matches(const bgm_bar_window_capture_t& capture,
    song_id_t project, chart_id_t chart, std::uint64_t revision, asset_id_t asset,
    std::uint64_t total_frames, std::uint32_t sample_rate) -> bool {
    return capture.project == project && capture.chart == chart &&
        capture.document_revision == revision && capture.asset == asset &&
        capture.total_frames == total_frames && capture.sample_rate == sample_rate &&
        std::isfinite(capture.range.begin) && std::isfinite(capture.range.end) &&
        capture.range.begin >= 0.0 && capture.range.begin < capture.range.end &&
        capture.range.end <= 1.0;
}

auto bgm_bar_zoom(bgm_bar_range_t range, double anchor, double steps, double minimum_span)
    -> bgm_bar_range_t {
    range = bgm_bar_fit(range, minimum_span);
    if (!std::isfinite(anchor) || !std::isfinite(steps)) return range;
    const auto span = range.end - range.begin;
    const auto anchored = std::clamp(anchor, range.begin, range.end);
    const auto along = (anchored - range.begin) / span;
    const auto next_span = std::clamp(span / std::pow(bgm_bar_zoom_step, steps),
                                      std::clamp(minimum_span, 1e-9, 1.0), 1.0);
    return bgm_bar_fit({anchored - along * next_span, anchored - along * next_span + next_span},
                       minimum_span);
}

auto bgm_bar_zoom_centered(bgm_bar_range_t range, double center, double steps,
                           double minimum_span) -> bgm_bar_range_t {
    range = bgm_bar_fit(range, minimum_span);
    if (!std::isfinite(center) || !std::isfinite(steps)) return range;
    const auto span = range.end - range.begin;
    const auto next_span = std::clamp(span / std::pow(bgm_bar_zoom_step, steps),
                                      std::clamp(minimum_span, 1e-9, 1.0), 1.0);
    return bgm_bar_center_on({center - next_span * 0.5, center + next_span * 0.5}, center,
                             minimum_span);
}

auto bgm_bar_pan(bgm_bar_range_t range, double delta_fraction) -> bgm_bar_range_t {
    if (!std::isfinite(delta_fraction)) return range;
    const auto span = range.end - range.begin;
    return bgm_bar_fit({range.begin + delta_fraction, range.begin + delta_fraction + span}, span);
}

auto bgm_bar_measure_marks(const wormedit::measure_index_t& measures,
                           const wormedit::timing_index_t& timing, std::uint32_t sample_rate,
                           std::uint64_t total_frames, bgm_bar_range_t range,
                           std::size_t capacity) -> std::vector<bgm_bar_mark_t> {
    std::vector<bgm_bar_mark_t> marks;
    if (capacity == 0 || sample_rate == 0 || total_frames == 0 || measures.entries.empty() ||
        !(range.begin < range.end))
        return marks;
    const auto fraction_of = [&](std::size_t index) {
        return measure_fraction(measures.entries[index], timing, sample_rate, total_frames);
    };
    // Measure times only grow with their row, so the first visible measure is a binary search.
    std::size_t low = 0, high = measures.entries.size();
    while (low < high) {
        const auto middle = low + (high - low) / 2;
        const auto fraction = fraction_of(middle);
        if (fraction && *fraction < range.begin) low = middle + 1;
        else high = middle;
    }
    std::vector<bgm_bar_mark_t> visible;
    for (std::size_t index = low; index < measures.entries.size(); ++index) {
        const auto fraction = fraction_of(index);
        if (!fraction) continue;
        if (*fraction > range.end) break;
        if (*fraction >= range.begin) visible.push_back({*fraction, static_cast<std::uint32_t>(index)});
    }
    const auto stride = std::max<std::size_t>(1, (visible.size() + capacity - 1) / capacity);
    for (std::size_t index = 0; index < visible.size(); index += stride) marks.push_back(visible[index]);
    return marks;
}

auto consume_bgm_bar_inputs(wormedit::ui::workspace_action_t& action, bgm_bar_range_t& range,
                            std::optional<bgm_bar_pan_override_t>& pan_override,
                            const bgm_bar_input_context_t& context) -> bool {
    if (action.compose_inputs.size() > wormedit::ui::compose_input_admission_limit) return false;
    const auto minimum_span = bgm_bar_minimum_span(context.total_frames);
    const auto visible_center = std::midpoint(context.visible_begin_fraction,
                                              context.visible_end_fraction);
    if (pan_override && !bgm_bar_pan_override_matches(*pan_override,
            context.visible_begin_fraction, context.visible_end_fraction,
            context.project, context.chart, context.document_revision, context.asset))
        pan_override.reset();
    bool consumed = false;
    std::size_t kept = 0;
    for (std::size_t index = 0; index < action.compose_inputs.size(); ++index) {
        const auto& input = action.compose_inputs[index];
        switch (input.kind) {
        case WORM_UI_COMPOSE_INPUT_BGM_BAR_ZOOM:
            range = bgm_bar_zoom_centered(pan_override ? pan_override->range : range,
                                          visible_center, input.x_pixels, minimum_span);
            pan_override.reset();
            consumed = true;
            break;
        case WORM_UI_COMPOSE_INPUT_BGM_BAR_PAN:
            range = bgm_bar_fit(range, minimum_span);
            pan_override = bgm_bar_pan_override_t{
                .range = bgm_bar_pan(pan_override ? pan_override->range
                                                 : bgm_bar_center_on_visible_span(
                                                       range, context.visible_begin_fraction,
                                                       context.visible_end_fraction, minimum_span),
                                     input.x_pixels),
                .visible_begin_fraction = context.visible_begin_fraction,
                .visible_end_fraction = context.visible_end_fraction,
                .project = context.project,
                .chart = context.chart,
                .document_revision = context.document_revision,
                .asset = context.asset};
            consumed = true;
            break;
        case WORM_UI_COMPOSE_INPUT_BGM_BAR_RESET:
            range = {};
            pan_override.reset();
            consumed = true;
            break;
        default:
            action.compose_inputs[kept++] = input;
        }
    }
    action.compose_inputs.resize(kept);
    return consumed;
}

} // namespace wormedit::sys
