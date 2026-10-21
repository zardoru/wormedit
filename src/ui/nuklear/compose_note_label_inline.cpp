#include "ui/nuklear/compose_note_label_inline.h"

#include <algorithm>
#include <cstring>

namespace {
constexpr float popup_width = 300.0f;
constexpr float popup_height = 58.0f;
constexpr float gap_above_note = 40.0f;
} // namespace

auto begin_compose_note_label_inline(inline_label_editor_t& editor,
                                     const wormedit::ui::compose_view_t& view, float anchor_x,
                                     float anchor_y) -> bool {
    if (editor.release_pending || view.selected_note_count != 1 ||
        view.selected_note_label_note_id.value == 0)
        return false;
    editor = {};
    editor.kind = inline_label_kind_t::note_label;
    editor.active = true;
    editor.focus_pending = true;
    editor.project_id = view.project_id.value;
    editor.chart_id = view.chart_id.value;
    editor.document_revision = view.document_revision;
    editor.anchor_x = anchor_x;
    editor.anchor_y = anchor_y;
    if (view.selected_note_label.c_str())
        std::strncpy(editor.label, view.selected_note_label.c_str(), sizeof(editor.label) - 1);
    return true;
}

auto note_label_inline_bounds(const inline_label_editor_t& editor, struct nk_rect content)
    -> struct nk_rect {
    const auto width = std::min(popup_width, content.w);
    const auto x = std::clamp(editor.anchor_x - content.x - width * 0.5f, 0.0f,
                              std::max(0.0f, content.w - width));
    const auto y = std::clamp(editor.anchor_y - content.y - gap_above_note, 0.0f,
                              std::max(0.0f, content.h - popup_height));
    return nk_rect(x, y, width, popup_height);
}

void stage_note_label_inline(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view,
                             const inline_label_editor_t& editor) {
    action.compose_action = editor.label[0] == '\0' ? WORM_UI_COMPOSE_ACTION_CLEAR_NOTE_LABEL
                                                    : WORM_UI_COMPOSE_ACTION_APPLY_NOTE_LABEL;
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = editor.chart_id;
    action.compose_input_document_revision = editor.document_revision;
    action.compose_note_label = editor.label;

}

auto begin_compose_bookmark_rename_anchored(inline_label_editor_t& editor,
                                            const wormedit::ui::compose_view_t& view,
                                            std::uint64_t bookmark_id, float anchor_x,
                                            float anchor_y) -> bool {
    if (!begin_compose_bookmark_rename(editor, view, bookmark_id)) return false;
    editor.anchored = true;
    editor.anchor_x = anchor_x;
    editor.anchor_y = anchor_y;
    return true;
}

auto begin_compose_section_label_inline(inline_label_editor_t& editor,
                                        const wormedit::ui::compose_view_t& view, std::uint64_t section_id,
                                        float anchor_x, float anchor_y) -> bool {
    if (editor.release_pending) return false;
    const auto& sections = view.beat_sections;
    const auto found = std::ranges::find_if(sections,
        [&](const wormedit::ui::compose_section_view_t& section) { return section.section_id.value == section_id; });
    if (found == sections.end()) return false;
    editor = {};
    editor.kind = inline_label_kind_t::section_label;
    editor.active = true;
    editor.focus_pending = true;
    editor.anchored = true;
    editor.project_id = view.project_id.value;
    editor.chart_id = view.chart_id.value;
    editor.document_revision = view.document_revision;
    editor.bookmark_id = section_id;
    editor.anchor_x = anchor_x;
    editor.anchor_y = anchor_y;
    if (!found->label.empty()) std::strncpy(editor.label, found->label.data(), sizeof(editor.label) - 1);
    return true;
}

void stage_section_label_inline(wormedit::ui::workspace_action_t& action, const wormedit::ui::compose_view_t& view,
                                const inline_label_editor_t& editor) {
    action.compose_action = WORM_UI_COMPOSE_ACTION_UPDATE_BEAT_SECTION_LABEL;
    action.compose_section_id.value = editor.bookmark_id;
    action.compose_input_project_id.value = view.project_id.value;
    action.compose_input_chart_id.value = editor.chart_id;
    action.compose_input_document_revision = editor.document_revision;
    action.compose_marker_label = editor.label;

}
