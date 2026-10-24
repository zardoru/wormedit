#include "ui/nuklear/audio/preview_audio_dialog.h"
#include <cstdio>

namespace wormedit::ui {
void preview_audio_dialog_t::start(const wormedit::ui::compose_view_t& view, preview_audio_range_view_t source,
                                   std::uint64_t begin_row, std::uint64_t end_row) {
    if (!source.available || begin_row >= end_row) return;
    *this = {};
    open = true;
    draft = {.project = view.project_id.value, .chart = view.chart_id.value, .revision = view.document_revision,
        .begin_row = begin_row, .end_row = end_row};
    range = source;
    duration = std::min(range.end_seconds - range.begin_seconds, 600.0);
    std::snprintf(filename.data(), filename.size(), "%s", "preview.ogg");
}

void preview_audio_dialog_t::begin(const wormedit::ui::compose_view_t& view) {
    start(view, view.preview_audio_range, view.area_range_begin_row, view.area_range_end_row);
}

void preview_audio_dialog_t::begin_from_preview(const wormedit::ui::compose_view_t& view) {
    const auto& span = view.preview_span;
    if (!span.available) return;
    start(view, preview_audio_range_view_t::from(static_cast<double>(span.start_milliseconds) / 1000.0,
                                                 static_cast<double>(span.end_milliseconds) / 1000.0),
          span.begin_row, span.end_row);
}

void preview_audio_dialog_t::render(nk_context* context, const wormedit::ui::compose_view_t& view,
                                   wormedit::ui::workspace_action_t& action) {
    if (!open) return;
    if (draft.project != view.project_id.value || draft.chart != view.chart_id.value || draft.revision != view.document_revision) {
        open = false;
        return;
    }
    if (chrome::dialog_begin(dialog, context, "Export preview audio", "Export preview audio",
        {.width = 600, .enter_confirms = false})) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_labelf(context, NK_TEXT_LEFT, "Source interval: %.3f–%.3f s (%.3f s)",
            range.begin_seconds, range.end_seconds, range.end_seconds - range.begin_seconds);
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "Output filename (relative to the project folder)", NK_TEXT_LEFT);
        nk_layout_row_dynamic(context, 28, 1);
        chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD, filename.data(), filename.size(), nk_filter_default);
        nk_layout_row_dynamic(context, 28, 1);
        nk_property_double(context, "Duration (seconds)", std::min(0.01, range.end_seconds - range.begin_seconds), &duration,
            std::min(range.end_seconds - range.begin_seconds, 600.0), 0.1, 0.1F);
        nk_layout_row_dynamic(context, 28, 1);
        nk_property_int(context, "Fade out (ms)", 0, &fade_out_ms, 10000, 100, 10);
        nk_layout_row_dynamic(context, 24, 1);
        nk_checkbox_label(context, "Replace an existing file", &replace_existing);
        nk_layout_row_dynamic(context, 36, 1);
        nk_label_wrap(context, "The interval is mixed as chart autoplay. A shorter duration trims its end; fades do not change chart data.");
        chrome::dialog_footer_begin(dialog, context, 2);
        (void)chrome::dialog_button(dialog, context, "Cancel", chrome::DialogButtonRole::secondary);
        (void)chrome::dialog_button(dialog, context, "Export", chrome::DialogButtonRole::primary, filename[0] != '\0');
    }
    switch (chrome::dialog_end(dialog, context)) {
    case chrome::DialogResult::primary:
        draft.filename = filename.data(); draft.duration_seconds = duration;
        draft.fade_out_ms = static_cast<std::uint32_t>(fade_out_ms); draft.replace_existing = replace_existing != 0;
        action.preview_audio_export = draft; open = false; break;
    case chrome::DialogResult::cancel:
    case chrome::DialogResult::secondary: open = false; break;
    default: break;
    }
}
}
