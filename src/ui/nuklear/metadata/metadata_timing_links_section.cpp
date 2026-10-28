#include "ui/nuklear/metadata/metadata_timing_links_section.h"
#include "ui/nuklear/metadata/metadata_panel_internal.h"
#include "ui/nuklear/chrome/chrome.h"
#include <algorithm>
using namespace wormedit;
namespace chrome = wormedit::ui::chrome;
void metadata_timing_link_draft_t::synchronize(const ui::metadata_view_t& view) {
    if (!view.visible || !view.has_chart || !view.chart_fields_editable || view.timing_locked) confirmation_.cancel();
    if (context_ == view.timing_link_context) return;
    confirmation_.cancel();
    context_ = view.timing_link_context;
    selected_ = {};
    for (const auto& row : view.timing_links) source_for(row.category) = row.source;
}
void metadata_timing_link_draft_t::update_release(nk_context* context) {
    (void)chrome::popup_update_release_pending(context, confirmation_.release_pending);
}
void metadata_timing_link_draft_t::request(const ui::metadata_view_t& view,
    const ui::timing_link_category_view_t& row, timing_link_operation_t operation, ui::workspace_action_t& action) {
    if (!view.visible || !view.has_chart || !view.chart_fields_editable || view.timing_locked || action.timing_link.requested || confirmation_.owns_input()) return;
    ui::timing_link_action_t request{.requested = true, .context = view.timing_link_context,
        .request = {.category = row.category, .operation = operation,
            .source = operation == timing_link_operation_t::relink ? row.source : source_for(row.category)}};
    if (operation != timing_link_operation_t::detach && row.requires_confirmation) {
        pending_ = request;
        confirmation_.open = true;
        dialog_ = {};
    } else action.timing_link = request;
}
void metadata_timing_link_draft_t::render_confirmation(nk_context* context,
    const ui::metadata_view_t& view, ui::workspace_action_t& action) {
    synchronize(view);
    if (!confirmation_.open) return;
    if (chrome::dialog_begin(dialog_, context, "Replace category timing", "Replace local timing?", {.enter_confirms = false})) {
        const auto row = std::ranges::find(view.timing_links, pending_.request.category, &ui::timing_link_category_view_t::category);
        if (row != view.timing_links.end()) {
            nk_layout_row_dynamic(context, 24, 1);
            nk_label(context, row->label.c_str(), NK_TEXT_LEFT);
        }
        chrome::dialog_text(dialog_, context, "This replaces this category's local data with its source timing. You can Undo the change.", context->style.text.color);
        chrome::dialog_footer_begin(dialog_, context, 2, true);
        if (chrome::dialog_button(dialog_, context, "Replace", chrome::DialogButtonRole::destructive)) {
            pending_.request.discard_confirmed = true;
            action.timing_link = pending_;
        }
        (void)chrome::dialog_button(dialog_, context, "Cancel", chrome::DialogButtonRole::secondary);
    }
    if (chrome::dialog_end(dialog_, context) != chrome::DialogResult::none) confirmation_.cancel();
}
namespace {
auto source_name(const ui::metadata_view_t& view, chart_id_t source) -> const char* {
    const auto found = std::ranges::find(view.chart_choices, source, &ui::metadata_chart_choice_view_t::chart_id);
    return found == view.chart_choices.end() ? "Choose source" : found->name.c_str();
}
void render_source_choice(const MetadataPanel& panel, timing_category_t category) {
    auto& selected = state.timing_links.source_for(category);
    const auto& view = *panel.metadata;
    if (!chrome::safe_combo_begin_label(panel.context, source_name(view, selected), nk_vec2(280, 240))) return;
    nk_layout_row_dynamic(panel.context, 24, 1);
    for (const auto& chart : view.chart_choices) {
        if (chart.chart_id == view.chart_id) continue;
        if (nk_combo_item_label(panel.context, chart.name.c_str(), NK_TEXT_LEFT)) selected = chart.chart_id;
    }
    chrome::safe_combo_end(panel.context);
}
void render_link_buttons(const MetadataPanel& panel, const ui::timing_link_category_view_t& row) {
    const bool can_link = state.timing_links.source_for(row.category) &&
        (row.is_local_override || state.timing_links.source_for(row.category) != row.source);
    if (!can_link) nk_widget_disable_begin(panel.context);
    const bool link = nk_button_label(panel.context, "Link") && can_link;
    if (!can_link) nk_widget_disable_end(panel.context);
    if (link) state.timing_links.request(*panel.metadata, row, timing_link_operation_t::link, *panel.action);
    if (!row.source) nk_widget_disable_begin(panel.context);
    const bool detach = nk_button_label(panel.context, "Detach") && row.source;
    if (!row.source) nk_widget_disable_end(panel.context);
    if (detach) state.timing_links.request(*panel.metadata, row, timing_link_operation_t::detach, *panel.action);
    if (!row.is_local_override) nk_widget_disable_begin(panel.context);
    const bool relink = nk_button_label(panel.context, "Relink") && row.is_local_override;
    if (!row.is_local_override) nk_widget_disable_end(panel.context);
    if (relink) state.timing_links.request(*panel.metadata, row, timing_link_operation_t::relink, *panel.action);
}
void render_link_row(const MetadataPanel& panel, const ui::timing_link_category_view_t& row) {
    constexpr float widths[]{0.18f, 0.19f, 0.30f, 0.10f, 0.12f, 0.11f};
    nk_layout_row(panel.context, NK_DYNAMIC, 28, 6, widths);
    nk_label(panel.context, row.label.c_str(), NK_TEXT_LEFT);
    nk_label(panel.context, row.status.c_str(), NK_TEXT_LEFT);
    render_source_choice(panel, row.category);
    render_link_buttons(panel, row);
}
}
void render_metadata_timing_links_section(const MetadataPanel& panel) {
    const auto& view = *panel.metadata;
    if (view.timing_links.empty()) return;
    state.timing_links.synchronize(view);
    if (!metadata_tree_push(panel.context, NK_TREE_NODE, "Timing links", NK_MINIMIZED, 754)) return;
    muted_label(panel.context, "Choose a source chart for each category. Local edits create an override.");
    const bool editable = view.chart_fields_editable && !view.timing_locked;
    if (!editable) nk_widget_disable_begin(panel.context);
    for (const auto& row : view.timing_links) render_link_row(panel, row);
    if (!editable) nk_widget_disable_end(panel.context);
    wormedit::ui::chrome::section_pop(panel.context);
}
void synchronize_metadata_timing_links(nk_context* context, const ui::metadata_view_t& view) {
    auto& draft = worm_ui_nuklear_metadata_state().timing_links;
    draft.update_release(context);
    draft.synchronize(view);
}
auto metadata_timing_links_own_input() -> bool { return worm_ui_nuklear_metadata_state().timing_links.owns_input(); }
void render_metadata_timing_link_confirmation(nk_context* context, const ui::metadata_view_t& view, ui::workspace_action_t& action) {
    worm_ui_nuklear_metadata_state().timing_links.render_confirmation(context, view, action);
}
