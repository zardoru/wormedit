#include "sys/project/workspace_view.h"
#include "sys/project/compare_view.h"
#include "sys/notifications/notification_history_runtime.h"

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <tuple>
#include <vector>

namespace wormedit::sys {
namespace {

auto difficulty_name(const wormedit::chart_document_t& chart) -> const std::string& {
    return chart.metadata.name;
}

auto diagnostic_position(const wormedit::song_document_t& document,
                         wormedit::object_id_t id)
    -> wormedit::sys::notification_history_target_t {
    return wormedit::sys::resolve_notification_target(document, wormedit::notification_target_t{id});
}

auto chart_owns_object(const wormedit::chart_document_t& chart, wormedit::object_id_t id) -> bool {
    const auto contains = [id](const auto& items) {
        return std::ranges::find(items, id, [](const auto& item) { return item.id; }) != items.end();
    };
    const auto direct = std::apply([&](const auto&... items) {
        return (contains(items) || ...);
    }, std::tie(chart.lane_layouts, chart.bgm_source_lanes, chart.bpms, chart.pauses,
                chart.warps, chart.scrolls, chart.speeds, chart.kiai, chart.timing_anchors,
                chart.bookmarks, chart.sections, chart.bga_events, chart.sound_channels));
    if (direct) return true;
    for (const auto& measure : chart.measures)
        if (contains(measure.notes) || contains(measure.bgm_events)) return true;
    return std::ranges::any_of(chart.sound_channels, [&](const auto& channel) {
        return contains(channel.events);
    });
}

auto diagnostic_belongs_to_other_chart(const wormedit::song_document_t& document,
                                       const wormedit::chart_document_t& chart,
                                       wormedit::object_id_t id,
                                       const wormedit::sys::notification_history_target_t& target)
    -> bool {
    if (target.availability == WORM_UI_NOTIFICATION_TARGET_POSITION ||
        target.availability == WORM_UI_NOTIFICATION_TARGET_NONPOSITIONAL)
        return target.chart != chart.id;
    const auto owner = std::ranges::find_if(document.charts, [id](const auto& candidate) {
        return chart_owns_object(candidate, id);
    });
    return owner != document.charts.end() && owner->id != chart.id;
}

} // namespace

void compare_scene_cache_t::panes_t::reset() {
    first_state.reset();
    second_state.reset();
    first_scene = {};
    second_scene = {};
}

void cache_compare_diagnostics(compare_scene_cache_t& cache,
                               const wormedit::song_document_t& document) {
    const auto stamp = wormedit::document_stamp_t::from(document);
    // Preserve the former initially-current all-zero binding.
    if (!cache.diagnostics.has_value() && stamp == wormedit::document_stamp_t{}) return;
    static_cast<void>(cache.diagnostics.get_or_build(stamp,
        std::bind_front(wormedit::validate_document, std::cref(document))));
}

auto build_workspace_frame_model(compare_scene_cache_t& cache,
                                 const wormedit::workspace_controller_t& workspace,
                                 const wormedit::song_document_t& document,
                                 std::uint64_t session_generation,
                                 std::optional<wormedit::branch_id_t> authoring_chart)
    -> wormedit::workspace_view_outcome_t {
    cache.bind_project_session(session_generation);
    const bool compare_active = workspace.active_tab == wormedit::workspace_tab_t::compare_validate;
    if (compare_active) cache_compare_diagnostics(cache, document);
    const auto* cached = compare_active ? &cache.diagnostics.value() : nullptr;
    return wormedit::workspace_view_t::from(workspace, document, workspace.active_tab, cached,
                                         &cache.branch_outline, &cache.binding_validation, authoring_chart);
}

namespace {

void build_diagnostics_view(compare_scene_cache_t::diagnostic_rows_t& rows,
                            const wormedit::song_document_t& document,
                            const wormedit::chart_document_t& chart,
                            const wormedit::diagnostic_list_t& diagnostics) {
    rows.labels.clear();
    rows.rows.clear();
    for (const auto& diagnostic : diagnostics) {
        auto position = std::optional<wormedit::local_position_t>{};
        if (diagnostic.object) {
            const auto target = diagnostic_position(document, *diagnostic.object);
            if (diagnostic_belongs_to_other_chart(document, chart, *diagnostic.object, target))
                continue;
            if (target.availability == WORM_UI_NOTIFICATION_TARGET_POSITION)
                position = target.position;
        }
        rows.labels.push_back("[" + std::to_string(static_cast<int>(diagnostic.code)) +
                              "] " + diagnostic.message);
        rows.rows.push_back({.row = position ? position->row : 0,
            .measure = position ? position->measure : wormedit::measure_id_t{},
            .has_row = position.has_value()});
    }
    for (std::size_t index = 0; index < rows.rows.size(); ++index)
        rows.rows[index].label = rows.labels[index];
}

} // namespace

auto compare_local_difference_positions(const wormedit::chart_comparison_t& comparison,
                                       wormedit::chart_id_t chart)
    -> std::vector<wormedit::local_position_t> {
    std::vector<wormedit::local_position_t> rows;
    for (const auto& difference : comparison.differing_rows) {
        const auto pane = std::ranges::find(difference.panes, chart, &wormedit::chart_compare_pane_row_t::chart);
        if (pane != difference.panes.end() && pane->local_position)
            rows.push_back(*pane->local_position);
    }
    return rows;
}

namespace {

void build_pane_view(frontend_workspace_view_t& pane, wormedit::document_history_t& history,
                  const wormedit::workspace_controller_t& workspace, wormedit::chart_id_t chart,
                  compose_scene_cache_t& scene, std::optional<wormedit::editor_interaction_state_t>& state,
                  application_command_state_t& app, int width, int height,
                  std::span<const wormedit::local_position_t> differences) {
    auto pane_workspace = workspace;
    pane_workspace.selected_chart = chart;
    pane.view.compose.visible = true;
    pane.view.compose.input_occluded = true;
    populate_compose_view(pane, history, pane_workspace, scene, width, height, state, app, nullptr,
                          differences);
    pane.view.compose.audio_offset_editable = false;
    pane.view.compose.input_occluded = true;
}

} // namespace

void populate_compare_view(frontend_workspace_view_t& result, frontend_workspace_view_t& first_pane,
                           frontend_workspace_view_t& second_pane,
                           wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compare_scene_cache_t& cache,
                           application_command_state_t& application_state,
                           int window_width, int window_height) {
    first_pane.reset();
    second_pane.reset();
    auto& view = result.view.compare;
    view.visible = workspace.active_tab == wormedit::workspace_tab_t::compare_validate;
    if (!view.visible) return;

    auto& available = cache.available_charts;
    available.clear();
    available.reserve(history.document.charts.size());
    for (const auto& chart : history.document.charts) available.push_back(chart.id);
    const auto pair = choose_compare_chart_pair(available, workspace.selected_chart,
                                                workspace.comparison_charts);
    const auto first_id = pair ? pair->first
        : workspace.selected_chart.value_or(available.empty() ? wormedit::chart_id_t{}
                                                               : available.front());
    const auto second_id = pair ? pair->second : wormedit::chart_id_t{};
    const auto* first = history.document.find_chart(first_id);
    const auto* second = history.document.find_chart(second_id);
    view.first_chart_id = first_id;
    view.second_chart_id = second_id;
    view.first_difficulty = first ? difficulty_name(*first).c_str() : "No difficulty";
    view.second_difficulty = second ? difficulty_name(*second).c_str() : "No comparison";
    view.has_two_difficulties = pair.has_value() && first && second;
    view.charts.clear();
    for (const auto& chart : history.document.charts)
        view.charts.push_back({.id = chart.id, .label = difficulty_name(chart)});
    if (first) {
        cache_compare_diagnostics(cache, history.document);
        const auto key = std::pair{first->id, history.document.revision};
        // Preserve the former initially-current chart/revision zero binding.
        if (cache.diagnostics_projection.has_value() || key != decltype(key){}) {
            static_cast<void>(cache.diagnostics_projection.get_or_build(key,
                [&cache, &history, first](compare_scene_cache_t::diagnostic_rows_t& rows) {
                    build_diagnostics_view(rows, history.document, *first, cache.diagnostics.value());
                }));
        }
        const auto& rows = cache.diagnostics_projection.value();
        view.diagnostics.assign(rows.rows.begin(), rows.rows.end());
        view.diagnostic_chart_id = first->id;
    } else {
        view.diagnostics.clear();
        view.diagnostic_chart_id = {};
    }
    if (!view.has_two_difficulties) return;

    wormedit::chart_compare_request_t request;
    request.charts = {first_id, second_id};
    const auto compared = wormedit::chart_comparison_t::from(history.document, request);
    const auto first_rows = compare_local_difference_positions(compared.value, first_id);
    const auto second_rows = compare_local_difference_positions(compared.value, second_id);
    auto& panes = cache.panes.get_or_build(
        std::tuple{wormedit::document_stamp_t::from(history.document), first_id, second_id},
        std::mem_fn(&compare_scene_cache_t::panes_t::reset));
    auto first_app = application_state;
    auto second_app = application_state;
    if (panes.first_state) {
        wormedit::set_cursor_scale(*panes.first_state, cache.pixels_per_measure);
        wormedit::set_viewport_scroll_pixels(*panes.first_state,
            cache.shared_scroll_measures * cache.pixels_per_measure);
    }
    build_pane_view(first_pane, history, workspace, first_id, panes.first_scene, panes.first_state,
                 first_app, window_width, window_height, first_rows);
    if (panes.first_state) {
        cache.pixels_per_measure = panes.first_state->pixels_per_standard_measure;
        cache.shared_scroll_measures = panes.first_state->viewport_scroll_pixels /
                                       cache.pixels_per_measure;
    }
    if (panes.second_state) {
        wormedit::set_cursor_scale(*panes.second_state, cache.pixels_per_measure);
        wormedit::set_viewport_scroll_pixels(*panes.second_state,
            cache.shared_scroll_measures * cache.pixels_per_measure);
    }
    build_pane_view(second_pane, history, workspace, second_id, panes.second_scene, panes.second_state,
                 second_app, window_width, window_height, second_rows);
    view.first_pane = &first_pane.view.compose;
    view.second_pane = &second_pane.view.compose;
    view.scroll_pixels = cache.shared_scroll_measures * cache.pixels_per_measure;
}

} // namespace wormedit::sys
