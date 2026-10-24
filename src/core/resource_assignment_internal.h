#pragma once
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/bms_program_edit.h>

namespace wormedit {
[[nodiscard]] auto bms_occupied(const song_document_t& document, std::string_view name_space,
    std::size_t base, std::span<const resource_id_t> ignored = {}) -> std::vector<bool>;
[[nodiscard]] auto stage_resource_asset_import(document_command_transaction_t& transaction,
    const import_resource_asset_action_t& action, std::optional<bms_program_target_key_t> selection)
    -> attempt_t<document_edit_effect_t>;
[[nodiscard]] auto stage_resource_sound_metadata(document_command_transaction_t& transaction,
    const set_resource_sound_metadata_action_t& action) -> attempt_t<document_edit_effect_t>;
[[nodiscard]] auto stage_bms_resource_declaration(document_command_transaction_t& transaction,
    const bms_program_edit_target_t& target, bms_resource_declaration_t declaration)
    -> attempt_t<document_edit_effect_t>;
}
