#pragma once
#include "ui/frontend.h"

struct nk_context;
struct nk_rect;

// A right-click on an SV recipe area opens a small menu with its commands (enable or disable,
// materialize into ordinary events, delete). Choosing one queues a RECIPE_COMMAND compose input;
// the menu closes when the chart, the revision or the window focus changes.
void process_compose_recipe_menu(nk_context*, const WormUiComposeView&, wormedit::ui::workspace_action_t&,
                                 struct nk_rect viewport, double input_right, bool focused);
auto compose_recipe_menu_open() -> bool;
auto compose_recipe_menu_consumed() -> bool;
void reset_compose_recipe_menu();
