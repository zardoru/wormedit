#pragma once

#include "ui/nuklear/compose_bookmark_rename.h"

// Opens the inline note-label editor over the double-clicked note, prefilled with the label of the
// single selected note. Anchor coordinates are window pixels. Returns false when exactly one note
// with a known label owner is not selected or another inline editor is still releasing input.
auto begin_compose_note_label_inline(bookmark_rename_editor_t& editor,
                                     const WormUiComposeView& view, float anchor_x,
                                     float anchor_y) -> bool;

// Popup rectangle in the window content region, just above the anchor and kept inside the region.
[[nodiscard]] auto note_label_inline_bounds(const bookmark_rename_editor_t& editor,
                                            struct nk_rect content) -> struct nk_rect;

// Stages the commit: APPLY_NOTE_LABEL for text, CLEAR_NOTE_LABEL for an empty field. The captured
// chart and document revision travel with it so a stale editor applies nothing.
void stage_note_label_inline(WormUiWorkspaceAction& action, const WormUiComposeView& view,
                             const bookmark_rename_editor_t& editor);
