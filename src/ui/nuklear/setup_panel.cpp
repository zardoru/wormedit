#include "ui/nuklear/chrome.h"
#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/setup_state.h"

#include "wormedit/nuklear_config.h"

#include <array>
#include <algorithm>

namespace {

void publish_policy_action(WormUiWorkspaceAction* action, int file_profile, int timing_syntax,
                           int implementation) {
    if (!action || action->kind != WORM_UI_WORKSPACE_ACTION_NONE)
        return;
    const auto file = static_cast<WormUiStepmaniaFileProfile>(file_profile);
    const auto timing = static_cast<WormUiStepmaniaTimingSyntax>(timing_syntax);
    const auto target = static_cast<WormUiStepmaniaImplementation>(implementation);
    if (!worm_ui_nuklear_setup_policy_values_valid(file, timing, target))
        return;
    action->kind = WORM_UI_WORKSPACE_ACTION_SET_STEPMANIA_EXPORT_POLICY;
    action->stepmania_file_profile = file;
    action->stepmania_timing_syntax = timing;
    action->stepmania_implementation = target;
}

struct project_panel_state_t {
    project_chart_selection_t selection;
    char author[1024]{};
    char rating_numerator[24]{"0"};
    char rating_denominator[24]{"1"};
    nk_bool edit_author{};
    nk_bool edit_rating{};
    nk_bool unset_rating{};
};
project_panel_state_t project_state;

void publish_chart_action(const WormUiSetupView& view, WormUiWorkspaceAction& action,
    WormUiProjectChartActionKind kind, uint64_t chart) {
    if (action.project_chart.kind != WORM_UI_PROJECT_CHART_NONE) return;
    action.project_chart.kind = kind;
    action.project_chart.chart_id = chart;
    action.project_chart.document_revision = view.document_revision;
    action.project_chart.session_generation = view.session_generation;
}

void render_project_batch(nk_context* context, const WormUiSetupView& view, WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(context, 22, 1);
    nk_labelf(context, NK_TEXT_LEFT, "%zu charts selected", project_state.selection.count);
    if (project_state.selection.count < 2) return;
    nk_layout_row_dynamic(context, 26, 2);
    nk_checkbox_label(context, "Set author", &project_state.edit_author);
    nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, project_state.author, sizeof(project_state.author), nk_filter_default);
    nk_layout_row_dynamic(context, 26, 4);
    nk_checkbox_label(context, "Set rating", &project_state.edit_rating);
    nk_checkbox_label(context, "Unset rating", &project_state.unset_rating);
    nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, project_state.rating_numerator, sizeof(project_state.rating_numerator), nk_filter_default);
    nk_edit_string_zero_terminated(context, NK_EDIT_FIELD, project_state.rating_denominator, sizeof(project_state.rating_denominator), nk_filter_default);
    nk_layout_row_dynamic(context, 28, 1);
    if (nk_button_label(context, "Apply to selected charts")) {
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_BATCH_METADATA, 0);
        auto& batch = action.project_chart;
        batch.selected_count = project_state.selection.count;
        std::copy_n(project_state.selection.ids.begin(), batch.selected_count, batch.selected_chart_ids);
        batch.edit_author = project_state.edit_author;
        batch.author = project_state.author;
        batch.edit_rating = project_state.edit_rating;
        batch.unset_rating = project_state.unset_rating;
        batch.rating_numerator = project_state.rating_numerator;
        batch.rating_denominator = project_state.rating_denominator;
    }
}

void render_delete_confirmation(nk_context* context, const WormUiSetupView& view,
    const WormUiProjectChartRow& row, WormUiWorkspaceAction& action) {
    if (project_state.selection.pending_delete != row.chart_id) return;
    nk_layout_row_dynamic(context, 38, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Delete %s? Removes %llu notes; group memberships: %s", row.name,
        static_cast<unsigned long long>(row.note_count), row.output_groups);
    nk_layout_row_dynamic(context, 28, 2);
    if (nk_button_label(context, "Confirm Delete")) {
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_DELETE, row.chart_id);
        project_state.selection.pending_delete = 0;
    }
    if (nk_button_label(context, "Cancel Delete")) project_state.selection.pending_delete = 0;
}

void render_chart_actions(nk_context* context, const WormUiSetupView& view,
    const WormUiProjectChartRow& row, WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(context, 25, 6);
    if (nk_button_label(context, "Open in Compose")) publish_chart_action(view, action, WORM_UI_PROJECT_CHART_OPEN, row.chart_id);
    if (nk_button_label(context, "Duplicate")) publish_chart_action(view, action, WORM_UI_PROJECT_CHART_DUPLICATE, row.chart_id);
    if (nk_button_label(context, "Delete")) {
        project_state.selection.pending_delete = row.chart_id;
        project_state.selection.pending_delete_revision = view.document_revision;
    }
    if (nk_button_label(context, "Up")) publish_chart_action(view, action, WORM_UI_PROJECT_CHART_UP, row.chart_id);
    if (nk_button_label(context, "Down")) publish_chart_action(view, action, WORM_UI_PROJECT_CHART_DOWN, row.chart_id);
    nk_label(context, "", NK_TEXT_LEFT);
    render_delete_confirmation(context, view, row, action);
}

void render_chart_row(nk_context* context, const WormUiSetupView& view,
    const WormUiProjectChartRow& row, WormUiWorkspaceAction& action) {
    nk_layout_row_dynamic(context, 26, 8);
    nk_bool selected = worm_ui_nuklear_project_chart_selected(project_state.selection, row.chart_id);
    if (nk_checkbox_label(context, row.name, &selected))
        worm_ui_nuklear_toggle_project_chart_selection(project_state.selection, row.chart_id);
    nk_label(context, row.author, NK_TEXT_LEFT);
    nk_label(context, row.rating, NK_TEXT_LEFT);
    nk_label(context, row.mode, NK_TEXT_LEFT);
    nk_labelf(context, NK_TEXT_LEFT, "%u keys", row.key_count);
    nk_label(context, row.output_groups, NK_TEXT_LEFT);
    nk_labelf(context, NK_TEXT_LEFT, "%llu notes", static_cast<unsigned long long>(row.note_count));
    nk_label(context, row.length, NK_TEXT_LEFT);
    render_chart_actions(context, view, row, action);
    if (row.diagnostic && row.diagnostic[0]) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label_colored(context, row.diagnostic, NK_TEXT_LEFT, nk_rgb(235, 150, 120));
    }
}

void render_project_charts(nk_context* context, const WormUiSetupView& view, WormUiWorkspaceAction& action) {
    if (project_state.selection.song != view.song_id ||
        project_state.selection.session_generation != view.session_generation) project_state = {};
    worm_ui_nuklear_reconcile_project_selection(project_state.selection, view);
    nk_layout_row_dynamic(context, 28, 2);
    if (nk_button_label(context, "New Chart")) publish_chart_action(view, action, WORM_UI_PROJECT_CHART_NEW, 0);
    if (nk_button_label(context, "Clear selection")) project_state.selection.count = 0;
    if (view.charts_truncated) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Showing the first 256 charts; the project contains additional charts.", NK_TEXT_LEFT);
    }
    render_project_batch(context, view, action);
    nk_layout_row_dynamic(context, 22, 8);
    for (const auto* heading : {"Name", "Author", "Rating", "Mode", "Keys", "Output groups", "Notes", "Length"})
        nk_label(context, heading, NK_TEXT_LEFT);
    for (size_t index = 0; index < view.chart_row_count; ++index)
        render_chart_row(context, view, view.charts[index], action);
}

} // namespace

void worm_ui_nuklear_render_setup_panel(struct nk_context* context,
                                        const WormUiSetupView* view,
                                        WormUiWorkspaceAction* action) {
    if (!context || !view || !action || !view->visible)
        return;

    nk_layout_row_dynamic(context, 22, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Charts: %zu · Audio assets: %zu · Visual assets: %zu",
              view->chart_count, view->audio_asset_count, view->visual_asset_count);
    render_project_charts(context, *view, *action);
    nk_layout_row_dynamic(context, 26, 1);
    nk_label(context, "StepMania export policy", NK_TEXT_LEFT);

    constexpr std::array file_labels{"SM", "SSC"};
    constexpr std::array timing_labels{"Native WARPS", "Legacy negative BPM",
                                       "Legacy negative STOP"};
    constexpr std::array implementation_labels{"Syntax only", "StepMania 5.1",
                                                "Etterna 0.75.1"};
    int file_profile = static_cast<int>(view->stepmania_file_profile);
    int timing_syntax = static_cast<int>(view->stepmania_timing_syntax);
    int implementation = static_cast<int>(view->stepmania_implementation);

    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "File family", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.72f);
    const auto next_file = wormedit::ui::chrome::safe_combo(context, file_labels.data(), file_labels.size(), file_profile,
                                    24, nk_vec2(280, 100));
    nk_layout_row_end(context);

    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Timing syntax", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.72f);
    const auto next_timing =
        wormedit::ui::chrome::safe_combo(context, timing_labels.data(), timing_labels.size(), timing_syntax, 24,
                 nk_vec2(280, 120));
    nk_layout_row_end(context);

    nk_layout_row_begin(context, NK_DYNAMIC, 28, 2);
    nk_layout_row_push(context, 0.28f);
    nk_label(context, "Implementation target", NK_TEXT_LEFT);
    nk_layout_row_push(context, 0.72f);
    const auto next_implementation =
        wormedit::ui::chrome::safe_combo(context, implementation_labels.data(), implementation_labels.size(),
                 implementation, 24, nk_vec2(280, 120));
    nk_layout_row_end(context);

    if (next_file != file_profile || next_timing != timing_syntax ||
        next_implementation != implementation) {
        publish_policy_action(action, next_file, next_timing, next_implementation);
    }

    nk_layout_row_dynamic(context, 22, 1);
    nk_label_colored(context, view->stepmania_policy_status, NK_TEXT_LEFT,
                     view->stepmania_policy_valid ? nk_rgb(120, 210, 150)
                                                  : nk_rgb(235, 150, 120));
    nk_layout_row_dynamic(context, 38, 1);
    nk_label_wrap(context,
                  "These choices stage a typed conversion request. Export destination and "
                  "publication remain separate transactional steps.");
}
