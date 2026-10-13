#include "sys/menu_command_view.h"
#include "sys/shortcut_rows.h"
#include "ui/workspace_menu.h"
#include <cstdio>

namespace wormedit::sys {
namespace {
auto menu_shortcut(shortcut_chord_t chord, shortcut_primary_modifier_t primary) -> std::string {
    const auto bit = static_cast<shortcut_modifiers_t>(shortcut_modifier_t::primary);
    if (chord.modifiers & bit) {
        chord.modifiers &= ~bit;
        chord.modifiers |= static_cast<shortcut_modifiers_t>(primary == shortcut_primary_modifier_t::meta
            ? shortcut_modifier_t::meta : shortcut_modifier_t::control);
    }
    auto text = shortcut_chord_text(chord);
    if (primary == shortcut_primary_modifier_t::meta) {
        const auto at = text.find("Meta+");
        if (at != std::string::npos) text.replace(at, 5, "⌘+");
    }
    return text;
}
}

void build_menu_commands_view(const command_registry_t& registry, WormUiWorkspaceView& view, shortcut_primary_modifier_t primary) {
    static_assert(ui::workspace_menu_command_ids.size() <= WORM_UI_MENU_COMMAND_CAPACITY);
    view.menu_command_count = 0;
    for (const auto* id : ui::workspace_menu_command_ids) {
        const auto* command = find_command(registry, {id});
        if (!command) continue;
        auto& row = view.menu_commands[view.menu_command_count++];
        row = {};
        std::snprintf(row.id, sizeof(row.id), "%s", id);
        std::snprintf(row.label, sizeof(row.label), "%s", command->definition.display_name.c_str());
        const auto shortcut = command->current_shortcuts.empty() ? std::string{}
            : menu_shortcut(command->current_shortcuts.front(), primary);
        std::snprintf(row.shortcut, sizeof(row.shortcut), "%s", shortcut.c_str());
        row.enabled = command->definition.enabled;
        row.checked = command->definition.checked.value_or(false);
    }
}
}
