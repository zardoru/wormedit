#pragma once

#include "ui/nuklear/compose/compose_inspector.h"

// The selected SV recipe's editor in the Compose inspector: output, curve with its whole-span
// shape, values as draggable exact numbers, points, SPEEDS transition, adjustments and impulses.
// Edits preview in the lane as they happen and apply as one undo step when they settle (Enter,
// leaving a field, releasing a drag, a pick). Order controls appear only when recipes overlap.
void render_compose_recipe_form(const ComposeInspectorContext& inspector);
