#include "sys/compare_view.h"
#include "sys/notification_history_runtime.h"

#include <algorithm>
#include <array>
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

void cache_compare_diagnostics(compare_scene_cache_t& cache,
                               const wormedit::song_document_t& document) {
    if (cache.diagnostics_song == document.id &&
        cache.diagnostics_revision == document.revision) return;
    cache.diagnostics_song = document.id;
    cache.diagnostics_revision = document.revision;
    cache.cached_diagnostics = wormedit::validate_document(document);
}

auto build_workspace_frame_model(compare_scene_cache_t& cache,
                                 const wormedit::workspace_controller_t& workspace,
                                 const wormedit::song_document_t& document,
                                 std::uint64_t session_generation)
    -> wormedit::workspace_view_result_t {
    cache.bind_project_session(session_generation);
    const bool compare_active = workspace.active_tab == wormedit::workspace_tab_t::compare_validate;
    if (compare_active) cache_compare_diagnostics(cache, document);
    const auto* cached = compare_active ? &cache.cached_diagnostics : nullptr;
    return wormedit::workspace_view_t::from(workspace, document, workspace.active_tab, cached,
                                         &cache.branch_outline, &cache.binding_validation);
}

namespace {

void project_diagnostics(compare_scene_cache_t& cache,
                         const wormedit::song_document_t& document,
                         const wormedit::chart_document_t& chart) {
    cache_compare_diagnostics(cache, document);
    if (cache.diagnostics_chart == chart.id &&
        cache.diagnostics_projection_revision == document.revision) return;
    cache.diagnostics_chart = chart.id;
    cache.diagnostics_projection_revision = document.revision;
    cache.diagnostic_labels.clear();
    cache.diagnostic_label_pointers.clear();
    cache.diagnostic_rows.clear();
    cache.diagnostic_measures.clear();
    cache.diagnostic_has_rows.clear();
    for (const auto& diagnostic : cache.cached_diagnostics) {
        if (cache.diagnostic_labels.size() == 256) break;
        auto position = std::optional<wormedit::local_position_t>{};
        if (diagnostic.object) {
            const auto target = diagnostic_position(document, *diagnostic.object);
            if (diagnostic_belongs_to_other_chart(document, chart, *diagnostic.object, target))
                continue;
            if (target.availability == WORM_UI_NOTIFICATION_TARGET_POSITION) {
                position = target.position;
            }
        }
        cache.diagnostic_labels.push_back("[" + std::to_string(static_cast<int>(diagnostic.code)) +
                                          "] " + diagnostic.message);
        cache.diagnostic_rows.push_back(position ? position->row : 0);
        cache.diagnostic_measures.push_back(position ? position->measure.value : 0);
        cache.diagnostic_has_rows.push_back(position.has_value() ? 1 : 0);
    }
    cache.diagnostic_label_pointers.reserve(cache.diagnostic_labels.size());
    for (const auto& label : cache.diagnostic_labels)
        cache.diagnostic_label_pointers.push_back(label.c_str());
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
    const auto* first = history.document.find_chart(first_id);
    const auto* second = history.document.find_chart(second_id);
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
    if (first) {
        project_diagnostics(cache, history.document, *first);
        view.diagnostic_count = cache.diagnostic_labels.size();
        view.diagnostic_labels = cache.diagnostic_label_pointers.data();
        view.diagnostic_rows = cache.diagnostic_rows.data();
        view.diagnostic_measures = cache.diagnostic_measures.data();
        view.diagnostic_chart_id = cache.diagnostics_chart.value;
        view.diagnostic_has_rows = cache.diagnostic_has_rows.data();
    } else {
        view.diagnostic_count = 0;
        view.diagnostic_labels = nullptr;
        view.diagnostic_rows = nullptr;
        view.diagnostic_measures = nullptr;
        view.diagnostic_chart_id = 0;
        view.diagnostic_has_rows = nullptr;
    }
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
