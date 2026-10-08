#pragma once

#include "ui/workspace_menu.h"
#include <wormedit/core/command_registry.h>

namespace wormedit::sys {

bool initialize_macos_menu();
void shutdown_macos_menu();
bool macos_menu_active();
void refresh_macos_menu_shortcuts(const command_registry_t& registry);
void refresh_macos_menu(const ui::workspace_menu_t& model);
ui::workspace_menu_action_t take_macos_menu_action();

} // namespace wormedit::sys
