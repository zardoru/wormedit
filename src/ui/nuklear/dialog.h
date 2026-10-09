#pragma once

#include "ui/nuklear/chrome.h"
#include "wormedit/nuklear_config.h"

#include <optional>

namespace wormedit::ui::chrome {

// One shared dialog surface (see docs/chart-editor-plan/tasks/dialogs.md for the rules): an
// elevation-2 painted surface with 16 px padding and 8 px row spacing, no Nuklear title bar
// and no dragging, a title row, a body, and a right-aligned footer of 28 px buttons. The
// height fits the content as measured on the previous frame, and the dialog is centered in
// `area`. Modal dialogs dim and block everything behind them.
//
// The caller keeps one Dialog per dialog and draws it every frame while it is open:
//
//   if (dialog_begin(dialog, context, "Unsaved changes", "Unsaved changes", {.area = size})) {
//       nk_layout_row_dynamic(context, 15, 1);  // body rows
//       ...
//       dialog_footer_begin(dialog, context, 3, true);
//       if (dialog_button(dialog, context, "Discard", DialogButtonRole::destructive)) ...
//   }
//   switch (dialog_end(dialog, context)) { ... }   // always, even when begin returned false

enum class DialogButtonRole { primary, secondary, destructive };

enum class DialogResult {
    none,
    primary,     // the primary button, or Enter
    secondary,
    destructive,
    cancel,      // Escape or the close icon
};

struct DialogOptions {
    bool modal{true};
    bool closable{false};  // shows the close icon (non-modal tools); confirmations use Escape
    float width{420.0f};
    struct nk_vec2 area{}; // The window the dialog is centered in; empty uses the app viewport.
    // Escape cancels. Modal dialogs default to true; a non-modal tool leaves Escape to the
    // workspace unless it opts in.
    std::optional<bool> escape_cancels{};
    // The dialog ignores pointer and key input this frame (a closing click still being released).
    bool no_input{};
    // Enter triggers the primary button. Same default; a tool with text fields leaves it off.
    std::optional<bool> enter_confirms{};
};

struct Dialog {
    // Measured on the previous frame; zero until the first frame has been drawn.
    float window_height{};
    bool has_primary{};         // A primary button was drawn last frame, so Enter has a target.
    DialogResult result{DialogResult::none};
    // The bounds the dialog was given this frame, for tests and callers that anchor popups.
    struct nk_rect bounds{};
    // Per-frame state restored by dialog_end().
    SurfaceWindow surface{};
    struct nk_vec2 saved_padding{}, saved_spacing{};
    float saved_border{};
    nk_flags saved_window_flags{};
    bool primary_drawn{};
    bool enter_pending{};  // Enter was pressed this frame and the primary button should act.
    bool open{};
    bool modal{};
    bool closable{};
    bool escape_cancels{};
    bool enter_confirms{};
};

inline constexpr float dialog_padding = metrics::gap_large;
inline constexpr float dialog_spacing = metrics::gap_medium;
inline constexpr float dialog_title_height = 15.0f;
inline constexpr float dialog_button_height = metrics::control_default;
inline constexpr float dialog_button_width = 96.0f;

// Starts the dialog window. Returns false when it is hidden; dialog_end() must still follow.
auto dialog_begin(Dialog& dialog, nk_context* context, const char* id, const char* title,
                  const DialogOptions& options = {}) -> bool;

// A paragraph in the body: wrapped to the dialog's content width, one tight row per line, so
// the measured height needs no hand-picked row height. Draw it before the footer.
void dialog_text(Dialog& dialog, nk_context* context, const char* text, struct nk_color tint);

// Starts the footer row: `buttons` equal-width buttons, right-aligned, 28 px tall. With
// `destructive_first`, the first button sits at the far left, apart from the others.
// `button_width` is the width of every button; widen it for long labels.
void dialog_footer_begin(Dialog& dialog, nk_context* context, int buttons,
                         bool destructive_first = false,
                         float button_width = dialog_button_width);

// One footer button. Records its role as the result when clicked; the enabled primary button
// also reports Enter (when the dialog allows it) as a click.
auto dialog_button(Dialog& dialog, nk_context* context, const char* label,
                   DialogButtonRole role, bool enabled = true) -> bool;

// Ends the window, restores the style, and returns the result of this frame: a clicked
// button, or Escape/close icon for cancel. Enter acts through the primary button, so a
// disabled primary button ignores it.
auto dialog_end(Dialog& dialog, nk_context* context) -> DialogResult;

} // namespace wormedit::ui::chrome
