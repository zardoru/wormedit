#pragma once
#include "ui/frontend/sound_import.h"

#include "sys/compose_action_outcome.h"
#include "sys/resources/sound_drop_transaction.h"
#include <span>
#include <string>

namespace wormedit::sys {

auto stage_sound_import(const song_document_t& document, const workspace_controller_t& workspace,
                        std::span<const std::string> paths, wormedit::ui::sound_import_kind_t fixed_kind = wormedit::ui::sound_import_kind_t::generic)
    -> compose_action_outcome_t;
auto stage_sound_slot_batch(const song_document_t& document, chart_id_t chart,
                            std::uint16_t first_slot, std::span<const std::string> paths)
    -> compose_action_outcome_t;
auto stage_dropped_sound_import(const song_document_t& document, const sound_drop_batch_t& batch)
    -> compose_action_outcome_t;

} // namespace wormedit::sys
