#pragma once

#include "ui/frontend.h"

// Every loss of Compose input ownership uses the same capture inventory.
void cancel_compose_gestures(WormUiWorkspaceAction* action);
[[nodiscard]] auto has_compose_gesture_capture() -> bool;
