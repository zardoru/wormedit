#pragma once

#include "ui/frontend/storyboard.h"
#include <wormedit/core/storyboard_vocabulary.h>

namespace wormedit { struct osbx_document_t; }
namespace wormedit::sys {
class storyboard_scene_presenter_t;
void populate_storyboard_canvas(ui::storyboard_canvas_view_t&, storyboard_scene_presenter_t&,
    storyboard_id_t, const result_t<storyboard_time_t, failure_code_t>&,
    storyboard_conditional_layer_t = storyboard_conditional_layer_t::pass);
void filter_storyboard_canvas_hidden(ui::storyboard_view_t&, const osbx_document_t*);
void refill_storyboard_canvas_outlines(ui::storyboard_view_t&, const osbx_document_t*);
} // namespace wormedit::sys
