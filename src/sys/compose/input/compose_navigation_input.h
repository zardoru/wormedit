#pragma once
#include "sys/compose/views/compose_interaction_view.h"
#include "sys/app/application_edit_inputs.h"
#include "sys/timing/timing_edit_runtime.h"
#include "sys/timing/timing_view.h"
#include "sys/compose/input/compose_input.h"
#include "sys/compose/input/compose_minimap_navigation.h"
#include "sys/compose/input/compose_navigation.h"
#include "sys/compose/actions/compose_note_to_bgm.h"
#include "sys/compose/input/compose_playhead_skip.h"
#include "sys/compose/compose_scroll_geometry.h"
#include "sys/compose/compose_semantic_layer_commands.h"
#include "sys/compose/compose_sound_selection.h"
#include <wormedit/core/chart_clipboard.h>

#include <wormedit/audio/decoder.h>
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/command.h>
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/grid.h>
#include <wormedit/core/kiai_spans.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/note_label_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/span_manipulation.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/virtual_canvas.h>
#include <wormedit/core/workspace.h>

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace wormedit::sys::compose_navigation_input_detail {

auto staged_input_severity(bool staged, wormedit::notification_severity_t fallback)
    -> wormedit::notification_severity_t;
auto navigation_measure_count(wormedit::editor_navigation_action_t action) -> std::uint64_t;
auto navigation_minimum_rows(const wormedit::document_history_t& history,
                             const wormedit::editor_interaction_state_t& state,
                             wormedit::editor_navigation_action_t action) -> std::uint64_t;
auto navigation_page_message(wormedit::editor_navigation_action_t action) -> std::string_view;
auto skip_playhead_for_navigation(wormedit::editor_navigation_action_t action,
                                  const wormedit::document_history_t& history,
                                  const wormedit::editor_interaction_state_t& state,
                                  application_command_state_t& application)
    -> compose_action_outcome_t;
} // namespace wormedit::sys::compose_navigation_input_detail
namespace wormedit::sys {
auto compose_result_status(const wormedit::editor_interaction_result_t& result,
                           std::string_view accepted) -> std::string;
auto compose_navigation_action(WormUiComposeNavigationAction action)
    -> std::optional<wormedit::editor_navigation_action_t>;
} // namespace wormedit::sys
