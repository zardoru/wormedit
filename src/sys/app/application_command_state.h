#pragma once
#include "sys/compose/compose_clipboard_workflow.h"
#include "sys/compose/actions/compose_quantize_state.h"
#include "sys/bms_program_runtime.h"
#include "sys/bookmarks/bookmark_navigation.h"
#include "sys/compose/actions/compose_audition_requests.h"
#include "sys/compose/actions/compose_action_outcome.h"
#include "sys/audio/metronome.h"
#include "sys/compose/compose_bgm_bar.h"
#include "sys/compose/views/recipe_form.h"
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/player_preview.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/lane_keys.h>
#include <wormedit/core/preview_mix.h>
#include <wormedit/core/preferences.h>
#include <wormedit/audio/playback_rate.h>
#include <wormedit/core/interaction_state.h>
#include "ui/compose_timing_geometry.h"
#include "ui/compose_mode.h"
#include <chrono>
#include <functional>
#include <set>
#include <tuple>
#include <unordered_map>
namespace wormedit::sys { class recipe_evaluation_runtime_t; }
namespace wormedit::sys {
class generator_runtime_t;
namespace compose_input_detail { struct compose_input_context_t; }
enum class external_player_action_t;

// Application-lifetime callbacks are one unit. Replacing a project resets chart state,
// while this complete bundle stays in place.
struct application_command_services_t {
    std::function<bool(wormedit::chart_id_t, std::span<const wormedit::layer_id_t>)>
        transport_set_layer_assist;
    std::function<bool(wormedit::rational_t)> transport_set_playback_rate;
    std::function<bool(bool)> transport_set_keep_pitch;
    std::function<bool()> transport_active;
    std::function<bool()> transport_inline_active;
    std::function<bool()> transport_preview_playing;
    std::function<bool()> transport_preview_paused;
    std::function<std::optional<std::uint64_t>()> transport_preview_end_frame;
    std::function<std::uint64_t(std::uint64_t)> transport_preview_frame_from_source;
    std::function<bool(std::uint64_t, std::uint64_t, std::uint64_t)> open_sound_picker;
    std::function<bool(std::uint64_t, std::uint64_t, std::uint64_t)> open_project_export;
    std::function<bool(WormUiComposeDockCommand, std::uint64_t, std::uint64_t, std::uint64_t)>
        compose_dock_command;
    std::function<int()> compose_left_dock_pixels;
    std::function<double(std::uint64_t, std::uint64_t, std::uint64_t)> compose_bgm_lane_width;
    std::function<std::string()> transport_play_pause;
    std::function<std::string()> transport_play_from_start;
    std::function<compose_action_outcome_t(std::uint64_t)> transport_seek;
    std::function<std::optional<std::int64_t>()> transport_audio_milliseconds;
    std::function<compose_action_outcome_t(std::int64_t)> transport_seek_audio_milliseconds;
    std::function<void()> transport_stop;
    std::function<bool(wormedit::chart_id_t, std::uint64_t, std::uint64_t, std::uint64_t, bool)>
        transport_set_loop_selection;
    std::function<bool()> transport_loop_selection_active;
    std::function<bool(wormedit::chart_id_t, std::span<const std::uint64_t>)> transport_set_bgm_mix;
    std::function<bool(const wormedit::preview_mix_t&)> transport_set_preview_mix;
    std::function<std::string(external_player_action_t)> external_player;
    std::function<std::string()> transport_cancel_restore;
    // Current system clipboard text, or empty when unavailable.
    std::function<std::string()> system_clipboard_text;
};

struct bgm_lane_mix_state_t {
    std::set<std::uint64_t> muted;
    std::set<std::uint64_t> solo;
};
struct application_command_state_t {
    using quantize_editor_t = compose_quantize_editor_t;
    using quantize_applied_report_t = compose_quantize_applied_report_t;
    using paste_session_t = compose_paste_session_t;
    struct bmson_marker_draft_t;
    using bmson_marker_draft_proposal_t = wormedit::proposal_t<bmson_marker_draft_t>;
    using bmson_marker_draft_outcome_t = wormedit::proposal_outcome_t<bmson_marker_draft_t,
        wormedit::chart_stamp_t, std::optional<wormedit::diagnostic_t>>;
    struct bmson_marker_draft_t {
        wormedit::object_id_t reference;
        std::vector<wormedit::bmson_sound_marker_insertion_t> markers;
        [[nodiscard]] auto has_position(wormedit::local_position_t position) const -> bool;

        [[nodiscard]] static auto propose(const wormedit::document_history_t& history,
            const wormedit::editor_interaction_state_t& state, wormedit::object_id_t reference,
            std::span<const wormedit::bmson_sound_marker_insertion_t> markers)
            -> bmson_marker_draft_outcome_t;
        [[nodiscard]] static auto revise(const wormedit::document_history_t& history,
            const wormedit::editor_interaction_state_t& state,
            std::optional<bmson_marker_draft_proposal_t>& previous, wormedit::object_id_t reference,
            std::span<const wormedit::bmson_sound_marker_insertion_t> markers)
            -> bmson_marker_draft_outcome_t;
        // Input routing checked the capture and native scratch validation prepared these markers.
        [[nodiscard]] auto revise(wormedit::chart_stamp_t captured,
            std::vector<wormedit::bmson_sound_marker_insertion_t> prepared)
            -> bmson_marker_draft_outcome_t;
    };
    static_assert(wormedit::staged_proposal<bmson_marker_draft_t,
        std::tuple<const wormedit::document_history_t&, const wormedit::editor_interaction_state_t&,
            wormedit::object_id_t, std::span<const wormedit::bmson_sound_marker_insertion_t>>,
        std::tuple<const wormedit::document_history_t&, const wormedit::editor_interaction_state_t&,
            std::optional<bmson_marker_draft_proposal_t>&, wormedit::object_id_t,
            std::span<const wormedit::bmson_sound_marker_insertion_t>>,
        wormedit::chart_stamp_t, std::optional<wormedit::diagnostic_t>>);
    static_assert(wormedit::staged_proposal<bmson_marker_draft_t,
        std::tuple<const wormedit::document_history_t&, const wormedit::editor_interaction_state_t&,
            wormedit::object_id_t, std::span<const wormedit::bmson_sound_marker_insertion_t>>,
        std::tuple<wormedit::chart_stamp_t, std::vector<wormedit::bmson_sound_marker_insertion_t>>,
        wormedit::chart_stamp_t, std::optional<wormedit::diagnostic_t>>);
    struct bmson_razor_stroke_t;
    using bmson_razor_stroke_proposal_t = wormedit::proposal_t<bmson_razor_stroke_t>;
    using bmson_razor_stroke_outcome_t = wormedit::proposal_outcome_t<bmson_razor_stroke_t>;
    struct bmson_razor_stroke_t {
        wormedit::object_id_t reference;
        wormedit::snap_grid_t snap;
        wormedit::local_position_t last_position;

        // A token exists only after its marker owner staged this exact pulse.
        [[nodiscard]] static auto propose(const wormedit::document_history_t& history,
            wormedit::chart_id_t chart, const bmson_marker_draft_proposal_t* markers,
            wormedit::object_id_t reference, wormedit::snap_grid_t snap,
            wormedit::local_position_t position) -> bmson_razor_stroke_outcome_t;
        // The input controller already validated the path and acknowledged its marker segment.
        [[nodiscard]] auto revise(wormedit::chart_stamp_t captured,
            wormedit::local_position_t prepared_position) -> bmson_razor_stroke_outcome_t;
    };
    static_assert(wormedit::staged_proposal<bmson_razor_stroke_t,
        std::tuple<const wormedit::document_history_t&, wormedit::chart_id_t,
            const bmson_marker_draft_proposal_t*, wormedit::object_id_t, wormedit::snap_grid_t,
            wormedit::local_position_t>,
        std::tuple<wormedit::chart_stamp_t, wormedit::local_position_t>>);
    struct bga_draw_stroke_t;
    using bga_draw_stroke_proposal_t = wormedit::proposal_t<bga_draw_stroke_t>;
    using bga_draw_stroke_outcome_t = wormedit::proposal_outcome_t<bga_draw_stroke_t>;
    struct bga_draw_stroke_t {
        std::optional<wormedit::resource_id_t> resource;
        bool erase{};
        uint64_t last_row{};
        wormedit::measure_index_t geometry;
        std::vector<wormedit::bga_stroke_target_t> targets;
        std::set<std::pair<uint64_t, uint8_t>> visited;

        [[nodiscard]] static auto propose(const wormedit::ui::compose_input_action_t& input,
            const compose_input_detail::compose_input_context_t& context,
            const wormedit::chart_document_t& chart) -> bga_draw_stroke_outcome_t;
        [[nodiscard]] static auto is_lane_valid(const wormedit::ui::compose_input_action_t& input) -> bool;
        [[nodiscard]] auto revise(wormedit::chart_stamp_t captured,
            const wormedit::ui::compose_input_action_t& input, const wormedit::chart_document_t& chart,
            const wormedit::editor_interaction_state_t& state, std::optional<uint64_t> row,
            bool is_beginning) -> bga_draw_stroke_outcome_t;
    };
    static_assert(wormedit::staged_proposal<bga_draw_stroke_t,
        std::tuple<const wormedit::ui::compose_input_action_t&,
            const compose_input_detail::compose_input_context_t&, const wormedit::chart_document_t&>,
        std::tuple<wormedit::chart_stamp_t, const wormedit::ui::compose_input_action_t&,
            const wormedit::chart_document_t&, const wormedit::editor_interaction_state_t&,
            std::optional<uint64_t>, bool>>);
    struct minimap_start_frame_t {
        wormedit::chart_id_t chart;
        std::uint64_t revision{};
        wormedit::local_position_t cursor;
        std::uint64_t frame{};
        double cursor_subrow{};
    };
    // The target of the last playing playhead skip (wheel notch or step), for the same reason.
    struct playhead_skip_record_t {
        wormedit::chart_id_t chart{};
        std::uint64_t row{};
        bool forward{};
        std::chrono::steady_clock::time_point at;
    };

    application_command_services_t services;
    bookmark_navigation_t bookmarks;
    metronome_t* metronome{};
    [[nodiscard]] auto metronome_settings() const -> const metronome_settings_t& {
        static const metronome_settings_t defaults;
        return metronome ? metronome->settings : defaults;
    }
    // Session-only playback mix, like the BGM lane mix; never saved with the project.
    wormedit::preview_mix_t preview_mix;
    struct preferences_t {
        wormedit::lane_key_preferences_t lane_keys;
        wormedit::default_lane_keys_t default_lane_keys;
        // Editor placement audition is distinct from Sounds-panel preview-on-click.
        wormedit::editor_preferences_t editor_preferences;
        bool minimap_click_moves_paused_playhead{};
        std::size_t transport_rate_index{static_cast<std::size_t>(wormedit::audio::playback_rate_id_t::normal)};
        bool transport_keep_pitch{};
        bool transport_keep_pitch_available{};
        bool timing_settings_visible{};
    } preferences;
    // Resolved from display preferences and the field skin, outside the project lifetime.
    wormedit::editor_scratch_presentation_t scratch_on_right;
    // Whether the system clipboard holds BMSE or iBMSC text; refreshed when the clipboard changes.
    bool system_clipboard_has_bms_editor_text{};

    struct project_t {
        bms_program_runtime_t bms_program;
        compose_auditions_t auditions;

        struct sound_selection_state_t {
            // Fresh per-frame source context, stamped to prevent cross-chart/stale hover reuse.
            std::optional<wormedit::chart_id_t> sound_selection_chart;
            std::uint64_t sound_selection_revision{};
            std::optional<wormedit::object_id_t> sound_selection_hover;
            std::vector<wormedit::resource_id_t> sound_selection_dock;
        } sound_selection;

        struct feedback_state_t {
        } feedback;

        compose_clipboard_workflow_t clipboard;
        compose_quantize_state_t quantize;

        struct compose_gesture_state_t {
            std::optional<bmson_marker_draft_proposal_t> bmson_marker_draft;
            std::optional<bmson_razor_stroke_proposal_t> bmson_razor_stroke;
            std::optional<bga_draw_stroke_proposal_t> bga_draw_stroke;
            uint8_t draw_source_kind{};
            uint64_t draw_source_id{};
            bool note_paint_mode_active{};
            bool semantic_layer_paint_mode_active{};
            bool resource_paint_mode_active{};
            [[nodiscard]] auto can_paint(const wormedit::song_document_t& document,
                const wormedit::editor_interaction_state_t& state) const -> bool {
                const auto* chart = document.read_chart(state.chart);
                return chart && (sample_resource(document).has_value() ||
                    state.has_hitsound_palette(chart->source_family));
            }
            [[nodiscard]] auto sample_resource(const wormedit::song_document_t& document) const
                -> std::optional<wormedit::resource_id_t> {
                if (draw_source_kind != 1) return std::nullopt;
                wormedit::document_command_result_t eligibility;
                const auto id = wormedit::resource_id_t{draw_source_id};
                if (!wormedit::validate_resource_role(document, id, wormedit::resource_role_t::sample, eligibility))
                    return std::nullopt;
                return id;
            }
        } compose_gesture;

        struct semantic_layer_state_t {
            bool semantic_layer_solo_active{};
            std::optional<wormedit::layer_id_t> semantic_layer_target;
            bool semantic_layer_target_none{};
            std::optional<wormedit::chart_id_t> layer_assist_chart;
            std::vector<wormedit::layer_id_t> layer_assist_layers;
            // IDs are chart-local, so filters are stamped to their owning chart.
            std::optional<wormedit::chart_id_t> semantic_layer_filter_chart;
            std::vector<wormedit::layer_id_t> required_semantic_layer_filters;
            std::vector<wormedit::layer_id_t> excluded_semantic_layer_filters;
            std::optional<wormedit::chart_id_t> semantic_layer_palette_chart;
            std::size_t semantic_layer_palette_page{};
            std::size_t semantic_layer_first_row{};
            std::size_t semantic_layer_row_count{32};
            wormedit::chart_id_t note_layer_menu_chart{};
            std::uint64_t note_layer_menu_revision{};
            std::size_t note_layer_menu_first_row{};
            std::size_t note_layer_menu_row_count{};
        } semantic_layer;

        struct compose_view_state_t {
            double compose_editor_right_pixels{};
            std::unordered_map<std::uint64_t, std::uint64_t> requested_snap_denominators;
            std::unordered_map<std::uint64_t, double> bgm_horizontal_scroll_pixels;
            std::uint16_t compose_timing_lane_visibility_mask{wormedit::default_timing_lane_mask};
            bool compose_timing_focus{};
            bool edit_snap_enabled{true};
            // Shared by the edit grid and Quantize (see wormedit::snap_grid_t).
            wormedit::quantization_space_t snap_space{wormedit::quantization_space_t::standard_measure};
            bool y_lock{false};
            // Session-only, like the bar-line time policy below: never persisted.
            bool measure_lane_visible{false};
            wormedit::bar_line_time_t bar_line_time{wormedit::bar_line_time_t::keep_song_time};
            bool player_preview_visible{};
            wormedit::player_preview_skin_t player_preview_skin{
                wormedit::player_preview_skin_t::neutral};
        } compose_view;

        struct bgm_state_t {
            std::unordered_map<std::uint64_t, bgm_lane_mix_state_t> bgm_lane_mix;
            // Session-only BGM bar window and pointer capture.
            bgm_bar_range_t bgm_bar_range;
            std::optional<bgm_bar_pan_override_t> bgm_bar_pan_override;
            std::optional<bgm_bar_window_capture_t> bgm_bar_window_capture;
            std::optional<bool> bgm_bar_hidden;
        } bgm;

        struct generator_state_t {
            recipe_id_t selected;
            std::shared_ptr<generator_runtime_t> runtime;
        } generator;
        struct timing_recipe_state_t {
            bool timing_locked{};
            std::uint64_t kiai_selected_head{};
            std::uint64_t recipe_selected{};
            std::optional<recipe_form_outcome_t> recipe_preview;
            std::shared_ptr<recipe_evaluation_runtime_t> evaluation;
            bool audio_offset_locked{};
        } timing_recipe;

        struct transport_state_t {
            std::optional<double> preview_playhead_absolute_row;
            std::optional<minimap_start_frame_t> minimap_start_frame;
            std::optional<playhead_skip_record_t> last_playhead_skip;
        } transport;
    } project;

    [[nodiscard]] bool audio_offset_edits_locked() const noexcept {
        return project.timing_recipe.timing_locked || project.timing_recipe.audio_offset_locked;
    }
    // Any other seek or a playback-session change breaks a repeat sequence: the next skip or
    // bookmark step must start from the live playhead.
    void invalidate_playing_seek_records() {
        bookmarks.clear_seek_record();
        project.transport.last_playhead_skip.reset();
    }
};

auto begin_compose_paste(application_command_state_t& application_state,
    const wormedit::document_history_t& history, const wormedit::editor_interaction_state_t& state,
    std::string& status) -> bool;
[[nodiscard]] inline auto compose_mode_of(const application_command_state_t& state)
    -> wormedit::ui::compose_mode_t {
    return state.project.compose_view.compose_timing_focus ? wormedit::ui::compose_mode_t::timing_focus
                                                    : wormedit::ui::compose_mode_t::full;
}

// Timing lane mask in force for drawing, hit testing, and geometry.
[[nodiscard]] inline auto effective_compose_timing_lane_mask(
    const application_command_state_t& state) -> std::uint16_t {
    return wormedit::ui::compose_effective_timing_lane_mask(
        compose_mode_of(state), state.project.compose_view.compose_timing_lane_visibility_mask);
}

inline auto toggle_transport_metronome(application_command_state_t& state) -> bool {
    return state.metronome && state.metronome->set_enabled(!state.metronome->settings.enabled);
}
inline void apply_timing_settings_action(application_command_state_t& state,
                                         const wormedit::ui::timing_settings_action_t& action) {
    if (action.toggle) state.preferences.timing_settings_visible = !state.preferences.timing_settings_visible;
    if (action.close) state.preferences.timing_settings_visible = false;
    if (!state.metronome) return;
    if (action.change_enable) static_cast<void>(state.metronome->set_enabled(action.enabled));
    if (action.change_volume) static_cast<void>(state.metronome->set_volume(action.volume));
    if (action.change_count_in) static_cast<void>(state.metronome->set_count_in(action.count_in));
}

inline void apply_preview_mix_action(application_command_state_t& state,
                                     const std::optional<wormedit::preview_mix_t>& requested) {
    if (!requested || *requested == state.preview_mix || !state.services.transport_set_preview_mix) return;
    if (state.services.transport_set_preview_mix(*requested)) state.preview_mix = *requested;
}

} // namespace wormedit::sys
