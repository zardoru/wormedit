#pragma once

#include "ui/nuklear/compose/compose_ui_window.h"
#include "ui/frontend/common.h"
#include "ui/frontend/compose.h"
#include "ui/frontend/project.h"
#include "ui/frontend/timing_preview.h"

// Every loss of Compose input ownership uses the same capture inventory.
void cancel_compose_gestures(wormedit::ui::compose_ui_window_t* action);
[[nodiscard]] auto has_compose_gesture_capture() -> bool;
