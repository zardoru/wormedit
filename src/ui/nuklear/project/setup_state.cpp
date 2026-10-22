#include "ui/nuklear/project/setup_state.h"
#include <algorithm>

namespace {
bool project_rows_valid(const wormedit::ui::setup_view_t& view) {
    return std::ranges::all_of(view.charts, [](const auto& row) { return row.chart_id.value != 0; });
}

bool output_group_rows_valid(const wormedit::ui::setup_view_t& view) {
    for (const auto& group : view.output_groups) {
        if (group.family == wormedit::format_family_t::stepmania && !group.stepmania) return false;
        if (group.stepmania &&
            (group.stepmania->file_profile > wormedit::stepmania_profile_t::ssc ||
             group.stepmania->timing_syntax < wormedit::stepmania_timing_syntax_t::native_warps ||
             group.stepmania->timing_syntax > wormedit::stepmania_timing_syntax_t::legacy_negative_stop ||
             group.stepmania->implementation_profile > wormedit::stepmania_implementation_profile_t::etterna_0_75_1))
            return false;
    }
    return true;
}
}

bool worm_ui_nuklear_setup_view_valid(const wormedit::ui::setup_view_t* view) {
    return view &&
           (!view->visible || (project_rows_valid(*view) && output_group_rows_valid(*view)));
}

bool worm_ui_nuklear_project_chart_selected(const project_chart_selection_t& state, std::uint64_t id) {
    return std::ranges::find(state.ids, id) != state.ids.end();
}

void worm_ui_nuklear_toggle_project_chart_selection(project_chart_selection_t& state, std::uint64_t id) {
    const auto found = std::ranges::find(state.ids, id);
    if (found != state.ids.end()) state.ids.erase(found);
    else if (id) state.ids.push_back(id);
}

namespace {

auto shown_index(std::span<const std::uint64_t> shown, std::uint64_t id) -> std::size_t {
    return static_cast<std::size_t>(std::find(shown.begin(), shown.end(), id) - shown.begin());
}

void select_only(project_chart_selection_t& state, std::uint64_t id) {
    state.ids.clear();
    if (id) state.ids.push_back(id);
}

void select_range(project_chart_selection_t& state, std::span<const std::uint64_t> shown,
                  std::size_t first, std::size_t last) {
    state.ids.clear();
    for (auto index = std::min(first, last); index <= std::max(first, last); ++index)
        if (shown[index]) state.ids.push_back(shown[index]);
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
    state.ids.clear();
    if (summary == project_selection_summary_t::all) return;
    for (const auto id : shown) {
        if (id) state.ids.push_back(id);
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

void worm_ui_nuklear_reconcile_project_selection(project_chart_selection_t& state, const wormedit::ui::setup_view_t& view) {
    if (state.song != view.song_id.value || state.session_generation != view.session_generation) {
        state = {.song = view.song_id.value, .session_generation = view.session_generation};
    }
    const auto exists = [&](std::uint64_t id) {
        for (std::size_t index = 0; index < view.charts.size(); ++index)
            if (view.charts[index].chart_id.value == id) return true;
        return false;
    };
    std::size_t kept{};
    for (std::size_t index = 0; index < state.ids.size(); ++index)
        if (exists(state.ids[index])) state.ids[kept++] = state.ids[index];
    state.ids.resize(kept);
    if (!exists(state.anchor)) state.anchor = 0;
    if (!exists(state.pending_delete) || state.pending_delete_revision != view.document_revision)
        state.pending_delete = 0;
}

namespace {
bool chart_row_exists(const wormedit::ui::setup_view_t& view, std::uint64_t id) {
    if (!id) return false;
    return std::ranges::any_of(view.charts,
        [id](const auto& row) { return row.chart_id.value == id; });
}
}

bool worm_ui_nuklear_begin_chart_drag(project_chart_drag_t& state, const wormedit::ui::setup_view_t& view,
    std::uint64_t source) {
    state = {};
    if (!view.visible || !chart_row_exists(view, source)) return false;
    state = {view.song_id.value, view.session_generation, view.document_revision, source};
    return true;
}

void worm_ui_nuklear_reconcile_chart_drag(project_chart_drag_t& state, const wormedit::ui::setup_view_t& view) {
    if (!view.visible || state.song != view.song_id.value || state.session_generation != view.session_generation ||
        state.revision != view.document_revision || !chart_row_exists(view, state.source) ||
        (state.target && !chart_row_exists(view, state.target))) state = {};
}

void worm_ui_nuklear_target_chart_drag(project_chart_drag_t& state, const wormedit::ui::setup_view_t& view,
    std::uint64_t target, bool after) {
    worm_ui_nuklear_reconcile_chart_drag(state, view);
    state.target = state.source && chart_row_exists(view, target) ? target : 0;
    state.after = after;
}

auto worm_ui_nuklear_finish_chart_drag(project_chart_drag_t& state, const wormedit::ui::setup_view_t& view)
    -> wormedit::ui::project_chart_action_t {
    worm_ui_nuklear_reconcile_chart_drag(state, view);
    wormedit::ui::project_chart_action_t result{};
    if (state.source && state.target && state.source != state.target)
        result = {.kind = WORM_UI_PROJECT_CHART_REORDER, .document_revision = state.revision,
            .session_generation = state.session_generation, .chart_id = wormedit::chart_id_t{state.source},
            .target_chart_id = wormedit::chart_id_t{state.target}, .insert_after = state.after};
    state = {};
    return result;
}
