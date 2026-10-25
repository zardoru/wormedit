#include <wormedit/core/timing_lane_catalog.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/song_preview_timing.h>
#include <wormedit/core/span_manipulation.h>
#include "core/timing_arithmetic.h"
#include "sys/compose/input/compose_input_internal.h"
namespace wormedit::sys::compose_input_detail {
auto select_compose_timing_marker(const wormedit::ui::compose_input_action_t& input,
                                  compose_input_context_t& context,
                                  wormedit::editor_interaction_result_t& result,
                                  std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto identity = wormedit::find_catalog_entry(wormedit::timing_lanes, input.timing_lane);
    const auto mode = context.selection_mode(input.selection_mode);
    if (!identity || !mode || input.object_id.value == 0)
        return "Timing marker selection is stale or invalid.";
    if (!context.is_timing_input_current(input))
        return stale_action_message("Timing marker selection");
    result = dispatch_editor_action(
        context.environment.document.history, context.state,
        select_editor_objects_action_t{
            .objects = {{identity->selection_kind, input.object_id}}, .mode = *mode});
    accepted = "Timing marker selected.";
    return std::nullopt;
}

auto timing_pointer_row(compose_input_context_t& context, const wormedit::chart_document_t& chart,
                        const wormedit::editor_interaction_view_model_t& view,
                        const wormedit::measure_index_t& index, double y_pixels)
    -> std::optional<std::uint64_t> {
    if (!std::isfinite(y_pixels))
        return std::nullopt;
    const auto y = std::clamp(y_pixels, 0.0, std::max(0.0, view.viewport_height_pixels - 0.001));
    const auto position = context.position_at_compose_y_unrestricted(chart, view, y);
    const auto row = position ? (index).row_of(*position) : std::nullopt;
    if (!row)
        return std::nullopt;
    const auto snapped = wormedit::snap_absolute_row(
        chart, *row, wormedit::edit_snap_grid(chart, context.state));
    return snapped;
}

auto move_compose_timing_selection(const wormedit::ui::compose_input_action_t& input,
                                   compose_input_context_t& context,
                                   wormedit::editor_interaction_result_t& result,
                                   std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto identity = wormedit::find_catalog_entry(wormedit::timing_lanes, input.timing_lane);
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!identity || !chart || !view || !std::isfinite(input.y_pixels))
        return "Timing marker move is stale or invalid.";
    if (!context.is_timing_input_current(input))
        return stale_action_message("Timing marker move");
    const auto clicked =
        editor_selection_object_t{identity->selection_kind, input.object_id};
    if (std::ranges::find(context.state.selected_objects, clicked) ==
        context.state.selected_objects.end())
        return "Select the timing marker before dragging it.";
    const auto& measures = context.environment.document.history.indices(chart->id)->measures;
    const auto* index = measures ? &*measures : nullptr;
    const auto target_row =
        index ? timing_pointer_row(context, *chart, *view, *index, input.y_pixels) : std::nullopt;
    const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (!target_row || input.timing_anchor_chart_row > maximum || *target_row > maximum)
        return "Timing marker move has no valid destination row.";
    const auto delta = static_cast<std::int64_t>(*target_row) -
                       static_cast<std::int64_t>(input.timing_anchor_chart_row);
    std::vector<timing_lane_event_ref_t> selected;
    selected.reserve(context.state.selected_objects.size());
    for (const auto& object : context.state.selected_objects) {
        const auto lane = timing_lane_for_selection(object.kind);
        if (lane)
            selected.push_back({*lane, object.id});
    }
    if (selected.empty())
        return "No timing markers are selected.";
    if (delta == 0) {
        result.accepted = true;
        accepted = "Timing markers stayed on their rows.";
        return std::nullopt;
    }
    const auto moved = move_timing_lane_event_group(
        context.environment.document.history, chart->id, selected, delta, context.environment.commands.project.timing_recipe.timing_locked);
    result.accepted = moved.applied;
    result.document_changed = moved.applied;
    result.diagnostics = moved.diagnostics;
    accepted = "Selected timing markers moved vertically.";
    return std::nullopt;
}

auto kiai_manipulated_rows(const wormedit::kiai_span_t* span, std::uint8_t edge,
                           std::uint64_t anchor_row, std::uint64_t pointer_row,
                           std::uint64_t total_rows)
    -> std::optional<std::pair<std::uint64_t, std::uint64_t>> {
    using namespace wormedit;
    const auto handle = edge == WORM_UI_SPAN_HANDLE_BEGIN ? span_handle_t::begin
                        : edge == WORM_UI_SPAN_HANDLE_END ? span_handle_t::end
                                                          : span_handle_t::body;
    const auto original =
        span ? span_rows_t{span->begin_row, span->end_row} : span_rows_t{anchor_row, anchor_row};
    const auto moved = manipulate_span(original, span ? handle : span_handle_t::end, anchor_row,
                                       pointer_row, total_rows);
    if (!moved || (span && *moved == original))
        return std::nullopt;
    return std::pair{moved->begin, moved->end};
}

auto edit_compose_kiai_span(const wormedit::ui::compose_input_action_t& input, compose_input_context_t& context,
                            wormedit::editor_interaction_result_t& result,
                            std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!chart || !view) return "Kiai edit is stale or invalid.";
    if (!context.is_timing_input_current(input))
        return stale_action_message("Kiai edit");
    if (context.environment.commands.project.timing_recipe.timing_locked)
        return "Timing is locked.";
    const auto& measures = context.environment.document.history.indices(chart->id)->measures;
    const auto* index = measures ? &*measures : nullptr;
    const auto row =
        index ? timing_pointer_row(context, *chart, *view, *index, input.y_pixels) : std::nullopt;
    if (!row)
        return "Kiai edit has no valid destination row.";
    kiai_span_edit_t edit;
    const auto anchor =
        timing_pointer_row(context, *chart, *view, *index, input.span_anchor_y_pixels);
    // Edges 0 and 1 drag the begin and end edge, 3 the body; 2 creates a new area.
    const kiai_span_t* span = nullptr;
    std::vector<kiai_span_t> spans;
    if (input.span_edge != WORM_UI_SPAN_HANDLE_CREATE) {
        spans = kiai_spans_of(*chart, *index);
        const auto found =
            std::ranges::find(spans, input.object_id, &kiai_span_t::head);
        if (found == spans.end())
            return "The Kiai area no longer exists.";
        span = &*found;
        edit.head = found->head;
    }
    edit.rows = anchor
                    ? kiai_manipulated_rows(span, input.span_edge, *anchor, *row, index->total_rows)
                    : std::nullopt;
    if (!edit.rows) {
        result.accepted = true;
        accepted = "Kiai area unchanged.";
        return std::nullopt;
    }
    const auto edited =
        apply_marker_edit(context.environment.document.history,
                          edit_kiai_span_action_t{.chart = chart->id, .edit = edit});
    result.accepted = edited.applied;
    result.document_changed = edited.applied;
    result.diagnostics = edited.diagnostics;
    accepted = edited.applied ? "Kiai area edited." : "Kiai area unchanged.";
    return std::nullopt;
}

namespace {
auto preview_span_rows(const wormedit::timing_index_t& timing, const wormedit::song_preview_t& preview)
    -> std::optional<wormedit::span_rows_t> {
    const auto rows = wormedit::song_preview_rows(timing, preview);
    if (!rows) return std::nullopt;
    return wormedit::span_rows_t{rows->begin, rows->end};
}

auto preview_from_rows(const wormedit::timing_index_t& timing, wormedit::span_rows_t rows, bool has_length)
    -> std::optional<wormedit::song_preview_t> {
    const auto begin = wormedit::song_preview_milliseconds_at(timing, rows.begin);
    const auto end = wormedit::song_preview_milliseconds_at(timing, rows.end);
    if (!begin || (has_length && (!end || *end <= *begin))) return std::nullopt;
    return wormedit::song_preview_t{.start_milliseconds = *begin,
                                    .length_milliseconds = has_length ? std::optional{*end - *begin} : std::nullopt};
}

auto preview_span_handle(std::uint8_t edge) -> wormedit::span_handle_t {
    if (edge == WORM_UI_SPAN_HANDLE_END) return wormedit::span_handle_t::end;
    if (edge == WORM_UI_SPAN_HANDLE_BEGIN) return wormedit::span_handle_t::begin;
    return wormedit::span_handle_t::body;
}

// A point-only preview has no end to keep: its start line and label shift the point by the
// drag, and its end line gives it a length.
auto moved_preview_rows(const wormedit::song_preview_t& preview, wormedit::span_rows_t original,
                        wormedit::span_handle_t handle, std::uint64_t anchor, std::uint64_t pointer,
                        std::uint64_t total_rows) -> std::optional<wormedit::span_rows_t> {
    if (preview.length_milliseconds || handle == wormedit::span_handle_t::end)
        return wormedit::manipulate_span(original, handle, anchor, pointer, total_rows);
    if (total_rows == 0) return std::nullopt;
    const auto shifted = static_cast<long double>(original.begin) + static_cast<long double>(pointer) -
                         static_cast<long double>(anchor);
    const auto begin = static_cast<std::uint64_t>(
        std::clamp(shifted, 0.0L, static_cast<long double>(total_rows - 1)));
    return wormedit::span_rows_t{begin, begin};
}
} // namespace

// A song-preview edge or the whole span dragged (open question 26). Rows snap like other
// timing pointers, then become source-song milliseconds; one Undo step per release. The
// preview is metadata, so the timing lock does not apply.
auto edit_compose_preview_span(const wormedit::ui::compose_input_action_t& input, compose_input_context_t& context,
                               wormedit::editor_interaction_result_t& result,
                               std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    auto& history = context.environment.document.history;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(history, context.state);
    if (!chart || !view) return "The preview edit is stale.";
    if (!context.is_timing_input_current(input)) return stale_action_message("Preview edit");
    const auto& preview = history.document.effective_preview(*chart);
    const auto* indices = history.indices(chart->id);
    if (!preview || !indices || !indices->measures || !indices->timing.value)
        return "The song preview is no longer available.";
    const auto& timing = *indices->timing.value;
    const auto row = timing_pointer_row(context, *chart, *view, *indices->measures, input.y_pixels);
    const auto anchor = timing_pointer_row(context, *chart, *view, *indices->measures, input.span_anchor_y_pixels);
    const auto original = preview_span_rows(timing, *preview);
    if (!row || !anchor || !original) return "The preview edit has no valid destination row.";
    const auto handle = preview_span_handle(input.span_edge);
    const auto moved = moved_preview_rows(*preview, *original, handle, *anchor, *row, timing.total_rows);
    const auto edited = moved && *moved != *original
        ? preview_from_rows(timing, *moved, preview->length_milliseconds || handle == span_handle_t::end)
        : std::nullopt;
    if (!edited) {
        result.accepted = true;
        accepted = "Preview unchanged.";
        return std::nullopt;
    }
    const auto owner = chart->metadata.preview_override ? std::optional{chart->id} : std::nullopt;
    const auto applied = apply_song_preview_edit(history, {.chart = owner, .preview = *edited});
    result.accepted = applied.applied || applied.diagnostics.empty();
    result.document_changed = applied.applied;
    result.diagnostics = applied.diagnostics;
    accepted = applied.applied ? "Preview edited." : "Preview unchanged.";
    return std::nullopt;
}

auto speed_transition_between(const wormedit::chart_document_t& chart,
                              const wormedit::timing_index_t* timing,
                              const wormedit::speed_event_t& speed, std::uint64_t begin_row,
                              std::uint64_t end_row) -> std::optional<wormedit::rational_t> {
    using namespace wormedit;
    if (end_row <= begin_row)
        return rational_t{0, 1};
    if (!speed.transition_is_seconds) {
        const auto numerator = (end_row - begin_row) * 4;
        return rows_to_fraction(numerator, chart.divisions_per_standard_measure);
    }
    if (!timing)
        return std::nullopt;
    const auto begin = timing->query_seconds(begin_row);
    const auto end = timing->query_seconds(end_row);
    if (!begin || !end)
        return std::nullopt;
    return timing_arithmetic::subtract_exact<rational_t>(end->value, begin->value);
}

auto edit_compose_speed_span(const wormedit::ui::compose_input_action_t& input,
                             compose_input_context_t& context,
                             wormedit::editor_interaction_result_t& result,
                             std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!chart || !view) return "Speed edit is stale or invalid.";
    if (!context.is_timing_input_current(input))
        return stale_action_message("Speed edit");
    const auto& measures = context.environment.document.history.indices(chart->id)->measures;
    const auto* index = measures ? &*measures : nullptr;
    const auto speed =
        std::ranges::find(chart->speeds, input.object_id, &speed_event_t::id);
    if (!index || speed == chart->speeds.end())
        return "The Speed event no longer exists.";
    const auto row = timing_pointer_row(context, *chart, *view, *index, input.y_pixels);
    const auto begin_row = (*index).row_of(speed->position);
    if (!row || !begin_row)
        return "Speed edit has no valid destination row.";
    // The span as drawn: from the event to the end of its transition.
    const auto& projected = context.environment.controls.cache.timing.speed_spans;
    const auto span = projected
                          ? std::ranges::find(projected->spans, speed->id, &speed_span_t::object)
                          : std::vector<speed_span_t>::const_iterator{};
    if (!projected || span == projected->spans.end())
        return "The Speed span is unavailable.";
    const auto anchor =
        timing_pointer_row(context, *chart, *view, *index, input.span_anchor_y_pixels);
    if (!anchor)
        return "Speed edit has no valid destination row.";
    const auto original = span_rows_t{
        *begin_row, std::max<std::uint64_t>(
                        *begin_row, static_cast<std::uint64_t>(std::llround(span->end_row)))};
    const auto handle = input.span_edge == WORM_UI_SPAN_HANDLE_BEGIN ? span_handle_t::begin
                        : input.span_edge == WORM_UI_SPAN_HANDLE_END ? span_handle_t::end
                                                                     : span_handle_t::body;
    const auto moved =
        manipulate_span(original, handle, *anchor, *row, index->total_rows, {.allow_empty = true});
    if (!moved || *moved == original) {
        result.accepted = true;
        accepted = "Speed span unchanged.";
        return std::nullopt;
    }
    speed_span_edit_t edit;
    if (moved->begin != original.begin) {
        edit.position = (*index).position_at( moved->begin);
        if (!edit.position)
            return "Speed edit has no valid destination row.";
    }
    const auto timing = context.environment.controls.cache.timing.marker_timing_index
                            ? &*context.environment.controls.cache.timing.marker_timing_index
                            : nullptr;
    edit.transition = speed_transition_between(*chart, timing, *speed, moved->begin, moved->end);
    if (!edit.transition)
        return "The Speed transition length cannot be measured there.";
    const auto edited =
        edit_speed_event_span(context.environment.document.history, chart->id, speed->id, edit,
                              context.environment.commands.project.timing_recipe.timing_locked);
    result.accepted = edited.applied;
    result.document_changed = edited.applied;
    result.diagnostics = edited.diagnostics;
    accepted = edited.applied ? "Speed span edited." : "Speed span unchanged.";
    return std::nullopt;
}

auto find_input_recipe(const wormedit::chart_document_t& chart, std::uint64_t id)
    -> const wormedit::sv_recipe_t* {
    const auto found =
        std::ranges::find(chart.sv_recipes, wormedit::recipe_id_t{id}, &wormedit::sv_recipe_t::id);
    return found == chart.sv_recipes.end() ? nullptr : &*found;
}

auto edit_compose_recipe_range(const wormedit::ui::compose_input_action_t& input,
                               compose_input_context_t& context,
                               wormedit::editor_interaction_result_t& result,
                               std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto view = compose_view_for_state(context.environment.document.history, context.state);
    if (!chart || !view) return "SV recipe edit is stale or invalid.";
    if (!context.is_timing_input_current(input))
        return stale_action_message("SV recipe edit");
    if (!find_input_recipe(*chart, input.object_id.value))
        return "The SV recipe no longer exists.";
    const auto press = context.position_at_compose_y_unrestricted(
        *chart, *view,
        std::clamp(input.span_anchor_y_pixels, 0.0, view->viewport_height_pixels - 0.001));
    const auto release = context.position_at_compose_y_unrestricted(
        *chart, *view, std::clamp(input.y_pixels, 0.0, view->viewport_height_pixels - 0.001));
    if (!press || !release)
        return "SV recipe edit has no valid destination.";
    const auto handle =
        input.span_edge == WORM_UI_SPAN_HANDLE_BEGIN ? sv_recipe_range_handle_t::begin
        : input.span_edge == WORM_UI_SPAN_HANDLE_END ? sv_recipe_range_handle_t::end
                                                     : sv_recipe_range_handle_t::whole_span;
    auto drag = sv_recipe_range_drag_t::propose(
        context.environment.document.history, chart->id, recipe_id_t{input.object_id.value}, handle, *press,
        edit_snap_grid(*chart, context.state),
        context.environment.commands.project.timing_recipe.timing_locked);
    if (!drag.value) {
        result.diagnostics = drag.diagnostics;
        return drag.diagnostics.empty() ? "SV recipe edit was rejected."
                                        : drag.diagnostics.front().message;
    }
    auto revised = drag.value->revise(context.environment.document.history,
        {.is_timing_locked = context.environment.commands.project.timing_recipe.timing_locked}, *release);
    if (!revised.value) {
        result.diagnostics = std::move(revised.diagnostics);
        return result.diagnostics.empty() ? "SV recipe edit was rejected."
                                          : result.diagnostics.front().message;
    }
    const auto committed = apply_proposal(
        context.environment.document.history, *revised.value,
        proposal_application_input_t{
            .is_timing_locked = context.environment.commands.project.timing_recipe.timing_locked});
    result.accepted = committed.applied;
    result.document_changed = committed.applied;
    result.diagnostics = committed.diagnostics;
    accepted = committed.applied ? "SV recipe moved." : "SV recipe unchanged.";
    return std::nullopt;
}

auto run_compose_recipe_command(std::uint64_t id, std::uint8_t command,
                                compose_input_context_t& context,
                                wormedit::editor_interaction_result_t& result,
                                std::string_view& accepted) -> std::optional<std::string> {
    using namespace wormedit;
    const auto* chart = context.chart();
    const auto* recipe = chart ? find_input_recipe(*chart, id) : nullptr;
    if (!recipe)
        return "The SV recipe no longer exists.";
    if (context.environment.commands.project.timing_recipe.timing_locked)
        return "Timing is locked.";
    const auto recipe_id = recipe->id;
    const auto chart_id = chart->id;
    const auto enabled = recipe->enabled;
    if (command > static_cast<std::uint8_t>(ui::compose_recipe_command_t::later))
        return "Unknown SV recipe command.";
    sv_recipe_edit_action_t action;
    switch (static_cast<ui::compose_recipe_command_t>(command)) {
    case ui::compose_recipe_command_t::toggle_enabled:
        action = set_sv_recipe_enabled_action_t{chart_id, recipe_id, !enabled};
        accepted = enabled ? "SV recipe disabled." : "SV recipe enabled.";
        break;
    case ui::compose_recipe_command_t::materialize:
        action = materialize_sv_recipe_action_t{chart_id, recipe_id};
        accepted = "SV recipe materialized into events.";
        break;
    case ui::compose_recipe_command_t::remove:
        action = delete_edit_t<sv_recipe_t>{.chart = chart_id, .id = recipe_id};
        accepted = "SV recipe deleted.";
        break;
    case ui::compose_recipe_command_t::duplicate:
        action = duplicate_sv_recipe_action_t{.chart = chart_id, .id = recipe_id};
        accepted = "SV recipe duplicated.";
        break;
    case ui::compose_recipe_command_t::earlier:
    case ui::compose_recipe_command_t::later: {
        const auto index = static_cast<std::size_t>(recipe - chart->sv_recipes.data());
        const bool earlier = static_cast<ui::compose_recipe_command_t>(command) ==
                             ui::compose_recipe_command_t::earlier;
        if ((earlier && index == 0) || (!earlier && index + 1 == chart->sv_recipes.size()))
            return "SV recipe is already at the order boundary.";
        action = reorder_sv_recipe_action_t{.chart = chart_id, .id = recipe_id,
                                            .new_index = earlier ? index - 1 : index + 1};
        accepted = "SV recipe reordered.";
        break;
    }
    }
    const auto edited = apply_sv_recipe_edit(
        context.environment.document.history, action);
    result.accepted = edited.applied;
    result.document_changed = edited.applied;
    result.diagnostics = edited.diagnostics;
    if (!edited.applied)
        accepted = "SV recipe command rejected.";
    if (edited.applied && (command == static_cast<std::uint8_t>(ui::compose_recipe_command_t::remove) ||
                           command == static_cast<std::uint8_t>(ui::compose_recipe_command_t::materialize)))
        context.environment.commands.project.timing_recipe.recipe_selected = 0;
    return std::nullopt;
}
} // namespace wormedit::sys::compose_input_detail
