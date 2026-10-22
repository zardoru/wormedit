#pragma once
#include "ui/frontend.h"
#include "sys/commands/shortcut_rows.h"
#include <wormedit/core/command_registry.h>

namespace wormedit::sys {
void build_menu_commands_view(const command_registry_t& registry, wormedit::ui::workspace_view_t& view,
                           command_shortcut_text_t& shortcuts,
#if defined(__APPLE__)
                           shortcut_primary_modifier_t primary = shortcut_primary_modifier_t::meta);
#else
                           shortcut_primary_modifier_t primary = shortcut_primary_modifier_t::control);
#endif
}
