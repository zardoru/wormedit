#pragma once

#include "ui/nuklear/compose/compose_ui_window.h"
#include "ui/frontend.h"

// Every loss of Compose input ownership uses the same capture inventory.
void cancel_compose_gestures(wormedit::ui::compose_ui_window_t* action);
[[nodiscard]] auto has_compose_gesture_capture() -> bool;
