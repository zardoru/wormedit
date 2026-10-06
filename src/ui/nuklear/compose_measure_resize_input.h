#pragma once

#include "ui/nuklear/compose_panel_internal.h"

// Alt+drag of a measure's end line. Returns true while the gesture owns the pointer. A commit
// raises WORM_UI_COMPOSE_ACTION_SET_MEASURES_METER for that one measure.
bool process_compose_measure_resize_pointer(const ComposeBeatFitInput& input);
void cancel_compose_measure_resize();
