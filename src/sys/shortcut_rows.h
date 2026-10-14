#pragma once

#include "ui/frontend.h"
#include <wormedit/core/command_registry.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit::sys {

// Frame-local storage behind WormUiShortcutRowView pointers. Rebuild every frame before the
// view reaches the frontend; the row pointers stay valid until the next rebuild.
struct shortcut_rows_projection_t {
    std::vector<WormUiShortcutRowView> rows;
    std::vector<command_id_t> commands;
    std::vector<std::string> labels;
    std::vector<std::string> chords;
};

// Projects every registry command, bounded by WORM_UI_SHORTCUT_ROW_CAPACITY. A row is marked
// as conflicting when find_shortcut_conflicts pairs its command with another.
void build_shortcut_rows_view(const command_registry_t& registry,
                           shortcut_primary_modifier_t primary, shortcut_rows_projection_t& out);

// Human-readable chord, for example "Ctrl+Shift+Z"; empty for an unbindable key.
[[nodiscard]] auto shortcut_chord_text(const shortcut_chord_t& chord) -> std::string;

// Result of one frame's key batch while a capture is waiting. Escape without modifiers cancels.
struct shortcut_capture_t {
    bool cancel{};
    std::optional<shortcut_chord_t> chord;
};

// The first usable press in the batch: repeats, tap-timing presses, modifier-only events and
// unknown usages are skipped. An empty batch captures nothing.
[[nodiscard]] auto capture_shortcut_batch(std::span<const WormUiShortcutEvent> events)
    -> shortcut_capture_t;

} // namespace wormedit::sys
