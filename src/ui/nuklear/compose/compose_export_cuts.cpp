#include "ui/nuklear/compose/compose_export_cuts.h"
#include "ui/nuklear/compose/compose_state.h"
#include "ui/nuklear/compose/compose_action_stamp.h"
#include "ui/nuklear/chrome/chrome.h"
#include <algorithm>
#include <cstdio>

namespace {
using editor_t = worm_ui_nuklear_compose_state_t::export_cut_editor_t;

auto raise(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view,
           WormUiComposeActionKind kind, const editor_t& editor) -> bool {
    if (!wormedit::ui::raise_compose_action(action, view, kind,
            wormedit::ui::compose_project_stamp_t::preserve)) return false;
    action.compose_export_cut_id = editor.selected;
    action.compose_marker_label = editor.label.data();
    action.compose_marker_audio_begin = editor.begin.data();
    action.compose_marker_audio_end = editor.end.data();
    return true;
}

void render_cut_rows(nk_context* context, const wormedit::ui::compose_view_t& view, editor_t& editor) {
    if (view.export_cuts.empty()) {
        nk_layout_row_dynamic(context, 22, 1);
        nk_label(context, "No export cuts", NK_TEXT_LEFT);
        return;
    }
    nk_layout_row_dynamic(context, std::min(156.0f, 26.0f * view.export_cuts.size()), 1);
    struct nk_list_view list{};
    if (!nk_list_view_begin(context, &list, "Song export cuts", NK_WINDOW_BORDER, 26,
                            static_cast<int>(view.export_cuts.size()))) return;
    for (auto index = list.begin; index < std::min(list.end, static_cast<int>(view.export_cuts.size())); ++index) {
        const auto& cut = view.export_cuts[static_cast<std::size_t>(index)];
        nk_bool selected = cut.id == editor.selected;
        nk_layout_row_dynamic(context, 24, 1);
        if (nk_selectable_label(context, cut.label.empty() ? "Export cut" : cut.label.c_str(), NK_TEXT_LEFT, &selected))
            editor.select(cut);
    }
    nk_list_view_end(&list);
}

void render_cut_fields(nk_context* context, editor_t& editor) {
    nk_layout_row_dynamic(context, 18, 1);
    nk_label(context, "Label (optional)", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 25, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        editor.label.data(), static_cast<int>(editor.label.size()), nk_filter_default);
    nk_layout_row_dynamic(context, 18, 2);
    nk_label(context, "Start (ms)", NK_TEXT_LEFT);
    nk_label(context, "End (ms)", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 25, 2);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        editor.begin.data(), static_cast<int>(editor.begin.size()), nk_filter_decimal);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        editor.end.data(), static_cast<int>(editor.end.size()), nk_filter_decimal);
}
} // namespace

void editor_t::select(const wormedit::ui::compose_export_cut_view_t& cut) {
    selected = cut.id;
    std::snprintf(label.data(), label.size(), "%s", cut.label.c_str());
    std::snprintf(begin.data(), begin.size(), "%lld", static_cast<long long>(cut.span.start_milliseconds));
    std::snprintf(end.data(), end.size(), "%lld", static_cast<long long>(cut.span.end_milliseconds));
}

void editor_t::synchronize(const wormedit::ui::compose_view_t& view) {
    if (project_id == view.project_id.value && revision == view.document_revision) return;
    if (project_id != view.project_id.value) selected = {};
    project_id = view.project_id.value;
    revision = view.document_revision;
    const auto found = std::ranges::find(view.export_cuts, selected, &wormedit::ui::compose_export_cut_view_t::id);
    if (found != view.export_cuts.end()) select(*found);
    else {
        selected = {};
        label.fill('\0');
    }
}

void worm_ui_nuklear_render_compose_export_cuts(nk_context* context,
    const wormedit::ui::compose_view_t& view, wormedit::ui::workspace_action_t& action) {
    auto& editor = worm_ui_nuklear_compose_state().export_cut_editor;
    editor.synchronize(view);
    if (!nk_tree_push(context, NK_TREE_TAB, "Export cuts", NK_MINIMIZED)) return;
    if (view.export_cut_overlay_truncated) {
        nk_layout_row_dynamic(context, 34, 1);
        nk_label_wrap(context, "Some cut bands exceed the display limit; every cut remains in this list.");
    }
    render_cut_rows(context, view, editor);
    nk_layout_row_dynamic(context, 25, 1);
    const auto can_create = view.scene_available && view.preview_audio_range.available;
    if (!can_create) nk_widget_disable_begin(context);
    const auto create = nk_button_label(context, "Create from selection");
    if (!can_create) nk_widget_disable_end(context);
    if (create && can_create)
        (void)raise(action, view, WORM_UI_COMPOSE_ACTION_ADD_EXPORT_CUT_FROM_SELECTION, editor);
    if (!editor.selected.value) nk_widget_disable_begin(context);
    render_cut_fields(context, editor);
    nk_layout_row_dynamic(context, 25, 2);
    const auto apply = nk_button_label(context, "Apply cut");
    const auto remove = nk_button_label(context, "Delete cut");
    if (!editor.selected.value) nk_widget_disable_end(context);
    if (editor.selected.value && apply) (void)raise(action, view, WORM_UI_COMPOSE_ACTION_UPDATE_EXPORT_CUT, editor);
    if (editor.selected.value && remove) (void)raise(action, view, WORM_UI_COMPOSE_ACTION_DELETE_EXPORT_CUT, editor);
    nk_tree_pop(context);
}
