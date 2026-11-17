#pragma once
#include "ui/nuklear/compose/compose_ui_window.h"
#include "ui/frontend/common.h"
#include "ui/frontend/compose.h"
#include "ui/frontend/project.h"
#include "ui/frontend/timing_preview.h"
struct nk_context;
void render_compose_snap_badge(nk_context* context, const wormedit::ui::compose_view_t& view,
                               wormedit::ui::compose_request_t& compose, wormedit::ui::request_stamp_t& stamp, bool available);
