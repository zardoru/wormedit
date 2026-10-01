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
}

bool worm_ui_nuklear_setup_view_valid(const WormUiSetupView* view) {
    return view && (!view->visible ||
        (view->stepmania_policy_status && project_rows_valid(*view) &&
         worm_ui_nuklear_setup_policy_values_valid(view->stepmania_file_profile,
            view->stepmania_timing_syntax, view->stepmania_implementation)));
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
    if (!exists(state.pending_delete) || state.pending_delete_revision != view.document_revision)
        state.pending_delete = 0;
}
