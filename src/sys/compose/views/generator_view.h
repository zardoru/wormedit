#pragma once
#include "sys/compose/views/compose_view.h"
#include "sys/project/workspace_view.h"
namespace wormedit::sys {
void populate_compose_generator(frontend_workspace_view_t&, const document_history_t&,
    const chart_document_t&, const editor_interaction_view_model_t&,
    const application_command_state_t&, ui::compose_view_t&, const editor_field_scene_request_t&);
}
