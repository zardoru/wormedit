#pragma once

#include "ui/nuklear/compose_panel_internal.h"

void queue_compose_key_input(std::uint32_t keycode, std::uint32_t scancode,
                             std::uint16_t modifiers, bool down, bool repeat);
void process_compose_key_events(struct nk_context* context, const WormUiComposeView* view,
                                WormUiWorkspaceAction* action, bool window_focused,
                                bool ime_composition_active);
