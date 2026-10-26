#pragma once

#include "sys/compose/compose_scroll_geometry.h"
#include "ui/frontend.h"

namespace wormedit::sys {

// Prefer the source coordinate captured from the drawn axis. Surfaces without
// chart-axis geometry map their raw pointer through the visible document slice.
inline auto timing_target_fraction(const wormedit::ui::timing_target_t& target) -> double {
    if (target.source_document_fraction)
        return *target.source_document_fraction;
    const auto viewport_fraction = compose_source_fraction(
        target.screen_y, target.viewport_height_pixels, target.downscroll);
    return target.document_begin_fraction +
           viewport_fraction * (target.document_end_fraction - target.document_begin_fraction);
}

} // namespace wormedit::sys
