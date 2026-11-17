#pragma once

#include "ui/frontend/timing_edit.h"

struct nk_context;

// Whether a timing view crossing the frontend boundary is well-formed. Compose validates the same
// view because its lanes are projected from it.
[[nodiscard]] int worm_ui_nuklear_timing_view_valid(const wormedit::ui::timing_view_t* view);

// The staged timing event editor (value, transition, pause unit, kiai and anchor fields, confirm,
// cancel and delete) that the Compose inspector shows for a pending lane event.
void worm_ui_nuklear_render_timing_stage_editor(struct nk_context* context,
                                                const wormedit::ui::timing_view_t* view,
                                                wormedit::ui::timing_edit_request_t* action);

// The audio-time anchoring checkboxes (and rounding counts while audio time is kept) for BPM,
// stop, delay and warp edits. The height is that of the rows rendered, including the spacing
// below each one, so a popup can fit them exactly.
void worm_ui_nuklear_render_timing_anchoring(nk_context*, const wormedit::ui::timing_view_t&,
    wormedit::ui::timing_edit_request_t&);
[[nodiscard]] auto worm_ui_nuklear_timing_anchoring_height(const nk_context*,
    const wormedit::ui::timing_view_t&) -> float;
