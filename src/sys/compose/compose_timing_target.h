#pragma once

#include "sys/compose/compose_scroll_geometry.h"
#include "ui/frontend.h"

namespace wormedit::sys {

// The one conversion from a raw timing pointer press to a document fraction in source order:
// downscroll mirroring first, then the visible slice of the document.
inline auto timing_target_fraction(const wormedit::ui::timing_target_t& target) -> double {
    const auto viewport_fraction = compose_source_fraction(
        target.screen_y, target.viewport_height_pixels, target.downscroll);
    return target.document_begin_fraction +
           viewport_fraction * (target.document_end_fraction - target.document_begin_fraction);
}

} // namespace wormedit::sys
