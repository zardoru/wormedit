#include "sys/menu_command_view.h"
#include "ui/workspace_menu.h"

namespace wormedit::sys {
void build_menu_commands_view(const command_registry_t& registry, ui::workspace_view_t& view,
                              command_shortcut_text_t& shortcuts,
                              shortcut_primary_modifier_t primary) {
    shortcuts.refresh(registry, primary);
    view.menu_commands.resize(ui::workspace_menu_command_ids.size());
    std::size_t count{};
    for (const auto* id : ui::workspace_menu_command_ids) {
        const auto* command = find_command(registry, {id});
        if (!command) continue;
        auto& row = view.menu_commands[count++];
        row.id = id;
        row.label = command->definition.display_name;
        const auto command_index = static_cast<std::size_t>(command - registry.commands.data());
        row.shortcut = shortcuts.first_text(command_index);
        row.enabled = command->definition.enabled;
        row.checked = command->definition.checked.value_or(false);
    }
    view.menu_commands.resize(count);
}
}
