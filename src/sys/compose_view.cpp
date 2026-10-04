#include "sys/frame_timing.h"
#include "sys/compose_view.h"
#include "sys/recipe_form.h"
#include <wormedit/core/sv_recipe.h>
#include "ui/nuklear/arrow_glyph.h"
#include "sys/compose_area_actions.h"
#include "sys/compose_resource_presenter.h"

#include <wormedit/core/editor_scene.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/lane_profile_switch.h>
#include <wormedit/core/lane_profiles.h>
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

namespace {
void report_compose_projection(flattened_workspace_t& flattened, projection_feedback_kind_t producer,
    wormedit::notification_severity_t severity, std::string message, std::uint64_t object = 0,
    wormedit::notification_source_t source = wormedit::notification_source_t::document) {
    flattened.projection_feedback.push_back({producer, object, severity, source, std::move(message)});
}

void report_compose_diagnostic(flattened_workspace_t& flattened, projection_feedback_kind_t producer,
    const wormedit::diagnostic_list_t& diagnostics, const char* fallback) {
    report_compose_projection(flattened, producer, projection_diagnostic_severity(diagnostics),
        diagnostics.empty() ? std::string{fallback} : diagnostics.front().message);
}
} // namespace

auto resolve_compose_paint_cell(
    const wormedit::chart_document_t& chart, const wormedit::editor_hit_test_index_t& index,
    const wormedit::editor_interaction_view_model_t& view, double x, double y,
    double field_left, double field_width, bool downscroll, std::uint64_t snap,
    const dead_span_projection_t* dead_spans)
    -> std::optional<wormedit::editor_hit_target_t> {
    auto hit_view = view;
    hit_view.active_snap_denominator = snap;
    auto target = wormedit::hit_test_editor(chart, index, hit_view,
        {.x_pixels = x, .y_pixels = downscroll ? view.viewport_height_pixels - y : y,
         .field_left_pixels = field_left, .field_width_pixels = field_width});
    if (!target) return std::nullopt;
    if (!target->note && dead_spans) {
        auto bands = project_dead_span_bands(*dead_spans, view.pixels_per_standard_measure /
            chart.divisions_per_standard_measure, view.viewport_scroll_pixels,
            view.viewport_height_pixels, downscroll, dead_span_pause_band_pixels);
        std::erase_if(bands, [](const auto& band) { return band.kind == dead_span_kind_t::warp; });
        if (pointer_in_dead_span(bands, y)) return std::nullopt;
    }
    const auto snapped = wormedit::snap_position(chart, index.measures, target->position, snap);
    if (!snapped) return std::nullopt;
    target->position = *snapped;
    return target;
}

namespace {
auto paste_anchor(application_command_state_t::paste_session_t& session,
                  wormedit::local_position_t cursor) -> wormedit::local_position_t {
    if (session.pointer_cursor && *session.pointer_cursor == cursor) return session.anchor;
    session.pointer_cursor.reset();
    return cursor;
}

auto paste_geometry(const wormedit::chart_document_t& source,
                    const wormedit::chart_paste_proposal_t& proposal) -> wormedit::chart_document_t {
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
    for (const auto& object : proposal.objects) {
        // Ghost previews draw notes and BGM events; pasted timing objects have no ghost.
        std::visit([&chart](const auto& value) {
            using value_t = std::decay_t<decltype(value)>;
            if constexpr (std::is_same_v<value_t, wormedit::note_t> ||
                          std::is_same_v<value_t, wormedit::bgm_event_t>) {
                const auto found = std::ranges::find(chart.measures, value.position.measure,
                                                     &wormedit::measure_chunk_t::id);
                if (found == chart.measures.end()) return;
                if constexpr (std::is_same_v<value_t, wormedit::note_t>)
                    found->notes.push_back(value);
                else found->bgm_events.push_back(value);
            }
        }, object);
    }
    return chart;
}
} // namespace

auto update_compose_paste(application_command_state_t& application_state,
    const wormedit::song_document_t& document, wormedit::chart_id_t chart,
    wormedit::local_position_t anchor) -> bool {
    auto& session = application_state.paste_session;
    if (!session) return false;
    if (session->proposal.chart != chart || session->proposal.revision != document.revision) {
        session.reset();
        return false;
    }
    if (session->anchor == anchor && session->index) return true;
    const auto source = std::ranges::find(document.charts, chart, &wormedit::chart_document_t::id);
    if (source == document.charts.end()) { session.reset(); return false; }
    session->anchor = anchor;
    session->anchor_available = true;
    session->proposal = wormedit::propose_chart_paste(document, chart, session->clipboard,
                                                    anchor, document.revision);
    session->geometry = paste_geometry(*source, session->proposal);
    const auto indexed = wormedit::build_editor_field_scene_index(session->geometry, document.revision);
    const auto* index = std::get_if<wormedit::editor_field_scene_index_t>(&indexed);
    session->index = index ? std::optional{*index} : std::nullopt;
    return true;
}

auto begin_compose_paste(application_command_state_t& application_state,
    const wormedit::song_document_t& document, const wormedit::editor_interaction_state_t& state,
    std::string& status) -> bool {
    if (!application_state.chart_clipboard || editor_interaction_has_pending_gesture(state)) {
        status = "Paste requires a copied selection and no pending gesture.";
        return false;
    }
    application_command_state_t::paste_session_t session;
    session.clipboard = *application_state.chart_clipboard;
    session.proposal.chart = state.chart;
    session.proposal.revision = document.revision;
    application_state.note_paint_mode_active = false;
    application_state.semantic_layer_paint_mode_active = false;
    application_state.paste_session = std::move(session);
    const bool accepted = update_compose_paste(application_state, document, state.chart, state.cursor);
    status = accepted ? "Paste preview: click or Enter to commit; Escape to cancel."
                      : "The paste chart is unavailable.";
    return accepted;
}

auto commit_compose_paste(application_command_state_t& application_state,
    wormedit::document_history_t& history, wormedit::editor_interaction_state_t& state,
    std::string& status) -> bool {
    const auto anchor = application_state.paste_session
        ? paste_anchor(*application_state.paste_session, state.cursor) : state.cursor;
    if (!update_compose_paste(application_state, history.document, state.chart, anchor)) {
        status = "The paste preview is stale.";
        return false;
    }
    auto& session = *application_state.paste_session;
    if (!session.anchor_available) { status = session.anchor_error; return false; }
    if (!session.proposal.valid) { status = session.proposal.error; return false; }
    const auto applied = wormedit::paste_chart_clipboard(history, state.chart, session.clipboard,
                                                       session.anchor, session.proposal.revision);
    if (!applied.applied) {
        status = applied.diagnostics.empty() ? "Paste failed." : applied.diagnostics.front().message;
        return false;
    }
    application_state.paste_session.reset();
    status = "Selection pasted.";
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


namespace {
auto known_mania_key_profile(std::string_view id) -> bool {
    constexpr std::string_view prefix{"osu-mania-"};
    if (!id.starts_with(prefix) || !id.ends_with('k')) return false;
    const auto digits = id.substr(prefix.size(), id.size() - prefix.size() - 1);
    unsigned keys{};
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), keys);
    return parsed.ec == std::errc{} && parsed.ptr == digits.data() + digits.size() && keys >= 1 && keys <= 18;
}
} // namespace
// Switchable profiles per lane count, built once: the frame path must not allocate.
// Only the UI thread builds frames, so the cache needs no lock.
static const std::vector<wormedit::lane_input_profile_t>& cached_lane_profile_choices(std::size_t lanes) {
    static std::array<std::optional<std::vector<wormedit::lane_input_profile_t>>, 19> cache;
    static const std::vector<wormedit::lane_input_profile_t> none;
    if (lanes >= cache.size()) return none;
    if (!cache[lanes]) cache[lanes] = wormedit::lane_profile_choices(lanes);
    return *cache[lanes];
}

// Fills the switchable-profile list for the layout segment active at the cursor.
static void populate_lane_profile_choices(WormUiComposeView& view, const wormedit::chart_document_t& chart,
                                   wormedit::local_position_t cursor) {
    view.lane_profile_choice_count = 0;
    view.lane_profile_current = -1;
    const auto* segment = wormedit::active_lane_layout_segment(chart, cursor);
    if (!segment) return;
    const auto& choices = cached_lane_profile_choices(segment->layout.lanes.size());
    const auto count = std::min<std::size_t>(choices.size(), WORM_UI_COMPOSE_LANE_PROFILE_CAPACITY);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& layout = choices[index].layout;
        const auto* prefix = layout.profile_id.starts_with("stepmania:") ? "StepMania " : "osu!mania ";
        std::snprintf(view.lane_profile_names[index], sizeof(view.lane_profile_names[index]),
                      "%s%s", prefix, layout.display_name.c_str());
        if (layout.profile_id == segment->layout.profile_id)
            view.lane_profile_current = static_cast<std::int8_t>(index);
    }
    // The switcher only covers osu!mania/StepMania key modes. A layout outside it (BMS DP7,
    // custom maps) must not show as the first choice, where one click would convert it.
    if (view.lane_profile_current < 0) return;
    view.lane_profile_choice_count = static_cast<std::uint8_t>(count);
}

auto compose_mode_family(const wormedit::lane_layout_t& layout) -> wormedit::editor_mode_family_t;

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

void populate_lane_layout_editor(WormUiComposeView& view, const wormedit::chart_document_t& chart,
                                 const wormedit::measure_index_t& measures,
                                 wormedit::local_position_t cursor, bool virtual_cursor) {
    view.lane_layout_profile_choice_count = 0;
    view.lane_layout_profile_current = -1;
    view.lane_layout_edit_segment_id = 0;
    view.lane_layout_edit_is_update = false;
    view.lane_layout_edit_available = !virtual_cursor;
    const auto measure = std::ranges::find(measures.entries, cursor.measure,
                                           &wormedit::measure_index_entry_t::id);
    if (measure == measures.entries.end()) {
        view.lane_layout_edit_available = false;
        return;
    }
    const auto measure_number = static_cast<std::size_t>(measure - measures.entries.begin()) + 1;
    view.lane_layout_edit_measure_number = static_cast<std::uint32_t>(
        std::min<std::size_t>(measure_number, std::numeric_limits<std::uint32_t>::max()));
    const auto segment = std::ranges::find_if(chart.lane_layouts, [&](const auto& value) {
        return value.position.measure == cursor.measure && value.position.row == 0;
    });
    if (segment != chart.lane_layouts.end()) {
        view.lane_layout_edit_segment_id = segment->id.value;
        view.lane_layout_edit_is_update = true;
    }
    const auto* active = wormedit::active_lane_layout_segment(chart, cursor);
    const auto mode = active ? compose_mode_family(active->layout) : wormedit::editor_mode_family_t::other;
    const auto& choices = cached_lane_layout_profile_choices(chart.source_family, mode);
    auto append_choice = [&](const wormedit::lane_layout_t& layout) {
        const auto index = static_cast<std::size_t>(view.lane_layout_profile_choice_count);
        if (index >= WORM_UI_COMPOSE_LAYOUT_PROFILE_CAPACITY) return;
        std::snprintf(view.lane_layout_profile_names[index],
                      sizeof(view.lane_layout_profile_names[index]), "%s",
                      layout.display_name.c_str());
        std::snprintf(view.lane_layout_profile_ids[index], sizeof(view.lane_layout_profile_ids[index]),
                      "%s", layout.profile_id.c_str());
        if (active && layout.profile_id == active->layout.profile_id)
            view.lane_layout_profile_current = static_cast<std::int8_t>(index);
        ++view.lane_layout_profile_choice_count;
    };
    for (const auto& profile : choices) append_choice(profile.layout);
    if (active && std::ranges::none_of(choices, [&](const auto& profile) {
            return profile.layout.profile_id == active->layout.profile_id;
        }))
        append_choice(active->layout);
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
    const auto pixels_per_row = interaction.pixels_per_standard_measure /
                                static_cast<double>(chart.divisions_per_standard_measure);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& segment = chart.lane_layouts[index];
        const auto measure = std::ranges::find(measures.entries, segment.position.measure,
                                               &wormedit::measure_index_entry_t::id);
        const auto row = wormedit::absolute_row(measures, segment.position);
        if (measure == measures.entries.end() || !row) {
            view.truncated = true;
            return;
        }
        const auto measure_number = static_cast<std::size_t>(measure - measures.entries.begin()) + 1;
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

auto compose_horizontal_layout(double viewport_width_pixels, std::size_t lane_count,
                               bool has_bgm_lanes, bool has_bga_lane,
                               std::size_t visible_timing_lane_count)
    -> compose_horizontal_layout_t {
    constexpr double bga_pane_width = 48.0;
    const auto desired_timing_right = compose_measure_rail_width_pixels +
        compose_timing_lane_width_pixels * visible_timing_lane_count;
    const auto reserved_bga = has_bga_lane ? bga_pane_width : 0.0;
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

auto compose_lane_is_editable(const wormedit::lane_definition_t& lane) -> bool {
    using wormedit::lane_role_t;
    return lane.role != lane_role_t::autoplay && lane.role != lane_role_t::invisible &&
           lane.role != lane_role_t::mine;
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
    const auto found = application_state.requested_snap_denominators.find(chart.value);
    return found == application_state.requested_snap_denominators.end() ? 4 : found->second;
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
    return state.pending_hold.has_value() || state.pending_virtual_lane_key.has_value() ||
           state.pending_note_duration_gesture.has_value() ||
           state.pending_note_move_gesture.has_value() ||
           state.pending_note_paint_gesture.has_value() ||
           state.pending_semantic_layer_paint_gesture.has_value() ||
           state.pending_editor_area_selection.has_value() ||
           state.pending_space_range_selection.has_value();
}


namespace {
struct compose_scene_projection_context_t {
    flattened_workspace_t& flattened;
    const wormedit::chart_document_t& chart;
    const wormedit::editor_field_scene_index_t& index;
    const wormedit::editor_field_scene_t& scene;
    const wormedit::document_history_t& history;
    application_command_state_t& application_state;
    const wormedit::editor_interaction_view_model_t& interaction_view;
    WormUiComposeView& view;
    double field_left_pixels;
    double playable_field_width;
    double pixels_per_row;
    bool downscroll;
    bool lane_key_bindings_truncated;
    const compose_bga_move_preview_input_t* bga_move_preview{};
    std::uint64_t bga_move_snap_denominator{};
};

void populate_compose_sound_labels(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& history = projection.history;
    const auto& bgm_events = projection.scene.bgm_events;
    std::unordered_set<std::uint64_t> visible_sound_ids;
    visible_sound_ids.reserve(projection.scene.note_endpoints.size() + bgm_events.size());
    const auto note_count = std::min(projection.scene.note_endpoints.size(),
                                     static_cast<std::size_t>(WORM_UI_COMPOSE_NOTE_CAPACITY));
    for (std::size_t index = 0; index < note_count; ++index) {
        if (const auto sound = projection.scene.note_endpoints[index].sound)
            visible_sound_ids.insert(sound->value);
    }
    const auto bgm_event_count = std::min(bgm_events.size(),
                                          static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_EVENT_CAPACITY));
    for (std::size_t index = 0; index < bgm_event_count; ++index) {
        if (bgm_events[index].sound) visible_sound_ids.insert(bgm_events[index].sound->value);
    }
    flattened.compose.sound_labels.reserve(visible_sound_ids.size());
    for (const auto& resource : history.document.resources) {
        if (!visible_sound_ids.contains(resource.id.value)) continue;
        flattened.compose.sound_labels.emplace(
            resource.id.value, compose_resource_sound_label(resource));
    }
}

auto prepare_compose_grid_rows(flattened_workspace_t& flattened, std::size_t count) -> bool {
    auto& grid = flattened.compose.grid;
    grid.line = {};
    grid.row = {};
    if (count == 0) return true;
    auto* arena = flattened.arena();
    if (!arena) return false;
    auto lines = arena_array<WormUiComposeGridLineView>(*arena, count);
    auto rows = arena_array<wormedit::exact_chart_row_t>(*arena, count);
    if (lines.capacity() != count || rows.capacity() != count) return false;
    grid.line = lines;
    grid.row = rows;
    return true;
}

auto append_compose_grid_row(flattened_workspace_t& flattened,
                             const WormUiComposeGridLineView& line,
                             const wormedit::exact_chart_row_t& row) -> bool {
    auto& grid = flattened.compose.grid;
    if (grid.line.size() == grid.line.capacity() || grid.row.size() == grid.row.capacity())
        return false;
    return grid.line.push_back(line) && grid.row.push_back(row);
}

void project_compose_grid_lines(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& chart = projection.chart;
    const auto& scene = projection.scene;
    auto& view = projection.view;
    const auto& lines = scene.grid_lines;
    if (lines.empty()) return;
    const auto line_count =
        std::min(lines.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_GRID_CAPACITY));
    std::unordered_map<std::uint64_t, std::uint32_t> measure_numbers;
    measure_numbers.reserve(projection.index.measures.entries.size());
    for (std::size_t measure = 0; measure < projection.index.measures.entries.size(); ++measure) {
        measure_numbers.emplace(projection.index.measures.entries[measure].absolute_begin_row,
                                static_cast<std::uint32_t>(measure + 1));
    }
    std::optional<wormedit::virtual_canvas_t> virtual_canvas;
    if (view.bms_measure_labels) {
        auto canvas = wormedit::build_virtual_canvas(chart);
        if (auto* built = std::get_if<wormedit::virtual_canvas_t>(&canvas)) {
            virtual_canvas = std::move(*built);
        }
    }
    if (!prepare_compose_grid_rows(flattened, line_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < line_count; ++index) {
        const auto& line = lines[index];
        const auto measure = measure_numbers.find(line.chart_row.whole_rows);
        std::uint32_t measure_number = measure != measure_numbers.end() ? measure->second : 0;
        if (line.kind == wormedit::editor_grid_line_kind_t::measure &&
            measure_number == 0 && virtual_canvas) {
            const auto virtual_measure =
                virtual_canvas->measure_containing(line.chart_row.whole_rows);
            if (virtual_measure && virtual_measure->begin_row == line.chart_row.whole_rows) {
                measure_number = static_cast<std::uint32_t>(virtual_measure->number + 1);
            }
        }
        if (!append_compose_grid_row(flattened,
            {.y_pixels = line.y_pixels,
             .kind = static_cast<std::uint8_t>(line.kind),
             .emphasis = static_cast<std::uint8_t>(line.emphasis),
             .measure_number = line.kind == wormedit::editor_grid_line_kind_t::measure
                                   ? measure_number
                                   : 0,
             .chart_row =
             line.chart_row.fractional_rows.numerator == 0 ? line.chart_row.whole_rows : 0},
             line.chart_row)) {
            view.truncated = true;
            break;
        }
    }
    view.truncated = view.truncated || line_count != lines.size();

}

auto compose_note_display_lane_number(const wormedit::editor_field_scene_t& scene,
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

auto compose_note_key_color(const wormedit::editor_field_scene_t& scene, const auto& endpoint)
    -> std::uint8_t {
    for (const auto& lane : scene.lanes) {
        if (lane.lane == endpoint.lane) return static_cast<std::uint8_t>(lane.key_color);
    }
    return 0;
}

auto compose_endpoint_snap_class(const compose_scene_projection_context_t& projection,
                                 const auto& endpoint)
    -> std::uint8_t {
    const auto found = projection.index.note_indices_by_id.find(endpoint.note.value);
    if (found == projection.index.note_indices_by_id.end()) return 0;
    const auto& source = projection.index.notes[found->second];
    bool tail = false;
    if constexpr (requires { endpoint.tail; }) tail = endpoint.tail;
    return wormedit::ui::note_snap_class(tail ? source.end_row : source.begin_row,
                                        projection.chart.divisions_per_standard_measure);
}

template<class T>
auto prepare_compose_geometry_rows(flattened_workspace_t& flattened, frame_span<T>& rows,
                                   std::size_t count) -> bool {
    if (count == 0) { rows = {}; return true; }
    auto* arena = flattened.arena();
    rows = arena ? arena_array<T>(*arena, count) : frame_span<T>{};
    return rows.capacity() == count;
}
template<class T>
void append_compose_geometry_row(frame_span<T>& rows, const T& row, bool& truncated) {
    if (!rows.push_back(row)) truncated = true;
}

void project_compose_scene_objects(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.view;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    const auto& differences = scene.comparison_differences;
    const auto difference_count =
        std::min(differences.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_DIFFERENCE_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.difference, difference_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < difference_count; ++index) {
        append_compose_geometry_row(flattened.compose.difference,
            {.bounds = convert_rect(differences[index].bounds)}, view.truncated);
    }
    const auto& holds = scene.hold_bodies;
    const auto hold_count =
        std::min(holds.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_HOLD_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.hold, hold_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < hold_count; ++index) {
        const auto& hold = holds[index];
        append_compose_geometry_row(flattened.compose.hold,
            {.bounds = convert_rect(hold.bounds),
             .note_id = hold.note.value,
             .lane_id = hold.lane.value,
             .display_lane_number = compose_note_display_lane_number(scene, hold),
             .key_color = compose_note_key_color(scene, hold),
             .snap_class = compose_endpoint_snap_class(projection, hold),
             .selected = hold.selected,
             .proposal_origin = hold.proposal_origin,
             .proposal_ghost = hold.proposal_ghost,
             .semantic_color_visible = hold.semantic_color.has_value(),
             .semantic_red =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->red : 0),
             .semantic_green =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->green : 0),
             .semantic_blue =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->blue : 0),
             .semantic_alpha =
                 static_cast<std::uint8_t>(hold.semantic_color ? hold.semantic_color->alpha : 0)}, view.truncated);
    }
    const auto& endpoints = scene.note_endpoints;
    const auto endpoint_count =
        std::min(endpoints.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_NOTE_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.note, endpoint_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < endpoint_count; ++index) {
        const auto& endpoint = endpoints[index];
        const auto sound = compose_note_sound_text(endpoint.sound,
                                                   flattened.compose.sound_labels);
        append_compose_geometry_row(flattened.compose.note,
            {.bounds = convert_rect(endpoint.bounds),
             .note_id = endpoint.note.value,
             .lane_id = endpoint.lane.value,
             .display_lane_number = compose_note_display_lane_number(scene, endpoint),
             .key_color = compose_note_key_color(scene, endpoint),
             .sound_label = sound.label,
             .sound_detail = sound.detail,
             .glyph = static_cast<std::uint8_t>(endpoint.glyph),
             .snap_class = compose_endpoint_snap_class(projection, endpoint),
             .kind = static_cast<std::uint8_t>(endpoint.kind),
             .tail = endpoint.tail,
             .selected = endpoint.selected,
             .proposal_origin = endpoint.proposal_origin,
             .proposal_ghost = endpoint.proposal_ghost,
             .semantic_color_visible = endpoint.semantic_color.has_value(),
             .semantic_red = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->red : 0),
             .semantic_green = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->green : 0),
             .semantic_blue = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->blue : 0),
             .semantic_alpha = static_cast<std::uint8_t>(
                 endpoint.semantic_color ? endpoint.semantic_color->alpha : 0),
             .paint_preview = endpoint.paint_preview,
             .paint_erase_preview = endpoint.paint_erase_preview,
             .paint_invalid_preview = endpoint.paint_invalid_preview}, view.truncated);
    }
    view.truncated = view.truncated || difference_count != differences.size() ||
                     hold_count != holds.size() || endpoint_count != endpoints.size();


}

void project_compose_layout_and_notes(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.view;
    const auto field_left_pixels = projection.field_left_pixels;
    const auto playable_field_width = projection.playable_field_width;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    const auto& bands = scene.layout_bands;
    const auto band_count =
        std::min(bands.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BAND_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.band, band_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < band_count; ++index) {
        const auto& band = bands[index];
        append_compose_geometry_row(flattened.compose.band,
            {.bounds = {field_left_pixels, band.top_pixels,
                        field_left_pixels + playable_field_width,
                        band.bottom_pixels},
             .split_stage = band.split_stage}, view.truncated);
    }
    const auto& lanes = scene.lanes;
    const auto lane_count =
        std::min(lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_LANE_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.lane, lane_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < lane_count; ++index) {
        const auto& lane = lanes[index];
        append_compose_geometry_row(flattened.compose.lane,{.bounds = convert_rect(lane.bounds),
                                                  .lane_id = lane.lane.value,
                                                  .display_order = lane.display_order,
                                                  .role = static_cast<std::uint8_t>(lane.role),
                                                  .stage = static_cast<std::uint8_t>(lane.stage),
                                                  .glyph = static_cast<std::uint8_t>(lane.glyph),
                                                  .key_color = static_cast<std::uint8_t>(lane.key_color)}, view.truncated);
    }
    project_compose_grid_lines(projection);
    project_compose_scene_objects(projection);
    view.truncated = view.truncated || projection.lane_key_bindings_truncated ||
                     band_count != bands.size() || lane_count != lanes.size();
}

template <typename ConvertRect>
void project_compose_bgm_events(flattened_workspace_t& flattened,
                                const std::vector<wormedit::editor_field_bgm_event_geometry_t>& events,
                                WormUiComposeView& view, ConvertRect&& convert_rect) {
    const auto event_count = std::min(events.size(),
        static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_EVENT_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.bgm.event, event_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < event_count; ++index) {
        const auto& event = events[index];
        const auto sound = event.sound
            ? flattened.compose.sound_labels.find(event.sound->value)
            : flattened.compose.sound_labels.end();
        append_compose_geometry_row(flattened.compose.bgm.event,
            {.bounds = convert_rect(event.bounds),
             .event_id = event.event.value,
             .source_lane_id = event.source_lane.value,
             .sound_id = event.sound ? event.sound->value : 0,
             .has_sound = event.sound.has_value(),
             .bmson_slice = event.bmson_restart.has_value(),
             .bmson_restart = event.bmson_restart.value_or(false),
             .selected = event.selected,
             .sound_label = sound == flattened.compose.sound_labels.end()
                                ? (event.sound ? "?" : "") : sound->second.first.c_str(),
             .sound_detail = sound == flattened.compose.sound_labels.end()
                                ? (event.sound ? "Unknown sound resource" : "Unbound BGM slot")
                                : sound->second.second.c_str(),
             .semantic_color_visible = event.semantic_color.has_value(),
             .semantic_red = static_cast<std::uint8_t>(
                 event.semantic_color ? event.semantic_color->red : 0),
             .semantic_green = static_cast<std::uint8_t>(
                 event.semantic_color ? event.semantic_color->green : 0),
             .semantic_blue = static_cast<std::uint8_t>(
                 event.semantic_color ? event.semantic_color->blue : 0),
             .semantic_alpha = static_cast<std::uint8_t>(
                 event.semantic_color ? event.semantic_color->alpha : 0)}, view.truncated);
    }
    view.truncated = view.truncated || event_count != events.size();
}

void project_compose_bgm(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& scene = projection.scene;
    auto& view = projection.view;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    const auto& bgm_lanes = scene.bgm_lanes;
    const auto bgm_lane_count =
        std::min(bgm_lanes.size(), static_cast<std::size_t>(WORM_UI_COMPOSE_BGM_LANE_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.bgm.lane, bgm_lane_count)) {
        view.truncated = true;
        return;
    }
    for (std::size_t index = 0; index < bgm_lane_count; ++index) {
        const auto& lane = bgm_lanes[index];
        append_compose_geometry_row(flattened.compose.bgm.lane, {.bounds = convert_rect(lane.bounds),
                                                      .source_lane_id = lane.source_lane.value,
                                                      .source_order = lane.source_order}, view.truncated);
    }
    project_compose_bgm_events(flattened, scene.bgm_events, view, convert_rect);
    view.truncated = view.truncated || bgm_lane_count != bgm_lanes.size();
}

auto bga_preview_authored_row(const compose_scene_projection_context_t& projection,
                              double source_y) -> std::optional<std::uint64_t> {
    const auto position = wormedit::editor_position_at_y_pixels(
        projection.chart, projection.interaction_view, source_y);
    if (!position) return std::nullopt;
    const auto target = projection.bga_move_snap_denominator == 0
        ? position
        : wormedit::snap_position(projection.chart, projection.index.measures, *position,
                                  projection.bga_move_snap_denominator);
    return target ? wormedit::absolute_row(projection.index.measures, *target) : std::nullopt;
}

auto bga_preview_virtual_row(const compose_scene_projection_context_t& projection,
                             double source_y) -> std::optional<std::uint64_t> {
    using namespace wormedit;
    if (projection.chart.source_family != format_family_t::bms ||
        !chart_has_virtual_tail(projection.chart)) return std::nullopt;
    const auto& interaction = projection.interaction_view;
    const auto raw = (static_cast<long double>(interaction.viewport_scroll_pixels) + source_y) *
        projection.chart.divisions_per_standard_measure / interaction.pixels_per_standard_measure;
    if (!std::isfinite(raw) || raw < 0.0L ||
        raw >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
        return std::nullopt;
    const auto rounded = static_cast<std::uint64_t>(std::floor(raw + 0.5L));
    const auto built = build_virtual_canvas(projection.chart);
    const auto* canvas = std::get_if<virtual_canvas_t>(&built);
    const auto source_measure = canvas ? canvas->measure_containing(rounded) : std::nullopt;
    if (!source_measure || source_measure->authored) return std::nullopt;
    const auto target_row = projection.bga_move_snap_denominator == 0
        ? std::optional{rounded}
        : snap_absolute_row(projection.chart, rounded, projection.bga_move_snap_denominator);
    const auto target_measure = canvas && target_row
        ? canvas->measure_containing(*target_row) : std::nullopt;
    return target_measure && !target_measure->authored ? target_row : std::nullopt;
}

auto bga_preview_target_row(const compose_scene_projection_context_t& projection,
                            const compose_bga_move_preview_input_t& input)
    -> std::optional<std::uint64_t> {
    const auto height = projection.interaction_view.viewport_height_pixels;
    const auto y = std::clamp(input.y_pixels, 0.0, std::max(0.0, height - 0.001));
    const auto source_y = projection.downscroll ? height - y : y;
    if (const auto authored = bga_preview_authored_row(projection, source_y)) return authored;
    return bga_preview_virtual_row(projection, source_y);
}

void project_compose_bga_move_preview(const compose_scene_projection_context_t& projection) {
    auto& view = projection.view;
    view.bga_move_preview_valid = false;
    const auto* input = projection.bga_move_preview;
    if (!input || input->event_id == 0 || input->project_id != view.project_id ||
        input->chart_id != projection.chart.id.value ||
        input->document_revision != view.document_revision || !std::isfinite(input->y_pixels))
        return;
    const auto event = std::ranges::find(projection.scene.bga_events,
        wormedit::object_id_t{input->event_id}, &wormedit::editor_field_bga_event_geometry_t::event);
    if (event == projection.scene.bga_events.end() ||
        (event->kind != wormedit::visual_event_kind_t::background &&
         event->kind != wormedit::visual_event_kind_t::video) || !event->selected)
        return;
    const auto row = bga_preview_target_row(projection, *input);
    if (!row) return;
    view.bga_move_preview_valid = true;
    view.bga_move_preview_event_id = input->event_id;
    view.bga_move_preview_y_pixels = compose_project_y(
        *row, projection.pixels_per_row,
        projection.interaction_view.viewport_scroll_pixels,
        view.viewport_height_pixels, projection.downscroll);
}

void project_compose_bga(const compose_scene_projection_context_t& projection) {
    static_assert(wormedit::bga_lane_overlap_stack_threshold ==
                  WORM_UI_COMPOSE_BGA_OVERLAP_STACK_THRESHOLD);
    auto& flattened = projection.flattened;
    auto& view = projection.view;
    const auto& scene = projection.scene;
    const auto count = std::min(scene.bga_events.size(),
                                static_cast<std::size_t>(WORM_UI_COMPOSE_BGA_EVENT_CAPACITY));
    if (!prepare_compose_geometry_rows(flattened, flattened.compose.bga_event, count)) {
        view.truncated = true;
        return;
    }
    view.bga_lane_visible = projection.interaction_view.bga_lane_visible;
    view.bga_lane_bounds = scene.bga_lane
        ? WormUiComposeRect{scene.bga_lane->bounds.left, scene.bga_lane->bounds.top,
                            scene.bga_lane->bounds.right, scene.bga_lane->bounds.bottom}
        : WormUiComposeRect{};
    for (std::size_t index = 0; index < count; ++index) {
        const auto& event = scene.bga_events[index];
        const auto& document = projection.history.document;
        const auto resource = std::ranges::find(document.resources, event.resource,
                                                 &wormedit::resource_entry_t::id);
        const wormedit::target_resource_identity_t* identity{};
        if (resource != document.resources.end()) {
            const auto target = std::ranges::find_if(resource->target_identities,
                [](const auto& value) {
                    return value.family == wormedit::format_family_t::bms &&
                           value.namespace_name == "BMP";
                });
            if (target != resource->target_identities.end()) identity = &*target;
        }
        const auto* name = resource != document.resources.end() && !resource->name.empty()
                               ? resource->name.c_str() : "Missing BGA resource";
        const auto* bmp_id = identity ? identity->identifier.c_str() : "";
        const auto asset = resource == document.resources.end()
            ? document.visual_assets.end()
            : std::ranges::find(document.visual_assets, resource->asset,
                                &wormedit::visual_asset_t::id);
        const auto expected_asset_kind = event.kind == wormedit::visual_event_kind_t::video
            ? wormedit::asset_kind_t::video : wormedit::asset_kind_t::image;
        append_compose_geometry_row(flattened.compose.bga_event,
            {.event_id = event.event.value,
             .resource_id = event.resource.value,
             .canvas_row = event.row,
             .overlap_count = event.overlap_count,
             .kind = static_cast<std::uint8_t>(event.kind),
             .name = name,
             .bmp_id = bmp_id,
             .missing = resource == document.resources.end() || resource->missing ||
                        asset == document.visual_assets.end() || asset->kind != expected_asset_kind,
             .selected = event.selected,
             .bounds = {event.bounds.left, event.bounds.top,
                        event.bounds.right, event.bounds.bottom}}, view.truncated);
    }
    view.truncated = view.truncated || count != scene.bga_events.size();
    project_compose_bga_move_preview(projection);
}

const wormedit::bgm_event_t* find_compose_bgm_event(
    const wormedit::chart_document_t& chart, const auto& event_id) {
    for (const auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.bgm_events, event_id,
                                             &wormedit::bgm_event_t::id);
        if (found != measure.bgm_events.end()) return &*found;
    }
    return nullptr;
}

void project_compose_draft_marker(const compose_scene_projection_context_t& projection,
                                 const auto& marker,
                                 const wormedit::editor_field_rect_t* bgm_bounds) {
    const auto& scene = projection.scene;
    const auto& view = projection.view;
    const auto& interaction_view = projection.interaction_view;
    const auto row = wormedit::absolute_row(projection.index.measures, marker.position);
    if (!row) return;
    const auto y = compose_project_y(*row, projection.pixels_per_row,
                                     interaction_view.viewport_scroll_pixels,
                                     view.viewport_height_pixels, projection.downscroll);
    if (y < -8.0 || y > view.viewport_height_pixels + 8.0) return;
    const wormedit::editor_field_rect_t* bounds = bgm_bounds;
    if (marker.playable_lane) {
        const auto playable = std::ranges::find_if(scene.lanes, [&](const auto& candidate) {
            return candidate.lane == *marker.playable_lane &&
                   y >= candidate.bounds.top - 8.0 && y <= candidate.bounds.bottom + 8.0;
        });
        bounds = playable == scene.lanes.end() ? nullptr : &playable->bounds;
    }
    if (!bounds) return;
    projection.flattened.compose.bgm.draft.push_back(
        {.bounds = {bounds->left, y - 8.0, bounds->right, y + 8.0},
         .restart = marker.restart,
         .playable = marker.playable_lane.has_value()});
}

void project_compose_marker_draft(const compose_scene_projection_context_t& projection) {
    const auto& draft = projection.application_state.bmson_marker_draft;
    if (!draft || projection.pixels_per_row <= 0.0) return;
    const auto* reference = find_compose_bgm_event(projection.chart, draft->reference);
    if (!reference) return;
    const auto lane = std::ranges::find_if(projection.scene.bgm_lanes, [&](const auto& candidate) {
        return candidate.source_lane == reference->source_lane;
    });
    const auto* bounds = lane == projection.scene.bgm_lanes.end() ? nullptr : &lane->bounds;
    for (const auto& marker : draft->markers)
        project_compose_draft_marker(projection, marker, bounds);
}

void project_compose_marker_previews(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    const auto& scene = projection.scene;
    const auto& application_state = projection.application_state;
    const auto& interaction_view = projection.interaction_view;
    auto& view = projection.view;
    const auto pixels_per_row = projection.pixels_per_row;
    const auto downscroll = projection.downscroll;
    const auto convert_rect = [](const wormedit::editor_field_rect_t& rect) {
        return WormUiComposeRect{rect.left, rect.top, rect.right, rect.bottom};
    };
    project_compose_marker_draft(projection);
    if (application_state.bmson_marker_move_preview && pixels_per_row > 0.0) {
        const auto& preview = *application_state.bmson_marker_move_preview;
        const auto lane = std::ranges::find_if(scene.bgm_lanes, [&](const auto& candidate) {
            return candidate.source_lane == preview.source_lane;
        });
        const auto row = preview.virtual_canvas_row
            ? preview.virtual_canvas_row
            : wormedit::absolute_row(projection.index.measures, preview.target);
        if (lane != scene.bgm_lanes.end() && row) {
            const auto y = compose_project_y(*row, pixels_per_row,
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
            if (y >= -8.0 && y <= view.viewport_height_pixels + 8.0) {
                flattened.compose.bgm.move = WormUiComposeBgmMovePreviewView{
                    .bounds = {lane->bounds.left, y - 8.0, lane->bounds.right, y + 8.0},
                    .valid = preview.valid};
            }
        }
    }
    if (scene.cursor) {
        flattened.compose.cursor = WormUiComposeCursorView{
            .bounds = convert_rect(scene.cursor->bounds),
            .lane_id = scene.cursor->lane.value,
        };
    }

}

void publish_compose_scene(const compose_scene_projection_context_t& projection) {
    auto& flattened = projection.flattened;
    auto& view = projection.view;
    view.bands = flattened.compose.band.data();
    view.band_count = flattened.compose.band.size();
    view.lanes = flattened.compose.lane.data();
    view.lane_count = flattened.compose.lane.size();
    view.grid_lines = flattened.compose.grid.line.data();
    view.grid_line_count = flattened.compose.grid.line.size();
    view.differences = flattened.compose.difference.data();
    view.difference_count = flattened.compose.difference.size();
    view.hold_bodies = flattened.compose.hold.data();
    view.hold_body_count = flattened.compose.hold.size();
    view.note_endpoints = flattened.compose.note.data();
    view.note_endpoint_count = flattened.compose.note.size();
    view.bgm_lanes = flattened.compose.bgm.lane.data();
    view.bgm_lane_count = flattened.compose.bgm.lane.size();
    view.bgm_events = flattened.compose.bgm.event.data();
    view.bgm_event_count = flattened.compose.bgm.event.size();
    view.bga_events = flattened.compose.bga_event.data();
    view.bga_event_count = flattened.compose.bga_event.size();
    view.bmson_marker_drafts = flattened.compose.bgm.draft.data();
    view.bmson_marker_draft_count = flattened.compose.bgm.draft.size();
    view.bmson_marker_move_preview = flattened.compose.bgm.move
        ? &*flattened.compose.bgm.move : nullptr;
    view.cursor = flattened.compose.cursor ? &*flattened.compose.cursor : nullptr;
}
bool initialize_compose_view_state(
    flattened_workspace_t& flattened, compose_scene_cache_t& cache, int window_width,
    int window_height, const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    const application_command_state_t& application_state) {
    auto& view = flattened.view.compose;
    if (!view.visible) {
        cache.interaction_view.reset();
        cache.hover.reset();
        cache.paint_pointer.reset();
        cache.focus_note_label_editor = false;
        cache.viewport_autoscroll_active = false;
        return false;
    }
    view.viewport_width_pixels = std::max(160.0, static_cast<double>(window_width) - 48.0);
    view.bgm_bar_available = cache.bgm_bar_audio_seen;
    view.bgm_bar_reserved = cache.bgm_bar_audio_seen && !application_state.bgm_bar_hidden;
    view.viewport_height_pixels = compose_surface_height(window_height, flattened.view);
    view.bga_lane_visible = !retained_state || retained_state->bga_lane_visible;
    view.playable_field_left_pixels = compose_measure_rail_width_pixels;
    view.playable_field_right_pixels = view.viewport_width_pixels;
    view.bms_measure_labels = false;
    view.compose_measure_count = 0;
    view.selected_bms_measure_number_plus_one = 0;
    view.timing_document_begin_fraction = 0.0;
    view.timing_document_end_fraction = 1.0;
    view.truncated = false;
    view.pixels_per_standard_measure = retained_state
                                           ? retained_state->pixels_per_standard_measure
                                           : wormedit::default_pixels_per_standard_measure;
    const auto notes_editable = wormedit::ui::compose_notes_editable(compose_mode_of(application_state));
    view.note_paint_mode_active = notes_editable && application_state.note_paint_mode_active;
    view.semantic_layer_paint_mode_active =
        notes_editable && application_state.semantic_layer_paint_mode_active;
    view.semantic_layer_gesture_active = retained_state &&
        editor_interaction_has_pending_gesture(*retained_state);
    view.timing_stage_active = false;
    view.timing_stage_valid = false;
    view.timing_stage_lane = WORM_UI_TIMING_LANE_BPM;
    view.timing_stage_y_pixels = 0.0;
    view.timing_lane_visibility_mask = effective_compose_timing_lane_mask(application_state);
    view.timing_focus = application_state.compose_timing_focus;
    view.selected_note_count = 0;
    view.selected_bgm_event_count = 0;
    view.selected_assigned_resource_id = 0;
    view.selected_bgm_event_id = 0;
    view.selected_bmson_bgm_event_id = 0;
    view.selected_bmson_note_event_id = 0;
    view.selected_bmson_source_lane_id = 0;
    view.staged_bmson_marker_count = 0;
    view.staged_bmson_marker_playable_lanes = nullptr;
    view.staged_bmson_marker_restarts = nullptr;
    view.bmson_marker_drafts = nullptr;
    view.bmson_marker_draft_count = 0;
    view.bmson_marker_move_preview = nullptr;
    view.waveform_rows = nullptr;
    view.waveform_row_count = 0;
    view.channel_waveform_rows = nullptr;
    view.channel_waveform_row_count = 0;
    view.channel_waveform_source_lane_id = 0;
    view.waveform_overview_rows = nullptr;
    view.waveform_overview_row_count = 0;
    view.waveform_overview_visible_begin_fraction = 0.0;
    view.waveform_overview_visible_end_fraction = 0.0;
    view.waveform_beat_zero_visible = false;
    view.waveform_beat_zero_fraction = 0.0;
    view.waveform_overview_total_frames = 0;
    view.waveform_overview_sample_rate = 0;
    view.bgm_bar_rows = nullptr;
    view.bgm_bar_row_count = 0;
    view.bgm_bar_begin_fraction = 0.0;
    view.bgm_bar_end_fraction = 1.0;
    view.bgm_bar_marks = nullptr;
    view.bgm_bar_mark_count = 0;
    view.audio_offset_editable = false;
    view.audio_offset_locked = application_state.audio_offset_locked;
    view.waveform_audio_offset_numerator = 0;
    view.waveform_audio_offset_denominator = 1;
    view.waveform_offset_drag_active = false;
    view.beat_fit_drag_active = false;
    view.offset_fit_drag_active = false;
    view.offset_fit_target_y_pixels = -1.0;
    view.proposed_grid_lines = nullptr;
    view.proposed_grid_line_count = 0;
    view.timing_preview_rects = nullptr;
    view.timing_preview_rect_count = 0;
    view.timing_preview_markers = nullptr;
    view.timing_preview_marker_count = 0;
    view.dead_spans = nullptr;
    view.dead_span_count = 0;
    view.kiai_spans = nullptr;
    view.kiai_span_count = 0;
    view.recipe_areas = nullptr;
    view.recipe_area_count = 0;
    view.speed_spans = nullptr;
    view.speed_span_count = 0;
    view.recipe_graph_points = nullptr;
    view.recipe_graph_point_count = 0;
    view.sv_graph_points = nullptr;
    view.sv_graph_point_count = 0;
    view.sv_graph_one_fraction = -1.0;
    view.speed_graph_points = nullptr;
    view.speed_graph_point_count = 0;
    view.speed_graph_one_fraction = -1.0;
    view.waveform_proposed_beat_zero_visible = false;
    view.waveform_proposed_beat_zero_fraction = 0.0;
    view.waveform_proposed_audio_offset_numerator = 0;
    view.waveform_proposed_audio_offset_denominator = 1;
    view.waveform_timing_locked = application_state.timing_locked;
    view.waveform_analysis_pending = false;
    view.beat_marker_navigation_available = false;
    view.audio_marker_navigation_available = false;
    view.audio_marker_transport_available = false;
    view.beat_bookmarks = flattened.compose.bookmark.data();
    view.beat_bookmark_count = 0;
    view.beat_bookmarks_truncated = false;
    view.beat_sections = flattened.compose.section.data();
    view.beat_section_count = 0;
    view.beat_sections_truncated = false;
    view.beat_section_selection_span_available = false;
    view.beat_section_selection_begin_measure_number = 0;
    view.beat_section_selection_begin_row = 0;
    view.beat_section_selection_end_measure_number = 0;
    view.beat_section_selection_end_row = 0;
    view.audio_bookmarks = flattened.compose.audio_marker.bookmark.data();
    view.audio_bookmark_count = 0;
    view.audio_bookmarks_truncated = false;
    view.audio_sections = flattened.compose.audio_marker.section.data();
    view.audio_section_count = 0;
    view.audio_sections_truncated = false;
    view.semantic_layers = flattened.compose.semantic_layer.view.data();
    view.semantic_layer_count = 0;
    view.semantic_layer_total_count = 0;
    view.semantic_layer_first_row = 0;
    view.note_menu_layers = nullptr;
    view.note_menu_layer_count = 0;
    view.note_menu_layer_first_row = 0;
    view.note_menu_layer_total_count = 0;
    view.semantic_layer_target = {};
    view.semantic_layer_target_none = false;
    view.semantic_layer_unassigned_note_count = 0;
    view.semantic_layer_unassigned_bgm_count = 0;
    view.semantic_layer_page = 0;
    view.semantic_layer_page_count = 0;
    view.semantic_layers_truncated = false;
    view.semantic_layer_target_locked = false;
    view.semantic_layer_target_visible = false;
    view.semantic_layer_target_required_filter = false;
    view.semantic_layer_target_excluded_filter = false;
    view.semantic_layer_solo_active = application_state.semantic_layer_solo_active;
    view.status_text = nullptr;
    view.selected_note_label = nullptr;
    view.selected_note_label_note_id = 0;
    view.focus_note_label_editor = false;
    view.hover_note_visible = false;
    view.hover_note_id = 0;
    view.hover_tooltip = nullptr;
    view.timing_markers = nullptr;
    view.timing_marker_count = 0;
    view.viewport_autoscroll_active = cache.viewport_autoscroll_active;
    view.viewport_autoscroll_anchor_x_pixels = cache.viewport_autoscroll_anchor_x_pixels;
    view.viewport_autoscroll_anchor_y_pixels = cache.viewport_autoscroll_anchor_y_pixels;
    view.viewport_autoscroll_pointer_x_pixels = cache.viewport_autoscroll_pointer_x_pixels;
    view.viewport_autoscroll_pointer_y_pixels = cache.viewport_autoscroll_pointer_y_pixels;
    flattened.compose.hover_tooltip.clear();
    flattened.compose.selected_note_label.clear();
    return true;
}

void populate_semantic_layer_target_state(WormUiComposeView& view,
                                         const application_command_state_t& application_state,
                                         const auto& layer, bool selected) {
    view.semantic_layer_target_locked = view.semantic_layer_target_locked ||
                                        (selected && layer.locked);
    view.semantic_layer_target_visible = view.semantic_layer_target_visible ||
                                         (selected && layer.visible);
    view.semantic_layer_target_required_filter =
        view.semantic_layer_target_required_filter ||
        (selected && std::ranges::find(application_state.required_semantic_layer_filters,
                                       layer.id) !=
                         application_state.required_semantic_layer_filters.end());
    view.semantic_layer_target_excluded_filter =
        view.semantic_layer_target_excluded_filter ||
        (selected && std::ranges::find(application_state.excluded_semantic_layer_filters,
                                       layer.id) !=
                         application_state.excluded_semantic_layer_filters.end());
}

auto append_semantic_layer_row(flattened_workspace_t& flattened, compose_scene_cache_t& cache,
                               const wormedit::semantic_layer_t& layer, bool selected) -> bool {
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
            .assigned_note_count = cache.semantic_layer_membership_counts[layer.id.value],
            .visible = layer.visible,
            .locked = layer.locked,
            .selected = selected,
            .assigned_bgm_count = cache.semantic_layer_bgm_membership_counts[layer.id.value],
    });
}

void populate_semantic_membership_counts(compose_scene_cache_t& cache,
                                          const wormedit::chart_document_t& chart) {
    cache.semantic_layer_membership_counts.clear();
    cache.semantic_layer_bgm_membership_counts.clear();
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.semantic_layers.empty()) ++cache.semantic_layer_membership_counts[0];
            for (const auto layer : note.semantic_layers) {
                auto& count = cache.semantic_layer_membership_counts[layer.value];
                if (count != std::numeric_limits<std::size_t>::max()) ++count;
            }
        }
        for (const auto& event : measure.bgm_events) {
            if (event.semantic_layers.empty()) ++cache.semantic_layer_bgm_membership_counts[0];
            for (const auto layer : event.semantic_layers) {
                auto& count = cache.semantic_layer_bgm_membership_counts[layer.value];
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
    if (app.note_layer_menu_chart != chart.id || app.note_layer_menu_revision != revision)
        app.note_layer_menu_row_count = 0;
    if (!app.note_layer_menu_row_count) { rows = {}; return; }
    const auto window = wormedit::make_semantic_layer_palette_window(chart,
        app.note_layer_menu_first_row,
        std::min<std::size_t>(app.note_layer_menu_row_count, WORM_UI_COMPOSE_NOTE_LAYER_WINDOW_CAPACITY));
    if (!window || !prepare_compose_geometry_rows(flattened, rows, window->layer_indices.size())) return;
    app.note_layer_menu_first_row = window->first_row;
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
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        populate_semantic_membership_counts(cache, chart);
    }
    if (application_state.semantic_layer_palette_chart != chart.id) {
        application_state.semantic_layer_palette_chart = chart.id;
        application_state.semantic_layer_first_row = 0;
    }
    view.semantic_layer_target_none = application_state.semantic_layer_target_none;
    view.semantic_layer_unassigned_note_count = cache.semantic_layer_membership_counts[0];
    view.semantic_layer_unassigned_bgm_count =
        cache.semantic_layer_bgm_membership_counts[0];
    const auto palette = wormedit::make_semantic_layer_palette_window(
        chart, application_state.semantic_layer_first_row,
        std::clamp<std::size_t>(application_state.semantic_layer_row_count, 1,
                               WORM_UI_COMPOSE_SEMANTIC_LAYER_WINDOW_CAPACITY));
    if (!palette) {
        report_compose_projection(flattened, projection_feedback_kind_t::layer_palette,
            wormedit::notification_severity_t::error, "The semantic-layer palette exceeds its UI limit.");
        return false;
    }
    application_state.semantic_layer_first_row = palette->first_row;
    view.semantic_layer_first_row = palette->first_row;
    view.semantic_layer_total_count = palette->total_count;
    const auto target = std::ranges::find(chart.semantic_layers,
        application_state.semantic_layer_target.value_or(wormedit::layer_id_t{}),
        &wormedit::semantic_layer_t::id);
    if (target != chart.semantic_layers.end()) {
        const auto* name = flattened.arena() ? arena_string(*flattened.arena(), target->name) : nullptr;
        view.semantic_layer_target = {
            .layer_id = target->id.value, .name = name,
            .red = target->color.red, .green = target->color.green,
            .blue = target->color.blue, .alpha = target->color.alpha,
            .assigned_note_count = cache.semantic_layer_membership_counts[target->id.value],
            .visible = target->visible, .locked = target->locked, .selected = true,
            .assigned_bgm_count = cache.semantic_layer_bgm_membership_counts[target->id.value],
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
        const auto selected = application_state.semantic_layer_target == layer.id;
        if (!append_semantic_layer_row(flattened, cache, layer, selected)) {
            view.semantic_layers_truncated = true;
            break;
        }
        view.semantic_layer_count = flattened.compose.semantic_layer.view.size();
        populate_semantic_layer_target_state(view, application_state, layer, selected);
    }
    return true;
}

template<class T>
struct compose_marker_storage_t {
    flattened_workspace_t& flattened;
    frame_span<T>& rows;
    std::size_t& published_count;
    bool& truncated;
    std::size_t limit;
    std::size_t processed{};
    bool failed{};
};
template<class T>
void prepare_compose_marker_storage(compose_marker_storage_t<T>& storage, std::size_t candidates) {
    const auto count = std::min(candidates, storage.limit);
    storage.rows = {};
    if (count == 0) return;
    auto* arena = storage.flattened.arena();
    storage.rows = arena ? arena_array<T>(*arena, count) : frame_span<T>{};
    storage.failed = storage.rows.capacity() != count;
    storage.truncated = storage.truncated || storage.failed;
}
template<class T>
auto compose_marker_at_capacity(compose_marker_storage_t<T>& storage) -> bool {
    if (storage.processed >= storage.limit) {
        storage.truncated = true;
        return true;
    }
    ++storage.processed;
    return false;
}
template<class T>
void append_compose_marker(compose_marker_storage_t<T>& storage, std::string_view label, T value) {
    if (storage.failed) return;
    auto* arena = storage.flattened.arena();
    value.label = arena ? arena_string(*arena, label) : nullptr;
    if (!value.label || !storage.rows.push_back(value)) {
        storage.failed = true;
        storage.truncated = true;
        return;
    }
    storage.published_count = storage.rows.size();
}

auto populate_compose_audio_bookmark(compose_marker_storage_t<WormUiComposeAudioBookmarkView>& storage,
                                     const wormedit::bookmark_t& bookmark, WormUiComposeView& view) -> bool {
    if (!bookmark.audio_milliseconds || bookmark.position) {
        report_compose_projection(storage.flattened, projection_feedback_kind_t::audio_bookmark,
            wormedit::notification_severity_t::error, "An audio-time bookmark has inconsistent position data.", bookmark.id.value);
        return false;
    }
    if (compose_marker_at_capacity(storage)) return true;
    append_compose_marker(storage, bookmark.label, WormUiComposeAudioBookmarkView{
                .bookmark_id = bookmark.id.value,
                .label = nullptr,
                .audio_milliseconds = *bookmark.audio_milliseconds,
                .projection_visible = false,
                .projected_y_pixels = 0.0,
            });
    return true;
}
auto populate_compose_beat_bookmark(compose_marker_storage_t<WormUiComposeBookmarkView>& storage,
                                    const wormedit::bookmark_t& bookmark,
                                    const wormedit::measure_index_t& measures, WormUiComposeView& view) -> bool {
    if (!bookmark.position) {
        report_compose_projection(storage.flattened, projection_feedback_kind_t::beat_bookmark_position,
            wormedit::notification_severity_t::error, "A beat-anchored bookmark has no valid chart position.", bookmark.id.value);
        return false;
    }
    if (compose_marker_at_capacity(storage)) return true;
    const auto measure = std::ranges::find(measures.entries, bookmark.position->measure,
                                          &wormedit::measure_index_entry_t::id);
    if (measure == measures.entries.end() || bookmark.position->row >= measure->row_capacity) {
        report_compose_projection(storage.flattened, projection_feedback_kind_t::beat_bookmark_geometry,
            wormedit::notification_severity_t::error, "A beat-anchored bookmark is outside the chart measure geometry.", bookmark.id.value);
        return false;
    }
    append_compose_marker(storage, bookmark.label, WormUiComposeBookmarkView{
            .bookmark_id = bookmark.id.value,
            .label = nullptr,
            .measure_number =
                static_cast<std::uint64_t>(std::distance(measures.entries.begin(), measure) + 1),
            .row = bookmark.position->row,
            .chart_row = measure->absolute_begin_row + bookmark.position->row,
            .projection_visible = false,
            .projected_y_pixels = 0.0,
            .proposed_projection_visible = false,
            .proposed_y_pixels = 0.0,
        });
    return true;
}
bool populate_compose_bookmark_storage(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::measure_index_t& measures, WormUiComposeView& view) {
    compose_marker_storage_t beat{flattened, flattened.compose.bookmark, view.beat_bookmark_count,
        view.beat_bookmarks_truncated, WORM_UI_COMPOSE_BOOKMARK_CAPACITY};
    compose_marker_storage_t audio{flattened, flattened.compose.audio_marker.bookmark, view.audio_bookmark_count,
        view.audio_bookmarks_truncated, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY};
    prepare_compose_marker_storage(beat, static_cast<std::size_t>(std::ranges::count(
        chart.bookmarks, wormedit::marker_anchor_t::beat, &wormedit::bookmark_t::anchor)));
    prepare_compose_marker_storage(audio, static_cast<std::size_t>(std::ranges::count(
        chart.bookmarks, wormedit::marker_anchor_t::audio_time, &wormedit::bookmark_t::anchor)));
    view.beat_bookmarks = flattened.compose.bookmark.data();
    view.audio_bookmarks = flattened.compose.audio_marker.bookmark.data();
    for (const auto& bookmark : chart.bookmarks) {
        if (bookmark.anchor == wormedit::marker_anchor_t::audio_time) {
            if (!populate_compose_audio_bookmark(audio, bookmark, view)) return false;
        } else if (bookmark.anchor == wormedit::marker_anchor_t::beat) {
            if (!populate_compose_beat_bookmark(beat, bookmark, measures, view)) return false;
        }
    }
    return true;
}

auto populate_compose_audio_section(compose_marker_storage_t<WormUiComposeAudioSectionView>& storage,
                                    const wormedit::section_t& section, WormUiComposeView& view) -> bool {
    if (!section.audio_millisecond_span || section.beat_span ||
        section.audio_millisecond_span->second <= section.audio_millisecond_span->first) {
        report_compose_projection(storage.flattened, projection_feedback_kind_t::audio_section,
            wormedit::notification_severity_t::error, "An audio-time section has inconsistent span data.", section.id.value);
        return false;
    }
    if (compose_marker_at_capacity(storage)) return true;
    append_compose_marker(storage, section.label, WormUiComposeAudioSectionView{
                .section_id = section.id.value,
                .label = nullptr,
                .begin_audio_milliseconds = section.audio_millisecond_span->first,
                .end_audio_milliseconds = section.audio_millisecond_span->second,
                .begin_projection_visible = false,
                .end_projection_visible = false,
                .span_projection_visible = false,
                .projected_begin_y_pixels = 0.0,
                .projected_end_y_pixels = 0.0,
                .projected_span_top_y_pixels = 0.0,
                .projected_span_bottom_y_pixels = 0.0,
            });
    return true;
}
auto populate_compose_beat_section(compose_marker_storage_t<WormUiComposeSectionView>& storage,
                                   const wormedit::section_t& section,
                                   const wormedit::measure_index_t& measures, WormUiComposeView& view) -> bool {
    if (!section.beat_span) {
        report_compose_projection(storage.flattened, projection_feedback_kind_t::beat_section_position,
            wormedit::notification_severity_t::error, "A beat-anchored section has no valid chart span.", section.id.value);
        return false;
    }
    if (compose_marker_at_capacity(storage)) return true;
    const auto begin_measure = std::ranges::find(measures.entries, section.beat_span->begin.measure,
                                                &wormedit::measure_index_entry_t::id);
    const auto end_measure = std::ranges::find(measures.entries, section.beat_span->end.measure,
                                              &wormedit::measure_index_entry_t::id);
    if (begin_measure == measures.entries.end() || end_measure == measures.entries.end() ||
        section.beat_span->begin.row >= begin_measure->row_capacity ||
        section.beat_span->end.row >= end_measure->row_capacity) {
        report_compose_projection(storage.flattened, projection_feedback_kind_t::beat_section_geometry,
            wormedit::notification_severity_t::error, "A beat-anchored section is outside the chart measure geometry.", section.id.value);
        return false;
    }
    append_compose_marker(storage, section.label, WormUiComposeSectionView{
            .section_id = section.id.value,
            .label = nullptr,
            .begin_measure_number = static_cast<std::uint64_t>(
                std::distance(measures.entries.begin(), begin_measure) + 1),
            .begin_row = section.beat_span->begin.row,
            .end_measure_number = static_cast<std::uint64_t>(
                std::distance(measures.entries.begin(), end_measure) + 1),
            .end_row = section.beat_span->end.row,
            .begin_chart_row = begin_measure->absolute_begin_row + section.beat_span->begin.row,
            .end_chart_row = end_measure->absolute_begin_row + section.beat_span->end.row,
            .begin_projection_visible = false,
            .end_projection_visible = false,
            .span_projection_visible = false,
            .projected_begin_y_pixels = 0.0,
            .projected_end_y_pixels = 0.0,
            .projected_span_top_y_pixels = 0.0,
            .projected_span_bottom_y_pixels = 0.0,
            .proposed_begin_projection_visible = false,
            .proposed_end_projection_visible = false,
            .proposed_span_projection_visible = false,
            .proposed_begin_y_pixels = 0.0,
            .proposed_end_y_pixels = 0.0,
            .proposed_span_top_y_pixels = 0.0,
            .proposed_span_bottom_y_pixels = 0.0,
        });
    return true;
}
bool populate_compose_section_storage(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::measure_index_t& measures, WormUiComposeView& view) {
    compose_marker_storage_t beat{flattened, flattened.compose.section, view.beat_section_count,
        view.beat_sections_truncated, WORM_UI_COMPOSE_SECTION_CAPACITY};
    compose_marker_storage_t audio{flattened, flattened.compose.audio_marker.section, view.audio_section_count,
        view.audio_sections_truncated, WORM_UI_COMPOSE_AUDIO_MARKER_CAPACITY};
    prepare_compose_marker_storage(beat, static_cast<std::size_t>(std::ranges::count(
        chart.sections, wormedit::marker_anchor_t::beat, &wormedit::section_t::anchor)));
    prepare_compose_marker_storage(audio, static_cast<std::size_t>(std::ranges::count(
        chart.sections, wormedit::marker_anchor_t::audio_time, &wormedit::section_t::anchor)));
    view.beat_sections = flattened.compose.section.data();
    view.audio_sections = flattened.compose.audio_marker.section.data();
    for (const auto& section : chart.sections) {
        if (section.anchor == wormedit::marker_anchor_t::audio_time) {
            if (!populate_compose_audio_section(audio, section, view)) return false;
        } else if (section.anchor == wormedit::marker_anchor_t::beat) {
            if (!populate_compose_beat_section(beat, section, measures, view)) return false;
        }
    }
    return true;
}

bool populate_compose_bookmark_and_section_storage(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::measure_index_t& measures, WormUiComposeView& view) {
    return populate_compose_bookmark_storage(flattened, chart, measures, view) &&
           populate_compose_section_storage(flattened, chart, measures, view);
}

auto compose_timing_short_number(wormedit::signed_rational_t value) -> std::string {
    if (value.denominator == 0) return "?";
    std::array<char, 96> buffer{};
    const auto number = static_cast<double>(value.numerator) /
                        static_cast<double>(value.denominator);
    const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number,
                                         std::chars_format::fixed, 2);
    if (formatted.ec != std::errc{}) return "?";
    std::string result(buffer.data(), formatted.ptr);
    if (result.find('.') != std::string::npos) {
        while (result.back() == '0') result.pop_back();
        if (result.back() == '.') result.pop_back();
    }
    return result == "-0" ? "0" : result;
}

auto compose_timing_exact_number(wormedit::signed_rational_t value) -> std::string {
    if (value.denominator == 0) return "invalid fraction";
    std::string fraction = std::to_string(value.numerator);
    if (value.denominator != 1) {
        fraction += "/" + std::to_string(value.denominator);
    }
    return fraction;
}

auto compose_timing_decimal_detail(wormedit::signed_rational_t value) -> std::string {
    if (value.denominator == 0) return "?";
    const auto exact = compose_timing_exact_number(value);
    if (value.denominator == 1) return exact;
    std::array<char, 96> buffer{};
    const auto number = static_cast<double>(value.numerator) /
                        static_cast<double>(value.denominator);
    const auto formatted = std::to_chars(buffer.data(), buffer.data() + buffer.size(), number,
                                         std::chars_format::general,
                                         std::numeric_limits<double>::max_digits10);
    if (formatted.ec != std::errc{}) return exact;
    const std::string decimal(buffer.data(), formatted.ptr);
    return decimal + " (exact " + exact + ")";
}

// " · transition 2 beats" (or seconds) for a SPEEDS event that interpolates; empty when instant.
auto compose_speed_transition_detail(const wormedit::speed_event_t& event) -> std::string {
    if (event.transition_beats.numerator == 0 || event.transition_beats.denominator == 0) return {};
    std::string text = " · transition " + std::to_string(event.transition_beats.numerator);
    if (event.transition_beats.denominator != 1)
        text += "/" + std::to_string(event.transition_beats.denominator);
    return text + (event.transition_is_seconds ? " s" : " beats");
}

auto compose_pause_unit_label(wormedit::pause_unit_t unit) -> std::string_view {
    switch (unit) {
    case wormedit::pause_unit_t::seconds: return "s";
    case wormedit::pause_unit_t::milliseconds: return "ms";
    case wormedit::pause_unit_t::bms_stop_ticks: return "ticks";
    case wormedit::pause_unit_t::beats: return "beats";
    }
    return "";
}

auto compose_initial_bpm(const wormedit::chart_document_t& chart,
                         const wormedit::measure_index_t& measures)
    -> const wormedit::bpm_event_t* {
    const wormedit::bpm_event_t* initial = nullptr;
    for (const auto& bpm : chart.bpms) {
        if (wormedit::absolute_row(measures, bpm.position) == 0 &&
            bpm.beats_per_minute.numerator > 0 && bpm.beats_per_minute.denominator != 0 &&
            (!initial || bpm.provenance.source_order >= initial->provenance.source_order)) {
            initial = &bpm;
        }
    }
    return initial;
}

template <typename AppendMarker>
void append_compose_initial_bpm(const wormedit::chart_document_t& chart,
                                const wormedit::bpm_event_t* initial,
                                AppendMarker&& append) {
    if (initial) {
        const auto index = static_cast<std::size_t>(initial - chart.bpms.data());
        append(initial->position, initial->id.value, initial->provenance.source_order, index,
               WORM_UI_TIMING_LANE_BPM,
               compose_timing_short_number(initial->beats_per_minute),
               "BPM " + compose_timing_decimal_detail(initial->beats_per_minute), true);
    } else if (!chart.measures.empty()) {
        append(wormedit::local_position_t{chart.measures.front().id, 0}, 0, 0, 0,
               WORM_UI_TIMING_LANE_BPM, "?", "Initial BPM missing", true);
    }
}

template <typename AppendMarker>
void append_compose_chart_timing_markers(const wormedit::chart_document_t& chart,
                                         const wormedit::measure_index_t& measures,
                                         AppendMarker&& append) {
    const auto* initial_bpm = compose_initial_bpm(chart, measures);
    append_compose_initial_bpm(chart, initial_bpm, append);
    for (std::size_t index = 0; index < chart.bpms.size(); ++index) {
        if (&chart.bpms[index] == initial_bpm) continue;
        append(chart.bpms[index].position, chart.bpms[index].id.value,
               chart.bpms[index].provenance.source_order, index, WORM_UI_TIMING_LANE_BPM,
               compose_timing_short_number(chart.bpms[index].beats_per_minute),
               "BPM " + compose_timing_decimal_detail(chart.bpms[index].beats_per_minute),
               false);
    }
    for (std::size_t index = 0; index < chart.scrolls.size(); ++index)
        append(chart.scrolls[index].position, chart.scrolls[index].id.value,
               chart.scrolls[index].provenance.source_order, index, WORM_UI_TIMING_LANE_SCROLL,
               compose_timing_short_number(chart.scrolls[index].multiplier),
               "Scroll " + compose_timing_decimal_detail(chart.scrolls[index].multiplier), false);
    for (std::size_t index = 0; index < chart.speeds.size(); ++index)
        append(chart.speeds[index].position, chart.speeds[index].id.value,
               chart.speeds[index].provenance.source_order, index, WORM_UI_TIMING_LANE_SPEED,
               compose_timing_short_number(chart.speeds[index].multiplier),
               "Speed " + compose_timing_decimal_detail(chart.speeds[index].multiplier) +
                   compose_speed_transition_detail(chart.speeds[index]), false);
    for (std::size_t index = 0; index < chart.pauses.size(); ++index)
        append(chart.pauses[index].position, chart.pauses[index].id.value,
               chart.pauses[index].provenance.source_order, index,
               chart.pauses[index].kind == wormedit::pause_kind_t::stop
                   ? WORM_UI_TIMING_LANE_STOP
                   : WORM_UI_TIMING_LANE_DELAY,
               compose_timing_short_number(chart.pauses[index].duration),
               std::string(chart.pauses[index].kind == wormedit::pause_kind_t::stop ? "Stop "
                                                                                   : "Delay ") +
                   compose_timing_decimal_detail(chart.pauses[index].duration) + " " +
                   std::string(compose_pause_unit_label(chart.pauses[index].source_unit)), false);
    for (std::size_t index = 0; index < chart.warps.size(); ++index) {
        const auto begin = wormedit::absolute_row(measures, chart.warps[index].span.begin);
        const auto end = wormedit::absolute_row(measures, chart.warps[index].span.end);
        const auto rows = begin && end && *end >= *begin ? *end - *begin : 0;
        append(chart.warps[index].span.begin, chart.warps[index].id.value,
               chart.warps[index].provenance.source_order, index, WORM_UI_TIMING_LANE_WARP,
               std::to_string(rows), "Warp " + std::to_string(rows) + " rows", false);
    }
    // Kiai events are not markers: Compose draws each merged Kiai area instead (see
    // populate_compose_kiai_and_sv), and edits it as one area.
    for (std::size_t index = 0; index < chart.timing_anchors.size(); ++index)
        append(chart.timing_anchors[index].position, chart.timing_anchors[index].id.value,
               index, index, WORM_UI_TIMING_LANE_ANCHOR,
               std::to_string(chart.timing_anchors[index].source_sample_frame),
               "Anchor at sample frame " +
                   std::to_string(chart.timing_anchors[index].source_sample_frame) +
                   (chart.timing_anchors[index].locked ? " (locked)" : ""), false);
}

void assign_compose_timing_marker_fractions(flattened_workspace_t& flattened) {
    std::ranges::sort(flattened.compose.timing_marker, [](const auto& left, const auto& right) {
        return std::tie(left.lane, left.marker_y_pixels, left.source_order, left.storage_order,
                        left.object_id) <
               std::tie(right.lane, right.marker_y_pixels, right.source_order, right.storage_order,
                        right.object_id);
    });
    for (std::size_t first = 0; first < flattened.compose.timing_marker.size();) {
        auto after = first + 1;
        while (after < flattened.compose.timing_marker.size() &&
               flattened.compose.timing_marker[after].lane ==
                   flattened.compose.timing_marker[first].lane &&
               flattened.compose.timing_marker[after].marker_y_pixels ==
                   flattened.compose.timing_marker[first].marker_y_pixels) {
            ++after;
        }
        const auto count = static_cast<double>(after - first);
        for (auto index = first; index < after; ++index) {
            flattened.compose.timing_marker[index].marker_left_fraction =
                static_cast<double>(index - first) / count;
            flattened.compose.timing_marker[index].marker_right_fraction =
                static_cast<double>(index - first + 1) / count;
        }
        first = after;
    }
}

auto compose_marker_selection_kind(WormUiTimingLaneKind lane)
    -> wormedit::editor_selection_object_kind_t {
    using kind_t = wormedit::editor_selection_object_kind_t;
    switch (lane) {
    case WORM_UI_TIMING_LANE_BPM: return kind_t::bpm_event;
    case WORM_UI_TIMING_LANE_SCROLL: return kind_t::scroll_event;
    case WORM_UI_TIMING_LANE_SPEED: return kind_t::speed_event;
    case WORM_UI_TIMING_LANE_STOP: return kind_t::stop_event;
    case WORM_UI_TIMING_LANE_DELAY: return kind_t::delay_event;
    case WORM_UI_TIMING_LANE_WARP: return kind_t::warp_event;
    case WORM_UI_TIMING_LANE_KIAI: return kind_t::kiai_event;
    case WORM_UI_TIMING_LANE_ANCHOR: return kind_t::timing_anchor;
    case WORM_UI_TIMING_LANE_COUNT: break;
    }
    return kind_t::bpm_event;
}

auto compose_timing_marker_selected(const wormedit::editor_interaction_view_model_t& interaction,
                                    WormUiTimingLaneKind lane, std::uint64_t id) -> bool {
    if (id == 0) return false;
    const auto kind = compose_marker_selection_kind(lane);
    return std::ranges::any_of(interaction.selected_objects, [kind, id](const auto& object) {
        return object.kind == kind && object.id.value == id;
    });
}

auto compose_bga_event_selected(const wormedit::editor_interaction_view_model_t& interaction,
                                std::uint64_t id) -> bool {
    return id != 0 && std::ranges::any_of(interaction.selected_objects, [id](const auto& object) {
        return object.kind == wormedit::editor_selection_object_kind_t::bga_event &&
               object.id.value == id;
    });
}

struct compose_marker_projection_context_t {
    flattened_workspace_t& flattened;
    const wormedit::chart_document_t& chart;
    const compose_scene_cache_t& cache;
    const wormedit::editor_interaction_view_model_t& interaction_view;
    WormUiComposeView& view;
    bool downscroll;
};

void project_compose_beat_markers(const compose_marker_projection_context_t& input) {
    auto& flattened = input.flattened;
    const auto& chart = input.chart;
    const auto& interaction_view = input.interaction_view;
    auto& view = input.view;
    const auto downscroll = input.downscroll;
    const auto beat_pixels_per_row =
        interaction_view.pixels_per_standard_measure / chart.divisions_per_standard_measure;
    const auto visible_beat_y = [&](std::uint64_t row) -> std::optional<double> {
        const auto y = compose_project_y(row, beat_pixels_per_row,
                                         interaction_view.viewport_scroll_pixels,
                                         view.viewport_height_pixels, downscroll);
        if (!std::isfinite(y) || y < 0.0 || y > view.viewport_height_pixels)
            return std::nullopt;
        return y;
    };
    for (std::size_t index = 0; index < view.beat_bookmark_count; ++index) {
        auto& bookmark = flattened.compose.bookmark[index];
        const auto y = visible_beat_y(bookmark.chart_row);
        bookmark.projection_visible = y.has_value();
        bookmark.projected_y_pixels = y.value_or(0.0);
    }
    for (std::size_t index = 0; index < view.beat_section_count; ++index) {
        auto& section = flattened.compose.section[index];
        const auto begin_y = compose_project_y(section.begin_chart_row, beat_pixels_per_row,
                                               interaction_view.viewport_scroll_pixels,
                                               view.viewport_height_pixels, downscroll);
        const auto end_y = compose_project_y(section.end_chart_row, beat_pixels_per_row,
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
        section.begin_projection_visible =
            std::isfinite(begin_y) && begin_y >= 0.0 && begin_y <= view.viewport_height_pixels;
        section.end_projection_visible =
            std::isfinite(end_y) && end_y >= 0.0 && end_y <= view.viewport_height_pixels;
        section.projected_begin_y_pixels = section.begin_projection_visible ? begin_y : 0.0;
        section.projected_end_y_pixels = section.end_projection_visible ? end_y : 0.0;
        const auto span_top = std::min(begin_y, end_y);
        const auto span_bottom = std::max(begin_y, end_y);
        section.span_projection_visible = std::isfinite(begin_y) && std::isfinite(end_y) &&
                                          span_bottom >= 0.0 &&
                                          span_top <= view.viewport_height_pixels;
        if (section.span_projection_visible) {
            section.projected_span_top_y_pixels =
                std::clamp(span_top, 0.0, view.viewport_height_pixels);
            section.projected_span_bottom_y_pixels =
                std::clamp(span_bottom, 0.0, view.viewport_height_pixels);
        }
    }

}

std::optional<double> project_compose_audio_marker_y(
    const compose_marker_projection_context_t& input, const auto& timing_index,
    std::int64_t milliseconds) {
    const auto projected = wormedit::musical_position_at(
        timing_index, wormedit::signed_rational_t{milliseconds, 1'000});
    if (!projected || !projected.absolute_row || projected.absolute_row->denominator == 0)
        return std::nullopt;
    const auto row = static_cast<long double>(projected.absolute_row->numerator) /
                     static_cast<long double>(projected.absolute_row->denominator);
    const auto pixels_per_row =
        static_cast<long double>(input.interaction_view.pixels_per_standard_measure) /
        static_cast<long double>(input.chart.divisions_per_standard_measure);
    const auto y = compose_project_y(row, static_cast<double>(pixels_per_row),
                                     input.interaction_view.viewport_scroll_pixels,
                                     input.view.viewport_height_pixels, input.downscroll);
    if (!std::isfinite(y) ||
        y < static_cast<long double>(std::numeric_limits<double>::lowest()) ||
        y > static_cast<long double>(std::numeric_limits<double>::max())) return std::nullopt;
    return static_cast<double>(y);
}

bool compose_audio_marker_y_visible(double y, const WormUiComposeView& view) {
    return std::isfinite(y) && y >= 0.0 && y <= view.viewport_height_pixels;
}

void project_compose_audio_bookmarks(const compose_marker_projection_context_t& input,
                                    const auto& timing_index) {
    for (std::size_t index = 0; index < input.view.audio_bookmark_count; ++index) {
        auto& bookmark = input.flattened.compose.audio_marker.bookmark[index];
        const auto y = project_compose_audio_marker_y(input, timing_index,
                                                      bookmark.audio_milliseconds);
        if (y && compose_audio_marker_y_visible(*y, input.view)) {
            bookmark.projection_visible = true;
            bookmark.projected_y_pixels = *y;
        }
    }
}

void project_compose_audio_sections(const compose_marker_projection_context_t& input,
                                   const auto& timing_index) {
    auto& view = input.view;
    for (std::size_t index = 0; index < view.audio_section_count; ++index) {
        auto& section = input.flattened.compose.audio_marker.section[index];
        const auto begin = project_compose_audio_marker_y(input, timing_index,
                                                          section.begin_audio_milliseconds);
        const auto end = project_compose_audio_marker_y(input, timing_index,
                                                        section.end_audio_milliseconds);
        if (!begin || !end) continue;
        section.begin_projection_visible = compose_audio_marker_y_visible(*begin, view);
        section.end_projection_visible = compose_audio_marker_y_visible(*end, view);
        if (section.begin_projection_visible) section.projected_begin_y_pixels = *begin;
        if (section.end_projection_visible) section.projected_end_y_pixels = *end;
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

void project_compose_audio_markers(const compose_marker_projection_context_t& input) {
    if (!input.cache.marker_timing_index) return;
    const auto& timing_index = *input.cache.marker_timing_index;
    project_compose_audio_bookmarks(input, timing_index);
    project_compose_audio_sections(input, timing_index);
}

void project_compose_markers(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const compose_scene_cache_t& cache,
    const wormedit::editor_interaction_view_model_t& interaction_view, WormUiComposeView& view,
    bool downscroll) {
    const compose_marker_projection_context_t input{
        flattened, chart, cache, interaction_view, view, downscroll};
    project_compose_beat_markers(input);
    project_compose_audio_markers(input);
}

void populate_compose_timing_markers(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const compose_scene_cache_t& cache,
    const wormedit::editor_interaction_view_model_t& interaction_view,
    WormUiComposeView& view, bool downscroll, double pixels_per_row,
    std::uint64_t canvas_rows,
    const wormedit::timing_lane_pseudonote_preview_t* timing_stage) {
    flattened.compose.timing_marker.clear();
    flattened.compose.timing_marker_labels.clear();
    const auto marker_total = chart.bpms.size() + chart.scrolls.size() + chart.speeds.size() +
                              chart.pauses.size() + chart.warps.size() + chart.kiai.size() +
                              chart.timing_anchors.size() + 1;
    flattened.compose.timing_marker_labels.reserve(
        std::min<std::size_t>(marker_total, WORM_UI_TIMING_MARKER_CAPACITY));
    if (canvas_rows != 0 && pixels_per_row > 0.0) {
        const auto total_rows = static_cast<double>(canvas_rows);
        view.timing_document_begin_fraction = std::clamp(
            interaction_view.viewport_scroll_pixels / pixels_per_row / total_rows, 0.0, 1.0);
        view.timing_document_end_fraction = std::clamp(
            (interaction_view.viewport_scroll_pixels + view.viewport_height_pixels) /
                pixels_per_row / total_rows,
            view.timing_document_begin_fraction, 1.0);
    }
    const auto append_timing_marker = [&](wormedit::local_position_t position, std::uint64_t id,
                                          std::size_t source_order, std::size_t storage_order,
                                          WormUiTimingLaneKind lane, std::string short_label,
                                          std::string detail, bool fixed_initial) {
        if (flattened.compose.timing_marker.size() >= WORM_UI_TIMING_MARKER_CAPACITY) {
            view.truncated = true;
            return;
        }
        const auto row = wormedit::absolute_row(cache.index->measures, position);
        if (!row) {
            return;
        }
        const auto y = compose_project_y(*row, pixels_per_row,
                                         interaction_view.viewport_scroll_pixels,
                                         view.viewport_height_pixels, downscroll);
        if (!std::isfinite(y) || y < 0.0 || y > view.viewport_height_pixels) {
            return;
        }
        auto& labels = flattened.compose.timing_marker_labels.emplace_back(
            std::move(short_label), std::move(detail));
        flattened.compose.timing_marker.push_back({
            .object_id = id,
            .source_order = static_cast<std::uint64_t>(source_order),
            .storage_order = static_cast<std::uint64_t>(storage_order),
            .marker_y_pixels = y,
            .marker_left_fraction = 0.0,
            .marker_right_fraction = 1.0,
            .chart_row = *row,
            .lane = lane,
            .short_label = labels.first.c_str(),
            .detail = labels.second.c_str(),
            .fixed_initial = fixed_initial,
            .selected = compose_timing_marker_selected(interaction_view, lane, id),
        });
    };
    append_compose_chart_timing_markers(chart, cache.index->measures, append_timing_marker);
    assign_compose_timing_marker_fractions(flattened);
    view.timing_markers = flattened.compose.timing_marker.data();
    view.timing_marker_count = flattened.compose.timing_marker.size();
    if (timing_stage &&
        timing_stage->status == wormedit::timing_lane_pseudonote_status_t::editing &&
        timing_stage->chart == chart.id &&
        timing_stage->document_revision == view.document_revision) {
        const auto stage_row = wormedit::absolute_row(cache.index->measures, timing_stage->position);
        if (stage_row) {
            const auto y = compose_project_y(*stage_row, pixels_per_row,
                                             interaction_view.viewport_scroll_pixels,
                                             view.viewport_height_pixels, downscroll);
            if (std::isfinite(y) && y >= 0.0 && y <= view.viewport_height_pixels) {
                view.timing_stage_active = true;
                view.timing_stage_valid = timing_stage->valid;
                view.timing_stage_lane = timing_lane_kind_to_ui(timing_stage->kind);
                view.timing_stage_y_pixels = y;
            }
        }
    }
}

std::string dead_span_label(const wormedit::sys::dead_span_band_t& band) {
    using wormedit::sys::dead_span_kind_t;
    if (band.kind == dead_span_kind_t::warp) return "WARP";
    const auto seconds = static_cast<double>(band.seconds.numerator) /
                         static_cast<double>(band.seconds.denominator);
    char text[48]{};
    std::snprintf(text, sizeof(text), "%s %.3fs", band.kind == dead_span_kind_t::stop ? "STOP" : "DELAY",
                  seconds);
    return text;
}

void populate_compose_dead_spans(flattened_workspace_t& flattened, const compose_scene_cache_t& cache,
                                 const wormedit::editor_interaction_view_model_t& interaction_view,
                                 WormUiComposeView& view, bool downscroll, double pixels_per_row) {
    flattened.compose.dead_span.clear();
    flattened.compose.dead_span_labels.clear();
    view.dead_spans = nullptr;
    view.dead_span_count = 0;
    if (!cache.dead_spans) return;
    const auto bands = wormedit::sys::project_dead_span_bands(
        *cache.dead_spans, pixels_per_row, interaction_view.viewport_scroll_pixels,
        view.viewport_height_pixels, downscroll,
        wormedit::sys::dead_span_pause_band_pixels);
    const auto count = std::min<std::size_t>(bands.size(), WORM_UI_COMPOSE_DEAD_SPAN_CAPACITY);
    flattened.compose.dead_span.reserve(count);
    flattened.compose.dead_span_labels.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& band = bands[index];
        const auto& label = flattened.compose.dead_span_labels.emplace_back(dead_span_label(band));
        flattened.compose.dead_span.push_back({
            .bounds = {0.0, band.top, view.viewport_width_pixels, band.bottom},
            .kind = static_cast<std::uint8_t>(band.kind),
            .object_id = band.object.value,
            .label = label.c_str()});
    }
    view.dead_spans = flattened.compose.dead_span.data();
    view.dead_span_count = flattened.compose.dead_span.size();
    if (bands.size() > count) view.truncated = true;
}

// Kiai areas and the SV step graph as they sit in the current viewport. Both draw inside their
// timing lane, so only vertical geometry is projected here.
void populate_compose_kiai_and_sv(flattened_workspace_t& flattened, const compose_scene_cache_t& cache,
                                  const wormedit::editor_interaction_view_model_t& interaction_view,
                                  WormUiComposeView& view, bool downscroll, double pixels_per_row,
                                  std::uint64_t total_rows, std::uint64_t kiai_selected_head) {
    flattened.compose.kiai_span.clear();
    flattened.compose.sv_graph_point.clear();
    view.kiai_spans = nullptr;
    view.kiai_span_count = 0;
    view.sv_graph_points = nullptr;
    view.sv_graph_point_count = 0;
    view.sv_graph_one_fraction = -1.0;
    if (!(pixels_per_row > 0.0)) return;
    const auto scroll = interaction_view.viewport_scroll_pixels;
    const auto height = view.viewport_height_pixels;
    const auto row_to_y = [&](std::uint64_t row) {
        return compose_project_y(static_cast<long double>(row), pixels_per_row, scroll, height,
                                 downscroll);
    };
    if (cache.kiai_spans) {
        for (const auto& span : cache.kiai_spans->spans) {
            const auto begin_y = row_to_y(span.begin_row);
            const auto end_y = row_to_y(span.end_row);
            const auto top = std::min(begin_y, end_y);
            const auto bottom = std::max(begin_y, end_y);
            if (bottom < 0.0 || top > height) continue;
            flattened.compose.kiai_span.push_back({
                .bounds = {0.0, top, 0.0, bottom},
                .head_object_id = span.head.value,
                .tail_object_id = span.tail.value,
                .begin_row = span.begin_row,
                .end_row = span.end_row,
                .open_ended = span.open_ended,
                .head_visible = begin_y >= 0.0 && begin_y <= height,
                .tail_visible = !span.open_ended && end_y >= 0.0 && end_y <= height,
                .selected = span.head.value == kiai_selected_head});
            if (flattened.compose.kiai_span.size() >= WORM_UI_COMPOSE_KIAI_SPAN_CAPACITY) break;
        }
        view.kiai_spans = flattened.compose.kiai_span.data();
        view.kiai_span_count = flattened.compose.kiai_span.size();
    }
    if (cache.sv_steps && !cache.sv_steps->steps.empty()) {
        const auto first = static_cast<std::uint64_t>(std::max(0.0, std::floor(scroll / pixels_per_row)));
        const auto last = std::min<std::uint64_t>(
            total_rows, static_cast<std::uint64_t>(std::ceil((scroll + height) / pixels_per_row)));
        const auto graph = wormedit::sys::build_sv_graph(*cache.sv_steps, first, last, row_to_y);
        for (const auto& point : graph.points) {
            if (flattened.compose.sv_graph_point.size() >= WORM_UI_COMPOSE_SV_GRAPH_CAPACITY) break;
            flattened.compose.sv_graph_point.push_back({point.x, point.y});
        }
        view.sv_graph_points = flattened.compose.sv_graph_point.data();
        view.sv_graph_point_count = flattened.compose.sv_graph_point.size();
        view.sv_graph_minimum = graph.minimum;
        view.sv_graph_maximum = graph.maximum;
        view.sv_graph_one_fraction = graph.one_x;
    }
}

std::string recipe_area_label(const wormedit::sys::recipe_area_t& area) {
    std::string label = area.name;
    if (area.curve == wormedit::sv_recipe_curve_t::lua_expression) label += " (Lua)";
    if (!area.enabled) label += " (off)";
    return label;
}

// Stored SV recipes as areas in the SV (sv/scrolls output) or SPD (speeds output) lane, each with
// its relative value as a line graph normalized to what is visible.
void populate_compose_recipe_areas(flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
                                   const compose_scene_cache_t& cache,
                                   const wormedit::editor_interaction_view_model_t& interaction_view,
                                   WormUiComposeView& view, bool downscroll, double pixels_per_row,
                                   std::uint64_t total_rows, std::uint64_t selected_recipe,
                                   const recipe_form_preview_t* preview) {
    flattened.compose.recipe_area.clear();
    flattened.compose.recipe_graph_point.clear();
    flattened.compose.recipe_area_labels.clear();
    view.recipe_areas = nullptr;
    view.recipe_area_count = 0;
    view.recipe_graph_points = nullptr;
    view.recipe_graph_point_count = 0;
    if (!cache.recipe_areas || !(pixels_per_row > 0.0)) return;
    const auto scroll = interaction_view.viewport_scroll_pixels;
    const auto height = view.viewport_height_pixels;
    const auto row_to_y = [&](std::uint64_t row) {
        return compose_project_y(static_cast<long double>(row), pixels_per_row, scroll, height,
                                 downscroll);
    };
    const auto first = static_cast<std::uint64_t>(std::max(0.0, std::floor(scroll / pixels_per_row)));
    const auto last = std::min<std::uint64_t>(
        total_rows, static_cast<std::uint64_t>(std::ceil((scroll + height) / pixels_per_row)));
    flattened.compose.recipe_area_labels.reserve(cache.recipe_areas->areas.size());
    for (const auto& stored_area : cache.recipe_areas->areas) {
        const auto* shown = &stored_area;
        wormedit::sys::recipe_area_t previewed;
        const bool previewing = preview && preview->candidate.id == stored_area.recipe;
        if (previewing) {
            // The unapplied candidate replaces the stored values in this area's graph.
            previewed = stored_area;
            const auto sampled = wormedit::sample_sv_recipe(chart, preview->candidate);
            previewed.evaluable = static_cast<bool>(sampled) && !sampled.points.empty();
            previewed.points.clear();
            for (const auto& point : sampled.points)
                previewed.points.push_back({point.absolute_row, point.value});
            shown = &previewed;
        }
        const auto& area = *shown;
        if (area.end_row <= first || area.begin_row >= last) continue;
        if (flattened.compose.recipe_area.size() >= WORM_UI_COMPOSE_RECIPE_AREA_CAPACITY) break;
        const auto begin_y = row_to_y(area.begin_row);
        const auto end_y = row_to_y(area.end_row);
        const auto graph = wormedit::sys::build_recipe_graph(area, first, last, row_to_y);
        const auto graph_first = flattened.compose.recipe_graph_point.size();
        for (const auto& point : graph.points) {
            if (flattened.compose.recipe_graph_point.size() >= WORM_UI_COMPOSE_RECIPE_GRAPH_CAPACITY) break;
            flattened.compose.recipe_graph_point.push_back({point.x, point.y});
        }
        const auto& label = flattened.compose.recipe_area_labels.emplace_back(recipe_area_label(area));
        flattened.compose.recipe_area.push_back({
            .bounds = {0.0, std::min(begin_y, end_y), 0.0, std::max(begin_y, end_y)},
            .recipe_id = area.recipe.value,
            .label = label.c_str(),
            .lane = static_cast<std::uint8_t>(area.output == wormedit::sv_recipe_output_t::speeds
                                                  ? WORM_UI_TIMING_LANE_SPEED
                                                  : WORM_UI_TIMING_LANE_SCROLL),
            .curve = static_cast<std::uint8_t>(area.curve),
            .enabled = area.enabled,
            .selected = area.recipe.value == selected_recipe,
            .evaluable = area.evaluable,
            .begin_visible = begin_y >= 0.0 && begin_y <= height,
            .end_visible = end_y >= 0.0 && end_y <= height,
            .previewing = previewing,
            .graph_first = graph_first,
            .graph_count = flattened.compose.recipe_graph_point.size() - graph_first});
    }
    view.recipe_areas = flattened.compose.recipe_area.data();
    view.recipe_area_count = flattened.compose.recipe_area.size();
    view.recipe_graph_points = flattened.compose.recipe_graph_point.data();
    view.recipe_graph_point_count = flattened.compose.recipe_graph_point.size();
}

// SPEEDS transitions as spans in the SPD lane, those that reach the viewport.
void populate_compose_speed_spans(flattened_workspace_t& flattened, const compose_scene_cache_t& cache,
                                  const wormedit::editor_interaction_view_model_t& interaction_view,
                                  WormUiComposeView& view, bool downscroll, double pixels_per_row) {
    flattened.compose.speed_span.clear();
    flattened.compose.speed_graph_point.clear();
    view.speed_spans = nullptr;
    view.speed_span_count = 0;
    view.speed_graph_points = nullptr;
    view.speed_graph_point_count = 0;
    view.speed_graph_one_fraction = -1.0;
    if (!cache.speed_spans || !(pixels_per_row > 0.0)) return;
    const auto scroll = interaction_view.viewport_scroll_pixels;
    const auto height = view.viewport_height_pixels;
    if (!cache.speed_spans->spans.empty()) {
        const auto graph = wormedit::sys::build_speed_graph(
            *cache.speed_spans, std::max(0.0, scroll / pixels_per_row),
            (scroll + height) / pixels_per_row, [&](double row) {
                return compose_project_y(row, pixels_per_row, scroll, height, downscroll);
            });
        for (const auto& point : graph.points) {
            if (flattened.compose.speed_graph_point.size() >= WORM_UI_COMPOSE_SV_GRAPH_CAPACITY) break;
            flattened.compose.speed_graph_point.push_back({point.x, point.y});
        }
        view.speed_graph_points = flattened.compose.speed_graph_point.data();
        view.speed_graph_point_count = flattened.compose.speed_graph_point.size();
        view.speed_graph_one_fraction = graph.one_x;
    }
    for (const auto& span : cache.speed_spans->spans) {
        const auto begin_y = compose_project_y(span.begin_row, pixels_per_row, scroll, height, downscroll);
        const auto end_y = compose_project_y(span.end_row, pixels_per_row, scroll, height, downscroll);
        const auto top = std::min(begin_y, end_y);
        const auto bottom = std::max(begin_y, end_y);
        if (bottom < 0.0 || top > height) continue;
        if (flattened.compose.speed_span.size() >= WORM_UI_COMPOSE_SPEED_SPAN_CAPACITY) break;
        flattened.compose.speed_span.push_back({.bounds = {0.0, top, 0.0, bottom},
                                                .object_id = span.object.value,
                                                .multiplier = span.multiplier,
                                                .transition_length = span.transition_length,
                                                .transition_is_seconds = span.transition_is_seconds,
                                                .clipped = span.clipped});
    }
    view.speed_spans = flattened.compose.speed_span.data();
    view.speed_span_count = flattened.compose.speed_span.size();
}

// A preview belongs to one selected recipe at one document revision; anything else drops it.
void drop_stale_recipe_preview(application_command_state_t& application_state,
                               const wormedit::chart_document_t& chart, std::uint64_t revision) {
    auto& preview = application_state.recipe_preview;
    if (!preview) return;
    const bool current = preview->chart == chart.id && preview->revision == revision &&
                         preview->candidate.id.value == application_state.recipe_selected &&
                         std::ranges::any_of(chart.sv_recipes, [&](const auto& recipe) {
                             return recipe.id == preview->candidate.id;
                         });
    if (!current) preview.reset();
}

// The form of the selected recipe as stored; the frontend keeps its own edit buffers on top.
void populate_compose_recipe_form(flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
                                  const application_command_state_t& application_state,
                                  WormUiComposeView& view) {
    flattened.compose.recipe_form_text.clear();
    view.recipe_form = {};
    const auto recipe = std::ranges::find(chart.sv_recipes,
                                          wormedit::recipe_id_t{application_state.recipe_selected},
                                          &wormedit::sv_recipe_t::id);
    if (application_state.recipe_selected == 0 || recipe == chart.sv_recipes.end()) return;
    flattened.compose.recipe_form_text.reserve(3);
    const auto& name = flattened.compose.recipe_form_text.emplace_back(recipe->name);
    const auto& source = flattened.compose.recipe_form_text.emplace_back(recipe->expression_source);
    const auto& status = flattened.compose.recipe_form_text.emplace_back(
        application_state.recipe_preview ? application_state.recipe_preview->error : std::string{});
    view.recipe_form.visible = true;
    view.recipe_form.recipe_id = recipe->id.value;
    view.recipe_form.enabled = recipe->enabled;
    view.recipe_form.name = name.c_str();
    view.recipe_form.expression_source = source.c_str();
    view.recipe_form.status_text = status.empty() ? nullptr : status.c_str();
    view.recipe_form.form = wormedit::sys::recipe_form_from_recipe(*recipe);
}

void populate_compose_selection_preview(
    const compose_scene_cache_t& cache,
    std::optional<wormedit::editor_interaction_state_t>& retained_state,
    wormedit::editor_interaction_view_model_t& interaction_view, WormUiComposeView& view) {
    interaction_view.pending_hold = retained_state->pending_hold;
    interaction_view.pending_note_duration_gesture = retained_state->pending_note_duration_gesture;
    interaction_view.pending_note_move_gesture = retained_state->pending_note_move_gesture;
    interaction_view.pending_note_paint_gesture =
        retained_state->pending_note_paint_gesture
            ? std::optional{wormedit::note_paint_gesture_preview_t{
                  .document_revision =
                      retained_state->pending_note_paint_gesture->document_revision,
                  .targets = retained_state->pending_note_paint_gesture->targets,
              }}
            : std::nullopt;
    interaction_view.retained_selection_range = retained_state->retained_selection_range;
    interaction_view.pending_editor_area_selection =
        retained_state->pending_editor_area_selection
            ? std::optional{wormedit::editor_area_selection_preview_t{
                  .document_revision =
                      retained_state->pending_editor_area_selection->document_revision,
                  .left_pixels =
                      std::min(retained_state->pending_editor_area_selection->anchor_x_pixels,
                               retained_state->pending_editor_area_selection->current_x_pixels),
                  .top_pixels =
                      std::min(retained_state->pending_editor_area_selection->anchor_y_pixels,
                               retained_state->pending_editor_area_selection->current_y_pixels),
                  .right_pixels =
                      std::max(retained_state->pending_editor_area_selection->anchor_x_pixels,
                               retained_state->pending_editor_area_selection->current_x_pixels),
                  .bottom_pixels =
                      std::max(retained_state->pending_editor_area_selection->anchor_y_pixels,
                               retained_state->pending_editor_area_selection->current_y_pixels),
                  .notes = retained_state->pending_editor_area_selection->notes,
                  .objects = retained_state->pending_editor_area_selection->objects,
              }}
            : std::nullopt;
    interaction_view.selected_notes = retained_state->selected_notes;
    interaction_view.selected_objects = retained_state->selected_objects;
    if (retained_state->retained_selection_range) {
        const auto& retained = *retained_state->retained_selection_range;
        const auto begin_row = wormedit::absolute_row(cache.index->measures, retained.begin);
        const auto end_row = wormedit::absolute_row(cache.index->measures, retained.end);
        const auto begin_measure =
            std::ranges::find(cache.index->measures.entries, retained.begin.measure,
                              &wormedit::measure_index_entry_t::id);
        const auto end_measure =
            std::ranges::find(cache.index->measures.entries, retained.end.measure,
                              &wormedit::measure_index_entry_t::id);
        if (begin_row && end_row && *begin_row < *end_row &&
            begin_measure != cache.index->measures.entries.end() &&
            end_measure != cache.index->measures.entries.end()) {
            view.area_range_available = true;
            view.area_range_begin_row = *begin_row;
            view.area_range_end_row = *end_row;
            view.beat_section_selection_span_available = true;
            view.beat_section_selection_begin_measure_number = static_cast<std::uint64_t>(
                std::distance(cache.index->measures.entries.begin(), begin_measure) + 1);
            view.beat_section_selection_begin_row = retained.begin.row;
            view.beat_section_selection_end_measure_number = static_cast<std::uint64_t>(
                std::distance(cache.index->measures.entries.begin(), end_measure) + 1);
            view.beat_section_selection_end_row = retained.end.row;
        } else {
            retained_state->retained_selection_range.reset();
            retained_state->retained_canvas_selection_range.reset();
            interaction_view.retained_selection_range.reset();
            interaction_view.retained_canvas_selection_range.reset();
        }
    }
    view.selected_note_count = retained_state->selected_notes.size();
    view.selected_object_count = retained_state->selected_objects.size();
}

// Fills the quantize dry-run counts; the cache recomputes only when the selection, snap or
// document changes, so idle frames copy three integers.
static auto quantize_report_row_view(const quantize_report_row_t& row)
    -> WormUiComposeQuantizeReportRowView {
    return {.object_id = row.object.value,
            .lane_id = row.lane.value,
            .start_before_absolute_row = row.start_before_absolute_row,
            .start_after_absolute_row = row.start_after_absolute_row,
            .start_before = {.measure_id = row.start_before.measure.value,
                             .row = row.start_before.row},
            .start_after = {.measure_id = row.start_after.measure.value,
                            .row = row.start_after.row},
            .has_end_before = row.end_before.has_value(),
            .has_end_after = row.end_after.has_value(),
            .end_before = row.end_before
                              ? WormUiComposeQuantizePositionView{
                                    .measure_id = row.end_before->measure.value,
                                    .row = row.end_before->row}
                              : WormUiComposeQuantizePositionView{},
            .end_after = row.end_after
                             ? WormUiComposeQuantizePositionView{
                                   .measure_id = row.end_after->measure.value,
                                   .row = row.end_after->row}
                             : WormUiComposeQuantizePositionView{},
            .moved = row.moved,
            .collision = row.collision,
            .collision_peer_id = row.collision_peer ? row.collision_peer->value : 0,
            .has_collision_peer = row.collision_peer.has_value()};
}

static auto active_quantize_request(application_command_state_t& application,
                                    const wormedit::song_document_t& document,
                                    const wormedit::chart_document_t& chart,
                                    std::uint64_t active_snap)
    -> wormedit::quantize_request_t {
    using namespace wormedit;
    if (application.quantize_editor && application.quantize_editor->song == document.id &&
        application.quantize_editor->chart == chart.id) {
        auto request = application.quantize_editor->request;
        request.objects.clear();
        if (application.quantize_editor->preset_index &&
            *application.quantize_editor->preset_index >= document.quantization_presets.size())
            application.quantize_editor->preset_index.reset();
        if (application.quantize_editor->preset_index) {
            const auto& preset = document.quantization_presets[*application.quantize_editor->preset_index];
            request.divisions = preset.divisions;
            request.space = preset.space;
            request.tie = preset.tie;
            request.holds = preset.holds;
            application.quantize_editor->request = request;
        }
        return request;
    }
    return quantize_request_t{.divisions = active_snap};
}

static void populate_quantize_presets(compose_scene_cache_t& cache,
                                      const wormedit::song_document_t& document,
                                      WormUiComposeView& view) {
    cache.quantize_preset_views.clear();
    cache.quantize_preset_views.reserve(document.quantization_presets.size());
    for (const auto& preset : document.quantization_presets) {
        cache.quantize_preset_views.push_back({.name = preset.name.c_str(),
                                               .divisions = preset.divisions,
                                               .space = static_cast<std::uint8_t>(preset.space),
                                               .tie = static_cast<std::uint8_t>(preset.tie),
                                               .holds = static_cast<std::uint8_t>(preset.holds)});
    }
    view.quantize_presets = cache.quantize_preset_views.data();
    view.quantize_preset_count = cache.quantize_preset_views.size();
}

static void populate_quantize_applied_report(compose_scene_cache_t& cache,
                                             const wormedit::song_document_t& document,
                                             const wormedit::chart_document_t& chart,
                                             const application_command_state_t& application,
                                             WormUiComposeView& view) {
    cache.quantize_applied_row_views.clear();
    if (!application.quantize_applied_report ||
        application.quantize_applied_report->song != document.id ||
        application.quantize_applied_report->chart != chart.id) {
        view.quantize_applied_rows = nullptr;
        view.quantize_applied_row_count = view.quantize_applied_total_rows = 0;
        view.quantize_applied_rows_truncated = false;
        return;
    }
    const auto& report = *application.quantize_applied_report;
    cache.quantize_applied_row_views.reserve(report.rows.size());
    for (const auto& row : report.rows)
        cache.quantize_applied_row_views.push_back(quantize_report_row_view(row));
    view.quantize_applied_rows = cache.quantize_applied_row_views.data();
    view.quantize_applied_row_count = cache.quantize_applied_row_views.size();
    view.quantize_applied_total_rows = report.total_rows;
    view.quantize_applied_rows_truncated = report.truncated;
}

static void populate_quantize_preview(compose_scene_cache_t& cache,
                                      const wormedit::song_document_t& document,
                                      const wormedit::chart_document_t& chart,
                                      std::uint64_t revision,
                                      const wormedit::editor_interaction_state_t& state,
                                      application_command_state_t& application,
                                      WormUiComposeView& view) {
    view.quantize_preview_moved = view.quantize_preview_unchanged = 0;
    view.quantize_preview_blocked = 0;
    view.quantize_preview_colliding_objects = 0;
    view.quantize_preview_message[0] = '\0';
    cache.quantize_preview_row_views.clear();
    populate_quantize_presets(cache, document, view);
    populate_quantize_applied_report(cache, document, chart, application, view);
    const auto request = active_quantize_request(application, document, chart,
                                                state.active_snap_denominator);
    view.quantize_divisions = request.divisions;
    view.quantize_space = static_cast<std::uint8_t>(request.space);
    view.quantize_tie = static_cast<std::uint8_t>(request.tie);
    view.quantize_holds = static_cast<std::uint8_t>(request.holds);
    view.quantize_selected_preset_valid = application.quantize_editor &&
        application.quantize_editor->song == document.id &&
        application.quantize_editor->chart == chart.id &&
        application.quantize_editor->preset_index.has_value();
    view.quantize_selected_preset_index = view.quantize_selected_preset_valid
        ? *application.quantize_editor->preset_index : 0;
    if (state.selected_notes.empty()) {
        view.quantize_preview_rows = nullptr;
        view.quantize_preview_row_count = view.quantize_preview_total_rows = 0;
        view.quantize_preview_rows_truncated = false;
        return;
    }
    const auto& preview = cache.quantize_preview.get(chart, revision, state.selected_notes, request);
    view.quantize_preview_moved = preview.moved;
    view.quantize_preview_unchanged = preview.unchanged;
    view.quantize_preview_blocked = preview.blocked;
    view.quantize_preview_colliding_objects = preview.colliding_objects;
    cache.quantize_preview_row_views.reserve(preview.report_rows.size());
    for (const auto& row : preview.report_rows)
        cache.quantize_preview_row_views.push_back(quantize_report_row_view(row));
    view.quantize_preview_rows = cache.quantize_preview_row_views.data();
    view.quantize_preview_row_count = cache.quantize_preview_row_views.size();
    view.quantize_preview_total_rows = preview.report_total_rows;
    view.quantize_preview_rows_truncated = preview.report_truncated;
    std::snprintf(view.quantize_preview_message, sizeof(view.quantize_preview_message), "%s",
                  preview.message.c_str());
}

void populate_compose_staged_draft(flattened_workspace_t& flattened,
                                   application_command_state_t& application_state,
                                   WormUiComposeView& view);
void populate_compose_selected_bgm(
    const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    WormUiComposeView& view);
void populate_compose_selected_note(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    WormUiComposeView& view);

void populate_compose_selected_objects_and_drafts(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const wormedit::document_history_t& history,
    application_command_state_t& application_state,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    WormUiComposeView& view) {
    if (application_state.bmson_marker_draft &&
        (application_state.bmson_marker_draft->chart != chart.id ||
         application_state.bmson_marker_draft->revision != history.document.revision)) {
        application_state.bmson_marker_draft.reset();
        application_state.bmson_razor_stroke.reset();
    }
    populate_compose_staged_draft(flattened, application_state, view);
    if (application_state.bmson_marker_move_preview &&
        (application_state.bmson_marker_move_preview->chart != chart.id ||
         application_state.bmson_marker_move_preview->revision != history.document.revision)) {
        application_state.bmson_marker_move_preview.reset();
    }
    populate_compose_selected_bgm(chart, retained_state, view);
    populate_compose_selected_note(flattened, chart, retained_state, view);
}

void populate_compose_staged_draft(flattened_workspace_t& flattened,
                                   application_command_state_t& application_state,
                                   WormUiComposeView& view) {
    if (!application_state.bmson_marker_draft) return;
    view.staged_bmson_marker_count = application_state.bmson_marker_draft->markers.size();
    flattened.compose.bmson_draft_playable_lane.clear();
    flattened.compose.bmson_draft_restart.clear();
    flattened.compose.bmson_draft_playable_lane.reserve(view.staged_bmson_marker_count);
    flattened.compose.bmson_draft_restart.reserve(view.staged_bmson_marker_count);
    for (const auto& marker : application_state.bmson_marker_draft->markers) {
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
    WormUiComposeView& view) {
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
    for (const auto& measure : chart.measures) {
        const auto event = std::ranges::find(measure.bgm_events, selected,
                                              &wormedit::bgm_event_t::id);
        if (event == measure.bgm_events.end()) continue;
        view.selected_bgm_event_id = selected.value;
        view.selected_assigned_resource_id = event->sound.value_or(wormedit::resource_id_t{}).value;
        if (chart.source_family == wormedit::format_family_t::bmson && event->sound_channel_event) {
            view.selected_bmson_bgm_event_id = selected.value;
            view.selected_bmson_source_lane_id = event->source_lane.value;
        }
        break;
    }
}

void populate_compose_selected_note(
    flattened_workspace_t& flattened, const wormedit::chart_document_t& chart,
    const std::optional<wormedit::editor_interaction_state_t>& retained_state,
    WormUiComposeView& view) {
    if (retained_state->selected_notes.size() != 1) return;
    const auto note_id = retained_state->selected_notes.front();
    for (const auto& measure : chart.measures) {
        const auto note = std::ranges::find(measure.notes, note_id, &wormedit::note_t::id);
        if (note == measure.notes.end()) continue;
        if (note->label) flattened.compose.selected_note_label = *note->label;
        view.selected_note_label = flattened.compose.selected_note_label.c_str();
        view.selected_note_label_note_id = note_id.value;
        view.selected_assigned_resource_id = note->sound.value_or(wormedit::resource_id_t{}).value;
        if (chart.source_family == wormedit::format_family_t::bmson && note->sound_channel_event &&
            note->kind == wormedit::note_kind_t::tap)
            view.selected_bmson_note_event_id = note_id.value;
        break;
    }
}


auto cached_compose_measures(flattened_workspace_t& flattened,
                             const wormedit::chart_document_t& chart,
                             compose_scene_cache_t& cache) -> const wormedit::measure_index_t* {
    if (cache.cached_measure_index) return &*cache.cached_measure_index;
    auto measure_index = wormedit::build_measure_index(chart);
    if (!std::holds_alternative<wormedit::measure_index_t>(measure_index)) {
        const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(measure_index);
        report_compose_diagnostic(flattened, projection_feedback_kind_t::measure_geometry,
                                 diagnostics, "The chart measure geometry is invalid.");
        return nullptr;
    }
    cache.cached_measure_index = std::move(std::get<wormedit::measure_index_t>(measure_index));
    return &*cache.cached_measure_index;
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
        static_cast<bool>(application_state.transport_audio_milliseconds) &&
        static_cast<bool>(application_state.transport_seek_audio_milliseconds) &&
        application_state.transport_audio_milliseconds().has_value();
    if (application_state.semantic_layer_filter_chart != chart.id) {
        application_state.semantic_layer_filter_chart = chart.id;
        application_state.required_semantic_layer_filters.clear();
        application_state.excluded_semantic_layer_filters.clear();
    }
    if (application_state.semantic_layer_target &&
        std::ranges::find(chart.semantic_layers, *application_state.semantic_layer_target,
                          &wormedit::semantic_layer_t::id) == chart.semantic_layers.end()) {
        application_state.semantic_layer_target.reset();
        application_state.semantic_layer_paint_mode_active = false;
        application_state.semantic_layer_solo_active = false;
    }
    view.semantic_layer_paint_mode_active = application_state.semantic_layer_paint_mode_active;
    view.semantic_layer_solo_active = application_state.semantic_layer_solo_active;
    view.project_id = history.document.id.value;
    view.chart_id = chart.id.value;
    view.document_revision = history.document.revision;
    populate_note_layer_menu(flattened, chart, history.document.revision, application_state);
    if (!populate_semantic_layer_palette(flattened, chart, cache, history, application_state)) {
        return nullptr;
    }
    if (!cache.attempted || cache.chart != chart.id ||
        cache.revision != history.document.revision) {
        frame_phase_scope_t frame_scene_index(frame_phase_t::scene);
        cache.index.reset();
        cache.cached_measure_index.reset();
        cache.bgm_rows_by_id.clear();
        cache.hit_test_index.reset();
        cache.marker_timing_index.reset();
        cache.dead_spans.reset();
        cache.kiai_spans.reset();
        cache.sv_steps.reset();
        cache.recipe_areas.reset();
        cache.speed_spans.reset();
        cache.proposed_offset_timing_index.reset();
        cache.proposed_offset_seconds.reset();
        cache.beat_fit_drag.reset();
        cache.beat_fit_asset.reset();
        cache.proposed_beat_timing_index.reset();
        cache.proposed_beat_bpm.reset();
        cache.proposed_beat_event = {};
        cache.channel_waveform_channel = {};
        cache.channel_waveform_source = nullptr;
        cache.channel_waveform_source_generation = 0;
        cache.channel_waveform_slices.reset();
        cache.channel_waveform_projection_valid = false;
        cache.channel_waveform_rows.clear();
        cache.interaction_view.reset();
        cache.hover.reset();
        cache.focus_note_label_editor = false;
        cache.viewport_autoscroll_active = false;
        cache.projection_error.clear();
        cache.paint_pointer.reset();
        cache.chart = chart.id;
        cache.revision = history.document.revision;
        cache.attempted = true;
        const auto built =
            wormedit::build_editor_field_scene_index(chart, history.document.revision);
        if (std::holds_alternative<wormedit::editor_field_scene_index_t>(built)) {
            cache.index = std::get<wormedit::editor_field_scene_index_t>(built);
            cache.bgm_rows_by_id.reserve(cache.index->bgm_events.size());
            for (const auto& event : cache.index->bgm_events) {
                cache.bgm_rows_by_id.emplace(event.event.value, event.row);
            }
        } else {
            const auto& diagnostics = std::get<wormedit::diagnostic_list_t>(built);
            cache.projection_error = diagnostics.empty() ? "The field layout could not be indexed."
                                               : diagnostics.front().message;
            cache.projection_error_severity = projection_diagnostic_severity(diagnostics);
        }
        if (cache.index) {
            cache.kiai_spans = wormedit::sys::project_kiai_spans(
                chart, cache.index->measures, WORM_UI_COMPOSE_KIAI_SPAN_CAPACITY);
            cache.sv_steps = wormedit::sys::project_sv_steps(
                chart, cache.index->measures, WORM_UI_COMPOSE_SV_GRAPH_CAPACITY);
            cache.recipe_areas = wormedit::sys::project_recipe_areas(
                chart, cache.index->measures, WORM_UI_COMPOSE_RECIPE_AREA_CAPACITY);
        }
        auto timing = wormedit::build_timing_index(chart);
        if (timing) {
            cache.dead_spans = wormedit::sys::project_dead_spans(
                chart, cache.index->measures, *timing.index, WORM_UI_COMPOSE_DEAD_SPAN_CAPACITY);
            cache.speed_spans = wormedit::sys::project_speed_spans(
                chart, *timing.index, WORM_UI_COMPOSE_SPEED_SPAN_CAPACITY);
            cache.marker_timing_index = std::move(*timing.index);
        }
    }
    if (!cache.index) {
        report_compose_projection(flattened, projection_feedback_kind_t::field_index,
                                 cache.projection_error_severity, cache.projection_error);
        return nullptr;
    }
    view.has_bgm_events = !cache.index->bgm_events.empty();
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

struct compose_interaction_population_context_t {
    flattened_workspace_t& flattened;
    wormedit::document_history_t& history;
    const wormedit::chart_document_t& chart;
    const wormedit::measure_index_t& measures;
    std::optional<wormedit::editor_interaction_state_t>& retained_state;
    application_command_state_t& application_state;
    WormUiComposeView& view;
    bool downscroll;
};

bool initialize_compose_interaction(const compose_interaction_population_context_t& input) {
    auto& flattened = input.flattened;
    auto& history = input.history;
    const auto& chart = input.chart;
    const auto& measures = input.measures;
    auto& retained_state = input.retained_state;
    auto& application_state = input.application_state;
    auto& view = input.view;
    const auto downscroll = input.downscroll;
    const auto initial_interaction = [&]() -> wormedit::editor_interaction_setup_t {
        std::optional<wormedit::local_position_t> initial_cursor;
        std::optional<wormedit::lane_id_t> initial_lane;
        std::uint64_t earliest_layout_row = std::numeric_limits<std::uint64_t>::max();
        for (const auto& segment : chart.lane_layouts) {
            const auto row = wormedit::absolute_row(measures, segment.position);
            if (!row || *row >= earliest_layout_row)
                continue;
            const auto lane = std::ranges::find_if(segment.layout.lanes, compose_lane_is_editable);
            if (lane == segment.layout.lanes.end())
                continue;
            earliest_layout_row = *row;
            initial_cursor = segment.position;
            initial_lane = lane->id;
        }
        if (!initial_cursor || !initial_lane) {
            return {};
        }
        return wormedit::make_editor_interaction(
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
        const auto initial_cursor_row = *wormedit::absolute_row(measures, retained_state->cursor);
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
    const auto& chart = input.chart;
    auto& retained_state = input.retained_state;
    auto& application_state = input.application_state;
    auto& flattened = input.flattened;
    auto& view = input.view;
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
        retained_state->snap_enabled = application_state.edit_snap_enabled;
    }
    return true;
}

std::optional<wormedit::editor_interaction_view_model_t> inspect_compose_interaction(
    const compose_interaction_population_context_t& input) {
    auto& history = input.history;
    auto& retained_state = input.retained_state;
    auto& flattened = input.flattened;
    auto& view = input.view;
    const auto geometry_changed = retained_state->viewport_height_pixels != view.viewport_height_pixels ||
        retained_state->scroll_direction != (input.downscroll ? wormedit::editor_field_scroll_direction_t::down
                                                            : wormedit::editor_field_scroll_direction_t::up);
    wormedit::configure_cursor_viewport(*retained_state, view.viewport_height_pixels,
        input.downscroll ? wormedit::editor_field_scroll_direction_t::down
                         : wormedit::editor_field_scroll_direction_t::up);
    if (geometry_changed && !editor_interaction_has_pending_gesture(*retained_state))
        (void)wormedit::synchronize_editor_cursor_to_viewport(input.chart, input.measures, *retained_state);
    auto inspected = wormedit::inspect_editor_interaction(history, *retained_state);
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

struct compose_final_projection_context_t {
    flattened_workspace_t& flattened;
    wormedit::document_history_t& history;
    const wormedit::chart_document_t& chart;
    compose_scene_cache_t& cache;
    std::optional<wormedit::editor_interaction_state_t>& retained_state;
    application_command_state_t& application_state;
    WormUiComposeView& view;
    wormedit::editor_interaction_view_model_t& interaction_view;
    const wormedit::measure_index_t& measures;
    bool downscroll;
    const wormedit::timing_lane_pseudonote_preview_t* timing_stage;
    std::span<const wormedit::local_position_t> comparison_rows;
    const compose_bga_move_preview_input_t* bga_move_preview{};
};

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

std::uint64_t prepare_compose_projection_state(const compose_final_projection_context_t& input) {
    auto& flattened = input.flattened;
    const auto& history = input.history;
    const auto& chart = input.chart;
    auto& cache = input.cache;
    auto& retained_state = input.retained_state;
    auto& application_state = input.application_state;
    auto& view = input.view;
    auto& interaction_view = input.interaction_view;
    const auto downscroll = input.downscroll;
    project_compose_markers(flattened, chart, cache, interaction_view, view, downscroll);
    if (application_state.preview_playhead_absolute_row) {
        const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                    static_cast<double>(chart.divisions_per_standard_measure);
        const auto playhead_y = compose_project_y(
            *application_state.preview_playhead_absolute_row, pixels_per_row,
            interaction_view.viewport_scroll_pixels, view.viewport_height_pixels, downscroll);
        if (std::isfinite(playhead_y) && playhead_y >= 0.0 &&
            playhead_y <= view.viewport_height_pixels) {
            view.playhead_visible = true;
            view.playhead_y_pixels = playhead_y;
        }
    }
    populate_compose_selection_preview(cache, retained_state, interaction_view, view);
    populate_compose_selected_objects_and_drafts(flattened, chart, history,
                                                 application_state, retained_state, view);
    auto canvas_rows = cache.index->measures.total_rows;
    if (wormedit::chart_has_virtual_tail(chart)) {
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
        view.area_delete_supported = compose_area_delete_supported(chart, cache.index->measures, *retained_state);
    cache.interaction_view = interaction_view;
    const auto hover_gesture_active =
        retained_state->pending_hold.has_value() ||
        retained_state->pending_note_duration_gesture.has_value() ||
        retained_state->pending_note_move_gesture.has_value() ||
        retained_state->pending_note_paint_gesture.has_value() ||
        retained_state->pending_semantic_layer_paint_gesture.has_value() ||
        retained_state->pending_editor_area_selection.has_value() ||
        retained_state->pending_space_range_selection.has_value();
    if (!hover_gesture_active && cache.hover &&
        cache.hover->document_revision == history.document.revision &&
        cache.hover->object.kind == wormedit::editor_selection_object_kind_t::note) {
        view.hover_note_visible = true;
        view.hover_note_id = cache.hover->object.id.value;
        flattened.compose.hover_tooltip = cache.hover->tooltip;
        view.hover_tooltip = flattened.compose.hover_tooltip.c_str();
    }
    if (!hover_gesture_active && cache.focus_note_label_editor &&
        retained_state->selected_notes.size() == 1) {
        view.focus_note_label_editor = true;
    }
    return canvas_rows;
}

void populate_compose_pending_previews(const compose_final_projection_context_t& input) {
    const auto& chart = input.chart;
    auto& retained_state = input.retained_state;
    auto& view = input.view;
    auto& interaction_view = input.interaction_view;
    const auto& measures = input.measures;
    const auto downscroll = input.downscroll;
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
                                    : wormedit::absolute_row(measures, retained_state->cursor);
        if (cursor_row) {
            const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                        static_cast<double>(chart.divisions_per_standard_measure);
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

template<class T>
auto compose_vector_bytes(const std::vector<T>& values) -> std::size_t {
    return values.capacity() * sizeof(T);
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
    if (!cache.static_scene_storage) return;
    const auto& source = cache.static_scene_storage->view.compose;
    const auto origin = view.downscroll
        ? -cache.scene_window.view.viewport_scroll_pixels - source.viewport_height_pixels
        : cache.scene_window.view.viewport_scroll_pixels;
    view.static_scene = &source;
    view.static_scene_source_generation = cache.static_scene_source_generation;
    view.static_scene_world_origin_pixels = origin;
    // Source-window clipped objects cannot define complete tiles at its edges.
    double guard=96;
    for (std::size_t index=0; index<source.lane_count; ++index)
        guard=std::max(guard,(source.lanes[index].bounds.right-source.lanes[index].bounds.left)*.5+20);
    const bool chart_begin=cache.scene_window.view.viewport_scroll_pixels==0;
    view.static_scene_covered_begin_pixels = origin + (chart_begin && !view.downscroll ? 0 : guard);
    view.static_scene_covered_end_pixels = origin + source.viewport_height_pixels -
        (chart_begin && view.downscroll ? 0 : guard);
    view.static_scene_accounted_bytes = cache.static_scene_accounted_bytes +
        cache.static_projection_index_bytes;
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
    cache.static_scene_storage.reset();
    cache.static_projection_indices = {};
    cache.static_projection_scratch = {};
    cache.static_projection_source_generation = 0;
    cache.static_projection_index_bytes = 0;
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
    retained.downscroll = view.downscroll;
    retained.bga_lane_visible = view.bga_lane_visible;
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
    auto& view = projection.view;
    view.static_scene = nullptr;
    if (!cache.scene_window.scene || view.truncated) {
        discard_compose_static_source(cache);
        return;
    }
    if (cache.static_scene_storage &&
        cache.static_scene_rebuild_count == cache.scene_window.rebuild_count &&
        cache.static_scene_source_style == cache.static_scene_style_generation &&
        compose_static_source_layout_matches(cache.static_scene_storage->view.compose,view)) {
        publish_compose_static_source(cache, view);
        return;
    }
    if (!compose_static_labels_fit(projection.history)) {
        discard_compose_static_source(cache);
        return;
    }
    discard_compose_static_source(cache);
    auto storage = std::make_shared<flattened_workspace_t>();
    auto& retained = storage->view.compose;
    retained = make_compose_static_view(view, cache.scene_window.view);
    const compose_scene_projection_context_t expanded{
        *storage, projection.chart, projection.index, *cache.scene_window.scene,
        projection.history, projection.application_state, cache.scene_window.view, retained,
        projection.field_left_pixels, projection.playable_field_width,
        projection.pixels_per_row, projection.downscroll, false};
    populate_compose_sound_labels(expanded);
    project_compose_layout_and_notes(expanded);
    project_compose_bgm(expanded);
    project_compose_bga(expanded);
    populate_compose_timing_markers(*storage, projection.chart, cache, cache.scene_window.view,
        retained, projection.downscroll, projection.pixels_per_row, canvas_rows, nullptr);
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
    cache.static_scene_storage = std::move(storage);
    cache.static_scene_accounted_bytes = bytes;
    cache.static_scene_rebuild_count = cache.scene_window.rebuild_count;
    cache.static_scene_source_style = cache.static_scene_style_generation;
    ++cache.static_scene_source_generation;
    publish_compose_static_source(cache, view);
}

void refresh_compose_static_scene_style(compose_scene_cache_t& cache,WormUiComposeView& view) {
    const auto& previous_style=cache.scene_window.request;
    const auto& current_style=cache.preview_scene_request;
    if (!cache.scene_window.scene ||
        cache.static_scene_lane_legend != view.lane_key_legend_visible ||
        previous_style.solo_semantic_layer!=current_style.solo_semantic_layer ||
        previous_style.required_semantic_layers!=current_style.required_semantic_layers ||
        previous_style.excluded_semantic_layers!=current_style.excluded_semantic_layers)
    {
        // A reload can recreate the cache while retaining native chart IDs and
        // revision. Epochs distinguish that new source from retained GPU tiles.
        static std::atomic<std::uint64_t> next_generation{1};
        cache.static_scene_style_generation=next_generation.fetch_add(1,std::memory_order_relaxed);
    }
    cache.static_scene_lane_legend=view.lane_key_legend_visible;
    view.static_scene_style_generation=cache.static_scene_style_generation;
}

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
void refresh_scalar_projection_indices(compose_scene_cache_t& cache) {
    if (cache.static_projection_source_generation == cache.static_scene_source_generation)
        return;
    const auto& source = cache.static_scene_storage->compose;
    const auto bounds = [](const auto& record) {
        return std::pair{record.bounds.top, record.bounds.bottom};
    };
    rebuild_scalar_intervals(cache.static_projection_indices[0], source.note, bounds);
    rebuild_scalar_intervals(cache.static_projection_indices[1], source.hold, bounds);
    rebuild_scalar_intervals(cache.static_projection_indices[2], source.bgm.event, bounds);
    rebuild_scalar_intervals(cache.static_projection_indices[4], source.bga_event, bounds);
    rebuild_scalar_intervals(
        cache.static_projection_indices[3], source.grid.row,
        [](const auto& row) { return std::pair{double(row.whole_rows), double(row.whole_rows)}; });
    cache.static_projection_scratch.resize(std::max(
        {source.note.size(), source.hold.size(), source.bgm.event.size(),
         source.bga_event.size(), source.grid.row.size()}));
    cache.static_projection_index_bytes =
        cache.static_projection_scratch.capacity() * sizeof(std::size_t);
    for (const auto& index : cache.static_projection_indices)
        cache.static_projection_index_bytes += index.accounted_bytes();
    cache.static_projection_source_generation = cache.static_scene_source_generation;
}
auto scalar_projection_query(compose_scene_cache_t& cache, unsigned pass, double begin, double end)
    -> std::optional<std::span<const std::size_t>> {
    const auto query = cache.static_projection_indices[pass].query_into(
        begin, end, cache.static_projection_scratch);
    cache.static_projection_scanned += query.scanned;
    if (query.overflow)
        return std::nullopt;
    cache.static_projection_extracted += query.count;
    return std::span<const std::size_t>(cache.static_projection_scratch).first(query.count);
}
auto scalar_display_lane_number(const auto& record, const wormedit::editor_field_scene_t& scene)
    -> std::uint32_t {
    const auto x = (record.bounds.left + record.bounds.right) * .5;
    const auto y = (record.bounds.top + record.bounds.bottom) * .5;
    for (const auto& lane : scene.lanes)
        if (lane.lane.value == record.lane_id && x >= lane.bounds.left && x < lane.bounds.right &&
            y >= lane.bounds.top && y < lane.bounds.bottom)
            return lane.display_order + 1;
    return 0;
}
template<class T>
auto prepare_scalar_geometry_output(flattened_workspace_t& flattened, frame_span<T>& output,
                                    std::size_t count) -> bool {
    return prepare_compose_geometry_rows(flattened, output, count);
}
template<class T>
auto prepare_scalar_geometry_output(flattened_workspace_t&, std::vector<T>& output,
                                    std::size_t count) -> bool {
    output.reserve(count);
    return true;
}
template<class T>
auto append_scalar_geometry_output(frame_span<T>& output, const T& record) -> bool {
    return output.push_back(record);
}
template<class T>
auto append_scalar_geometry_output(std::vector<T>& output, const T& record) -> bool {
    output.push_back(record);
    return true;
}
template <class Records, class Transform>
auto extract_scalar_rectangles(compose_scene_cache_t& cache, flattened_workspace_t& flattened,
                               unsigned pass, const Records& source,
                               Records& output, double begin, double end, double shift,
                               Transform transform) -> bool {
    const auto indices = scalar_projection_query(cache, pass, begin - shift, end - shift);
    if (!indices)
        return false;
    if (!prepare_scalar_geometry_output(flattened, output, indices->size())) return false;
    for (const auto index : *indices) {
        auto record = source[index];
        record.bounds.top += shift;
        record.bounds.bottom += shift;
        if (transform(record, index) && !append_scalar_geometry_output(output, record)) return false;
    }
    return true;
}
auto extract_scalar_grid(compose_scene_cache_t& cache, const wormedit::editor_field_scene_t& scene,
                         flattened_workspace_t& flattened, double shift) -> bool {
    const auto indices = scalar_projection_query(cache, 3, double(scene.visible_begin_row),
                                                 double(scene.visible_end_row));
    if (!indices)
        return false;
    const auto& source = cache.static_scene_storage->compose.grid;
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
                            const wormedit::editor_field_scene_window_metadata_t& metadata,
                            flattened_workspace_t& flattened,
                            const wormedit::editor_interaction_view_model_t& interaction,
                            double shift, double height) -> bool {
    const auto& source = cache.static_scene_storage->compose;
    const auto& scene = *metadata.projection.scene;
    const auto& retained = *cache.scene_window.scene;
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
    const auto& scalar = projection.flattened.compose;
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
    if (!cache.static_scene_storage || !cache.scene_window.scene ||
        cache.static_scene_source_style != cache.static_scene_style_generation ||
        !compose_static_source_layout_matches(cache.static_scene_storage->view.compose, view))
        return false;
    const auto& source = cache.static_scene_storage->compose;
    const auto& retained = *cache.scene_window.scene;
    return source.note.size() == retained.note_endpoints.size() &&
           source.hold.size() == retained.hold_bodies.size() &&
           source.bgm.event.size() == retained.bgm_events.size() &&
           source.bga_event.size() == retained.bga_events.size() &&
           source.grid.row.size() == retained.grid_lines.size();
}
auto project_reused_scalar_scene(const compose_final_projection_context_t& input, double field_left,
                                 double field_width, double pixels_per_row, bool downscroll,
                                 bool bindings_truncated) -> bool {
    auto& cache = input.cache;
    auto& flattened = input.flattened;
    auto& view = flattened.view.compose;
    cache.static_projection_scanned = cache.static_projection_extracted = 0;
    if (!reusable_scalar_source(cache, view))
        return false;
    const auto metadata = wormedit::project_editor_field_scene_window_metadata(
        input.chart, *cache.index, input.interaction_view, cache.preview_scene_request,
        cache.scene_window);
    if (!metadata.projection)
        return false;
    refresh_scalar_projection_indices(cache);
    const auto shift = downscroll ? view.viewport_height_pixels -
                                        cache.scene_window.view.viewport_height_pixels +
                                        input.interaction_view.viewport_scroll_pixels -
                                        cache.scene_window.view.viewport_scroll_pixels
                                  : cache.scene_window.view.viewport_scroll_pixels -
                                        input.interaction_view.viewport_scroll_pixels;
    const compose_scene_projection_context_t projection{flattened,
                                                        input.chart,
                                                        *cache.index,
                                                        *metadata.projection.scene,
                                                        input.history,
                                                        input.application_state,
                                                        input.interaction_view,
                                                        view,
                                                        field_left,
                                                        field_width,
                                                        pixels_per_row,
                                                        downscroll,
                                                        bindings_truncated,
                                                        input.bga_move_preview,
                                                        input.retained_state
                                                            ? wormedit::edit_snap_denominator(
                                                                input.chart, *input.retained_state)
                                                            : input.chart.divisions_per_standard_measure};
    // Metadata contains no grid/objects; their immutable scalar records are extracted below.
    project_compose_layout_and_notes(projection);
    project_compose_bgm(projection);
    project_compose_bga(projection);
    if (!extract_scalar_objects(cache, metadata, flattened, input.interaction_view, shift,
                                view.viewport_height_pixels) ||
        !scalar_projection_fits_budget(projection, cache.preview_scene_request) || view.truncated) {
        discard_scalar_projection(flattened);
        return false;
    }
    project_compose_marker_previews(projection);
    publish_compose_scene(projection);
    publish_compose_static_source(cache, view);
    view.scene_available = true;
    ++cache.scene_window.reuse_count;
    ++cache.static_projection_reused_frames;
    return true;
}

bool compose_project_scene(const compose_final_projection_context_t& input,
                           std::uint64_t canvas_rows) {
    auto& flattened = input.flattened;
    auto& history = input.history;
    const auto& chart = input.chart;
    auto& cache = input.cache;
    auto& retained_state = input.retained_state;
    auto& application_state = input.application_state;
    auto& view = input.view;
    view.note_kind_palette_visible = application_state.editor_preferences.note_kind_palette_visible;
    view.lane_key_legend_visible = application_state.editor_preferences.lane_key_legend_visible;
    view.area_band_alpha = application_state.editor_preferences.area_band_alpha;
    view.armed_note_kind = static_cast<std::uint32_t>(retained_state->armed_note_kind);
    view.allowed_note_kind_mask = 0;
    for (unsigned kind = 0; kind <= static_cast<unsigned>(wormedit::note_kind_t::fake); ++kind)
        if (wormedit::note_kind_allowed(chart, static_cast<wormedit::note_kind_t>(kind)))
            view.allowed_note_kind_mask |= 1U << kind;
    auto& interaction_view = input.interaction_view;
    const auto& timing_stage = input.timing_stage;
    const auto downscroll = input.downscroll;
    flattened.compose.interaction_view = interaction_view;
    view.bms_measure_labels = chart.source_family == wormedit::format_family_t::bms;
    populate_lane_profile_choices(view, chart, interaction_view.cursor);
    populate_lane_layout_editor(view, chart, input.measures, interaction_view.cursor,
                                retained_state && retained_state->virtual_bms_cursor_row.has_value());
    populate_lane_layout_segments(flattened, chart, input.measures, interaction_view, downscroll, view);
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

    const auto horizontal_layout =
        compose_horizontal_layout(view.viewport_width_pixels, maximum_layout_lane_count(chart),
                                  !chart.bgm_source_lanes.empty(),
                                  interaction_view.bga_lane_visible,
                                  std::popcount(
                                      effective_compose_timing_lane_mask(application_state)));
    const auto bgm_pane_width = horizontal_layout.bgm_pane_width_pixels;
    const auto bga_pane_width = horizontal_layout.bga_pane_width_pixels;
    const auto playable_field_width = horizontal_layout.playable_field_width_pixels;
    const auto field_left_pixels = horizontal_layout.field_left_pixels;
    view.playable_field_left_pixels = field_left_pixels;
    view.playable_field_right_pixels = field_left_pixels + playable_field_width;

    const auto pixels_per_row = interaction_view.pixels_per_standard_measure /
                                static_cast<double>(chart.divisions_per_standard_measure);
    populate_compose_timing_markers(flattened, chart, cache, interaction_view, view,
                                    downscroll, pixels_per_row, canvas_rows, timing_stage);
    populate_compose_dead_spans(flattened, cache, interaction_view, view, downscroll,
                                pixels_per_row);
    populate_compose_kiai_and_sv(flattened, cache, interaction_view, view, downscroll,
                                 pixels_per_row, canvas_rows, application_state.kiai_selected_head);
    populate_compose_speed_spans(flattened, cache, interaction_view, view, downscroll, pixels_per_row);
    drop_stale_recipe_preview(application_state, chart, history.document.revision);
    populate_compose_recipe_areas(flattened, chart, cache, interaction_view, view, downscroll,
                                  pixels_per_row, canvas_rows, application_state.recipe_selected,
                                  application_state.recipe_preview ? &*application_state.recipe_preview
                                                                   : nullptr);
    populate_compose_recipe_form(flattened, chart, application_state, view);
    const auto maximum_bgm_scroll = std::max(
        0.0, static_cast<double>(chart.bgm_source_lanes.size()) * compose_bgm_lane_width_pixels -
                 bgm_pane_width);
    auto& bgm_horizontal_scroll = application_state.bgm_horizontal_scroll_pixels[chart.id.value];
    bgm_horizontal_scroll = std::clamp(bgm_horizontal_scroll, 0.0, maximum_bgm_scroll);
    view.bgm_horizontal_scroll_pixels = bgm_horizontal_scroll;
    view.bgm_maximum_horizontal_scroll_pixels = maximum_bgm_scroll;
    cache.preview_scene_request = {
        .field_left_pixels = field_left_pixels,
        .field_width_pixels = playable_field_width,
        .split_stage_gutter_pixels = 14.0,
        .note_head_height_pixels = 16.0,
        .scroll_direction = downscroll
            ? wormedit::editor_field_scroll_direction_t::down
            : wormedit::editor_field_scroll_direction_t::up,
        .fixed_cursor_screen_y_pixels = retained_state->fixed_cursor_screen_y_pixels,
        .bgm_pane_left_pixels = horizontal_layout.bgm_pane_left_pixels,
        .bgm_pane_width_pixels = bgm_pane_width,
        .bgm_lane_width_pixels = compose_bgm_lane_width_pixels,
        .bgm_horizontal_scroll_pixels = bgm_horizontal_scroll,
        .bga_pane_left_pixels = horizontal_layout.bga_pane_left_pixels,
        .bga_pane_width_pixels = bga_pane_width,
        .include_virtual_bms_tail = wormedit::chart_has_virtual_tail(chart),
        .solo_semantic_layer = application_state.semantic_layer_solo_active
                                   ? application_state.semantic_layer_target
                                   : std::nullopt,
        .required_semantic_layers = application_state.required_semantic_layer_filters,
        .excluded_semantic_layers = application_state.excluded_semantic_layer_filters,
        .maximum_grid_lines = WORM_UI_COMPOSE_GRID_CAPACITY,
        .maximum_scene_primitives = WORM_UI_COMPOSE_SCENE_PRIMITIVE_CAPACITY,
        // One physical 512-row tile at scale >= .5, plus conservative glyph halo.
        .retained_window_minimum_margin_pixels =
            1024.0 + std::max(96.0, playable_field_width * .5 + 20.0)};
    cache.preview_scene_request.comparison_difference_rows.assign(
        input.comparison_rows.begin(), input.comparison_rows.end());
    refresh_compose_static_scene_style(cache,view);
    const auto frame_scene_previous = frame_timing().enter(frame_phase_t::scene);
    if (project_reused_scalar_scene(input, field_left_pixels, playable_field_width, pixels_per_row,
                                    downscroll, lane_key_bindings_truncated)) {
        frame_timing().restore(frame_scene_previous);
        return true;
    }
    const auto prior_window_work=cache.scene_window.rebuild_count+cache.scene_window.reuse_count;
    const auto composed = wormedit::make_editor_field_scene_reusing_window(chart, *cache.index, interaction_view,
                                                            cache.preview_scene_request, cache.scene_window);
    frame_timing().restore(frame_scene_previous);
    if (!composed.scene || composed.has_errors()) {
        report_compose_diagnostic(flattened, projection_feedback_kind_t::field_scene,
                                 composed.diagnostics, "The chart field could not be composed.");
        return false;
    }
    const auto& scene = *composed.scene;
    view.scene_available = true;
    const compose_scene_projection_context_t projection{
        flattened, chart, *cache.index, scene, history, application_state, interaction_view, view,
        field_left_pixels, playable_field_width, pixels_per_row, downscroll,
        lane_key_bindings_truncated, input.bga_move_preview,
        retained_state ? wormedit::edit_snap_denominator(chart, *retained_state)
                       : chart.divisions_per_standard_measure};
    populate_compose_sound_labels(projection);
    project_compose_layout_and_notes(projection);
    project_compose_bgm(projection);
    project_compose_bga(projection);
    project_compose_marker_previews(projection);
    publish_compose_scene(projection);
    if (cache.scene_window.rebuild_count+cache.scene_window.reuse_count != prior_window_work)
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
    const auto count = std::popcount(view.timing_lane_visibility_mask);
    if (count == 0) return;
    const auto timing_right = view.bga_lane_visible ? view.bga_lane_bounds.left
                                                     : view.playable_field_left_pixels;
    const auto width = (timing_right - 42.0) / count;
    for (std::size_t i = 0; i < view.timing_marker_count; ++i) {
        const auto& marker = view.timing_markers[i];
        const auto kind = area_timing_kind(marker.lane);
        if (!kind || marker.object_id == 0) continue;
        const auto bit = static_cast<std::uint8_t>(1U << static_cast<unsigned>(marker.lane));
        if ((view.timing_lane_visibility_mask & bit) == 0) continue;
        const auto slot = std::popcount(static_cast<std::uint8_t>(
            view.timing_lane_visibility_mask & (bit - 1)));
        const auto inner_left = 42.0 + width * slot + 2.0;
        const auto inner_width = std::max(0.0, width - 4.0);
        const auto left = inner_left + inner_width * marker.marker_left_fraction;
        const auto right = std::max(left + 1.0,
            inner_left + inner_width * marker.marker_right_fraction);
        const auto top = std::clamp(marker.marker_y_pixels - 8.0, 0.0,
                                    std::max(0.0, view.viewport_height_pixels - 16.0));
        append_area_object_geometry(snapshot, {*kind, wormedit::object_id_t{marker.object_id}},
                                     {left, top, right, top + 16.0}, view, downscroll);
    }
}

void cache_area_object_geometry(const compose_final_projection_context_t& input) {
    auto& cache = input.cache;
    const auto& view = input.view;
    if (!view.scene_available) {
        cache.area_object_geometry.reset();
        return;
    }
    auto snapshot = std::make_shared<wormedit::editor_area_geometry_snapshot_t>();
    snapshot->chart = input.chart.id;
    snapshot->document_revision = input.history.document.revision;
    snapshot->projection = wormedit::editor_area_geometry_projection_t{
        input.interaction_view.viewport_scroll_pixels, input.interaction_view.pixels_per_standard_measure,
        input.interaction_view.viewport_height_pixels};
    for (std::size_t i = 0; i < view.bgm_event_count; ++i) {
        const auto& event = view.bgm_events[i];
        append_area_object_geometry(*snapshot,
            {wormedit::editor_selection_object_kind_t::bgm_event,
             wormedit::object_id_t{event.event_id}}, event.bounds, view, input.downscroll);
    }
    append_area_timing_geometry(*snapshot, view, input.downscroll);
    cache.area_object_geometry = std::move(snapshot);
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

void append_paste_timing_ghosts(const compose_final_projection_context_t& input,
                                application_command_state_t::paste_session_t& session) {
    const auto pixels_per_row = input.interaction_view.pixels_per_standard_measure /
                                static_cast<double>(input.chart.divisions_per_standard_measure);
    if (!(pixels_per_row > 0.0)) return;
    const auto height = input.view.viewport_height_pixels;
    const auto project = [&](wormedit::local_position_t position) -> std::optional<double> {
        const auto row = wormedit::absolute_row(input.measures, position);
        if (!row) return std::nullopt;
        return compose_project_y(*row, pixels_per_row, input.interaction_view.viewport_scroll_pixels,
                                 height, input.downscroll);
    };
    for (const auto& object : session.proposal.objects) {
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

void populate_compose_paste_projection(const compose_final_projection_context_t& input) {
    auto& application = input.application_state;
    if (!input.retained_state || !application.paste_session) return;
    const auto anchor = paste_anchor(*application.paste_session, input.retained_state->cursor);
    if (!update_compose_paste(application, input.history.document, input.chart.id, anchor)) return;
    auto& session = *application.paste_session;
    auto& view = input.view;
    view.paste_active = true;
    view.paste_valid = session.anchor_available && session.proposal.valid;
    view.paste_diagnostic = session.anchor_available ? session.proposal.error.c_str()
                                                     : session.anchor_error.c_str();
    session.ghosts.clear();
    if (!session.index) return;
    const auto composed = wormedit::make_editor_field_scene(session.geometry, *session.index,
        input.interaction_view, input.cache.preview_scene_request);
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

void populate_compose_paint_ghost(const compose_final_projection_context_t& input) {
    using namespace wormedit;
    auto& view = input.view;
    auto& cache = input.cache;
    view.paint_ghost_visible = false;
    if (!view.note_paint_mode_active || !cache.paint_pointer || !input.retained_state ||
        editor_interaction_has_pending_gesture(*input.retained_state)) return;
    if (!cache.hit_test_index) {
        auto built = build_editor_hit_test_index(input.chart, input.history.document.revision);
        if (!std::holds_alternative<editor_hit_test_index_t>(built)) return;
        cache.hit_test_index = std::get<editor_hit_test_index_t>(std::move(built));
    }
    const auto [x, y] = *cache.paint_pointer;
    const auto target = resolve_compose_paint_cell(input.chart, *cache.hit_test_index,
        input.interaction_view, x, y, view.playable_field_left_pixels,
        view.playable_field_right_pixels - view.playable_field_left_pixels, input.downscroll,
        edit_snap_denominator(input.chart, *input.retained_state),
        cache.dead_spans ? &*cache.dead_spans : nullptr);
    if (!target) return;
    const auto row = absolute_row(input.measures, target->position);
    if (!row) return;
    const auto source_y = static_cast<double>(*row) *
        input.interaction_view.pixels_per_standard_measure / input.chart.divisions_per_standard_measure -
        input.interaction_view.viewport_scroll_pixels;
    const auto ghost_y = input.downscroll ? view.viewport_height_pixels - source_y : source_y;
    const auto lanes = std::span(view.lanes, view.lane_count);
    const auto lane = std::ranges::find_if(lanes, [&](const auto& value) {
        return value.lane_id == target->lane.value && ghost_y >= value.bounds.top &&
               ghost_y <= value.bounds.bottom;
    });
    if (lane == lanes.end()) return;
    bool occupied = false;
    for (const auto& measure : input.chart.measures)
        occupied = occupied || std::ranges::any_of(measure.notes, [&](const auto& note) {
            return note.lane == target->lane && note.position == target->position;
        });
    const auto rectangular = lane->glyph <= 1;
    const auto height = cache.preview_scene_request.note_head_height_pixels;
    const auto top = rectangular ? (input.downscroll ? ghost_y - height : ghost_y) : ghost_y - height / 2;
    view.paint_ghost = {.bounds = {lane->bounds.left, top, lane->bounds.right, top + height},
        .lane_id = lane->lane_id, .display_lane_number = lane->display_order + 1,
        .key_color = lane->key_color, .glyph = lane->glyph,
        .snap_class = wormedit::ui::note_snap_class(*row, input.chart.divisions_per_standard_measure),
        .kind = static_cast<std::uint8_t>(view.armed_note_kind),
        .paint_erase_preview = occupied,
        .paint_invalid_preview = !note_kind_allowed(input.chart, input.retained_state->armed_note_kind)};
    view.paint_ghost_visible = true;
}

void populate_compose_final_projection(const compose_final_projection_context_t& input) {
    input.view.clipboard_copy_available = input.retained_state &&
        !editor_interaction_has_pending_gesture(*input.retained_state) &&
        (!input.retained_state->selected_objects.empty() ||
         !input.retained_state->selected_notes.empty() ||
         input.application_state.kiai_selected_head != 0);
    input.view.clipboard_paste_available = input.application_state.chart_clipboard.has_value() &&
        input.retained_state && !editor_interaction_has_pending_gesture(*input.retained_state);
    const auto rows = prepare_compose_projection_state(input);
    populate_compose_pending_previews(input);
    (void)compose_project_scene(input, rows);
    populate_compose_paste_projection(input);
    cache_area_object_geometry(input);
    populate_compose_paint_ghost(input);
}

void populate_bgm_only_compose_projection(
    flattened_workspace_t& flattened, wormedit::document_history_t& history,
    const wormedit::chart_document_t& chart, const wormedit::measure_index_t& measures,
    compose_scene_cache_t& cache, application_command_state_t& application_state,
    WormUiComposeView& view, bool downscroll,
    const wormedit::timing_lane_pseudonote_preview_t* timing_stage) {
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
    const auto cached_view_matches = cache.interaction_view &&
        cache.interaction_view->chart == chart.id &&
        cache.interaction_view->document_revision == history.document.revision;
    const auto viewport_scroll_pixels = cached_view_matches
        ? cache.interaction_view->viewport_scroll_pixels
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
    const compose_final_projection_context_t final_context{
        flattened, history, chart, cache, projection_state, application_state, view,
        interaction_view, measures, downscroll, timing_stage};
    const auto rows = prepare_compose_projection_state(final_context);
    populate_compose_pending_previews(final_context);
    (void)compose_project_scene(final_context, rows);
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

void project_loop_selection_controls(WormUiComposeView& view,
                                     const application_command_state_t& application) {
    view.transport_loop_selection_enabled = application.transport_loop_selection_active &&
                                            application.transport_loop_selection_active();
    view.transport_loop_selection_available =
        static_cast<bool>(application.transport_set_loop_selection) &&
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

void project_branch_statement_feedback(WormUiComposeView& view,
                                      application_command_state_t& application_state,
                                      wormedit::chart_id_t chart,
                                      std::uint64_t revision) {
    auto& feedback = application_state.bms_branch_statement_feedback;
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

} // namespace

void populate_compose_view(flattened_workspace_t& flattened,
                           wormedit::document_history_t& history,
                           const wormedit::workspace_controller_t& workspace,
                           compose_scene_cache_t& cache, int window_width, int window_height,
                           std::optional<wormedit::editor_interaction_state_t>& retained_state,
                           application_command_state_t& application_state,
                           const wormedit::timing_lane_pseudonote_preview_t* timing_stage,
                           std::span<const wormedit::local_position_t> comparison_rows,
                           const compose_bga_move_preview_input_t* bga_move_preview) {
    if (!workspace.selected_chart || !application_state.bms_branch_statement_feedback ||
        application_state.bms_branch_statement_feedback->chart != *workspace.selected_chart ||
        application_state.bms_branch_statement_feedback->revision != history.document.revision)
        application_state.bms_branch_statement_feedback.reset();
    cache.area_object_geometry.reset();
    if (!initialize_compose_view_state(flattened, cache, window_width, window_height,
                                       retained_state, application_state)) {
        return;
    }
    auto& view = flattened.view.compose;
    if (!workspace.selected_chart) {
        report_compose_projection(flattened, projection_feedback_kind_t::choose_chart,
            wormedit::notification_severity_t::hint, "Choose a difficulty to display its chart field.", 0,
            wormedit::notification_source_t::input);
        return;
    }
    const auto chart_it = std::ranges::find(history.document.charts, *workspace.selected_chart,
                                            &wormedit::chart_document_t::id);
    if (chart_it == history.document.charts.end()) {
        report_compose_projection(flattened, projection_feedback_kind_t::missing_chart,
            wormedit::notification_severity_t::warning, "The selected difficulty is no longer available.");
        return;
    }
    const auto& chart = *chart_it;
    populate_branch_statement_metadata(flattened, chart);
    project_branch_statement_feedback(view, application_state, chart.id, history.document.revision);
    view.bgm_column_count = static_cast<std::uint32_t>(chart.bgm_source_lanes.size());
    view.bgm_column_count_editable =
        chart.source_family == wormedit::format_family_t::bms ||
        chart.source_family == wormedit::format_family_t::osu_mania;
    const auto downscroll = compose_field_downscroll(chart, application_state.editor_preferences);
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
        project_loop_selection_controls(view, application_state);
        return;
    }
    const compose_interaction_population_context_t interaction_context{
        flattened, history, chart, measures, retained_state, application_state, view, downscroll};
    auto interaction_data = prepare_compose_interaction(interaction_context);
    if (!interaction_data) return;
    auto interaction_view = std::move(*interaction_data);
    const compose_final_projection_context_t final_context{
        flattened, history, chart, cache, retained_state, application_state, view,
        interaction_view, measures, downscroll, timing_stage, comparison_rows,
        bga_move_preview};
    populate_compose_final_projection(final_context);
    project_loop_selection_controls(view, application_state);
}

auto toggle_compose_audio_offset_lock(const WormUiWorkspaceAction& action,
    const wormedit::document_history_t& history, const wormedit::workspace_controller_t& workspace,
    application_command_state_t& application, compose_scene_cache_t& cache,
    std::optional<wormedit::audio_offset_drag_t>& drag,
    std::optional<wormedit::workspace_tab_t>& drag_tab) -> std::string {
    if (workspace.active_tab != wormedit::workspace_tab_t::compose || !workspace.selected_chart ||
        workspace.selected_chart->value != action.compose_input_chart_id ||
        history.document.revision != action.compose_input_document_revision)
        return "Audio offset lock ignored because the chart or document revision changed.";
    application.audio_offset_locked = !application.audio_offset_locked;
    if (application.audio_offset_locked) {
        drag.reset();
        drag_tab.reset();
        cache.offset_fit_drag.reset();
        cache.proposed_offset_timing_index.reset();
        cache.proposed_offset_seconds.reset();
    }
    return application.audio_offset_locked ? "Audio offset locked." : "Audio offset unlocked.";
}

} // namespace wormedit::sys
