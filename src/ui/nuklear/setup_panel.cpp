#include "ui/nuklear/chrome.h"
#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/setup_state.h"
#include "ui/nuklear/tooltip.h"

#include "wormedit/nuklear_config.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <SDL3/SDL_keyboard.h>

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
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, project_state.author, sizeof(project_state.author), nk_filter_default);
    nk_layout_row_dynamic(context, 26, 4);
    nk_checkbox_label(context, "Set rating", &project_state.edit_rating);
    nk_checkbox_label(context, "Unset rating", &project_state.unset_rating);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, project_state.rating_numerator, sizeof(project_state.rating_numerator), nk_filter_default);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, project_state.rating_denominator, sizeof(project_state.rating_denominator), nk_filter_default);
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
    if (wormedit::ui::chrome::Button{"Open", wormedit::ui::chrome::ButtonKind::secondary, "Open in Compose"}.render(context))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_OPEN, row.chart_id);
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

// Fixed table template: grip, checkbox, eight text columns sharing the rest, and actions.
constexpr float grip_width = 18.0f;
constexpr float checkbox_width = 26.0f;
constexpr float actions_width = 64.0f;
constexpr float cell_padding = 6.0f;
// Name, Author, Rating, Mode, Keys, Groups, Notes, Length.
constexpr std::array<float, 8> text_shares{22.0f, 18.0f, 8.0f, 12.0f, 6.0f, 14.0f, 8.0f, 8.0f};
constexpr std::size_t column_count = 11;
using column_widths_t = std::array<float, 8>;
using table_ratios_t = std::array<float, column_count>;

auto column_widths(float available) -> column_widths_t {
    const auto flexible = std::max(0.0f, available - grip_width - checkbox_width - actions_width);
    constexpr float share_total = 96.0f;
    column_widths_t widths{};
    for (std::size_t index = 0; index < widths.size(); ++index)
        widths[index] = flexible * text_shares[index] / share_total;
    return widths;
}

// Every table row uses the same fractions of the panel width, so the header and rows align.
auto table_ratios(float available, const column_widths_t& widths) -> table_ratios_t {
    const auto total = std::max(1.0f, available);
    table_ratios_t ratios{grip_width / total, checkbox_width / total};
    for (std::size_t index = 0; index < widths.size(); ++index) ratios[index + 2] = widths[index] / total;
    ratios[column_count - 1] = actions_width / total;
    return ratios;
}

void begin_table_row(nk_context* context, float height, const table_ratios_t& ratios) {
    nk_layout_row(context, NK_DYNAMIC, height, static_cast<int>(column_count), ratios.data());
}

// Shortens to the cell with "…" and returns the text to draw; a fit leaves it unchanged.
auto fit_cell_text(const nk_user_font* font, const char* text, float width) -> std::string {
    constexpr const char* ellipsis = "\xE2\x80\xA6";
    const auto length = static_cast<int>(std::strlen(text));
    const float budget = width - cell_padding;
    if (font->width(font->userdata, font->height, text, length) <= budget) return text;
    const auto ellipsis_width = font->width(font->userdata, font->height, ellipsis, 3);
    int keep = 0;
    for (int end = 1; end < length; ++end) {
        if ((static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) continue;
        if (font->width(font->userdata, font->height, text, end) + ellipsis_width > budget) break;
        keep = end;
    }
    return std::string(text, static_cast<std::size_t>(keep)) + ellipsis;
}

// A text cell that ellipsizes and shows the full text in a tooltip when it was shortened.
void text_cell(nk_context* context, const char* text, float width) {
    const auto shown = fit_cell_text(context->style.font, text, width);
    nk_label(context, shown.c_str(), NK_TEXT_LEFT);
    if (shown != text && nk_widget_is_hovered(context))
        wormedit::ui::nuklear::tooltip::text(context, text);
}

auto shift_or_ctrl(int modifiers) -> std::pair<bool, bool> {
    return {(modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0};
}

void render_table_header(nk_context* context, const std::array<std::uint64_t, WORM_UI_PROJECT_CHART_CAPACITY>& shown,
    std::size_t shown_count, const column_widths_t& widths, const table_ratios_t& ratios) {
    const auto summary = worm_ui_nuklear_project_selection_summary(project_state.selection, {shown.data(), shown_count});
    begin_table_row(context, 22, ratios);
    nk_label(context, "", NK_TEXT_LEFT);
    const auto state = summary == project_selection_summary_t::all ? wormedit::ui::chrome::CheckState::on
        : summary == project_selection_summary_t::partial ? wormedit::ui::chrome::CheckState::mixed
        : wormedit::ui::chrome::CheckState::off;
    if (wormedit::ui::chrome::Checkbox{"", state, "Select all charts"}.render(context))
        worm_ui_nuklear_toggle_all_project_charts(project_state.selection, {shown.data(), shown_count});
    std::size_t column = 0;
    for (const auto* heading : {"Name", "Author", "Rating", "Mode", "Keys", "Groups", "Notes", "Length"})
        text_cell(context, heading, widths[column++]);
    nk_label(context, "", NK_TEXT_LEFT);
}

void render_chart_row(nk_context* context, const WormUiSetupView& view,
    const WormUiProjectChartRow& row, const std::array<std::uint64_t, WORM_UI_PROJECT_CHART_CAPACITY>& shown,
    std::size_t shown_count, const column_widths_t& widths, const table_ratios_t& ratios,
    WormUiWorkspaceAction& action) {
    begin_table_row(context, 26, ratios);
    nk_label(context, "", NK_TEXT_LEFT);
    const bool selected = worm_ui_nuklear_project_chart_selected(project_state.selection, row.chart_id);
    if (wormedit::ui::chrome::Checkbox{"", selected ? wormedit::ui::chrome::CheckState::on : wormedit::ui::chrome::CheckState::off,
            "Select chart"}.render(context))
        worm_ui_nuklear_click_project_chart(project_state.selection, {shown.data(), shown_count}, row.chart_id, true, false);
    nk_bool name_selected = selected;
    const auto name = fit_cell_text(context->style.font, row.name, widths[0]);
    if (nk_selectable_label(context, name.c_str(), NK_TEXT_LEFT, &name_selected)) {
        const auto modifiers = SDL_GetModState();
        const auto [ctrl, shift] = shift_or_ctrl(modifiers);
        worm_ui_nuklear_click_project_chart(project_state.selection, {shown.data(), shown_count}, row.chart_id, ctrl, shift);
    }
    if (name != row.name && nk_widget_is_hovered(context))
        wormedit::ui::nuklear::tooltip::text(context, row.name);
    text_cell(context, row.author, widths[1]);
    text_cell(context, row.rating, widths[2]);
    text_cell(context, row.mode, widths[3]);
    nk_labelf(context, NK_TEXT_LEFT, "%u", row.key_count);
    text_cell(context, row.output_groups, widths[5]);
    nk_labelf(context, NK_TEXT_LEFT, "%llu", static_cast<unsigned long long>(row.note_count));
    text_cell(context, row.length, widths[7]);
    nk_label(context, "", NK_TEXT_LEFT);
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
    std::array<std::uint64_t, WORM_UI_PROJECT_CHART_CAPACITY> shown{};
    const auto shown_count = std::min<std::size_t>(view.chart_row_count, shown.size());
    for (std::size_t index = 0; index < shown_count; ++index) shown[index] = view.charts[index].chart_id;

    constexpr float heading_ratios[]{0.85f, 0.15f};
    nk_layout_row(context, NK_DYNAMIC, 28, 2, heading_ratios);
    nk_labelf(context, NK_TEXT_LEFT, "%zu charts · %zu selected", view.chart_count,
              project_state.selection.count);
    if (wormedit::ui::chrome::Button{"+", wormedit::ui::chrome::ButtonKind::primary, "New chart"}.render(context))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_NEW, 0);
    if (view.charts_truncated) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Showing the first 256 charts; the project contains additional charts.", NK_TEXT_LEFT);
    }
    render_project_batch(context, view, action);
    // The table fills the panel's row, not the whole window: a one-column row supplies that width.
    nk_layout_row_dynamic(context, 22, 1);
    const auto available = nk_widget_width(context);
    const auto widths = column_widths(available);
    const auto ratios = table_ratios(available, widths);
    render_table_header(context, shown, shown_count, widths, ratios);
    for (size_t index = 0; index < shown_count; ++index)
        render_chart_row(context, view, view.charts[index], shown, shown_count, widths, ratios, action);
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
    // Breathing room so the policy form reads as its own section, not as chart-list rows.
    nk_layout_row_dynamic(context, 18, 1);
    nk_spacing(context, 1);
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
