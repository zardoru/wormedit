#include "ui/nuklear/preferences/shortcuts_panel.h"
#include "wormedit/nuklear_config.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace wormedit::ui::nuklear {
namespace {

void render_row_chord(nk_context* context, const wormedit::ui::shortcut_row_view_t& row, bool capturing) {
    std::string text{capturing ? std::string_view{"Press a chord..."} : row.chord};
    if (!capturing && row.conflict) text += "  (conflict)";
    nk_label(context, text.c_str(), NK_TEXT_LEFT);
}

void emit_row_action(wormedit::ui::options_action_t& action, const wormedit::ui::options_view_t& view,
                     WormUiOptionsActionKind kind, size_t index) {
    if (action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = kind, .revision = view.revision, .category = view.category,
              .shortcut_command_id = std::string{view.shortcut_rows[index].command_id}};
}

void render_row(nk_context* context, const wormedit::ui::options_view_t& view, wormedit::ui::options_action_t& action,
                size_t index) {
    const auto& row = view.shortcut_rows[index];
    const bool capturing = view.shortcut_capturing && view.shortcut_capture_command_id == row.command_id;
    constexpr float ratios[]{0.42f, 0.38f, 0.20f};
    nk_layout_row(context, NK_DYNAMIC, 28, 3, ratios);
    nk_label(context, row.label.data(), NK_TEXT_LEFT);
    render_row_chord(context, row, capturing);
    if (capturing) {
        if (nk_button_label(context, "Cancel")) emit_row_action(action, view, WORM_UI_OPTIONS_SHORTCUT_CANCEL, index);
    } else if (nk_button_label(context, "Change")) {
        emit_row_action(action, view, WORM_UI_OPTIONS_SHORTCUT_CAPTURE, index);
    }
}

} // namespace

void render_shortcuts_page(nk_context* context, const wormedit::ui::options_view_t* view,
                           wormedit::ui::options_action_t* action) {
    nk_layout_row_dynamic(context, 48, 1);
    nk_label_wrap(context, "Change captures the next key press as the command's chord and saves it at once. "
        "Conflicting chords are marked but kept. Escape cancels a capture.");
    if (view->shortcut_rows.size() == 0) {
        nk_layout_row_dynamic(context, 28, 1);
        nk_label(context, "No commands are registered.", NK_TEXT_LEFT);
        return;
    }
    for (size_t index = 0; index < view->shortcut_rows.size(); ++index)
        render_row(context, *view, *action, index);
}

} // namespace wormedit::ui::nuklear
