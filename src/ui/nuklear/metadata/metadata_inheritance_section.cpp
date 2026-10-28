#include "ui/nuklear/metadata/metadata_inheritance_section.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/tooltip.h"
#include <cstdio>

namespace {
void render_inheritance_row(const MetadataPanel& panel,
    const wormedit::ui::metadata_inheritance_field_view_t& field) {
    auto* context = panel.context;
    auto& row = state.inheritance.row_for(field.field);
    const bool editable = panel.metadata->chart_fields_editable && row.can_override;
    metadata_form_row(context, 26);
    char label[64];
    std::snprintf(label, sizeof(label), "Inherit %s", field.label.c_str());
    if (!editable) nk_widget_disable_begin(context);
    nk_bool inherit = row.inherit;
    (void)nk_checkbox_label(context, label, &inherit);
    if (!editable) nk_widget_disable_end(context);
    if (editable) row.inherit = inherit != 0;
    if (row.inherit) nk_label_colored(context, field.inherited.empty() ? "(empty)" : field.inherited.c_str(),
        NK_TEXT_LEFT, wormedit::ui::theme::ui(wormedit::ui::theme::UiColor::text_muted));
    else {
        if (!editable) nk_widget_disable_begin(context);
        (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
            row.text.data(), row.text.size(), nk_filter_default);
        if (!editable) nk_widget_disable_end(context);
    }
    if (!row.can_override && (context->last_widget_state & NK_WIDGET_STATE_HOVER) != 0)
        wormedit::ui::nuklear::tooltip::text(context, "This source value exceeds the editable metadata limit.");
}
void apply_inheritance(const MetadataPanel& panel) {
    auto& action = *panel.action;
    if (action.metadata_edit != WORM_UI_METADATA_EDIT_NONE) return;
    action.metadata_edit = WORM_UI_METADATA_EDIT_INHERITANCE;
    action.metadata_document_revision = panel.metadata->document_revision;
    action.metadata_chart_id = panel.metadata->chart_id;
    action.metadata_overrides = state.inheritance.overrides();
}
}
void render_metadata_inheritance_section(const MetadataPanel& panel) {
    const auto& metadata = *panel.metadata;
    if (metadata.inheritance.empty()) return;
    auto* context = panel.context;
    if (!metadata_tree_push(context, NK_TREE_NODE, "Song field overrides", NK_MINIMIZED, 125)) return;
    muted_label(context, "Export values for this difficulty.");
    state.inheritance.synchronize(metadata);
    for (const auto& field : metadata.inheritance) render_inheritance_row(panel, field);
    const bool changed = metadata.chart_fields_editable && state.inheritance.is_changed(metadata);
    bool revert{}, apply{};
    render_footer_buttons(context, changed, changed, "Revert fields", "Apply fields", revert, apply);
    if (revert) state.inheritance.reset(metadata);
    if (apply) apply_inheritance(panel);
    wormedit::ui::chrome::section_pop(context);
}
