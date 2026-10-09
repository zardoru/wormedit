#include "ui/nuklear/shortcuts_panel.h"
#include "wormedit/nuklear_config.h"

#include <cstddef>
#include <cstdint>
#include <string>

namespace wormedit::ui::nuklear {
namespace {

void render_row_chord(nk_context* context, const WormUiShortcutRowView& row, bool capturing) {
    std::string text = capturing ? "Press a chord..." : row.chord;
    if (!capturing && row.conflict) text += "  (conflict)";
    nk_label(context, text.c_str(), NK_TEXT_LEFT);
}

void emit_row_action(WormUiOptionsAction& action, const WormUiOptionsView& view,
                     WormUiOptionsActionKind kind, size_t index) {
    if (action.kind != WORM_UI_OPTIONS_NONE) return;
    action = {.kind = kind, .revision = view.revision, .category = view.category,
              .shortcut_row = static_cast<uint32_t>(index)};
}

void render_row(nk_context* context, const WormUiOptionsView& view, WormUiOptionsAction& action,
                size_t index) {
    const auto& row = view.shortcut_rows[index];
    const bool capturing = view.shortcut_capturing && view.shortcut_capture_row == index;
    constexpr float ratios[]{0.42f, 0.38f, 0.20f};
    nk_layout_row(context, NK_DYNAMIC, 28, 3, ratios);
    nk_label(context, row.label, NK_TEXT_LEFT);
    render_row_chord(context, row, capturing);
    if (capturing) {
        if (nk_button_label(context, "Cancel")) emit_row_action(action, view, WORM_UI_OPTIONS_SHORTCUT_CANCEL, index);
    } else if (nk_button_label(context, "Change")) {
        emit_row_action(action, view, WORM_UI_OPTIONS_SHORTCUT_CAPTURE, index);
    }
}

} // namespace

void render_shortcuts_page(nk_context* context, const WormUiOptionsView* view,
                           WormUiOptionsAction* action) {
    nk_layout_row_dynamic(context, 48, 1);
    nk_label_wrap(context, "Change captures the next key press as the command's chord and saves it at once. "
        "Conflicting chords are marked but kept. Escape cancels a capture.");
    if (view->shortcut_row_count == 0) {
        nk_layout_row_dynamic(context, 28, 1);
        nk_label(context, "No commands are registered.", NK_TEXT_LEFT);
        return;
    }
    for (size_t index = 0; index < view->shortcut_row_count; ++index)
        render_row(context, *view, *action, index);
}

} // namespace wormedit::ui::nuklear
