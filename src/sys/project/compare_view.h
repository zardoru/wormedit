#pragma once

#include "sys/compose/views/compose_view.h"

#include <wormedit/core/chart_compare.h>

#include <algorithm>
#include <string>
#include <tuple>
#include <span>
#include <vector>

namespace wormedit::sys {

struct compare_scene_cache_t {
    std::uint64_t session_generation{};
    void bind_project_session(std::uint64_t generation) {
        if (session_generation == generation) return;
        *this = {};
        session_generation = generation;
    }
    wormedit::workspace_binding_validation_cache_t binding_validation;
    wormedit::workspace_bms_branch_outline_cache_t branch_outline;
    struct panes_t {
        compose_scene_cache_t first_scene, second_scene;
        std::optional<wormedit::editor_interaction_state_t> first_state, second_state;
        void reset();
    };
    wormedit::keyed_cache_t<
        std::tuple<wormedit::document_stamp_t, wormedit::chart_id_t, wormedit::chart_id_t>,
        panes_t> panes;
    double shared_scroll_measures{};
    double pixels_per_measure{wormedit::default_pixels_per_standard_measure};
    std::vector<wormedit::chart_id_t> available_charts;
    struct diagnostic_rows_t {
        std::vector<std::string> labels;
        std::vector<ui::compare_diagnostic_view_t> rows;
    };
    wormedit::keyed_cache_t<wormedit::document_stamp_t, wormedit::diagnostic_list_t> diagnostics;
    wormedit::keyed_cache_t<std::pair<wormedit::chart_id_t, std::uint64_t>,
                           diagnostic_rows_t> diagnostics_projection;
};

void cache_compare_diagnostics(compare_scene_cache_t& cache,
                               const wormedit::song_document_t& document);
[[nodiscard]] auto build_workspace_frame_model(
    compare_scene_cache_t& cache, const wormedit::workspace_controller_t& workspace,
    const wormedit::song_document_t& document, std::uint64_t session_generation = 0,
    std::optional<wormedit::branch_id_t> authoring_chart = {})
    -> wormedit::workspace_view_outcome_t;

[[nodiscard]] inline auto choose_compare_chart_pair(
    std::span<const wormedit::chart_id_t> available,
    std::optional<wormedit::chart_id_t> active,
    std::span<const wormedit::chart_id_t> comparison)
    -> std::optional<std::pair<wormedit::chart_id_t, wormedit::chart_id_t>> {
    if (available.size() < 2) return std::nullopt;
    auto first = active && std::ranges::find(available, *active) != available.end()
                     ? *active
                     : (!comparison.empty() ? comparison.front() : available.front());
    if (std::ranges::find(available, first) == available.end()) first = available.front();
    auto second = std::ranges::find_if(comparison, [first](auto chart) {
        return chart != first;
    });
    if (second != comparison.end() && std::ranges::find(available, *second) != available.end())
        return std::pair{first, *second};
    const auto other = std::ranges::find_if(available, [first](auto chart) {
        return chart != first;
    });
    if (other == available.end()) return std::nullopt;
    return std::pair{first, *other};
}

[[nodiscard]] auto compare_local_difference_positions(
    const wormedit::chart_comparison_t& comparison, wormedit::chart_id_t chart)
    -> std::vector<wormedit::local_position_t>;

void populate_compare_view(frontend_workspace_view_t& result,
                           frontend_workspace_view_t& first_pane,
                           frontend_workspace_view_t& second_pane,
                           wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compare_scene_cache_t& cache,
                           application_command_state_t& application_state,
                           int window_width, int window_height);

} // namespace wormedit::sys
