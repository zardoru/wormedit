#pragma once

#include "ui/frontend.h"

struct nk_context;

// Whether a timing view crossing the frontend boundary is well-formed. Compose validates the same
// view because its lanes are projected from it.
[[nodiscard]] int worm_ui_nuklear_timing_view_valid(const WormUiTimingView* view);

// The staged timing event editor (value, transition, pause unit, kiai and anchor fields, confirm,
// cancel and delete) that the Compose inspector shows for a pending lane event.
void worm_ui_nuklear_render_timing_stage_editor(struct nk_context* context,
                                                const WormUiTimingView* view,
                                                WormUiWorkspaceAction* action);
