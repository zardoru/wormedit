#pragma once

#include "sys/compose_view.h"
#include "sys/project/workspace_view.h"

namespace wormedit::sys {

// References to the live synchronous projection inputs; never retained by caches.
struct compose_frame_inputs_t {
    frontend_workspace_view_t& flattened;
    wormedit::document_history_t& history;
    compose_scene_cache_t& cache;
    std::optional<wormedit::editor_interaction_state_t>& retained_state;
    application_command_state_t& application_state;
    const wormedit::timing_lane_pseudonote_outcome_t* const& timing_stage;
    const std::span<const wormedit::local_position_t>& comparison_rows;
    const compose_bga_move_preview_input_t* const& bga_move_preview;

    auto view() const -> wormedit::ui::compose_view_t& {
        return flattened.view.compose;
    }
    [[nodiscard]] auto for_output(frontend_workspace_view_t& output) const -> compose_frame_inputs_t {
        return {output, history, cache, retained_state, application_state, timing_stage,
                comparison_rows, bga_move_preview};
    }
};

// Constness differs between inspection and final interaction publication, not geometry.
template <class View = const wormedit::editor_interaction_view_model_t>
struct compose_chart_scope_t {
    wormedit::chart_scope_t chart_scope;
    View& interaction_view;
    bool downscroll;
    double pixels_per_row;

    [[nodiscard]] static auto from(const wormedit::chart_scope_t& geometry, View& view,
                                   bool direction, double row_pixels) -> compose_chart_scope_t {
        return {geometry, view, direction, row_pixels};
    }
    template <class OtherView>
    [[nodiscard]] auto for_view(OtherView& view) const -> compose_chart_scope_t<OtherView> {
        return for_view(view, chart_scope.measures);
    }
    // Render indexes may include the virtual tail; do not fall back to the authored index.
    template <class OtherView>
    [[nodiscard]] auto for_view(OtherView& view, const wormedit::measure_index_t& measures) const
        -> compose_chart_scope_t<OtherView> {
        return {{chart_scope.chart, measures}, view, downscroll, pixels_per_row};
    }
};

// One frame/scope bundle; only the interaction-view constness varies by phase.
template <class View = const wormedit::editor_interaction_view_model_t>
struct compose_projection_context_t {
    const compose_frame_inputs_t& frame;
    const compose_chart_scope_t<View>& scope;

    [[nodiscard]] static auto from(const compose_frame_inputs_t& frame_inputs,
                                   const compose_chart_scope_t<View>& chart_scope)
        -> compose_projection_context_t {
        return {frame_inputs, chart_scope};
    }
    [[nodiscard]] auto history() const -> const wormedit::document_history_t& { return frame.history; }
    [[nodiscard]] auto cache() const -> const compose_scene_cache_t& { return frame.cache; }
};

} // namespace wormedit::sys
