#include "ui/nuklear/preferences/shortcuts_panel.h"
#include "ui/nuklear/chrome/chrome.h"
#include "core/text_util.h"
#include "wormedit/nuklear_config.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <string>

namespace wormedit::ui::nuklear {
namespace {
struct shortcut_filter_t {
    std::array<char, 129> query{};
    auto matches(const wormedit::ui::shortcut_row_view_t& row) const -> bool {
        return wormedit::text_util::ascii_icontains(row.label, query.data()) ||
               wormedit::text_util::ascii_icontains(row.chord, query.data()) ||
               wormedit::text_util::ascii_icontains(row.command_id, query.data());
    }
} filter;

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
    nk_layout_row_dynamic(context, 20, 1);
    nk_label(context, "Find shortcut by command or chord", NK_TEXT_LEFT);
    nk_layout_row_dynamic(context, 28, 1);
    (void)wormedit::ui::chrome::worm_ui_nuklear_text_field(context, NK_EDIT_FIELD,
        filter.query.data(), static_cast<int>(filter.query.size()), nk_filter_default);
    bool has_matches = false;
    for (size_t index = 0; index < view->shortcut_rows.size(); ++index) {
        const auto& row = view->shortcut_rows[index];
        const auto capturing = view->shortcut_capturing && view->shortcut_capture_command_id == row.command_id;
        if (!capturing && !filter.matches(row)) continue;
        has_matches = true;
        render_row(context, *view, *action, index);
    }
    if (!has_matches) {
        nk_layout_row_dynamic(context, 28, 1);
        nk_label(context, "No matching shortcuts", NK_TEXT_LEFT);
    }
}
void reset_shortcuts_filter() { filter = {}; }

} // namespace wormedit::ui::nuklear
