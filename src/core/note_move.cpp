#include "interaction_internal.h"
namespace wormedit::interaction_detail {
auto resolve_resize_parameters(const chart_document_t& chart, const measure_index_t& index,
                               std::vector<object_id_t>& targets, object_id_t reference_id,
                               local_position_t reference_end, std::uint64_t snap_denominator,
                               bool exact) -> std::variant<resize_parameters_t, diagnostic_list_t> {
    const auto* reference = chart.find_note(reference_id);
    if (!reference || (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "duration resize requires a tap or hold reference note",
                                       reference_id)};
    const auto snapped =
        exact ? (absolute_row(index, reference_end) ? std::optional{reference_end} : std::nullopt)
              : snapped_position(chart, index, reference_end, snap_denominator);
    const auto begin = absolute_row(index, reference->position);
    const auto old_end = reference->end ? absolute_row(index, *reference->end) : begin;
    const auto new_end = snapped ? absolute_row(index, *snapped) : std::nullopt;
    if (!snapped || !begin || !old_end || !new_end || *new_end < *begin)
        return diagnostic_list_t{error(diagnostic_code_t::invalid_hold,
                                       "reference duration cannot end before its note head",
                                       reference_id)};
    if (std::ranges::find(targets, reference_id) == targets.end())
        targets = {reference_id};
    const auto old_duration = *old_end - *begin;
    const auto new_duration = *new_end - *begin;
    return resize_parameters_t{reference, *snapped,
                               new_duration < old_duration ? old_duration - new_duration
                                                           : new_duration - old_duration,
                               new_duration < old_duration};
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto valid_resize_ids(const std::vector<object_id_t>& targets)
    -> std::variant<std::unordered_set<std::uint64_t>, diagnostic_list_t> {
    std::unordered_set<std::uint64_t> ids;
    ids.reserve(targets.size());
    for (const auto id : targets) {
        if (id.value == 0 || !ids.insert(id.value).second)
            return diagnostic_list_t{
                error(diagnostic_code_t::duplicate_id,
                      "duration resize contains an invalid or duplicate note ID", id)};
    }
    return ids;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto resize_one_note(duration_resize_context_t& context, const resize_parameters_t& parameters,
                     duration_resize_plan_t& plan, object_id_t id) -> std::optional<diagnostic_t> {
    const auto found = context.target_notes.find(id.value);
    const auto* note = found == context.target_notes.end() ? nullptr : found->second;
    if (!note || (note->kind != note_kind_t::tap && note->kind != note_kind_t::hold))
        return error(diagnostic_code_t::invalid_hold,
                     "selected duration resize contains a non-tap/hold note", id);
    const auto begin = context.row_for(note->position);
    const auto old_end = note->end ? context.row_for(*note->end) : begin;
    if (!begin || !old_end)
        return error(diagnostic_code_t::row_out_of_range,
                     "selected note duration lies outside chart geometry", id);
    const auto old_duration = *old_end - *begin;
    if (parameters.shrinking && parameters.delta > old_duration)
        return error(diagnostic_code_t::invalid_hold,
                     "duration delta would move a selected tail before its head", id);
    if (!parameters.shrinking &&
        parameters.delta > std::numeric_limits<std::uint64_t>::max() - old_duration)
        return error(diagnostic_code_t::row_out_of_range, "selected note duration would overflow",
                     id);
    const auto duration =
        parameters.shrinking ? old_duration - parameters.delta : old_duration + parameters.delta;
    if (duration > context.index.total_rows - *begin)
        return error(diagnostic_code_t::row_out_of_range,
                     "selected note tail would leave chart geometry", id);
    const auto end = *begin + duration;
    const auto end_position =
        duration == 0 ? std::optional<local_position_t>{} : position_at(context.index, end);
    if (duration != 0 && !end_position)
        return error(diagnostic_code_t::row_out_of_range,
                     "selected note tail cannot be represented in the chart", id);
    if (end_position &&
        find_active_lane(context.chart, context.index, *end_position, note->lane) == nullptr)
        return error(diagnostic_code_t::unknown_lane,
                     "selected note lane is unavailable at its resized tail", id);
    plan.resized.push_back({id, note->lane, *begin, end, end_position});
    context.collisions.push_back({note->lane, *begin, end, id, true});
    return std::nullopt;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto resize_selected_notes(duration_resize_context_t& context,
                           const resize_parameters_t& parameters, duration_resize_plan_t& plan)
    -> std::optional<diagnostic_t> {
    plan.resized.reserve(plan.notes.size());
    for (const auto id : plan.notes)
        if (auto failure = resize_one_note(context, parameters, plan, id))
            return failure;
    return std::nullopt;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto validate_resize_collisions(std::vector<duration_collision_interval_t>& collisions)
    -> std::optional<diagnostic_t> {
    std::ranges::sort(collisions, [](const auto& left, const auto& right) {
        if (left.lane != right.lane)
            return left.lane.value < right.lane.value;
        if (left.begin != right.begin)
            return left.begin < right.begin;
        if (left.end != right.end)
            return left.end < right.end;
        return left.resizing < right.resizing;
    });
    duration_collision_sweep_t sweep;
    for (const auto& interval : collisions)
        if (auto failure = sweep.process(interval))
            return failure;
    return std::nullopt;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto plan_duration_resize(const chart_document_t& chart, const measure_index_t& index,
                          std::vector<object_id_t> targets, object_id_t reference_id,
                          local_position_t reference_end, std::uint64_t snap_denominator,
                          bool reference_end_is_exact)
    -> std::variant<duration_resize_plan_t, diagnostic_list_t> {
    auto parameters_result =
        resolve_resize_parameters(chart, index, targets, reference_id, reference_end,
                                  snap_denominator, reference_end_is_exact);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&parameters_result))
        return *diagnostics;
    const auto parameters = std::get<resize_parameters_t>(parameters_result);
    auto ids_result = valid_resize_ids(targets);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&ids_result))
        return *diagnostics;
    duration_resize_context_t context{
        .chart = chart, .index = index, .measures = measure_lookup_t::from(index),
        .target_notes = {}, .collisions = {}};
    if (auto failure =
            context.collect_existing(std::get<std::unordered_set<std::uint64_t>>(ids_result)))
        return diagnostic_list_t{*failure};
    duration_resize_plan_t plan{.reference_note = reference_id,
                                .reference_lane = parameters.reference->lane,
                                .reference_end = parameters.snapped_end,
                                .notes = std::move(targets),
                                .resized = {}};
    if (auto failure = resize_selected_notes(context, parameters, plan))
        return diagnostic_list_t{*failure};
    if (auto failure = validate_resize_collisions(context.collisions))
        return diagnostic_list_t{*failure};
    return plan;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto duration_previews(const duration_resize_plan_t& plan) -> std::vector<note_duration_preview_t> {
    std::vector<note_duration_preview_t> result;
    result.reserve(plan.resized.size());
    for (const auto& note : plan.resized) {
        result.push_back(
            {note.id, note.end_position ? note_kind_t::hold : note_kind_t::tap, note.end_position});
    }
    return result;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto move_linked_sound_events(chart_document_t& chart, const note_move_plan_t& plan)
    -> moved_stem_events_t {
    moved_stem_events_t moved;
    for (const auto& candidate : plan.moved) {
        const auto* note = chart.find_note(candidate.id);
        if (!note || !note->sound_channel_event)
            continue;
        for (std::size_t index = 0; index < chart.sound_channels.size(); ++index) {
            auto& events = chart.sound_channels[index].events;
            const auto event =
                std::ranges::find(events, *note->sound_channel_event, &sound_channel_event_t::id);
            if (event == events.end())
                continue;
            event->position = candidate.position;
            event->end = candidate.end_position;
            if (event->playable)
                event->lane = candidate.lane;
            moved.channels.push_back(index);
            moved.events.push_back(event->id);
        }
    }
    std::ranges::sort(moved.channels);
    moved.channels.erase(std::unique(moved.channels.begin(), moved.channels.end()),
                         moved.channels.end());
    return moved;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto landed_on_taken_row(const sound_channel_t& channel, const moved_stem_events_t& moved)
    -> const sound_channel_event_t* {
    for (const auto& event : channel.events) {
        if (!moved.has_object(event.id))
            continue;
        const auto taken = std::ranges::any_of(channel.events, [&](const auto& other) {
            return !moved.has_object(other.id) && other.position == event.position &&
                   other.playable == event.playable;
        });
        if (taken)
            return &event;
    }
    return nullptr;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto sync_linked_sound_events(chart_document_t& chart, const note_move_plan_t& plan,
                              const song_document_t& document) -> std::optional<diagnostic_t> {
    const auto moved = move_linked_sound_events(chart, plan);
    for (const auto index : moved.channels) {
        const auto& channel = chart.sound_channels[index];
        if (const auto* taken = landed_on_taken_row(channel, moved))
            return error(diagnostic_code_t::collision,
                         "another point of this stem is already on that row", taken->id);
        const auto asset = document.find_audio_asset(channel.source_clip.source);
        if (asset == nullptr || asset->stream.sample_rate == 0)
            continue;
        if (!derive_sound_channel_slices(chart, channel, asset->stream.sample_rate,
                                         asset->stream.frame_count))
            return error(diagnostic_code_t::invalid_resolution,
                         "moving this point would leave the stem with unusable slices", channel.id);
    }
    return std::nullopt;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto known_move_lane(const chart_document_t& chart, lane_id_t id) -> const lane_definition_t* {
    for (const auto& layout : chart.lane_layouts) {
        for (const auto& lane : layout.layout.lanes) {
            if (lane.id == id && lane.is_editable())
                return &lane;
        }
    }
    return nullptr;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto move_lane_column(const chart_document_t& chart, const measure_index_t& index,
                      local_position_t position, const lane_definition_t& lane,
                      const editor_scratch_presentation_t& sides) -> std::int64_t {
    const auto* layout = effective_layout(chart, index, position);
    const auto active_column = layout ? editor_presented_column(*layout, lane.id, sides) : -1;
    if (active_column >= 0)
        return active_column;
    for (const auto& segment : chart.lane_layouts) {
        const auto column = editor_presented_column(segment.layout, lane.id, sides);
        if (column >= 0)
            return column;
    }
    return lane.display_order;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto resolve_note_move_parameters(const chart_document_t& chart, const measure_index_t& index,
                                  std::vector<object_id_t>& targets, object_id_t reference_id,
                                  local_position_t reference_position, lane_id_t requested_lane,
                                  std::uint64_t snap_denominator, bool exact, bool keep_rows,
                                  const editor_scratch_presentation_t& sides)
    -> std::variant<note_move_parameters_t, diagnostic_list_t> {
    const auto* reference = chart.find_note(reference_id);
    if (!reference || (reference->kind != note_kind_t::tap && reference->kind != note_kind_t::hold))
        return diagnostic_list_t{error(diagnostic_code_t::invalid_id,
                                       "note move requires a tap or hold reference note",
                                       reference_id)};
    auto snapped =
        exact ? (absolute_row(index, reference_position) ? std::optional{reference_position}
                                                         : std::nullopt)
              : snapped_position(chart, index, reference_position, snap_denominator);
    // A pointer move that stays in the reference note's own grid cell (a
    // horizontal drag) keeps its exact row instead of resnapping it; Y-lock keeps
    // the row whatever the pointer does.
    if (!exact && snapped) {
        const auto home = snapped_position(chart, index, reference->position, snap_denominator);
        if (keep_rows || (home && *home == *snapped))
            snapped = reference->position;
    }
    const auto begin = absolute_row(index, reference->position);
    const auto target = snapped ? absolute_row(index, *snapped) : std::nullopt;
    if (!snapped || !begin || !target)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "note move reference cannot be snapped inside chart",
                                       reference_id)};
    if (std::ranges::find(targets, reference_id) == targets.end())
        targets = {reference_id};
    const auto ids = valid_resize_ids(targets);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&ids)) {
        auto errors = *diagnostics;
        if (!errors.empty())
            errors.front().message = "note move contains an invalid or duplicate note ID";
        return errors;
    }
    const auto preserve = !requested_lane;
    const auto* source_lane = find_active_lane(chart, index, reference->position, reference->lane);
    const auto desired_lane = preserve ? reference->lane : requested_lane;
    const auto* destination = find_active_lane(chart, index, *snapped, desired_lane);
    if (!destination)
        destination = known_move_lane(chart, desired_lane);
    if (!source_lane || !destination)
        return diagnostic_list_t{error(diagnostic_code_t::unknown_lane,
                                       "note move reference lane is unavailable at its target row",
                                       reference_id)};
    return note_move_parameters_t{
        .reference = reference,
        .snapped_position = *snapped,
        .delta = *target >= *begin ? *target - *begin : *begin - *target,
        .forward = *target >= *begin,
        .preserve_lanes = preserve,
        .target_lane = destination->id,
        .lane_delta =
            preserve ? 0
                     : move_lane_column(chart, index, *snapped, *destination, sides) -
                           move_lane_column(chart, index, reference->position, *source_lane, sides),
        .scratch_on_right = sides};
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto shifted_move_row(std::uint64_t row, std::uint64_t last,
                      const note_move_parameters_t& parameters) -> shifted_move_row_t {
    if (parameters.forward) {
        if (parameters.delta > last - row)
            return {last, false, true};
        return {row + parameters.delta, false, false};
    }
    if (parameters.delta > row)
        return {0, true, false};
    return {row - parameters.delta, false, false};
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto display_move_row(std::uint64_t row, const note_move_parameters_t& parameters) -> long double {
    const auto delta = static_cast<long double>(parameters.delta);
    return static_cast<long double>(row) + (parameters.forward ? delta : -delta);
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto move_one_note(note_move_context_t& context, object_id_t id, note_move_plan_t& plan)
    -> std::optional<diagnostic_t> {
    const auto found = context.targets.find(id.value);
    const auto* note = found == context.targets.end() ? nullptr : found->second;
    if (!note || (note->kind != note_kind_t::tap && note->kind != note_kind_t::hold))
        return error(diagnostic_code_t::invalid_hold,
                     "selected note move contains a non-tap/hold note", id);
    const auto begin = absolute_row(context.index, note->position);
    const auto end = note->end ? absolute_row(context.index, *note->end) : begin;
    if (!begin || !end || *end < *begin)
        return error(diagnostic_code_t::row_out_of_range,
                     "selected note move source lies outside chart geometry", id);
    const auto last = context.index.total_rows - 1;
    const auto shifted_begin = shifted_move_row(*begin, last, context.parameters);
    const auto shifted_end = shifted_move_row(*end, last, context.parameters);
    const auto outside = shifted_begin.before_chart || shifted_end.after_chart;
    const auto moved_begin = shifted_begin.row;
    const auto moved_end = shifted_end.row;
    const auto position = position_at(context.index, moved_begin);
    const auto end_position =
        note->end ? position_at(context.index, moved_end) : std::optional<local_position_t>{};
    if (!position || (note->end && !end_position))
        return error(diagnostic_code_t::row_out_of_range,
                     "selected note move could not resolve its target row", id);
    auto destination = context.destination_lane(*note, *position);
    if (const auto* failure = std::get_if<diagnostic_t>(&destination))
        return *failure;
    const auto target = std::get<move_destination_t>(destination);
    const auto available =
        target.available && context.is_lane_available_across(moved_begin, moved_end, target.lane);
    auto conflict = available ? note_move_conflict_t::none : note_move_conflict_t::out_of_lane;
    if (available && is_head_blocked_by_warp(context.chart, context.index, moved_begin, note->kind))
        conflict = note_move_conflict_t::other;
    if (outside)
        conflict = note_move_conflict_t::out_of_chart;
    plan.moved.push_back(
        {id,
         target.lane,
         moved_begin,
         moved_end,
         *position,
         end_position,
         conflict,
         {},
         target.column,
         target.columns,
         outside ? std::optional{display_move_row(*begin, context.parameters)} : std::nullopt,
         outside && note->end ? std::optional{display_move_row(*end, context.parameters)}
                              : std::nullopt});
    // Partially visible invalid holds still occupy their safely clipped interval.
    if (target.available && !shifted_begin.after_chart && !shifted_end.before_chart)
        context.collisions.push_back({target.lane, moved_begin, moved_end, id, true});
    return std::nullopt;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto merged_move_ranges(const std::vector<duration_collision_interval_t>& collisions,
                        std::size_t moved_count) -> std::vector<duration_collision_interval_t> {
    std::vector<duration_collision_interval_t> ranges;
    ranges.reserve(moved_count);
    for (const auto& interval : collisions)
        if (interval.resizing)
            ranges.push_back(interval);
    std::ranges::sort(ranges, [](const auto& left, const auto& right) {
        return std::tie(left.lane.value, left.begin, left.end) <
               std::tie(right.lane.value, right.begin, right.end);
    });
    std::size_t merged = 0;
    for (const auto& range : ranges) {
        if (merged != 0 && ranges[merged - 1].lane == range.lane &&
            range.begin <= ranges[merged - 1].end) {
            ranges[merged - 1].end = std::max(ranges[merged - 1].end, range.end);
        } else {
            ranges[merged++] = range;
        }
    }
    ranges.resize(merged);
    return ranges;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto is_overlapping_move_range(const duration_collision_interval_t& interval,
                         const std::vector<duration_collision_interval_t>& ranges) -> bool {
    const auto key = std::pair{interval.lane.value, interval.begin};
    const auto found = std::ranges::lower_bound(
        ranges, key, {}, [](const auto& range) { return std::pair{range.lane.value, range.end}; });
    return found != ranges.end() && found->lane == interval.lane && found->begin <= interval.end;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
void mark_move_collisions(std::vector<duration_collision_interval_t>& collisions,
                          note_move_plan_t& plan) {
    const auto ranges = merged_move_ranges(collisions, plan.moved.size());
    // Only intervals intersecting a proposed closed range can be occupants.
    // Keeping all moved intervals preserves selected-selected conflicts,
    // including clipped invalid hold portions.
    std::erase_if(collisions, [&](const auto& interval) {
        return !interval.resizing && !is_overlapping_move_range(interval, ranges);
    });
    std::ranges::sort(collisions, [](const auto& left, const auto& right) {
        return std::tie(left.lane.value, left.begin, left.end, left.resizing, left.note.value) <
               std::tie(right.lane.value, right.begin, right.end, right.resizing, right.note.value);
    });
    note_move_collision_sweep_t sweep{plan};
    for (const auto& interval : collisions)
        sweep.process(interval);
    for (const auto id : sweep.occupants)
        plan.occupants.push_back(object_id_t{id});
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto plan_note_move(const chart_document_t& chart, const measure_index_t& index,
                    std::vector<object_id_t> targets, object_id_t reference_id,
                    local_position_t reference_position, lane_id_t requested_reference_lane,
                    std::uint64_t snap_denominator, bool reference_position_is_exact,
                    bool keep_rows, const editor_scratch_presentation_t& sides)
    -> std::variant<note_move_plan_t, diagnostic_list_t> {
    auto parameters_result = resolve_note_move_parameters(
        chart, index, targets, reference_id, reference_position, requested_reference_lane,
        snap_denominator, reference_position_is_exact, keep_rows, sides);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&parameters_result))
        return *diagnostics;
    const auto parameters = std::get<note_move_parameters_t>(parameters_result);
    auto ids_result = valid_resize_ids(targets);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&ids_result)) {
        auto errors = *diagnostics;
        if (!errors.empty())
            errors.front().message = "note move contains an invalid or duplicate note ID";
        return errors;
    }
    duration_resize_context_t collected{
        .chart = chart, .index = index, .measures = measure_lookup_t::from(index),
        .target_notes = {}, .collisions = {}};
    if (auto failure =
            collected.collect_existing(std::get<std::unordered_set<std::uint64_t>>(ids_result))) {
        auto diagnostics = diagnostic_list_t{*failure};
        if (!diagnostics.empty())
            diagnostics.front().message = "existing note lies outside chart geometry";
        return diagnostics;
    }
    note_move_plan_t plan{.reference_note = reference_id,
                          .reference_lane = parameters.target_lane,
                          .reference_position = parameters.snapped_position,
                          .notes = std::move(targets),
                          .moved = {},
                          .occupants = {},
                          .bgm_moves = {}};
    note_move_context_t context{.chart = chart,
                                .index = index,
                                .parameters = parameters,
                                .targets = collected.target_notes,
                                .collisions = collected.collisions};
    plan.moved.reserve(plan.notes.size());
    for (const auto id : plan.notes)
        if (auto failure = move_one_note(context, id, plan))
            return diagnostic_list_t{*failure};
    mark_move_collisions(collected.collisions, plan);
    return plan;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto selected_move_bgm_ids(const editor_interaction_state_t& state, object_id_t reference,
                           std::span<const object_id_t>) -> std::vector<object_id_t> {
    const auto reference_selected =
        std::ranges::find(state.selected_notes, reference) != state.selected_notes.end() ||
        std::ranges::any_of(state.selected_objects,
                            [&](const auto& object) { return object.id == reference; });
    if (!reference_selected)
        return {};
    std::vector<object_id_t> ids;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::bgm_event)
            ids.push_back(object.id);
    return ids;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto translated_object_position(const measure_index_t& index, local_position_t position,
                                std::uint64_t origin, std::uint64_t target)
    -> std::optional<local_position_t> {
    const auto row = absolute_row(index, position);
    if (!row)
        return std::nullopt;
    const auto delta = target >= origin ? target - origin : origin - target;
    if (target < origin && delta > *row)
        return std::nullopt;
    if (target >= origin && delta > std::numeric_limits<std::uint64_t>::max() - *row)
        return std::nullopt;
    return position_at_absolute_row(index, target >= origin ? *row + delta : *row - delta);
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto append_selected_bgm_moves(const chart_document_t& chart, const measure_index_t& index,
                               std::span<const object_id_t> ids, local_position_t origin,
                               note_move_plan_t& plan) -> diagnostic_list_t {
    const auto from = absolute_row(index, origin);
    const auto to = absolute_row(index, plan.reference_position);
    if (!from || !to)
        return {error(diagnostic_code_t::row_out_of_range,
                      "Mixed move reference is outside chart geometry")};
    for (const auto id : ids) {
        const auto* event = chart.find_bgm_event(id);
        if (!event)
            return {error(diagnostic_code_t::invalid_id, "Selected BGM event is unavailable", id)};
        const auto position = translated_object_position(index, event->position, *from, *to);
        const auto end =
            event->end ? translated_object_position(index, *event->end, *from, *to) : std::nullopt;
        if (!position || (event->end && !end))
            return {error(diagnostic_code_t::row_out_of_range,
                          "Mixed move would place a BGM head or tail outside the chart", id)};
        plan.bgm_moves.push_back({id, event->source_lane, *position, end, event->sound});
    }
    return {};
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto plan_selected_object_move(const chart_document_t& chart, const measure_index_t& index,
                               const editor_interaction_state_t& state,
                               std::vector<object_id_t> notes, object_id_t reference,
                               local_position_t position, lane_id_t lane, std::uint64_t snap,
                               bool exact) -> std::variant<note_move_plan_t, diagnostic_list_t> {
    const auto bgm_ids = selected_move_bgm_ids(state, reference, notes);
    const auto* reference_note = chart.find_note(reference);
    const auto* reference_bgm = chart.find_bgm_event(reference);
    if (reference_note) {
        auto planned = plan_note_move(chart, index, std::move(notes), reference, position, lane,
                                      snap, exact, state.y_lock, state.scratch_on_right);
        if (auto* plan = std::get_if<note_move_plan_t>(&planned)) {
            auto diagnostics =
                append_selected_bgm_moves(chart, index, bgm_ids, reference_note->position, *plan);
            if (!diagnostics.empty())
                return diagnostics;
        }
        return planned;
    }
    if (!reference_bgm || bgm_ids.empty())
        return diagnostic_list_t{
            error(diagnostic_code_t::invalid_id, "Move reference is unavailable", reference)};
    const auto target =
        state.y_lock ? std::optional{reference_bgm->position}
                     : resolve_resource_placement_position(chart, position, exact ? 0 : snap);
    if (!target)
        return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                       "BGM move reference is outside chart geometry")};
    note_move_plan_t plan{.reference_note = reference,
                          .reference_lane = state.active_lane,
                          .reference_position = *target,
                          .notes = {},
                          .moved = {},
                          .occupants = {},
                          .bgm_moves = {}};
    if (!notes.empty()) {
        const auto* first = chart.find_note(notes.front());
        const auto origin = absolute_row(index, reference_bgm->position);
        const auto destination = absolute_row(index, *target);
        const auto shifted =
            first && origin && destination
                ? translated_object_position(index, first->position, *origin, *destination)
                : std::nullopt;
        if (!shifted)
            return diagnostic_list_t{error(diagnostic_code_t::row_out_of_range,
                                           "Mixed move note is outside chart geometry")};
        auto planned = plan_note_move(chart, index, std::move(notes), first->id, *shifted, {}, 0,
                                      true, state.y_lock, state.scratch_on_right);
        if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&planned))
            return *diagnostics;
        plan = std::get<note_move_plan_t>(std::move(planned));
        plan.reference_note = reference;
        plan.reference_position = *target;
    }
    auto diagnostics =
        append_selected_bgm_moves(chart, index, bgm_ids, reference_bgm->position, plan);
    if (!diagnostics.empty())
        return diagnostics;
    return plan;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto stage_single_bgm_move(document_history_t& scratch, chart_id_t chart, const bgm_event_t& source,
                           local_position_t position, object_id_t lane,
                           std::optional<std::uint64_t> canvas_row, std::uint64_t snap)
    -> document_command_result_t {
    if (source.sound_channel_event) {
        if (canvas_row)
            return {.revision = scratch.document.revision,
                    .diagnostics = {error(diagnostic_code_t::row_out_of_range,
                                          "Stem markers cannot move into virtual BMS rows")}};
        return move_bmson_bgm_marker(scratch, chart, source.id,
                                     position);
    }
    if (canvas_row)
        return apply_virtual_bms_bgm_event_move(scratch,
                                                {.chart = chart,
                                                 .event = source.id,
                                                 .canvas_row = *canvas_row,
                                                 .source_lane = lane,
                                                 .snap_denominator = snap});
    return apply_bgm_event_move(
        scratch,
        {.chart = chart, .event = source.id, .position = position, .source_lane = lane});
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto plan_single_bgm_move(const document_history_t& history,
                          const editor_interaction_state_t& state, const bgm_event_t& source,
                          local_position_t requested, std::optional<object_id_t> lane,
                          std::optional<std::uint64_t> canvas_row, bool exact)
    -> std::variant<note_move_plan_t, diagnostic_list_t> {
    const auto& chart = *history.document.find_chart(state.chart);
    const auto target =
        state.y_lock ? std::optional{source.position}
        : canvas_row ? std::optional{source.position}
                     : resolve_resource_placement_position(
                           chart, requested, exact ? 0 : edit_snap_denominator(chart, state));
    if (!target)
        return diagnostic_list_t{
            error(diagnostic_code_t::row_out_of_range, "BGM move cannot snap to this chart row")};
    auto scratch = document_history_t::from(history.document);
    const auto moved = stage_single_bgm_move(
        scratch, state.chart, source, *target, lane.value_or(source.source_lane),
        state.y_lock ? std::nullopt : canvas_row, exact ? 0 : edit_snap_denominator(chart, state));
    if (!moved.diagnostics.empty())
        return moved.diagnostics;
    const auto& destination = *scratch.document.find_chart(state.chart);
    const auto* event = destination.find_bgm_event(source.id);
    const auto built = build_measure_index(destination);
    if (const auto* errors = std::get_if<diagnostic_list_t>(&built))
        return *errors;
    const auto& index = std::get<measure_index_t>(built);
    note_move_bgm_preview_t preview{event->id, event->source_lane, event->position, event->end,
                                    event->sound};
    preview.canvas_begin_row = absolute_row(index, event->position);
    preview.canvas_end_row =
        event->end ? absolute_row(index, *event->end) : preview.canvas_begin_row;
    note_move_plan_t plan{.reference_note = source.id,
                          .reference_lane = state.active_lane,
                          .reference_position = event->position,
                          .notes = {},
                          .moved = {},
                          .occupants = {},
                          .bgm_moves = {preview}};
    if (moved.applied)
        plan.staged_bgm_document = std::move(scratch.document);
    return plan;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto plan_move_gesture(const document_history_t& history, const editor_interaction_state_t& state,
                       const chart_document_t& chart, const measure_index_t& index,
                       std::vector<object_id_t> targets, object_id_t reference,
                       local_position_t position, lane_id_t lane, bool exact_position,
                       std::optional<object_id_t> bgm_lane,
                       std::optional<std::uint64_t> virtual_row)
    -> std::variant<note_move_plan_t, diagnostic_list_t> {
    const auto* bgm = chart.find_bgm_event(reference);
    if (bgm && state.selected_notes.empty() && state.selected_objects.size() == 1 &&
        state.selected_objects.front() ==
            editor_selection_object_t{editor_selection_object_kind_t::bgm_event, reference})
        return plan_single_bgm_move(history, state, *bgm, position, bgm_lane, virtual_row,
                                    exact_position);
    if (virtual_row)
        return diagnostic_list_t{
            error(diagnostic_code_t::row_out_of_range, "Mixed moves require authored chart rows")};
    return plan_selected_object_move(chart, index, state, std::move(targets), reference, position,
                                     lane, edit_snap_denominator(chart, state), exact_position);
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto relocate_selected_bgm(song_document_t& proposed, chart_id_t chart_id,
                           const note_move_plan_t& plan) -> diagnostic_list_t {
    if (plan.bgm_moves.empty())
        return {};
    auto scratch = document_history_t::from(proposed);
    std::vector<bgm_event_t> ordinary;
    for (const auto& preview : plan.bgm_moves) {
        auto* chart = scratch.document.find_chart(chart_id);
        const auto* source = (*chart).find_bgm_event(preview.note);
        if (!source)
            return {error(diagnostic_code_t::invalid_id, "Selected BGM event is unavailable",
                          preview.note)};
        if (source->sound_channel_event) {
            const auto moved = move_bmson_bgm_marker(scratch, chart_id,
                                                     preview.note, preview.position);
            if (!moved.diagnostics.empty())
                return moved.diagnostics;
        } else {
            auto moved = *source;
            if (moved.position != preview.position || moved.end != preview.end) {
                moved.structural_slot = 0;
                moved.provenance = {};
            }
            moved.position = preview.position;
            moved.end = preview.end;
            ordinary.push_back(std::move(moved));
        }
    }
    auto* chart = scratch.document.find_chart(chart_id);
    for (auto& measure : chart->measures)
        std::erase_if(measure.bgm_events, [&](const auto& event) {
            return std::ranges::any_of(ordinary,
                                       [&](const auto& moved) { return moved.id == event.id; });
        });
    for (auto& moved : ordinary) {
        const auto destination =
            chart->find_measure(moved.position.measure);
        if (destination == nullptr)
            return {error(diagnostic_code_t::invalid_measure,
                          "Mixed move BGM destination is unavailable", moved.id)};
        destination->bgm_events.push_back(std::move(moved));
    }
    // Validate the complete destination set, so selected objects vacate their old
    // rows together. The ordinary single-object validator allocates lanes on
    // collision; a group move keeps lanes.
    for (const auto& preview : plan.bgm_moves) {
        const auto* source = (*scratch.document.find_chart(chart_id)).find_bgm_event(preview.note);
        if (source->sound_channel_event)
            continue;
        const auto validated = apply_bgm_event_move(scratch,
                                                    {.chart = chart_id,
                                                     .event = preview.note,
                                                     .position = preview.position,
                                                     .source_lane = preview.lane});
        if (!validated.diagnostics.empty())
            return validated.diagnostics;
        if (validated.applied)
            return {error(diagnostic_code_t::collision,
                          "Mixed move would collide in a BGM source lane", preview.note)};
    }
    proposed = std::move(scratch.document);
    return {};
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto move_previews(const note_move_plan_t& plan) -> std::vector<note_move_preview_t> {
    std::vector<note_move_preview_t> result;
    result.reserve(plan.moved.size());
    for (const auto& note : plan.moved) {
        result.push_back({note.id, note.lane, note.position, note.end_position, note.conflict,
                          note.occupant, note.display_column, note.display_columns,
                          note.display_begin_row, note.display_end_row});
    }
    return result;
}
} // namespace wormedit::interaction_detail

namespace wormedit::interaction_detail {
auto move_conflict_count(const note_move_plan_t& plan) -> std::size_t {
    return std::ranges::count_if(
        plan.moved, [](const auto& note) { return note.conflict != note_move_conflict_t::none; });
}
} // namespace wormedit::interaction_detail

namespace wormedit {
using namespace interaction_detail;
auto apply_duration_resize_plan(document_history_t& history, editor_interaction_state_t& state,
                                const duration_resize_plan_t& plan) -> editor_interaction_result_t {
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto* chart = proposed.find_chart(state.chart);
    auto changed = false;
    for (const auto& candidate : plan.resized) {
        auto* note = (*chart).find_note(candidate.id);
        const auto kind = candidate.end_position ? note_kind_t::hold : note_kind_t::tap;
        changed = changed || note->kind != kind || note->end != candidate.end_position;
        note->kind = kind;
        note->end = candidate.end_position;
    }
    if (changed) {
        auto command = transaction.commit(plan.notes.size() == 1 ? "Resize note duration" : "Resize selected note durations");
        if (!command)
            return diagnostic_result(history, state, std::move(command.diagnostics));
    }
    state.pending_note_duration_gesture.reset();
    set_selected_notes(state, plan.notes);
    state.active_lane = plan.reference_lane;
    place_cursor_at(state, plan.reference_end);
    auto result = editor_interaction_result_t::from(history, state, changed);
    result.accepted = true;
    return result;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto has_note_move_changes(const chart_document_t& chart, const note_move_plan_t& plan) -> bool {
    for (const auto& candidate : plan.moved) {
        const auto* note = chart.find_note(candidate.id);
        if (!note || note->position != candidate.position || note->lane != candidate.lane ||
            note->end != candidate.end_position)
            return true;
    }
    return false;
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto relocate_moved_notes(chart_document_t& chart, const note_move_plan_t& plan,
                          document_history_t& history, editor_interaction_state_t& state)
    -> std::optional<diagnostic_t> {
    for (const auto& candidate : plan.moved) {
        measure_chunk_t* source{};
        auto found = std::vector<note_t>::iterator{};
        for (auto measure = chart.measures.begin(); measure != chart.measures.end(); ++measure) {
            const auto item = std::ranges::find(measure->notes, candidate.id, &note_t::id);
            if (item == measure->notes.end())
                continue;
            source = &*measure;
            found = item;
            break;
        }
        auto destination =
            chart.find_measure(candidate.position.measure);
        if (source == nullptr || destination == nullptr)
            return error(diagnostic_code_t::invalid_id,
                         "note move could not relocate a planned note", candidate.id);
        auto moved = std::move(*found);
        source->notes.erase(found);
        moved.position = candidate.position;
        moved.lane = candidate.lane;
        moved.end = candidate.end_position;
        destination->notes.push_back(std::move(moved));
    }
    (void)state;
    return sync_linked_sound_events(chart, plan, history.document);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto stage_bgm_note_move(const document_history_t& history, editor_interaction_state_t& state,
                         const note_move_plan_t& plan, const pending_note_move_gesture_t& pending)
    -> std::variant<song_document_t, diagnostic_list_t> {
    for (const auto& moved : plan.moved) {
        if (moved.conflict == note_move_conflict_t::none ||
            (pending.to_bgm && moved.conflict == note_move_conflict_t::out_of_lane))
            continue;
        if (moved.conflict != note_move_conflict_t::occupied)
            return diagnostic_list_t{
                error(diagnostic_code_t::collision, "Note move lies outside chart geometry")};
        if (!pending.to_bgm && (!pending.displace_to_bgm || !moved.occupant ||
                                std::ranges::find(plan.notes, *moved.occupant) != plan.notes.end()))
            return diagnostic_list_t{
                error(diagnostic_code_t::collision, "Selected notes would overlap")};
    }
    auto scratch = document_history_t::from(history.document);
    const auto* source = history.document.find_chart(state.chart);
    const note_to_bgm_policy_t policy{.allow_new_lanes =
                                          source->source_family == format_family_t::bms ||
                                          source->source_family == format_family_t::bmson ||
                                          source->source_family == format_family_t::native_project};
    if (!pending.to_bgm) {
        auto converted = move_notes_to_bgm(scratch, state.chart,
                                           plan.occupants, std::nullopt, policy);
        if (!converted)
            return converted.diagnostics;
    }
    auto* chart = scratch.document.find_chart(state.chart);
    if (auto failure = relocate_moved_notes(*chart, plan, scratch, state))
        return diagnostic_list_t{*failure};
    if (pending.to_bgm) {
        auto converted = move_notes_to_bgm(scratch, state.chart,
                                           plan.notes, pending.preferred_bgm_lane, policy);
        if (!converted)
            return converted.diagnostics;
    }
    auto failures = relocate_selected_bgm(scratch.document, state.chart, plan);
    if (!failures.empty())
        return failures;
    failures = validate_document(scratch.document);
    if (!failures.empty())
        return failures;
    return std::move(scratch.document);
}
} // namespace wormedit

namespace wormedit {
using namespace interaction_detail;
auto apply_note_move_plan(document_history_t& history, editor_interaction_state_t& state,
                          const note_move_plan_t& plan) -> editor_interaction_result_t {
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(plan.staged_bgm_document.value_or(history.document));
    auto& proposed = transaction.copy_document();
    auto* chart = proposed.find_chart(state.chart);
    const auto changed = plan.staged_bgm_document.has_value() || has_note_move_changes(*chart, plan) ||
                         std::ranges::any_of(plan.bgm_moves, [&](const auto& move) {
                             const auto* source = (*chart).find_bgm_event(move.note);
                             return !source || source->position != move.position ||
                                    source->end != move.end || source->source_lane != move.lane;
                         });
    if (changed) {
        if (auto failure = relocate_moved_notes(*chart, plan, history, state))
            return diagnostic_result(history, state, {*failure});
        auto failures = plan.staged_bgm_document
                            ? diagnostic_list_t{}
                            : relocate_selected_bgm(proposed, state.chart, plan);
        if (!failures.empty()) {
            state.pending_note_move_gesture.reset();
            return diagnostic_result(history, state, std::move(failures));
        }
        auto command = transaction.commit(plan.bgm_moves.empty() ? (plan.notes.size() == 1 ? "Move note" : "Move selected notes")
                                   : "Move selected chart objects");
        if (!command)
            return diagnostic_result(history, state, std::move(command.diagnostics));
    }
    state.pending_note_move_gesture.reset();
    set_selected_notes(state, plan.notes);
    for (const auto& moved : plan.bgm_moves)
        state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, moved.note});
    state.active_lane = plan.reference_lane;
    place_cursor_at(state, plan.reference_position);
    auto result = editor_interaction_result_t::from(history, state, changed);
    result.accepted = true;
    return result;
}
} // namespace wormedit
