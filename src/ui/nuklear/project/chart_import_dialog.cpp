#include "ui/nuklear/project/chart_import_dialog.h"

#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/dialog.h"

#include <array>
#include <cstring>
#include <string>

namespace {

namespace chrome = wormedit::ui::chrome;

struct path_field_t {
    std::array<char, wormedit::maximum_project_path_bytes + 1> text{};
    std::string seen;  // The view path the field was last reset from.
};

auto path_field() -> path_field_t& {
    static path_field_t field;
    return field;
}

void emit(wormedit::ui::chart_import_action_t& import_request, WormUiChartImportActionKind kind, wormedit::chart_id_t chart = {},
          std::uint64_t revision = 0) {
    if (import_request.kind != WORM_UI_CHART_IMPORT_NONE) return;
    import_request = {.kind = kind, .chart_id = chart, .document_revision = revision};
}

// Resets the text field when the session shows a different path (a picked or retried file).
void sync_path_field(const wormedit::ui::chart_import_view_t& view) {
    auto& field = path_field();
    if (view.path == field.seen) return;
    field.seen = view.path;
    std::snprintf(field.text.data(), field.text.size(), "%s", field.seen.c_str());
}

auto render_path_row(nk_context* context, wormedit::ui::chart_import_action_t& import_request,
                     wormedit::ui::project_open_request_t& project_open) -> bool {
    auto& field = path_field();
    constexpr float row = wormedit::ui::metrics::control_default;
    nk_layout_row_template_begin(context, row);
    nk_layout_row_template_push_variable(context, 120.0f);
    nk_layout_row_template_push_static(context, 96.0f);
    nk_layout_row_template_push_static(context, 64.0f);
    nk_layout_row_template_end(context);
    chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, field.text.data(),
                                       static_cast<int>(field.text.size()), nk_filter_default);
    const bool browse = chrome::Button{"Browse…", chrome::ButtonKind::secondary,
                                       "Pick a chart file with the system dialog"}.render(context);
    if (chrome::Button{"Read", chrome::ButtonKind::secondary, "Read the chart file at this path",
                       field.text[0] != '\0'}.render(context)) {
        emit(import_request, WORM_UI_CHART_IMPORT_LOAD);
        project_open.path = field.text.data();
    }
    return browse;
}

void render_difficulties(nk_context* context, const wormedit::ui::chart_import_view_t& view,
                         wormedit::ui::chart_import_action_t& import_request) {
    for (const auto& row : view.rows) {
        nk_layout_row_dynamic(context, wormedit::ui::metrics::control_default, 1);
        nk_bool selected = row.selected ? nk_true : nk_false;
        const std::string label{row.label};
        if (wormedit::ui::chrome::checkbox_label(context, label.c_str(), &selected))
            emit(import_request, WORM_UI_CHART_IMPORT_TOGGLE, row.chart_id);
    }
}

} // namespace

auto worm_ui_nuklear_render_chart_import_dialog(struct nk_context* context,
    const wormedit::ui::chart_import_view_t& view, wormedit::ui::chart_import_action_t& import_request,
    wormedit::ui::project_open_request_t& project_open, int window_width, int window_height) -> bool {
    if (!context || !view.visible) return false;
    static chrome::Dialog dialog;
    bool browse = false;
    sync_path_field(view);
    if (chrome::dialog_begin(dialog, context, "Import chart", "Import chart",
            {.modal = true, .width = 640.0f,
             .area = nk_vec2(static_cast<float>(window_width), static_cast<float>(window_height)),
             .enter_confirms = false})) {
        browse = render_path_row(context, import_request, project_open);
        if (!view.error.empty())
            chrome::dialog_text(dialog, context, view.error.data(), chrome::color(chrome::UiColor::conflict));
        if (view.rows.empty() && view.error.empty())
            chrome::dialog_text(dialog, context, "Pick or type a chart file to list its difficulties.",
                                chrome::color(chrome::UiColor::text_muted));
        render_difficulties(context, view, import_request);
        if (!view.summary.empty())
            chrome::dialog_text(dialog, context, view.summary.data(), chrome::color(chrome::UiColor::text_bright));
        for (const auto note : view.notes)
            chrome::dialog_text(dialog, context, note.data(), chrome::color(chrome::UiColor::text_muted));
        chrome::dialog_footer_begin(dialog, context, 2);
        if (chrome::dialog_button(dialog, context, "Cancel", chrome::DialogButtonRole::secondary))
            emit(import_request, WORM_UI_CHART_IMPORT_CANCEL);
        if (chrome::dialog_button(dialog, context, "Import", chrome::DialogButtonRole::primary, view.can_import))
            emit(import_request, WORM_UI_CHART_IMPORT_CONFIRM, {}, view.document_revision);
    }
    if (chrome::dialog_end(dialog, context) == chrome::DialogResult::cancel)
        emit(import_request, WORM_UI_CHART_IMPORT_CANCEL);
    return browse;
}
