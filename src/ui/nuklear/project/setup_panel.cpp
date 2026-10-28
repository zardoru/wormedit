#include <wormedit/core/diagnostic.h>
#include <wormedit/core/metadata_rating.h>
#include "ui/nuklear/chrome/text_fit.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/project/setup_panel.h"
#include "ui/nuklear/project/setup_state.h"
#include "ui/nuklear/chrome/tooltip.h"
#include "ui/nuklear/chrome/context_menu.h"
#include "ui/nuklear/chrome/dialog.h"

#include "wormedit/nuklear_config.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <initializer_list>
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

// Section headers: the heading fills the row and the section's actions sit at its right edge.
constexpr float project_section_header_height = 32.0f;

void begin_project_section_header(nk_context* context, std::initializer_list<float> action_widths) {
    nk_layout_row_template_begin(context, project_section_header_height);
    nk_layout_row_template_push_dynamic(context);
    for (const float width : action_widths) nk_layout_row_template_push_static(context, width);
    nk_layout_row_template_end(context);
}

void project_section_gap(nk_context* context) {
    nk_layout_row_dynamic(context, wormedit::ui::metrics::gap_large, 1);
    nk_spacing(context, 1);
}

// Painted like a tree tab header (Project settings, the Metadata sections), so every section on
// the form screens opens with the same bar.
void project_section_heading(nk_context* context, const char* text) {
    struct nk_rect bounds{};
    if (nk_widget(&bounds, context) == NK_WIDGET_INVALID) return;
    auto* canvas = nk_window_get_canvas(context);
    const auto& style = context->style;
    nk_fill_rect(canvas, bounds, style.tab.rounding, wormedit::ui::chrome::section_header_color());
    const float inset = wormedit::ui::metrics::gap_medium + wormedit::ui::metrics::gap_small;
    const float height = style.font->height;
    nk_draw_text(canvas, nk_rect(bounds.x + inset, bounds.y + (bounds.h - height) * 0.5f, bounds.w - inset, height),
                 text, static_cast<int>(std::strlen(text)), style.font, wormedit::ui::chrome::section_header_color(), style.tab.text);
}

struct project_metadata_popup_t : wormedit::ui::chrome::retained_popup_t {
    wormedit::ui::chrome::PopupContextStamp stamp;
    std::vector<std::uint64_t> selection;
    struct nk_rect bounds{};
    bool focus_pending{};
};

struct project_panel_state_t {
    std::optional<wormedit::ui::chrome::PopupContextStamp> creation_stamp;
    wormedit::timing_link_selection_t creation_timing;
    wormedit::ui::chrome::Dialog creation_dialog;
    std::optional<std::uint64_t> settings_revision;
    std::array<char, wormedit::maximum_project_path_bytes + 1> audio_directory{}, visual_directory{};
    project_metadata_popup_t metadata_popup;
    std::vector<std::uint64_t> delete_selection;
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
    nk_bool group_flatten_bms_branches{};
    wormedit::export_audio_settings_t group_audio;
    int group_stepmania_file{WORM_UI_STEPMANIA_FILE_SSC};
    int group_stepmania_timing{WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS};
    int group_stepmania_implementation{WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY};
    std::vector<nk_bool> group_charts;
    std::vector<std::uint64_t> shown_charts;
    wormedit::ui::chrome::ContextMenu group_menu{.title = "Export target actions"};
    wormedit::ui::chrome::Dialog group_editor_dialog;
    wormedit::ui::chrome::Dialog group_delete_dialog;
    size_t group_menu_index{};
    bool group_delete_pending{};
    size_t group_plan_index{};
    bool group_plan_expanded{};
    uint64_t membership_chart{};
    uint64_t membership_revision{};
    wormedit::ui::chrome::Dialog membership_dialog;
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

void begin_chart_memberships(uint64_t chart, uint64_t revision) {
    project_state.membership_chart = chart;
    project_state.membership_revision = revision;
    project_state.membership_dialog = {};
}

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

void publish_chart_action(const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action,
    WormUiProjectChartActionKind kind, uint64_t chart) {
    if (action.project_chart.kind != WORM_UI_PROJECT_CHART_NONE) return;
    action.project_chart.kind = kind;
    action.project_chart.chart_id = wormedit::chart_id_t{chart};
    action.project_chart.document_revision = view.document_revision;
    action.project_chart.session_generation = view.session_generation;
}

void publish_selected_chart_action(const wormedit::ui::setup_view_t& view,
    wormedit::ui::workspace_action_t& action, WormUiProjectChartActionKind kind,
    std::span<const std::uint64_t> ids) {
    if (action.project_chart.kind != WORM_UI_PROJECT_CHART_NONE) return;
    publish_chart_action(view, action, kind, 0);
    for (const auto id : ids) action.project_chart.selected_chart_ids.push_back(wormedit::chart_id_t{id});
}

void publish_batch(const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action) {
    publish_selected_chart_action(view, action, WORM_UI_PROJECT_CHART_BATCH_METADATA,
                                  project_state.metadata_popup.selection);
    auto& batch = action.project_chart;
    batch.edit_author = project_state.edit_author;
    batch.author = project_state.author;
    batch.edit_rating = project_state.edit_rating;
    batch.unset_rating = project_state.unset_rating;
    batch.rating_numerator = project_state.rating_numerator;
    batch.rating_denominator = project_state.rating_denominator;
}

void open_project_metadata(nk_context* context, const wormedit::ui::setup_view_t& view, bool author) {
    auto& popup = project_state.metadata_popup;
    popup.open = true;
    popup.focus_pending = true;
    popup.stamp = wormedit::ui::chrome::PopupContextStamp::from(view, 0);
    popup.selection = project_state.selection.ids;
    project_state.edit_author = author;
    project_state.edit_rating = !author;
    popup.bounds = wormedit::ui::chrome::clamped_popup_bounds(context,
        nk_vec2(context->input.mouse.pos.x, context->input.mouse.pos.y + 20),
        nk_vec2(380, author ? 140 : 228));
}

void render_project_metadata_fields(nk_context* context, const wormedit::ui::setup_view_t& view,
    wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    auto& state = project_state;
    auto& popup = state.metadata_popup;
    const bool quiet = popup_input_is_quiet(context, PopupReleaseKeys::all);
    bool focus_pending = popup.focus_pending && quiet;
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, state.edit_author ? "Set author" : "Set rating", NK_TEXT_LEFT);
    if (state.edit_author) {
        nk_layout_row_dynamic(context, 28, 1);
        autofocus_text_field(context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_AUTO_SELECT,
            state.author, sizeof(state.author), nk_filter_default, focus_pending);
    } else {
        nk_layout_row_dynamic(context, 24, 1);
        nk_checkbox_label(context, "Unset rating", &state.unset_rating);
        nk_layout_row_dynamic(context, 20, 2);
        nk_label(context, "Numerator", NK_TEXT_LEFT);
        nk_label(context, "Denominator", NK_TEXT_LEFT);
        nk_layout_row_dynamic(context, 28, 2);
        autofocus_text_field(context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_AUTO_SELECT,
            state.rating_numerator, sizeof(state.rating_numerator), nk_filter_default, focus_pending);
        worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            state.rating_denominator, sizeof(state.rating_denominator), nk_filter_default);
    }
    if (quiet) popup.focus_pending = false;
    const bool valid = state.edit_author || state.unset_rating ||
        wormedit::parse_metadata_rating(state.rating_numerator, state.rating_denominator).has_value();
    if (!valid) {
        nk_layout_row_dynamic(context, 32, 1);
        nk_label_wrap(context, "Enter an integer numerator and a positive denominator.");
    }
    nk_layout_row_dynamic(context, 28, 2);
    if (Button{"Apply", ButtonKind::primary, nullptr, valid}.render(context)) {
        publish_batch(view, action);
        popup.cancel();
    }
    if (Button{.label = "Cancel"}.render(context)) popup.cancel();
}

void render_project_metadata_popup(nk_context* context, const wormedit::ui::setup_view_t& view,
    wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    auto& state = project_state.metadata_popup;
    if (!state.open && !state.close_pending) return;
    if (!popup_context_matches(state.stamp, PopupContextStamp::from(view, 0)) ||
        state.selection != project_state.selection.ids ||
        popup_dismiss_requested(context, popup_screen_bounds(context, state.bounds), !state.focus_pending))
        state.cancel();
    auto popup = begin_retained_popup(context, state, "Project chart metadata",
        NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR, state.bounds, retained_popup_begin_failure_t::cancel);
    if (popup.has_contents() && state.open) render_project_metadata_fields(context, view, action);
    end_retained_popup(context, state, popup, retained_popup_edit_focus_t::unfocus_on_close,
                       retained_popup_close_trigger_t::closed_or_pending);
}

void render_project_batch(nk_context* context, const wormedit::ui::setup_view_t& view,
    wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    const auto count = project_state.selection.ids.size();
    if (count == 0) return;
    const bool metadata_enabled = count <= wormedit::maximum_batch_chart_metadata_charts;
    const BarAction actions[]{
        {.label = "Set author…", .enabled = metadata_enabled},
        {.label = "Set rating…", .enabled = metadata_enabled},
        {.label = "Duplicate"},
        {.label = "Delete…", .kind = ButtonKind::destructive, .enabled = count < view.charts.size()},
        {.label = "×", .tooltip = "Clear chart selection"}};
    const auto summary = std::to_string(count) + " selected";
    switch (SelectionBar{summary.c_str(), actions, 5}.render(context)) {
    case 0: open_project_metadata(context, view, true); break;
    case 1: open_project_metadata(context, view, false); break;
    case 2:
        publish_selected_chart_action(view, action, WORM_UI_PROJECT_CHART_DUPLICATE, project_state.selection.ids);
        break;
    case 3:
        project_state.delete_selection = project_state.selection.ids;
        project_state.selection.pending_delete_revision = view.document_revision;
        project_state.delete_dialog = {};
        break;
    case 4: project_state.selection.ids.clear(); break;
    default: break;
    }
}


void focus_project_chart(std::uint64_t id) {
    project_state.menu_chart = id;
    wormedit::ui::chrome::context_menu_focus(wormedit::ui::chrome::KeyboardContextTarget::charts, id);
}

void render_chart_actions(nk_context* context, const wormedit::ui::setup_view_t& view,
    const wormedit::ui::project_chart_row_view_t& row, bool hovered, wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    const bool focused = context_menu_focused_target() == KeyboardContextTarget::charts &&
                         context_menu_focused_object() == row.chart_id.value;
    char group[64]{};
    std::snprintf(group, sizeof(group), "chart-actions-%llu", static_cast<unsigned long long>(row.chart_id.value));
    nk_style_push_vec2(context, &context->style.window.group_padding, nk_vec2(0, 0));
    const bool open = nk_group_begin(context, group, NK_WINDOW_NO_SCROLLBAR);
    if (!open) {
        nk_style_pop_vec2(context);
        return;
    }
    nk_layout_row_dynamic(context, 26, 2);
    const auto open_tip = std::string{"Open in Compose: "} + std::string{row.name};
    if (IconButton{wormedit::ui::lucide::Glyph::chevron_right, open_tip.c_str()}.render(context))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_OPEN, row.chart_id.value);
    const DrawnIconButton more{[](nk_command_buffer* canvas, struct nk_rect bounds, struct nk_color tint) {
        const auto x = bounds.x + bounds.w * 0.5f;
        const auto y = bounds.y + bounds.h * 0.5f;
        for (int index = -1; index <= 1; ++index)
            nk_fill_circle(canvas, nk_rect(x + index * 5.0f - 1.5f, y - 1.5f, 3, 3), tint);
    }, "Chart actions"};
    if ((hovered || focused) && more.render(context)) {
        focus_project_chart(row.chart_id.value);
        context_menu_open(project_state.row_menu, context->input.mouse.pos, wormedit::ui::chrome::PopupContextStamp::from(view, row.chart_id.value));
    }
    if (!hovered && !focused) nk_spacing(context, 1);
    nk_group_end(context);
    nk_style_pop_vec2(context);
}

void render_row_menu(nk_context* context, const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    auto& menu = project_state.row_menu;
    if (!context_menu_begin(menu, context, wormedit::ui::chrome::PopupContextStamp::from(view, project_state.menu_chart),
                            nk_vec2(220, context_menu_height(3)))) return;
    const auto id = project_state.menu_chart;
    if (context_menu_item(menu, context, "Open in Compose"))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_OPEN, id);
    if (context_menu_item(menu, context, "Duplicate"))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_DUPLICATE, id);
    if (context_menu_item(menu, context, "Delete…")) {
        project_state.selection.pending_delete = id;
        project_state.selection.pending_delete_revision = view.document_revision;
        project_state.delete_dialog = {};
    }
    context_menu_end(menu, context);
}

void render_delete_confirmation(nk_context* context, const wormedit::ui::setup_view_t& view,
                                wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    const auto id = project_state.selection.pending_delete;
    const bool batch = !project_state.delete_selection.empty();
    if (!id && !batch) return;
    if (project_state.selection.pending_delete_revision != view.document_revision) {
        project_state.delete_selection.clear();
        return;
    }
    const auto row = std::find_if(view.charts.begin(), view.charts.end(),
                                 [id](const auto& item) { return item.chart_id.value == id; });
    if (!batch && row == view.charts.end()) return;
    auto& dialog = project_state.delete_dialog;
    if (dialog_begin(dialog, context, "Delete chart", "Delete chart?")) {
        const auto summary = batch ? std::to_string(project_state.delete_selection.size()) + " selected charts" : std::string{row->name};
        dialog_text(dialog, context, summary.c_str(), context->style.text.color);
        dialog_text(dialog, context, "This removes the chart and its notes from the project. You can Undo this change.",
                    context->style.text.color);
        dialog_footer_begin(dialog, context, 2, true);
        if (dialog_button(dialog, context, "Delete", DialogButtonRole::destructive)) {
            if (batch) publish_selected_chart_action(view, action, WORM_UI_PROJECT_CHART_DELETE, project_state.delete_selection);
            else publish_chart_action(view, action, WORM_UI_PROJECT_CHART_DELETE, id);
        }
        (void)dialog_button(dialog, context, "Cancel", DialogButtonRole::secondary);
    }
    if (dialog_end(dialog, context) != DialogResult::none) {
        project_state.selection.pending_delete = 0;
        project_state.delete_selection.clear();
        project_state.release_pending = true;
    }
}

// Fixed table template: grip, checkbox, eight text columns sharing the rest, and actions.
constexpr float grip_width = 18.0f;
constexpr float checkbox_width = 26.0f;
constexpr float actions_width = 2 * wormedit::ui::chrome::IconButton::width;
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

void render_chart_grip(nk_context* context, const wormedit::ui::setup_view_t& view, std::uint64_t id) {
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

void preview_chart_drop(nk_context* context, const wormedit::ui::setup_view_t& view,
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

void prepare_chart_drag(nk_context* context, const wormedit::ui::setup_view_t& view) {
    worm_ui_nuklear_reconcile_chart_drag(project_state.drag, view);
    if (nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE) ||
        worm_ui_nuklear_setup_popup_owns_input()) project_state.drag = {};
    if (!project_state.drag.source) return;
    project_state.drag.target = 0; // Leaving the rows cancels the drop rather than retaining a target.
    const auto mouse = context->input.mouse.pos;
    project_state.drag_moved = project_state.drag_moved ||
        std::hypot(mouse.x - project_state.drag_start.x, mouse.y - project_state.drag_start.y) >= 3.0f;
}

void finish_chart_drag(nk_context* context, const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (!project_state.drag.source || context->input.mouse.buttons[NK_BUTTON_LEFT].down) return;
    if (project_state.drag_moved && nk_input_is_mouse_released(&context->input, NK_BUTTON_LEFT) &&
        action.project_chart.kind == WORM_UI_PROJECT_CHART_NONE)
        action.project_chart = worm_ui_nuklear_finish_chart_drag(project_state.drag, view);
    project_state.drag = {};
}

void render_table_header(nk_context* context, const std::vector<std::uint64_t>& shown,
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

void render_chart_row(nk_context* context, const wormedit::ui::setup_view_t& view,
    const wormedit::ui::project_chart_row_view_t& row, const std::vector<std::uint64_t>& shown,
    std::size_t shown_count, const column_widths_t& widths, const table_ratios_t& ratios,
    wormedit::ui::workspace_action_t& action) {
    begin_table_row(context, 30, ratios);
    const auto row_bounds = table_row_bounds(context, widths);
    const bool selected = worm_ui_nuklear_project_chart_selected(project_state.selection, row.chart_id.value);
    if (!worm_ui_nuklear_setup_popup_owns_input() &&
        nk_input_is_mouse_pressed(&context->input, NK_BUTTON_LEFT) &&
        nk_input_is_mouse_hovering_rect(&context->input, row_bounds)) {
        focus_project_chart(row.chart_id.value);
    }
    const bool keyboard_open = (project_state.menu_chart == row.chart_id.value ||
        (!project_state.menu_chart && selected)) &&
        wormedit::ui::chrome::context_menu_take_open_request(wormedit::ui::chrome::KeyboardContextTarget::charts);
    if (keyboard_open) {
        project_state.menu_chart = row.chart_id.value;
        wormedit::ui::chrome::context_menu_open(project_state.row_menu,
            nk_vec2(row_bounds.x, row_bounds.y + row_bounds.h), wormedit::ui::chrome::PopupContextStamp::from(view, row.chart_id.value), true);
    }
    paint_table_row(context, row_bounds, false, selected);
    render_chart_grip(context, view, row.chart_id.value);
    if (wormedit::ui::chrome::Checkbox{"", selected ? wormedit::ui::chrome::CheckState::on : wormedit::ui::chrome::CheckState::off,
            "Select chart"}.render(context))
        worm_ui_nuklear_click_project_chart(project_state.selection, {shown.data(), shown_count}, row.chart_id.value, true, false);
    const auto name_bounds = nk_widget_bounds(context);
    if (!worm_ui_nuklear_setup_popup_owns_input() &&
        nk_input_is_mouse_pressed(&context->input, NK_BUTTON_RIGHT) &&
        nk_input_is_mouse_hovering_rect(&context->input, row_bounds)) {
        focus_project_chart(row.chart_id.value);
        wormedit::ui::chrome::context_menu_open(project_state.row_menu, context->input.mouse.pos,
                                               wormedit::ui::chrome::PopupContextStamp::from(view, row.chart_id.value));
    }
    if (!worm_ui_nuklear_setup_popup_owns_input() &&
        nk_input_mouse_clicked(&context->input, NK_BUTTON_DOUBLE, name_bounds))
        publish_chart_action(view, action, WORM_UI_PROJECT_CHART_OPEN, row.chart_id.value);
    nk_bool name_selected = selected;
    const auto* label = row.name.data()[0] ? row.name.data() : "Untitled chart";
    const auto name = wormedit::ui::chrome::fit_text_with_ellipsis(context->style.font, label, widths[0] - cell_padding,
        wormedit::ui::chrome::ellipsis_overflow_t::suffix);
    if (nk_selectable_label(context, name.c_str(), NK_TEXT_LEFT, &name_selected)) {
        const auto modifiers = SDL_GetModState();
        const auto [ctrl, shift] = shift_or_ctrl(modifiers);
        worm_ui_nuklear_click_project_chart(project_state.selection, {shown.data(), shown_count}, row.chart_id.value, ctrl, shift);
    }
    if (name != label && nk_widget_is_hovered(context))
        wormedit::ui::nuklear::tooltip::text(context, label);
    text_cell(context, row.author.data(), widths[1]);
    text_cell(context, row.rating.data(), widths[2]);
    text_cell(context, row.mode.data(), widths[3]);
    nk_labelf(context, NK_TEXT_LEFT, "%u", row.key_count);
    if (nk_button_label(context, row.output_groups.data()))
        begin_chart_memberships(row.chart_id.value, view.document_revision);
    nk_labelf(context, NK_TEXT_LEFT, "%llu", static_cast<unsigned long long>(row.note_count));
    text_cell(context, row.length.data(), widths[7]);
    const bool hovered = nk_input_is_mouse_hovering_rect(&context->input, context->current->buffer.clip) &&
                         nk_input_is_mouse_hovering_rect(&context->input, row_bounds);
    render_chart_actions(context, view, row, hovered, action);
    preview_chart_drop(context, view, row.chart_id.value, row_bounds);
    if (row.diagnostic.data() && row.diagnostic.data()[0]) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label_colored(context, row.diagnostic.data(), NK_TEXT_LEFT, nk_rgb(235, 150, 120));
    }
}

void render_creation_sources(nk_context* context, const wormedit::ui::setup_view_t& view) {
    for (const auto& category : view.timing_categories) {
        auto& source = project_state.creation_timing.sources.at(static_cast<std::size_t>(category.category));
        const auto selected = std::ranges::find(view.charts, source, &wormedit::ui::project_chart_row_view_t::chart_id);
        nk_layout_row_dynamic(context, 26, 2);
        nk_label(context, category.label.c_str(), NK_TEXT_LEFT);
        const std::string_view label = selected == view.charts.end() ? "Independent" : selected->name;
        if (!wormedit::ui::chrome::safe_combo_begin_label(context, label.data(), nk_vec2(300, 220))) continue;
        nk_layout_row_dynamic(context, 24, 1);
        if (nk_combo_item_label(context, "Independent", NK_TEXT_LEFT)) source = {};
        for (const auto& chart : view.charts)
            if (nk_combo_item_text(context, chart.name.data(), static_cast<int>(chart.name.size()), NK_TEXT_LEFT)) source = chart.chart_id;
        wormedit::ui::chrome::safe_combo_end(context);
    }
}
void render_creation_dialog(nk_context* context, const wormedit::ui::setup_view_t& view,
    wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    if (!project_state.creation_stamp) return;
    if (!popup_context_matches(*project_state.creation_stamp, PopupContextStamp::from(view, view.selected_chart_id.value))) {
        project_state.creation_stamp.reset();
        project_state.creation_timing = {};
        project_state.release_pending = true;
        return;
    }
    auto& dialog = project_state.creation_dialog;
    if (dialog_begin(dialog, context, "New chart timing", "New chart", {.width = 560, .enter_confirms = false})) {
        dialog_text(dialog, context, "Select timing sources for the new difficulty. Independent categories keep local timing.", context->style.text.color);
        render_creation_sources(context, view);
        dialog_footer_begin(dialog, context, 2);
        if (dialog_button(dialog, context, "Create", DialogButtonRole::primary)) {
            publish_chart_action(view, action, WORM_UI_PROJECT_CHART_NEW, view.selected_chart_id.value);
            action.project_chart.timing_sources = project_state.creation_timing;
        }
        (void)dialog_button(dialog, context, "Cancel", DialogButtonRole::secondary);
    }
    if (dialog_end(dialog, context) != DialogResult::none) {
        project_state.creation_stamp.reset();
        project_state.creation_timing = {};
        project_state.release_pending = true;
    }
}

void render_project_charts(nk_context* context, const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (project_state.selection.song != view.song_id.value ||
        project_state.selection.session_generation != view.session_generation) project_state = {};
    worm_ui_nuklear_reconcile_project_selection(project_state.selection, view);
    auto& shown = project_state.shown_charts;
    shown.resize(view.charts.size());
    const auto shown_count = shown.size();
    for (std::size_t index = 0; index < shown_count; ++index) shown[index] = view.charts[index].chart_id.value;

    begin_project_section_header(context, {160.0f, 32.0f});
    char heading[64];
    std::snprintf(heading, sizeof(heading), "Charts · %zu", view.chart_count);
    project_section_heading(context, heading);
    if (wormedit::ui::chrome::Button{"Import chart…", wormedit::ui::chrome::ButtonKind::secondary,
                                     "Add difficulties from another chart file"}.render(context) &&
        action.chart_import.kind == WORM_UI_CHART_IMPORT_NONE)
        action.chart_import.kind = WORM_UI_CHART_IMPORT_BEGIN;
    if (wormedit::ui::chrome::Button{"+", wormedit::ui::chrome::ButtonKind::primary, "New chart"}.render(context)) {
        project_state.creation_stamp = wormedit::ui::chrome::PopupContextStamp::from(view, view.selected_chart_id.value);
        project_state.creation_timing = {};
        project_state.creation_dialog = {};
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
    render_project_metadata_popup(context, view, action);
}


void load_output_group_draft(const wormedit::ui::setup_view_t& view, const wormedit::ui::project_output_group_view_t* group,
                             bool create) {
    auto& state = project_state;
    state.group_editor_open = true;
    state.group_create = create;
    state.group_index = group ? group->document_index : 0;
    state.group_revision = view.document_revision;
    state.group_family = group ? static_cast<int>(group->family) : 4;
    state.group_existing_files = group ? static_cast<int>(group->existing_files) : WORM_UI_EXISTING_FILES_ASK;
    state.group_stepmania_file = group && group->stepmania.has_value()
                                     ? static_cast<int>(group->stepmania->file_profile)
                                     : WORM_UI_STEPMANIA_FILE_SSC;
    state.group_stepmania_timing = group && group->stepmania.has_value()
                                       ? static_cast<int>(group->stepmania->timing_syntax) - 1
                                       : WORM_UI_STEPMANIA_TIMING_NATIVE_WARPS;
    state.group_stepmania_implementation = group && group->stepmania.has_value()
                                               ? static_cast<int>(group->stepmania->implementation_profile)
                                               : WORM_UI_STEPMANIA_IMPLEMENTATION_SYNTAX_ONLY;
    state.group_flatten_bms_branches = group && group->flatten_bms_branches;
    state.group_audio = group ? group->audio : wormedit::export_audio_settings_t{};
    state.group_editor_dialog = {};
    std::snprintf(state.group_name, sizeof(state.group_name), "%s",
                  group ? group->name.data() : "New export target");
    std::snprintf(state.group_path, sizeof(state.group_path), "%s",
                  group ? group->path.data() : "exports/bmson");
    state.group_charts.assign(view.charts.size(), false);
    for (size_t i = 0; i < view.charts.size(); ++i) {
        state.group_charts[i] = group ? std::ranges::find(
            std::span{group->chart_ids}, view.charts[i].chart_id) !=
            std::span{group->chart_ids}.end() : view.charts[i].chart_id.value == view.selected_chart_id.value;
    }
}
void begin_output_group_editor(const wormedit::ui::setup_view_t& view, const wormedit::ui::project_output_group_view_t* group) {
    load_output_group_draft(view, group, group == nullptr);
}
void begin_output_group_suggestion(const wormedit::ui::setup_view_t& view,
                                   const wormedit::ui::project_output_suggestion_view_t& suggestion) {
    load_output_group_draft(view, nullptr, true);
    project_state.group_family = static_cast<int>(suggestion.family);
    project_state.group_stepmania_file = static_cast<int>(suggestion.stepmania.value_or(wormedit::stepmania_export_policy_t{}).file_profile);
    project_state.group_stepmania_timing = static_cast<int>(suggestion.stepmania.value_or(wormedit::stepmania_export_policy_t{}).timing_syntax) - 1;
    project_state.group_stepmania_implementation = static_cast<int>(suggestion.stepmania.value_or(wormedit::stepmania_export_policy_t{}).implementation_profile);
    std::snprintf(project_state.group_name, sizeof(project_state.group_name), "%s", suggestion.name.data());
    std::snprintf(project_state.group_path, sizeof(project_state.group_path), "%s", suggestion.path.data());
}
auto output_group_name_used(const wormedit::ui::setup_view_t& view, std::string_view name) -> bool {
    return std::ranges::any_of(std::span{view.output_groups},
        [name](const auto& group) { return group.name.data() && name == group.name.data(); });
}
void begin_output_group_duplicate(const wormedit::ui::setup_view_t& view,
                                  const wormedit::ui::project_output_group_view_t& group) {
    load_output_group_draft(view, &group, true);
    const auto source = std::string_view{group.name.data()};
    const auto kept = std::min(source.size(), sizeof(project_state.group_name) - size_t{16});
    std::string candidate{source.substr(0, kept)};
    candidate += " copy";
    for (int suffix = 2; output_group_name_used(view, candidate); ++suffix)
        candidate = std::string{source.substr(0, kept)} + " copy " + std::to_string(suffix);
    std::snprintf(project_state.group_name, sizeof(project_state.group_name), "%s", candidate.c_str());
}
void begin_output_group_delete(const wormedit::ui::setup_view_t& view, size_t index) {
    project_state.group_index = index;
    project_state.group_revision = view.document_revision;
    project_state.group_delete_pending = true;
    project_state.group_delete_dialog = {};
}
void publish_output_group_action(const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action,
    WormUiProjectOutputGroupActionKind kind, size_t index) {
    if (action.project_output_group.kind != WORM_UI_PROJECT_OUTPUT_GROUP_NONE) return;
    auto& output = action.project_output_group;
    output.kind = kind;
    output.project_id = view.song_id;
    output.document_revision = view.document_revision;
    output.session_generation = view.session_generation;
    output.group_index = index;
    if (kind != WORM_UI_PROJECT_OUTPUT_GROUP_CREATE && kind != WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE)
        return;
    output.name = project_state.group_name;
    output.path = project_state.group_path;
    output.family = static_cast<wormedit::format_family_t>(project_state.group_family);
    output.destination_kind = project_state.group_family == 0 || project_state.group_family == 2
        ? wormedit::output_destination_kind_t::file : wormedit::output_destination_kind_t::directory;
    output.existing_files = static_cast<wormedit::existing_files_policy_t>(project_state.group_existing_files);
    output.flatten_bms_branches = project_state.group_flatten_bms_branches;
    output.audio = project_state.group_audio;
    output.stepmania.reset();
    if (project_state.group_family == 2)
        output.stepmania = wormedit::stepmania_export_policy_t{
            .file_profile = static_cast<wormedit::stepmania_profile_t>(project_state.group_stepmania_file),
            .timing_syntax = static_cast<wormedit::stepmania_timing_syntax_t>(project_state.group_stepmania_timing + 1),
            .implementation_profile = static_cast<wormedit::stepmania_implementation_profile_t>(
                project_state.group_stepmania_implementation)};
    output.chart_ids.clear();
    for (size_t i = 0; i < view.charts.size(); ++i)
        if (project_state.group_charts[i]) output.chart_ids.push_back(view.charts[i].chart_id);
}

auto find_project_chart(const wormedit::ui::setup_view_t& view, std::uint64_t chart_id)
    -> const wormedit::ui::project_chart_row_view_t* {
    const auto charts = std::span{view.charts};
    const auto found = std::ranges::find(charts, wormedit::chart_id_t{chart_id}, &wormedit::ui::project_chart_row_view_t::chart_id);
    return found == charts.end() ? nullptr : &*found;
}
auto chart_is_in_output_group(const wormedit::ui::project_output_group_view_t& group, std::uint64_t chart_id)
    -> bool {
    const auto charts = std::span{group.chart_ids};
    return std::ranges::find(charts, wormedit::chart_id_t{chart_id}) != charts.end();
}
auto chart_supports_output_group(const wormedit::ui::project_chart_row_view_t& chart,
                                 const wormedit::ui::project_output_group_view_t& group) -> bool {
    return static_cast<unsigned>(group.family) < 5 &&
           (chart.compatible_output_families & (std::uint32_t{1} << static_cast<unsigned>(group.family)));
}
auto output_incompatibility_reason(const wormedit::ui::project_chart_row_view_t& chart, std::uint8_t family)
    -> const char* {
    if (family < 5 && !chart.output_incompatibility_reasons[family].empty())
        return chart.output_incompatibility_reasons[family].data();
    return "This output format is not supported.";
}
void publish_chart_membership(const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action,
                              const wormedit::ui::project_output_group_view_t& group, std::uint64_t chart_id,
                              bool included) {
    load_output_group_draft(view, &group, false);
    for (size_t row = 0; row < view.charts.size(); ++row)
        if (view.charts[row].chart_id.value == chart_id) project_state.group_charts[row] = included;
    publish_output_group_action(view, action, WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE,
                                group.document_index);
    project_state.group_editor_open = false;
    project_state.membership_chart = 0;
}
auto render_chart_membership(nk_context* context, const wormedit::ui::setup_view_t& view,
                             wormedit::ui::workspace_action_t& action, const wormedit::ui::project_chart_row_view_t& chart,
                             const wormedit::ui::project_output_group_view_t& group) -> bool {
    using namespace wormedit::ui::chrome;
    nk_bool included = chart_is_in_output_group(group, chart.chart_id.value);
    const auto compatible = chart_supports_output_group(chart, group);
    const auto blocked = !compatible && !included;
    nk_layout_row_dynamic(context, 25, 1);
    if (blocked) nk_widget_disable_begin(context);
    const auto changed = nk_checkbox_label(context, group.name.data(), &included);
    if (blocked) nk_widget_disable_end(context);
    if (!compatible) {
        nk_layout_row_dynamic(context, 20, 1);
        nk_label_colored(context, output_incompatibility_reason(chart, static_cast<std::uint8_t>(group.family)), NK_TEXT_LEFT,
                         color(UiColor::text_muted));
    }
    if (!changed || blocked) return false;
    publish_chart_membership(view, action, group, chart.chart_id.value, included);
    return true;
}
void render_chart_memberships(nk_context* context, const wormedit::ui::setup_view_t& view,
                              wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    if (!project_state.membership_chart) return;
    if (project_state.membership_revision != view.document_revision) {
        project_state.membership_chart = 0;
        return;
    }
    const auto* chart = find_project_chart(view, project_state.membership_chart);
    if (!chart) {
        project_state.membership_chart = 0;
        return;
    }
    if (dialog_begin(project_state.membership_dialog, context, "Chart export memberships",
                     "Edit chart exports", {.width = 480.0f, .enter_confirms = false})) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, !chart->name.empty() ? chart->name.data() : "Untitled chart",
                 NK_TEXT_LEFT);
        for (size_t index = 0; index < view.output_groups.size(); ++index)
            if (render_chart_membership(context, view, action, *chart, view.output_groups[index]))
                break;
        dialog_footer_begin(project_state.membership_dialog, context, 1);
        (void)dialog_button(project_state.membership_dialog, context, "Close",
                            DialogButtonRole::secondary);
    }
    const auto result = dialog_end(project_state.membership_dialog, context);
    if (result == DialogResult::cancel || result == DialogResult::secondary)
        project_state.membership_chart = 0;
}
void render_output_group_members(nk_context* context, const wormedit::ui::setup_view_t& view) {
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Included charts", NK_TEXT_LEFT);
    for (size_t i = 0; i < view.charts.size(); ++i) {
        nk_layout_row_dynamic(context, 24, 1);
        const auto family = static_cast<std::size_t>(project_state.group_family);
        const auto compatible = family < 5 &&
            (view.charts[i].compatible_output_families & (std::uint32_t{1} << family));
        const auto blocked = !compatible && !project_state.group_charts[i];
        if (blocked) nk_widget_disable_begin(context);
        nk_checkbox_label(context, !view.charts[i].name.empty() ? view.charts[i].name.data() : "Untitled chart",
            &project_state.group_charts[i]);
        if (blocked) nk_widget_disable_end(context);
        if (!compatible) {
            nk_layout_row_dynamic(context, 20, 1);
            const auto reason = family < 5 ? view.charts[i].output_incompatibility_reasons[family]
                                            : "This output format is not supported.";
            nk_label_colored(context, !reason.empty() ? reason.data() : "This output format is not supported.",
                             NK_TEXT_LEFT, wormedit::ui::chrome::color(
                                 wormedit::ui::chrome::UiColor::text_muted));
        }
    }
}
auto render_output_family_picker(nk_context* context, const wormedit::ui::kind_catalog_view_t<wormedit::format_family_t>& catalog,
                                 int selected) -> int {
    const auto label = catalog.label(static_cast<wormedit::format_family_t>(selected), "Unsupported");
    if (wormedit::ui::chrome::safe_combo_begin_label(context, label, nk_vec2(220, 160))) {
        nk_layout_row_dynamic(context, 24, 1);
        for (std::size_t index = 0; index < catalog.entries.size(); ++index) {
            const auto& family = catalog.entries[index];
            if (nk_combo_item_label(context, family.label.data(), NK_TEXT_LEFT))
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

void render_export_audio_settings(nk_context* context) {
    using namespace wormedit;
    using namespace wormedit::ui::chrome;
    auto& audio = project_state.group_audio;
    constexpr std::array labels{"MP3", "Ogg Vorbis"};
    constexpr float fields[]{0.22f, 0.78f};
    nk_layout_row(context, NK_DYNAMIC, 28, 2, fields);
    nk_label(context, "Cut audio", NK_TEXT_LEFT);
    audio.codec = static_cast<export_audio_codec_t>(safe_combo(context, labels.data(), labels.size(),
        static_cast<int>(audio.codec), 24, nk_vec2(280, 100)));
    nk_layout_row(context, NK_DYNAMIC, 28, 2, fields);
    if (audio.codec == export_audio_codec_t::ogg) {
        nk_label(context, "Vorbis quality", NK_TEXT_LEFT);
        nk_property_float(context, "Quality", -0.1F, &audio.ogg_quality, 1.0F, 0.1F, 0.01F);
        return;
    }
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "MP3 bitrate", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 24, 5);
    if (nk_option_label(context, "Auto", audio.mp3_bitrate_kbps == 0)) audio.mp3_bitrate_kbps = 0;
    for (const auto bitrate : mp3_bitrates_kbps)
        if (nk_option_label(context, (std::to_string(bitrate) + " kbps").c_str(), audio.mp3_bitrate_kbps == bitrate))
            audio.mp3_bitrate_kbps = bitrate;
    nk_layout_row_dynamic(context, 22, 1);
    nk_label(context, "Auto uses source bitrate, up to 192 kbps", NK_TEXT_LEFT);
}

void render_output_group_form(nk_context* context, const wormedit::ui::setup_view_t& view) {
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
    if (state.group_family != 0) render_export_audio_settings(context);
    if (state.group_family >= 2) {
        nk_layout_row_dynamic(context, 28, 1);
        nk_checkbox_label(context, "Flatten BMS branches to the active outcome",
                          &state.group_flatten_bms_branches);
    }
    dialog_text(
        state.group_editor_dialog, context,
        state.group_family == 0 || state.group_family == 2
            ? "Song file inside the project. Preview shows copied assets beside it."
            : "Folder inside the project. Chart and asset filenames are derived in Preview.",
        color(UiColor::text_muted));
    render_output_group_members(context, view);
}

void render_output_group_editor(nk_context* context, const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action) {
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
void export_project_group(const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action, size_t index,
                          WormUiExistingFilesChoice existing_files) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_EXPORT_OUTPUT_GROUP;
    action.compose_input_project_id.value = view.song_id.value;
    action.compose_input_chart_id.value = view.selected_chart_id.value;
    action.compose_input_document_revision = view.document_revision;
    action.compose_output_group_index = index;
    action.compose_existing_files = existing_files;
}
void export_all_project_groups(const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE)
        return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_EXPORT_ALL_OUTPUT_GROUPS;
    action.compose_input_project_id.value = view.song_id.value;
    action.compose_input_chart_id.value = view.selected_chart_id.value;
    action.compose_input_document_revision = view.document_revision;
}
auto output_group_editable(const wormedit::ui::setup_view_t&,
                           const wormedit::ui::project_output_group_view_t& group) -> bool {
    return group.name.size() < sizeof(project_state.group_name) &&
        group.path.size() < sizeof(project_state.group_path);
}
auto output_group_chart_name(const wormedit::ui::setup_view_t& view, uint64_t id) -> const char* {
    const auto row = std::find_if(view.charts.begin(), view.charts.end(),
        [id](const auto& chart) { return chart.chart_id.value == id; });
    return row == view.charts.end() || row->name.empty()
        ? "Untitled chart" : row->name.data();
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
void render_output_group_chips(nk_context* context, const wormedit::ui::setup_view_t& view,
                               const wormedit::ui::project_output_group_view_t& group, float width,
                               wormedit::ui::workspace_action_t& action) {
    constexpr size_t maximum_chips = 4;
    const auto shown = std::min(group.chart_ids.size(), maximum_chips);
    const auto chip_width = std::max(72.0f, std::min(150.0f, width / maximum_chips));
    float x = 80.0f;
    for (size_t index = 0; index < shown; ++index) {
        nk_layout_space_push(context, nk_rect(x, 38.0f, chip_width, 22.0f));
        if (wormedit::ui::chrome::Chip{
                output_group_chart_name(view, group.chart_ids[index].value), true, true,
                "Remove chart from this target"}.render(context)) {
            load_output_group_draft(view, &group, false);
            for (size_t row = 0; row < view.charts.size(); ++row)
                if (view.charts[row].chart_id == group.chart_ids[index])
                    project_state.group_charts[row] = false;
            publish_output_group_action(view, action, WORM_UI_PROJECT_OUTPUT_GROUP_UPDATE,
                                        group.document_index);
            project_state.group_editor_open = false;
        }
        x += chip_width + 4.0f;
    }
    if (group.chart_ids.size() == 0) {
        nk_layout_space_push(context, nk_rect(x, 40.0f, 120.0f, 18.0f));
        nk_label_colored(context, "No charts", NK_TEXT_LEFT,
                         wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::text_muted));
    } else if (group.chart_ids.size() > shown) {
        char more[32];
        std::snprintf(more, sizeof(more), "+%zu charts", group.chart_ids.size() - shown);
        nk_layout_space_push(context, nk_rect(x, 40.0f, 100.0f, 18.0f));
        nk_label(context, more, NK_TEXT_LEFT);
    }
    if (x + 28.0f < width + 80.0f) {
        nk_layout_space_push(context, nk_rect(x, 38.0f, 28.0f, 22.0f));
        if (wormedit::ui::chrome::Button{"+", wormedit::ui::chrome::ButtonKind::secondary,
                                         "Add a compatible chart"}.render(context))
            begin_output_group_editor(view, &group);
    }
}
void render_output_group_card_actions(nk_context* context, const wormedit::ui::setup_view_t& view,
    const wormedit::ui::project_output_group_view_t& group, wormedit::ui::workspace_action_t& action,
    struct nk_vec2 position) {
    using namespace wormedit::ui::chrome;
    nk_layout_space_push(context, nk_rect(position.x, position.y, 84.0f, 28.0f));
    if (Button{"Preview", ButtonKind::secondary, "Check the export plan before publishing",
               view.project_has_native_path && !group.chart_ids.empty()}.render(context)) {
        publish_output_group_action(view, action, WORM_UI_PROJECT_OUTPUT_GROUP_PREVIEW, group.document_index);
        project_state.group_plan_index = group.document_index;
        project_state.group_plan_expanded = true;
    }
    nk_layout_space_push(context, nk_rect(position.x + 88.0f, position.y, 112.0f, 28.0f));
    const auto export_tooltip = std::string{"Publish using saved "} +
                                existing_files_label(static_cast<int>(group.existing_files)) + " policy";
    if (Button{view.project_has_native_path ? "Export" : "Save project…", ButtonKind::primary,
               view.project_has_native_path ? export_tooltip.c_str() : "Save the project before previewing or exporting",
               !group.chart_ids.empty()}.render(context)) {
        if (view.project_has_native_path)
            export_project_group(view, action, group.document_index, WORM_UI_EXISTING_FILES_SAVED);
        else if (action.kind == WORM_UI_WORKSPACE_ACTION_NONE)
            action.kind = WORM_UI_WORKSPACE_ACTION_SAVE_PROJECT;
    }
    nk_layout_space_push(context, nk_rect(position.x + 208.0f, position.y, 28.0f, 28.0f));
    if (Button{"⋯", ButtonKind::secondary, "Export target actions"}.render(context)) {
        project_state.group_menu_index = group.document_index;
        context_menu_open(project_state.group_menu, context->input.mouse.pos,
            PopupContextStamp::from(view, group.document_index + 1));
    }
}
void render_output_group_card(nk_context* context, const wormedit::ui::setup_view_t& view,
                              const wormedit::ui::project_output_group_view_t& group,
                              wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    const bool compact = nk_window_get_content_region(context).w < 650.0f;
    const auto height = compact ? 124.0f : 88.0f;
    nk_layout_space_begin(context, NK_STATIC, height, 10);
    const auto space = nk_layout_space_bounds(context);
    const auto origin = nk_layout_space_to_screen(context, nk_vec2(0.0f, 0.0f));
    const auto card = nk_rect(origin.x, origin.y, space.w, height);
    const auto viewport = wormedit::ui::nuklear::tooltip::viewport();
    const auto width = std::max(1.0f, std::min(card.w, viewport.x + viewport.w - card.x - 8.0f));
    nk_fill_rect(nk_window_get_canvas(context), card, 6.0f, color(UiColor::surface));
    nk_stroke_rect(nk_window_get_canvas(context), card, 6.0f, 1.0f, color(UiColor::border));
    const auto action_x = compact ? 12.0f : std::max(200.0f, width - 240.0f);
    const auto action_y = compact ? 66.0f : 5.0f;
    const auto metadata_end = compact ? width - 12.0f : action_x;
    const auto name_width = std::max(80.0f, std::min(180.0f, (metadata_end - 92.0f) * 0.42f));
    const auto path_x = 92.0f + name_width + 8.0f;
    nk_layout_space_push(context, nk_rect(12.0f, 7.0f, 64.0f, 24.0f));
    render_output_group_badge(context, view.output_families.label(static_cast<wormedit::format_family_t>(group.family), "Unsupported"));
    nk_layout_space_push(context, nk_rect(84.0f, 8.0f, name_width, 22.0f));
    text_cell(context, group.name.data(), name_width);
    nk_layout_space_push(context, nk_rect(path_x, 8.0f, std::max(1.0f, metadata_end - path_x - 8.0f), 22.0f));
    std::string destination{"→ "};
    destination += group.destination_kind == wormedit::output_destination_kind_t::file ? "File · "
                                                                             : "Folder · ";
    destination += group.path.data();
    text_cell(context, destination.c_str(), std::max(1.0f, metadata_end - path_x - 8.0f));
    render_output_group_card_actions(context, view, group, action, nk_vec2(action_x, action_y));
    render_output_group_chips(context, view, group, std::max(1.0f, metadata_end - 92.0f), action);
    nk_layout_space_push(context, nk_rect(84.0f, height - 23.0f, std::max(1.0f, width - 100.0f), 18.0f));
    const auto status = std::string_view{group.preview_status.data() ? group.preview_status.data() : ""};
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
            text_cell(context, row.path.data(), 360.0f);
            nk_label(context, bytes, NK_TEXT_RIGHT);
        }
        for (size_t index = 0; index < group.diagnostics.size(); ++index) {
            nk_layout_row_dynamic(context, 22, 1);
            nk_label_colored(context, group.diagnostics[index].message.data(), NK_TEXT_LEFT,
                             color(group.diagnostics[index].severity == wormedit::diagnostic_severity_t::error
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
auto find_output_group(const wormedit::ui::setup_view_t& view, size_t index)
    -> const wormedit::ui::project_output_group_view_t* {
    const auto group = std::find_if(view.output_groups.begin(), view.output_groups.end(),
        [index](const auto& candidate) { return candidate.document_index == index; });
    return group == view.output_groups.end() ? nullptr : &*group;
}
void render_output_group_menu(nk_context* context, const wormedit::ui::setup_view_t& view,
                              wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    const auto* group = find_output_group(view, project_state.group_menu_index);
    if (!group) { context_menu_reset(project_state.group_menu); return; }
    auto& menu = project_state.group_menu;
    if (!context_menu_begin(menu, context, PopupContextStamp::from(view, group->document_index + 1),
                            nk_vec2(260.0f, context_menu_height(7))))
        return;
    const auto* edit_reason = output_group_editable(view, *group) ? nullptr
        : "This target exceeds the editable Project view limits.";
    const auto* export_reason = view.project_has_native_path ? nullptr : "Save the project first (Ctrl+S).";
    if (context_menu_item(menu, context, "Export · Replace existing", export_reason))
        export_project_group(view, action, group->document_index, WORM_UI_EXISTING_FILES_REPLACE);
    if (context_menu_item(menu, context, "Export · Keep existing", export_reason))
        export_project_group(view, action, group->document_index, WORM_UI_EXISTING_FILES_KEEP);
    if (context_menu_item(menu, context, "Edit", edit_reason)) begin_output_group_editor(view, group);
    if (context_menu_item(menu, context, "Preview", export_reason)) {
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
void render_output_group_delete(nk_context* context, const wormedit::ui::setup_view_t& view,
                                wormedit::ui::workspace_action_t& action) {
    using namespace wormedit::ui::chrome;
    auto& state = project_state;
    if (!state.group_delete_pending) return;
    const auto* group = find_output_group(view, state.group_index);
    if (state.group_revision != view.document_revision || !group) {
        state.group_delete_pending = false;
        return;
    }
    if (dialog_begin(state.group_delete_dialog, context, "Delete export target", "Delete export target?")) {
        dialog_text(state.group_delete_dialog, context, group->name.data(), color(UiColor::text_bright));
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
void render_output_groups(nk_context* context, const wormedit::ui::setup_view_t& view, wormedit::ui::workspace_action_t& action) {
    (void)wormedit::ui::chrome::context_menu_update(project_state.group_menu, context);
    project_section_gap(context);
    begin_project_section_header(context, {110.0f, 96.0f});
    char heading[64];
    std::snprintf(heading, sizeof(heading), "Exports · %zu", view.output_groups.size());
    project_section_heading(context, heading);
    if (wormedit::ui::chrome::Button{"Export all", wormedit::ui::chrome::ButtonKind::secondary,
                                     "Prepare every export target, then publish them",
                                     view.project_has_native_path && !view.output_groups.empty()}
            .render(context))
        export_all_project_groups(view, action);
    if (wormedit::ui::chrome::Button{"+ New", wormedit::ui::chrome::ButtonKind::secondary,
                                     "Configure a new export target"}.render(context))
        begin_output_group_editor(view, nullptr);
    if (view.output_groups.size() == 0) {
        nk_layout_row_dynamic(context, 24, 1);
        nk_label_wrap(context, view.output_suggestions.empty()
            ? "No export targets yet. Choose + New to configure a target."
            : "No export targets yet. Start with a target suggested for this layout:");
        // An empty row would still take its height, so only lay it out for suggestions.
        constexpr int columns = 3;
        if (!view.output_suggestions.empty()) nk_layout_row_dynamic(context, 30, columns);
        for (const auto& suggestion : view.output_suggestions)
            if (wormedit::ui::chrome::Button{suggestion.name.data(),
                    wormedit::ui::chrome::ButtonKind::secondary,
                    "Open a normal target draft"}.render(context))
                begin_output_group_suggestion(view, suggestion);
    }
    for (size_t i = 0; i < view.output_groups.size(); ++i)
        render_output_group_card(context, view, view.output_groups[i], action);
    render_output_group_menu(context, view, action);
}

} // namespace

bool worm_ui_nuklear_project_chart_shortcut(const wormedit::ui::setup_view_t& view,
    const wormedit::ui::shortcut_event_t& key, wormedit::ui::workspace_action_t& action) {
    using namespace wormedit;
    namespace chrome = ui::chrome;
    const bool up = key.hid_usage == shortcut_key_t::up;
    if (!view.visible || key.modifiers != WORM_UI_SHORTCUT_MODIFIER_ALT ||
        (!up && key.hid_usage != shortcut_key_t::down) || project_state.drag.source ||
        chrome::context_menu_focused_target() != chrome::KeyboardContextTarget::charts)
        return false;
    const auto id = chrome::context_menu_focused_object();
    if (!id || id != project_state.menu_chart) return false;
    const auto row = std::ranges::find_if(view.charts,
        [id](const auto& item) { return item.chart_id.value == id; });
    if (row == view.charts.end()) return false;
    if ((up && row != view.charts.begin()) || (!up && std::next(row) != view.charts.end()))
        publish_chart_action(view, action, up ? WORM_UI_PROJECT_CHART_UP : WORM_UI_PROJECT_CHART_DOWN, id);
    return true;
}

bool worm_ui_nuklear_setup_popup_owns_input() {
    return project_state.creation_stamp.has_value() || project_state.selection.pending_delete != 0 || !project_state.delete_selection.empty() ||
        project_state.metadata_popup.owns_input() || project_state.release_pending ||
        project_state.group_editor_open || project_state.group_delete_pending ||
        project_state.membership_chart != 0 ||
        wormedit::ui::chrome::context_menu_owns_input(project_state.row_menu) ||
        wormedit::ui::chrome::context_menu_owns_input(project_state.group_menu);
}

void worm_ui_nuklear_sync_setup_popups(nk_context* context, const wormedit::ui::setup_view_t* view) {
    const auto& selection = project_state.selection;
    const bool same_session = view && view->visible && selection.song == view->song_id.value &&
        selection.session_generation == view->session_generation;
    if (!same_session) {
        if (wormedit::ui::chrome::context_menu_focused_target() ==
            wormedit::ui::chrome::KeyboardContextTarget::charts)
            wormedit::ui::chrome::context_menu_focus(wormedit::ui::chrome::KeyboardContextTarget::none);
        if (context && project_state.delete_dialog.open) nk_window_close(context, "Delete chart");
        auto retired_metadata = project_state.metadata_popup;
        retired_metadata.cancel();
        auto retired_menu = project_state.row_menu;
        auto retired_group_menu = project_state.group_menu;
        auto project_directory = std::move(project_state.project_directory);
        wormedit::ui::chrome::context_menu_abandon(retired_menu);
        wormedit::ui::chrome::context_menu_abandon(retired_group_menu);
        project_state = {};
        project_state.metadata_popup = std::move(retired_metadata);
        project_state.row_menu = retired_menu;
        project_state.group_menu = retired_group_menu;
        project_state.project_directory = std::move(project_directory);
    } else {
        worm_ui_nuklear_reconcile_project_selection(project_state.selection, *view);
        worm_ui_nuklear_reconcile_chart_drag(project_state.drag, *view);
    }
}

void worm_ui_nuklear_retire_setup_popups(nk_context* context) {
    auto& popup = project_state.metadata_popup;
    if (popup.close_pending && !wormedit::ui::chrome::popup_panel(context)) {
        wormedit::ui::chrome::dismiss_popup(context, "Project chart metadata", popup.bounds);
        popup.close_pending = false;
    }
    if (!popup.open) (void)wormedit::ui::chrome::release_retained_popup(context, popup,
        wormedit::ui::chrome::PopupReleaseKeys::all);
    wormedit::ui::chrome::context_menu_retire(project_state.row_menu, context);
    wormedit::ui::chrome::context_menu_retire(project_state.group_menu, context);
}

namespace {
void emit_project_setting(const wormedit::ui::setup_view_t& view,
                          wormedit::ui::workspace_action_t& action, wormedit::project_setting_field_t field,
                          bool use_default, std::uint32_t integer = 0, std::string_view text = {}) {
    if (action.project_settings.requested) return;
    action.project_settings = {.requested = true, .project = view.song_id,
        .revision = view.document_revision, .session_generation = view.session_generation,
        .field = field, .use_program_default = use_default, .integer = integer, .text = std::string{text}};
}
auto project_default_toggle(nk_context* context, const char* label, bool overridden) -> bool {
    nk_layout_row_dynamic(context, 25, 1);
    nk_bool use_default = !overridden;
    return nk_checkbox_label(context, label, &use_default) != 0;
}
void render_project_snap(nk_context* context, const wormedit::ui::setup_view_t& view,
                         wormedit::ui::workspace_action_t& action) {
    const auto& settings = view.settings;
    if (project_default_toggle(context, "Use program default for new-chart snap", settings.snap_overridden))
        emit_project_setting(view, action, wormedit::project_setting_field_t::default_snap,
            settings.snap_overridden, settings.default_snap);
    if (!settings.snap_overridden) nk_widget_disable_begin(context);
    int snap = static_cast<int>(settings.default_snap);
    nk_layout_row_dynamic(context, 28, 1);
    nk_property_int(context, "New-chart snap denominator", 1, &snap, 1920, 1, 1);
    if (settings.snap_overridden && snap != static_cast<int>(settings.default_snap))
        emit_project_setting(view, action, wormedit::project_setting_field_t::default_snap, false, static_cast<std::uint32_t>(snap));
    if (!settings.snap_overridden) nk_widget_disable_end(context);
    nk_layout_row_dynamic(context, 20, 1);
    nk_labelf(context, NK_TEXT_LEFT, "Program default: 1/%u", settings.program_default_snap);
}
void render_project_directory(nk_context* context, const wormedit::ui::setup_view_t& view,
                              wormedit::ui::workspace_action_t& action, bool visual) {
    const auto& settings = view.settings;
    const bool overridden = visual ? settings.visual_overridden : settings.audio_overridden;
    const auto field = visual ? wormedit::project_setting_field_t::visual_directory : wormedit::project_setting_field_t::audio_directory;
    auto& buffer = visual ? project_state.visual_directory : project_state.audio_directory;
    if (project_default_toggle(context, visual ? "Use program default for visual assets" : "Use program default for audio assets", overridden))
        emit_project_setting(view, action, field, overridden, 0, buffer.data());
    if (!overridden) nk_widget_disable_begin(context);
    constexpr float ratios[]{0.8f, 0.2f};
    nk_layout_row(context, NK_DYNAMIC, 28, 2, ratios);
    wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        buffer.data(), static_cast<int>(buffer.size()), nk_filter_default);
    if (nk_button_label(context, visual ? "Set visual folder" : "Set audio folder") && overridden)
        emit_project_setting(view, action, field, false, 0, buffer.data());
    if (!overridden) nk_widget_disable_end(context);
}
void render_project_settings(nk_context* context, const wormedit::ui::setup_view_t& view,
                             wormedit::ui::workspace_action_t& action) {
    if (!wormedit::ui::chrome::section_push(context, NK_TREE_TAB, "Project settings", NK_MINIMIZED)) return;
    if (project_state.settings_revision != view.document_revision) {
        std::snprintf(project_state.audio_directory.data(), project_state.audio_directory.size(), "%.*s",
            static_cast<int>(view.settings.audio_directory.size()), view.settings.audio_directory.empty() ? "" : view.settings.audio_directory.data());
        std::snprintf(project_state.visual_directory.data(), project_state.visual_directory.size(), "%.*s",
            static_cast<int>(view.settings.visual_directory.size()), view.settings.visual_directory.empty() ? "" : view.settings.visual_directory.data());
        project_state.settings_revision = view.document_revision;
    }
    nk_layout_row_dynamic(context, 32, 1);
    nk_label_wrap(context, "Overrides are saved with this project and can be undone. Asset folders apply to future copies; existing files stay in place.");
    render_project_snap(context, view, action);
    render_project_directory(context, view, action, false);
    render_project_directory(context, view, action, true);
    wormedit::ui::chrome::section_pop(context);
}
}

void worm_ui_nuklear_render_setup_panel(struct nk_context* context,
                                        const wormedit::ui::setup_view_t* view,
                                        wormedit::ui::workspace_action_t* action) {
    if (!context || !view || !action || !view->visible)
        return;

    nk_layout_row_dynamic(context, 22, 1);
    nk_labelf_colored(context, NK_TEXT_LEFT, wormedit::ui::chrome::color(wormedit::ui::chrome::UiColor::text_muted),
                      "Audio assets: %zu · Visual assets: %zu", view->audio_asset_count, view->visual_asset_count);
    render_project_charts(context, *view, *action);
    render_output_groups(context, *view, *action);
    project_section_gap(context);
    render_project_settings(context, *view, *action);
}

void worm_ui_nuklear_render_setup_dialogs(nk_context* context, const wormedit::ui::setup_view_t& view,
                                         wormedit::ui::workspace_action_t& action) {
    if (!view.visible) return;
    render_creation_dialog(context, view, action);
    render_delete_confirmation(context, view, action);
    render_output_group_editor(context, view, action);
    render_output_group_delete(context, view, action);
    render_chart_memberships(context, view, action);
}

void worm_ui_nuklear_cancel_setup_drag() {
    project_state.drag = {};
}

void worm_ui_nuklear_set_project_directory(const char* path) {
    project_state.project_directory = path ? path : "";
}
