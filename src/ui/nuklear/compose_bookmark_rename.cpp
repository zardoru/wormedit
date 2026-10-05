#include "ui/nuklear/compose_bookmark_rename.h"

#include "ui/nuklear/compose_bookmark_label.h"
#include "ui/nuklear/chrome.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace {

struct bookmark_candidate_t {
    std::uint64_t id{};
    const char* label{};
    double y{};
};

auto bookmark_by_id(const WormUiComposeView& view, std::uint64_t id)
    -> bookmark_candidate_t {
    for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
        const auto& item = view.beat_bookmarks[index];
        if (item.bookmark_id == id)
            return {id, item.label, item.projected_y_pixels};
    }
    for (std::size_t index = 0; index < view.audio_bookmark_count; ++index) {
        const auto& item = view.audio_bookmarks[index];
        if (item.bookmark_id == id)
            return {id, item.label, item.projected_y_pixels};
    }
    return {};
}

auto visible_bookmark_lines(const WormUiComposeView& view)
    -> std::vector<wormedit::ui::bookmark_rename_line_t> {
    std::vector<wormedit::ui::bookmark_rename_line_t> lines;
    lines.reserve(view.beat_bookmark_count + view.audio_bookmark_count);
    for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
        const auto& item = view.beat_bookmarks[index];
        if (item.projection_visible)
            lines.push_back({item.bookmark_id, item.projected_y_pixels});
    }
    for (std::size_t index = 0; index < view.audio_bookmark_count; ++index) {
        const auto& item = view.audio_bookmarks[index];
        if (item.projection_visible)
            lines.push_back({item.bookmark_id, item.projected_y_pixels});
    }
    return lines;
}

auto nearest_bookmark(const WormUiComposeView& view, double x, double y)
    -> bookmark_candidate_t {
    const auto id = wormedit::ui::bookmark_rename_target_at(
        visible_bookmark_lines(view), x, y, static_cast<double>(view.playable_field_right_pixels));
    return id ? bookmark_by_id(view, *id) : bookmark_candidate_t{};
}

// Roughly one text line; the label box is placed just above its bookmark line.
constexpr double label_zone_height = 18.0;

auto editor_popup_bounds(nk_context* context, const WormUiComposeView& view,
                         const bookmark_rename_editor_t& editor, struct nk_rect viewport)
    -> struct nk_rect {
    const auto target = bookmark_by_id(view, editor.bookmark_id);
    const auto content = nk_window_get_content_region(context);
    const auto field_right = view.viewport_width_pixels > 0.0
        ? viewport.x + static_cast<float>(view.playable_field_right_pixels) *
              viewport.w / static_cast<float>(view.viewport_width_pixels)
        : viewport.x + viewport.w;
    const auto x = std::clamp(field_right - 300.0f - content.x,
                              0.0f, std::max(0.0f, content.w - 300.0f));
    const auto scale = view.viewport_height_pixels > 0.0
        ? viewport.h / static_cast<float>(view.viewport_height_pixels) : 1.0f;
    const auto line_y = viewport.y + static_cast<float>(target.y) * scale - 32.0f;
    const auto y = std::clamp(line_y - content.y, 0.0f,
                              std::max(0.0f, content.h - 58.0f));
    return nk_rect(x, y, std::min(300.0f, content.w), 58.0f);
}

auto rename_input_is_quiet(const nk_input& input) -> bool {
    const auto& left = input.mouse.buttons[NK_BUTTON_LEFT];
    const auto key_down = nk_input_is_key_down(&input, NK_KEY_ENTER) ||
                          nk_input_is_key_down(&input, NK_KEY_TEXT_RESET_MODE);
    const auto key_edge = nk_input_is_key_pressed(&input, NK_KEY_ENTER) ||
                          nk_input_is_key_pressed(&input, NK_KEY_TEXT_RESET_MODE) ||
                          nk_input_is_key_released(&input, NK_KEY_ENTER) ||
                          nk_input_is_key_released(&input, NK_KEY_TEXT_RESET_MODE);
    const auto mouse_edge = nk_input_is_mouse_pressed(&input, NK_BUTTON_LEFT) ||
                            nk_input_is_mouse_released(&input, NK_BUTTON_LEFT);
    return !left.down && !key_down && !key_edge && !mouse_edge;
}

void request_editor_close(bookmark_rename_editor_t& editor) {
    editor.active = false;
    editor.focus_pending = false;
    editor.release_pending = true;
    editor.close_pending = editor.popup_open;
}

void clear_editor_after_close(bookmark_rename_editor_t& editor) {
    const auto release_pending = editor.release_pending;
    editor = {};
    editor.release_pending = release_pending;
}

void close_pending_popup(nk_context* context, bookmark_rename_editor_t& editor) {
    if (!editor.close_pending) {
        clear_editor_after_close(editor);
        return;
    }
    const auto bounds = nk_rect(editor.popup_bounds[0], editor.popup_bounds[1],
                                editor.popup_bounds[2], editor.popup_bounds[3]);
    if (nk_popup_begin(context, NK_POPUP_STATIC, "Rename bookmark", NK_WINDOW_NO_SCROLLBAR,
                       bounds)) {
        nk_popup_close(context);
        nk_popup_end(context);
    }
    clear_editor_after_close(editor);
}

void stage_rename(WormUiWorkspaceAction& action, const WormUiComposeView& view,
                  const bookmark_rename_editor_t& editor) {
    if (action.compose_action != WORM_UI_COMPOSE_ACTION_NONE) return;
    action.compose_action = WORM_UI_COMPOSE_ACTION_RENAME_BOOKMARK;
    action.compose_bookmark_id = editor.bookmark_id;
    action.compose_input_project_id = view.project_id;
    action.compose_input_chart_id = editor.chart_id;
    action.compose_input_document_revision = editor.document_revision;
    std::memcpy(action.compose_marker_label, editor.label, sizeof(action.compose_marker_label));
    action.compose_marker_label[sizeof(action.compose_marker_label) - 1] = '\0';
}

} // namespace

auto begin_compose_bookmark_rename(bookmark_rename_editor_t& editor,
                                   const WormUiComposeView& view,
                                   std::uint64_t bookmark_id) -> bool {
    if (editor.release_pending) return false;
    const auto candidate = bookmark_by_id(view, bookmark_id);
    if (candidate.id == 0 || !candidate.label) return false;
    editor = {};
    editor.active = true;
    editor.focus_pending = true;
    editor.project_id = view.project_id;
    editor.chart_id = view.chart_id;
    editor.document_revision = view.document_revision;
    editor.bookmark_id = bookmark_id;
    std::strncpy(editor.label, candidate.label, sizeof(editor.label) - 1);
    return true;
}

void cancel_compose_bookmark_rename(bookmark_rename_editor_t& editor) {
    if (editor.active || editor.popup_open || editor.close_pending)
        request_editor_close(editor);
}

auto begin_compose_bookmark_rename_at_pointer(bookmark_rename_editor_t& editor,
                                             const WormUiComposeView& view,
                                             double pointer_x, double pointer_y,
                                             bool pressed, bool available) -> bool {
    if (!pressed || !available || !std::isfinite(pointer_x) || !std::isfinite(pointer_y))
        return false;
    const auto candidate = nearest_bookmark(view, pointer_x, pointer_y);
    return candidate.id != 0 && begin_compose_bookmark_rename(editor, view, candidate.id);
}

auto begin_compose_bookmark_rename_at_double_click(bookmark_rename_editor_t& editor,
                                                  const WormUiComposeView& view,
                                                  double pointer_x, double pointer_y) -> bool {
    const auto id = wormedit::ui::bookmark_double_click_target_at(
        visible_bookmark_lines(view), pointer_x, pointer_y,
        static_cast<double>(view.playable_field_right_pixels), label_zone_height);
    return id && begin_compose_bookmark_rename(editor, view, *id);
}

auto compose_bookmark_rename_owns_input(const bookmark_rename_editor_t& editor,
                                        const WormUiComposeView& view,
                                        bool window_focused) -> bool {
    return editor.active && window_focused && view.visible && view.scene_available &&
           !view.input_occluded && view.project_id == editor.project_id &&
           view.chart_id == editor.chart_id &&
           view.document_revision == editor.document_revision;
}

void render_compose_bookmark_rename(nk_context* context, const WormUiComposeView& view,
                                    WormUiWorkspaceAction& action,
                                    bookmark_rename_editor_t& editor,
                                    struct nk_rect viewport, bool window_focused) {
    if (editor.release_pending && rename_input_is_quiet(context->input))
        editor.release_pending = false;
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
    if (editor.focus_pending && !rename_input_is_quiet(context->input)) return;
    const auto bounds = editor_popup_bounds(context, view, editor, viewport);
    if (!nk_popup_begin(context, NK_POPUP_STATIC, "Rename bookmark", NK_WINDOW_NO_SCROLLBAR,
                        bounds)) {
        request_editor_close(editor);
        close_pending_popup(context, editor);
        return;
    }
    editor.popup_open = true;
    editor.popup_bounds[0] = bounds.x;
    editor.popup_bounds[1] = bounds.y;
    editor.popup_bounds[2] = bounds.w;
    editor.popup_bounds[3] = bounds.h;
    nk_layout_row_dynamic(context, 28.0f, 1);
    const auto flags = wormedit::ui::chrome::autofocus_text_field(
        context, static_cast<nk_flags>(NK_EDIT_FIELD) | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT,
        editor.label, static_cast<int>(sizeof(editor.label)), nk_filter_default,
        editor.focus_pending);
    editor.focus_pending = false;
    if (nk_input_is_key_pressed(&context->input, NK_KEY_TEXT_RESET_MODE)) {
        nk_popup_close(context);
        nk_popup_end(context);
        request_editor_close(editor);
        clear_editor_after_close(editor);
        return;
    } else if ((flags & NK_EDIT_COMMITTED) != 0) {
        stage_rename(action, view, editor);
        nk_popup_close(context);
        nk_popup_end(context);
        request_editor_close(editor);
        clear_editor_after_close(editor);
        return;
    } else if ((flags & NK_EDIT_DEACTIVATED) != 0) {
        nk_popup_close(context);
        nk_popup_end(context);
        request_editor_close(editor);
        clear_editor_after_close(editor);
        return;
    }
    nk_popup_end(context);
}
