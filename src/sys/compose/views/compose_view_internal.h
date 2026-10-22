#pragma once
#include <wormedit/core/keyed_cache.h>
#include "sys/compose/actions/compose_area_actions.h"
#include "sys/compose/views/compose_draw_source_view.h"
#include "sys/compose/actions/compose_note_to_bgm.h"
#include "sys/compose/views/compose_resource_presenter.h"
#include "sys/compose/compose_sound_selection.h"
#include "sys/compose/views/compose_projection_context.h"
#include "sys/app/frame_timing.h"
#include "sys/compose/views/recipe_form.h"
#include "ui/nuklear/render/arrow_glyph.h"
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/sv_recipe.h>

#include <wormedit/core/editor_scene.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace wormedit::sys {
namespace compose_projection_detail {}
using namespace compose_projection_detail;
} // namespace wormedit::sys

namespace wormedit::sys::compose_projection_detail {
void report_compose_projection(
    frontend_workspace_view_t& flattened, projection_feedback_kind_t producer,
    wormedit::notification_severity_t severity, std::string message, std::uint64_t object = 0,
    wormedit::notification_source_t source = wormedit::notification_source_t::document);

void report_compose_diagnostic(frontend_workspace_view_t& flattened,
                               projection_feedback_kind_t producer,
                               const wormedit::diagnostic_list_t& diagnostics,
                               const char* fallback);

auto paste_anchor(application_command_state_t::paste_session_t& session,
                  wormedit::local_position_t cursor) -> wormedit::local_position_t;

void block_paste_heads_in_warps(const wormedit::chart_document_t& chart,
                                wormedit::chart_paste_outcome_t& proposal);

auto paste_geometry(const wormedit::chart_document_t& source,
                    const wormedit::chart_paste_report_t& report) -> wormedit::chart_document_t;

auto known_mania_key_profile(std::string_view id) -> bool;

} // namespace wormedit::sys::compose_projection_detail

namespace wormedit::sys {
const std::vector<wormedit::lane_input_profile_t>& cached_lane_profile_choices(std::size_t lanes);

const std::vector<wormedit::lane_input_profile_t>& cached_initial_lane_profile_choices();

void populate_lane_profile_choices(wormedit::ui::compose_view_t& view, const wormedit::chart_document_t& chart,
                                   wormedit::local_position_t cursor);

auto compose_mode_family(const wormedit::lane_layout_t& layout) -> wormedit::editor_mode_family_t;

void append_bms_layout_profiles(std::vector<wormedit::lane_input_profile_t>& choices);

void append_stepmania_layout_profiles(std::vector<wormedit::lane_input_profile_t>& choices);

void append_osu_layout_profiles(std::vector<wormedit::lane_input_profile_t>& choices);

void append_mode_layout_profiles(std::vector<wormedit::lane_input_profile_t>& choices,
                                 wormedit::editor_mode_family_t mode);

auto make_lane_layout_profile_choices(wormedit::format_family_t family,
                                      wormedit::editor_mode_family_t mode)
    -> std::vector<wormedit::lane_input_profile_t>;

auto cached_lane_layout_profile_choices(wormedit::format_family_t family,
                                        wormedit::editor_mode_family_t mode)
    -> const std::vector<wormedit::lane_input_profile_t>&;

using measure_removal_cache_t = wormedit::keyed_cache_t<
    std::pair<wormedit::chart_stamp_t, wormedit::measure_id_t>, wormedit::measure_removal_proposal_t>;

auto cached_measure_removal(const wormedit::song_document_t& song,
                            const wormedit::chart_document_t& chart, wormedit::measure_id_t measure)
    -> const wormedit::measure_removal_proposal_t&;

void populate_measure_removal(wormedit::ui::compose_view_t& view, const wormedit::song_document_t& song,
                              const wormedit::chart_document_t& chart,
                              wormedit::local_position_t cursor, bool virtual_cursor);

void append_lane_layout_profile_choice(wormedit::ui::compose_view_t& view,
                                       const wormedit::lane_layout_t& layout,
                                       const wormedit::lane_layout_segment_t* active);

void populate_lane_layout_profile_choices(wormedit::ui::compose_view_t& view,
                                          const wormedit::chart_document_t& chart,
                                          const wormedit::lane_layout_segment_t* active);

void populate_lane_layout_editor(wormedit::ui::compose_view_t& view, const wormedit::chart_document_t& chart,
                                 const wormedit::measure_index_t& measures,
                                 wormedit::local_position_t cursor, bool virtual_cursor,
                                 std::optional<std::size_t> selected_canvas_measure);

void populate_lane_layout_segments(frontend_workspace_view_t& flattened,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::measure_index_t& measures,
                                   const wormedit::editor_interaction_view_model_t& interaction,
                                   bool downscroll, wormedit::ui::compose_view_t& view);

} // namespace wormedit::sys

namespace wormedit::sys::compose_projection_detail {
struct compose_scene_projection_context_t {
    const compose_projection_context_t<>& inputs;
    const wormedit::editor_field_scene_index_t& index;
    const wormedit::editor_field_scene_view_t& scene;
    double field_left_pixels;
    double playable_field_width;
    const compose_bga_move_preview_input_t* bga_move_preview{};
    wormedit::snap_grid_t bga_move_snap;
    auto history() const -> const wormedit::document_history_t& {
        return inputs.history();
    }
};

void populate_compose_sound_labels(const compose_scene_projection_context_t& projection);

auto prepare_compose_grid_rows(frontend_workspace_view_t& flattened, std::size_t count) -> bool;

auto append_compose_grid_row(frontend_workspace_view_t& flattened,
                             const wormedit::ui::compose_grid_line_view_t& line,
                             const wormedit::exact_chart_row_t& row) -> bool;

// Where a row sits in the active snap space's measure grid, for snap colours.
auto compose_grid_phase(const compose_scene_projection_context_t& projection, std::uint64_t row)
    -> wormedit::grid_phase_t;

void build_compose_grid_lines_view(const compose_scene_projection_context_t& projection);

auto compose_note_display_lane_number(const wormedit::editor_field_scene_view_t& scene,
                                      const auto& endpoint) -> std::uint32_t {
    const auto x = (endpoint.bounds.left + endpoint.bounds.right) * 0.5;
    const auto y = (endpoint.bounds.top + endpoint.bounds.bottom) * 0.5;
    for (const auto& lane : scene.lanes) {
        if (lane.lane == endpoint.lane && x >= lane.bounds.left && x < lane.bounds.right &&
            y >= lane.bounds.top && y < lane.bounds.bottom) {
            return lane.display_order + 1U;
        }
    }
    return 0;
}

auto compose_note_key_color(const wormedit::editor_field_scene_view_t& scene, const auto& endpoint)
    -> wormedit::lane_key_color_t {
    for (const auto& lane : scene.lanes) {
        if (lane.lane == endpoint.lane)
            return lane.key_color;
    }
    return wormedit::lane_key_color_t::generic;
}

auto compose_endpoint_snap_class(const compose_scene_projection_context_t& projection,
                                 const auto& endpoint) -> std::uint8_t {
    const auto found = projection.index.note_indices_by_id.find(endpoint.note.value);
    if (found == projection.index.note_indices_by_id.end())
        return 0;
    const auto& source = projection.index.notes[found->second];
    bool tail = false;
    if constexpr (requires { endpoint.tail; })
        tail = endpoint.tail;
    return wormedit::ui::note_snap_class(
        compose_grid_phase(projection, tail ? source.end_row : source.begin_row));
}

template <class T>
auto prepare_compose_geometry_rows(frontend_workspace_view_t&, std::vector<T>& rows,
                                   std::size_t count) -> bool {
    rows.clear();
    rows.reserve(count);
    return true;
}

template <class T>
void append_compose_geometry_row(std::vector<T>& rows, const T& row, bool&) {
    rows.push_back(row);
}



void build_compose_scene_objects_view(const compose_scene_projection_context_t& projection);

void build_compose_layout_and_notes_view(const compose_scene_projection_context_t& projection);

void build_compose_bgm_events_view(
    frontend_workspace_view_t& flattened,
    const std::vector<wormedit::editor_field_bgm_event_geometry_t>& events,
    wormedit::ui::compose_view_t& view);

auto compose_stem_filename(const compose_scene_projection_context_t& projection,
                           const wormedit::bgm_source_lane_t& lane) -> std::string_view;

void build_compose_bgm_lane_modes_view(const compose_scene_projection_context_t& projection,
                                    const compose_scene_cache_t& cache);

void build_compose_bgm_view(const compose_scene_projection_context_t& projection,
                         const compose_scene_cache_t& cache);

auto bga_preview_authored_row(const compose_scene_projection_context_t& projection, double source_y)
    -> std::optional<std::uint64_t>;

auto bga_preview_virtual_row(const compose_scene_projection_context_t& projection, double source_y)
    -> std::optional<std::uint64_t>;

auto bga_preview_target_row(const compose_scene_projection_context_t& projection,
                            const compose_bga_move_preview_input_t& input)
    -> std::optional<std::uint64_t>;

void build_compose_bga_move_preview_view(const compose_scene_projection_context_t& projection);

void build_compose_bga_view(const compose_scene_projection_context_t& projection);

void build_compose_draft_marker_view(const compose_scene_projection_context_t& projection,
                                  const auto& marker,
                                  const wormedit::editor_field_rect_t* bgm_bounds) {
    const auto& scene = projection.scene;
    const auto& view = projection.inputs.frame.view();
    const auto& interaction_view = projection.inputs.scope.interaction_view;
    const auto row = (projection.index.measures).row_of(marker.position);
    if (!row)
        return;
    const auto y = compose_project_y(*row, projection.inputs.scope.pixels_per_row,
                                     interaction_view.viewport_scroll_pixels,
                                     view.viewport_height_pixels, projection.inputs.scope.downscroll);
    if (y < -8.0 || y > view.viewport_height_pixels + 8.0)
        return;
    const wormedit::editor_field_rect_t* bounds = bgm_bounds;
    if (marker.playable_lane) {
        const auto playable = std::ranges::find_if(scene.lanes, [&](const auto& candidate) {
            return candidate.lane == *marker.playable_lane && y >= candidate.bounds.top - 8.0 &&
                   y <= candidate.bounds.bottom + 8.0;
        });
        bounds = playable == scene.lanes.end() ? nullptr : &playable->bounds;
    }
    if (!bounds)
        return;
    projection.inputs.frame.flattened.view.compose.bmson_marker_drafts.push_back(
        {.bounds = {bounds->left, y - 8.0, bounds->right, y + 8.0},
         .restart = marker.restart,
         .playable = marker.playable_lane.has_value()});
}

void build_compose_marker_draft_view(const compose_scene_projection_context_t& projection);

void build_compose_marker_previews_view(const compose_scene_projection_context_t& projection);


bool initialize_compose_view_state(
    frontend_workspace_view_t& flattened, compose_scene_cache_t& cache, int window_width,
    int window_height, bool default_bgm_bar_hidden,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    const application_command_state_t& application_state);

void populate_semantic_layer_target_state(wormedit::ui::compose_view_t& view,
                                          const application_command_state_t& application_state,
                                          const auto& layer, bool selected) {
    view.semantic_layer_target_locked =
        view.semantic_layer_target_locked || (selected && layer.locked);
    view.semantic_layer_target_visible =
        view.semantic_layer_target_visible || (selected && layer.visible);
    view.semantic_layer_target_required_filter =
        view.semantic_layer_target_required_filter ||
        (selected &&
         std::ranges::find(application_state.project.semantic_layer.required_semantic_layer_filters, layer.id) !=
             application_state.project.semantic_layer.required_semantic_layer_filters.end());
    view.semantic_layer_target_excluded_filter =
        view.semantic_layer_target_excluded_filter ||
        (selected &&
         std::ranges::find(application_state.project.semantic_layer.excluded_semantic_layer_filters, layer.id) !=
             application_state.project.semantic_layer.excluded_semantic_layer_filters.end());
}

auto layer_assist_enabled(const application_command_state_t& app, wormedit::chart_id_t chart,
                          wormedit::layer_id_t layer) -> bool;


void populate_semantic_membership_counts(compose_scene_cache_t& cache,
                                         const wormedit::chart_document_t& chart);

void populate_note_layer_menu(frontend_workspace_view_t& flattened,
                              const wormedit::chart_document_t& chart, std::uint64_t revision,
                              application_command_state_t& app);

bool populate_semantic_layer_palette(frontend_workspace_view_t& flattened,
                                     const wormedit::chart_document_t& chart,
                                     compose_scene_cache_t& cache,
                                     const wormedit::document_history_t& history,
                                     application_command_state_t& application_state);

template <class T> struct compose_marker_storage_t {
    frontend_workspace_view_t& flattened;
    std::vector<T>& rows;
    std::size_t next{};
    ~compose_marker_storage_t() { rows.resize(next); }
};

template <class T>
void prepare_compose_marker_storage(compose_marker_storage_t<T>& storage, std::size_t candidates) {
    storage.rows.resize(candidates);
}

template <class T>
void append_compose_marker(compose_marker_storage_t<T>& storage, std::string_view label, T value) {
    auto& target = storage.rows[storage.next++];
    value.label = std::move(target.label);
    value.label.assign(label);
    target = std::move(value);
}

auto populate_compose_audio_bookmark(
    compose_marker_storage_t<wormedit::ui::compose_audio_bookmark_view_t>& storage,
    const wormedit::bookmark_t& bookmark, wormedit::ui::compose_view_t& view) -> bool;

auto populate_compose_beat_bookmark(compose_marker_storage_t<wormedit::ui::compose_bookmark_view_t>& storage,
                                    const wormedit::bookmark_t& bookmark,
                                    const wormedit::measure_index_t& measures,
                                    wormedit::ui::compose_view_t& view) -> bool;

bool populate_compose_bookmark_storage(frontend_workspace_view_t& flattened,
                                       const wormedit::chart_document_t& chart,
                                       const wormedit::measure_index_t& measures,
                                       wormedit::ui::compose_view_t& view);

auto populate_compose_audio_section(
    compose_marker_storage_t<wormedit::ui::compose_audio_section_view_t>& storage,
    const wormedit::section_t& section, wormedit::ui::compose_view_t& view) -> bool;

auto populate_compose_beat_section(compose_marker_storage_t<wormedit::ui::compose_section_view_t>& storage,
                                   const wormedit::section_t& section,
                                   const wormedit::measure_index_t& measures,
                                   wormedit::ui::compose_view_t& view) -> bool;

bool populate_compose_section_storage(frontend_workspace_view_t& flattened,
                                      const wormedit::chart_document_t& chart,
                                      const wormedit::measure_index_t& measures,
                                      wormedit::ui::compose_view_t& view);

bool populate_compose_bookmark_and_section_storage(frontend_workspace_view_t& flattened,
                                                   const wormedit::chart_document_t& chart,
                                                   const wormedit::measure_index_t& measures,
                                                   wormedit::ui::compose_view_t& view);

auto compose_timing_short_number(wormedit::signed_rational_t value) -> std::string;

auto compose_timing_exact_number(wormedit::signed_rational_t value) -> std::string;

auto compose_timing_decimal_detail(wormedit::signed_rational_t value) -> std::string;

auto compose_speed_transition_detail(const wormedit::speed_event_t& event) -> std::string;

auto compose_pause_unit_label(wormedit::pause_unit_t unit) -> std::string_view;

auto compose_initial_bpm(const wormedit::chart_document_t& chart,
                         const wormedit::measure_index_t& measures) -> const wormedit::bpm_event_t*;

template <typename AppendMarker>
void append_compose_initial_bpm(const wormedit::chart_document_t& chart,
                                const wormedit::bpm_event_t* initial, AppendMarker&& append) {
    if (initial) {
        const auto index = static_cast<std::size_t>(initial - chart.bpms.data());
        append(initial->position, initial->id.value, initial->provenance.source_order, index,
               wormedit::timing_lane_kind_t::bpm, compose_timing_short_number(initial->beats_per_minute),
               "BPM " + compose_timing_decimal_detail(initial->beats_per_minute), true);
    } else if (!chart.measures.empty()) {
        append(wormedit::local_position_t{chart.measures.front().id, 0}, 0, 0, 0,
               wormedit::timing_lane_kind_t::bpm, "?", "Initial BPM missing", true);
    }
}

template <typename AppendMarker>
void append_compose_chart_timing_markers(const wormedit::chart_document_t& chart,
                                         const wormedit::measure_index_t& measures,
                                         AppendMarker&& append) {
    const auto* initial_bpm = compose_initial_bpm(chart, measures);
    append_compose_initial_bpm(chart, initial_bpm, append);
    for (std::size_t index = 0; index < chart.bpms.size(); ++index) {
        if (&chart.bpms[index] == initial_bpm)
            continue;
        append(chart.bpms[index].position, chart.bpms[index].id.value,
               chart.bpms[index].provenance.source_order, index, wormedit::timing_lane_kind_t::bpm,
               compose_timing_short_number(chart.bpms[index].beats_per_minute),
               "BPM " + compose_timing_decimal_detail(chart.bpms[index].beats_per_minute), false);
    }
    for (std::size_t index = 0; index < chart.scrolls.size(); ++index)
        append(chart.scrolls[index].position, chart.scrolls[index].id.value,
               chart.scrolls[index].provenance.source_order, index, wormedit::timing_lane_kind_t::scroll,
               compose_timing_short_number(chart.scrolls[index].multiplier),
               "Scroll " + compose_timing_decimal_detail(chart.scrolls[index].multiplier), false);
    for (std::size_t index = 0; index < chart.speeds.size(); ++index)
        append(chart.speeds[index].position, chart.speeds[index].id.value,
               chart.speeds[index].provenance.source_order, index, wormedit::timing_lane_kind_t::speed,
               compose_timing_short_number(chart.speeds[index].multiplier),
               "Speed " + compose_timing_decimal_detail(chart.speeds[index].multiplier) +
                   compose_speed_transition_detail(chart.speeds[index]),
               false);
    for (std::size_t index = 0; index < chart.pauses.size(); ++index)
        append(chart.pauses[index].position, chart.pauses[index].id.value,
               chart.pauses[index].provenance.source_order, index,
               chart.pauses[index].kind == wormedit::pause_kind_t::stop ? wormedit::timing_lane_kind_t::stop
                                                                        : wormedit::timing_lane_kind_t::delay,
               compose_timing_short_number(chart.pauses[index].duration),
               std::string(chart.pauses[index].kind == wormedit::pause_kind_t::stop ? "Stop "
                                                                                    : "Delay ") +
                   compose_timing_decimal_detail(chart.pauses[index].duration) + " " +
                   std::string(compose_pause_unit_label(chart.pauses[index].source_unit)),
               false);
    for (std::size_t index = 0; index < chart.warps.size(); ++index) {
        const auto begin = (measures).row_of(chart.warps[index].span.begin);
        const auto end = (measures).row_of(chart.warps[index].span.end);
        const auto rows = begin && end && *end >= *begin ? *end - *begin : 0;
        append(chart.warps[index].span.begin, chart.warps[index].id.value,
               chart.warps[index].provenance.source_order, index, wormedit::timing_lane_kind_t::warp,
               std::to_string(rows), "Warp " + std::to_string(rows) + " rows", false);
    }
    // Kiai events are not markers: Compose draws each merged Kiai area instead (see
    // populate_compose_kiai_and_sv), and edits it as one area.
    for (std::size_t index = 0; index < chart.timing_anchors.size(); ++index)
        append(chart.timing_anchors[index].position, chart.timing_anchors[index].id.value, index,
               index, wormedit::timing_lane_kind_t::anchor,
               std::to_string(chart.timing_anchors[index].source_sample_frame),
               "Anchor at sample frame " +
                   std::to_string(chart.timing_anchors[index].source_sample_frame) +
                   (chart.timing_anchors[index].locked ? " (locked)" : ""),
               false);
}

void assign_compose_timing_marker_fractions(wormedit::ui::compose_view_t& view);

auto compose_timing_marker_selected(const wormedit::editor_interaction_view_model_t& interaction,
                                    wormedit::timing_lane_kind_t lane, std::uint64_t id) -> bool;

auto compose_bga_event_selected(const wormedit::editor_interaction_view_model_t& interaction,
                                std::uint64_t id) -> bool;


void build_compose_beat_markers_view(const compose_projection_context_t<>& input);

std::optional<double>
build_compose_audio_marker_y_view(const compose_projection_context_t<>& input,
                               const auto& timing_index, std::int64_t milliseconds) {
    const auto projected = timing_index.query_position(wormedit::signed_rational_t{milliseconds, 1'000});
    if (!projected || projected->value.denominator == 0)
        return std::nullopt;
    const auto row = wormedit::to_long_double(projected->value);
    const auto pixels_per_row =
        static_cast<long double>(input.scope.interaction_view.pixels_per_standard_measure) /
        static_cast<long double>(input.scope.chart_scope.chart.divisions_per_standard_measure);
    const auto y =
        compose_project_y(row, static_cast<double>(pixels_per_row),
                          input.scope.interaction_view.viewport_scroll_pixels,
                          input.frame.view().viewport_height_pixels, input.scope.downscroll);
    if (!std::isfinite(y) || y < static_cast<long double>(std::numeric_limits<double>::lowest()) ||
        y > static_cast<long double>(std::numeric_limits<double>::max()))
        return std::nullopt;
    return static_cast<double>(y);
}

bool compose_audio_marker_y_visible(double y, const wormedit::ui::compose_view_t& view);

void build_compose_audio_bookmarks_view(const compose_projection_context_t<>& input,
                                     const auto& timing_index) {
    for (std::size_t index = 0; index < input.frame.view().audio_bookmarks.size(); ++index) {
        auto& bookmark = input.frame.flattened.view.compose.audio_bookmarks[index];
        const auto y =
            build_compose_audio_marker_y_view(input, timing_index, bookmark.audio_milliseconds);
        if (y && compose_audio_marker_y_visible(*y, input.frame.view())) {
            bookmark.projection_visible = true;
            bookmark.projected_y_pixels = *y;
        }
    }
}

void build_compose_audio_sections_view(const compose_projection_context_t<>& input,
                                    const auto& timing_index) {
    auto& view = input.frame.view();
    for (std::size_t index = 0; index < view.audio_sections.size(); ++index) {
        auto& section = input.frame.flattened.view.compose.audio_sections[index];
        const auto begin =
            build_compose_audio_marker_y_view(input, timing_index, section.begin_audio_milliseconds);
        const auto end =
            build_compose_audio_marker_y_view(input, timing_index, section.end_audio_milliseconds);
        if (!begin || !end)
            continue;
        section.begin_projection_visible = compose_audio_marker_y_visible(*begin, view);
        section.end_projection_visible = compose_audio_marker_y_visible(*end, view);
        if (section.begin_projection_visible)
            section.projected_begin_y_pixels = *begin;
        if (section.end_projection_visible)
            section.projected_end_y_pixels = *end;
        const auto top = std::min(*begin, *end);
        const auto bottom = std::max(*begin, *end);
        if (bottom >= 0.0 && top <= view.viewport_height_pixels) {
            section.span_projection_visible = true;
            section.projected_span_top_y_pixels = std::clamp(top, 0.0, view.viewport_height_pixels);
            section.projected_span_bottom_y_pixels =
                std::clamp(bottom, 0.0, view.viewport_height_pixels);
        }
    }
}

void build_compose_audio_markers_view(const compose_projection_context_t<>& input);

void build_compose_markers_view(
    const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& source);

auto cached_compose_timing_payloads(const wormedit::song_document_t& document,
                                    const wormedit::chart_document_t& chart,
                                    const compose_scene_cache_t& cache)
    -> const std::vector<compose_timing_marker_payload_t>&;

void populate_compose_timing_markers(
    frontend_workspace_view_t& flattened, const wormedit::song_document_t& document,
    const wormedit::chart_document_t& chart, const compose_scene_cache_t& cache,
    const wormedit::editor_interaction_view_model_t& interaction_view, wormedit::ui::compose_view_t& view,
    bool downscroll, double pixels_per_row, std::uint64_t canvas_rows,
    const wormedit::timing_lane_pseudonote_outcome_t* timing_stage);

void assign_dead_span_label(std::string& label, const wormedit::sys::dead_span_band_t& band);

void populate_compose_dead_spans(frontend_workspace_view_t& flattened,
                                 const compose_scene_cache_t& cache,
                                 const wormedit::editor_interaction_view_model_t& interaction_view,
                                 wormedit::ui::compose_view_t& view, bool downscroll, double pixels_per_row);

void populate_compose_kiai_and_sv(frontend_workspace_view_t& flattened,
                                  const compose_scene_cache_t& cache,
                                  const wormedit::editor_interaction_view_model_t& interaction_view,
                                  wormedit::ui::compose_view_t& view, bool downscroll, double pixels_per_row,
                                  std::uint64_t total_rows, std::uint64_t kiai_selected_head);


void populate_compose_recipe_areas(
    frontend_workspace_view_t& flattened, const wormedit::chart_document_t& chart,
    const compose_scene_cache_t& cache,
    const wormedit::editor_interaction_view_model_t& interaction_view, wormedit::ui::compose_view_t& view,
    bool downscroll, double pixels_per_row, std::uint64_t total_rows, std::uint64_t selected_recipe,
    const recipe_form_outcome_t* preview);

void populate_compose_speed_spans(frontend_workspace_view_t& flattened,
                                  const compose_scene_cache_t& cache,
                                  const wormedit::editor_interaction_view_model_t& interaction_view,
                                  wormedit::ui::compose_view_t& view, bool downscroll, double pixels_per_row);

void drop_stale_recipe_preview(application_command_state_t& application_state,
                               const wormedit::chart_document_t& chart, std::uint64_t revision);

void populate_compose_recipe_form(frontend_workspace_view_t& flattened,
                                  const wormedit::chart_document_t& chart,
                                  const application_command_state_t& application_state,
                                  wormedit::ui::compose_view_t& view);

auto converted_bgm_preview_count(const wormedit::editor_interaction_state_t& state) -> std::size_t;

void populate_compose_selection_preview(
    const compose_scene_cache_t& cache,
    std::optional<wormedit::editor_interaction_state_t>& retained_state,
    wormedit::editor_interaction_view_model_t& interaction_view, wormedit::ui::compose_view_t& view);

auto quantize_report_row_view(const quantize_report_row_t& row)
    -> wormedit::ui::compose_quantize_report_row_view_t;

auto active_quantize_request(application_command_state_t& application,
                             const wormedit::song_document_t& document,
                             const wormedit::chart_document_t& chart, wormedit::snap_grid_t active_snap)
    -> wormedit::quantize_request_t;

void populate_quantize_presets(compose_scene_cache_t& cache,
                               const wormedit::song_document_t& document, wormedit::ui::compose_view_t& view);


void populate_measure_rows(compose_scene_cache_t& cache, const wormedit::chart_document_t& chart,
                           wormedit::ui::compose_view_t& view);

void populate_quantize_applied_report(compose_scene_cache_t& cache,
                                      const wormedit::song_document_t& document,
                                      const wormedit::chart_document_t& chart,
                                      const application_command_state_t& application,
                                      wormedit::ui::compose_view_t& view);

void populate_quantize_preview(compose_scene_cache_t& cache,
                               const wormedit::song_document_t& document,
                               const wormedit::chart_document_t& chart, std::uint64_t revision,
                               const wormedit::editor_interaction_state_t& state,
                               application_command_state_t& application, wormedit::ui::compose_view_t& view);

using quantize_lane_order_map_t =
    std::unordered_map<const wormedit::editor_field_scene_layout_source_t*,
                       std::vector<const wormedit::lane_definition_t*>>;

struct quantize_projection_scope_t {
    const wormedit::editor_field_scene_index_t& index;
    const wormedit::editor_field_scene_request_t& request;
    quantize_lane_order_map_t& lane_orders;
    const std::uint64_t& scale;
    const double& pixels_per_row;
    const double& scroll;
    const double& height;
    const bool& downscroll;
};

struct quantize_lane_geometry_t {
    double left{}, right{};
    wormedit::lane_glyph_t glyph{wormedit::lane_glyph_t::bar};
};

auto quantize_lane_geometry(const wormedit::editor_field_scene_layout_source_t& layout,
                            wormedit::lane_id_t lane_id,
                            const wormedit::editor_field_scene_request_t& request,
                            quantize_lane_order_map_t& lane_orders)
    -> std::optional<quantize_lane_geometry_t>;

auto quantize_layout_at(const wormedit::editor_field_scene_index_t& index, std::uint64_t row)
    -> const wormedit::editor_field_scene_layout_source_t*;

auto quantize_endpoint_bounds(const quantize_lane_geometry_t& lane, double y, double height,
                              wormedit::editor_field_scroll_direction_t direction)
    -> wormedit::ui::compose_rect_t;

auto quantize_row_visible(const auto& note, const wormedit::editor_field_scene_request_t& request)
    -> bool {
    const auto has_layer = [&](wormedit::layer_id_t layer) {
        return std::ranges::find(note.semantic_layers, layer) != note.semantic_layers.end();
    };
    return (!request.solo_semantic_layer || has_layer(*request.solo_semantic_layer)) &&
           std::ranges::all_of(request.required_semantic_layers, has_layer) &&
           !std::ranges::any_of(request.excluded_semantic_layers, has_layer);
}

auto quantize_endpoint_anchor(const wormedit::ui::compose_rect_t& bounds, wormedit::lane_glyph_t glyph,
                              wormedit::editor_field_scroll_direction_t direction)
    -> std::pair<double, double>;

struct quantize_endpoint_projection_t {
    wormedit::ui::compose_rect_t bounds;
    wormedit::lane_glyph_t glyph{};
    double anchor_x{}, anchor_y{};
};

auto build_quantize_endpoint_view(const quantize_projection_scope_t& scope,
                               wormedit::lane_id_t lane_id, std::uint64_t absolute_row,
                               bool promoted) -> std::optional<quantize_endpoint_projection_t>;

auto make_quantize_ghost_geometry(const quantize_projection_scope_t& scope,
                                  const wormedit::ui::compose_quantize_report_row_view_t& report,
                                  const wormedit::editor_field_scene_note_source_t& note)
    -> std::optional<wormedit::ui::compose_quantize_ghost_view_t>;

auto add_quantize_tail_geometry(const quantize_projection_scope_t& scope,
                                wormedit::ui::compose_quantize_ghost_view_t& ghost,
                                const wormedit::ui::compose_quantize_report_row_view_t& report) -> bool;

auto bgm_quantize_ghost(const wormedit::ui::compose_quantize_report_row_view_t& report,
                        const wormedit::chart_document_t& chart,
                        const wormedit::editor_field_scene_request_t& request, std::uint64_t scale,
                        double pixels_per_row, double scroll, double height, bool downscroll)
    -> std::optional<wormedit::ui::compose_quantize_ghost_view_t>;

void append_bgm_quantize_ghost(compose_scene_cache_t& cache,
                               const wormedit::ui::compose_quantize_report_row_view_t& report,
                               const wormedit::chart_document_t& chart,
                               const wormedit::editor_field_scene_request_t& request,
                               std::uint64_t scale, double pixels_per_row, double scroll,
                               double height, bool downscroll);

void populate_quantize_ghost_geometry(compose_scene_cache_t& cache,
                                      const wormedit::song_document_t& document,
                                      const wormedit::chart_document_t& chart,
                                      application_command_state_t& application,
                                      const wormedit::editor_interaction_state_t& state,
                                      const wormedit::editor_interaction_view_model_t& interaction,
                                      wormedit::ui::compose_view_t& view, double pixels_per_row,
                                      bool downscroll);

void populate_compose_staged_draft(application_command_state_t& application_state,
                                   wormedit::ui::compose_view_t& view);

void populate_compose_selected_bgm(
    const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    wormedit::ui::compose_view_t& view, compose_scene_cache_t& cache);

void populate_compose_selected_note(
    frontend_workspace_view_t& flattened, const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    wormedit::ui::compose_view_t& view, compose_scene_cache_t& cache);

void populate_compose_selected_objects_and_drafts(
    frontend_workspace_view_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::document_history_t& history, application_command_state_t& application_state,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    wormedit::ui::compose_view_t& view, compose_scene_cache_t& cache);


template <typename Event>
auto is_selected_source_location_current(const wormedit::chart_document_t& chart,
    wormedit::object_id_t id, std::vector<Event> wormedit::measure_chunk_t::* container,
    const std::optional<std::pair<std::size_t, std::size_t>>& location) -> bool {
    if (!location) return true;
    const auto [group, item] = *location;
    if (group >= chart.measures.size()) return false;
    const auto& events = chart.measures[group].*container;
    return item < events.size() && events[item].id == id;
}

template <typename Event>
auto find_selected_source_location(const wormedit::chart_document_t& chart,
    wormedit::object_id_t id, std::vector<Event> wormedit::measure_chunk_t::* container,
    compose_scene_cache_t& cache) -> std::optional<std::pair<std::size_t, std::size_t>> {
    for (std::size_t group = 0; group < chart.measures.size(); ++group) {
        const auto& events = chart.measures[group].*container;
        for (std::size_t item = 0; item < events.size(); ++item) {
            ++cache.scene.selected_source_scanned;
            if (events[item].id == id) return std::pair{group, item};
        }
    }
    return std::nullopt;
}

template <typename Event>
auto cached_selected_source(const wormedit::chart_document_t& chart, wormedit::object_id_t id,
                            compose_scene_cache_t::selected_source_location_t& slot,
                            std::vector<Event> wormedit::measure_chunk_t::* container,
                            compose_scene_cache_t& cache) -> const Event* {
    const auto builds = slot.build_count();
    const auto& location = slot.get_or_build(id,
        std::bind_front(find_selected_source_location<Event>, std::cref(chart), id, container, std::ref(cache)),
        std::bind_front(is_selected_source_location_current<Event>, std::cref(chart), id, container));
    if (!location) return nullptr;
    if (slot.build_count() == builds) ++cache.scene.selected_source_reused;
    const auto [group, item] = *location;
    return &(chart.measures[group].*container)[item];
}

void populate_compose_selected_bgm(
    const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    wormedit::ui::compose_view_t& view, compose_scene_cache_t& cache);

void populate_compose_selected_note(
    frontend_workspace_view_t& flattened, const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    wormedit::ui::compose_view_t& view, compose_scene_cache_t& cache);

auto cached_compose_measures(frontend_workspace_view_t& flattened,
                             const wormedit::chart_document_t& chart,
                             const wormedit::document_history_t& history,
                             compose_scene_cache_t& cache)
    -> const wormedit::measure_index_t*;

void discard_compose_static_source(compose_scene_cache_t& cache);

const wormedit::measure_index_t*
prepare_compose_chart_view(frontend_workspace_view_t& flattened,
                           const wormedit::chart_document_t& chart,
                           wormedit::document_history_t& history, compose_scene_cache_t& cache,
                           application_command_state_t& application_state);

struct compose_interaction_population_context_t {
    compose_frame_inputs_t& frame;
    const wormedit::chart_scope_t& scope;
    bool downscroll;
};

bool initialize_compose_interaction(const compose_interaction_population_context_t& input);

bool update_compose_snap_preferences(const compose_interaction_population_context_t& input);

std::optional<wormedit::editor_interaction_view_model_t>
inspect_compose_interaction(const compose_interaction_population_context_t& input);

std::optional<wormedit::editor_interaction_view_model_t>
prepare_compose_interaction(const compose_interaction_population_context_t& input);


WormUiComposeSelectionMode
compose_scalar_selection_mode(wormedit::editor_area_selection_mode_t mode);

std::uint64_t prepare_compose_projection_state(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input);

void populate_compose_pending_previews(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input);

template <class T> auto compose_vector_bytes(const std::vector<T>& values) -> std::size_t {
    return values.capacity() * sizeof(T);
}

auto compose_static_storage_bytes(const frontend_workspace_view_t& storage) -> std::size_t;

void publish_compose_static_source(compose_scene_cache_t& cache, wormedit::ui::compose_view_t& view);



void clear_static_source_selection(frontend_workspace_view_t& storage);

auto compose_static_labels_fit(const wormedit::document_history_t& history) -> bool;

void discard_compose_static_source(compose_scene_cache_t& cache);

auto make_compose_static_view(const wormedit::ui::compose_view_t& view,
                              const wormedit::editor_interaction_view_model_t& expanded)
    -> wormedit::ui::compose_view_t;

void refresh_compose_static_source(const compose_scene_projection_context_t& projection,
                                   compose_scene_cache_t& cache, std::uint64_t canvas_rows);

void refresh_compose_static_scene_style(compose_scene_cache_t& cache, wormedit::ui::compose_view_t& view);

template <class Records, class Bounds>
void rebuild_scalar_intervals(wormedit::common::static_interval_index_t& index,
                              const Records& records, Bounds bounds) {
    std::vector<wormedit::common::static_interval_t> intervals;
    intervals.reserve(records.size());
    for (std::size_t record = 0; record < records.size(); ++record) {
        const auto [begin, end] = bounds(records[record]);
        intervals.push_back({begin, end, record});
    }
    index.rebuild(intervals);
}

void refresh_scalar_projection_indices(compose_scene_cache_t& cache);

auto scalar_projection_query(compose_scene_cache_t& cache, unsigned pass, double begin, double end)
    -> std::optional<std::span<const std::size_t>>;

auto scalar_display_lane_number(const auto& record,
                                const wormedit::editor_field_scene_view_t& scene) -> std::uint32_t {
    const auto x = (record.bounds.left + record.bounds.right) * .5;
    const auto y = (record.bounds.top + record.bounds.bottom) * .5;
    for (const auto& lane : scene.lanes)
        if (lane.lane == record.lane_id && x >= lane.bounds.left && x < lane.bounds.right &&
            y >= lane.bounds.top && y < lane.bounds.bottom)
            return lane.display_order + 1;
    return 0;
}


template <class T>
auto prepare_scalar_geometry_output(frontend_workspace_view_t&, std::vector<T>& output,
                                    std::size_t count) -> bool {
    output.clear();
    output.reserve(count);
    return true;
}


template <class T>
auto append_scalar_geometry_output(std::vector<T>& output, const T& record) -> bool {
    output.push_back(record);
    return true;
}

template <class Records, class Transform>
auto extract_scalar_rectangles(compose_scene_cache_t& cache, frontend_workspace_view_t& flattened,
                               unsigned pass, const Records& source, Records& output, double begin,
                               double end, double shift, Transform transform) -> bool {
    const auto indices = scalar_projection_query(cache, pass, begin - shift, end - shift);
    if (!indices)
        return false;
    if (!prepare_scalar_geometry_output(flattened, output, indices->size()))
        return false;
    for (const auto index : *indices) {
        auto record = source[index];
        record.bounds.top += shift;
        record.bounds.bottom += shift;
        if (transform(record, index) && !append_scalar_geometry_output(output, record))
            return false;
    }
    return true;
}

auto extract_scalar_grid(compose_scene_cache_t& cache,
                         const wormedit::editor_field_scene_view_t& scene,
                         frontend_workspace_view_t& flattened, double shift) -> bool;

auto extract_scalar_objects(compose_scene_cache_t& cache,
                            const wormedit::editor_field_scene_window_metadata_view_t& metadata,
                            frontend_workspace_view_t& flattened,
                            const wormedit::editor_interaction_view_model_t& interaction,
                            double shift, double height) -> bool;

auto scalar_projection_fits_budget(const compose_scene_projection_context_t& projection,
                                   const wormedit::editor_field_scene_request_t& request) -> bool;

void discard_scalar_projection(frontend_workspace_view_t& flattened);

auto reusable_scalar_source(const compose_scene_cache_t& cache, const wormedit::ui::compose_view_t& view)
    -> bool;

auto build_reused_scalar_scene_view(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input, double field_left,
                                 double field_width, bool downscroll) -> bool;

void prepare_compose_bgm_widths(const wormedit::chart_document_t& chart, std::uint64_t project,
                                compose_scene_cache_t& cache,
                                const application_command_state_t& application);

auto compose_has_bgm_pane(const chart_document_t& chart,
                          const editor_interaction_view_model_t& view) -> bool;

bool compose_project_scene(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input,
                           std::uint64_t canvas_rows);

void append_area_object_geometry(wormedit::editor_area_geometry_snapshot_t& snapshot,
                                 wormedit::editor_selection_object_t object,
                                 wormedit::ui::compose_rect_t bounds, const wormedit::ui::compose_view_t& view,
                                 bool downscroll);

void append_area_timing_geometry(wormedit::editor_area_geometry_snapshot_t& snapshot,
                                 const wormedit::ui::compose_view_t& view, bool downscroll);

void append_area_visual_geometry(wormedit::editor_area_geometry_snapshot_t& snapshot,
                                 const wormedit::ui::compose_view_t& view, bool downscroll);

void append_area_bar_line_geometry(wormedit::editor_area_geometry_snapshot_t& snapshot,
                                   const wormedit::ui::compose_view_t& view, bool downscroll);

void cache_area_object_geometry(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input);

auto paste_timing_ghost_lane(const wormedit::chart_clipboard_payload_t& object)
    -> std::optional<wormedit::timing_lane_kind_t>;

auto paste_timing_ghost_span(const wormedit::chart_clipboard_payload_t& object)
    -> std::pair<wormedit::local_position_t, wormedit::local_position_t>;

void append_paste_timing_ghosts(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input,
                                application_command_state_t::paste_session_t& session);

void populate_compose_paste_projection(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input);

auto compose_paint_head_occupied(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input,
                                 const wormedit::editor_hit_target_t& target, std::uint64_t row)
    -> bool;

void populate_compose_paint_ghost(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input);

void populate_compose_bgm_mix(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input);

void populate_compose_final_projection(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input);

void populate_bgm_only_compose_projection(
    frontend_workspace_view_t& flattened, wormedit::document_history_t& history,
    const wormedit::chart_document_t& chart, const wormedit::measure_index_t& measures,
    compose_scene_cache_t& cache, application_command_state_t& application_state,
    wormedit::ui::compose_view_t& view, bool downscroll,
    const wormedit::timing_lane_pseudonote_outcome_t* timing_stage);

void build_loop_selection_controls_view(wormedit::ui::compose_view_t& view,
                                     const application_command_state_t& application);

void bind_branch_statement_metadata(const wormedit::bms_control_node_t& node,
                                    std::vector<wormedit::ui::compose_branch_row_view_t>& rows,
                                    std::size_t& row_index);

void populate_branch_statement_metadata(frontend_workspace_view_t& flattened,
                                        const wormedit::chart_document_t& chart);

void build_branch_statement_feedback_view(wormedit::ui::compose_view_t& view,
                                       application_command_state_t& application_state,
                                       wormedit::chart_id_t chart, std::uint64_t revision);

} // namespace wormedit::sys::compose_projection_detail

namespace wormedit::sys {
auto default_song_bar_hidden(const wormedit::song_document_t& document,
                             std::optional<wormedit::chart_id_t> chart) -> bool;

auto projected_pointer_hold_active(const std::optional<wormedit::editor_interaction_state_t>& state)
    -> bool;

} // namespace wormedit::sys
