#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/resource_assignment.h>

#include <string>
#include <optional>
#include <cstddef>
#include <vector>

namespace wormedit {

inline constexpr std::size_t maximum_bulk_sound_import = 1'295;

struct bulk_sound_import_entry_t {
    // role is forced to sample; the catalog role set is derived from `kind`.
    import_resource_asset_action_t asset;
    sound_kind_t kind{sound_kind_t::keysound};
    std::vector<std::string> tags;
};

struct bulk_sound_import_group_t {
    std::string name;
    std::optional<sound_group_id_t> existing_group;
    // Indexes into the entries vector; each entry belongs to at most one group.
    std::vector<std::size_t> entry_indexes;
};

struct bulk_sound_import_action_t {
    // Chart that receives stems as BMSON sound channels. Empty, or a chart
    // that is not BMSON, registers stems as catalog sounds only.
    chart_id_t chart;
    std::vector<bulk_sound_import_entry_t> entries;
    std::vector<bulk_sound_import_group_t> groups;
};

// Registers 1..1295 probed audio files as assets and resources in one history
// step, in list order. BMS WAV identifiers are allocated as one contiguous run
// when possible, or in the fewest largest free ranges while preserving order.
// Entries assigned to a group receive their own contiguous WAV ID run, and the
// group edits commit with the import in the same history step.
// Each stem imported into a BMSON chart also gets a sound
// channel with a restart event at the first row and a linked semantic BGM
// marker in its own BGM lane, so it appears in Compose. File copying stays with
// the caller, which reserves consecutive asset IDs from `next_resource_asset_id`
// for copied entries exactly as for a single import. One invalid entry (or a
// duplicate source path) rejects the whole batch and leaves the document alone.
[[nodiscard]] auto apply_bulk_sound_import(document_history_t& history,
                                           std::uint64_t expected_revision,
                                           const bulk_sound_import_action_t& action)
    -> document_command_result_t;

} // namespace wormedit
