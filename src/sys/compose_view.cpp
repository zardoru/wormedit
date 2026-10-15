#include "sys/compose_view.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_draw_source_view.h"
#include "sys/compose_note_to_bgm.h"
#include "sys/compose_view_internal.h"
#include "sys/compose_resource_presenter.h"
#include "sys/compose_sound_selection.h"
#include "sys/frame_timing.h"
#include "sys/frontend_geometry.h"
#include "sys/recipe_form.h"
#include "ui/nuklear/arrow_glyph.h"
#include <wormedit/core/dead_span_rules.h>
#include <wormedit/core/interaction_gestures.h>
#include <wormedit/core/sv_recipe.h>

#include <wormedit/core/editor_scene.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/semantic_layer_editor.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/virtual_canvas.h>

#include <SDL3/SDL_scancode.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdint>
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
using namespace compose_projection_detail;


void compose_scene_cache_t::invalidate_scene_cache() {
    scene.index.reset();
    scene.scene_window.scene.reset();
    discard_compose_static_source(*this);
    scene.cached_measure_index.reset();
    scene.selected_note_source = {};
    scene.selected_bgm_source = {};
    scene.bgm_rows_by_id.clear();
    scene.hit_test_index.reset();
    timing.marker_timing_index.reset();
    timing.dead_spans.reset();
    timing.kiai_spans.reset();
    timing.sv_steps.reset();
    timing.recipe_areas.reset();
    timing.speed_spans.reset();
    timing.proposed_offset_timing_index.reset();
    timing.proposed_offset_seconds.reset();
    timing.beat_fit_drag.reset();
    timing.beat_fit_asset.reset();
    timing.proposed_beat_timing_index.reset();
    timing.proposed_beat_bpm.reset();
    timing.proposed_beat_event = {};
    waveform.channel_waveform_channel = {};
    waveform.channel_waveform_source = nullptr;
    waveform.channel_waveform_source_generation = 0;
    waveform.channel_waveform_slices.reset();
    waveform.channel_waveform_projection_valid = false;
    waveform.channel_waveform_rows.clear();
    frame.interaction_view.reset();
    frame.hover.reset();
    frame.focus_note_label_editor = false;
    frame.viewport_autoscroll_active = false;
    frame.projection_error.clear();
    frame.paint_pointer.reset();
    scene.attempted = false;
}

bool compose_scene_cache_t::ensure_scene_key(const wormedit::song_document_t& document,
    const wormedit::chart_document_t& chart) {
    if (is_scene_key_current(document, chart)) return false;
    invalidate_scene_cache();
    scene.scene_document_identity = &document;
    scene.scene_chart_identity = &chart;
    scene.scene_song = document.id;
    scene.chart = chart.id;
    scene.revision = document.revision;
    scene.attempted = true;
    return true;
}

namespace compose_projection_detail {
void report_compose_projection(flattened_workspace_t& flattened,
                               projection_feedback_kind_t producer,
                               wormedit::notification_severity_t severity, std::string message,
                               std::uint64_t object, wormedit::notification_source_t source) {
    flattened.projection_feedback.push_back({producer, object, severity, source, std::move(message)});
}

void report_compose_diagnostic(flattened_workspace_t& flattened, projection_feedback_kind_t producer,
    const wormedit::diagnostic_list_t& diagnostics, const char* fallback) {
    report_compose_projection(flattened, producer, to_notification_severity(diagnostics),
                              diagnostics.empty() ? std::string{fallback}
                                                  : diagnostics.front().message);
}
} // namespace compose_projection_detail

auto resolve_compose_paint_cell(const wormedit::chart_document_t& chart,
                                const wormedit::editor_hit_test_index_t& index,
                                const wormedit::editor_interaction_view_model_t& view, double x,
                                double y, double field_left, double field_width, bool downscroll,
                                std::uint64_t snap)
    -> std::optional<wormedit::editor_hit_target_t> {
    auto hit_view = view;
    hit_view.active_snap_denominator = snap;
    auto target = wormedit::hit_test_editor(chart, index, hit_view,
        {.x_pixels = x, .y_pixels = downscroll ? view.viewport_height_pixels - y : y,
         .field_left_pixels = field_left, .field_width_pixels = field_width});
    if (!target) return std::nullopt;
    const auto snapped = wormedit::snap_position(chart, index.measures, target->position, snap);
    if (!snapped) return std::nullopt;
    target->position = *snapped;
    return target;
}

namespace compose_projection_detail {
auto paste_anchor(application_command_state_t::paste_session_t& session,
                  wormedit::local_position_t cursor) -> wormedit::local_position_t {
    if (session.pointer_cursor && *session.pointer_cursor == cursor) return session.anchor;
    session.pointer_cursor.reset();
    return cursor;
}

// Pasted playable heads obey the same Warp rule as placed ones; fakes, mines and tails may land.
void block_paste_heads_in_warps(const wormedit::chart_document_t& chart,
                                wormedit::chart_paste_outcome_t& proposal) {
    if (!proposal.value || chart.warps.empty()) return;
    const auto indexed = wormedit::build_measure_index(chart);
    const auto* index = std::get_if<wormedit::measure_index_t>(&indexed);
    if (!index) return;
    for (const auto& object : proposal.value->payload.report.objects) {
        const auto* note = std::get_if<wormedit::note_t>(&object);
        const auto row = note ? index->row_of(note->position) : std::nullopt;
        if (!row || !wormedit::is_head_blocked_by_warp(chart, *index, *row, note->kind)) continue;
        proposal.diagnostics.push_back(wormedit::chart_paste_diagnostic_t::from(
            "Playable note heads cannot sit inside a warp.", proposal.value->stamp,
            std::move(proposal.value->payload.report)));
        proposal.value.reset();
        return;
    }
}

auto paste_geometry(const wormedit::chart_document_t& source,
                    const wormedit::chart_paste_report_t& report) -> wormedit::chart_document_t {
    wormedit::chart_document_t chart;
    chart.id = source.id;
    chart.source_family = source.source_family;
    chart.divisions_per_standard_measure = source.divisions_per_standard_measure;
    chart.lane_layouts = source.lane_layouts;
    chart.input_profile = source.input_profile;
    chart.semantic_layers = source.semantic_layers;
    chart.bgm_source_lanes = source.bgm_source_lanes;
    chart.active_branch_selection = source.active_branch_selection;
    for (const auto& measure : source.measures)
        chart.measures.push_back({.id = measure.id, .definition = measure.definition});
    for (const auto& object : report.objects) {
        // Ghost previews draw notes and BGM events; pasted timing objects have no ghost.
        std::visit([&chart](const auto& value) {
            using value_t = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<value_t, wormedit::note_t> ||
                          std::is_same_v<value_t, wormedit::bgm_event_t>) {
                const auto found = chart.find_measure(value.position.measure);
                if (found == nullptr) return;
                if constexpr (std::is_same_v<value_t, wormedit::note_t>)
                    found->notes.push_back(value);
                else found->bgm_events.push_back(value);
            }
        }, object);
    }
    return chart;
}
void build_compose_paste_preview(application_command_state_t::paste_session_t& session,
    const wormedit::song_document_t& document, const wormedit::chart_document_t& source,
    wormedit::local_position_t anchor) {
    session.anchor = anchor;
    session.anchor_available = true;
    session.proposal = wormedit::chart_paste_proposal_t::propose(document, source.id,
        session.clipboard, anchor, document.revision);
    block_paste_heads_in_warps(source, session.proposal);
    const auto preview = wormedit::chart_paste_preview_view_t::from(session.proposal);
    const wormedit::chart_paste_report_t empty;
    session.geometry = paste_geometry(source, preview.report ? *preview.report : empty);
    const auto indexed = wormedit::build_editor_field_scene_index(session.geometry, document.revision);
    const auto* index = std::get_if<wormedit::editor_field_scene_index_t>(&indexed);
    session.index = index ? std::optional{*index} : std::nullopt;
}
} // namespace compose_projection_detail

auto revise_compose_paste(application_command_state_t& application_state,
    const wormedit::song_document_t& document, wormedit::chart_id_t chart,
    wormedit::local_position_t anchor, std::string* failure) -> bool {
    auto& session = application_state.project.clipboard.paste_session;
    if (!session) {
        if (failure) *failure = "The paste preview is stale.";
        return false;
    }
    const auto preview = wormedit::chart_paste_preview_view_t::from(session->proposal);
    if (!preview.stamp || !preview.stamp->is_current(document, chart,
            wormedit::document_identity_policy_t::revision_only)) {
        if (failure) *failure = wormedit::stale_action_message("Paste preview");
        session.reset();
        return false;
    }
    if (session->anchor == anchor && session->index) return true;
    const auto source = document.find_chart(chart);
    if (source == nullptr) {
        if (failure) *failure = "The paste preview is stale.";
        session.reset();
        return false;
    }
    build_compose_paste_preview(*session, document, *source, anchor);
    return true;
}

auto begin_compose_paste(application_command_state_t& application_state,
    const wormedit::song_document_t& document, const wormedit::editor_interaction_state_t& state,
    std::string& status) -> bool {
    if (!application_state.project.clipboard.chart_clipboard || editor_interaction_has_pending_gesture(state)) {
        status = "Paste requires a copied selection and no pending gesture.";
        return false;
    }
    application_command_state_t::paste_session_t session;
    session.clipboard = *application_state.project.clipboard.chart_clipboard;
    application_state.project.compose_gesture.note_paint_mode_active = false;
    application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
    const auto* source = document.find_chart(state.chart);
    if (!source) {
        application_state.project.clipboard.paste_session.reset();
        status = "The paste chart is unavailable.";
        return false;
    }
    build_compose_paste_preview(session, document, *source, state.cursor);
    application_state.project.clipboard.paste_session = std::move(session);
    status = "Paste preview: click or Enter to commit; Escape to cancel.";
    return true;
}

auto apply_compose_paste(application_command_state_t& application_state,
    wormedit::document_history_t& history, wormedit::editor_interaction_state_t& state,
    std::string& status, wormedit::notification_severity_t* severity) -> bool {
    if (severity) *severity = wormedit::notification_severity_t::warning;
    const auto anchor = application_state.project.clipboard.paste_session
        ? paste_anchor(*application_state.project.clipboard.paste_session, state.cursor) : state.cursor;
    if (!revise_compose_paste(application_state, history.document, state.chart, anchor, &status))
        return false;
    auto& session = *application_state.project.clipboard.paste_session;
    if (!session.anchor_available) { status = session.anchor_error; return false; }
    if (!session.proposal.value) {
        const auto preview = wormedit::chart_paste_preview_view_t::from(session.proposal);
        status = preview.diagnostic ? preview.diagnostic->message : "";
        return false;
    }
    const auto applied = wormedit::paste_chart_clipboard(history, state.chart, session.clipboard,
                                                       session.anchor);
    if (!applied.applied) {
        status = applied.diagnostics.empty() ? "Paste failed." : applied.diagnostics.front().message;
        if (severity && !applied.diagnostics.empty()) {
            *severity = wormedit::to_notification_severity(
                applied.diagnostics.front().severity, wormedit::notification_severity_t::warning);
        }
        return false;
    }
    application_state.project.clipboard.paste_session.reset();
    status = "Selection pasted.";
    if (severity) *severity = wormedit::notification_severity_t::success;
    return true;
}

auto compose_surface_height(int window_height, const WormUiWorkspaceView& view) -> double {
    float recovery_height = 0.0f;
    if (view.recovery_panel_visible) {
        recovery_height = 40.0f + (view.recovery_notice && view.recovery_notice[0] ? 22.0f : 0.0f) +
                          static_cast<float>(view.recovery_candidate_count) * 48.0f +
                          static_cast<float>(view.recovery_diagnostic_count) * 18.0f;
        for (std::size_t row = 0; row < view.recovery_candidate_count; ++row) {
            const auto& candidate = view.recovery_candidates[row];
            if ((candidate.action_mask & WORM_UI_RECOVERY_ACTION_SAVE_AS) != 0) {
                recovery_height += 25.0f;
                if (candidate.path_label == nullptr || candidate.path_label[0] == '\0')
                    recovery_height += 18.0f;
            }
            if (candidate.disabled_reason && candidate.disabled_reason[0])
                recovery_height += 20.0f;
        }
    }
    const auto content_height =
        window_height > 300 ? static_cast<float>(window_height) - 132.0f -
                                  (view.compose.bgm_bar_reserved ? WORM_UI_BGM_BAR_CHROME_HEIGHT : 0.0f) -
                                  recovery_height : 80.0f;
    const auto& compose = view.compose;
    const auto rows_shown = std::min<std::size_t>(compose.branch_row_count, 4);
    const auto diagnostics_shown = std::min<std::size_t>(compose.branch_diagnostic_count, 2);
    const bool branch_status_shown =
        compose.branch_rows_truncated || compose.branch_display_text_truncated ||
        compose.branch_diagnostics_truncated || compose.branch_row_count > rows_shown ||
        compose.branch_diagnostic_count > diagnostics_shown;
    const bool branch_visible = compose.branch_outline_available || compose.branch_row_count != 0 ||
                                compose.branch_diagnostic_count != 0 || branch_status_shown;
    const auto branch_height = branch_visible
                                   ? 123.0f + static_cast<float>(rows_shown + diagnostics_shown +
                                                                 (branch_status_shown ? 1 : 0)) *
                                                  18.0f
                                   : 0.0f;
    return std::max(64.0, static_cast<double>(content_height - 38.0f - 18.0f - branch_height));
}

namespace compose_projection_detail {
auto known_mania_key_profile(std::string_view id) -> bool {
    constexpr std::string_view prefix{"osu-mania-"};
    if (!id.starts_with(prefix) || !id.ends_with('k')) return false;
    const auto digits = id.substr(prefix.size(), id.size() - prefix.size() - 1);
    unsigned keys{};
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), keys);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() && keys >= 1 && keys <= 18;
}
} // namespace compose_projection_detail
// Switchable profiles per lane count, built once: the frame path must not allocate.
// Only the UI thread builds frames, so the cache needs no lock.
const std::vector<wormedit::lane_input_profile_t>& cached_lane_profile_choices(std::size_t lanes) {
    static std::array<std::optional<std::vector<wormedit::lane_input_profile_t>>, 19> cache;
    static const std::vector<wormedit::lane_input_profile_t> none;
    if (lanes >= cache.size()) return none;
    if (!cache[lanes]) cache[lanes] = wormedit::lane_profile_choices(lanes);
    return *cache[lanes];
}

const std::vector<wormedit::lane_input_profile_t>& cached_initial_lane_profile_choices() {
    static const auto choices = wormedit::lane_profile_choices();
    return choices;
}

// Fills the switchable-profile list for the layout segment active at the cursor.
void populate_lane_profile_choices(WormUiComposeView& view, const wormedit::chart_document_t& chart,
                                   wormedit::local_position_t cursor) {
    view.lane_profile_choice_count = 0;
    view.lane_profile_current = -1;
    const auto* segment = wormedit::active_lane_layout_segment(chart, cursor);
    if (!segment) {
        const auto& choices = cached_initial_lane_profile_choices();
        const auto count = std::min<std::size_t>(choices.size(), WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY);
        for (std::size_t index = 0; index < count; ++index) {
            const auto& profile = choices[index];
            const auto& id = profile.layout.profile_id;
            const auto is_bms = id.starts_with("bms-") || id.starts_with("bme-") ||
                                id.starts_with("pms-") || id.starts_with("project2dx-");
            const auto prefix = is_bms ? "" : id.starts_with("stepmania:")
                ? "StepMania " : "osu!mania ";
            const auto suffix = id.ends_with("-specialstyle") ? " (SpecialStyle)" : "";
            std::snprintf(view.lane_profile_names[index], sizeof(view.lane_profile_names[index]),
                          "%s%s%s", prefix, profile.layout.display_name.c_str(), suffix);
        }
        view.lane_profile_choice_count = static_cast<std::uint8_t>(count);
        return;
    }
    if (!wormedit::can_use_lane_key_profiles(segment->layout)) return;
    const auto& choices = cached_lane_profile_choices(segment->layout.lanes.size());
    const auto count = std::min<std::size_t>(choices.size(), WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& layout = choices[index].layout;
        const auto* prefix = layout.profile_id.starts_with("stepmania:") ? "StepMania " : "osu!mania ";
        std::snprintf(view.lane_profile_names[index], sizeof(view.lane_profile_names[index]),
                      "%s%s%s", prefix, layout.display_name.c_str(),
                      layout.profile_id.ends_with("-specialstyle") ? " (SpecialStyle)" : "");
        if (layout.profile_id == segment->layout.profile_id)
            view.lane_profile_current = static_cast<std::int8_t>(index);
    }
    // An unrecognized all-key layout stays unselected until the user deliberately chooses
    // an equal-count profile; its ID is not an eligibility requirement.
    view.lane_profile_choice_count = static_cast<std::uint8_t>(count);
}

void append_bms_layout_profiles(std::vector<wormedit::lane_input_profile_t>& choices) {
    for (const auto& profile : wormedit::built_in_bms_lane_profiles())
        choices.push_back(profile.profile);
}

void append_stepmania_layout_profiles(std::vector<wormedit::lane_input_profile_t>& choices) {
    constexpr std::array<std::string_view, 3> step_types{"pump-single", "pump-double", "dance-solo"};
    for (const auto type : step_types)
        if (auto profile = wormedit::make_stepmania_profile_for(type))
            choices.push_back(std::move(*profile));
}

void append_osu_layout_profiles(std::vector<wormedit::lane_input_profile_t>& choices) {
    for (std::uint32_t keys = 1; keys <= 18; ++keys)
        if (auto profile = wormedit::make_osu_mania_profile(keys))
            choices.push_back(std::move(*profile));
    for (const auto keys : {12u, 16u})
        choices.push_back(*wormedit::make_osu_mania_profile(keys, true));
}

void append_mode_layout_profiles(std::vector<wormedit::lane_input_profile_t>& choices,
                                 wormedit::editor_mode_family_t mode) {
    switch (mode) {
    case wormedit::editor_mode_family_t::beatmania:
    case wormedit::editor_mode_family_t::popn: append_bms_layout_profiles(choices); break;
    case wormedit::editor_mode_family_t::dance:
    case wormedit::editor_mode_family_t::pump: append_stepmania_layout_profiles(choices); break;
    case wormedit::editor_mode_family_t::keyboard: append_osu_layout_profiles(choices); break;
    case wormedit::editor_mode_family_t::other: break;
    }
}

auto make_lane_layout_profile_choices(wormedit::format_family_t family,
                                      wormedit::editor_mode_family_t mode)
    -> std::vector<wormedit::lane_input_profile_t> {
    std::vector<wormedit::lane_input_profile_t> choices;
    if (family == wormedit::format_family_t::bms) {
        append_bms_layout_profiles(choices);
        return choices;
    }
    if (family == wormedit::format_family_t::osu_mania) {
        append_osu_layout_profiles(choices);
        return choices;
    }
    if (family == wormedit::format_family_t::stepmania) {
        append_stepmania_layout_profiles(choices);
        return choices;
    }
    if (family == wormedit::format_family_t::bmson) {
        choices.push_back(wormedit::make_bmson_generic_24k_profile());
        choices.push_back(wormedit::make_bmson_generic_48k_profile());
        append_mode_layout_profiles(choices, mode);
        return choices;
    }
    append_mode_layout_profiles(choices, mode);
    return choices;
}

auto cached_lane_layout_profile_choices(wormedit::format_family_t family,
                                        wormedit::editor_mode_family_t mode)
    -> const std::vector<wormedit::lane_input_profile_t>& {
    static std::array<std::array<std::optional<std::vector<wormedit::lane_input_profile_t>>, 6>, 7> cache;
    static const std::vector<wormedit::lane_input_profile_t> none;
    const auto key = static_cast<std::size_t>(family);
    const auto mode_key = static_cast<std::size_t>(mode);
    if (key >= cache.size() || mode_key >= cache.front().size()) return none;
    if (!cache[key][mode_key]) cache[key][mode_key] = make_lane_layout_profile_choices(family, mode);
    return *cache[key][mode_key];
}


// The proposal copies the chart, so it is reused until the document or cursor measure changes.
auto cached_measure_removal(const wormedit::song_document_t& song, const wormedit::chart_document_t& chart,
                            wormedit::measure_id_t measure) -> const wormedit::measure_removal_proposal_t& {
    static measure_removal_cache_t cache;
    const auto matches = cache.valid && cache.song == song.id.value && cache.revision == song.revision &&
                         cache.chart == chart.id.value && cache.measure == measure.value;
    if (!matches) {
        cache.proposal = wormedit::propose_measure_removal(chart, measure);
        cache.song = song.id.value;
        cache.revision = song.revision;
        cache.chart = chart.id.value;
        cache.measure = measure.value;
        cache.valid = true;
    }
    return cache.proposal;
}

void populate_measure_removal(WormUiComposeView& view, const wormedit::song_document_t& song,
                              const wormedit::chart_document_t& chart,
                              wormedit::local_position_t cursor, bool virtual_cursor) {
    view.measure_removal_available = false;
    view.measure_removal_has_contents = false;
    view.measure_removal_blocked = false;
    view.measure_removal_measure_id = 0;
    view.measure_removal_message[0] = '\0';
    view.measure_removal_deleted_notes = view.measure_removal_deleted_bgm_events = 0;
    view.measure_removal_deleted_bga_events = view.measure_removal_shortened_holds = 0;
    view.measure_removal_moved_positions = 0;
    const auto materialized = std::ranges::any_of(
        chart.measures, [&](const auto& measure) { return measure.id == cursor.measure; });
    if (virtual_cursor || !materialized) return;
    const auto& proposal = cached_measure_removal(song, chart, cursor.measure);
    view.measure_removal_available = true;
    view.measure_removal_measure_id = cursor.measure.value;
    view.measure_removal_blocked = !proposal;
    if (!proposal.diagnostics.empty())
        std::snprintf(view.measure_removal_message, sizeof(view.measure_removal_message), "%s",
                      proposal.diagnostics.front().message.c_str());
    view.measure_removal_deleted_notes = proposal.deleted_notes;
    view.measure_removal_deleted_bgm_events = proposal.deleted_bgm_events;
    view.measure_removal_deleted_bga_events = proposal.deleted_bga_events;
    view.measure_removal_shortened_holds = proposal.shortened_holds;
    view.measure_removal_moved_positions = proposal.moved_positions;
    // A blocked removal of a measure that still anchors objects must show its reason instead of
    // falling back to the plain delete, which would be rejected as an unknown measure.
    view.measure_removal_has_contents =
        proposal.deleted_notes + proposal.deleted_bgm_events + proposal.deleted_bga_events +
                proposal.shortened_holds + proposal.moved_positions != 0 ||
        (view.measure_removal_blocked &&
         wormedit::has_measure_positioned_objects(chart, cursor.measure));
}

void append_lane_layout_profile_choice(WormUiComposeView& view,
    const wormedit::lane_layout_t& layout, const wormedit::lane_layout_segment_t* active) {
    const auto index = static_cast<std::size_t>(view.lane_layout_profile_choice_count);
    for (std::size_t prior = 0; prior < index; ++prior)
        if (std::string_view(view.lane_layout_profile_ids[prior]) == layout.profile_id) return;
    if (index >= WORM_UI_COMPOSE_LAYOUT_PROFILE_CAPACITY) return;
    const auto* prefix = layout.profile_id.starts_with("osu-mania-") ? "osu!mania "
        : layout.profile_id.starts_with("stepmania:") ? "StepMania " : "";
    std::snprintf(view.lane_layout_profile_names[index],
        sizeof(view.lane_layout_profile_names[index]), "%s%s%s", prefix, layout.display_name.c_str(),
                      layout.profile_id.ends_with("-specialstyle") ? " (SpecialStyle)" : "");
    std::snprintf(view.lane_layout_profile_ids[index], sizeof(view.lane_layout_profile_ids[index]),
        "%s", layout.profile_id.c_str());
    if (active && layout.profile_id == active->layout.profile_id)
        view.lane_layout_profile_current = static_cast<std::int8_t>(index);
    ++view.lane_layout_profile_choice_count;
}

void populate_lane_layout_profile_choices(WormUiComposeView& view,
    const wormedit::chart_document_t& chart, const wormedit::lane_layout_segment_t* active) {
    const auto mode = active ? compose_mode_family(active->layout) : wormedit::editor_mode_family_t::other;
    for (const auto& profile : cached_lane_layout_profile_choices(chart.source_family, mode))
        append_lane_layout_profile_choice(view, profile.layout, active);
    if (active && wormedit::can_use_lane_key_profiles(active->layout)) {
        for (const auto& profile : cached_lane_profile_choices(active->layout.lanes.size()))
            append_lane_layout_profile_choice(view, profile.layout, active);
    }
    if (active) append_lane_layout_profile_choice(view, active->layout, active);
}

void populate_lane_layout_editor(WormUiComposeView& view, const wormedit::chart_document_t& chart,
                                 const wormedit::measure_index_t& measures,
                                 wormedit::local_position_t cursor, bool virtual_cursor,
                                 std::optional<std::size_t> selected_canvas_measure) {
    view.lane_layout_profile_choice_count = 0;
    view.lane_layout_profile_current = -1;
    view.lane_layout_edit_segment_id = 0;
    view.lane_layout_edit_is_update = false;
    view.lane_layout_edit_available = true;
    const auto* measure = measures.entry_for(cursor.measure);
    // At a virtual-only cursor the target is the selected canvas measure, created on apply.
    const auto virtual_target = virtual_cursor || !measure;
    if (virtual_target && !selected_canvas_measure) {
        view.lane_layout_edit_available = false;
        return;
    }
    const auto measure_number = virtual_target
        ? *selected_canvas_measure + 1
        : static_cast<std::size_t>(measure - measures.entries.data()) + 1;
    if (virtual_target) cursor = {};
    view.lane_layout_edit_measure_number = static_cast<std::uint32_t>(
        std::min<std::size_t>(measure_number, std::numeric_limits<std::uint32_t>::max()));
    const auto target_measure = virtual_target && measure_number <= measures.entries.size()
        ? measures.entries[measure_number - 1].id : cursor.measure;
    const auto segment = std::ranges::find_if(chart.lane_layouts, [&](const auto& value) {
        return target_measure && value.position.measure == target_measure && value.position.row == 0;
    });
    if (segment != chart.lane_layouts.end()) {
        view.lane_layout_edit_segment_id = segment->id.value;
        view.lane_layout_edit_is_update = true;
    }
    const auto* active = virtual_target
        ? (chart.lane_layouts.empty() ? nullptr : &chart.lane_layouts.front())
        : wormedit::active_lane_layout_segment(chart, cursor);
    populate_lane_layout_profile_choices(view, chart, active);
}

void populate_lane_layout_segments(flattened_workspace_t& flattened,
                                   const wormedit::chart_document_t& chart,
                                   const wormedit::measure_index_t& measures,
                                   const wormedit::editor_interaction_view_model_t& interaction,
                                   bool downscroll, WormUiComposeView& view) {
    const auto count = std::min<std::size_t>(chart.lane_layouts.size(),
                                             WORM_UI_COMPOSE_LAYOUT_SEGMENT_CAPACITY);
    auto* arena = flattened.arena();
    auto storage = arena ? arena_array<WormUiComposeLaneLayoutSegmentView>(*arena, count)
                         : frame_span<WormUiComposeLaneLayoutSegmentView>{};
    if (count != 0 && storage.capacity() != count) {
        view.truncated = true;
        return;
    }
    const auto pixels_per_row = compose_pixels_per_row(chart, interaction);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& segment = chart.lane_layouts[index];
        const auto* measure = measures.entry_for(segment.position.measure);
        const auto row = (measures).row_of(segment.position);
        if (!measure || !row) {
            view.truncated = true;
            return;
        }
        const auto measure_number = static_cast<std::size_t>(measure - measures.entries.data()) + 1;
        const auto projected_y = compose_project_y(*row, pixels_per_row,
                                                   interaction.viewport_scroll_pixels,
                                                   view.viewport_height_pixels, downscroll);
        if (!storage.push_back({.segment_id = segment.id.value,
                                .measure_number = static_cast<std::uint32_t>(std::min<std::size_t>(
                                    measure_number, std::numeric_limits<std::uint32_t>::max())),
                                .row = segment.position.row,
                                .y_pixels = projected_y,
                                .profile_name = segment.layout.display_name.c_str()})) {
            view.truncated = true;
            return;
        }
    }
    view.lane_layout_segments = storage.data();
    view.lane_layout_segment_count = storage.size();
    if (chart.lane_layouts.size() > count) view.truncated = true;
}

auto compose_mode_family(const wormedit::lane_layout_t& layout) -> wormedit::editor_mode_family_t {
    using family = wormedit::editor_mode_family_t;
    const auto& id = layout.profile_id;
    const auto catalog = wormedit::built_in_bms_lane_profiles();
    const auto found = std::ranges::find(catalog, id, [](const auto& item) -> const std::string& { return item.profile.layout.profile_id; });
    if (found != catalog.end()) {
        if (found->family == wormedit::bms_lane_profile_family_t::popn) return family::popn;
        return found->family == wormedit::bms_lane_profile_family_t::special ? family::other : family::beatmania;
    }
    constexpr std::array<std::string_view, 4> beat{"bmson-beat-5k", "bmson-beat-7k", "bmson-beat-10k", "bmson-beat-14k"};
    if (std::ranges::find(beat, id) != beat.end()) return family::beatmania;
    if (id == "bmson-popn-5k" || id == "bmson-popn-9k") return family::popn;
    constexpr std::array<std::string_view, 5> dance{"stepmania:dance-single", "stepmania:dance-double", "stepmania:dance-couple", "stepmania:dance-solo", "stepmania:dance-threepanel"};
    if (std::ranges::find(dance, id) != dance.end()) return family::dance;
    constexpr std::array<std::string_view, 4> pump{"stepmania:pump-single", "stepmania:pump-double", "stepmania:pump-couple", "stepmania:pump-halfdouble"};
    if (std::ranges::find(pump, id) != pump.end()) return family::pump;
    if (layout.bmson_generic_keys_only || id == "stepmania:kb7-single" || known_mania_key_profile(id))
        return family::keyboard;
    return family::other;
}

auto compose_field_downscroll(const wormedit::chart_document_t& chart) -> bool {
    for (const auto& segment : chart.lane_layouts) {
        for (const auto& lane : segment.layout.lanes) {
            switch (lane.glyph) {
            case wormedit::lane_glyph_t::arrow_left:
            case wormedit::lane_glyph_t::arrow_down:
            case wormedit::lane_glyph_t::arrow_up:
            case wormedit::lane_glyph_t::arrow_right:
            case wormedit::lane_glyph_t::arrow_up_left:
            case wormedit::lane_glyph_t::arrow_up_right:
            case wormedit::lane_glyph_t::arrow_down_left:
            case wormedit::lane_glyph_t::arrow_down_right:
            case wormedit::lane_glyph_t::arrow_center:
                return false;
            case wormedit::lane_glyph_t::bar:
            case wormedit::lane_glyph_t::square:
                break;
            }
        }
    }
    return true;
}

auto compose_field_downscroll(const wormedit::chart_document_t& chart,
    const wormedit::editor_preferences_t& preferences) -> bool {
    const auto family = chart.lane_layouts.empty() ? wormedit::editor_mode_family_t::other
        : compose_mode_family(chart.lane_layouts.front().layout);
    const auto direction = preferences.scroll_directions[static_cast<std::size_t>(family)];
    return direction == 0 ? compose_field_downscroll(chart) : direction == 2;
}

auto compose_project_y(long double row, double pixels_per_row, double scroll,
                       double viewport_height, bool downscroll) -> double {
    const auto offset = row * pixels_per_row - scroll;
    return static_cast<double>(downscroll ? viewport_height - offset : offset);
}

auto maximum_layout_lane_count(const wormedit::chart_document_t& chart) -> std::size_t {
    std::size_t result{};
    for (const auto& segment : chart.lane_layouts)
        result = std::max(result, segment.layout.lanes.size());
    return result;
}

auto compose_bgm_muted_lanes(const wormedit::chart_document_t& chart,
    const bgm_lane_mix_state_t& mix) -> std::vector<std::uint64_t> {
    const auto any_solo = std::ranges::any_of(chart.bgm_source_lanes,
        [&](const auto& lane) { return mix.solo.contains(lane.id.value); });
    std::vector<std::uint64_t> muted;
    for (const auto& lane : chart.bgm_source_lanes)
        if (mix.muted.contains(lane.id.value) || (any_solo && !mix.solo.contains(lane.id.value)))
            muted.push_back(lane.id.value);
    std::ranges::sort(muted);
    return muted;
}

auto compose_editor_layout_width(double viewport_width, double editor_right) -> double {
    return std::isfinite(editor_right) && editor_right > 0.0
        ? std::min(viewport_width, editor_right) : viewport_width;
}

auto compose_horizontal_layout(double viewport_width_pixels, std::size_t lane_count,
                               bool has_bgm_lanes, std::size_t bga_lane_count,
                               std::uint8_t timing_lane_mask)
    -> compose_horizontal_layout_t {
    constexpr double bga_pane_width = 48.0;
    const auto desired_timing_right = compose_measure_rail_width_pixels +
        wormedit::ui::compose_timing_width(timing_lane_mask);
    const auto reserved_bga = bga_pane_width * std::min<std::size_t>(bga_lane_count, 4);
    const auto bga_left = std::clamp(
        viewport_width_pixels - compose_minimum_playable_field_width_pixels - reserved_bga,
        compose_measure_rail_width_pixels, desired_timing_right);
    const auto field_left = bga_left + reserved_bga;
    const auto available = std::max(1.0, viewport_width_pixels - field_left);
    auto playable = std::clamp(static_cast<double>(std::max<std::size_t>(lane_count, 1)) *
                                   compose_playable_lane_width_pixels,
                               compose_minimum_playable_field_width_pixels,
                               compose_maximum_playable_field_width_pixels);
    playable = std::min(playable, available);
    if (!has_bgm_lanes) {
        playable = std::min(playable, available);
        return {.bga_pane_left_pixels = bga_left,
                .field_left_pixels = field_left, .playable_field_width_pixels = playable,
                .bgm_pane_left_pixels = field_left + playable,
                .bga_pane_width_pixels = reserved_bga};
    }
    if (available > compose_minimum_bgm_pane_width_pixels)
        playable = std::min(playable, available - compose_minimum_bgm_pane_width_pixels);
    return {.bga_pane_left_pixels = bga_left,
            .field_left_pixels = field_left,
            .playable_field_width_pixels = std::max(1.0, playable),
            .bgm_pane_left_pixels = field_left + std::max(1.0, playable),
            .bgm_pane_width_pixels = std::max(0.0, available - playable),
            .bga_pane_width_pixels = reserved_bga};
}

auto has_beat_marker_navigation(const wormedit::chart_document_t& chart) -> bool {
    return std::ranges::any_of(chart.bookmarks,
                               [](const auto& bookmark) {
                                   return bookmark.anchor == wormedit::marker_anchor_t::beat;
                               }) ||
           std::ranges::any_of(chart.sections, [](const auto& section) {
               return section.anchor == wormedit::marker_anchor_t::beat;
           });
}

auto has_audio_marker_navigation(const wormedit::chart_document_t& chart) -> bool {
    return std::ranges::any_of(chart.bookmarks,
                               [](const auto& bookmark) {
                                   return bookmark.anchor == wormedit::marker_anchor_t::audio_time;
                               }) ||
           std::ranges::any_of(chart.sections, [](const auto& section) {
               return section.anchor == wormedit::marker_anchor_t::audio_time;
           });
}

auto timing_lane_kind_to_ui(wormedit::timing_lane_kind_t lane) -> WormUiTimingLaneKind {
    using kind_t = wormedit::timing_lane_kind_t;
    switch (lane) {
    case kind_t::bpm:
        return WORM_UI_TIMING_LANE_BPM;
    case kind_t::scroll:
        return WORM_UI_TIMING_LANE_SCROLL;
    case kind_t::speed:
        return WORM_UI_TIMING_LANE_SPEED;
    case kind_t::stop:
        return WORM_UI_TIMING_LANE_STOP;
    case kind_t::delay:
        return WORM_UI_TIMING_LANE_DELAY;
    case kind_t::warp:
        return WORM_UI_TIMING_LANE_WARP;
    case kind_t::kiai:
        return WORM_UI_TIMING_LANE_KIAI;
    case kind_t::anchor:
        return WORM_UI_TIMING_LANE_ANCHOR;
    }
    return WORM_UI_TIMING_LANE_BPM;
}

auto compose_physical_scancode(wormedit::physical_key_t key) -> std::uint32_t {
    using wormedit::physical_key_t;
    switch (key) {
    case physical_key_t::digit_1:
        return SDL_SCANCODE_1;
    case physical_key_t::digit_2:
        return SDL_SCANCODE_2;
    case physical_key_t::digit_3:
        return SDL_SCANCODE_3;
    case physical_key_t::digit_4:
        return SDL_SCANCODE_4;
    case physical_key_t::digit_5:
        return SDL_SCANCODE_5;
    case physical_key_t::digit_6:
        return SDL_SCANCODE_6;
    case physical_key_t::digit_7:
        return SDL_SCANCODE_7;
    case physical_key_t::digit_8:
        return SDL_SCANCODE_8;
    case physical_key_t::digit_9:
        return SDL_SCANCODE_9;
    case physical_key_t::digit_0:
        return SDL_SCANCODE_0;
    case physical_key_t::key_q:
        return SDL_SCANCODE_Q;
    case physical_key_t::key_w:
        return SDL_SCANCODE_W;
    case physical_key_t::key_e:
        return SDL_SCANCODE_E;
    case physical_key_t::key_r:
        return SDL_SCANCODE_R;
    case physical_key_t::key_t:
        return SDL_SCANCODE_T;
    case physical_key_t::key_y:
        return SDL_SCANCODE_Y;
    case physical_key_t::key_u:
        return SDL_SCANCODE_U;
    case physical_key_t::key_i:
        return SDL_SCANCODE_I;
    case physical_key_t::key_o:
        return SDL_SCANCODE_O;
    case physical_key_t::key_p:
        return SDL_SCANCODE_P;
    case physical_key_t::key_a:
        return SDL_SCANCODE_A;
    case physical_key_t::key_s:
        return SDL_SCANCODE_S;
    case physical_key_t::key_d:
        return SDL_SCANCODE_D;
    case physical_key_t::key_f:
        return SDL_SCANCODE_F;
    case physical_key_t::key_g:
        return SDL_SCANCODE_G;
    case physical_key_t::key_h:
        return SDL_SCANCODE_H;
    case physical_key_t::key_j:
        return SDL_SCANCODE_J;
    case physical_key_t::key_k:
        return SDL_SCANCODE_K;
    case physical_key_t::key_l:
        return SDL_SCANCODE_L;
    case physical_key_t::key_z:
        return SDL_SCANCODE_Z;
    case physical_key_t::key_x:
        return SDL_SCANCODE_X;
    case physical_key_t::key_c:
        return SDL_SCANCODE_C;
    case physical_key_t::key_v:
        return SDL_SCANCODE_V;
    case physical_key_t::key_b:
        return SDL_SCANCODE_B;
    case physical_key_t::key_n:
        return SDL_SCANCODE_N;
    case physical_key_t::key_m:
        return SDL_SCANCODE_M;
    case physical_key_t::unknown:
        return 0;
    }
    return 0;
}

auto requested_snap_denominator(const application_command_state_t& application_state,
                                wormedit::chart_id_t chart) -> std::uint64_t {
    const auto found = application_state.project.compose_view.requested_snap_denominators.find(chart.value);
    return found == application_state.project.compose_view.requested_snap_denominators.end() ? 4 : found->second;
}

auto compose_snap_preferences(const application_command_state_t& state, wormedit::chart_id_t chart)
    -> std::array<std::uint64_t, chart_snap_preferences.size() + 1> {
    std::array<std::uint64_t, chart_snap_preferences.size() + 1> result{};
    std::ranges::copy(chart_snap_preferences, result.begin());
    result.back() = requested_snap_denominator(state, chart);
    return result;
}

auto editor_interaction_has_pending_gesture(const wormedit::editor_interaction_state_t& state)
    -> bool {
    return wormedit::has_pending_editor_gesture(state);
}

namespace compose_projection_detail {

auto layer_assist_enabled(const application_command_state_t& app, wormedit::chart_id_t chart,
                          wormedit::layer_id_t layer) -> bool {
    return app.project.semantic_layer.layer_assist_chart == chart &&
           std::ranges::find(app.project.semantic_layer.layer_assist_layers, layer) != app.project.semantic_layer.layer_assist_layers.end();
}

auto append_semantic_layer_row(flattened_workspace_t& flattened, compose_scene_cache_t& cache,
                               const wormedit::semantic_layer_t& layer, bool selected, bool assist_tick) -> bool {
    auto* arena = flattened.arena();
    const auto* name = arena ? arena_string(*arena, layer.name) : nullptr;
    if (!name) return false;
    return flattened.compose.semantic_layer.view.push_back({
            .layer_id = layer.id.value,
            .name = name,
            .red = layer.color.red,
            .green = layer.color.green,
            .blue = layer.color.blue,
            .alpha = layer.color.alpha,
            .assigned_note_count = cache.frame.semantic_layer_membership_counts[layer.id.value],
            .visible = layer.visible,
            .locked = layer.locked,
            .selected = selected,
            .assigned_bgm_count = cache.frame.semantic_layer_bgm_membership_counts[layer.id.value],
            .assist_tick_enabled = assist_tick,
    });
}

void populate_semantic_membership_counts(compose_scene_cache_t& cache,
                                          const wormedit::chart_document_t& chart) {
    cache.frame.semantic_layer_membership_counts.clear();
    cache.frame.semantic_layer_bgm_membership_counts.clear();
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.semantic_layers.empty()) ++cache.frame.semantic_layer_membership_counts[0];
            for (const auto layer : note.semantic_layers) {
                auto& count = cache.frame.semantic_layer_membership_counts[layer.value];
                if (count != std::numeric_limits<std::size_t>::max()) ++count;
            }
        }
        for (const auto& event : measure.bgm_events) {
            if (event.semantic_layers.empty()) ++cache.frame.semantic_layer_bgm_membership_counts[0];
            for (const auto layer : event.semantic_layers) {
                auto& count = cache.frame.semantic_layer_bgm_membership_counts[layer.value];
                if (count != std::numeric_limits<std::size_t>::max()) ++count;
            }
        }
    }
}

void populate_note_layer_menu(flattened_workspace_t& flattened,
    const wormedit::chart_document_t& chart, std::uint64_t revision,
    application_command_state_t& app) {
    auto& view = flattened.view.compose;
    auto& rows = flattened.compose.semantic_layer.note_menu;
    view.note_menu_layer_total_count = chart.semantic_layers.size();
    if (app.project.semantic_layer.note_layer_menu_chart != chart.id || app.project.semantic_layer.note_layer_menu_revision != revision)
        app.project.semantic_layer.note_layer_menu_row_count = 0;
    if (!app.project.semantic_layer.note_layer_menu_row_count) { rows = {}; return; }
    const auto window = wormedit::semantic_layer_palette_window_t::from(chart,
        app.project.semantic_layer.note_layer_menu_first_row,
        std::min<std::size_t>(app.project.semantic_layer.note_layer_menu_row_count, WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY));
    if (!window || !prepare_compose_geometry_rows(flattened, rows, window->layer_indices.size())) return;
    app.project.semantic_layer.note_layer_menu_first_row = window->first_row;
    view.note_menu_layer_first_row = window->first_row;
    for (const auto index : window->layer_indices) {
        const auto& layer = chart.semantic_layers[index];
        const auto* name = flattened.arena() ? arena_string(*flattened.arena(), layer.name) : nullptr;
        if (!name) break;
        if (!rows.push_back({.layer_id = layer.id.value, .name = name,
            .red = layer.color.red, .green = layer.color.green, .blue = layer.color.blue,
            .alpha = layer.color.alpha, .visible = layer.visible, .locked = layer.locked})) break;
    }
    view.note_menu_layers = rows.data();
    view.note_menu_layer_count = rows.size();
}

bool populate_semantic_layer_palette(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    compose_scene_cache_t& cache, const wormedit::document_history_t& history,
    application_command_state_t& application_state) {
    auto& view = flattened.view.compose;
    if (!cache.is_scene_key_current(history.document, chart)) {
        populate_semantic_membership_counts(cache, chart);
    }
    if (application_state.project.semantic_layer.semantic_layer_palette_chart != chart.id) {
        application_state.project.semantic_layer.semantic_layer_palette_chart = chart.id;
        application_state.project.semantic_layer.semantic_layer_first_row = 0;
    }
    view.semantic_layer_assist_available = application_state.metronome_settings().available &&
                                           static_cast<bool>(application_state.services.transport_set_layer_assist);
    view.semantic_layer_target_none = application_state.project.semantic_layer.semantic_layer_target_none;
    view.semantic_layer_unassigned_note_count = cache.frame.semantic_layer_membership_counts[0];
    view.semantic_layer_unassigned_bgm_count =
        cache.frame.semantic_layer_bgm_membership_counts[0];
    const auto palette = wormedit::semantic_layer_palette_window_t::from(
        chart, application_state.project.semantic_layer.semantic_layer_first_row,
        std::clamp<std::size_t>(application_state.project.semantic_layer.semantic_layer_row_count, 1,
                               WORM_UI_COMPOSE_SEMANTIC_LAYER_WINDOW_CAPACITY));
    if (!palette) {
        report_compose_projection(flattened, projection_feedback_kind_t::layer_palette,
            wormedit::notification_severity_t::error, "The semantic-layer palette exceeds its UI limit.");
        return false;
    }
    application_state.project.semantic_layer.semantic_layer_first_row = palette->first_row;
    view.semantic_layer_first_row = palette->first_row;
    view.semantic_layer_total_count = palette->total_count;
    const auto target = std::ranges::find(chart.semantic_layers,
        application_state.project.semantic_layer.semantic_layer_target.value_or(wormedit::layer_id_t{}),
        &wormedit::semantic_layer_t::id);
    if (target != chart.semantic_layers.end()) {
        view.semantic_layer_target_row = static_cast<std::size_t>(target - chart.semantic_layers.begin());
        const auto* name = flattened.arena() ? arena_string(*flattened.arena(), target->name) : nullptr;
        view.semantic_layer_target = {
            .layer_id = target->id.value, .name = name,
            .red = target->color.red, .green = target->color.green,
            .blue = target->color.blue, .alpha = target->color.alpha,
            .assigned_note_count = cache.frame.semantic_layer_membership_counts[target->id.value],
            .visible = target->visible, .locked = target->locked, .selected = true,
            .assigned_bgm_count = cache.frame.semantic_layer_bgm_membership_counts[target->id.value],
            .assist_tick_enabled = layer_assist_enabled(application_state, chart.id, target->id),
        };
        populate_semantic_layer_target_state(view, application_state, *target, true);
    }
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.semantic_layer.view,
                                        palette->layer_indices.size())) {
        view.semantic_layers_truncated = true;
        view.semantic_layers = nullptr;
        return true;
    }
    view.semantic_layers = flattened.compose.semantic_layer.view.data();
    for (const auto layer_index : palette->layer_indices) {
        const auto& layer = chart.semantic_layers[layer_index];
        const auto selected = application_state.project.semantic_layer.semantic_layer_target == layer.id;
        if (!append_semantic_layer_row(flattened, cache, layer, selected,
                layer_assist_enabled(application_state, chart.id, layer.id))) {
            view.semantic_layers_truncated = true;
            break;
        }
        view.semantic_layer_count = flattened.compose.semantic_layer.view.size();
        populate_semantic_layer_target_state(view, application_state, layer, selected);
    }
    return true;
}


// " · transition 2 beats" (or seconds) for a SPEEDS event that interpolates; empty when instant.


// Kiai areas and the SV step graph as they sit in the current viewport. Both draw inside their
// timing lane, so only vertical geometry is projected here.


// Stored SV recipes as areas in the SV (sv/scrolls output) or SPD (speeds output) lane, each with
// its relative value as a line graph normalized to what is visible.

// SPEEDS transitions as spans in the SPD lane, those that reach the viewport.

// A preview belongs to one selected recipe at one document revision; anything else drops it.

// The form of the selected recipe as stored; the frontend keeps its own edit buffers on top.

// Fills the quantize dry-run counts; the cache recomputes only when the selection, snap or
// document changes, so idle frames copy three integers.


// Bounded so a huge chart cannot grow the per-frame projection without limit.

// One row per chart position: the authored measures, then the default 4/4 measures of the virtual
// tail (BMS: up to #999). A row's selection key is its position, which the resize command takes.


void populate_compose_selected_objects_and_drafts(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::document_history_t& history,
    application_command_state_t& application_state,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    WormUiComposeView& view, compose_scene_cache_t& cache) {
    if (application_state.project.compose_gesture.bmson_marker_draft &&
        !application_state.project.compose_gesture.bmson_marker_draft->stamp.is_current(
            history.document, chart.id, wormedit::document_identity_policy_t::revision_only)) {
        application_state.project.compose_gesture.bmson_marker_draft.reset();
        application_state.project.compose_gesture.bmson_razor_stroke.reset();
    }
    populate_compose_staged_draft(flattened, application_state, view);

    populate_compose_selected_bgm(chart, retained_state, view, cache);
    populate_compose_selected_note(flattened, chart, retained_state, view, cache);
}

void populate_compose_staged_draft(flattened_workspace_t& flattened,
                                   application_command_state_t& application_state,
                                   WormUiComposeView& view) {
    if (!application_state.project.compose_gesture.bmson_marker_draft) return;
    view.staged_bmson_marker_count = application_state.project.compose_gesture.bmson_marker_draft->payload.markers.size();
    flattened.compose.bmson_draft_playable_lane.clear();
    flattened.compose.bmson_draft_restart.clear();
    flattened.compose.bmson_draft_playable_lane.reserve(view.staged_bmson_marker_count);
    flattened.compose.bmson_draft_restart.reserve(view.staged_bmson_marker_count);
    for (const auto& marker : application_state.project.compose_gesture.bmson_marker_draft->payload.markers) {
        flattened.compose.bmson_draft_playable_lane.push_back(
            marker.playable_lane.value_or(wormedit::lane_id_t{}).value);
        flattened.compose.bmson_draft_restart.push_back(marker.restart ? 1 : 0);
    }
    view.staged_bmson_marker_playable_lanes = flattened.compose.bmson_draft_playable_lane.data();
    view.staged_bmson_marker_restarts = flattened.compose.bmson_draft_restart.data();
}


void populate_compose_selected_bgm(
    const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    WormUiComposeView& view, compose_scene_cache_t& cache) {
    view.selected_bgm_event_count = retained_state
        ? static_cast<std::size_t>(std::ranges::count_if(
              retained_state->selected_objects, [](const auto& object) {
                  return object.kind == wormedit::editor_selection_object_kind_t::bgm_event;
              }))
        : 0;
    if (retained_state->selected_objects.size() != 1 ||
        retained_state->selected_objects.front().kind !=
            wormedit::editor_selection_object_kind_t::bgm_event) return;
    const auto selected = retained_state->selected_objects.front().id;
    const auto* event = cached_selected_source(chart, selected, cache.scene.selected_bgm_source,
        &wormedit::measure_chunk_t::bgm_events, cache);
    if (!event) return;
    view.selected_bgm_event_id = selected.value;
    view.selected_assigned_resource_id = event->sound.value_or(wormedit::resource_id_t{}).value;
    if (chart.source_family == wormedit::format_family_t::bmson && event->sound_channel_event) {
        view.selected_bmson_bgm_event_id = selected.value;
        view.selected_bmson_source_lane_id = event->source_lane.value;
    }
}

void populate_compose_selected_note(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    WormUiComposeView& view, compose_scene_cache_t& cache) {
    if (retained_state->selected_notes.size() != 1) return;
    const auto note_id = retained_state->selected_notes.front();
    const auto* note = cached_selected_source(chart, note_id, cache.scene.selected_note_source,
        &wormedit::measure_chunk_t::notes, cache);
    if (!note) return;
    if (note->label) flattened.compose.selected_note_label = *note->label;
    view.selected_note_label = flattened.compose.selected_note_label.c_str();
    view.selected_note_label_note_id = note_id.value;
    view.selected_assigned_resource_id = note->sound.value_or(wormedit::resource_id_t{}).value;
    if (chart.source_family == wormedit::format_family_t::bmson && note->sound_channel_event &&
        note->kind == wormedit::note_kind_t::tap)
        view.selected_bmson_note_event_id = note_id.value;
}


auto cached_compose_measures(flattened_workspace_t& flattened,
                             const wormedit::chart_document_t& chart,
                             compose_scene_cache_t& cache) -> const wormedit::measure_index_t* {
    if (cache.scene.cached_measure_index) return &*cache.scene.cached_measure_index;
    auto measure_index = wormedit::build_measure_index(chart);
    if (!std::holds_alternative<wormedit::measure_index_t>(measure_index)) {
        const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(measure_index);
        report_compose_diagnostic(flattened, projection_feedback_kind_t::measure_geometry,
                                 diagnostics, "The chart measure geometry is invalid.");
        return nullptr;
    }
    cache.scene.cached_measure_index = std::move(std::get<wormedit::measure_index_t>(measure_index));
    return &*cache.scene.cached_measure_index;
}


const wormedit::measure_index_t* prepare_compose_chart_view(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    wormedit::document_history_t& history, compose_scene_cache_t& cache,
    application_command_state_t& application_state) {
    auto& view = flattened.view.compose;
    view.waveform_audio_offset_numerator = chart.beat_zero_audio_offset_seconds.numerator;
    view.waveform_audio_offset_denominator = chart.beat_zero_audio_offset_seconds.denominator;
    view.beat_marker_navigation_available = has_beat_marker_navigation(chart);
    view.audio_marker_navigation_available = has_audio_marker_navigation(chart);
    view.audio_marker_transport_available =
        static_cast<bool>(application_state.services.transport_audio_milliseconds) &&
        static_cast<bool>(application_state.services.transport_seek_audio_milliseconds) &&
        application_state.services.transport_audio_milliseconds().has_value();
    if (application_state.project.semantic_layer.semantic_layer_filter_chart != chart.id) {
        application_state.project.semantic_layer.semantic_layer_filter_chart = chart.id;
        application_state.project.semantic_layer.required_semantic_layer_filters.clear();
        application_state.project.semantic_layer.excluded_semantic_layer_filters.clear();
    }
    if (application_state.project.semantic_layer.semantic_layer_target &&
        std::ranges::find(chart.semantic_layers, *application_state.project.semantic_layer.semantic_layer_target,
                          &wormedit::semantic_layer_t::id) == chart.semantic_layers.end()) {
        application_state.project.semantic_layer.semantic_layer_target.reset();
        application_state.project.compose_gesture.semantic_layer_paint_mode_active = false;
        application_state.project.semantic_layer.semantic_layer_solo_active = false;
    }
    view.semantic_layer_paint_mode_active = application_state.project.compose_gesture.semantic_layer_paint_mode_active;
    view.semantic_layer_solo_active = application_state.project.semantic_layer.semantic_layer_solo_active;
    view.project_id = history.document.id.value;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    populate_note_layer_menu(flattened, chart, history.document.revision, application_state);
    if (!populate_semantic_layer_palette(flattened, chart, cache, history, application_state)) {
        return nullptr;
    }
    if (!cache.is_scene_key_current(history.document, chart)) {
        frame_phase_scope_t frame_scene_index(frame_phase_t::scene);
        cache.ensure_scene_key(history.document, chart);
        const auto built =
            wormedit::build_editor_field_scene_index(chart, history.document.revision);
        if (std::holds_alternative<wormedit::editor_field_scene_index_t>(built)) {
            cache.scene.index = std::get<wormedit::editor_field_scene_index_t>(built);
            cache.scene.bgm_rows_by_id.reserve(cache.scene.index->bgm_events.size());
            for (const auto& event : cache.scene.index->bgm_events) {
                cache.scene.bgm_rows_by_id.emplace(event.event.value, event.row);
            }
        } else {
            const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(built);
            cache.frame.projection_error = diagnostics.empty() ? "The field layout could not be indexed."
                                               : diagnostics.front().message;
            cache.frame.projection_error_severity = to_notification_severity(diagnostics);
        }
        if (cache.scene.index) {
            cache.timing.kiai_spans = wormedit::sys::kiai_spans_view_t::from(
                chart, cache.scene.index->measures, WORM_UI_COMPOSE_KIAI_SPAN_CAPACITY);
            cache.timing.sv_steps = wormedit::sys::sv_steps_view_t::from(
                chart, cache.scene.index->measures, WORM_UI_COMPOSE_SV_GRAPH_CAPACITY);
            cache.timing.recipe_areas = wormedit::sys::recipe_areas_view_t::from(
                chart, cache.scene.index->measures, WORM_UI_COMPOSE_RECIPE_AREA_CAPACITY);
        }
        auto timing = wormedit::timing_index_t::from(chart, {.allow_bounded_clock = true});
        if (timing.value) {
            cache.timing.dead_spans = wormedit::sys::dead_spans_view_t::from(
                chart, cache.scene.index->measures, *timing.value, WORM_UI_COMPOSE_DEAD_SPAN_CAPACITY);
            cache.timing.speed_spans = wormedit::sys::speed_spans_view_t::from(
                chart, *timing.value, WORM_UI_COMPOSE_SPEED_SPAN_CAPACITY);
            cache.timing.marker_timing_index = std::move(*timing.value);
        }
    }
    if (!cache.scene.index) {
        report_compose_projection(flattened, projection_feedback_kind_t::field_index,
                                 cache.frame.projection_error_severity, cache.frame.projection_error);
        return nullptr;
    }
    view.has_bgm_events = !cache.scene.index->bgm_events.empty();
    if (chart.measures.empty()) {
        report_compose_projection(flattened, projection_feedback_kind_t::empty_measures,
            wormedit::notification_severity_t::hint, "This difficulty has no measures to display.");
        return nullptr;
    }

    const auto* measures = cached_compose_measures(flattened, chart, cache);
    if (!measures || !populate_compose_bookmark_and_section_storage(flattened, chart, *measures, view))
        return nullptr;
    return measures;
}


bool initialize_compose_interaction(const compose_interaction_population_context_t& input) {
    auto& flattened = input.frame.flattened;
    auto& history = input.frame.history;
    const auto& chart = input.scope.chart;
    const auto& measures = input.scope.measures;
    auto& retained_state = input.frame.retained_state;
    auto& application_state = input.frame.application_state;
    auto& view = input.frame.view();
    const auto downscroll = input.downscroll;
    const auto initial_interaction = [&]() -> wormedit::editor_interaction_setup_t {
        std::optional<wormedit::local_position_t> initial_cursor;
        std::optional<wormedit::lane_id_t> initial_lane;
        std::uint64_t earliest_layout_row = std::numeric_limits<std::uint64_t>::max();
        for (const auto& segment : chart.lane_layouts) {
            const auto row = (measures).row_of(segment.position);
            if (!row || *row >= earliest_layout_row)
                continue;
            const auto lane = std::ranges::find_if(segment.layout.lanes, &wormedit::lane_definition_t::is_editable);
            if (lane == segment.layout.lanes.end())
                continue;
            earliest_layout_row = *row;
            initial_cursor = segment.position;
            initial_lane = lane->id;
        }
        if (!initial_cursor || !initial_lane) {
            return {};
        }
        return wormedit::editor_interaction_state_t::from(
            history, chart.id, *initial_lane, *initial_cursor, compose_snap_preferences(application_state, chart.id),
            requested_snap_denominator(application_state, chart.id), view.viewport_height_pixels);
    };
    if (!retained_state || retained_state->chart != chart.id) {
        const auto initial = initial_interaction();
        if (!initial.state || !initial.view) {
            report_compose_diagnostic(flattened, projection_feedback_kind_t::initial_interaction,
                                     initial.diagnostics, "This difficulty has no editable lanes to display.");
            return false;
        }
        retained_state = *initial.state;
        const auto initial_cursor_row = *(measures).row_of(retained_state->cursor);
        const auto desired_scroll =
            static_cast<double>(initial_cursor_row) * retained_state->pixels_per_standard_measure /
                chart.divisions_per_standard_measure -
            view.viewport_height_pixels * wormedit::editor_cursor_edge_margin_fraction;
        (void)wormedit::dispatch_editor_action(
            history, *retained_state,
            wormedit::scroll_editor_view_action_t{
                desired_scroll - retained_state->viewport_scroll_pixels,
                compose_cursor_screen_anchor(view.viewport_height_pixels, downscroll),
                downscroll ? wormedit::editor_field_scroll_direction_t::down
                           : wormedit::editor_field_scroll_direction_t::up});
    }

    return true;
}

bool update_compose_snap_preferences(const compose_interaction_population_context_t& input) {
    const auto& chart = input.scope.chart;
    auto& retained_state = input.frame.retained_state;
    auto& application_state = input.frame.application_state;
    auto& flattened = input.frame.flattened;
    // Keep a gesture's captured grid stable until the next idle frame.
    if (!editor_interaction_has_pending_gesture(*retained_state)) {
        const auto snap = wormedit::filter_snap_denominators(
            chart.divisions_per_standard_measure, compose_snap_preferences(application_state, chart.id),
            requested_snap_denominator(application_state, chart.id));
        if (!snap) {
            report_compose_projection(flattened, projection_feedback_kind_t::snap_configuration,
                wormedit::notification_severity_t::error, "The chart snap configuration is invalid.");
            return false;
        }
        retained_state->supported_snap_denominators = snap.supported_denominators;
        retained_state->active_snap_denominator = snap.active_denominator;
        retained_state->active_snap_was_fallback = snap.active_was_fallback;
        retained_state->snap_enabled = application_state.project.compose_view.edit_snap_enabled;
        retained_state->y_lock = application_state.project.compose_view.y_lock;
    }
    return true;
}

std::optional<wormedit::editor_interaction_view_model_t> inspect_compose_interaction(
    const compose_interaction_population_context_t& input) {
    auto& history = input.frame.history;
    auto& retained_state = input.frame.retained_state;
    auto& flattened = input.frame.flattened;
    auto& view = input.frame.view();
    if (!editor_interaction_has_pending_gesture(*retained_state))
        retained_state->scratch_on_right = input.frame.application_state.scratch_on_right;
    const auto geometry_changed = retained_state->viewport_height_pixels != view.viewport_height_pixels ||
        retained_state->scroll_direction != (input.downscroll ? wormedit::editor_field_scroll_direction_t::down
                                                            : wormedit::editor_field_scroll_direction_t::up);
    wormedit::configure_cursor_viewport(*retained_state, view.viewport_height_pixels,
        input.downscroll ? wormedit::editor_field_scroll_direction_t::down
                         : wormedit::editor_field_scroll_direction_t::up);
    if (geometry_changed && !editor_interaction_has_pending_gesture(*retained_state))
        (void)wormedit::synchronize_editor_cursor_to_viewport(
            input.scope.chart, input.scope.measures, *retained_state);
    // The one place selection is reconciled with the document: once per revision change.
    (void)wormedit::prune_stale_selection(input.scope.chart, history.document.revision,
                                          *retained_state);
    auto inspected =
        wormedit::inspect_editor_interaction(history, *retained_state,
                                             {.chart = input.scope.chart.id,
                                              .document_revision = history.document.revision,
                                              .index = &input.scope.measures});
    if (!std::holds_alternative<wormedit::editor_interaction_view_model_t>(inspected)) {
        auto recovered = wormedit::recover_editor_interaction(history, *retained_state);
        if (!recovered.state || !recovered.view) {
            retained_state.reset();
            report_compose_diagnostic(flattened, projection_feedback_kind_t::recover_interaction,
                                     recovered.diagnostics, "The chart field view could not be recovered.");
            return std::nullopt;
        }
        retained_state = std::move(*recovered.state);
        inspected = std::move(*recovered.view);
    }
    return std::get<wormedit::editor_interaction_view_model_t>(std::move(inspected));
}

std::optional<wormedit::editor_interaction_view_model_t> prepare_compose_interaction(
    const compose_interaction_population_context_t& input) {
    if (!initialize_compose_interaction(input) || !update_compose_snap_preferences(input))
        return std::nullopt;
    return inspect_compose_interaction(input);
}


WormUiComposeSelectionMode compose_scalar_selection_mode(
    wormedit::editor_area_selection_mode_t mode) {
    switch (mode) {
    case wormedit::editor_area_selection_mode_t::replace:
        return WORM_UI_COMPOSE_SELECTION_REPLACE;
    case wormedit::editor_area_selection_mode_t::add:
        return WORM_UI_COMPOSE_SELECTION_ADD;
    case wormedit::editor_area_selection_mode_t::toggle:
        return WORM_UI_COMPOSE_SELECTION_TOGGLE;
    }
    return WORM_UI_COMPOSE_SELECTION_REPLACE;
}

std::uint64_t prepare_compose_projection_state(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input) {
    auto& flattened = input.frame.flattened;
    const auto& history = input.frame.history;
    const auto& chart = input.scope.chart_scope.chart;
    auto& cache = input.frame.cache;
    auto& retained_state = input.frame.retained_state;
    auto& application_state = input.frame.application_state;
    auto& view = input.frame.view();
    auto& interaction_view = input.scope.interaction_view;
    const auto downscroll = input.scope.downscroll;
    build_compose_markers_view(input);
    if (application_state.project.transport.preview_playhead_absolute_row) {
        const auto pixels_per_row = input.scope.pixels_per_row;
        const auto playhead_y = compose_project_y(
            *application_state.project.transport.preview_playhead_absolute_row, pixels_per_row,
            interaction_view.viewport_scroll_pixels, view.viewport_height_pixels, downscroll);
        if (std::isfinite(playhead_y) && playhead_y >= 0.0 &&
            playhead_y <= view.viewport_height_pixels) {
            view.playhead_visible = true;
            view.playhead_y_pixels = playhead_y;
        }
    }
    populate_compose_selection_preview(cache, retained_state, interaction_view, view);
    populate_compose_selected_objects_and_drafts(flattened, chart, history,
                                                 application_state, retained_state, view, cache);
    auto canvas_rows = cache.scene.index->measures.total_rows;
    if (wormedit::has_chart_virtual_tail(chart)) {
        const auto built_canvas = wormedit::build_virtual_canvas(chart);
        if (const auto* canvas = std::get_if<wormedit::virtual_canvas_t>(&built_canvas))
            canvas_rows = canvas->total_rows;
    }
    const auto content_height = static_cast<double>(canvas_rows) *
                                interaction_view.pixels_per_standard_measure /
                                static_cast<double>(chart.divisions_per_standard_measure);
    const auto maximum_scroll = std::max(0.0, content_height -
        view.viewport_height_pixels * retained_state->viewport_end_margin_fraction);
    set_viewport_scroll_pixels(*retained_state, std::clamp(
        retained_state->viewport_scroll_pixels,
        -view.viewport_height_pixels * retained_state->viewport_minimum_margin_fraction,
        maximum_scroll));
    interaction_view.viewport_scroll_pixels = retained_state->viewport_scroll_pixels;
    view.area_canvas_rows = canvas_rows;
    view.area_rows_per_measure = chart.divisions_per_standard_measure;
    view.area_snap_denominator = retained_state->active_snap_denominator;
    view.edit_snap_enabled = retained_state->snap_enabled;
    view.y_lock = retained_state->y_lock;
    view.area_scroll_pixels = interaction_view.viewport_scroll_pixels;
    if (view.area_range_available) {
        retained_state->retained_canvas_selection_range = wormedit::canvas_row_span_t{
            view.area_range_begin_row, view.area_range_end_row};
    }
    if (retained_state->retained_canvas_selection_range) {
        const auto range = *retained_state->retained_canvas_selection_range;
        if (range.begin_row < range.end_row && range.end_row <= canvas_rows) {
            view.area_range_available = true;
            view.area_range_virtual = !retained_state->retained_selection_range.has_value();
            view.area_range_begin_row = range.begin_row;
            view.area_range_end_row = range.end_row;
        } else {
            retained_state->retained_canvas_selection_range.reset();
        }
        interaction_view.retained_canvas_selection_range = retained_state->retained_canvas_selection_range;
    }
    view.pixels_per_standard_measure = interaction_view.pixels_per_standard_measure;
    if (view.area_range_available && !view.area_range_virtual)
        view.area_delete_supported = compose_area_delete_supported(chart, cache.scene.index->measures, *retained_state);
    cache.frame.interaction_view = interaction_view;
    const auto hover_gesture_active =
        retained_state->pending_hold.has_value() ||
        retained_state->gesture<wormedit::pending_note_duration_gesture_t>() != nullptr ||
        retained_state->gesture<wormedit::pending_note_move_gesture_t>() != nullptr ||
        retained_state->pending_note_paint_gesture.has_value() ||
        retained_state->pending_semantic_layer_paint_gesture.has_value() ||
        retained_state->pending_editor_area_selection.has_value() ||
        retained_state->pending_space_range_selection.has_value();
    if (!hover_gesture_active && cache.frame.hover &&
        cache.frame.hover->document_revision == history.document.revision &&
        cache.frame.hover->object.kind == wormedit::editor_selection_object_kind_t::note) {
        view.hover_note_visible = true;
        view.hover_note_id = cache.frame.hover->object.id.value;
        flattened.compose.hover_tooltip = cache.frame.hover->tooltip;
        view.hover_tooltip = flattened.compose.hover_tooltip.c_str();
    }
    if (!hover_gesture_active && cache.frame.focus_note_label_editor &&
        retained_state->selected_notes.size() == 1) {
        view.focus_note_label_editor = true;
    }
    return canvas_rows;
}

void populate_compose_pending_previews(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input) {
    auto& retained_state = input.frame.retained_state;
    auto& view = input.frame.view();
    auto& interaction_view = input.scope.interaction_view;
    const auto& measures = input.scope.chart_scope.measures;
    const auto downscroll = input.scope.downscroll;
    if (interaction_view.pending_editor_area_selection) {
        const auto& preview = *interaction_view.pending_editor_area_selection;
        view.area_selection_preview_visible = true;
        view.area_selection_preview_mode =
            compose_scalar_selection_mode(retained_state->pending_editor_area_selection->mode);
        view.area_selection_preview_bounds = {
            std::clamp(preview.left_pixels, 0.0, view.viewport_width_pixels),
            std::clamp(downscroll ? view.viewport_height_pixels - preview.bottom_pixels
                                  : preview.top_pixels,
                       0.0, view.viewport_height_pixels),
            std::clamp(preview.right_pixels, 0.0, view.viewport_width_pixels),
            std::clamp(downscroll ? view.viewport_height_pixels - preview.top_pixels
                                  : preview.bottom_pixels,
                       0.0, view.viewport_height_pixels),
        };
    }
    if (retained_state->pending_space_range_selection && interaction_view.pending_space_range_selection) {
        const auto& pending = *retained_state->pending_space_range_selection;
        const auto cursor_row = retained_state->virtual_bms_cursor_row
                                    ? retained_state->virtual_bms_cursor_row
                                    : (measures).row_of(retained_state->cursor);
        if (cursor_row) {
            const auto pixels_per_row = input.scope.pixels_per_row;
            const auto& preview = *interaction_view.pending_space_range_selection;
            const auto first_row = preview.first_row;
            const auto last_row = preview.last_row;
            const auto first_y = compose_project_y(first_row, pixels_per_row,
                                                   interaction_view.viewport_scroll_pixels,
                                                   view.viewport_height_pixels, downscroll);
            const auto last_y = compose_project_y(last_row + (pending.minimum_span_rows == 0 ? 1 : 0), pixels_per_row,
                                                  interaction_view.viewport_scroll_pixels,
                                                  view.viewport_height_pixels, downscroll);
            const auto top = std::min(first_y, last_y);
            const auto bottom = std::max(first_y, last_y);
            view.space_range_preview_visible = true;
            view.space_range_preview_mode = compose_scalar_selection_mode(pending.mode);
            view.space_range_preview_bounds = {
                0.0,
                std::clamp(top, 0.0, view.viewport_height_pixels),
                view.viewport_width_pixels,
                std::clamp(bottom, 0.0, view.viewport_height_pixels),
            };
            const auto row_center = [&](std::uint64_t row) {
                return std::clamp(compose_project_y(
                                      static_cast<long double>(row) + 0.5L, pixels_per_row,
                                      interaction_view.viewport_scroll_pixels,
                                      view.viewport_height_pixels, downscroll),
                                  0.0, view.viewport_height_pixels);
            };
            view.space_range_anchor_y_pixels = row_center(pending.anchor_canvas_row);
            view.space_range_tail_y_pixels = row_center(
                pending.minimum_span_rows != 0 && pending.anchor_canvas_row == *cursor_row
                    ? last_row : *cursor_row);
        }
    }

}

auto compose_static_storage_bytes(const flattened_workspace_t& storage) -> std::size_t {
    const auto& data = storage.compose;
    // The independently owned arena's committed chunks include all span storage.
    // Count those chunks once, separately from the remaining heap containers.
    std::size_t bytes = sizeof(storage) + storage.owned_arena_committed_bytes() +
        compose_vector_bytes(data.timing_marker) + compose_vector_bytes(data.timing_marker_labels);
    for (const auto& label : data.timing_marker_labels)
        bytes += label.first.capacity() + label.second.capacity() + 2;
    // Include hash buckets, nodes, and owned labels conservatively.
    bytes += data.sound_labels.bucket_count() * sizeof(void*);
    for (const auto& [id, labels] : data.sound_labels)
        bytes += sizeof(id) + sizeof(labels) + 4 * sizeof(void*) +
                 labels.first.capacity() + labels.second.capacity() + 2;
    return bytes;
}
void publish_compose_static_source(compose_scene_cache_t& cache, WormUiComposeView& view) {
    if (!cache.scene.static_scene_storage) return;
    const auto& source = cache.scene.static_scene_storage->view.compose;
    const auto origin = view.downscroll
        ? -cache.scene.scene_window.view.viewport_scroll_pixels - source.viewport_height_pixels
        : cache.scene.scene_window.view.viewport_scroll_pixels;
    view.static_scene = &source;
    view.static_scene_source_generation = cache.scene.static_scene_source_generation;
    view.static_scene_world_origin_pixels = origin;
    // Source-window clipped objects cannot define complete tiles at its edges.
    double guard=96;
    for (std::size_t index=0; index<source.lane_count; ++index)
        guard=std::max(guard,(source.lanes[index].bounds.right-source.lanes[index].bounds.left)*.5+20);
    const bool chart_begin=cache.scene.scene_window.view.viewport_scroll_pixels==0;
    view.static_scene_covered_begin_pixels = origin + (chart_begin && !view.downscroll ? 0 : guard);
    view.static_scene_covered_end_pixels = origin + source.viewport_height_pixels -
        (chart_begin && view.downscroll ? 0 : guard);
    view.static_scene_accounted_bytes = cache.scene.static_scene_accounted_bytes +
        cache.scene.static_projection_index_bytes;
}
auto compose_static_source_layout_matches(const WormUiComposeView& source,
                                         const WormUiComposeView& view) -> bool {
    return source.project_id==view.project_id && source.chart_id==view.chart_id &&
        source.document_revision==view.document_revision &&
        source.viewport_width_pixels==view.viewport_width_pixels &&
        source.pixels_per_standard_measure==view.pixels_per_standard_measure &&
        source.playable_field_left_pixels==view.playable_field_left_pixels &&
        source.playable_field_right_pixels==view.playable_field_right_pixels &&
        source.bgm_horizontal_scroll_pixels==view.bgm_horizontal_scroll_pixels &&
        source.bga_lane_visible==view.bga_lane_visible &&
        source.bga_revealed_lane_mask==view.bga_revealed_lane_mask &&
        source.timing_lane_visibility_mask==view.timing_lane_visibility_mask &&
        source.timing_focus==view.timing_focus &&
        source.downscroll==view.downscroll && source.lane_key_legend_visible==view.lane_key_legend_visible;
}
void clear_static_source_selection(flattened_workspace_t& storage) {
    for (auto& note:storage.compose.note) note.selected=false;
    for (auto& hold:storage.compose.hold) hold.selected=false;
    for (auto& event:storage.compose.bgm.event) event.selected=false;
    for (auto& event:storage.compose.bga_event) event.selected=false;
    for (auto& marker:storage.compose.timing_marker) marker.selected=false;
}
auto compose_static_labels_fit(const wormedit::document_history_t& history) -> bool {
    std::size_t bytes{};
    for (const auto& resource:history.document.resources) {
        bytes+=resource.name.size()*2+64;
        for (const auto& identity:resource.target_identities) bytes+=identity.identifier.size();
        if (bytes>8*1024*1024) return false;
    }
    return true;
}
void discard_compose_static_source(compose_scene_cache_t& cache) {
    cache.scene.static_scene_storage.reset();
    cache.scene.static_projection_indices = {};
    cache.scene.static_projection_scratch = {};
    cache.scene.static_projection_source_generation = 0;
    cache.scene.static_projection_index_bytes = 0;
}
auto make_compose_static_view(const WormUiComposeView& view,
                              const wormedit::editor_interaction_view_model_t& expanded)
    -> WormUiComposeView {
    // Persistent scene storage retains scalar geometry/style identity only.
    // Every pointer is published from independently owned static projection data.
    WormUiComposeView retained{};
    retained.visible = view.visible;
    retained.scene_available = view.scene_available;
    retained.bms_measure_labels = view.bms_measure_labels;
    retained.bmson_tools_available = view.bmson_tools_available;
    retained.downscroll = view.downscroll;
    retained.bga_lane_visible = view.bga_lane_visible;
    retained.bga_revealed_lane_mask = view.bga_revealed_lane_mask;
    retained.lane_key_legend_visible = view.lane_key_legend_visible;
    retained.project_id = view.project_id;
    retained.chart_id = view.chart_id;
    retained.document_revision = view.document_revision;
    retained.static_scene_style_generation = view.static_scene_style_generation;
    retained.viewport_width_pixels = view.viewport_width_pixels;
    retained.bgm_horizontal_scroll_pixels = view.bgm_horizontal_scroll_pixels;
    retained.playable_field_left_pixels = view.playable_field_left_pixels;
    retained.playable_field_right_pixels = view.playable_field_right_pixels;
    retained.timing_lane_visibility_mask = view.timing_lane_visibility_mask;
    retained.timing_focus = view.timing_focus;
    retained.pixels_per_standard_measure = view.pixels_per_standard_measure;
    retained.viewport_height_pixels = expanded.viewport_height_pixels;
    retained.area_scroll_pixels = expanded.viewport_scroll_pixels;
    return retained;
}

void refresh_compose_static_source(const compose_scene_projection_context_t& projection,
                                  compose_scene_cache_t& cache, std::uint64_t canvas_rows) {
    auto& view = projection.inputs.frame.view();
    view.static_scene = nullptr;
    if (!cache.scene.scene_window.scene || view.truncated) {
        discard_compose_static_source(cache);
        return;
    }
    if (cache.scene.static_scene_storage &&
        cache.scene.static_scene_rebuild_count == cache.scene.scene_window.rebuild_count &&
        cache.scene.static_scene_source_style == cache.scene.static_scene_style_generation &&
        compose_static_source_layout_matches(cache.scene.static_scene_storage->view.compose,view)) {
        publish_compose_static_source(cache, view);
        return;
    }
    if (!compose_static_labels_fit(projection.history())) {
        discard_compose_static_source(cache);
        return;
    }
    discard_compose_static_source(cache);
    auto storage = std::make_shared<flattened_workspace_t>();
    auto& retained = storage->view.compose;
    retained = make_compose_static_view(view, cache.scene.scene_window.view);
    const auto static_frame = projection.inputs.frame.for_output(*storage);
    const auto scene_scope = projection.inputs.scope.for_view(
        std::as_const(cache.scene.scene_window.view), projection.index.measures);
    const auto static_context = compose_projection_context_t<>::from(static_frame, scene_scope);
    const compose_scene_projection_context_t expanded{static_context,
                                                      projection.index,
                                                      *cache.scene.scene_window.scene,
                                                      projection.field_left_pixels,
                                                      projection.playable_field_width,
                                                      false};
    populate_compose_sound_labels(expanded);
    build_compose_layout_and_notes_view(expanded);
    build_compose_bgm_view(expanded, cache);
    build_compose_bga_view(expanded);
    populate_compose_timing_markers(*storage, projection.history().document,
                                    projection.inputs.scope.chart_scope.chart, cache,
                                    cache.scene.scene_window.view, retained, projection.inputs.scope.downscroll,
                                    projection.inputs.scope.pixels_per_row, canvas_rows, nullptr);
    clear_static_source_selection(*storage);
    publish_compose_scene(expanded);
    const auto count = retained.band_count + retained.lane_count + retained.grid_line_count +
        retained.hold_body_count + retained.note_endpoint_count + retained.bgm_lane_count +
        retained.bgm_event_count + retained.bga_event_count + retained.timing_marker_count;
    const auto bytes = compose_static_storage_bytes(*storage);
    if (retained.truncated || count > WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY ||
        bytes > 16 * 1024 * 1024) {
        discard_compose_static_source(cache);
        return;
    }
    cache.scene.static_scene_storage = std::move(storage);
    cache.scene.static_scene_accounted_bytes = bytes;
    cache.scene.static_scene_rebuild_count = cache.scene.scene_window.rebuild_count;
    cache.scene.static_scene_source_style = cache.scene.static_scene_style_generation;
    ++cache.scene.static_scene_source_generation;
    publish_compose_static_source(cache, view);
}

void refresh_compose_static_scene_style(compose_scene_cache_t& cache,WormUiComposeView& view) {
    const auto& previous_style=cache.scene.scene_window.request;
    const auto& current_style=cache.frame.preview_scene_request;
    if (!cache.scene.scene_window.scene ||
        cache.scene.static_scene_lane_legend != view.lane_key_legend_visible ||
        previous_style.scratch_on_right != current_style.scratch_on_right ||
        previous_style.solo_semantic_layer!=current_style.solo_semantic_layer ||
        previous_style.required_semantic_layers!=current_style.required_semantic_layers ||
        previous_style.excluded_semantic_layers!=current_style.excluded_semantic_layers)
    {
        // A reload can recreate the cache while retaining native chart IDs and
        // revision. Epochs distinguish that new source from retained GPU tiles.
        static std::atomic<std::uint64_t> next_generation{1};
        cache.scene.static_scene_style_generation=next_generation.fetch_add(1,std::memory_order_relaxed);
    }
    cache.scene.static_scene_lane_legend=view.lane_key_legend_visible;
    view.static_scene_style_generation=cache.scene.static_scene_style_generation;
}

void refresh_scalar_projection_indices(compose_scene_cache_t& cache) {
    if (cache.scene.static_projection_source_generation == cache.scene.static_scene_source_generation)
        return;
    const auto& source = cache.scene.static_scene_storage->compose;
    const auto bounds = [](const auto& record) {
        return std::pair{record.bounds.top, record.bounds.bottom};
    };
    rebuild_scalar_intervals(cache.scene.static_projection_indices[0], source.note, bounds);
    rebuild_scalar_intervals(cache.scene.static_projection_indices[1], source.hold, bounds);
    rebuild_scalar_intervals(cache.scene.static_projection_indices[2], source.bgm.event, bounds);
    rebuild_scalar_intervals(cache.scene.static_projection_indices[4], source.bga_event, bounds);
    rebuild_scalar_intervals(
        cache.scene.static_projection_indices[3], source.grid.row,
        [](const auto& row) { return std::pair{double(row.whole_rows), double(row.whole_rows)}; });
    cache.scene.static_projection_scratch.resize(std::max(
        {source.note.size(), source.hold.size(), source.bgm.event.size(),
         source.bga_event.size(), source.grid.row.size()}));
    cache.scene.static_projection_index_bytes =
        cache.scene.static_projection_scratch.capacity() * sizeof(std::size_t);
    for (const auto& index : cache.scene.static_projection_indices)
        cache.scene.static_projection_index_bytes += index.accounted_bytes();
    cache.scene.static_projection_source_generation = cache.scene.static_scene_source_generation;
}
auto scalar_projection_query(compose_scene_cache_t& cache, unsigned pass, double begin, double end)
    -> std::optional<std::span<const std::size_t>> {
    const auto query = cache.scene.static_projection_indices[pass].query_into(
        begin, end, cache.scene.static_projection_scratch);
    cache.scene.static_projection_scanned += query.scanned;
    if (query.overflow)
        return std::nullopt;
    cache.scene.static_projection_extracted += query.count;
    return std::span<const std::size_t>(cache.scene.static_projection_scratch).first(query.count);
}

auto extract_scalar_grid(compose_scene_cache_t& cache,
                         const wormedit::editor_field_scene_view_t& scene,
                         flattened_workspace_t& flattened, double shift) -> bool {
    const auto indices = scalar_projection_query(cache, 3, double(scene.visible_begin_row),
                                                 double(scene.visible_end_row));
    if (!indices)
        return false;
    const auto& source = cache.scene.static_scene_storage->compose.grid;
    const auto visible = [&](std::size_t index) {
        return source.row[index].whole_rows >= scene.visible_begin_row &&
               source.row[index].whole_rows < scene.visible_end_row;
    };
    const auto count = static_cast<std::size_t>(std::count_if(indices->begin(), indices->end(), visible));
    if (!prepare_compose_grid_rows(flattened, count)) return false;
    for (const auto index : *indices) {
        if (source.row[index].whole_rows < scene.visible_begin_row ||
            source.row[index].whole_rows >= scene.visible_end_row)
            continue;
        auto line = source.line[index];
        line.y_pixels += shift;
        if (!append_compose_grid_row(flattened, line, source.row[index])) return false;
    }
    return true;
}
auto extract_scalar_objects(compose_scene_cache_t& cache,
                            const wormedit::editor_field_scene_window_metadata_view_t& metadata,
                            flattened_workspace_t& flattened,
                            const wormedit::editor_interaction_view_model_t& interaction,
                            double shift, double height) -> bool {
    const auto& source = cache.scene.static_scene_storage->compose;
    const auto& scene = *metadata.projection.scene;
    const auto& retained = *cache.scene.scene_window.scene;
    const auto note = [&](auto& record, std::size_t index) {
        record.selected = retained.note_endpoints[index].selected;
        record.display_lane_number = scalar_display_lane_number(record, scene);
        return record.bounds.bottom >= 0 && record.bounds.top <= height;
    };
    const auto hold = [&](auto& record, std::size_t index) {
        record.bounds.top = std::max(record.bounds.top, metadata.hold_clip_top);
        record.bounds.bottom = std::min(record.bounds.bottom, metadata.hold_clip_bottom);
        record.selected = retained.hold_bodies[index].selected;
        record.display_lane_number = scalar_display_lane_number(record, scene);
        return record.bounds.top < record.bounds.bottom;
    };
    const auto bgm = [&](auto& record, std::size_t index) {
        record.selected = retained.bgm_events[index].selected;
        record.head_top_pixels += static_cast<float>(shift);
        record.head_bottom_pixels += static_cast<float>(shift);
        return record.bounds.bottom >= 0 && record.bounds.top <= height;
    };
    const auto bga = [&](auto& record, std::size_t index) {
        (void)index;
        record.selected = compose_bga_event_selected(interaction, record.event_id);
        return record.bounds.bottom >= 0 && record.bounds.top <= height;
    };
    return extract_scalar_rectangles(cache, flattened, 0, source.note, flattened.compose.note, 0, height,
                                     shift, note) &&
           extract_scalar_rectangles(cache, flattened, 1, source.hold, flattened.compose.hold,
                                     metadata.hold_clip_top, metadata.hold_clip_bottom, shift,
                                     hold) &&
           extract_scalar_rectangles(cache, flattened, 2, source.bgm.event, flattened.compose.bgm.event, 0,
                                     height, shift, bgm) &&
           extract_scalar_rectangles(cache, flattened, 4, source.bga_event,
                                     flattened.compose.bga_event, 0, height, shift, bga) &&
           extract_scalar_grid(cache, scene, flattened, shift);
}
auto scalar_projection_fits_budget(const compose_scene_projection_context_t& projection,
                                   const wormedit::editor_field_scene_request_t& request) -> bool {
    const auto& scene = projection.scene;
    const auto& scalar = projection.inputs.frame.flattened.compose;
    const auto count = scene.layout_bands.size() + scene.lanes.size() + scene.bgm_lanes.size() +
                       std::size_t(scene.cursor.has_value()) + scalar.note.size() +
                       scalar.hold.size() + scalar.bgm.event.size() + scalar.bga_event.size() +
                       scalar.grid.line.size();
    return count <= request.maximum_scene_primitives &&
           scalar.note.size() <= WORM_UI_COMPOSE_NOTE_CAPACITY &&
           scalar.hold.size() <= WORM_UI_COMPOSE_HOLD_CAPACITY &&
           scalar.bgm.event.size() <= WORM_UI_COMPOSE_BGM_EVENT_CAPACITY &&
           scalar.bga_event.size() <= WORM_UI_COMPOSE_BGA_EVENT_CAPACITY &&
           scalar.grid.line.size() <= WORM_UI_COMPOSE_GRID_CAPACITY;
}
void discard_scalar_projection(flattened_workspace_t& flattened) {
    flattened.compose.band.clear();
    flattened.compose.lane.clear();
    flattened.compose.note.clear();
    flattened.compose.hold.clear();
    flattened.compose.bgm.lane.clear();
    flattened.compose.bgm.event.clear();
    flattened.compose.bga_event.clear();
    flattened.compose.grid.line.clear();
    flattened.compose.grid.row.clear();
    flattened.compose.cursor.reset();
}
auto reusable_scalar_source(const compose_scene_cache_t& cache, const WormUiComposeView& view)
    -> bool {
    if (!cache.scene.static_scene_storage || !cache.scene.scene_window.scene ||
        cache.scene.static_scene_source_style != cache.scene.static_scene_style_generation ||
        !compose_static_source_layout_matches(cache.scene.static_scene_storage->view.compose, view))
        return false;
    const auto& source = cache.scene.static_scene_storage->compose;
    const auto& retained = *cache.scene.scene_window.scene;
    return source.note.size() == retained.note_endpoints.size() &&
           source.hold.size() == retained.hold_bodies.size() &&
           source.bgm.event.size() == retained.bgm_events.size() &&
           source.bga_event.size() == retained.bga_events.size() &&
           source.grid.row.size() == retained.grid_lines.size();
}
auto build_reused_scalar_scene_view(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input, double field_left,
                                 double field_width, double pixels_per_row, bool downscroll,
                                 bool bindings_truncated) -> bool {
    auto& cache = input.frame.cache;
    auto& flattened = input.frame.flattened;
    auto& view = flattened.view.compose;
    cache.scene.static_projection_scanned = cache.scene.static_projection_extracted = 0;
    if (!reusable_scalar_source(cache, view))
        return false;
    const auto metadata = wormedit::editor_field_scene_window_metadata_view_t::from(
        input.scope.chart_scope.chart, *cache.scene.index, input.scope.interaction_view,
        cache.frame.preview_scene_request, cache.scene.scene_window);
    if (!metadata.projection)
        return false;
    refresh_scalar_projection_indices(cache);
    const auto shift = downscroll ? view.viewport_height_pixels -
                                        cache.scene.scene_window.view.viewport_height_pixels +
                                        input.scope.interaction_view.viewport_scroll_pixels -
                                        cache.scene.scene_window.view.viewport_scroll_pixels
                                  : cache.scene.scene_window.view.viewport_scroll_pixels -
                                        input.scope.interaction_view.viewport_scroll_pixels;
    const auto scene_scope = input.scope.for_view(
        std::as_const(input.scope.interaction_view), cache.scene.index->measures);
    const auto scene_context = compose_projection_context_t<>::from(input.frame, scene_scope);
    const compose_scene_projection_context_t projection{
        scene_context,
        *cache.scene.index,
        *metadata.projection.scene,
        field_left,
        field_width,
        bindings_truncated,
        input.frame.bga_move_preview,
        input.frame.retained_state ? wormedit::edit_snap_denominator(input.scope.chart_scope.chart,
                                                                     *input.frame.retained_state)
                                   : input.scope.chart_scope.chart.divisions_per_standard_measure};
    // Metadata contains no grid/objects; their immutable scalar records are extracted below.
    build_compose_layout_and_notes_view(projection);
    build_compose_bgm_view(projection, cache);
    build_compose_bga_view(projection);
    if (!extract_scalar_objects(cache, metadata, flattened, input.scope.interaction_view, shift,
                                view.viewport_height_pixels) ||
        !scalar_projection_fits_budget(projection, cache.frame.preview_scene_request) || view.truncated) {
        discard_scalar_projection(flattened);
        return false;
    }
    build_compose_marker_previews_view(projection);
    publish_compose_scene(projection);
    publish_compose_static_source(cache, view);
    view.scene_available = true;
    ++cache.scene.scene_window.reuse_count;
    ++cache.scene.static_projection_reused_frames;
    return true;
}

void prepare_compose_bgm_widths(const wormedit::chart_document_t& chart,
    std::uint64_t project, compose_scene_cache_t& cache, const application_command_state_t& application) {
    cache.scene.bgm_lane_widths_pixels.clear();
    cache.scene.bgm_lane_widths_pixels.reserve(chart.bgm_source_lanes.size());
    for (const auto& lane : chart.bgm_source_lanes) {
        const auto width = lane.stem_channel && application.services.compose_bgm_lane_width
            ? application.services.compose_bgm_lane_width(project, chart.id.value, lane.id.value) : 36.0;
        cache.scene.bgm_lane_widths_pixels.push_back(std::isnan(width) ? 36.0 : std::clamp(width, 36.0, 480.0));
    }
}
auto compose_has_bgm_pane(const chart_document_t& chart,
                           const editor_interaction_view_model_t& view) -> bool {
    return !chart.bgm_source_lanes.empty() || chart.source_family == format_family_t::bms ||
        chart.source_family == format_family_t::bmson || (view.gesture<wormedit::pending_note_move_gesture_t>() &&
        !view.gesture<wormedit::pending_note_move_gesture_t>()->new_bgm_lanes.empty());
}

bool compose_project_scene(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input,
                           std::uint64_t canvas_rows) {
    auto& flattened = input.frame.flattened;
    auto& history = input.frame.history;
    const auto& chart = input.scope.chart_scope.chart;
    auto& cache = input.frame.cache;
    auto& retained_state = input.frame.retained_state;
    auto& application_state = input.frame.application_state;
    auto& view = input.frame.view();
    view.note_kind_palette_visible = application_state.preferences.editor_preferences.note_kind_palette_visible;
    view.lane_key_legend_visible = application_state.preferences.editor_preferences.lane_key_legend_visible;
    view.audition_on_click = application_state.preferences.editor_preferences.audition_on_click;
    view.area_band_alpha = application_state.preferences.editor_preferences.area_band_alpha;
    view.armed_note_kind = static_cast<std::uint32_t>(retained_state->armed_note_kind);
    view.allowed_note_kind_mask = 0;
    for (unsigned kind = 0; kind <= static_cast<unsigned>(wormedit::note_kind_t::fake); ++kind)
        if (wormedit::is_note_kind_allowed(chart, static_cast<wormedit::note_kind_t>(kind)))
            view.allowed_note_kind_mask |= 1U << kind;
    auto& interaction_view = input.scope.interaction_view;
    const auto& timing_stage = input.frame.timing_stage;
    const auto downscroll = input.scope.downscroll;
    flattened.compose.interaction_view = interaction_view;
    view.same_sound_selection_available = compose_sound_selection_available(history.document, chart,
        *retained_state, application_state);
    view.move_to_bgm_available = compose_move_to_bgm_available(chart, *retained_state, application_state);
    view.bms_measure_labels = chart.source_family == wormedit::format_family_t::bms;
    view.bmson_tools_available = chart.source_family == wormedit::format_family_t::bmson;
    populate_lane_profile_choices(view, chart, interaction_view.cursor);
    populate_lane_layout_editor(
        view, chart, input.scope.chart_scope.measures, interaction_view.cursor,
        retained_state && retained_state->virtual_bms_cursor_row.has_value(),
        interaction_view.selected_canvas_measure);
    populate_measure_removal(view, history.document, chart, interaction_view.cursor,
                             retained_state && retained_state->virtual_bms_cursor_row.has_value());
    populate_lane_layout_segments(flattened, chart, input.scope.chart_scope.measures,
                                  interaction_view, downscroll, view);
    populate_quantize_preview(cache, history.document, chart, history.document.revision,
                              *retained_state, application_state, view);
    view.compose_measure_count = chart.measures.size();
    view.selected_bms_measure_number_plus_one =
        interaction_view.selected_canvas_measure
            ? static_cast<std::uint32_t>(*interaction_view.selected_canvas_measure + 1)
            : 0;
    std::size_t lane_key_binding_count{};
    bool lane_key_bindings_truncated{};
    for (const auto& binding : chart.input_profile.bindings) {
        const auto scancode = compose_physical_scancode(binding.key);
        if (scancode == 0) {
            continue;
        }
        if (lane_key_binding_count >= flattened.compose.lane_key.size()) {
            lane_key_bindings_truncated = true;
            break;
        }
        flattened.compose.lane_key[lane_key_binding_count++] = {
            .scancode = scancode,
            .lane_id = binding.lane.value,
        };
    }
    view.lane_key_bindings = flattened.compose.lane_key.data();
    view.lane_key_binding_count = lane_key_binding_count;
    view.lane_keys_unbound = chart.input_profile.bindings.empty();

    view.bga_lane_visible = interaction_view.bga_lane_visible &&
        wormedit::editor_bga_visible_lane_mask(chart, interaction_view.bga_revealed_lane_mask) != 0;
    const auto horizontal_layout =
        compose_horizontal_layout(compose_editor_layout_width(view.viewport_width_pixels,
                                      application_state.project.compose_view.compose_editor_right_pixels),
                                  maximum_layout_lane_count(chart),
                                  compose_has_bgm_pane(chart, interaction_view),
                                  interaction_view.bga_lane_visible ? std::popcount(
                                      wormedit::editor_bga_visible_lane_mask(chart,
                                          interaction_view.bga_revealed_lane_mask)) : 0,
                                  effective_compose_timing_lane_mask(application_state));
    const auto bgm_pane_width = horizontal_layout.bgm_pane_width_pixels;
    const auto bga_pane_width = horizontal_layout.bga_pane_width_pixels;
    const auto playable_field_width = horizontal_layout.playable_field_width_pixels;
    const auto field_left_pixels = horizontal_layout.field_left_pixels;
    view.playable_field_left_pixels = field_left_pixels;
    view.playable_field_right_pixels = field_left_pixels + playable_field_width;

    const auto pixels_per_row = input.scope.pixels_per_row;
    populate_compose_timing_markers(flattened, history.document, chart, cache, interaction_view, view,
                                    downscroll, pixels_per_row, canvas_rows, timing_stage);
    populate_compose_dead_spans(flattened, cache, interaction_view, view, downscroll,
                                pixels_per_row);
    populate_compose_kiai_and_sv(flattened, cache, interaction_view, view, downscroll,
                                 pixels_per_row, canvas_rows, application_state.project.timing_recipe.kiai_selected_head);
    populate_compose_speed_spans(flattened, cache, interaction_view, view, downscroll, pixels_per_row);
    drop_stale_recipe_preview(application_state, chart, history.document.revision);
    populate_compose_recipe_areas(flattened, chart, cache, interaction_view, view, downscroll,
                                  pixels_per_row, canvas_rows, application_state.project.timing_recipe.recipe_selected,
                                  application_state.project.timing_recipe.recipe_preview ? &*application_state.project.timing_recipe.recipe_preview
                                                                   : nullptr);
    populate_compose_recipe_form(flattened, chart, application_state, view);
    prepare_compose_bgm_widths(chart, history.document.id.value, cache, application_state);
    const auto bgm_content = wormedit::editor_bgm_content_width_pixels(chart,
        {.bgm_lane_widths_pixels = cache.scene.bgm_lane_widths_pixels});
    const auto preview_lane_count = interaction_view.gesture<wormedit::pending_note_move_gesture_t>()
        ? interaction_view.gesture<wormedit::pending_note_move_gesture_t>()->new_bgm_lanes.size() : 0;
    const auto maximum_bgm_scroll = std::max(0.0,
        bgm_content + preview_lane_count * compose_bgm_lane_width_pixels - bgm_pane_width);
    auto& bgm_horizontal_scroll = application_state.project.compose_view.bgm_horizontal_scroll_pixels[chart.id.value];
    bgm_horizontal_scroll = std::clamp(bgm_horizontal_scroll, 0.0, maximum_bgm_scroll);
    view.bgm_horizontal_scroll_pixels = bgm_horizontal_scroll;
    view.bgm_maximum_horizontal_scroll_pixels = maximum_bgm_scroll;
    cache.frame.preview_scene_request = {
        .field_left_pixels = field_left_pixels,
        .field_width_pixels = playable_field_width,
        .scratch_on_right = interaction_view.scratch_on_right,
        .split_stage_gutter_pixels = 14.0,
        .note_head_height_pixels = 16.0,
        .scroll_direction = downscroll
            ? wormedit::editor_field_scroll_direction_t::down
            : wormedit::editor_field_scroll_direction_t::up,
        .fixed_cursor_screen_y_pixels = retained_state->fixed_cursor_screen_y_pixels,
        .bgm_pane_left_pixels = horizontal_layout.bgm_pane_left_pixels,
        .bgm_pane_width_pixels = bgm_pane_width,
        .bgm_lane_width_pixels = compose_bgm_lane_width_pixels,
        .bgm_lane_widths_pixels = cache.scene.bgm_lane_widths_pixels,
        .bgm_horizontal_scroll_pixels = bgm_horizontal_scroll,
        .bga_pane_left_pixels = horizontal_layout.bga_pane_left_pixels,
        .bga_pane_width_pixels = bga_pane_width,
        .include_virtual_bms_tail = wormedit::has_chart_virtual_tail(chart),
        .solo_semantic_layer = application_state.project.semantic_layer.semantic_layer_solo_active
                                   ? application_state.project.semantic_layer.semantic_layer_target
                                   : std::nullopt,
        .required_semantic_layers = application_state.project.semantic_layer.required_semantic_layer_filters,
        .excluded_semantic_layers = application_state.project.semantic_layer.excluded_semantic_layer_filters,
        .maximum_grid_lines = WORM_UI_COMPOSE_GRID_CAPACITY,
        .maximum_scene_primitives = WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY,
        // One physical 512-row tile at scale >= .5, plus conservative glyph halo.
        .retained_window_minimum_margin_pixels =
            1024.0 + std::max(96.0, playable_field_width * .5 + 20.0)};
    cache.frame.preview_scene_request.comparison_difference_rows.assign(
        input.frame.comparison_rows.begin(), input.frame.comparison_rows.end());
    refresh_compose_static_scene_style(cache,view);
    const auto frame_scene_previous = frame_timing().enter(frame_phase_t::scene);
    if (build_reused_scalar_scene_view(input, field_left_pixels, playable_field_width, pixels_per_row,
                                    downscroll, lane_key_bindings_truncated)) {
        populate_quantize_ghost_geometry(cache, history.document, chart, application_state,
            *retained_state, interaction_view, view, pixels_per_row, downscroll);
        frame_timing().restore(frame_scene_previous);
        return true;
    }
    const auto prior_window_work=cache.scene.scene_window.rebuild_count+cache.scene.scene_window.reuse_count;
    const auto composed = wormedit::editor_field_scene_view_t::from(
        chart, *cache.scene.index, interaction_view, cache.frame.preview_scene_request,
        cache.scene.scene_window);
    frame_timing().restore(frame_scene_previous);
    if (!composed.scene || composed.has_errors()) {
        report_compose_diagnostic(flattened, projection_feedback_kind_t::field_scene,
                                 composed.diagnostics, "The chart field could not be composed.");
        return false;
    }
    const auto& scene = *composed.scene;
    view.scene_available = true;
    populate_quantize_ghost_geometry(cache, history.document, chart, application_state,
        *retained_state, interaction_view, view, pixels_per_row, downscroll);
    const auto scene_scope = input.scope.for_view(
        std::as_const(interaction_view), cache.scene.index->measures);
    const auto scene_context = compose_projection_context_t<>::from(input.frame, scene_scope);
    const compose_scene_projection_context_t projection{
        scene_context,
        *cache.scene.index,
        scene,
        field_left_pixels,
        playable_field_width,
        lane_key_bindings_truncated,
        input.frame.bga_move_preview,
        retained_state ? wormedit::edit_snap_denominator(chart, *retained_state)
                       : chart.divisions_per_standard_measure};
    populate_compose_sound_labels(projection);
    build_compose_layout_and_notes_view(projection);
    build_compose_bgm_view(projection, cache);
    build_compose_bga_view(projection);
    build_compose_marker_previews_view(projection);
    publish_compose_scene(projection);
    if (cache.scene.scene_window.rebuild_count+cache.scene.scene_window.reuse_count != prior_window_work)
        refresh_compose_static_source(projection, cache, canvas_rows);
    else {
        view.static_scene=nullptr;
        discard_compose_static_source(cache);
    }
    return true;
}

void append_area_object_geometry(wormedit::editor_area_geometry_snapshot_t& snapshot,
    wormedit::editor_selection_object_t object, WormUiComposeRect bounds,
    const WormUiComposeView& view, bool downscroll) {
    const auto first = compose_source_y(bounds.top, view.viewport_height_pixels, downscroll);
    const auto last = compose_source_y(bounds.bottom, view.viewport_height_pixels, downscroll);
    snapshot.objects.push_back({object, bounds.left, std::min(first, last), bounds.right,
                                std::max(first, last)});
}

auto area_timing_kind(WormUiTimingLaneKind lane)
    -> std::optional<wormedit::editor_selection_object_kind_t> {
    using kind = wormedit::editor_selection_object_kind_t;
    switch (lane) {
    case WORM_UI_TIMING_LANE_BPM: return kind::bpm_event;
    case WORM_UI_TIMING_LANE_SCROLL: return kind::scroll_event;
    case WORM_UI_TIMING_LANE_SPEED: return kind::speed_event;
    case WORM_UI_TIMING_LANE_STOP: return kind::stop_event;
    case WORM_UI_TIMING_LANE_DELAY: return kind::delay_event;
    case WORM_UI_TIMING_LANE_WARP: return kind::warp_event;
    case WORM_UI_TIMING_LANE_KIAI: return kind::kiai_event;
    case WORM_UI_TIMING_LANE_ANCHOR: return kind::timing_anchor;
    case WORM_UI_TIMING_LANE_COUNT: return std::nullopt;
    }
    return std::nullopt;
}

void append_area_timing_geometry(wormedit::editor_area_geometry_snapshot_t& snapshot,
                                const WormUiComposeView& view, bool downscroll) {
    for (std::size_t i = 0; i < view.timing_marker_count; ++i) {
        const auto& marker = view.timing_markers[i];
        const auto kind = area_timing_kind(marker.lane);
        if (!kind || marker.object_id == 0) continue;
        const auto bounds = wormedit::ui::compose_timing_marker_bounds(view, marker);
        if (bounds.right <= bounds.left) continue;
        append_area_object_geometry(snapshot, {*kind, wormedit::object_id_t{marker.object_id}},
                                     bounds, view, downscroll);
    }
}

void append_area_visual_geometry(wormedit::editor_area_geometry_snapshot_t& snapshot,
                                 const WormUiComposeView& view, bool downscroll) {
    for (const auto& event : std::span(view.bga_events, view.bga_event_count))
        append_area_object_geometry(snapshot,
            {wormedit::editor_selection_object_kind_t::bga_event, wormedit::object_id_t{event.event_id}},
            event.bounds, view, downscroll);
    for (const auto& marker : std::span(view.beat_bookmarks, view.beat_bookmark_count)) {
        if (!marker.projection_visible) continue;
        append_area_object_geometry(snapshot,
            {wormedit::editor_selection_object_kind_t::bookmark, wormedit::object_id_t{marker.bookmark_id}},
            {0, marker.projected_y_pixels, view.viewport_width_pixels, marker.projected_y_pixels},
            view, downscroll);
    }
    for (const auto& section : std::span(view.beat_sections, view.beat_section_count)) {
        if (!section.span_projection_visible) continue;
        append_area_object_geometry(snapshot,
            {wormedit::editor_selection_object_kind_t::section, wormedit::object_id_t{section.section_id}},
            {0, section.projected_span_top_y_pixels, view.viewport_width_pixels,
                section.projected_span_bottom_y_pixels}, view, downscroll);
    }
}

void cache_area_object_geometry(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input) {
    auto& cache = input.frame.cache;
    const auto& view = input.frame.view();
    if (!view.scene_available) {
        cache.scene.area_object_geometry.reset();
        return;
    }
    auto snapshot = std::make_shared<wormedit::editor_area_geometry_snapshot_t>();
    snapshot->chart = input.scope.chart_scope.chart.id;
    snapshot->document_revision = input.frame.history.document.revision;
    snapshot->projection = wormedit::editor_area_geometry_projection_t{
        input.scope.interaction_view.viewport_scroll_pixels,
        input.scope.interaction_view.pixels_per_standard_measure,
        input.scope.interaction_view.viewport_height_pixels};
    for (std::size_t i = 0; i < view.bgm_event_count; ++i) {
        const auto& event = view.bgm_events[i];
        append_area_object_geometry(*snapshot,
                                    {wormedit::editor_selection_object_kind_t::bgm_event,
                                     wormedit::object_id_t{event.event_id}},
                                    event.bounds, view, input.scope.downscroll);
    }
    append_area_timing_geometry(*snapshot, view, input.scope.downscroll);
    append_area_visual_geometry(*snapshot, view, input.scope.downscroll);
    cache.scene.area_object_geometry = std::move(snapshot);
}

// Pasted timing objects have no scene geometry; they are projected straight onto their lane.
auto paste_timing_ghost_lane(const wormedit::chart_clipboard_payload_t& object)
    -> std::optional<WormUiTimingLaneKind> {
    using namespace wormedit;
    if (std::holds_alternative<bpm_event_t>(object)) return WORM_UI_TIMING_LANE_BPM;
    if (const auto* pause = std::get_if<pause_event_t>(&object))
        return pause->kind == pause_kind_t::stop ? WORM_UI_TIMING_LANE_STOP
                                                 : WORM_UI_TIMING_LANE_DELAY;
    if (std::holds_alternative<warp_event_t>(object)) return WORM_UI_TIMING_LANE_WARP;
    if (std::holds_alternative<scroll_event_t>(object)) return WORM_UI_TIMING_LANE_SCROLL;
    if (std::holds_alternative<speed_event_t>(object)) return WORM_UI_TIMING_LANE_SPEED;
    if (std::holds_alternative<kiai_event_t>(object)) return WORM_UI_TIMING_LANE_KIAI;
    if (std::holds_alternative<timing_anchor_t>(object)) return WORM_UI_TIMING_LANE_ANCHOR;
    return std::nullopt;
}

auto paste_timing_ghost_span(const wormedit::chart_clipboard_payload_t& object)
    -> std::pair<wormedit::local_position_t, wormedit::local_position_t> {
    using namespace wormedit;
    if (const auto* warp = std::get_if<warp_event_t>(&object))
        return {warp->span.begin, warp->span.end};
    return std::visit([](const auto& value) -> std::pair<local_position_t, local_position_t> {
        if constexpr (requires { value.position; }) return {value.position, value.position};
        else return {};
    }, object);
}

void append_paste_timing_ghosts(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input,
                                application_command_state_t::paste_session_t& session) {
    const auto pixels_per_row = input.scope.pixels_per_row;
    if (!(pixels_per_row > 0.0)) return;
    const auto height = input.frame.view().viewport_height_pixels;
    const auto project = [&](wormedit::local_position_t position) -> std::optional<double> {
        const auto row = (input.scope.chart_scope.measures).row_of(position);
        if (!row)
            return std::nullopt;
        return compose_project_y(*row, pixels_per_row,
                                 input.scope.interaction_view.viewport_scroll_pixels, height,
                                 input.scope.downscroll);
    };
    const auto preview = wormedit::chart_paste_preview_view_t::from(session.proposal);
    if (!preview.report) return;
    for (const auto& object : preview.report->objects) {
        const auto lane = paste_timing_ghost_lane(object);
        if (!lane) continue;
        const auto [begin, end] = paste_timing_ghost_span(object);
        const auto begin_y = project(begin);
        const auto end_y = project(end);
        if (!begin_y || !end_y) continue;
        const auto top = std::min(*begin_y, *end_y);
        const auto bottom = std::max(*begin_y, *end_y);
        if (bottom < 0.0 || top > height) continue;
        session.ghosts.push_back({{0.0, top, 0.0, bottom}, 3, static_cast<std::uint8_t>(*lane)});
    }
}

void populate_compose_paste_projection(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input) {
    auto& application = input.frame.application_state;
    if (!input.frame.retained_state || !application.project.clipboard.paste_session)
        return;
    const auto anchor =
        paste_anchor(*application.project.clipboard.paste_session, input.frame.retained_state->cursor);
    if (!revise_compose_paste(application, input.frame.history.document,
                              input.scope.chart_scope.chart.id, anchor))
        return;
    auto& session = *application.project.clipboard.paste_session;
    auto& view = input.frame.view();
    view.paste_active = true;
    const auto preview = wormedit::chart_paste_preview_view_t::from(session.proposal);
    view.paste_valid = session.anchor_available && session.proposal.value.has_value();
    view.paste_diagnostic = session.anchor_available
        ? (preview.diagnostic ? preview.diagnostic->message.c_str() : "")
        : session.anchor_error.c_str();
    session.ghosts.clear();
    if (!session.index) return;
    const auto composed = wormedit::editor_field_scene_view_t::from(
        session.geometry, *session.index, input.scope.interaction_view,
        input.frame.cache.frame.preview_scene_request);
    if (!composed.scene) return;
    const auto append = [&session](const auto& object, std::uint8_t kind, std::uint8_t glyph = 0) {
        const auto& b = object.bounds;
        session.ghosts.push_back({{b.left, b.top, b.right, b.bottom}, kind, glyph});
    };
    for (const auto& hold : composed.scene->hold_bodies) append(hold, 1);
    for (const auto& note : composed.scene->note_endpoints)
        append(note, 0, static_cast<std::uint8_t>(note.glyph));
    for (const auto& event : composed.scene->bgm_events) append(event, 2);
    append_paste_timing_ghosts(input, session);
    view.paste_ghosts = session.ghosts.data();
    view.paste_ghost_count = session.ghosts.size();
}

auto compose_paint_head_occupied(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input,
    const wormedit::editor_hit_target_t& target, std::uint64_t row) -> bool {
    auto& cache = input.frame.cache;
    // Successful scene indices contain every head; cap/geometry failures return
    // diagnostics rather than a truncated index. Never use the visible window.
    if (cache.scene.index && cache.is_scene_key_current(input.frame.history.document, input.scope.chart_scope.chart,
            compose_scene_key_policy_t::paint_occupancy) &&
        cache.scene.index->note_indices_by_id.size() == cache.scene.index->notes.size()) {
        const auto& notes = cache.scene.index->notes;
        auto note = std::ranges::lower_bound(notes, row, {},
            &wormedit::editor_field_scene_note_source_t::begin_row);
        for (; note != notes.end() && note->begin_row == row; ++note) {
            ++cache.scene.paint_occupancy_scanned;
            if (note->lane == target.lane) return true;
        }
        return false;
    }
    // Exact fallback still restricts the head check to its authored measure.
    const auto measure = input.scope.chart_scope.chart.find_measure(target.position.measure);
    if (measure == nullptr)
        return false;
    for (const auto& note : measure->notes) {
        ++cache.scene.paint_occupancy_scanned;
        if (note.lane == target.lane && note.position == target.position) return true;
    }
    return false;
}

void populate_compose_paint_ghost(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input) {
    using namespace wormedit;
    auto& view = input.frame.view();
    auto& cache = input.frame.cache;
    view.paint_ghost_visible = false;
    if (!view.note_paint_mode_active || !cache.frame.paint_pointer || !input.frame.retained_state ||
        editor_interaction_has_pending_gesture(*input.frame.retained_state))
        return;
    if (!cache.scene.hit_test_index) {
        auto built = build_editor_hit_test_index(input.scope.chart_scope.chart,
                                                 input.frame.history.document.revision);
        if (!std::holds_alternative<editor_hit_test_index_t>(built)) return;
        cache.scene.hit_test_index = std::get<editor_hit_test_index_t>(std::move(built));
    }
    const auto [x, y] = *cache.frame.paint_pointer;
    const auto target = resolve_compose_paint_cell(
        input.scope.chart_scope.chart, *cache.scene.hit_test_index, input.scope.interaction_view, x, y,
        view.playable_field_left_pixels,
        view.playable_field_right_pixels - view.playable_field_left_pixels, input.scope.downscroll,
        edit_snap_denominator(input.scope.chart_scope.chart, *input.frame.retained_state));
    if (!target) return;
    const auto row = (input.scope.chart_scope.measures).row_of(target->position);
    if (!row) return;
    const auto source_y = static_cast<double>(*row) *
                              input.scope.interaction_view.pixels_per_standard_measure /
                              input.scope.chart_scope.chart.divisions_per_standard_measure -
                          input.scope.interaction_view.viewport_scroll_pixels;
    const auto ghost_y = input.scope.downscroll ? view.viewport_height_pixels - source_y : source_y;
    const auto lanes = std::span(view.lanes, view.lane_count);
    const auto lane = std::ranges::find_if(lanes, [&](const auto& value) {
        return value.lane_id == target->lane.value && ghost_y >= value.bounds.top &&
               ghost_y <= value.bounds.bottom;
    });
    if (lane == lanes.end()) return;
    const auto occupied = compose_paint_head_occupied(input, *target, *row);
    const auto rectangular = lane->glyph <= 1;
    const auto height = cache.frame.preview_scene_request.note_head_height_pixels;
    const auto top =
        rectangular ? (input.scope.downscroll ? ghost_y - height : ghost_y) : ghost_y - height / 2;
    view.paint_ghost = {
        .bounds = {lane->bounds.left, top, lane->bounds.right, top + height},
        .lane_id = lane->lane_id,
        .display_lane_number = lane->display_order + 1,
        .key_color = lane->key_color,
        .glyph = lane->glyph,
        .snap_class = wormedit::ui::note_snap_class(
            target->position.row, input.scope.chart_scope.chart.divisions_per_standard_measure),
        .kind = static_cast<std::uint8_t>(view.armed_note_kind),
        .paint_erase_preview = occupied,
        .paint_invalid_preview = !is_note_kind_allowed(input.scope.chart_scope.chart,
                                                    input.frame.retained_state->armed_note_kind)};
    view.paint_ghost_visible = true;
}

void populate_compose_bgm_mix(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input) {
    const auto found =
        input.frame.application_state.project.bgm.bgm_lane_mix.find(input.scope.chart_scope.chart.id.value);
    const auto* mix =
        found == input.frame.application_state.project.bgm.bgm_lane_mix.end() ? nullptr : &found->second;
    const auto muted = mix ? compose_bgm_muted_lanes(input.scope.chart_scope.chart, *mix)
                           : std::vector<std::uint64_t>{};
    input.frame.view().bgm_mix_available =
        static_cast<bool>(input.frame.application_state.services.transport_set_bgm_mix);
    for (auto& lane : input.frame.flattened.compose.bgm.lane) {
        lane.muted = mix && mix->muted.contains(lane.source_lane_id);
        lane.solo = mix && mix->solo.contains(lane.source_lane_id);
        lane.mix_silent = std::ranges::binary_search(muted, lane.source_lane_id);
    }
}

void populate_compose_final_projection(const compose_projection_context_t<wormedit::editor_interaction_view_model_t>& input) {
    input.frame.view().clipboard_copy_available =
        input.frame.retained_state &&
        !editor_interaction_has_pending_gesture(*input.frame.retained_state) &&
        (!input.frame.retained_state->selected_objects.empty() ||
         !input.frame.retained_state->selected_notes.empty() ||
         input.frame.application_state.project.timing_recipe.kiai_selected_head != 0);
    input.frame.view().clipboard_paste_available =
        input.frame.application_state.project.clipboard.chart_clipboard.has_value() && input.frame.retained_state &&
        !editor_interaction_has_pending_gesture(*input.frame.retained_state);
    const auto rows = prepare_compose_projection_state(input);
    populate_compose_pending_previews(input);
    (void)compose_project_scene(input, rows);
    populate_compose_bgm_mix(input);
    populate_compose_paste_projection(input);
    cache_area_object_geometry(input);
    populate_compose_paint_ghost(input);
}

void populate_bgm_only_compose_projection(
    flattened_workspace_t& flattened, wormedit::document_history_t& history,
    const wormedit::chart_document_t& chart, const wormedit::measure_index_t& measures,
    compose_scene_cache_t& cache, application_command_state_t& application_state,
    WormUiComposeView& view, bool downscroll,
    const wormedit::timing_lane_pseudonote_outcome_t* timing_stage) {
    std::optional<wormedit::editor_interaction_state_t> projection_state{
        std::in_place};
    projection_state->chart = chart.id;
    wormedit::set_cursor_scale(*projection_state, view.pixels_per_standard_measure);
    wormedit::configure_cursor_viewport(*projection_state, view.viewport_height_pixels,
        downscroll ? wormedit::editor_field_scroll_direction_t::down
                   : wormedit::editor_field_scroll_direction_t::up);
    projection_state->viewport_minimum_margin_fraction =
        wormedit::editor_cursor_edge_margin_fraction;
    projection_state->viewport_end_margin_fraction =
        wormedit::editor_cursor_edge_margin_fraction;
    projection_state->active_snap_denominator = 1;
    const auto cached_view_matches = cache.frame.interaction_view &&
        cache.frame.interaction_view->chart == chart.id &&
        cache.frame.interaction_view->document_revision == history.document.revision;
    const auto viewport_scroll_pixels = cached_view_matches
        ? cache.frame.interaction_view->viewport_scroll_pixels
        : -view.viewport_height_pixels * wormedit::editor_cursor_edge_margin_fraction;
    wormedit::restore_cursor_pose(*projection_state, {
        .cursor = {measures.entries.front().id, 0},
        .viewport_scroll_pixels = viewport_scroll_pixels,
        .fixed_cursor_screen_y_pixels = compose_cursor_screen_anchor(view.viewport_height_pixels, downscroll),
        .scroll_direction = downscroll ? wormedit::editor_field_scroll_direction_t::down
                                      : wormedit::editor_field_scroll_direction_t::up,
    });

    wormedit::editor_interaction_view_model_t interaction_view{};
    interaction_view.chart = chart.id;
    interaction_view.cursor = projection_state->cursor;
    interaction_view.active_snap_denominator = projection_state->active_snap_denominator;
    interaction_view.document_revision = history.document.revision;
    interaction_view.pixels_per_standard_measure = projection_state->pixels_per_standard_measure;
    interaction_view.viewport_scroll_pixels = projection_state->viewport_scroll_pixels;
    interaction_view.viewport_height_pixels = projection_state->viewport_height_pixels;
    interaction_view.viewport_minimum_margin_fraction = projection_state->viewport_minimum_margin_fraction;
    interaction_view.viewport_end_margin_fraction = projection_state->viewport_end_margin_fraction;
    interaction_view.scroll_direction = projection_state->scroll_direction;
    const std::span<const wormedit::local_position_t> comparison_rows{};
    const compose_bga_move_preview_input_t* bga_move_preview{};
    compose_frame_inputs_t frame{flattened,        history,           cache,
                                 projection_state, application_state, timing_stage,
                                 comparison_rows,  bga_move_preview};
    const auto final_scope = compose_chart_scope_t<wormedit::editor_interaction_view_model_t>::from(
        {chart, measures},
        interaction_view,
        downscroll,
        compose_pixels_per_row(chart, interaction_view));
    const auto final_context = compose_projection_context_t<wormedit::editor_interaction_view_model_t>::from(frame, final_scope);
    const auto rows = prepare_compose_projection_state(final_context);
    populate_compose_pending_previews(final_context);
    (void)compose_project_scene(final_context, rows);
    populate_compose_bgm_mix(final_context);
    view.clipboard_copy_available = false;
    view.clipboard_paste_available = false;
    view.paste_active = false;
    view.paste_valid = false;
    view.paste_ghosts = nullptr;
    view.paste_ghost_count = 0;
    view.allowed_note_kind_mask = 0;
    view.note_paint_mode_active = false;
    view.semantic_layer_paint_mode_active = false;
}

void build_loop_selection_controls_view(WormUiComposeView& view,
                                     const application_command_state_t& application) {
    view.transport_loop_selection_enabled = application.services.transport_loop_selection_active &&
                                            application.services.transport_loop_selection_active();
    view.transport_loop_selection_available =
        static_cast<bool>(application.services.transport_set_loop_selection) &&
        (view.transport_loop_selection_enabled ||
         (view.scene_available && view.area_range_available && !view.area_range_virtual &&
          view.area_range_begin_row < view.area_range_end_row));
}

void bind_branch_statement_metadata(const wormedit::bms_control_node_t& node,
                                    frame_span<WormUiComposeBranchRowView>& rows,
                                    std::size_t& row_index) {
    while (row_index < rows.size() &&
           (!rows[row_index].has_source_order ||
            rows[row_index].source_order < node.source_order))
        ++row_index;
    if (row_index < rows.size() && rows[row_index].has_source_order &&
        rows[row_index].source_order == node.source_order) {
        auto& row = rows[row_index++];
        if (node.kind == wormedit::bms_control_kind_t::raw_statement) {
            row.statement_id = node.id.value;
            row.statement_text = node.original_text.c_str();
            row.statement_text_bytes = node.original_text.size();
        }
    }
    for (const auto& child : node.children)
        bind_branch_statement_metadata(child, rows, row_index);
}

void populate_branch_statement_metadata(flattened_workspace_t& flattened,
                                       const wormedit::chart_document_t& chart) {
    if (!chart.bms_control_tree) return;
    auto& rows = flattened.compose.branch.row;
    std::size_t row_index{};
    for (const auto& child : chart.bms_control_tree->children)
        bind_branch_statement_metadata(child, rows, row_index);
}

void build_branch_statement_feedback_view(WormUiComposeView& view,
                                      application_command_state_t& application_state,
                                      wormedit::chart_id_t chart,
                                      std::uint64_t revision) {
    auto& feedback = application_state.project.feedback.bms_branch_statement_feedback;
    if (!feedback || feedback->chart != chart || feedback->revision != revision) {
        feedback.reset();
        return;
    }
    if (feedback->message.empty()) return;
    constexpr auto capacity = WORM_UI_COMPOSE_BRANCH_DIAGNOSTIC_CAPACITY;
    if (view.branch_diagnostic_count < capacity) {
        view.branch_diagnostics[view.branch_diagnostic_count++] = feedback->message.c_str();
        return;
    }
    view.branch_diagnostics[capacity - 1] = feedback->message.c_str();
    view.branch_diagnostics_truncated = true;
}

} // namespace compose_projection_detail

auto default_song_bar_hidden(const wormedit::song_document_t& document,
                             std::optional<wormedit::chart_id_t> chart) -> bool {
    if (!chart) return false;
    const auto selected = document.find_chart(*chart);
    return selected != nullptr &&
        (selected->source_family == wormedit::format_family_t::bms ||
         selected->source_family == wormedit::format_family_t::bmson);
}

auto projected_pointer_hold_active(const std::optional<wormedit::editor_interaction_state_t>& state)
    -> bool {
    return state && (state->gesture<wormedit::pending_note_duration_gesture_t>() ||
        (state->pending_hold && state->pending_hold->gesture_source ==
            wormedit::hold_gesture_source_t::alt_pointer));
}

void populate_compose_view(flattened_workspace_t& flattened,
                           wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compose_scene_cache_t& cache, int window_width, int window_height,
                           std::optional<wormedit::editor_interaction_state_t>& retained_state,
                           application_command_state_t& application_state,
                           const wormedit::timing_lane_pseudonote_outcome_t* timing_stage,
                           std::span<const wormedit::local_position_t> comparison_rows,
                           const compose_bga_move_preview_input_t* bga_move_preview) {
    compose_frame_inputs_t frame{flattened,       history,           cache,
                                 retained_state,  application_state, timing_stage,
                                 comparison_rows, bga_move_preview};

    if (!workspace.selected_chart || !application_state.project.feedback.bms_branch_statement_feedback ||
        application_state.project.feedback.bms_branch_statement_feedback->chart != *workspace.selected_chart ||
        application_state.project.feedback.bms_branch_statement_feedback->revision != history.document.revision)
        application_state.project.feedback.bms_branch_statement_feedback.reset();
    cache.scene.area_object_geometry.reset();
    if (!initialize_compose_view_state(flattened, cache, window_width, window_height,
                                       default_song_bar_hidden(history.document, workspace.selected_chart),
                                       retained_state, application_state)) {
        return;
    }
    auto& view = flattened.view.compose;
    view.editor_gesture_active = retained_state && has_pending_editor_gesture(*retained_state);
    view.editor_gesture_current = view.editor_gesture_active &&
        !is_editor_gestures_stale(*retained_state, history.document.revision);
    view.editor_pointer_hold_active = projected_pointer_hold_active(retained_state);
    if (!workspace.selected_chart) {
        report_compose_projection(flattened, projection_feedback_kind_t::choose_chart,
            wormedit::notification_severity_t::hint, "Choose a difficulty to display its chart field.", 0,
            wormedit::notification_source_t::input);
        return;
    }
    const auto chart_it = history.document.find_chart(*workspace.selected_chart);
    if (chart_it == nullptr) {
        report_compose_projection(flattened, projection_feedback_kind_t::missing_chart,
            wormedit::notification_severity_t::warning, "The selected difficulty is no longer available.");
        return;
    }
    const auto& chart = *chart_it;
    populate_draw_stem_sources(flattened, cache, history.document, chart);
    populate_draw_source_label(flattened, cache, history.document);
    populate_bgm_draw_hint(view, chart);
    populate_branch_statement_metadata(flattened, chart);
    build_branch_statement_feedback_view(view, application_state, chart.id, history.document.revision);
    view.bgm_column_count = static_cast<std::uint32_t>(chart.bgm_source_lanes.size());
    view.bgm_column_count_editable =
        chart.source_family == wormedit::format_family_t::bms ||
        chart.source_family == wormedit::format_family_t::osu_mania;
    const auto downscroll = compose_field_downscroll(chart, application_state.preferences.editor_preferences);
    view.downscroll = downscroll;
    const auto measure_data = prepare_compose_chart_view(flattened, chart, history, cache,
                                                         application_state);
    if (!measure_data) return;
    view.audio_offset_editable = !application_state.audio_offset_edits_locked() &&
        (!retained_state || !editor_interaction_has_pending_gesture(*retained_state));
    const auto& measures = *measure_data;
    if (chart.lane_layouts.empty() && !chart.bgm_source_lanes.empty()) {
        populate_bgm_only_compose_projection(flattened, history, chart, measures, cache,
                                             application_state, view, downscroll, timing_stage);
        populate_note_stem_sources(flattened, cache, history.document);
        build_loop_selection_controls_view(view, application_state);
        return;
    }
    const wormedit::chart_scope_t chart_scope{chart, measures};
    const compose_interaction_population_context_t interaction_context{frame, chart_scope,
                                                                       downscroll};
    auto interaction_data = prepare_compose_interaction(interaction_context);
    if (!interaction_data) return;
    auto interaction_view = std::move(*interaction_data);
    const auto final_scope = compose_chart_scope_t<wormedit::editor_interaction_view_model_t>::from(
        {chart, measures},
        interaction_view,
        downscroll,
        compose_pixels_per_row(chart, interaction_view));
    const auto final_context = compose_projection_context_t<wormedit::editor_interaction_view_model_t>::from(frame, final_scope);
    populate_compose_final_projection(final_context);
    populate_note_stem_sources(flattened, cache, history.document);
    build_loop_selection_controls_view(view, application_state);
}

auto toggle_compose_audio_offset_lock(const WormUiWorkspaceAction& action,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application, compose_scene_cache_t& cache,
    std::optional<wormedit::proposal_t<wormedit::audio_offset_drag_t>>& drag,
    std::optional<wormedit::workspace_tab_t>& drag_tab) -> std::string {
    if (workspace.active_tab != wormedit::workspace_tab_t::compose || !workspace.selected_chart)
        return "Audio offset lock ignored because the chart or document revision changed.";
    if (!wormedit::chart_stamp_t{
            .document = {.project = history.document.id, .revision = action.compose_input_document_revision},
            .chart = wormedit::chart_id_t{action.compose_input_chart_id}}
             .is_current(history.document, *workspace.selected_chart))
        return wormedit::stale_action_message("Audio offset lock");
    application.project.timing_recipe.audio_offset_locked = !application.project.timing_recipe.audio_offset_locked;
    if (application.project.timing_recipe.audio_offset_locked) {
        drag.reset();
        drag_tab.reset();
        cache.timing.offset_fit_drag.reset();
        cache.timing.proposed_offset_timing_index.reset();
        cache.timing.proposed_offset_seconds.reset();
    }
    return application.project.timing_recipe.audio_offset_locked ? "Audio offset locked." : "Audio offset unlocked.";
}

} // namespace wormedit::sys
