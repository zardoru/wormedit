#pragma once

#include <wormedit/core/osbx_document.h>
#include <wormedit/core/storyboard.h>

namespace wormedit {

struct osbx_edit_ids_t;
using osb_to_osbx_diagnostic_t = format_diagnostic_t<failure_code_t, osbx_diagnostic_location_t>;

// File-free, all-or-nothing conversion. IDs use the same history reservations
// as authored OSBX edits. Loops, retained directives (including P parameters),
// animations, interrupted varying curves and discontinuous shared endpoints
// refuse explicitly; comments, [Events] and blank formatting have no semantics.
// Inclusive sprite lifetimes match the shared integer-plus-fractional clock.
[[nodiscard]] auto convert_osb_to_osbx(const storyboard_document_model_t&, const osbx_edit_ids_t&)
    -> attempt_t<osbx_document_t, std::vector<osb_to_osbx_diagnostic_t>>;

} // namespace wormedit
