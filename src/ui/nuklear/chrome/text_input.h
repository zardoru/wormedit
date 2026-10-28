#pragma once

#include "wormedit/nuklear_config.h"

namespace wormedit::ui::chrome {

inline auto has_active_text_edit(const nk_context* context) -> bool {
    if (!context || !context->active) return false;
    const auto* window = context->active;
    // Closed popups and tooltips retain a window until Nuklear's next clear.
    // Only a live popup can take text input away from its parent.
    if (window->popup.active && window->popup.win)
        return window->popup.win->edit.active != 0;
    return window->edit.active != 0;
}

} // namespace wormedit::ui::chrome
