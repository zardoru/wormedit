#pragma once

#include <wormedit/core/interaction.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wormedit {

struct resource_assignment_limits_t {
    std::size_t maximum_selected_objects{50'000};
    std::size_t maximum_chart_objects{1'000'000};
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
// checks it again against the expected document revision before committing.
[[nodiscard]] auto next_resource_asset_id(const song_document_t& document)
    -> std::optional<asset_id_t>;

// Registers one externally validated absolute source file and its first
// catalog entry in one history command. File probing belongs to the caller.
[[nodiscard]] auto apply_resource_asset_import(document_history_t& history,
                                               std::uint64_t expected_revision,
                                               const import_resource_asset_action_t& action)
    -> document_command_result_t;

struct relink_resource_asset_action_t {
    asset_id_t asset;
    std::filesystem::path source_path;
};

// Repoints one registered audio/image source while preserving its stable asset
// ID and every resource/event reference. Relative paths stay inside the
// project directory; the caller verifies or copies the destination file.
[[nodiscard]] auto apply_resource_asset_relink(document_history_t& history,
                                               std::uint64_t expected_revision,
                                               const relink_resource_asset_action_t& action)
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
    std::uint64_t snap_denominator{};
};

struct place_virtual_bms_resource_action_t {
    chart_id_t chart;
    resource_id_t resource;
    std::uint64_t canvas_row{};
    lane_id_t lane;
    object_id_t source_lane;
    resource_placement_kind_t kind{resource_placement_kind_t::sample_note};
    std::uint64_t snap_denominator{};
};

struct move_bgm_event_action_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    // Empty keeps the event in its current lane. An occupied target advances
    // through source-lane order, appending a lane when none is free.
    object_id_t source_lane;
    std::uint64_t snap_denominator{};
};

// Moves one ordinary BGM event, preserving its ID and sound. Linked BMSON
// markers and BMS branch-owned events use their source-aware editors.
[[nodiscard]] auto apply_bgm_event_move(document_history_t& history,
                                        std::uint64_t expected_revision,
                                        const move_bgm_event_action_t& action)
    -> document_command_result_t;

[[nodiscard]] auto resolve_resource_placement_position(const chart_document_t& chart,
                                                       local_position_t position,
                                                       std::uint64_t snap_denominator)
    -> std::optional<local_position_t>;

// Inserts one resource-backed event at an exact chart row. BGM placement
// reuses the first unoccupied source lane or appends one in the same command.
[[nodiscard]] auto apply_resource_placement(document_history_t& history,
                                            std::uint64_t expected_revision,
                                            const place_resource_action_t& action)
    -> document_command_result_t;

// Commits required default BMS geometry and a resource-backed event together.
[[nodiscard]] auto apply_virtual_bms_resource_placement(
    document_history_t& history, std::uint64_t expected_revision,
    const place_virtual_bms_resource_action_t& action) -> document_command_result_t;

// Creates a catalog entry for an already registered asset. Optional BMS
// allocation uses the first free nonzero two-digit ID in its WAV/BMP namespace.
[[nodiscard]] auto apply_resource_creation(document_history_t& history,
                                           std::uint64_t expected_revision,
                                           const create_resource_action_t& action)
    -> document_command_result_t;

// Assigns one catalog resource to an explicit typed note/BGM/BGA selection.
// BMSON objects linked to retained sound-channel events cannot be rebound to
// another resource without editing their source channel. A no-op adds no history.
[[nodiscard]] auto apply_resource_assignment(document_history_t& history,
                                             std::uint64_t expected_revision,
                                             const assign_resource_action_t& action,
                                             resource_assignment_limits_t limits = {})
    -> document_command_result_t;

} // namespace wormedit
