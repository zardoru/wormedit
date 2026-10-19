#include "ui/nuklear/setup_state.h"

bool worm_ui_nuklear_setup_policy_values_valid(
    WormUiStepmaniaFileProfile file_profile, WormUiStepmaniaTimingSyntax timing_syntax,
    WormUiStepmaniaImplementation implementation) {
    return file_profile >= WORM_UI_STEPMANIA_FILE_SM &&
           file_profile <= WORM_UI_STEPMANIA_FILE_SSC &&
           timing_syntax >= WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS &&
           timing_syntax <= WORM_UI_STEPMANIA_TIMING_LEGACY_NEGATIVE_STOP &&
           implementation >= WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY &&
           implementation <= WORM_UI_STEPMANIA_IMPLEMENTATION_ETTERNA_0_75_1;
}

namespace {
bool project_rows_valid(const WormUiSetupView& view) {
    if (view.chart_row_count > WORM_UI_PROJECT_CHART_CAPACITY ||
        (view.chart_row_count != 0 && !view.charts)) return false;
    for (size_t index = 0; index < view.chart_row_count; ++index) {
        const auto& row = view.charts[index];
        if (!row.chart_id || !row.name || !row.author || !row.rating || !row.mode ||
            !row.output_groups || !row.length || !row.diagnostic) return false;
    }
    return true;
}

bool output_group_rows_valid(const WormUiSetupView& view) {
    if (view.output_group_count != 0 && !view.output_groups) return false;
    for (size_t index = 0; index < view.output_group_count; ++index) {
        const auto& group = view.output_groups[index];
        if (!group.name || !group.path || !group.preview_status ||
            (group.chart_count != 0 && !group.chart_ids)) return false;
        if (group.family == 2 &&
            (!group.has_stepmania_policy ||
             !worm_ui_nuklear_setup_policy_values_valid(
                 group.stepmania_file_profile, group.stepmania_timing_syntax,
                 group.stepmania_implementation))) return false;
    }
    return true;
}
}

bool worm_ui_nuklear_setup_view_valid(const WormUiSetupView* view) {
    return view &&
           (!view->visible || (project_rows_valid(*view) && output_group_rows_valid(*view)));
}

#include <algorithm>

bool worm_ui_nuklear_project_chart_selected(const project_chart_selection_t& state, std::uint64_t id) {
    return std::find(state.ids.begin(), state.ids.begin() + state.count, id) != state.ids.begin() + state.count;
}

void worm_ui_nuklear_toggle_project_chart_selection(project_chart_selection_t& state, std::uint64_t id) {
    const auto end = state.ids.begin() + state.count;
    const auto found = std::find(state.ids.begin(), end, id);
    if (found != end) {
        std::move(found + 1, end, found);
        --state.count;
    } else if (id && state.count < state.ids.size()) state.ids[state.count++] = id;
}

namespace {

auto shown_index(std::span<const std::uint64_t> shown, std::uint64_t id) -> std::size_t {
    return static_cast<std::size_t>(std::find(shown.begin(), shown.end(), id) - shown.begin());
}

void select_only(project_chart_selection_t& state, std::uint64_t id) {
    state.ids[0] = id;
    state.count = id ? 1 : 0;
}

void select_range(project_chart_selection_t& state, std::span<const std::uint64_t> shown,
                  std::size_t first, std::size_t last) {
    state.count = 0;
    for (auto index = std::min(first, last); index <= std::max(first, last); ++index)
        if (shown[index] && state.count < state.ids.size()) state.ids[state.count++] = shown[index];
}

} // namespace

void worm_ui_nuklear_click_project_chart(project_chart_selection_t& state,
    std::span<const std::uint64_t> shown, std::uint64_t id, bool ctrl, bool shift) {
    if (!id) return;
    const auto found = shown_index(shown, id);
    const bool anchor_shown = state.anchor && shown_index(shown, state.anchor) < shown.size();
    if (shift && anchor_shown && found < shown.size()) {
        select_range(state, shown, shown_index(shown, state.anchor), found);
        return;
    }
    if (ctrl) {
        worm_ui_nuklear_toggle_project_chart_selection(state, id);
    } else {
        select_only(state, id);
    }
    state.anchor = id;
}

void worm_ui_nuklear_toggle_all_project_charts(project_chart_selection_t& state,
    std::span<const std::uint64_t> shown) {
    const auto summary = worm_ui_nuklear_project_selection_summary(state, shown);
    state.count = 0;
    if (summary == project_selection_summary_t::all) return;
    for (const auto id : shown) {
        if (id && state.count < state.ids.size()) state.ids[state.count++] = id;
    }
}

project_selection_summary_t worm_ui_nuklear_project_selection_summary(
    const project_chart_selection_t& state, std::span<const std::uint64_t> shown) {
    std::size_t selected = 0;
    for (const auto id : shown)
        if (worm_ui_nuklear_project_chart_selected(state, id)) ++selected;
    if (selected == 0) return project_selection_summary_t::none;
    return selected == shown.size() ? project_selection_summary_t::all : project_selection_summary_t::partial;
}

void worm_ui_nuklear_reconcile_project_selection(project_chart_selection_t& state, const WormUiSetupView& view) {
    if (state.song != view.song_id || state.session_generation != view.session_generation) {
        state = {.song = view.song_id, .session_generation = view.session_generation};
    }
    const auto exists = [&](std::uint64_t id) {
        for (std::size_t index = 0; index < view.chart_row_count; ++index)
            if (view.charts[index].chart_id == id) return true;
        return false;
    };
    std::size_t kept{};
    for (std::size_t index = 0; index < state.count; ++index)
        if (exists(state.ids[index])) state.ids[kept++] = state.ids[index];
    state.count = kept;
    if (!exists(state.anchor)) state.anchor = 0;
    if (!exists(state.pending_delete) || state.pending_delete_revision != view.document_revision)
        state.pending_delete = 0;
}

namespace {
bool chart_row_exists(const WormUiSetupView& view, std::uint64_t id) {
    if (!id || !view.charts || view.chart_row_count > WORM_UI_PROJECT_CHART_CAPACITY) return false;
    return std::any_of(view.charts, view.charts + view.chart_row_count,
        [id](const auto& row) { return row.chart_id == id; });
}
}

bool worm_ui_nuklear_begin_chart_drag(project_chart_drag_t& state, const WormUiSetupView& view,
    std::uint64_t source) {
    state = {};
    if (!view.visible || !chart_row_exists(view, source)) return false;
    state = {view.song_id, view.session_generation, view.document_revision, source};
    return true;
}

void worm_ui_nuklear_reconcile_chart_drag(project_chart_drag_t& state, const WormUiSetupView& view) {
    if (!view.visible || state.song != view.song_id || state.session_generation != view.session_generation ||
        state.revision != view.document_revision || !chart_row_exists(view, state.source) ||
        (state.target && !chart_row_exists(view, state.target))) state = {};
}

void worm_ui_nuklear_target_chart_drag(project_chart_drag_t& state, const WormUiSetupView& view,
    std::uint64_t target, bool after) {
    worm_ui_nuklear_reconcile_chart_drag(state, view);
    state.target = state.source && chart_row_exists(view, target) ? target : 0;
    state.after = after;
}

auto worm_ui_nuklear_finish_chart_drag(project_chart_drag_t& state, const WormUiSetupView& view)
    -> WormUiProjectChartAction {
    worm_ui_nuklear_reconcile_chart_drag(state, view);
    WormUiProjectChartAction result{};
    if (state.source && state.target && state.source != state.target)
        result = {.kind = WORM_UI_PROJECT_CHART_REORDER, .document_revision = state.revision,
            .session_generation = state.session_generation, .chart_id = state.source,
            .target_chart_id = state.target, .insert_after = state.after};
    state = {};
    return result;
}
