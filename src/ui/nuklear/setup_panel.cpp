#include "ui/nuklear/catalog_view.h"
#include "ui/nuklear/text_fit.h"
#include "ui/nuklear/chrome.h"
#include "ui/nuklear/setup_panel.h"
#include "ui/nuklear/setup_state.h"
#include "ui/nuklear/tooltip.h"
#include "ui/nuklear/context_menu.h"
#include "ui/nuklear/dialog.h"

#include "wormedit/nuklear_config.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <numeric>
#include <span>
#include <string>
#include <utility>

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_video.h>

#include <filesystem>
#include <mutex>

namespace {

void project_section_heading(nk_context* context, const char* text) {
    using namespace wormedit::ui::chrome;
    const auto bounds = nk_widget_bounds(context);
    nk_fill_rect(nk_window_get_canvas(context), bounds, 4.0f, color(UiColor::surface));
    nk_label_colored(context, text, NK_TEXT_LEFT, color(UiColor::text_bright));
}

struct project_panel_state_t {
    project_chart_selection_t selection;
    project_chart_drag_t drag;
    struct nk_vec2 drag_start{};
    bool drag_moved{};
    char author[1024]{};
    char rating_numerator[24]{"0"};
    char rating_denominator[24]{"1"};
    nk_bool edit_author{};
    nk_bool edit_rating{};
    nk_bool unset_rating{};
    wormedit::ui::chrome::ContextMenu row_menu{.title = "Chart actions"};
    wormedit::ui::chrome::Dialog delete_dialog;
    uint64_t menu_chart{};
    bool release_pending{};
    bool group_editor_open{};
    bool group_create{};
    size_t group_index{};
    uint64_t group_revision{};
    char group_name[256]{};
    char group_path[1024]{};
    int group_family{4};
    int group_existing_files{};
    int group_stepmania_file{WORM_UI_STEPMANIA_FILE_SSC};
    int group_stepmania_timing{WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS};
    int group_stepmania_implementation{WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY};
    std::array<nk_bool, WORM_UI_PROJECT_CHART_CAPACITY> group_charts{};
    wormedit::ui::chrome::ContextMenu group_menu{.title = "Export target actions"};
    wormedit::ui::chrome::Dialog group_editor_dialog;
    wormedit::ui::chrome::Dialog group_delete_dialog;
    size_t group_menu_index{};
    bool group_delete_pending{};
    size_t group_plan_index{};
    bool group_plan_expanded{};
    std::string group_picker_error;
    std::string project_directory;
};
project_panel_state_t project_state;

struct output_destination_picker_t {
    std::mutex mutex;
    bool active{};
    bool ready{};
    std::string project_directory;
    std::string path;
    std::string error;
};
output_destination_picker_t output_destination_picker;

void output_destination_picked(void*, const char* const* files, int) {
    std::lock_guard lock{output_destination_picker.mutex};
    output_destination_picker.ready = true;
    if (!files) {
        const auto* error = SDL_GetError();
        output_destination_picker.error = error && error[0] ? error : "Destination picker failed.";
    } else if (files[0]) {
        output_destination_picker.path = files[0];
    }
}

void open_output_destination_picker() {
    {
        std::lock_guard lock{output_destination_picker.mutex};
        if (output_destination_picker.active || project_state.project_directory.empty())
            return;
        output_destination_picker.active = true;
        output_destination_picker.ready = false;
        output_destination_picker.project_directory = project_state.project_directory;
        output_destination_picker.path.clear();
        output_destination_picker.error.clear();
    }
    project_state.group_picker_error.clear();
    const auto file = project_state.group_family == 0 || project_state.group_family == 2;
    if (!file) {
        SDL_ShowOpenFolderDialog(output_destination_picked, nullptr, SDL_GetKeyboardFocus(),
                                 project_state.project_directory.c_str(), false);
        return;
    }
    static constexpr SDL_DialogFileFilter native_filter[] = {
        {"wormedit project", "wormedit"},
    };
    static constexpr SDL_DialogFileFilter stepmania_filter[] = {
        {"StepMania song", "sm;ssc"},
    };
    const auto* filter = project_state.group_family == 0 ? native_filter : stepmania_filter;
    const auto suggested = (std::filesystem::path{project_state.project_directory} /
                            (project_state.group_family == 0 ? "song.wormedit" : "song.ssc"))
                               .string();
    SDL_ShowSaveFileDialog(output_destination_picked, nullptr, SDL_GetKeyboardFocus(), filter, 1,
                           suggested.c_str());
}

void consume_output_destination_picker() {
    std::lock_guard lock{output_destination_picker.mutex};
    if (!output_destination_picker.active || !output_destination_picker.ready) return;
    output_destination_picker.active = false;
    output_destination_picker.ready = false;
    if (!output_destination_picker.error.empty()) {
        project_state.group_picker_error = std::move(output_destination_picker.error);
        return;
    }
    if (output_destination_picker.path.empty() ||
        output_destination_picker.project_directory != project_state.project_directory)
        return;
    const auto base =
        std::filesystem::path{output_destination_picker.project_directory}.lexically_normal();
    const auto picked = std::filesystem::path{output_destination_picker.path}.lexically_normal();
    const auto relative = picked.lexically_relative(base);
    if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") {
        project_state.group_picker_error = "Choose a destination inside the project folder.";
        return;
    }
    const auto text = relative.generic_string();
    if (text.size() >= sizeof(project_state.group_path)) {
        project_state.group_picker_error = "The selected destination path is too long.";
        return;
    }
    std::snprintf(project_state.group_path, sizeof(project_state.group_path), "%s", text.c_str());
}

auto can_open_output_destination_picker() -> bool {
    std::lock_guard lock{output_destination_picker.mutex};
    return !project_state.project_directory.empty() && !output_destination_picker.active;
}

void publish_chart_action(const WormUiSetupView& view, WormUiWorkspaceAction& action,
    WormUiProjectChartActionKind kind, uint64_t chart) {
    if (action.project_chart.kind != WORM_UI_PROJECT_CHART_NONE) return;
    action.project_chart.kind = kind;
    action.project_chart.chart_id = chart;
    action.project_chart.document_revision = view.document_revision;
    action.project_chart.session_generation = view.session_generation;
}

void publish_batch(const WormUiSetupView& view, WormUiWorkspaceAction& action) {
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

void render_project_batch(nk_context* context, const WormUiSetupView& view, WormUiWorkspaceAction& action) {
    if (project_state.selection.count == 0) return;
    nk_layout_row_dynamic(context, 28, 4);
    nk_labelf(context, NK_TEXT_LEFT, "%zu selected", project_state.selection.count);
    if (nk_button_label(context, "Set author…")) {
        project_state.edit_author = !project_state.edit_author;
        project_state.edit_rating = false;
    }
    if (nk_button_label(context, "Set rating…")) {
        project_state.edit_rating = !project_state.edit_rating;
        project_state.edit_author = false;
    }
    if (nk_button_label(context, "Clear selection")) project_state.selection.count = 0;
    if (project_state.edit_author) {
        nk_layout_row_dynamic(context, 28, 2);
        wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            project_state.author, sizeof(project_state.author), nk_filter_default);
        if (nk_button_label(context, "Apply author")) publish_batch(view, action);
    }
    if (project_state.edit_rating) {
        nk_layout_row_dynamic(context, 28, 4);
        nk_checkbox_label(context, "Unset rating", &project_state.unset_rating);
        wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            project_state.rating_numerator, sizeof(project_state.rating_numerator), nk_filter_default);
        wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            project_state.rating_denominator, sizeof(project_state.rating_denominator), nk_filter_default);
        if (nk_button_label(context, "Apply rating")) publish_batch(view, action);
    }
}


void render_chart_actions(nk_context* context, const WormUiSetupView& view,
    const WormUiProjectChartRow& row, WormUiWorkspaceAction& action) {
    using namespace wormedit::ui::chrome;
    constexpr float ratios[]{0.56f, 0.44f};
    nk_layout_row(context, NK_DYNAMIC, 26, 2, ratios);
    if (Button{"Open", ButtonKind::secondary, "Open in Compose"}.render(context))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_OPEN, row.chart_id);
    if (Button{"⋯", ButtonKind::secondary, "Chart actions"}.render(context)) {
        project_state.menu_chart = row.chart_id;
        context_menu_open(project_state.row_menu, context->input.mouse.pos, wormedit::ui::chrome::PopupContextStamp::from(view, row.chart_id));
    }
}

void render_row_menu(nk_context* context, const WormUiSetupView& view, WormUiWorkspaceAction& action) {
    using namespace wormedit::ui::chrome;
    auto& menu = project_state.row_menu;
    if (!context_menu_begin(menu, context, wormedit::ui::chrome::PopupContextStamp::from(view, project_state.menu_chart),
                            nk_vec2(220, context_menu_height(5)))) return;
    const auto id = project_state.menu_chart;
    if (context_menu_item(menu, context, "Open in Compose"))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_OPEN, id);
    if (context_menu_item(menu, context, "Duplicate"))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_DUPLICATE, id);
    if (context_menu_item(menu, context, "Move up"))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_UP, id);
    if (context_menu_item(menu, context, "Move down"))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_DOWN, id);
    if (context_menu_item(menu, context, "Delete…")) {
        project_state.selection.pending_delete = id;
        project_state.selection.pending_delete_revision = view.document_revision;
        project_state.delete_dialog = {};
    }
    context_menu_end(menu, context);
}

void render_delete_confirmation(nk_context* context, const WormUiSetupView& view,
                                WormUiWorkspaceAction& action) {
    using namespace wormedit::ui::chrome;
    const auto id = project_state.selection.pending_delete;
    if (!id) return;
    const auto* row = std::find_if(view.charts, view.charts + view.chart_row_count,
                                 [id](const auto& item) { return item.chart_id == id; });
    if (row == view.charts + view.chart_row_count) return;
    auto& dialog = project_state.delete_dialog;
    if (dialog_begin(dialog, context, "Delete chart", "Delete chart?")) {
        dialog_text(dialog, context, row->name, context->style.text.color);
        dialog_text(dialog, context, "This removes the chart and its notes from the project. You can Undo this change.",
                    context->style.text.color);
        dialog_footer_begin(dialog, context, 2, true);
        if (dialog_button(dialog, context, "Delete", DialogButtonRole::destructive))
            publish_chart_action(view, action, WORM_UI_PROJECT_CHART_DELETE, id);
        (void)dialog_button(dialog, context, "Cancel", DialogButtonRole::secondary);
    }
    if (dialog_end(dialog, context) != DialogResult::none) {
        project_state.selection.pending_delete = 0;
        project_state.release_pending = true;
    }
}

// Fixed table template: grip, checkbox, eight text columns sharing the rest, and actions.
constexpr float grip_width = 18.0f;
constexpr float checkbox_width = 26.0f;
constexpr float actions_width = 100.0f;
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

// A text cell that ellipsizes and shows the full text in a tooltip when it was shortened.
void text_cell(nk_context* context, const char* text, float width) {
    const auto shown = wormedit::ui::chrome::fit_text_with_ellipsis(context->style.font, text, width - cell_padding, wormedit::ui::chrome::ellipsis_overflow_t::suffix);
    nk_label(context, shown.c_str(), NK_TEXT_LEFT);
    if (shown != text && nk_widget_is_hovered(context))
        wormedit::ui::nuklear::tooltip::text(context, text);
}

auto shift_or_ctrl(int modifiers) -> std::pair<bool, bool> {
    return {(modifiers & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (modifiers & SDL_KMOD_SHIFT) != 0};
}

auto table_row_bounds(nk_context* context, const column_widths_t& widths) -> struct nk_rect {
    auto bounds = nk_widget_bounds(context);
    bounds.w = grip_width + checkbox_width + actions_width + std::accumulate(widths.begin(), widths.end(), 0.0f);
    return bounds;
}

void paint_table_row(nk_context* context, struct nk_rect bounds, bool header, bool selected) {
    using namespace wormedit::ui::chrome;
    if (!header && !selected) return;
    auto* canvas = nk_window_get_canvas(context);
    nk_fill_rect(canvas, bounds, 4.0f, color(header ? UiColor::surface : UiColor::selected));
    if (header) nk_stroke_rect(canvas, bounds, 4.0f, 1.0f, color(UiColor::border));
}

void render_chart_grip(nk_context* context, const WormUiSetupView& view, std::uint64_t id) {
    struct nk_rect bounds{};
    const auto widget = nk_widget(&bounds, context);
    using namespace wormedit::ui;
    lucide::draw(context, bounds, lucide::Glyph::grip_vertical,
        chrome::color(project_state.drag.source == id ? chrome::UiColor::accent : chrome::UiColor::text_muted));
    if (widget != NK_WIDGET_VALID) return;
    const bool hovered = nk_input_is_mouse_hovering_rect(&context->input, bounds);
    if (hovered) nuklear::tooltip::text(context, "Drag to reorder chart");
    if (hovered && !worm_ui_nuklear_setup_popup_owns_input() &&
        nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) &&
        worm_ui_nuklear_begin_chart_drag(project_state.drag, view, id)) {
        project_state.drag_start = context->input.mouse.pos;
        project_state.drag_moved = false;
    }
}

void preview_chart_drop(nk_context* context, const WormUiSetupView& view,
                        std::uint64_t id, struct nk_rect bounds) {
    if (!project_state.drag.source || !project_state.drag_moved ||
        !nk_input_is_mouse_hovering_rect(&context->input, context->current->buffer.clip) ||
        !nk_input_is_mouse_hovering_rect(&context->input, bounds)) return;
    const bool after = context->input.mouse.pos.y >= bounds.y + bounds.h * 0.5f;
    worm_ui_nuklear_target_chart_drag(project_state.drag, view, id, after);
    if (!project_state.drag.source || project_state.drag.source == id) return;
    const auto y = after ? bounds.y + bounds.h : bounds.y;
    nk_stroke_line(nk_window_get_canvas(context), bounds.x, y, bounds.x + bounds.w, y, 2.0f,
        wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::accent));
}

void prepare_chart_drag(nk_context* context, const WormUiSetupView& view) {
    worm_ui_nuklear_reconcile_chart_drag(project_state.drag, view);
    if (nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE) ||
        worm_ui_nuklear_setup_popup_owns_input()) project_state.drag = {};
    if (!project_state.drag.source) return;
    project_state.drag.target = 0; // Leaving the rows cancels the drop rather than retaining a target.
    const auto mouse = context->input.mouse.pos;
    project_state.drag_moved = project_state.drag_moved ||
        std::hypot(mouse.x - project_state.drag_start.x, mouse.y - project_state.drag_start.y) >= 3.0f;
}

void finish_chart_drag(nk_context* context, const WormUiSetupView& view, WormUiWorkspaceAction& action) {
    if (!project_state.drag.source || context->input.mouse.buttons[NK_BUTTON_LEFT].down) return;
    if (project_state.drag_moved && nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT) &&
        action.project_chart.kind == WORM_UI_PROJECT_CHART_NONE)
        action.project_chart = worm_ui_nuklear_finish_chart_drag(project_state.drag, view);
    project_state.drag = {};
}

void render_table_header(nk_context* context, const std::array<std::uint64_t, WORM_UI_PROJECT_CHART_CAPACITY>& shown,
    std::size_t shown_count, const column_widths_t& widths, const table_ratios_t& ratios) {
    const auto summary = worm_ui_nuklear_project_selection_summary(project_state.selection, {shown.data(), shown_count});
    begin_table_row(context, 30, ratios);
    paint_table_row(context, table_row_bounds(context, widths), true, false);
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
    begin_table_row(context, 30, ratios);
    const auto row_bounds = table_row_bounds(context, widths);
    const bool selected = worm_ui_nuklear_project_chart_selected(project_state.selection, row.chart_id);
    if (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) &&
        nk_input_is_mouse_hovering_rect(&context->input, row_bounds)) {
        project_state.menu_chart = row.chart_id;
        wormedit::ui::chrome::context_menu_focus(wormedit::ui::chrome::KeyboardContextTarget::charts, row.chart_id);
    }
    const bool keyboard_open = (project_state.menu_chart == row.chart_id ||
        (!project_state.menu_chart && selected)) &&
        wormedit::ui::chrome::context_menu_take_open_request(wormedit::ui::chrome::KeyboardContextTarget::charts);
    if (keyboard_open) {
        project_state.menu_chart = row.chart_id;
        wormedit::ui::chrome::context_menu_open(project_state.row_menu,
            nk_vec2(row_bounds.x, row_bounds.y + row_bounds.h), wormedit::ui::chrome::PopupContextStamp::from(view, row.chart_id), true);
    }
    paint_table_row(context, row_bounds, false, selected);
    render_chart_grip(context, view, row.chart_id);
    if (wormedit::ui::chrome::Checkbox{"", selected ? wormedit::ui::chrome::CheckState::on : wormedit::ui::chrome::CheckState::off,
            "Select chart"}.render(context))
        worm_ui_nuklear_click_project_chart(project_state.selection, {shown.data(), shown_count}, row.chart_id, true, false);
    const auto name_bounds = nk_widget_bounds(context);
    if (nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) &&
        nk_input_is_mouse_hovering_rect(&context->input, name_bounds)) {
        project_state.menu_chart = row.chart_id;
        wormedit::ui::chrome::context_menu_open(project_state.row_menu, context->input.mouse.pos,
                                               wormedit::ui::chrome::PopupContextStamp::from(view, row.chart_id));
    }
    if (nk_input_mouse_clicked(&context->input, NK_BUTTON_DOUBLE, name_bounds))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_OPEN, row.chart_id);
    nk_bool name_selected = selected;
    const auto* label = row.name[0] ? row.name : "Untitled chart";
    const auto name = wormedit::ui::chrome::fit_text_with_ellipsis(context->style.font, label, widths[0] - cell_padding,
        wormedit::ui::chrome::ellipsis_overflow_t::suffix);
    if (nk_selectable_label(context, name.c_str(), NK_TEXT_LEFT, &name_selected)) {
        const auto modifiers = SDL_GetModState();
        const auto [ctrl, shift] = shift_or_ctrl(modifiers);
        worm_ui_nuklear_click_project_chart(project_state.selection, {shown.data(), shown_count}, row.chart_id, ctrl, shift);
    }
    if (name != label && nk_widget_is_hovered(context))
        wormedit::ui::nuklear::tooltip::text(context, label);
    text_cell(context, row.author, widths[1]);
    text_cell(context, row.rating, widths[2]);
    text_cell(context, row.mode, widths[3]);
    nk_labelf(context, NK_TEXT_LEFT, "%u", row.key_count);
    text_cell(context, row.output_groups, widths[5]);
    nk_labelf(context, NK_TEXT_LEFT, "%llu", static_cast<unsigned long long>(row.note_count));
    text_cell(context, row.length, widths[7]);
    char group[64]{};
    std::snprintf(group, sizeof(group), "chart-actions-%llu", static_cast<unsigned long long>(row.chart_id));
    nk_style_push_vec2(context, &context->style.window.group_padding, nk_vec2(0, 0));
    if (nk_group_begin(context, group, NK_WINDOW_NO_SCROLLBAR)) {
        render_chart_actions(context, view, row, action);
        nk_group_end(context);
    }
    nk_style_pop_vec2(context);
    preview_chart_drop(context, view, row.chart_id, row_bounds);
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

    nk_layout_row_template_begin(context, 32);
    nk_layout_row_template_push_dynamic(context);
    nk_layout_row_template_push_static(context, 160);
    nk_layout_row_template_push_static(context, 32);
    nk_layout_row_template_end(context);
    char heading[64];
    std::snprintf(heading, sizeof(heading), "Charts · %zu", view.chart_count);
    project_section_heading(context, heading);
    if (wormedit::ui::chrome::Button{"Import chart…", wormedit::ui::chrome::ButtonKind::secondary,
                                     "Add difficulties from another chart file"}.render(context) &&
        action.chart_import.kind == WORM_UI_CHART_IMPORT_NONE)
        action.chart_import.kind = WORM_UI_CHART_IMPORT_BEGIN;
    if (wormedit::ui::chrome::Button{"+", wormedit::ui::chrome::ButtonKind::primary, "New chart"}.render(context))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_NEW, 0);
    if (view.charts_truncated) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Showing the first 256 charts; the project contains additional charts.", NK_TEXT_LEFT);
    }
    (void)wormedit::ui::chrome::context_menu_update(project_state.row_menu, context);
    (void)wormedit::ui::chrome::popup_update_release_pending(context, project_state.release_pending);
    // The table fills the panel's row, not the whole window: a one-column row supplies that width.
    nk_layout_row_dynamic(context, 22, 1);
    const auto available = nk_widget_width(context);
    const auto widths = column_widths(available);
    const auto ratios = table_ratios(available, widths);
    prepare_chart_drag(context, view);
    render_table_header(context, shown, shown_count, widths, ratios);
    for (size_t index = 0; index < shown_count; ++index)
        render_chart_row(context, view, view.charts[index], shown, shown_count, widths, ratios, action);
    finish_chart_drag(context, view, action);
    render_project_batch(context, view, action);
    render_row_menu(context, view, action);
}


void load_output_group_draft(const WormUiSetupView& view, const WormUiProjectOutputGroupView* group,
                             bool create) {
    auto& state = project_state;
    state.group_editor_open = true;
    state.group_create = create;
    state.group_index = group ? group->document_index : 0;
    state.group_revision = view.document_revision;
    state.group_family = group && group->family <= 4 ? group->family : 4;
    state.group_existing_files = group ? group->existing_files : WORM_UI_EXISTING_FILES_ASK;
    state.group_stepmania_file = group && group->has_stepmania_policy
                                     ? group->stepmania_file_profile
                                     : WORM_UI_STEPMANIA_FILE_SSC;
    state.group_stepmania_timing = group && group->has_stepmania_policy
                                       ? group->stepmania_timing_syntax
                                       : WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS;
    state.group_stepmania_implementation = group && group->has_stepmania_policy
                                               ? group->stepmania_implementation
                                               : WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY;
    state.group_editor_dialog = {};
    std::snprintf(state.group_name, sizeof(state.group_name), "%s",
                  group ? group->name : "New export target");
    std::snprintf(state.group_path, sizeof(state.group_path), "%s",
                  group ? group->path : "exports/bmson");
    state.group_charts.fill(false);
    for (size_t i = 0; i < std::min(view.chart_row_count, size_t{WORM_UI_PROJECT_CHART_CAPACITY}); ++i) {
        state.group_charts[i] = group ? std::ranges::find(
            std::span{group->chart_ids, group->chart_count}, view.charts[i].chart_id) !=
            std::span{group->chart_ids, group->chart_count}.end() : view.charts[i].chart_id == view.selected_chart_id;
    }
}
void begin_output_group_editor(const WormUiSetupView& view, const WormUiProjectOutputGroupView* group) {
    load_output_group_draft(view, group, group == nullptr);
}
auto output_group_name_used(const WormUiSetupView& view, std::string_view name) -> bool {
    return std::ranges::any_of(std::span{view.output_groups, view.output_group_count},
        [name](const auto& group) { return group.name && name == group.name; });
}
void begin_output_group_duplicate(const WormUiSetupView& view,
                                  const WormUiProjectOutputGroupView& group) {
    load_output_group_draft(view, &group, true);
    const auto source = std::string_view{group.name};
    const auto kept = std::min(source.size(), sizeof(project_state.group_name) - size_t{16});
    std::string candidate{source.substr(0, kept)};
    candidate += " copy";
    for (int suffix = 2; output_group_name_used(view, candidate); ++suffix)
        candidate = std::string{source.substr(0, kept)} + " copy " + std::to_string(suffix);
    std::snprintf(project_state.group_name, sizeof(project_state.group_name), "%s", candidate.c_str());
}
void begin_output_group_delete(const WormUiSetupView& view, size_t index) {
    project_state.group_index = index;
    project_state.group_revision = view.document_revision;
    project_state.group_delete_pending = true;
    project_state.group_delete_dialog = {};
}
void publish_output_group_action(const WormUiSetupView& view, WormUiWorkspaceAction& action,
    WormUiProjectOutputGroupActionKind kind, size_t index) {
    if (action.project_output_group.kind != WORM_UI_PROJECT_OUTPUT_GROUP_NONE) return;
    auto& output = action.project_output_group;
    output.kind = kind;
    output.project_id = view.song_id;
    output.document_revision = view.document_revision;
    output.session_generation = view.session_generation;
    output.group_index = index;
    output.name = project_state.group_name;
    output.path = project_state.group_path;
    output.family = static_cast<uint8_t>(project_state.group_family);
    output.destination_kind = project_state.group_family == 0 || project_state.group_family == 2
                                  ? WORM_UI_OUTPUT_DESTINATION_FILE
                                  : WORM_UI_OUTPUT_DESTINATION_DIRECTORY;
    output.existing_files =
        static_cast<WormUiExistingFilesChoice>(project_state.group_existing_files);
    output.has_stepmania_policy = project_state.group_family == 2;
    output.stepmania_file_profile =
        static_cast<WormUiStepmaniaFileProfile>(project_state.group_stepmania_file);
    output.stepmania_timing_syntax =
        static_cast<WormUiStepmaniaTimingSyntax>(project_state.group_stepmania_timing);
    output.stepmania_implementation =
        static_cast<WormUiStepmaniaImplementation>(project_state.group_stepmania_implementation);
    for (size_t i = 0; i < std::min(view.chart_row_count, size_t{WORM_UI_PROJECT_CHART_CAPACITY}); ++i)
        if (project_state.group_charts[i]) output.chart_ids[output.chart_count++] = view.charts[i].chart_id;
}
void render_output_group_members(nk_context* context, const WormUiSetupView& view) {
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Included charts", NK_TEXT_LEFT);
    for (size_t i = 0; i < std::min(view.chart_row_count, size_t{WORM_UI_PROJECT_CHART_CAPACITY}); ++i) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_checkbox_label(context, view.charts[i].name && view.charts[i].name[0] ? view.charts[i].name : "Untitled chart",
            &project_state.group_charts[i]);
    }
}
auto render_output_family_picker(nk_context* context, const WormUiKindCatalogView& catalog,
                                 int selected) -> int {
    const auto label = wormedit::ui::kind_label(catalog, selected, "Unsupported");
    if (wormedit::ui::chrome::safe_combo_begin_label(context, label, nk_vec2(220, 160))) {
        nk_layout_row_dynamic(context, 24, 1);
        for (std::size_t index = 0; index < catalog.count; ++index) {
            const auto& family = catalog.entries[index];
            if (nk_combo_item_label(context, family.label, NK_TEXT_LEFT))
                selected = static_cast<int>(family.id);
        }
        wormedit::ui::chrome::safe_combo_end(context);
    }
    return selected;
}
auto existing_files_label(int policy) -> const char* {
    switch (static_cast<WormUiExistingFilesChoice>(policy)) {
    case WORM_UI_EXISTING_FILES_ASK:
        return "Ask";
    case WORM_UI_EXISTING_FILES_REPLACE:
        return "Replace";
    case WORM_UI_EXISTING_FILES_KEEP:
        return "Keep";
    case WORM_UI_EXISTING_FILES_SAVED:
        break;
    }
    return "Ask";
}
auto render_existing_files_picker(nk_context* context, int selected) -> int {
    if (wormedit::ui::chrome::safe_combo_begin_label(context, existing_files_label(selected),
                                                     nk_vec2(220, 96))) {
        nk_layout_row_dynamic(context, 24, 1);
        for (const auto policy : {WORM_UI_EXISTING_FILES_ASK, WORM_UI_EXISTING_FILES_REPLACE,
                                  WORM_UI_EXISTING_FILES_KEEP})
            if (nk_combo_item_label(context, existing_files_label(policy), NK_TEXT_LEFT))
                selected = policy;
        wormedit::ui::chrome::safe_combo_end(context);
    }
    return selected;
}
void render_stepmania_policy(nk_context* context) {
    constexpr std::array file_labels{"SM", "SSC"};
    constexpr std::array timing_labels{"Native WARPS", "Legacy negative BPM",
                                       "Legacy negative STOP"};
    constexpr std::array implementation_labels{"Syntax only", "StepMania 5.1", "Etterna 0.75.1"};
    constexpr float fields[]{0.22f, 0.78f};
    auto& state = project_state;
    nk_layout_row(context, NK_DYNAMIC, 28, 2, fields);
    nk_label(context, "File profile", NK_TEXT_LEFT);
    state.group_stepmania_file =
        wormedit::ui::chrome::safe_combo(context, file_labels.data(), file_labels.size(),
                                         state.group_stepmania_file, 24, nk_vec2(280, 100));
    nk_layout_row(context, NK_DYNAMIC, 28, 2, fields);
    nk_label(context, "Timing syntax", NK_TEXT_LEFT);
    state.group_stepmania_timing =
        wormedit::ui::chrome::safe_combo(context, timing_labels.data(), timing_labels.size(),
                                         state.group_stepmania_timing, 24, nk_vec2(280, 120));
    nk_layout_row(context, NK_DYNAMIC, 28, 2, fields);
    nk_label(context, "Implementation", NK_TEXT_LEFT);
    state.group_stepmania_implementation = wormedit::ui::chrome::safe_combo(
        context, implementation_labels.data(), implementation_labels.size(),
        state.group_stepmania_implementation, 24, nk_vec2(280, 120));
    if (state.group_stepmania_implementation != WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY) {
        state.group_stepmania_file = WORM_UI_STEPMANIA_FILE_SSC;
        state.group_stepmania_timing = WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS;
    } else if (state.group_stepmania_file == WORM_UI_STEPMANIA_FILE_SM ||
               state.group_stepmania_timing != WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS) {
        state.group_stepmania_implementation = WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY;
    }
}

void render_output_destination(nk_context* context) {
    using namespace wormedit::ui::chrome;
    auto& state = project_state;
    constexpr float destination_fields[]{0.22f, 0.58f, 0.20f};
    nk_layout_row(context, NK_DYNAMIC, 28, 3, destination_fields);
    nk_label(context, "Save to", NK_TEXT_LEFT);
    worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.group_path,
                               sizeof(state.group_path), nk_filter_default);
    const auto can_browse = can_open_output_destination_picker();
    if (Button{"Browse…", ButtonKind::secondary, nullptr, can_browse}.render(context))
        open_output_destination_picker();
    if (!state.group_picker_error.empty())
        dialog_text(state.group_editor_dialog, context, state.group_picker_error.c_str(),
                    color(UiColor::conflict));
    std::string resolved{"Resolved "};
    resolved += state.group_family == 0 || state.group_family == 2 ? "file · " : "folder · ";
    resolved += state.group_path;
    dialog_text(state.group_editor_dialog, context, resolved.c_str(), color(UiColor::text_muted));
}

void render_output_group_form(nk_context* context, const WormUiSetupView& view) {
    using namespace wormedit::ui::chrome;
    auto& state = project_state;
    constexpr float fields[]{0.22f, 0.78f};
    nk_layout_row(context, NK_DYNAMIC, 28, 2, fields);
    nk_label(context, "Name", NK_TEXT_LEFT);
    worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, state.group_name,
                               sizeof(state.group_name), nk_filter_default);
    nk_layout_row(context, NK_DYNAMIC, 28, 2, fields);
    nk_label(context, "Format", NK_TEXT_LEFT);
    const auto previous_family = state.group_family;
    state.group_family = render_output_family_picker(context, view.output_families,
                                                      state.group_family);
    if (state.group_family != previous_family) {
        const auto* suggested = state.group_family == 0 ? "exports/native/song.wormedit"
                               : state.group_family == 1 ? "exports/bms"
                               : state.group_family == 2 ? "exports/stepmania/song.ssc"
                               : state.group_family == 3 ? "exports/osu"
                                                        : "exports/bmson";
        std::snprintf(state.group_path, sizeof(state.group_path), "%s", suggested);
    }
    render_output_destination(context);
    nk_layout_row(context, NK_DYNAMIC, 28, 2, fields);
    nk_label(context, "Existing files", NK_TEXT_LEFT);
    state.group_existing_files = render_existing_files_picker(context, state.group_existing_files);
    if (state.group_family == 2) render_stepmania_policy(context);
    dialog_text(
        state.group_editor_dialog, context,
        state.group_family == 0 || state.group_family == 2
            ? "Song file inside the project. Preview shows copied assets beside it."
            : "Folder inside the project. Chart and asset filenames are derived in Preview.",
        color(UiColor::text_muted));
    render_output_group_members(context, view);
}

void render_output_group_editor(nk_context* context, const WormUiSetupView& view, WormUiWorkspaceAction& action) {
    using namespace wormedit::ui::chrome;
    auto& state = project_state;
    if (!state.group_editor_open) return;
    consume_output_destination_picker();
    if (state.group_revision != view.document_revision) { state.group_editor_open = false; return; }
    char title[sizeof(state.group_name) + 32];
    std::snprintf(title, sizeof(title), state.group_create ? "Create export target" : "Edit export · %s",
                  state.group_create ? "" : state.group_name);
    bool delete_requested = false;
    bool save_requested = false;
    if (dialog_begin(state.group_editor_dialog, context, "Export target editor", title,
                     {.width = 560.0f, .enter_confirms = false})) {
        render_output_group_form(context, view);
        dialog_footer_begin(state.group_editor_dialog, context, state.group_create ? 2 : 3,
                            !state.group_create);
        if (!state.group_create)
            delete_requested = dialog_button(state.group_editor_dialog, context, "Delete…",
                                              DialogButtonRole::destructive);
        (void)dialog_button(state.group_editor_dialog, context, "Cancel", DialogButtonRole::secondary);
        save_requested = dialog_button(state.group_editor_dialog, context, "Save", DialogButtonRole::primary);
    }
    const auto result = dialog_end(state.group_editor_dialog, context);
    if (delete_requested) {
        state.group_editor_open = false;
        begin_output_group_delete(view, state.group_index);
    } else if (save_requested) {
        publish_output_group_action(view, action,
            state.group_create ? WORM_UI_PROJECT_OUTPUT_GROUP_CREATE : WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE,
            state.group_index);
        state.group_editor_open = false;
        state.release_pending = true;
    } else if (result == DialogResult::cancel || result == DialogResult::secondary) {
        state.group_editor_open = false;
        state.release_pending = true;
    }
}
void export_project_group(const WormUiSetupView& view, WormUiWorkspaceAction& action, size_t index,
                          WormUiExistingFilesChoice existing_files) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP;
    action.compose_input_project_id = view.song_id;
    action.compose_input_chart_id = view.selected_chart_id;
    action.compose_input_document_revision = view.document_revision;
    action.compose_output_group_index = index;
    action.compose_existing_files = existing_files;
}
void export_all_project_groups(const WormUiSetupView& view, WormUiWorkspaceAction& action) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_EXPORT_ALL_OUTPUT_GROUPS;
    action.compose_input_project_id = view.song_id;
    action.compose_input_chart_id = view.selected_chart_id;
    action.compose_input_document_revision = view.document_revision;
}
auto output_group_editable(const WormUiSetupView& view,
                           const WormUiProjectOutputGroupView& group) -> bool {
    return !view.charts_truncated && group.chart_count <= WORM_UI_PROJECT_CHART_CAPACITY &&
        std::strlen(group.name) < sizeof(project_state.group_name) &&
        std::strlen(group.path) < sizeof(project_state.group_path);
}
auto output_group_chart_name(const WormUiSetupView& view, uint64_t id) -> const char* {
    const auto* row = std::find_if(view.charts, view.charts + view.chart_row_count,
        [id](const auto& chart) { return chart.chart_id == id; });
    return row == view.charts + view.chart_row_count || !row->name || !row->name[0]
        ? "Untitled chart" : row->name;
}
void render_output_group_badge(nk_context* context, const char* label) {
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    using namespace wormedit::ui::chrome;
    nk_fill_rect(nk_window_get_canvas(context), bounds, 4.0f, color(UiColor::surface_active));
    nk_stroke_rect(nk_window_get_canvas(context), bounds, 4.0f, 1.0f, color(UiColor::border_hover));
    nk_draw_text(nk_window_get_canvas(context), nk_rect(bounds.x + 6.0f, bounds.y, bounds.w - 12.0f, bounds.h),
                 label, static_cast<int>(std::strlen(label)), context->style.font,
                 nk_rgba(0, 0, 0, 0), color(UiColor::text_bright));
}
void render_output_group_chips(nk_context* context, const WormUiSetupView& view,
                               const WormUiProjectOutputGroupView& group, float width) {
    constexpr size_t maximum_chips = 4;
    const auto shown = std::min(group.chart_count, maximum_chips);
    const auto chip_width = std::max(72.0f, std::min(150.0f, width / maximum_chips));
    float x = 80.0f;
    for (size_t index = 0; index < shown; ++index) {
        nk_layout_space_push(context, nk_rect(x, 38.0f, chip_width, 22.0f));
        (void)wormedit::ui::chrome::Chip{
            output_group_chart_name(view, group.chart_ids[index])}.render(context);
        x += chip_width + 4.0f;
    }
    if (group.chart_count == 0) {
        nk_layout_space_push(context, nk_rect(x, 40.0f, 120.0f, 18.0f));
        nk_label_colored(context, "No charts", NK_TEXT_LEFT,
                         wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::text_muted));
    } else if (group.chart_count > shown) {
        char more[32];
        std::snprintf(more, sizeof(more), "+%zu charts", group.chart_count - shown);
        nk_layout_space_push(context, nk_rect(x, 40.0f, 100.0f, 18.0f));
        nk_label(context, more, NK_TEXT_LEFT);
    }
}
void render_output_group_card(nk_context* context, const WormUiSetupView& view,
                              const WormUiProjectOutputGroupView& group,
                              WormUiWorkspaceAction& action) {
    using namespace wormedit::ui::chrome;
    nk_layout_space_begin(context, NK_STATIC, 88.0f, 10);
    const auto space = nk_layout_space_bounds(context);
    const auto origin = nk_layout_space_to_screen(context, nk_vec2(0.0f, 0.0f));
    const auto card = nk_rect(origin.x, origin.y, space.w, 88.0f);
    const auto viewport = wormedit::ui::nuklear::tooltip::viewport();
    const auto width = std::max(1.0f, std::min(card.w, viewport.x + viewport.w - card.x - 8.0f));
    nk_fill_rect(nk_window_get_canvas(context), card, 6.0f, color(UiColor::surface));
    nk_stroke_rect(nk_window_get_canvas(context), card, 6.0f, 1.0f, color(UiColor::border));
    const auto action_x = std::max(290.0f, width - 136.0f);
    const auto name_width = std::max(80.0f, std::min(180.0f, (action_x - 92.0f) * 0.42f));
    const auto path_x = 92.0f + name_width + 8.0f;
    nk_layout_space_push(context, nk_rect(12.0f, 7.0f, 64.0f, 24.0f));
    render_output_group_badge(context, wormedit::ui::kind_label(view.output_families, group.family,
                                                                "Unsupported"));
    nk_layout_space_push(context, nk_rect(84.0f, 8.0f, name_width, 22.0f));
    text_cell(context, group.name, name_width);
    nk_layout_space_push(context, nk_rect(path_x, 8.0f, std::max(1.0f, action_x - path_x - 8.0f), 22.0f));
    std::string destination{"→ "};
    destination += group.destination_kind == WORM_UI_OUTPUT_DESTINATION_FILE ? "File · "
                                                                             : "Folder · ";
    destination += group.path;
    text_cell(context, destination.c_str(), std::max(1.0f, action_x - path_x - 8.0f));
    nk_layout_space_push(context, nk_rect(action_x, 5.0f, 96.0f, 28.0f));
    const auto export_tooltip = std::string{"Publish using saved "} +
                                existing_files_label(group.existing_files) + " policy";
    if (Button{"Export", ButtonKind::primary, export_tooltip.c_str(), group.chart_count != 0}
            .render(context))
        export_project_group(view, action, group.document_index, WORM_UI_EXISTING_FILES_SAVED);
    nk_layout_space_push(context, nk_rect(action_x + 104.0f, 5.0f, 28.0f, 28.0f));
    if (Button{"⋯", ButtonKind::secondary, "Export target actions"}.render(context)) {
        project_state.group_menu_index = group.document_index;
        context_menu_open(project_state.group_menu, context->input.mouse.pos,
            PopupContextStamp::from(view, group.document_index + 1));
    }
    render_output_group_chips(context, view, group, std::max(1.0f, action_x - 92.0f));
    nk_layout_space_push(context, nk_rect(84.0f, 65.0f, std::max(1.0f, width - 100.0f), 18.0f));
    const auto status = std::string_view{group.preview_status ? group.preview_status : ""};
    const auto first_line = status.substr(0, status.find('\n'));
    std::string status_line{first_line.empty() ? "Preview not run" : first_line};
    text_cell(context, status_line.c_str(), std::max(1.0f, width - 100.0f));
    nk_layout_space_end(context);
    if (project_state.group_plan_expanded &&
        project_state.group_plan_index == group.document_index) {
        constexpr std::array action_labels{"new", "replace", "reuse", "copy"};
        for (size_t index = 0; index < group.plan_rows.size(); ++index) {
            const auto& row = group.plan_rows[index];
            char bytes[64];
            std::snprintf(bytes, sizeof(bytes), "%llu bytes",
                          static_cast<unsigned long long>(row.bytes));
            constexpr float columns[]{0.13f, 0.67f, 0.20f};
            nk_layout_row(context, NK_DYNAMIC, 22, 3, columns);
            nk_label(context, action_labels[static_cast<size_t>(row.action)], NK_TEXT_LEFT);
            text_cell(context, row.path, 360.0f);
            nk_label(context, bytes, NK_TEXT_RIGHT);
        }
        for (size_t index = 0; index < group.diagnostics.size(); ++index) {
            nk_layout_row_dynamic(context, 22, 1);
            nk_label_colored(context, group.diagnostics[index].message, NK_TEXT_LEFT,
                             color(group.diagnostics[index].severity == 2
                                       ? UiColor::conflict
                                       : UiColor::text_muted));
        }
        if (group.plan_rows_truncated || group.diagnostics_truncated) {
            nk_layout_row_dynamic(context, 22, 1);
            nk_label_colored(context, "Preview rows truncated.", NK_TEXT_LEFT,
                             color(UiColor::status));
        }
    }
}
auto find_output_group(const WormUiSetupView& view, size_t index)
    -> const WormUiProjectOutputGroupView* {
    const auto* group = std::find_if(view.output_groups, view.output_groups + view.output_group_count,
        [index](const auto& candidate) { return candidate.document_index == index; });
    return group == view.output_groups + view.output_group_count ? nullptr : group;
}
void render_output_group_menu(nk_context* context, const WormUiSetupView& view,
                              WormUiWorkspaceAction& action) {
    using namespace wormedit::ui::chrome;
    const auto* group = find_output_group(view, project_state.group_menu_index);
    if (!group) { context_menu_reset(project_state.group_menu); return; }
    auto& menu = project_state.group_menu;
    if (!context_menu_begin(menu, context, PopupContextStamp::from(view, group->document_index + 1),
                            nk_vec2(260.0f, context_menu_height(7))))
        return;
    const auto* edit_reason = output_group_editable(view, *group) ? nullptr
        : "This target exceeds the editable Project view limits.";
    if (context_menu_item(menu, context, "Export · Replace existing"))
        export_project_group(view, action, group->document_index, WORM_UI_EXISTING_FILES_REPLACE);
    if (context_menu_item(menu, context, "Export · Keep existing"))
        export_project_group(view, action, group->document_index, WORM_UI_EXISTING_FILES_KEEP);
    if (context_menu_item(menu, context, "Edit", edit_reason)) begin_output_group_editor(view, group);
    if (context_menu_item(menu, context, "Preview")) {
        publish_output_group_action(view, action, WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW, group->document_index);
        project_state.group_plan_index = group->document_index;
        project_state.group_plan_expanded = true;
    }
    const auto plan_label = project_state.group_plan_expanded &&
                                    project_state.group_plan_index == group->document_index
                                ? "Hide plan"
                                : "Show plan";
    if (context_menu_item(menu, context, plan_label,
                          !group->plan_rows.empty() || !group->diagnostics.empty()
                              ? nullptr
                              : "Run Preview first.")) {
        const auto was_open = project_state.group_plan_expanded &&
                              project_state.group_plan_index == group->document_index;
        project_state.group_plan_index = group->document_index;
        project_state.group_plan_expanded = !was_open;
    }
    if (context_menu_item(menu, context, "Duplicate", edit_reason)) begin_output_group_duplicate(view, *group);
    if (context_menu_item(menu, context, "Delete…")) begin_output_group_delete(view, group->document_index);
    context_menu_end(menu, context);
}
void render_output_group_delete(nk_context* context, const WormUiSetupView& view,
                                WormUiWorkspaceAction& action) {
    using namespace wormedit::ui::chrome;
    auto& state = project_state;
    if (!state.group_delete_pending) return;
    const auto* group = find_output_group(view, state.group_index);
    if (state.group_revision != view.document_revision || !group) {
        state.group_delete_pending = false;
        return;
    }
    if (dialog_begin(state.group_delete_dialog, context, "Delete export target", "Delete export target?")) {
        dialog_text(state.group_delete_dialog, context, group->name, color(UiColor::text_bright));
        dialog_text(state.group_delete_dialog, context,
                    "This removes the target from the project. Files already exported are kept, and you can Undo this change.",
                    color(UiColor::text));
        dialog_footer_begin(state.group_delete_dialog, context, 2, true);
        (void)dialog_button(state.group_delete_dialog, context, "Delete", DialogButtonRole::destructive);
        (void)dialog_button(state.group_delete_dialog, context, "Cancel", DialogButtonRole::secondary);
    }
    const auto result = dialog_end(state.group_delete_dialog, context);
    if (result == DialogResult::destructive)
        publish_output_group_action(view, action, WORM_UI_PROJECT_OUTPUT_GROUP_DELETE, state.group_index);
    if (result != DialogResult::none) {
        state.group_delete_pending = false;
        state.release_pending = true;
    }
}
void render_output_groups(nk_context* context, const WormUiSetupView& view, WormUiWorkspaceAction& action) {
    (void)wormedit::ui::chrome::context_menu_update(project_state.group_menu, context);
    nk_layout_row_dynamic(context, 18, 1);
    nk_spacing(context, 1);
    nk_layout_space_begin(context, NK_STATIC, 32.0f, 3);
    const auto header = nk_layout_space_bounds(context);
    const auto viewport = wormedit::ui::nuklear::tooltip::viewport();
    const auto width = std::max(250.0f,
        std::min(header.w, viewport.x + viewport.w - header.x - 8.0f));
    char heading[64];
    std::snprintf(heading, sizeof(heading), "Exports · %zu", view.output_group_count);
    nk_layout_space_push(context, nk_rect(0.0f, 0.0f, std::max(1.0f, width - 248.0f), 32.0f));
    project_section_heading(context, heading);
    nk_layout_space_push(context, nk_rect(width - 240.0f, 0.0f, 110.0f, 32.0f));
    if (wormedit::ui::chrome::Button{"Export all", wormedit::ui::chrome::ButtonKind::secondary,
                                     "Prepare every export target, then publish them",
                                     view.output_group_count != 0}
            .render(context))
        export_all_project_groups(view, action);
    nk_layout_space_push(context, nk_rect(width - 122.0f, 0.0f, 122.0f, 32.0f));
    if (nk_button_label(context, "+ New")) begin_output_group_editor(view, nullptr);
    nk_layout_space_end(context);
    if (view.output_group_count == 0) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label(context, "No export targets yet. Create one to choose a format, destination and included charts.", NK_TEXT_LEFT);
    }
    for (size_t i = 0; i < view.output_group_count; ++i)
        render_output_group_card(context, view, view.output_groups[i], action);
    if (view.output_groups_truncated) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Showing the first 256 output groups.", NK_TEXT_LEFT);
    }
    render_output_group_menu(context, view, action);
}

} // namespace

bool worm_ui_nuklear_setup_popup_owns_input() {
    return project_state.selection.pending_delete != 0 || project_state.release_pending ||
        project_state.group_editor_open || project_state.group_delete_pending ||
        wormedit::ui::chrome::context_menu_owns_input(project_state.row_menu) ||
        wormedit::ui::chrome::context_menu_owns_input(project_state.group_menu);
}

void worm_ui_nuklear_sync_setup_popups(nk_context* context, const WormUiSetupView* view) {
    const auto& selection = project_state.selection;
    const bool same_session = view && view->visible && selection.song == view->song_id &&
        selection.session_generation == view->session_generation;
    if (!same_session) {
        if (context && project_state.delete_dialog.open) nk_window_close(context, "Delete chart");
        auto retired_menu = project_state.row_menu;
        auto retired_group_menu = project_state.group_menu;
        auto project_directory = std::move(project_state.project_directory);
        wormedit::ui::chrome::context_menu_abandon(retired_menu);
        wormedit::ui::chrome::context_menu_abandon(retired_group_menu);
        project_state = {};
        project_state.row_menu = retired_menu;
        project_state.group_menu = retired_group_menu;
        project_state.project_directory = std::move(project_directory);
    } else {
        worm_ui_nuklear_reconcile_project_selection(project_state.selection, *view);
        worm_ui_nuklear_reconcile_chart_drag(project_state.drag, *view);
    }
}

void worm_ui_nuklear_retire_setup_popups(nk_context* context) {
    wormedit::ui::chrome::context_menu_retire(project_state.row_menu, context);
    wormedit::ui::chrome::context_menu_retire(project_state.group_menu, context);
}

void worm_ui_nuklear_render_setup_panel(struct nk_context* context,
                                        const WormUiSetupView* view,
                                        WormUiWorkspaceAction* action) {
    if (!context || !view || !action || !view->visible)
        return;

    nk_layout_row_dynamic(context, 22, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Audio assets: %zu · Visual assets: %zu",
              view->audio_asset_count, view->visual_asset_count);
    render_project_charts(context, *view, *action);
    render_output_groups(context, *view, *action);
}

void worm_ui_nuklear_render_setup_dialogs(nk_context* context, const WormUiSetupView& view,
                                         WormUiWorkspaceAction& action) {
    if (!view.visible) return;
    render_delete_confirmation(context, view, action);
    render_output_group_editor(context, view, action);
    render_output_group_delete(context, view, action);
}

void worm_ui_nuklear_cancel_setup_drag() {
    project_state.drag = {};
}

void worm_ui_nuklear_set_project_directory(const char* path) {
    project_state.project_directory = path ? path : "";
}
