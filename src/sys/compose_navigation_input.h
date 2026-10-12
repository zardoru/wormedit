#pragma once
#include "sys/application_dispatch_context.h"
#include "sys/compose_input.h"
#include "sys/compose_minimap_navigation.h"
#include "sys/compose_navigation.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_playhead_skip.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/compose_semantic_layer_commands.h"
#include "sys/compose_sound_selection.h"
#include <wormedit/core/chart_clipboard.h>
#include <wormedit/core/interaction_gestures.h>

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
auto input_notification_severity(wormedit::diagnostic_severity_t severity)
    -> wormedit::notification_severity_t;
auto post_input_feedback(const wormedit::notification_sink_t* notifications,
                         wormedit::notification_severity_t severity,
                         wormedit::notification_source_t source, std::string_view message) -> void;
auto staged_input_severity(bool staged, wormedit::notification_severity_t fallback)
    -> wormedit::notification_severity_t;
auto navigation_measure_count(wormedit::editor_navigation_action_t action) -> std::uint64_t;
auto navigation_minimum_rows(const wormedit::document_history_t& history,
                             const wormedit::editor_interaction_state_t& state,
                             wormedit::editor_navigation_action_t action) -> std::uint64_t;
auto navigation_page_message(wormedit::editor_navigation_action_t action) -> std::string_view;
struct playhead_navigation_feedback_t {
    std::string message;
    wormedit::notification_severity_t severity{wormedit::notification_severity_t::warning};
    bool posted_by_seek{};
};
auto skip_playhead_for_navigation(wormedit::editor_navigation_action_t action,
                                  const wormedit::document_history_t& history,
                                  const wormedit::editor_interaction_state_t& state,
                                  application_command_state_t& application)
    -> playhead_navigation_feedback_t;
} // namespace wormedit::sys::compose_navigation_input_detail
namespace wormedit::sys {
auto compose_result_status(const wormedit::editor_interaction_result_t& result,
                           std::string_view accepted) -> std::string;
auto compose_view_for_state(const wormedit::document_history_t& history,
                            const wormedit::editor_interaction_state_t& state)
    -> std::optional<wormedit::editor_interaction_view_model_t>;
auto compose_navigation_action(WormUiComposeNavigationAction action)
    -> std::optional<wormedit::editor_navigation_action_t>;
} // namespace wormedit::sys
