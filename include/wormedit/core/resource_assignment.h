#pragma once

#include <wormedit/core/command.h>
#include <wormedit/core/interaction_types.h>
#include <wormedit/core/snap.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace wormedit {

struct bms_identifier_capacity_t {
    std::size_t used{};
    std::size_t total{};
    std::size_t largest_free_run{};
};

// BMS WAV/BMP identifiers currently use the parser/writer's supported base-36
// alphabet (01..ZZ); 00 is reserved. Base-62 codec support is separate.
[[nodiscard]] auto find_bms_identifier_run(const song_document_t& document,
                                           std::string_view name_space,
                                           std::size_t count)
    -> std::optional<std::vector<std::string>>;
[[nodiscard]] auto bms_identifier_capacity(const song_document_t& document,
                                           std::string_view name_space)
    -> bms_identifier_capacity_t;

// Shared case-insensitive WAV namespace and base-36 slot matching for review
// and commit. Zero and malformed identifiers never occupy a supported slot.
[[nodiscard]] auto bms_wav_slot_value(const target_resource_identity_t& identity)
    -> std::optional<std::size_t>;
struct bms_wav_slot_owner_t {
    resource_id_t resource;
    bool ambiguous{};
};
[[nodiscard]] auto bms_wav_slot_owner(const song_document_t& document, std::size_t slot)
    -> bms_wav_slot_owner_t;

struct renumber_bms_identifiers_action_t {
    std::string name_space;
    // Empty means every resource of this namespace's kind.
    std::vector<resource_id_t> resources;
};

[[nodiscard]] auto apply_bms_identifier_renumber(
    document_history_t& history,
    const renumber_bms_identifiers_action_t& action) -> document_command_result_t;

struct resource_assignment_limits_t {
    std::size_t maximum_selected_objects{50'000};
    std::size_t maximum_chart_objects{default_maximum_chart_objects};
};

struct assign_resource_action_t {
    chart_id_t chart;
    resource_id_t resource;
    std::vector<editor_selection_object_t> objects;
};

struct create_resource_action_t {
    asset_id_t asset;
    resource_role_t role{resource_role_t::sample};
    std::string name;
    bool allocate_bms_identifier{};
};

// Returns the next usable catalog entry in document order for a placement
// role. The picker stops at the end; it never wraps to the first resource.
[[nodiscard]] auto next_eligible_placement_resource(const song_document_t& document,
                                                    resource_id_t current, resource_role_t role)
    -> std::optional<resource_id_t>;

struct import_resource_asset_action_t {
    std::filesystem::path source_path;
    // Optional project-relative path of a validated, published import copy.
    // An export copy may leave this empty and register the external source.
    // Both copied workflows reserve an asset ID before background publication.
    std::optional<std::filesystem::path> copied_project_relative_path;
    std::optional<asset_id_t> expected_new_asset_id;
    resource_role_t role{resource_role_t::sample};
    std::string name;
    bool allocate_bms_identifier{};
    std::optional<audio_stream_info_t> audio_stream;
    std::optional<std::pair<std::uint32_t, std::uint32_t>> image_geometry;
};

// Returns the ID the next imported asset will receive, subject to the catalog
// limit. A background copy can use it as a reservation; the import command
// checks it again before committing; delayed callers also validate their captured stamp.
[[nodiscard]] auto next_resource_asset_id(const song_document_t& document)
    -> std::optional<asset_id_t>;

// Registers one externally validated absolute source file and its first
// catalog entry in one history command. File probing belongs to the caller.
[[nodiscard]] auto apply_resource_asset_import(document_history_t& history,
                                               const import_resource_asset_action_t& action)
    -> document_command_result_t;

struct assign_bms_wav_slot_action_t {
    std::string identifier;
    import_resource_asset_action_t imported_file;
    bool confirm_replacement{};
};

struct assign_bms_wav_slot_batch_action_t {
    std::string first_identifier;
    std::vector<import_resource_asset_action_t> imported_files;
    bool confirm_replacements{};
};

// Imports one externally validated audio file and binds it to the exact BMS WAV
// identifier. Replacing an occupied slot requires explicit confirmation.
[[nodiscard]] auto apply_bms_wav_slot_assignment(
    document_history_t& history,
    const assign_bms_wav_slot_action_t& action) -> document_command_result_t;

// Assigns naturally ordered audio files to successive exact WAV slots in one
// command. The batch never skips occupied slots or wraps past ZZ.
[[nodiscard]] auto apply_bms_wav_slot_batch_assignment(
    document_history_t& history,
    const assign_bms_wav_slot_batch_action_t& action) -> document_command_result_t;

// Sets the song's main audio from an externally probed file. The first Music
// resource has its asset repointed (keeping every reference); without one, the
// file is imported as a new Music+BGM resource. Native charts grow to fit.
[[nodiscard]] auto apply_primary_music_set(document_history_t& history,
                                           import_resource_asset_action_t action)
    -> document_command_result_t;

struct relink_resource_asset_action_t {
    asset_id_t asset;
    std::filesystem::path source_path;
};

// Repoints one registered audio/image source while preserving its stable asset
// ID and every resource/event reference. Relative paths stay inside the
// project directory; the caller verifies or copies the destination file.
[[nodiscard]] auto apply_resource_asset_relink(document_history_t& history,
                                               const relink_resource_asset_action_t& action)
    -> document_command_result_t;

struct set_resource_sound_metadata_action_t {
    resource_id_t resource;
    sound_kind_t kind{sound_kind_t::unspecified};
    std::vector<std::string> tags;
    std::optional<std::string> name;
};

// Sets sound kind/tags and optional name of one resource in one history
// step. Tags are trimmed and de-duplicated ignoring ASCII case, keeping the
// first spelling; more than maximum_sound_tags or over-long, empty-after-trim
// or control-character tags reject the command. A supplied name is trimmed,
// nonempty, at most 256 bytes and contains no control characters; nullopt leaves
// the name unchanged. All fields validate atomically. A no-op creates no history entry.
[[nodiscard]] auto apply_resource_sound_metadata(document_history_t& history,
                                                 const set_resource_sound_metadata_action_t& action)
    -> document_command_result_t;

using resource_reference_counts_t = std::unordered_map<std::uint64_t, std::uint64_t>;

// Exact protected-deletion counts across all charts. Direct note/BGM/BGA IDs
// count separately from retained channel events, which protect every asset alias.
// Unreferenced resources may be absent from the map and have count zero.
[[nodiscard]] auto count_resource_references(const song_document_t& document)
    -> resource_reference_counts_t;

// Removes one catalog entry only when it has no references. Notes, BGM and
// BGA events count direct resource-ID references. Retained sound-channel events
// count against every resource sharing their source asset; a linked semantic
// note/BGM and retained channel event are distinct stored references. Assets
// and their files are never removed.
[[nodiscard]] auto apply_resource_deletion(document_history_t& history,
                                           resource_id_t resource) -> document_command_result_t;

inline constexpr std::size_t maximum_resource_deletions = 100'000;

// Removes several unreferenced catalog entries in one history step. Any unknown,
// repeated or still-referenced entry rejects the whole batch, and the
// diagnostic names the first referenced sound. Chart sound-layer defaults of
// the removed sounds go with them. Assets and their files are never removed.
[[nodiscard]] auto apply_resource_deletions(document_history_t& history,
                                            std::span<const resource_id_t> resources)
    -> document_command_result_t;

enum class resource_placement_kind_t {
    sample_note,
    bgm_event,
    bga_background,
};

struct place_resource_action_t {
    chart_id_t chart;
    resource_id_t resource;
    local_position_t position;
    lane_id_t lane;
    object_id_t source_lane;
    resource_placement_kind_t kind{resource_placement_kind_t::sample_note};
    // Zero keeps an already exact cursor row. Pointer tools pass the active
    // denominator so all placement kinds use the same nearest-row policy.
    snap_grid_t snap;
    std::optional<std::vector<layer_id_t>> semantic_layers;
    std::optional<visual_event_kind_t> bga_kind;
};

struct place_virtual_bms_resource_action_t {
    chart_id_t chart;
    resource_id_t resource;
    std::uint64_t canvas_row{};
    lane_id_t lane;
    object_id_t source_lane;
    resource_placement_kind_t kind{resource_placement_kind_t::sample_note};
    snap_grid_t snap;
    std::optional<std::vector<layer_id_t>> semantic_layers;
    std::optional<visual_event_kind_t> bga_kind;
};

struct bga_stroke_target_t {
    local_position_t position;
    visual_event_kind_t kind{visual_event_kind_t::background};
};

// Applies one atomic BGA draw/erase stroke. Draw preserves occupied cells;
// erase removes events in the visited BGA lanes without requiring a resource.
[[nodiscard]] auto apply_bga_stroke(document_command_transaction_t& transaction,
                                    chart_id_t chart,
                                    std::optional<resource_id_t> resource,
                                    std::span<const bga_stroke_target_t> targets,
                                    bool erase = false) -> document_command_result_t;

[[nodiscard]] auto apply_bga_stroke(document_history_t& history,
                                    chart_id_t chart,
                                    std::optional<resource_id_t> resource,
                                    std::span<const bga_stroke_target_t> targets,
                                    bool erase = false) -> document_command_result_t;

// Applies one atomic BGM draw/erase stroke on one source lane. Draw skips cells that
// already hold the same sound and rejects the whole stroke when a different sound
// occupies a cell; erase removes the lane's ordinary events at the visited rows.
// BMSON charts delegate to apply_bmson_packed_stroke; their stem lanes are not drawable.
[[nodiscard]] auto apply_bgm_stroke(document_command_transaction_t& transaction,
                                    chart_id_t chart,
                                    object_id_t source_lane,
                                    std::optional<resource_id_t> resource,
                                    std::span<const local_position_t> targets,
                                    bool erase = false) -> document_command_result_t;

[[nodiscard]] auto apply_bgm_stroke(document_history_t& history,
                                    chart_id_t chart,
                                    object_id_t source_lane,
                                    std::optional<resource_id_t> resource,
                                    std::span<const local_position_t> targets,
                                    bool erase = false) -> document_command_result_t;

// Applies one atomic draw/erase stroke on an unlinked (packed) BMSON keysound column.
// Draw adds sound-channel events for `resource` (creating its channel on demand) with
// their BGM projections and skips rows that already show the sound; erase removes the
// column's channel-linked events at the visited rows. Stem lanes are rejected.
// Defined in bmson_sound_channel_editor.cpp.
[[nodiscard]] auto apply_bmson_packed_stroke(document_command_transaction_t& transaction,
                                             chart_id_t chart,
                                             object_id_t source_lane,
                                             std::optional<resource_id_t> resource,
                                             std::span<const local_position_t> targets,
                                             bool erase = false) -> document_command_result_t;

[[nodiscard]] auto apply_bmson_packed_stroke(document_history_t& history,
                                             chart_id_t chart,
                                             object_id_t source_lane,
                                             std::optional<resource_id_t> resource,
                                             std::span<const local_position_t> targets,
                                             bool erase = false) -> document_command_result_t;

struct move_bgm_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    // Empty keeps the event in its current lane. An occupied target advances
    // through source-lane order, appending a lane when none is free.
    object_id_t source_lane;
    snap_grid_t snap;
};

struct move_bga_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    snap_grid_t snap;
    std::optional<visual_event_kind_t> bga_kind;
};

struct move_virtual_bms_bga_event_action_t {
    chart_id_t chart;
    object_id_t event;
    std::uint64_t canvas_row{};
    snap_grid_t snap;
    std::optional<visual_event_kind_t> bga_kind;
};

// Moves one background image/video event, preserving its stable identity,
// resource, kind, and source provenance.
[[nodiscard]] auto apply_bga_event_move(document_history_t& history,
                                        const move_bga_event_action_t& action)
    -> document_command_result_t;

// Materializes the virtual destination measure and moves one BGA event in a
// single undo command, preserving the event's identity and source metadata.
[[nodiscard]] auto apply_virtual_bms_bga_event_move(
    document_history_t& history,
    const move_virtual_bms_bga_event_action_t& action) -> document_command_result_t;

struct move_virtual_bms_bgm_event_action_t {
    chart_id_t chart;
    object_id_t event;
    std::uint64_t canvas_row{};
    object_id_t source_lane;
    snap_grid_t snap;
};

// Moves one ordinary BGM event, preserving its ID and sound. Linked BMSON
// markers and BMS branch-owned events use their source-aware editors.
[[nodiscard]] auto apply_bgm_event_move(document_history_t& history,
                                        const move_bgm_event_action_t& action)
    -> document_command_result_t;

// Materializes a virtual BMS destination and moves the BGM event in one undo
// command. Rejections leave both the chart and its authored extent untouched.
[[nodiscard]] auto apply_virtual_bms_bgm_event_move(
    document_history_t& history,
    const move_virtual_bms_bgm_event_action_t& action) -> document_command_result_t;

[[nodiscard]] auto resolve_resource_placement_position(const chart_document_t& chart,
                                                       local_position_t position,
                                                       snap_grid_t snap)
    -> std::optional<local_position_t>;

// Inserts one resource-backed event at an exact chart row. BGM placement
// reuses the first unoccupied source lane or appends one in the same command.
[[nodiscard]] auto apply_resource_placement(document_history_t& history,
                                            const place_resource_action_t& action)
    -> document_command_result_t;

// Commits required default BMS geometry and a resource-backed event together.
[[nodiscard]] auto apply_virtual_bms_resource_placement(
    document_history_t& history,
    const place_virtual_bms_resource_action_t& action) -> document_command_result_t;

// Creates a catalog entry for an already registered asset. Optional BMS
// allocation uses the first free nonzero two-digit ID in its WAV/BMP namespace.
[[nodiscard]] auto apply_resource_creation(document_history_t& history,
                                           const create_resource_action_t& action)
    -> document_command_result_t;

// Shared catalog eligibility for placement, pointer preflight and assignment.
// Notes require Sample; BGM accepts Sample or BGM; visual events require BGA.
[[nodiscard]] auto validate_resource_role(const song_document_t& document, resource_id_t resource,
    resource_role_t role, document_command_result_t& result) -> bool;

// Assigns one catalog resource to an explicit typed note/BGM/BGA selection.
// BMSON objects linked to retained sound-channel events cannot be rebound to
// another resource without editing their source channel. A no-op adds no history.
[[nodiscard]] auto apply_resource_assignment(document_history_t& history,
                                             const assign_resource_action_t& action,
                                             resource_assignment_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
