#pragma once

#include <wormedit/core/command.h>

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace wormedit {

// Toggles continuation/restart for every retained event at one pulse in a
// BMSON sound channel. The semantic BGM projection retains its stable IDs.
[[nodiscard]] auto toggle_bmson_sound_channel_restart(
    document_history_t& history,
    chart_id_t chart, object_id_t sound_channel_event) -> document_command_result_t;

// Sets every retained event at the selected non-playable event's pulse to
// restart or continue. An already matching pulse creates no history entry.
[[nodiscard]] auto set_bmson_sound_channel_restart(
    document_history_t& history,
    chart_id_t chart, object_id_t sound_channel_event, bool restart) -> document_command_result_t;

// Moves one semantic BMSON BGM marker and its retained source event together.
// The target must be an exact chart row free within that sound channel.
[[nodiscard]] auto move_bmson_bgm_marker(document_history_t& history,
                                         chart_id_t chart, object_id_t bgm_event,
                                         local_position_t target) -> document_command_result_t;

// Inserts a retained non-playable event and its semantic BGM projection in the
// selected marker's sound channel. The new pulse either continues or restarts.
[[nodiscard]] auto insert_bmson_bgm_marker(document_history_t& history,
                                           chart_id_t chart, object_id_t reference_bgm_event,
                                           local_position_t target, bool restart)
    -> document_command_result_t;

struct bmson_source_geometry_t {
    asset_id_t asset;
    std::uint32_t sample_rate{};
    std::uint64_t frame_count{};
};

struct bmson_channel_resource_binding_t {
    resource_id_t resource;
    std::optional<object_id_t> channel;
    bool ambiguous{};
};

// Resolves semantic BMSON sound markers to retained channels. Ambiguous
// resource reuse is reported explicitly instead of choosing an arbitrary channel.
[[nodiscard]] auto bmson_channel_resource_bindings(const song_document_t& document,
                                                   chart_id_t chart)
    -> std::vector<bmson_channel_resource_binding_t>;

// Inserts a point in an explicit stem channel and links its semantic BGM
// projection. Native geometry, when supplied, must identify that channel's
// source asset; document metadata is never rewritten from decoded geometry.
[[nodiscard]] auto insert_bmson_stem_point(
    document_history_t& history, chart_id_t chart,
    object_id_t channel, local_position_t target, bool prefer_restart,
    std::optional<bmson_source_geometry_t> geometry = {}) -> document_command_result_t;

// Classifies a stem point through the shared span path and assigns its linked
// semantic event to a playable lane in one atomic history entry. An existing
// pulse is reassigned with its stable note/event identity rather than duplicated.
[[nodiscard]] auto insert_bmson_playable_stem_point(
    document_history_t& history, chart_id_t chart,
    object_id_t channel, local_position_t target, lane_id_t lane,
    std::optional<bmson_source_geometry_t> geometry = {}) -> document_command_result_t;

// Inserts an unlinked packed one-shot in the selected source lane, reusing a
// non-stem channel for the asset or creating a full-source channel on demand.
[[nodiscard]] auto insert_bmson_packed_one_shot(
    document_history_t& history, chart_id_t chart,
    object_id_t source_lane, resource_id_t resource, local_position_t target)
    -> document_command_result_t;

struct bmson_sound_marker_insertion_t {
    local_position_t position;
    bool restart{};
    // Empty selects autoplay; a lane assigns this new slice to a playable tap.
    std::optional<lane_id_t> playable_lane;
};

struct bmson_razor_path_t {
    std::vector<local_position_t> positions;
    diagnostic_list_t diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return diagnostics.empty(); }
};

// Enumerates exact grid cells after `from` through `to`, in pointer order.
// Both endpoints must already be snapped to the captured denominator.
[[nodiscard]] auto interpolate_bmson_razor_path(
    const chart_document_t& chart, local_position_t from, local_position_t to,
    std::uint64_t snap_denominator, std::size_t maximum_positions = 256)
    -> bmson_razor_path_t;

// Commits up to 256 BGM/playable markers in one history step after validating
// every target and linked source-channel pulse.
[[nodiscard]] auto
insert_bmson_sound_markers(document_history_t& history,
                           chart_id_t chart, object_id_t reference_bgm_event,
                           std::span<const bmson_sound_marker_insertion_t> markers)
    -> document_command_result_t;

// Removes the linked semantic BGM marker and retained source event atomically.
[[nodiscard]] auto delete_bmson_bgm_marker(document_history_t& history,
                                           chart_id_t chart, object_id_t bgm_event)
    -> document_command_result_t;

// Reassigns one linked BMSON tap between autoplay and a playable lane while
// retaining its semantic and source-event IDs. A null lane selects BGM.
[[nodiscard]] auto assign_bmson_sound_event(document_history_t& history, chart_id_t chart,
                                            object_id_t semantic_event,
                                            std::optional<lane_id_t> playable_lane)
    -> document_command_result_t;

} // namespace wormedit
