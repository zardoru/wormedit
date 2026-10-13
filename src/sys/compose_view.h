#pragma once
#include "sys/metronome.h"
#include <wormedit/audio/playback_rate.h>
#include "sys/bookmark_navigation.h"
#include <algorithm>

#include "core/interaction_context.h"

#include "sys/workspace_projection.h"
#include "sys/compose_navigation_travel.h"
#include "common/static_interval_index.h"
#include "sys/compose_bgm_bar.h"
#include "sys/compose_dead_spans.h"
#include "sys/compose_kiai_spans.h"
#include "sys/compose_recipe_areas.h"
#include "sys/compose_speed_spans.h"
#include "sys/recipe_form.h"
#include "sys/compose_sv_graph.h"
#include "sys/compose_quantize_preview.h"
#include "sys/compose_scroll_geometry.h"
#include "sys/stem_waveform_window.h"
#include "ui/compose_mode.h"
#include "ui/compose_timing_geometry.h"
#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/tap_timing_apply.h>
#include <wormedit/core/interaction.h>
#include <wormedit/core/resource_assignment.h>
#include <wormedit/core/chart_clipboard.h>
#include <wormedit/core/player_preview.h>
#include <wormedit/core/sound_slice.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/workspace.h>
#include <wormedit/core/preferences.h>

#if defined(WORMEDIT_HAS_AUDIO_DECODER)
#include <wormedit/audio/waveform.h>
#include <wormedit/audio/aligned_waveform.h>
#include "sys/compose_onset_snap.h"
#endif

#include <array>
#include <cstddef>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace wormedit::sys {

struct compose_bga_move_preview_input_t {
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t document_revision{};
    std::uint64_t event_id{};
    double y_pixels{};
};

// Shared pointer cell resolution for idle paint previews and note placement.
[[nodiscard]] auto resolve_compose_paint_cell(const wormedit::chart_document_t& chart,
                                              const wormedit::editor_hit_test_index_t& index,
                                              const wormedit::editor_interaction_view_model_t& view,
                                              double x, double y, double field_left,
                                              double field_width, bool downscroll,
                                              std::uint64_t snap)
    -> std::optional<wormedit::editor_hit_target_t>;

inline constexpr double compose_measure_rail_width_pixels{wormedit::ui::compose_measure_rail_width};
inline constexpr std::size_t compose_timing_lane_count{8};
inline constexpr double compose_bgm_lane_width_pixels{36.0};
inline constexpr double compose_playable_lane_width_pixels{48.0};
inline constexpr double compose_minimum_playable_field_width_pixels{160.0};
inline constexpr double compose_maximum_playable_field_width_pixels{640.0};
inline constexpr double compose_minimum_bgm_pane_width_pixels{96.0};
inline constexpr std::array<std::uint64_t, 18> chart_snap_preferences{4, 5, 8, 6, 10, 12, 16, 20, 24, 32, 40, 48, 60, 64, 80, 96, 160, 320};

struct bmson_draft_audition_request_t {
    wormedit::chart_id_t chart;
    std::uint64_t revision{};
    wormedit::object_id_t reference;
    std::vector<wormedit::bmson_sound_marker_insertion_t> markers;
    std::size_t marker_index{};
};
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
// A bounded CPU envelope ring. Geometry and GPU textures live in the frontend.
struct envelope_projection_ring_t {
    struct row_t {
        std::int64_t world_row{std::numeric_limits<std::int64_t>::min()};
        WormUiComposeWaveformRowView value{};
        bool present{}, incomplete{};
    };
    std::vector<row_t> rows;
    std::optional<audio::waveform_key_t> source_key, detail_key;
    const void* source_identity{};
    const void* detail_identity{};
    std::uint64_t revision{}, project{}, chart{}, channel{}, source_generation{}, availability_evictions{};
    std::size_t availability_entries{}, availability_frames{};
    double zoom{};
    bool detail_level{}, valid{};
    std::size_t projected_rows{}, reused_rows{}, projection_calls{};
};
#endif

// Owned marker text and absolute rows; viewport placement/selection stay per frame.
struct compose_timing_marker_payload_t {
    std::optional<std::uint64_t> row;
    std::uint64_t id{};
    std::size_t source_order{}, storage_order{};
    WormUiTimingLaneKind lane{};
    std::string short_label, detail;
    bool fixed_initial{};
};
struct compose_timing_payload_cache_t {
    const void* document_identity{};
    const void* chart_identity{};
    std::uint64_t project{}, chart{}, revision{};
    bool valid{};
    std::vector<compose_timing_marker_payload_t> markers;
    std::size_t build_count{};
};

// Consumers retain different geometry contracts. Keep those policies explicit:
// transport uses a logical stamp; stem/paint indices own their own stamps.
enum class compose_scene_key_policy_t { projection, stem_geometry, measure_geometry,
                                        paint_occupancy, transport };

struct compose_scene_cache_t {
    bool is_scene_key_current(const wormedit::song_document_t& document,
        const wormedit::chart_document_t& chart, compose_scene_key_policy_t policy = compose_scene_key_policy_t::projection) const {
        const bool logical_stamp = scene.chart == chart.id && scene.revision == document.revision;
        if (policy == compose_scene_key_policy_t::transport) return logical_stamp;
        if (scene.scene_document_identity != &document || scene.scene_chart_identity != &chart)
            return false;
        switch (policy) {
        case compose_scene_key_policy_t::projection:
            return scene.attempted && logical_stamp && scene.scene_song == document.id;
        case compose_scene_key_policy_t::stem_geometry:
            return stems.stem_index.built && stems.stem_index.document == document.id &&
                stems.stem_index.chart == chart.id && stems.stem_index.revision == document.revision;
        case compose_scene_key_policy_t::measure_geometry:
            return logical_stamp;
        case compose_scene_key_policy_t::paint_occupancy:
            return scene.index && scene.index->chart == chart.id &&
                scene.index->document_revision == document.revision;
        case compose_scene_key_policy_t::transport:
            return logical_stamp;
        }
        return false;
    }
    bool ensure_scene_key(const wormedit::song_document_t& document,
                          const wormedit::chart_document_t& chart);
    void invalidate_scene_cache();
    compose_navigation_travel_t navigation_travel;
    keysound_list_cache_t keysound_list_cache;
    mutable compose_timing_payload_cache_t timing_payload_cache;
    // Internal reference-projection bypass for correctness/performance probes.
    bool timing_payload_cache_enabled{true};
    struct draw_stem_source_cache_t {
        const void* document{};
        const void* chart{};
        std::uint64_t revision{}, build_count{};
        std::unordered_map<std::uint64_t, bool> channel_continues;
        std::unordered_map<std::uint64_t, std::size_t> resource_lanes;
        std::unordered_map<std::uint64_t, std::size_t> explicit_stem_resources;
        std::unordered_map<std::uint64_t, std::size_t> resource_indices;
        std::uint8_t source_kind{};
        std::uint64_t source_id{};
        std::string source_label;
        bool source_label_valid{}, source_missing{};
    };
    struct channel_waveform_selection_t {
        wormedit::chart_id_t chart;
        std::uint64_t revision{};
        wormedit::object_id_t bgm_event;
        wormedit::object_id_t channel;
        wormedit::object_id_t source_lane;
        wormedit::asset_id_t asset;
    };
    struct selected_source_location_t {
        wormedit::object_id_t id;
        bool attempted{};
        std::optional<std::pair<std::size_t, std::size_t>> location;
    };
    struct offset_fit_drag_t {
        wormedit::beat_interval_drag_t beat;
        wormedit::timing_index_t timing;
        wormedit::asset_id_t asset;
        double target_y_pixels{};
    };
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
    struct stem_waveform_cache_t {
        wormedit::object_id_t channel;
        std::uint64_t source_lane{};
        const void* source{};
        std::uint64_t source_generation{};
        std::uint64_t revision{};
        std::optional<wormedit::sound_slice_result_t> slices;
        double scroll{}, height{}, zoom{};
        bool valid{}, pending{};
        std::vector<WormUiComposeWaveformRowView> rows;
        envelope_projection_ring_t ring;
        // The rows split by the lane of the slice they belong to, and the cut lines.
        struct piece_t {
            std::uint64_t lane{};
            bool playable{};
            std::vector<WormUiComposeWaveformRowView> rows;
        };
        std::vector<piece_t> pieces;
        std::vector<WormUiComposeStemCutView> cuts;
        struct point_ref_t {
            std::uint64_t object{}, lane{};
            bool playable{};
        };
        std::unordered_map<std::uint64_t, point_ref_t> refs;
        std::uint64_t refs_revision{};
        bool is_selected{};
    };
    struct bgm_bar_cache_t {
        std::optional<audio::waveform_key_t> key;
        const void* identity{};
        bgm_bar_range_t rows_range;
        std::vector<WormUiComposeWaveformOverviewRowView> rows;
        std::uint64_t marks_revision{};
        std::uint64_t marks_total_frames{};
        bgm_bar_range_t marks_range;
        bool marks_valid{};
        std::vector<WormUiComposeBgmBarMarkView> marks;
    };
#endif
    struct hover_timing_snapshot_t {
        std::int64_t source_frame{};
        std::uint64_t project_id{};
        std::uint64_t chart_id{};
        std::uint64_t document_revision{};
        std::uint64_t audio_asset_id{};
        std::uint64_t total_frames{};
        std::uint32_t sample_rate{};
        bool valid{};
    };

    // Retained chart geometry and reusable projections share the document/chart stamp.
    struct scene_state_t {
        const void* scene_document_identity{};
        const void* scene_chart_identity{};
        wormedit::song_id_t scene_song;
        wormedit::chart_id_t chart;
        std::uint64_t revision{};
        bool attempted{};
        std::optional<wormedit::editor_field_scene_index_t> index;
        wormedit::editor_field_scene_window_t scene_window;
        std::uint64_t static_scene_style_generation{};
        std::shared_ptr<flattened_workspace_t> static_scene_storage;
        std::size_t static_scene_rebuild_count{};
        std::uint64_t static_scene_source_generation{}, static_scene_source_style{};
        std::size_t static_scene_accounted_bytes{};
        std::optional<bool> static_scene_lane_legend;
        std::unordered_map<std::uint64_t, std::uint64_t> bgm_rows_by_id;
        std::vector<double> bgm_lane_widths_pixels;
        std::array<wormedit::common::static_interval_index_t, 5> static_projection_indices;
        std::vector<std::size_t> static_projection_scratch;
        std::uint64_t static_projection_source_generation{};
        std::size_t static_projection_index_bytes{}, static_projection_scanned{},
            static_projection_extracted{}, static_projection_reused_frames{};
        // Two bounded source locations, invalidated with the document/chart scene.
        selected_source_location_t selected_note_source, selected_bgm_source;
        std::size_t selected_source_scanned{}, selected_source_reused{}, paint_occupancy_scanned{};
        std::optional<wormedit::measure_index_t> cached_measure_index;
        std::optional<wormedit::editor_hit_test_index_t> hit_test_index;
        std::shared_ptr<const wormedit::editor_area_geometry_snapshot_t> area_object_geometry;
    } scene;

    struct quantize_state_t {
        quantize_preview_cache_t quantize_preview;
        std::vector<WormUiComposeQuantizePresetView> quantize_preset_views;
        std::vector<WormUiComposeQuantizeReportRowView> quantize_preview_row_views;
        std::vector<WormUiComposeQuantizeGhostView> quantize_ghost_views;
        std::vector<WormUiComposeQuantizeReportRowView> quantize_applied_row_views;
    } quantize;

    struct measure_state_t {
        std::vector<WormUiComposeMeasureRowView> measure_row_views;
        std::vector<std::uint64_t> measure_row_ids;
    } measures;

    struct timing_state_t {
        std::optional<tap_timing_apply_proposal_t> tap_apply_proposal;
        std::optional<tap_timing_apply_proposal_t> tap_preview_binding;
        std::optional<timing_index_t> tap_preview_timing;
        std::optional<wormedit::timing_index_t> marker_timing_index;
        std::optional<dead_span_projection_t> dead_spans;
        std::optional<kiai_projection_t> kiai_spans;
        std::optional<sv_steps_t> sv_steps;
        std::optional<recipe_areas_t> recipe_areas;
        std::optional<speed_spans_t> speed_spans;
        std::optional<wormedit::timing_index_t> proposed_offset_timing_index;
        std::optional<wormedit::signed_rational_t> proposed_offset_seconds;
        std::optional<wormedit::beat_interval_drag_t> beat_fit_drag;
        std::optional<wormedit::asset_id_t> beat_fit_asset;
        bool beat_fit_onset_snapped{};
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        std::shared_ptr<const audio::waveform_analysis_result_t> fit_waveform;
        std::optional<wormedit::asset_id_t> fit_waveform_asset;
        compose_onset_snap_t beat_fit_onsets;
        std::optional<audio::waveform_key_t> beat_fit_onset_source;
#endif
        std::optional<offset_fit_drag_t> offset_fit_drag;
        std::optional<wormedit::timing_index_t> proposed_beat_timing_index;
        std::optional<wormedit::signed_rational_t> proposed_beat_bpm;
        wormedit::object_id_t proposed_beat_event;
    } timing;

    struct stem_display_state_t {
        draw_stem_source_cache_t draw_stem_sources;
        wormedit::sys::stem_index_t stem_index;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        // One entry per BMSON stem drawn besides the selected channel (at most eight; the vector is
        // reserved up front so the view's row pointers stay valid through the frame).
        std::vector<stem_waveform_cache_t> stem_waveforms;
        packed_waveform_index_t packed_waveform_index;
        audio::display_row_table_t packed_waveform_row_table;
        std::uint64_t packed_clock_generation{};
        std::vector<stem_waveform_cache_t> packed_waveforms;
        std::vector<WormUiComposeStemWaveformView> stem_waveform_views;
        std::vector<WormUiComposeStemCutView> stem_cut_views;
        auto stem_for(wormedit::object_id_t channel) -> stem_waveform_cache_t& {
            const auto found = std::ranges::find(stem_waveforms, channel, &stem_waveform_cache_t::channel);
            if (found != stem_waveforms.end()) return *found;
            stem_waveforms.emplace_back();
            stem_waveforms.back().channel = channel;
            return stem_waveforms.back();
        }
        // Packed display keys are BGM event ids, rather than sound-channel ids.
        auto packed_for(wormedit::object_id_t event) -> stem_waveform_cache_t& {
            const auto found = std::ranges::find(packed_waveforms, event, &stem_waveform_cache_t::channel);
            if (found != packed_waveforms.end()) return *found;
            packed_waveforms.emplace_back();
            packed_waveforms.back().channel = event;
            return packed_waveforms.back();
        }
#endif
    } stems;

    struct waveform_state_t {
        std::optional<channel_waveform_selection_t> channel_waveform_selection;
        wormedit::object_id_t channel_waveform_channel;
        const void* channel_waveform_source{};
        std::uint64_t channel_waveform_source_generation{};
        std::optional<wormedit::sound_slice_result_t> channel_waveform_slices;
        double channel_waveform_scroll_pixels{};
        double channel_waveform_height_pixels{};
        double channel_waveform_pixels_per_measure{};
        bool channel_waveform_projection_valid{};
        std::vector<WormUiComposeWaveformRowView> channel_waveform_rows;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        audio::display_row_table_t waveform_row_table;
        envelope_projection_ring_t waveform_envelope_ring;
        envelope_projection_ring_t channel_envelope_ring;
        std::optional<wormedit::audio::waveform_key_t> waveform_overview_key;
        const void* waveform_overview_identity{};
        std::uint64_t waveform_overview_revision{};
        std::uint64_t waveform_overview_generation{};
        std::vector<WormUiComposeWaveformOverviewRowView> waveform_overview_rows;
        // BGM bar rows re-project only when the song window or the source changes; measure
        // marks also follow the document revision (tempo and offset edits move them).
        bgm_bar_cache_t bgm_bar;
#endif
        std::uint64_t waveform_overview_total_frames{};
        std::uint32_t waveform_overview_sample_rate{};
        hover_timing_snapshot_t hover_timing;
    } waveform;

    struct frame_state_t {
        std::optional<wormedit::editor_interaction_view_model_t> interaction_view;
        wormedit::editor_field_scene_request_t preview_scene_request;
        std::optional<wormedit::editor_object_hover_state_t> hover;
        std::optional<std::pair<double, double>> paint_pointer;
        std::optional<wormedit::editor_object_audition_request_t> audition_request;
        std::optional<bmson_draft_audition_request_t> draft_audition_request;
        std::unordered_map<std::uint64_t, std::size_t> semantic_layer_membership_counts;
        std::unordered_map<std::uint64_t, std::size_t> semantic_layer_bgm_membership_counts;
        bool focus_note_label_editor{};
        // The song has audio (decided last frame; the surface height is needed before the
        // waveform).
        bool bgm_bar_audio_seen{};
        bool viewport_autoscroll_active{};
        double viewport_autoscroll_anchor_x_pixels{};
        double viewport_autoscroll_anchor_y_pixels{};
        double viewport_autoscroll_pointer_x_pixels{};
        double viewport_autoscroll_pointer_y_pixels{};
        std::string projection_error;
        wormedit::notification_severity_t projection_error_severity{
            wormedit::notification_severity_t::error};
    } frame;

    void clear_beat_fit_capture() {
        timing.beat_fit_drag.reset();
        timing.beat_fit_asset.reset();
        timing.beat_fit_onset_snapped = false;
#if defined(WORMEDIT_HAS_AUDIO_DECODER)
        timing.beat_fit_onsets = {};
        timing.beat_fit_onset_source.reset();
#endif
    }
};

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
    std::function<bool(std::uint64_t, std::uint64_t, std::uint64_t)> open_sound_picker;
    std::function<bool(std::uint64_t, std::uint64_t, std::uint64_t)> open_project_export;
    std::function<bool(WormUiComposeDockCommand, std::uint64_t, std::uint64_t, std::uint64_t)>
        compose_dock_command;
    std::function<int()> compose_left_dock_pixels;
    std::function<double(std::uint64_t, std::uint64_t, std::uint64_t)> compose_bgm_lane_width;
    std::function<std::string()> transport_play_pause;
    std::function<std::string()> transport_play_from_start;
    std::function<std::string(std::uint64_t)> transport_seek;
    std::function<std::optional<std::int64_t>()> transport_audio_milliseconds;
    std::function<std::string(std::int64_t)> transport_seek_audio_milliseconds;
    std::function<void()> transport_stop;
    std::function<bool(wormedit::chart_id_t, std::uint64_t, std::uint64_t, std::uint64_t, bool)>
        transport_set_loop_selection;
    std::function<bool()> transport_loop_selection_active;
    std::function<bool(wormedit::chart_id_t, std::span<const std::uint64_t>)> transport_set_bgm_mix;
    std::function<std::string(external_player_action_t)> external_player;
    std::function<std::string()> transport_cancel_restore;
};

struct bgm_lane_mix_state_t {
    std::set<std::uint64_t> muted;
    std::set<std::uint64_t> solo;
};
auto compose_bgm_muted_lanes(const wormedit::chart_document_t& chart,
                             const bgm_lane_mix_state_t& mix) -> std::vector<std::uint64_t>;

struct application_command_state_t {
    struct bms_branch_statement_feedback_t {
        wormedit::chart_id_t chart;
        std::uint64_t revision{};
        std::string message;
    };
    struct quantize_editor_t {
        wormedit::song_id_t song;
        wormedit::chart_id_t chart;
        wormedit::quantize_request_t request;
        std::optional<std::size_t> preset_index;
        bool preview_enabled{};
    };
    struct quantize_applied_report_t {
        wormedit::song_id_t song;
        wormedit::chart_id_t chart;
        std::vector<quantize_report_row_t> rows;
        std::size_t total_rows{};
        bool truncated{};
    };
    struct paste_session_t {
        wormedit::chart_clipboard_t clipboard;
        wormedit::local_position_t anchor;
        bool anchor_available{true};
        std::string anchor_error;
        std::optional<wormedit::local_position_t> pointer_cursor;
        wormedit::chart_paste_proposal_t proposal;
        wormedit::chart_document_t geometry;
        std::optional<wormedit::editor_field_scene_index_t> index;
        std::vector<WormUiComposePasteGhostView> ghosts;
    };
    struct bmson_marker_draft_t {
        wormedit::chart_id_t chart;
        std::uint64_t revision{};
        wormedit::object_id_t reference;
        std::vector<wormedit::bmson_sound_marker_insertion_t> markers;
    };
    struct bmson_razor_stroke_t {
        wormedit::chart_id_t chart;
        std::uint64_t revision{};
        wormedit::object_id_t reference;
        std::uint64_t snap_denominator{};
        wormedit::local_position_t last_position;
    };
    struct bga_draw_stroke_t {
        wormedit::chart_id_t chart;
        uint64_t revision{};
        std::optional<wormedit::resource_id_t> resource;
        bool erase{};
        uint64_t last_row{};
        wormedit::measure_index_t geometry;
        std::vector<wormedit::bga_stroke_target_t> targets;
        std::set<std::pair<uint64_t, uint8_t>> visited;
    };
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
    struct preferences_t {
        // Editor placement audition is distinct from Sounds-panel preview-on-click.
        wormedit::editor_preferences_t editor_preferences;
        bool minimap_click_moves_paused_playhead{};
        std::size_t transport_rate_index{static_cast<std::size_t>(wormedit::audio::playback_rate_id_t::normal)};
        bool transport_keep_pitch{};
        bool transport_keep_pitch_available{};
        // Production seek services publish feedback; legacy/test callbacks leave this false.
        bool transport_seek_posts_feedback{};
        bool timing_settings_visible{};
    } preferences;
    // Resolved from display preferences and the field skin, outside the project lifetime.
    wormedit::editor_scratch_presentation_t scratch_on_right;

    struct project_t {
        struct sound_selection_state_t {
            // Fresh per-frame source context, stamped to prevent cross-chart/stale hover reuse.
            std::optional<wormedit::chart_id_t> sound_selection_chart;
            std::uint64_t sound_selection_revision{};
            std::optional<wormedit::object_id_t> sound_selection_hover;
            std::vector<wormedit::resource_id_t> sound_selection_dock;
            // Catalog sound the Compose keysound listbox asked to audition; consumed once per frame.
            std::optional<wormedit::resource_id_t> pending_sound_audition;
        } sound_selection;

        struct feedback_state_t {
            std::optional<bms_branch_statement_feedback_t> bms_branch_statement_feedback;
        } feedback;

        struct clipboard_state_t {
            std::optional<wormedit::chart_clipboard_t> chart_clipboard;
            std::optional<paste_session_t> paste_session;
        } clipboard;

        struct quantize_state_t {
            std::optional<quantize_editor_t> quantize_editor;
            std::optional<quantize_applied_report_t> quantize_applied_report;
        } quantize;

        struct compose_gesture_state_t {
            std::optional<bmson_marker_draft_t> bmson_marker_draft;
            std::optional<bmson_razor_stroke_t> bmson_razor_stroke;
            std::optional<bga_draw_stroke_t> bga_draw_stroke;
            uint8_t draw_source_kind{};
            uint64_t draw_source_id{};
            bool note_paint_mode_active{};
            bool semantic_layer_paint_mode_active{};
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
            std::uint8_t compose_timing_lane_visibility_mask{0xff};
            bool compose_timing_focus{};
            bool edit_snap_enabled{true};
            bool y_lock{false};
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

        struct timing_recipe_state_t {
            bool timing_locked{};
            std::uint64_t kiai_selected_head{};
            std::uint64_t recipe_selected{};
            std::optional<recipe_form_preview_t> recipe_preview;
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

[[nodiscard]] inline auto compose_mode_of(const application_command_state_t& state)
    -> wormedit::ui::compose_mode_t {
    return state.project.compose_view.compose_timing_focus ? wormedit::ui::compose_mode_t::timing_focus
                                                    : wormedit::ui::compose_mode_t::full;
}

// Timing lane mask in force for drawing, hit testing, and geometry.
[[nodiscard]] inline auto effective_compose_timing_lane_mask(
    const application_command_state_t& state) -> std::uint8_t {
    return wormedit::ui::compose_effective_timing_lane_mask(
        compose_mode_of(state), state.project.compose_view.compose_timing_lane_visibility_mask);
}

inline auto toggle_transport_metronome(application_command_state_t& state) -> bool {
    return state.metronome && state.metronome->set_enabled(!state.metronome->settings.enabled);
}
inline void apply_timing_settings_action(application_command_state_t& state,
                                         const WormUiTimingSettingsAction& action) {
    if (action.toggle) state.preferences.timing_settings_visible = !state.preferences.timing_settings_visible;
    if (action.close) state.preferences.timing_settings_visible = false;
    if (!state.metronome) return;
    if (action.change_enable) static_cast<void>(state.metronome->set_enabled(action.enabled));
    if (action.change_volume) static_cast<void>(state.metronome->set_volume(action.volume));
    if (action.change_count_in) static_cast<void>(state.metronome->set_count_in(action.count_in));
}

struct compose_horizontal_layout_t {
    double bga_pane_left_pixels{};
    double field_left_pixels{};
    double playable_field_width_pixels{};
    double bgm_pane_left_pixels{};
    double bgm_pane_width_pixels{};
    double bga_pane_width_pixels{};
};

auto compose_surface_height(int window_height, const WormUiWorkspaceView& view) -> double;
auto compose_mode_family(const wormedit::lane_layout_t& layout) -> wormedit::editor_mode_family_t;
auto compose_field_downscroll(const wormedit::chart_document_t& chart) -> bool;
auto compose_field_downscroll(const wormedit::chart_document_t& chart,
    const wormedit::editor_preferences_t& preferences) -> bool;
auto compose_project_y(long double row, double pixels_per_row, double scroll,
                       double viewport_height, bool downscroll) -> double;
// Bounds source geometry to the canvas left uncovered by frontend docks.
auto compose_editor_layout_width(double viewport_width, double editor_right) -> double;
auto compose_horizontal_layout(double viewport_width_pixels, std::size_t lane_count,
                               bool has_bgm_lanes, std::size_t bga_lane_count,
                               std::uint8_t timing_lane_mask)
    -> compose_horizontal_layout_t;
auto maximum_layout_lane_count(const wormedit::chart_document_t& chart) -> std::size_t;
auto compose_lane_is_editable(const wormedit::lane_definition_t& lane) -> bool;
auto compose_physical_scancode(wormedit::physical_key_t key) -> std::uint32_t;
auto has_beat_marker_navigation(const wormedit::chart_document_t& chart) -> bool;
auto has_audio_marker_navigation(const wormedit::chart_document_t& chart) -> bool;
auto timing_lane_kind_to_ui(wormedit::timing_lane_kind_t lane) -> WormUiTimingLaneKind;
auto requested_snap_denominator(const application_command_state_t& application_state,
                                wormedit::chart_id_t chart) -> std::uint64_t;
auto compose_snap_preferences(const application_command_state_t& state, wormedit::chart_id_t chart)
    -> std::array<std::uint64_t, chart_snap_preferences.size() + 1>;
auto editor_interaction_has_pending_gesture(
    const wormedit::editor_interaction_state_t& state) -> bool;

auto begin_compose_paste(application_command_state_t& application_state,
    const wormedit::song_document_t& document, const wormedit::editor_interaction_state_t& state,
    std::string& status) -> bool;
auto update_compose_paste(application_command_state_t& application_state,
    const wormedit::song_document_t& document, wormedit::chart_id_t chart,
    wormedit::local_position_t anchor) -> bool;
auto commit_compose_paste(application_command_state_t& application_state,
    wormedit::document_history_t& history, wormedit::editor_interaction_state_t& state,
    std::string& status, wormedit::notification_severity_t* severity = nullptr) -> bool;

auto toggle_compose_audio_offset_lock(const WormUiWorkspaceAction& action,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application, compose_scene_cache_t& cache,
    std::optional<wormedit::audio_offset_drag_t>& drag,
    std::optional<wormedit::workspace_tab_t>& drag_tab) -> std::string;

// Projects the audio fallback outcome of missing sounds into the compose view (arena-backed).
void populate_compose_keysound_fallbacks(flattened_workspace_t& flattened,
                                         const wormedit::document_history_t& history,
                                         const std::filesystem::path& asset_base);

// References to the live synchronous projection inputs; never retained by caches.
struct compose_frame_inputs_t {
    flattened_workspace_t& flattened;
    wormedit::document_history_t& history;
    compose_scene_cache_t& cache;
    std::optional<wormedit::editor_interaction_state_t>& retained_state;
    application_command_state_t& application_state;
    const wormedit::timing_lane_pseudonote_preview_t* const& timing_stage;
    const std::span<const wormedit::local_position_t>& comparison_rows;
    const compose_bga_move_preview_input_t* const& bga_move_preview;

    auto view() const -> WormUiComposeView& {
        return flattened.view.compose;
    }
};

// Constness differs between inspection and final interaction publication, not geometry.
template <class View = const wormedit::editor_interaction_view_model_t>
struct compose_chart_scope_t {
    wormedit::chart_scope_t chart_scope;
    View& interaction_view;
    bool downscroll;
    double pixels_per_row;
};
inline auto compose_pixels_per_row(const wormedit::chart_document_t& chart,
                                   const wormedit::editor_interaction_view_model_t& view)
    -> double {
    return view.pixels_per_standard_measure /
           static_cast<double>(chart.divisions_per_standard_measure);
}

void populate_compose_view(flattened_workspace_t& flattened,
                           wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compose_scene_cache_t& cache, int window_width, int window_height,
                           std::optional<wormedit::editor_interaction_state_t>& retained_state,
                           application_command_state_t& application_state,
                           const wormedit::timing_lane_pseudonote_preview_t* timing_stage,
                           std::span<const wormedit::local_position_t> comparison_rows = {},
                           const compose_bga_move_preview_input_t* bga_move_preview = nullptr);

} // namespace wormedit::sys
