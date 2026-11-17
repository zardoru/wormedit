#include "ui/nuklear/compose/compose_bookmark_rename.h"
#include "ui/nuklear/compose/compose_note_label_inline.h"

#include "ui/nuklear/compose/compose_bookmark_label.h"
#include "ui/nuklear/chrome/chrome.h"
#include "ui/nuklear/chrome/popup_lifecycle.h"
#include "ui/nuklear/chrome/text_prompt.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

// Every label editor opens and closes the same popup, so close_pending_popup can find it.
constexpr const char* label_popup_name = "Label editor";

struct bookmark_candidate_t {
    std::uint64_t id{};
    std::string_view label{};
    double y{};
};

auto bookmark_by_id(const wormedit::ui::compose_view_t& view, std::uint64_t id)
    -> bookmark_candidate_t {
    for (std::size_t index = 0; index < view.beat_bookmarks.size(); ++index) {
        const auto& item = view.beat_bookmarks[index];
        if (item.bookmark_id.value == id)
            return {id, item.label, item.projected_y_pixels};
    }
    for (std::size_t index = 0; index < view.audio_bookmarks.size(); ++index) {
        const auto& item = view.audio_bookmarks[index];
        if (item.bookmark_id.value == id)
            return {id, item.label, item.projected_y_pixels};
    }
    return {};
}

auto visible_bookmark_lines(const wormedit::ui::compose_view_t& view)
    -> std::vector<wormedit::ui::bookmark_rename_line_t> {
    std::vector<wormedit::ui::bookmark_rename_line_t> lines;
    lines.reserve(view.beat_bookmarks.size() + view.audio_bookmarks.size());
    for (std::size_t index = 0; index < view.beat_bookmarks.size(); ++index) {
        const auto& item = view.beat_bookmarks[index];
        if (item.projection_visible)
            lines.push_back({item.bookmark_id.value, item.projected_y_pixels,
                             !item.label.empty()});
    }
    for (std::size_t index = 0; index < view.audio_bookmarks.size(); ++index) {
        const auto& item = view.audio_bookmarks[index];
        if (item.projection_visible)
            lines.push_back({item.bookmark_id.value, item.projected_y_pixels,
                             !item.label.empty()});
    }
    return lines;
}

auto editor_popup_bounds(nk_context* context, const wormedit::ui::compose_view_t& view,
                         const inline_label_editor_t& editor, struct nk_rect viewport)
    -> struct nk_rect {
    const auto content = nk_window_get_content_region(context);
    if (editor.kind != inline_label_kind_t::bookmark || editor.anchored)
        return note_label_inline_bounds(editor, content);
    const auto target = bookmark_by_id(view, editor.bookmark_id);
    const auto field_right = view.viewport_width_pixels > 0.0
        ? viewport.x + static_cast<float>(view.playable_field_right_pixels) *
              viewport.w / static_cast<float>(view.viewport_width_pixels)
        : viewport.x + viewport.w;
    const auto width = wormedit::ui::chrome::text_prompt_width_for(content);
    const auto height = wormedit::ui::chrome::text_prompt_height;
    const auto x = std::clamp(field_right - width - content.x, 0.0f, std::max(0.0f, content.w - width));
    const auto scale = view.viewport_height_pixels > 0.0
        ? viewport.h / static_cast<float>(view.viewport_height_pixels) : 1.0f;
    const auto line_y = viewport.y + static_cast<float>(target.y) * scale - height;
    const auto y = std::clamp(line_y - content.y, 0.0f, std::max(0.0f, content.h - height));
    return nk_rect(x, y, width, height);
}

void request_editor_close(inline_label_editor_t& editor) {
    editor.active = false;
    editor.focus_pending = false;
    wormedit::ui::chrome::popup_arm_release(editor.release_pending);
    editor.close_pending = editor.popup_open;
}

void clear_editor_after_close(inline_label_editor_t& editor) {
    const auto release_pending = editor.release_pending;
    editor = {};
    editor.release_pending = release_pending;
}

void close_pending_popup(nk_context* context, inline_label_editor_t& editor) {
    if (!editor.close_pending) {
        clear_editor_after_close(editor);
        return;
    }
    const auto bounds = nk_rect(editor.popup_bounds[0], editor.popup_bounds[1],
                                editor.popup_bounds[2], editor.popup_bounds[3]);
    wormedit::ui::chrome::dismiss_popup(context, label_popup_name, bounds);
    clear_editor_after_close(editor);
}

struct prompt_text_t {
    const char* title;
    const char* placeholder;
    const char* hint;
};

auto prompt_text(inline_label_kind_t kind) -> prompt_text_t {
    switch (kind) {
    case inline_label_kind_t::note_label:
        return {"Note label", "Label…", "Enter to apply · Esc to cancel · empty clears"};
    case inline_label_kind_t::section_label:
        return {"Section label", "Section name…", "Enter to apply · Esc to cancel · empty clears"};
    case inline_label_kind_t::bookmark:
        break;
    }
    return {"Rename bookmark", "Bookmark name…", "Enter to apply · Esc to cancel"};
}

void stage_rename(wormedit::ui::compose_ui_window_t& action, const wormedit::ui::compose_view_t& view,
                  const inline_label_editor_t& editor) {
    if (action.compose.kind != WORM_UI_COMPOSE_ACTION_NONE) return;
    if (editor.kind == inline_label_kind_t::note_label) {
        stage_note_label_inline(action, view, editor);
        return;
    }
    if (editor.kind == inline_label_kind_t::section_label) {
        stage_section_label_inline(action, view, editor);
        return;
    }
    action.compose.kind = WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK;
    action.compose.marker.bookmark_id.value = editor.bookmark_id;
    action.stamp.project_id.value = view.project_id.value;
    action.stamp.chart_id.value = editor.chart_id;
    action.stamp.document_revision = editor.document_revision;
    action.compose.marker.label = editor.label;

}

} // namespace

auto begin_compose_bookmark_rename(inline_label_editor_t& editor,
                                   const wormedit::ui::compose_view_t& view,
                                   std::uint64_t bookmark_id) -> bool {
    if (editor.release_pending) return false;
    const auto candidate = bookmark_by_id(view, bookmark_id);
    if (candidate.id == 0) return false;
    editor = {};
    editor.active = true;
    editor.focus_pending = true;
    editor.project_id = view.project_id.value;
    editor.chart_id = view.chart_id.value;
    editor.document_revision = view.document_revision;
    editor.bookmark_id = bookmark_id;
    std::strncpy(editor.label, candidate.label.data(), sizeof(editor.label) - 1);
    return true;
}

void cancel_compose_bookmark_rename(inline_label_editor_t& editor) {
    if (editor.active || editor.popup_open || editor.close_pending)
        request_editor_close(editor);
}

auto compose_bookmark_at(const wormedit::ui::compose_view_t& view, double pointer_x, double pointer_y)
    -> std::optional<std::uint64_t> {
    return wormedit::ui::bookmark_double_click_target_at(
        visible_bookmark_lines(view), pointer_x, pointer_y, view.viewport_width_pixels,
        wormedit::ui::compose_bookmark_label_zone_height);
}

auto begin_compose_bookmark_rename_at_double_click(inline_label_editor_t& editor,
                                                  const wormedit::ui::compose_view_t& view,
                                                  double pointer_x, double pointer_y) -> bool {
    const auto id = compose_bookmark_at(view, pointer_x, pointer_y);
    return id && begin_compose_bookmark_rename(editor, view, *id);
}

auto compose_bookmark_rename_owns_input(const inline_label_editor_t& editor,
                                        const wormedit::ui::compose_view_t& view,
                                        bool window_focused) -> bool {
    return editor.active && window_focused && view.visible && view.scene_available &&
           !view.input_occluded && view.project_id.value == editor.project_id &&
           view.chart_id.value == editor.chart_id &&
           view.document_revision == editor.document_revision;
}

void render_compose_bookmark_rename(nk_context* context, const wormedit::ui::compose_view_t& view,
                                    wormedit::ui::compose_ui_window_t& action,
                                    inline_label_editor_t& editor,
                                    struct nk_rect viewport, bool window_focused) {
    (void)wormedit::ui::chrome::popup_update_release_pending(
        context, editor.release_pending, wormedit::ui::chrome::PopupReleaseKeys::text);
    if (editor.close_pending) {
        close_pending_popup(context, editor);
        return;
    }
    if (!editor.active) {
        return;
    }
    if (!compose_bookmark_rename_owns_input(editor, view, window_focused)) {
        request_editor_close(editor);
        close_pending_popup(context, editor);
        return;
    }
    if (editor.focus_pending && !wormedit::ui::chrome::popup_input_is_quiet(
            context, wormedit::ui::chrome::PopupReleaseKeys::text)) return;
    const auto bounds = editor_popup_bounds(context, view, editor, viewport);
    wormedit::ui::chrome::MenuSurface surface{};
    if (!wormedit::ui::chrome::text_prompt_begin(context, label_popup_name, bounds, surface)) {
        request_editor_close(editor);
        close_pending_popup(context, editor);
        return;
    }
    editor.popup_open = true;
    editor.popup_bounds[0] = bounds.x;
    editor.popup_bounds[1] = bounds.y;
    editor.popup_bounds[2] = bounds.w;
    editor.popup_bounds[3] = bounds.h;
    const auto text = prompt_text(editor.kind);
    const auto flags = wormedit::ui::chrome::text_prompt_body(
        context,
        {.title = text.title, .placeholder = text.placeholder, .hint = text.hint,
         .buffer = editor.label, .capacity = static_cast<int>(sizeof(editor.label))},
        editor.focus_pending);
    const bool cancelled = nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE) ||
                           ((flags & NK_EDIT_COMMITTED) == 0 && (flags & NK_EDIT_DEACTIVATED) != 0);
    const bool committed = !cancelled && (flags & NK_EDIT_COMMITTED) != 0;
    if (committed) stage_rename(action, view, editor);
    const bool finished = cancelled || committed;
    if (finished) nk_popup_close(context);
    wormedit::ui::chrome::text_prompt_end(context, surface);
    if (finished) {
        request_editor_close(editor);
        clear_editor_after_close(editor);
    }
}
