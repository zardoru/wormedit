#include "sys/storyboard/storyboard_timeline_view.h"
#include "core/timing_arithmetic.h"
#include "core/checked_math.h"
#include "ui/frontend/storyboard_timeline.h"
#include "sys/storyboard/storyboard_inspector_view.h"
#include <wormedit/core/rational.h>
#include <wormedit/core/interaction_state.h>
#include <wormedit/core/osbx_evaluator.h>
#include <wormedit/core/grid.h>
#include <algorithm>
#include <array>
#include <limits>
#include <cmath>
#include <string_view>

namespace wormedit::sys {
namespace {
auto time_seconds(storyboard_time_t time) -> std::optional<signed_rational_t> {
    if (!time.is_valid()) return std::nullopt;
    return timing_arithmetic::add_exact({time.milliseconds, 1000},
        {time.remainder, static_cast<std::uint64_t>(time.denominator) * 1000});
}
auto authored_milliseconds(signed_rational_t seconds) -> result_t<std::int64_t, failure_code_t> {
    const auto ms = timing_arithmetic::multiply(seconds, {1000, 1});
    if (!ms || ms->denominator > std::numeric_limits<std::uint32_t>::max())
        return std::unexpected(failure_code_t::arithmetic_overflow);
    const auto denominator = static_cast<std::int64_t>(ms->denominator);
    auto whole = ms->numerator / denominator;
    auto remainder = ms->numerator % denominator;
    if (remainder < 0) { --whole; remainder += denominator; }
    return snap_storyboard_key_time({.milliseconds = whole,
        .remainder = static_cast<std::uint32_t>(remainder),
        .denominator = static_cast<std::uint32_t>(denominator)});
}
} // namespace

auto storyboard_timeline_timing_t::from(const document_history_t& history, std::optional<chart_id_t> selected,
    const editor_interaction_state_t* state) -> storyboard_timeline_timing_t {
    if (!selected) return {};
    const auto* chart = history.read_chart(*selected);
    const auto* indices = history.indices(*selected);
    if (!chart || !indices || !indices->measures || !indices->timing.value) return {};
    return {.chart = chart, .measures = &*indices->measures, .timing = &*indices->timing.value,
        .grid = state && state->chart == *selected ? edit_snap_grid(*chart, *state) : snap_grid_t{}};
}
auto storyboard_timeline_timing_t::has_timing() const -> bool {
    return chart && measures && timing &&
        timing->divisions_per_standard_measure == chart->divisions_per_standard_measure &&
        timing->total_rows == measures->total_rows;
}
auto storyboard_timeline_timing_t::is_available() const -> bool {
    return has_timing() && snap_step(*chart, grid, chart->divisions_per_standard_measure).has_value();
}
auto plan_storyboard_timeline_time(storyboard_time_t time,
    const storyboard_timeline_timing_t& source, bool grid_enabled, bool shift)
    -> result_t<std::int64_t, failure_code_t> {
    if (!grid_enabled || shift) return snap_storyboard_key_time(time);
    if (!source.is_available()) return std::unexpected(failure_code_t::unavailable);
    if (!time.is_valid()) return std::unexpected(failure_code_t::invalid_request);
    const auto seconds = time_seconds(time);
    if (!seconds) return std::unexpected(failure_code_t::arithmetic_overflow);
    const auto position = source.timing->row_at(*seconds, chart_end_policy_t::reject);
    if (!position.absolute_row) return std::unexpected(failure_code_t::invalid_request);
    std::uint64_t canvas_rows{};
    if (!checked_math::checked_add(source.measures->total_rows, std::uint64_t{1}, canvas_rows))
        return std::unexpected(failure_code_t::arithmetic_overflow);
    const auto row = snap_canvas_row(*source.chart, *source.measures,
        to_long_double(*position.absolute_row), source.grid, canvas_rows);
    if (!row) return std::unexpected(failure_code_t::unavailable);
    const auto target = source.timing->seconds_at(*row);
    if (!target.seconds) return std::unexpected(failure_code_t::arithmetic_overflow);
    return authored_milliseconds(*target.seconds);
}
auto storyboard_timeline_pointer_time(double milliseconds) -> result_t<storyboard_time_t, failure_code_t> {
    if (!std::isfinite(milliseconds)) return std::unexpected(failure_code_t::invalid_request);
    const auto whole = std::floor(milliseconds);
    // Compare in long double before casting; double(INT64_MAX) is already 2^63.
    if (static_cast<long double>(whole) < static_cast<long double>(std::numeric_limits<std::int64_t>::min()) ||
        static_cast<long double>(whole) >= -static_cast<long double>(std::numeric_limits<std::int64_t>::min()))
        return std::unexpected(failure_code_t::arithmetic_overflow);
    const auto fraction = milliseconds - whole;
    if (fraction == 0) return storyboard_time_t{.milliseconds = static_cast<std::int64_t>(whole)};
    const auto rational = rationalized_value_t::from(fraction,
        {.maximum_denominator = std::numeric_limits<std::uint32_t>::max()});
    if (!rational) return std::unexpected(rational.error().code);
    if (rational->value.numerator == rational->value.denominator)
        return storyboard_time_t{.milliseconds = static_cast<std::int64_t>(whole) + 1};
    return storyboard_time_t{.milliseconds = static_cast<std::int64_t>(whole),
        .remainder = static_cast<std::uint32_t>(rational->value.numerator),
        .denominator = static_cast<std::uint32_t>(rational->value.denominator)};
}
namespace {
constexpr std::array<std::string_view, 35> osb_easing_labels{
    "Linear", "Out", "In", "In Quad", "Out Quad", "In Out Quad", "In Cubic", "Out Cubic", "In Out Cubic",
    "In Quart", "Out Quart", "In Out Quart", "In Quint", "Out Quint", "In Out Quint",
    "In Sine", "Out Sine", "In Out Sine", "In Expo", "Out Expo", "In Out Expo",
    "In Circ", "Out Circ", "In Out Circ", "In Elastic", "Out Elastic", "Out Elastic Half",
    "Out Elastic Quarter", "In Out Elastic", "In Back", "Out Back", "In Out Back",
    "In Bounce", "Out Bounce", "In Out Bounce"};
auto easing_label(const osbx_easing_t& easing) -> std::string_view {
    if (std::holds_alternative<osbx_step_easing_t>(easing)) return "Step";
    if (std::holds_alternative<osbx_linear_easing_t>(easing)) return "Linear";
    if (const auto* osb = std::get_if<osbx_osb_easing_t>(&easing)) {
        if (osb->value >= 0 && static_cast<std::size_t>(osb->value) < osb_easing_labels.size())
            return osb_easing_labels[static_cast<std::size_t>(osb->value)];
        return "Invalid easing";
    }
    return "Cubic bezier";
}
void build_easing_preview(ui::storyboard_easing_choice_t& choice, const osbx_easing_t& easing) {
    choice.easing = easing;
    choice.label = easing_label(easing);
    const osbx_track_t<> unit{.default_value = {0}, .keys = {
        {.id = "a", .time_ms = 0, .value = {0}, .easing = easing},
        {.id = "b", .time_ms = 32, .value = {1}}}};
    for (std::size_t i = 0; i < choice.preview.size(); ++i) {
        const auto sampled = sample_osbx_numeric_track(unit, static_cast<std::int64_t>(i));
        if (sampled.size() != 1 || !std::isfinite(sampled.front())) {
            choice.label = "Easing preview unavailable";
            choice.preview.fill(0);
            return;
        }
        choice.preview[i] = static_cast<float>(sampled.front());
    }
}
auto timeline_node(const osbx_document_t& scene, std::string_view id) -> const osbx_node_t* {
    const auto found = std::ranges::find(scene.nodes, id, &osbx_node_t::id);
    return found == scene.nodes.end() ? nullptr : &*found;
}
auto timeline_storyboard(const song_document_t& song, storyboard_id_t id) -> const storyboard_document_t* {
    const auto found = std::ranges::find(song.storyboards, id, &storyboard_document_t::id);
    return found == song.storyboards.end() ? nullptr : &*found;
}
using timeline_source_track_t = std::variant<const osbx_track_t<>*, const osbx_track_t<bool>*, const osbx_track_t<osbx_blend_t>*>;
auto timeline_source_track(const osbx_node_t&, const osbx_track_target_t&) -> std::optional<timeline_source_track_t>;
auto timeline_track_reason(const storyboard_document_t& board, const osbx_track_target_t& target) -> std::string {
    auto reason = storyboard_node_edit_reason(board, target.node);
    if (!reason.empty()) return reason;
    const auto* node = timeline_node(*board.osbx, target.node);
    if (!node) return "Generated node: detach before editing.";
    const auto track = timeline_source_track(*node, target);
    if (!track) return "Generated track: detach before editing.";
    return {};
}
struct timeline_rows_t {
    ui::storyboard_timeline_view_t& view;
    const storyboard_document_t& storyboard;
    std::size_t used{};
    template<class Value>
    void append(const osbx_track_t<Value>& source, osbx_track_target_t target,
                std::string_view label, std::size_t depth, std::string_view node_reason) {
        if (used == view.rows.size()) view.rows.emplace_back();
        auto& row = view.rows[used++];
        row.track = std::move(target);
        row.label.assign(row.track.node); row.label.append(" / "); row.label.append(label);
        row.depth = depth;
        row.reason = node_reason.empty() ? timeline_track_reason(storyboard, row.track) : std::string(node_reason);
        row.editable = row.reason.empty();
        row.keys.resize(source.keys.size());
        for (std::size_t i = 0; i < source.keys.size(); ++i) {
            const auto& key = source.keys[i];
            row.keys[i] = {.id = key.id, .time_ms = key.time_ms, .easing = key.easing,
                .easing_label = std::string(easing_label(key.easing)),
                .editable = row.editable && source.can_edit_key_at(key.time_ms)};
        }
    }
    void ops(std::span<const osbx_transform_op_t> stack, std::string_view node,
             std::size_t depth, std::string_view reason) {
        for (const auto& op : stack) {
            append(op.track, {.node = std::string(node), .kind = osbx_track_kind_t::transform, .op = op.id},
                storyboard_transform_label(op.kind), depth, reason);
            ops(op.children, node, depth + 1, reason);
        }
    }
    void node(const osbx_node_t& source) {
        const auto reason = storyboard_node_edit_reason(storyboard, source.id);
        ops(source.ops, source.id, 0, reason);
        append(source.tracks.opacity, {.node = source.id, .kind = osbx_track_kind_t::opacity}, "Opacity", 0, reason);
        append(source.tracks.color, {.node = source.id, .kind = osbx_track_kind_t::color}, "Color", 0, reason);
        append(source.tracks.visibility, {.node = source.id, .kind = osbx_track_kind_t::visibility}, "Visibility", 0, reason);
        append(source.tracks.blend, {.node = source.id, .kind = osbx_track_kind_t::blend}, "Blend", 0, reason);
    }
};
void build_timeline_rows(ui::storyboard_timeline_view_t& view, const storyboard_document_t* board,
    const osbx_document_t* scene, std::span<const std::string> selected) {
    if (!board || !board->osbx || !scene) { view.rows.clear(); return; }
    timeline_rows_t builder{.view = view, .storyboard = *board};
    // Prepared topology includes generated children, but hand-key markers always
    // come from the authored track. Worker samples are a separate Lua band.
    for (const auto& node : scene->nodes) {
        if (std::ranges::find(selected, node.id) == selected.end()) continue;
        const auto* authored = timeline_node(*board->osbx, node.id);
        builder.node(authored ? *authored : node);
    }
    view.rows.resize(builder.used);
}
void refill_lua_bands(ui::storyboard_timeline_view_t& view, const ui::storyboard_view_t& root) {
    const auto& viewport = root.state.viewport;
    for (auto& row : view.rows) {
        const auto* link = root.inspector.lua_link_for({.track = row.track});
        if (!link || link->link_id.empty()) { row.lua.reset(); continue; }
        if (!row.lua) row.lua.emplace();
        auto& band = *row.lua;
        const auto begin = link->last_good && !link->sample_times_ms.empty() ? link->sample_times_ms.front() : link->form.begin_ms;
        const auto end = link->last_good && !link->sample_times_ms.empty() ? link->sample_times_ms.back() : link->form.end_ms;
        band.begin = static_cast<double>((static_cast<long double>(begin) - viewport.timeline_begin_ms) * viewport.timeline_pixels_per_ms);
        band.end = static_cast<double>((static_cast<long double>(end) - viewport.timeline_begin_ms) * viewport.timeline_pixels_per_ms);
        band.samples.clear();
        for (const auto time : link->sample_times_ms)
            band.samples.push_back(static_cast<double>((static_cast<long double>(time) - viewport.timeline_begin_ms) * viewport.timeline_pixels_per_ms));
    }
}
void build_timeline_grid(ui::storyboard_timeline_view_t& view, const storyboard_timeline_timing_t& source) {
    view.lines.clear();
    view.grid_available = source.is_available();
    if (!source.has_timing()) return;
    const auto grid = editor_grid_t::from(*source.chart, {.visible_begin_row = 0,
        .visible_end_row = source.measures->total_rows,
        .active_snap = view.grid_available ? std::optional{source.grid} : std::nullopt,
        .density = {.minimum_group_spacing_pixels = 0, .minimum_beat_spacing_pixels = 0,
            .minimum_active_snap_spacing_pixels = 0}});
    if (!grid.ok()) { view.grid_available = false; return; }
    for (const auto& line : grid.value.lines) {
        const auto seconds = source.timing->seconds_at(line.chart_row);
        if (!seconds.seconds) continue;
        view.lines.push_back({.time_ms = to_double(*seconds.seconds) * 1000,
            .measure = line.kind == editor_grid_line_kind_t::measure});
    }
}
} // namespace

void storyboard_timeline_projection_t::refill(ui::storyboard_timeline_view_t& view,
    const song_document_t& song, const ui::storyboard_view_t& root, storyboard_time_t time,
    const osbx_document_t* prepared_scene, const storyboard_timeline_timing_t& timing) {
    const document_stamp_t stamp{.project = song.id, .revision = song.revision};
    const auto& selection = root.state.selection;
    const auto generation = root.canvas.prepared_generation;
    const auto chart = timing.chart ? timing.chart->id : chart_id_t{};
    const bool changed = !stamp_ || *stamp_ != stamp || session_ != root.session_generation;
    if (changed || chart_ != chart || grid_ != timing.grid) {
        build_timeline_grid(view, timing);
        chart_ = chart; grid_ = timing.grid;
    }
    if (changed ||
        prepared_generation_ != generation || storyboard_ != selection.storyboard || nodes_ != selection.nodes) {
        const auto* board = timeline_storyboard(song, selection.storyboard);
        const auto* scene = prepared_scene ? prepared_scene : board && board->osbx ? &*board->osbx : nullptr;
        build_timeline_rows(view, board, scene, selection.nodes);
        stamp_ = stamp; session_ = root.session_generation; prepared_generation_ = generation;
        storyboard_ = selection.storyboard; nodes_ = selection.nodes;
    }
    if (view.easings.empty()) {
        auto& step = view.easings.emplace_back(); build_easing_preview(step, osbx_step_easing_t{});
        auto& linear = view.easings.emplace_back(); build_easing_preview(linear, osbx_linear_easing_t{});
        for (std::int32_t i = 0; i < static_cast<std::int32_t>(osb_easing_labels.size()); ++i) {
            auto& choice = view.easings.emplace_back(); build_easing_preview(choice, osbx_osb_easing_t{.value = i});
        }
        auto& bezier = view.easings.emplace_back(); build_easing_preview(bezier, osbx_cubic_bezier_t{});
    }
    view.time = time;
    view.grid_enabled = view.grid_enabled && view.grid_available;
    view.playhead_position = time.is_valid() ? static_cast<double>((static_cast<long double>(time.milliseconds) + time.fraction() -
        root.state.viewport.timeline_begin_ms) * root.state.viewport.timeline_pixels_per_ms) : 0;
    view.message = view.rows.empty() ? "Select a node to show its keyframe tracks." : "";
    refill_lua_bands(view, root);
    for (auto& row : view.rows)
        for (auto& key : row.keys) {
            key.position = static_cast<double>((static_cast<long double>(key.time_ms) -
                root.state.viewport.timeline_begin_ms) * root.state.viewport.timeline_pixels_per_ms);
            key.selected = std::ranges::find(selection.keys, key.id) != selection.keys.end();
        }
}
namespace {
auto timeline_op(std::span<const osbx_transform_op_t> stack, std::string_view id) -> const osbx_transform_op_t* {
    for (const auto& op : stack) {
        if (op.id == id) return &op;
        if (const auto* found = timeline_op(op.children, id)) return found;
    }
    return nullptr;
}
auto timeline_source_track(const osbx_node_t& node, const osbx_track_target_t& target)
    -> std::optional<timeline_source_track_t> {
    switch (target.kind) {
    case osbx_track_kind_t::transform: {
        const auto* op = timeline_op(node.ops, target.op);
        if (op) return &op->track;
        return {};
    }
    case osbx_track_kind_t::opacity: return &node.tracks.opacity;
    case osbx_track_kind_t::color: return &node.tracks.color;
    case osbx_track_kind_t::visibility: return &node.tracks.visibility;
    case osbx_track_kind_t::blend: return &node.tracks.blend;
    }
    return {};
}
auto sampled_value(const osbx_track_t<>& track, storyboard_time_t time)
    -> result_t<std::vector<double>, failure_code_t> {
    auto value = sample_osbx_numeric_track(track, time);
    if (value.empty()) return std::unexpected(failure_code_t::invalid_data);
    return value;
}
auto sampled_value(const osbx_track_t<bool>& track, storyboard_time_t time) -> result_t<bool, failure_code_t> {
    return sample_osbx_visibility_track(track, time);
}
auto sampled_value(const osbx_track_t<osbx_blend_t>& track, storyboard_time_t time)
    -> result_t<osbx_blend_t, failure_code_t> {
    return sample_osbx_blend_track(track, time);
}
template<class Value>
auto key_time(const osbx_track_t<Value>& track, std::string_view id) -> std::optional<std::int64_t> {
    const auto found = std::ranges::find(track.keys, id, &osbx_key_t<Value>::id);
    return found == track.keys.end() ? std::nullopt : std::optional{found->time_ms};
}
auto plan_timeline_add(const osbx_node_t& node, const osbx_track_target_t& target,
    storyboard_time_t time, std::int64_t key_time) -> result_t<osbx_edit_change_t, failure_code_t> {
    const auto source = timeline_source_track(node, target);
    if (!source) return std::unexpected(failure_code_t::not_found);
    return std::visit([&](const auto* track) -> result_t<osbx_edit_change_t, failure_code_t> {
        if (!track->can_edit_key_at(key_time)) return std::unexpected(failure_code_t::unavailable);
        const auto value = sampled_value(*track, time);
        if (!value) return std::unexpected(value.error());
        if (track->keys.empty())
            return osbx_add_key_t{.track = target, .time_ms = key_time, .value = *value};
        return plan_storyboard_track_edit(*track, target, *value, key_time);
    }, *source);
}
auto plan_timeline_move(const ui::storyboard_timeline_action_t& action,
    const storyboard_timeline_timing_t& timing) -> result_t<osbx_edit_change_t, failure_code_t> {
    if (action.phase == ui::storyboard_inspector_phase_t::begin)
        return osbx_move_keys_t{.keys = action.keys, .delta_ms = 0};
    auto time = storyboard_timeline_pointer_time(action.pointer_delta_ms);
    if (!time) return std::unexpected(time.error());
    const auto anchored = checked_math::checked_add(action.anchor_time_ms, time->milliseconds);
    if (!anchored) return std::unexpected(failure_code_t::arithmetic_overflow);
    time->milliseconds = *anchored;
    const auto snapped = plan_storyboard_timeline_time(*time, timing, action.grid_enabled, action.shift);
    if (!snapped) return std::unexpected(snapped.error());
    const auto delta = timing_arithmetic::subtract_exact({*snapped, 1}, {action.anchor_time_ms, 1});
    if (!delta) return std::unexpected(failure_code_t::arithmetic_overflow);
    return osbx_move_keys_t{.keys = action.keys, .delta_ms = delta->numerator};
}
} // namespace

auto storyboard_timeline_key_time(const osbx_document_t& scene, const osbx_key_target_t& target)
    -> std::optional<std::int64_t> {
    const auto* node = timeline_node(scene, target.track.node);
    if (!node) return {};
    const auto source = timeline_source_track(*node, target.track);
    if (!source) return {};
    return std::visit([&](const auto* track) { return key_time(*track, target.key); }, *source);
}
auto plan_storyboard_timeline_edit(const song_document_t& song, storyboard_id_t id,
    const ui::storyboard_timeline_action_t& action, storyboard_time_t time,
    const osbx_document_t* /*prepared_scene*/, const storyboard_timeline_timing_t& timing)
    -> result_t<osbx_edit_change_t, failure_code_t> {
    const auto* board = timeline_storyboard(song, id);
    if (!board || !board->osbx) return std::unexpected(failure_code_t::not_found);
    const auto& target = action.keys.empty() ? action.track : action.keys.front().track;
    if (!timeline_track_reason(*board, target).empty()) return std::unexpected(failure_code_t::unavailable);
    using kind = ui::storyboard_timeline_action_kind_t;
    switch (action.kind) {
    case kind::add_key: {
        const auto snapped = plan_storyboard_timeline_time(time, timing, action.grid_enabled, action.shift);
        if (!snapped) return std::unexpected(snapped.error());
        const auto* node = timeline_node(*board->osbx, action.track.node);
        if (!node) return std::unexpected(failure_code_t::not_found);
        return plan_timeline_add(*node, action.track, time, *snapped);
    }
    case kind::move_keys: return plan_timeline_move(action, timing);
    case kind::easing:
        if (action.keys.size() != 1) return std::unexpected(failure_code_t::invalid_request);
        return osbx_set_easing_t{.track = target, .key = action.keys.front().key, .easing = action.easing};
    case kind::erase_keys:
        return osbx_delete_keys_t{.keys = action.keys};
    case kind::none: case kind::select_keys: case kind::seek: case kind::set_grid:
        return std::unexpected(failure_code_t::invalid_request);
    }
    return std::unexpected(failure_code_t::invalid_request);
}
} // namespace wormedit::sys
