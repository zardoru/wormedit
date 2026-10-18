#pragma once
#include <string>
#include <wormedit/core/command.h>
#include <wormedit/core/result.h>

namespace wormedit {
// Changes only the working document; the caller owns publication and the returned Undo label.
auto stage_delete_bmson_bgm_marker(document_command_transaction_t& transaction, chart_id_t chart,
                                   object_id_t bgm_event) -> attempt_t<document_edit_effect_t>;
auto stage_assign_bmson_sound_event(document_command_transaction_t& transaction, chart_id_t chart,
                                    object_id_t semantic_event,
                                    std::optional<lane_id_t> playable_lane)
    -> attempt_t<document_edit_effect_t>;
} // namespace wormedit
