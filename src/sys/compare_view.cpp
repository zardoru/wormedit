#include "sys/compare_view.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace wormedit::sys {
namespace {

auto difficulty_name(const wormedit::chart_document_t& chart) -> const std::string& {
    return chart.metadata.name;
}

auto find_chart(const wormedit::song_document_t& document, wormedit::chart_id_t id)
    -> const wormedit::chart_document_t* {
    const auto found = std::ranges::find(document.charts, id, &wormedit::chart_document_t::id);
    return found == document.charts.end() ? nullptr : &*found;
}

} // namespace

auto compare_local_difference_positions(const wormedit::chart_compare_result_t& comparison,
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

void project_pane(flattened_workspace_t& pane, wormedit::document_history_t& history,
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

void populate_compare_view(flattened_workspace_t& result, flattened_workspace_t& first_pane,
                           flattened_workspace_t& second_pane,
                           wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compare_scene_cache_t& cache,
                           application_command_state_t& application_state,
                           int window_width, int window_height) {
    auto& view = result.view.compare;
    view.visible = workspace.active_tab == wormedit::workspace_tab_t::compare_validate;
    if (!view.visible) return;

    std::vector<wormedit::chart_id_t> available;
    available.reserve(history.document.charts.size());
    for (const auto& chart : history.document.charts) available.push_back(chart.id);
    const auto pair = choose_compare_chart_pair(available, workspace.selected_chart,
                                                workspace.comparison_charts);
    const auto first_id = pair ? pair->first
        : workspace.selected_chart.value_or(available.empty() ? wormedit::chart_id_t{}
                                                               : available.front());
    const auto second_id = pair ? pair->second : wormedit::chart_id_t{};
    const auto* first = find_chart(history.document, first_id);
    const auto* second = find_chart(history.document, second_id);
    view.first_chart_id = first_id.value;
    view.second_chart_id = second_id.value;
    view.first_difficulty = first ? difficulty_name(*first).c_str() : "No difficulty";
    view.second_difficulty = second ? difficulty_name(*second).c_str() : "No comparison";
    view.has_two_difficulties = pair.has_value() && first && second;
    view.chart_count = std::min(history.document.charts.size(), std::size_t{256});
    cache.chart_ids.clear();
    cache.chart_names.clear();
    cache.chart_name_pointers.clear();
    cache.chart_ids.reserve(view.chart_count);
    cache.chart_names.reserve(view.chart_count);
    cache.chart_name_pointers.reserve(view.chart_count);
    for (std::size_t index = 0; index < view.chart_count; ++index) {
        cache.chart_ids.push_back(history.document.charts[index].id.value);
        cache.chart_names.push_back(difficulty_name(history.document.charts[index]));
    }
    for (const auto& name : cache.chart_names) cache.chart_name_pointers.push_back(name.c_str());
    view.chart_ids = cache.chart_ids.data();
    view.chart_names = cache.chart_name_pointers.data();
    if (!view.has_two_difficulties) return;

    wormedit::chart_compare_request_t request;
    request.charts = {first_id, second_id};
    const auto compared = wormedit::compare_chart_difficulties(history.document, request);
    const auto first_rows = compare_local_difference_positions(compared, first_id);
    const auto second_rows = compare_local_difference_positions(compared, second_id);
    if (cache.first_chart != first_id || cache.second_chart != second_id ||
        cache.revision != history.document.revision || cache.song != history.document.id) {
        cache.first_state.reset();
        cache.second_state.reset();
        cache.first_scene = {};
        cache.second_scene = {};
        cache.first_chart = first_id;
        cache.second_chart = second_id;
        cache.revision = history.document.revision;
        cache.song = history.document.id;
    }
    auto first_app = application_state;
    auto second_app = application_state;
    if (cache.first_state) {
        wormedit::set_cursor_scale(*cache.first_state, cache.pixels_per_measure);
        wormedit::set_viewport_scroll_pixels(*cache.first_state,
            cache.shared_scroll_measures * cache.pixels_per_measure);
    }
    project_pane(first_pane, history, workspace, first_id, cache.first_scene, cache.first_state,
                 first_app, window_width, window_height, first_rows);
    if (cache.first_state) {
        cache.pixels_per_measure = cache.first_state->pixels_per_standard_measure;
        cache.shared_scroll_measures = cache.first_state->viewport_scroll_pixels /
                                       cache.pixels_per_measure;
    }
    if (cache.second_state) {
        wormedit::set_cursor_scale(*cache.second_state, cache.pixels_per_measure);
        wormedit::set_viewport_scroll_pixels(*cache.second_state,
            cache.shared_scroll_measures * cache.pixels_per_measure);
    }
    project_pane(second_pane, history, workspace, second_id, cache.second_scene, cache.second_state,
                 second_app, window_width, window_height, second_rows);
    view.first_pane = &first_pane.view.compose;
    view.second_pane = &second_pane.view.compose;
    view.scroll_pixels = cache.shared_scroll_measures * cache.pixels_per_measure;
}

} // namespace wormedit::sys
