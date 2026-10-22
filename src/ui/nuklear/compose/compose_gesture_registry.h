#pragma once

#include "ui/frontend.h"

// Every loss of Compose input ownership uses the same capture inventory.
void cancel_compose_gestures(wormedit::ui::workspace_action_t* action);
[[nodiscard]] auto has_compose_gesture_capture() -> bool;
