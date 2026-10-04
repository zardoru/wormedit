#pragma once

#include "sys/compose_view.h"
#include "ui/frontend.h"

#include <wormedit/core/command.h>
#include <wormedit/core/notifications.h>
#include <wormedit/core/sound_import.h>
#include <wormedit/core/workspace.h>

#include <optional>
#include <span>
#include <cstdint>
#include <string>
#include <vector>

namespace wormedit::sys {

struct prepared_keysound_import_t {
    std::vector<bulk_sound_import_entry_t> entries;
    std::size_t skipped_unreadable{};
    std::size_t skipped_existing{};
    std::string message;
};

struct prepared_sound_slot_import_item_t {
    std::string path;
    std::string name;
    std::uint64_t occupied_resource_id{};
    std::string occupied_name;
    bool ambiguous{};
};

struct prepared_sound_slot_batch_t {
    std::vector<prepared_sound_slot_import_item_t> items;
    std::string reason;
};

// Stages only decodable audio candidates; it never mutates the document.
[[nodiscard]] auto prepare_keysound_import(const song_document_t& document, chart_id_t chart,
                                           std::span<const std::string> paths)
    -> prepared_keysound_import_t;

// Prepares a complete direct-slot preview in natural filename order. Every
// input is retained for review; any invalid path, duplicate, or range error
// sets reason and the whole batch remains ineligible for confirmation.
[[nodiscard]] auto prepare_sound_slot_batch(const song_document_t& document, chart_id_t chart,
                                            std::uint16_t first_slot,
                                            std::span<const std::string> paths)
    -> prepared_sound_slot_batch_t;

// Registers dropped or picked audio files as keysounds in one undo step. Files
// that are not decodable audio, or that already back a sample, are skipped and
// counted in the returned message. BMS charts give each new sound the next
// free #WAV slot. The input order is sorted by file name for stable slots.
[[nodiscard]] auto import_keysound_files(document_history_t& history,
                                         workspace_controller_t& workspace, chart_id_t chart,
                                         std::span<const std::string> paths,
                                         std::optional<notification_sink_t> notifications = {}) -> std::string;

// Keysound listbox actions. Returns nullopt for any other action. The status
// message is always non-empty on success or failure.
[[nodiscard]] auto dispatch_compose_sound_action(const WormUiWorkspaceAction& action,
                                                 document_history_t& history,
                                                 workspace_controller_t& workspace,
                                                 chart_id_t chart,
                                                 application_command_state_t& application_state,
                                                 std::optional<notification_sink_t> notifications = {})
    -> std::optional<std::string>;

} // namespace wormedit::sys
