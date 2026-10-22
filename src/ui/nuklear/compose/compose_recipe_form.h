#pragma once

#include "ui/nuklear/compose/compose_inspector.h"

// The selected SV recipe's editor in the Compose inspector: output, curve, exact values, sampling
// and (for SPEEDS) transition, with the candidate previewed in the lane as it is edited, Apply as
// one undo step and Revert. Draws nothing when no recipe is selected.
void render_compose_recipe_form(const ComposeInspectorContext& inspector);
