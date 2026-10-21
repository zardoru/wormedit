#pragma once
#include "sys/compose_view.h"
#include <wormedit/core/timing.h>
#include <optional>
#include <string>
namespace wormedit::sys {
struct compose_minimap_target_t {
    std::uint64_t frame{};
    long double absolute_row{};
    std::uint64_t snapped_row{};
};
[[nodiscard]] auto compose_minimap_target(double fraction, std::uint64_t total_frames,
    std::uint32_t waveform_rate, std::uint32_t preview_rate, const timing_index_t& timing,
    const measure_index_t& measures, snap_grid_t snap, std::optional<std::uint64_t> compiled_end = {})
    -> std::optional<compose_minimap_target_t>;
[[nodiscard]] auto move_compose_minimap_cursor(document_history_t& history,
    editor_interaction_state_t& state, const measure_index_t& measures,
    const timing_index_t& timing, const compose_minimap_target_t& target)
    -> editor_interaction_result_t;
[[nodiscard]] auto seek_stopped_compose_transport(document_history_t& history,
                                                  editor_interaction_state_t& state,
                                                  const compose_scene_cache_t& cache,
                                                  std::uint64_t frame) -> compose_action_outcome_t;
void retain_compose_minimap_start(application_command_state_t& application,
    const document_history_t& history, const editor_interaction_state_t& state,
    const compose_minimap_target_t& target, const timing_index_t& timing);
[[nodiscard]] auto compose_minimap_start_frame(application_command_state_t& application,
    const document_history_t& history, const editor_interaction_state_t* state)
    -> std::optional<std::uint64_t>;
// Never starts playback; transport failures are returned while navigation can continue.
[[nodiscard]] auto navigate_compose_minimap_transport(application_command_state_t& state,
    std::uint64_t frame, bool shift, bool primary) -> std::string;
}
