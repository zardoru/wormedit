#include "interaction_edit_commit.h"
#include "chart_object_ids.h"
#include <wormedit/core/timing_lane_catalog.h>
#include <wormedit/core/rational.h>
#include <wormedit/core/chart_clipboard.h>
#include "lane_occupancy.h"

#include <wormedit/core/sound_slice.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/kiai_spans.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/timing_lane_controller.h>
#include <wormedit/core/interaction_dispatch.h>

#include <algorithm>
#include <numeric>
#include <set>
#include <limits>
#include <tuple>

namespace wormedit {
namespace {
constexpr std::size_t maximum_objects = 32768;

using wormedit::rows_to_fraction;

auto note_lane(const chart_document_t& chart, const measure_index_t& index,
               const note_t& note) -> std::optional<std::size_t> {
    const auto* layout = layout_at(chart, index, note.position);
    if (!layout) return std::nullopt;
    const auto found = layout->find(note.lane);
    if (found == nullptr) return std::nullopt;
    return static_cast<std::size_t>(found - layout->lanes.data());
}

auto selected_objects(const editor_interaction_state_t& state)
    -> std::set<editor_selection_object_t> {
    std::set<editor_selection_object_t> selected(state.selected_objects.begin(),
                                               state.selected_objects.end());
    for (const auto id : state.selected_notes)
        selected.insert({editor_selection_object_kind_t::note, id});
    return selected;
}

auto selected_kiai_boundaries(const song_document_t& document,
                              const editor_interaction_state_t& state, object_id_t head)
    -> std::optional<std::vector<editor_selection_object_t>> {
    if (!head) return std::vector<editor_selection_object_t>{};
    const auto chart = document.find_chart(state.chart);
    if (chart == nullptr) return std::nullopt;
    const auto indexed = measure_index_t::from(*chart);
    if (!indexed) return std::nullopt;
    for (const auto& span : kiai_spans_of(*chart, *indexed)) {
        if (span.head != head) continue;
        std::vector<editor_selection_object_t> result{{editor_selection_object_kind_t::kiai_event, span.head}};
        if (!span.open_ended) result.push_back({editor_selection_object_kind_t::kiai_event, span.tail});
        return result;
    }
    return std::nullopt;
}

// The BMSON stem point behind a semantic object, if it has one.
auto stem_of(const chart_document_t& chart, const std::optional<object_id_t>& link)
    -> std::optional<chart_clipboard_stem_t> {
    if (!link) return std::nullopt;
    for (const auto& channel : chart.sound_channels) {
        const auto event = std::ranges::find(channel.events, *link, &sound_channel_event_t::id);
        if (event != channel.events.end())
            return chart_clipboard_stem_t{channel.id, event->continuation, event->playable};
    }
    return std::nullopt;
}

auto append_note(chart_clipboard_t& clipboard, const chart_document_t& chart,
                 const measure_index_t& index, const note_t& note) -> bool {
    const auto row = index.row_of(note.position);
    const auto lane = note_lane(chart, index, note);
    const auto end = note.end ? index.row_of(*note.end) : row;
    if (!row || !lane || !end || *end < *row) return false;
    clipboard.objects.push_back({rows_to_fraction(*row, chart.divisions_per_standard_measure),
        rows_to_fraction(*end - *row, chart.divisions_per_standard_measure), *lane, note,
        stem_of(chart, note.sound_channel_event)});
    return true;
}

auto append_bgm(chart_clipboard_t& clipboard, const chart_document_t& chart,
                const measure_index_t& index, const bgm_event_t& event) -> bool {
    const auto row = index.row_of(event.position);
    const auto lane = std::ranges::find(chart.bgm_source_lanes, event.source_lane,
                                       &bgm_source_lane_t::id);
    const auto end = event.end ? index.row_of(*event.end) : row;
    if (!row || !end || *end < *row || lane == chart.bgm_source_lanes.end()) return false;
    clipboard.objects.push_back({rows_to_fraction(*row, chart.divisions_per_standard_measure),
        rows_to_fraction(*end - *row, chart.divisions_per_standard_measure),
        static_cast<std::size_t>(lane - chart.bgm_source_lanes.begin()), event,
        stem_of(chart, event.sound_channel_event)});
    return true;
}

template <typename Event>
auto append_timing(chart_clipboard_t& clipboard, const chart_document_t& chart,
                   const measure_index_t& index, const Event& event) -> bool {
    std::optional<std::uint64_t> row;
    std::optional<std::uint64_t> end;
    if constexpr (std::is_same_v<Event, warp_event_t>) {
        row = index.row_of(event.span.begin);
        end = index.row_of(event.span.end);
    } else {
        row = index.row_of(event.position);
        end = row;
    }
    if (!row || !end || *end < *row) return false;
    clipboard.objects.push_back({.offset = rows_to_fraction(*row, chart.divisions_per_standard_measure),
        .duration = rows_to_fraction(*end - *row, chart.divisions_per_standard_measure), .lane_index = 0,
        .payload = event});
    return true;
}

template <typename Event>
auto collect_timing_events(chart_clipboard_t& clipboard, const chart_document_t& chart,
                           const measure_index_t& index, const std::vector<Event>& events,
                           std::set<editor_selection_object_t>& selected,
                           editor_selection_object_kind_t kind) -> bool {
    for (const auto& event : events) {
        if (selected.erase({kind, event.id}) && !append_timing(clipboard, chart, index, event))
            return false;
    }
    return true;
}

auto collect_pauses(chart_clipboard_t& clipboard, const chart_document_t& chart,
                    const measure_index_t& index, std::set<editor_selection_object_t>& selected)
    -> bool {
    for (const auto& event : chart.pauses) {
        const auto kind = event.kind == pause_kind_t::stop
            ? editor_selection_object_kind_t::stop_event
            : editor_selection_object_kind_t::delay_event;
        if (selected.erase({kind, event.id}) && !append_timing(clipboard, chart, index, event))
            return false;
    }
    return true;
}

auto collect_timing(chart_clipboard_t& clipboard, const chart_document_t& chart,
                    const measure_index_t& index, std::set<editor_selection_object_t>& selected)
    -> bool {
    using kind_t = editor_selection_object_kind_t;
    return collect_timing_events(clipboard, chart, index, chart.bpms, selected, kind_t::bpm_event) &&
           collect_pauses(clipboard, chart, index, selected) &&
           collect_timing_events(clipboard, chart, index, chart.warps, selected, kind_t::warp_event) &&
           collect_timing_events(clipboard, chart, index, chart.scrolls, selected,
                                 kind_t::scroll_event) &&
           collect_timing_events(clipboard, chart, index, chart.speeds, selected,
                                 kind_t::speed_event) &&
           collect_timing_events(clipboard, chart, index, chart.kiai, selected, kind_t::kiai_event) &&
           collect_timing_events(clipboard, chart, index, chart.timing_anchors, selected,
                                 kind_t::timing_anchor);
}

auto collect(chart_clipboard_t& clipboard, const chart_document_t& chart,
             const measure_index_t& index, std::set<editor_selection_object_t>& selected)
    -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (selected.erase({editor_selection_object_kind_t::note, note.id}) &&
                !append_note(clipboard, chart, index, note)) return false;
        }
        for (const auto& event : measure.bgm_events) {
            if (selected.erase({editor_selection_object_kind_t::bgm_event, event.id}) &&
                !append_bgm(clipboard, chart, index, event)) return false;
        }
    }
    for (const auto& entry : index.entries) {
        if (selected.erase({editor_selection_object_kind_t::bar_line, object_id_t{entry.id.value}}))
            clipboard.bar_lines.push_back(
                rows_to_fraction(entry.absolute_begin_row, chart.divisions_per_standard_measure));
    }
    return collect_timing(clipboard, chart, index, selected) && selected.empty();
}
} // namespace

auto chart_clipboard_t::from(const song_document_t& document,
                             const editor_interaction_state_t& state,
                             std::uint64_t expected_revision, object_id_t kiai_head)
    -> chart_clipboard_outcome_t {
    const document_stamp_t stamp{.project = document.id, .revision = expected_revision};
    if (!stamp.is_current(document))
        return std::unexpected(stale_action_message("Chart selection copy"));
    const auto chart = document.find_chart(state.chart);
    if (chart == nullptr)
        return std::unexpected(std::string{"The selected chart no longer exists."});
    auto selected = selected_objects(state);
    const auto boundaries = selected_kiai_boundaries(document, state, kiai_head);
    if (!boundaries)
        return std::unexpected(std::string{"The selected Kiai area no longer exists."});
    selected.insert(boundaries->begin(), boundaries->end());
    if (selected.empty())
        return std::unexpected(std::string{"Select notes, BGM events, timing objects or bar lines to copy."});
    if (selected.size() > maximum_objects)
        return std::unexpected(std::string{"The clipboard object limit was exceeded."});
    const auto indexed = measure_index_t::from(*chart);
    if (!indexed || !chart->divisions_per_standard_measure)
        return std::unexpected(std::string{"The chart positions are invalid."});
    chart_clipboard_t clipboard{.source_song = document.id, .source_chart = chart->id};
    if (!collect(clipboard, *chart, *indexed, selected))
        return std::unexpected(
            std::string{"The selection contains unavailable or unsupported objects."});
    // Every absolute offset originated as an integer row in this resolution.
    const auto resolution = chart->divisions_per_standard_measure;
    const auto row_of = [resolution](rational_t offset) {
        return offset.numerator * (resolution / offset.denominator);
    };
    std::ranges::stable_sort(clipboard.objects, {}, [&row_of](const auto& object) { return row_of(object.offset); });
    std::ranges::sort(clipboard.bar_lines, {}, row_of);
    std::optional<std::uint64_t> anchor;
    if (!clipboard.objects.empty()) anchor = row_of(clipboard.objects.front().offset);
    if (!clipboard.bar_lines.empty())
        anchor = std::min(anchor.value_or(std::numeric_limits<std::uint64_t>::max()),
                          row_of(clipboard.bar_lines.front()));
    for (auto& object : clipboard.objects)
        object.offset = rows_to_fraction(row_of(object.offset) - *anchor, resolution);
    for (auto& offset : clipboard.bar_lines) offset = rows_to_fraction(row_of(offset) - *anchor, resolution);
    return clipboard;
}

namespace {
auto selection_error(std::string message) -> editor_interaction_result_t {
    return {.diagnostics = {{.severity = diagnostic_severity_t::error,
                             .code = diagnostic_code_t::unsupported_feature,
                             .message = std::move(message)}}};
}

auto selection_timing_lane(editor_selection_object_kind_t kind) -> std::optional<timing_lane_kind_t> {
    return timing_lane_for_selection(kind);
}

auto stage_selection_spans(document_history_t& staged, editor_interaction_state_t& state,
                           const selection_delete_options_t& options,
                           const std::vector<editor_selection_object_t>& boundaries) -> diagnostic_list_t {
    if (options.timing_locked && (options.kiai_head || options.recipe))
        return selection_error("Timing is locked.").diagnostics;
    if (options.kiai_head) {
        const auto edited =
            apply_marker_edit(staged, edit_kiai_span_action_t{.chart = state.chart,
                                                              .edit = {.head = options.kiai_head}});
        if (!edited) return edited.diagnostics;
        std::erase_if(state.selected_objects, [&](const auto& object) {
            return std::ranges::find(boundaries, object) != boundaries.end();
        });
    }
    if (options.recipe) {
        const auto edited =
            apply_sv_recipe_edit(staged, wormedit::delete_edit_t<wormedit::sv_recipe_t>{
                                             .chart = state.chart, .id = options.recipe});
        if (!edited) return edited.diagnostics;
    }
    return {};
}

auto stage_selection_timing(document_history_t& staged, editor_interaction_state_t& state,
                            bool timing_locked) -> diagnostic_list_t {
    std::vector<timing_lane_event_ref_t> timing;
    std::vector<editor_selection_object_t> other;
    for (const auto& object : state.selected_objects) {
        if (const auto lane = selection_timing_lane(object.kind)) timing.push_back({*lane, object.id});
        else other.push_back(object);
    }
    if (timing.empty()) return {};
    const auto deleted = delete_timing_lane_event_group(staged, state.chart, timing, timing_locked);
    if (!deleted) return deleted.diagnostics;
    state.selected_objects = std::move(other);
    return {};
}
} // namespace

static auto delete_selection_all_impl(document_history_t& history,
                                      editor_interaction_state_t& state,
                                      const selection_delete_options_t& options)
    -> editor_interaction_result_t {
    if ((state).is_pending()) return selection_error("Finish or cancel the active gesture first.");
    const auto boundaries = selected_kiai_boundaries(history.document, state, options.kiai_head);
    if (!boundaries) return selection_error("The selected Kiai area no longer exists.");
    auto proposed_state = state;
    const auto selected = selected_objects(state);
    proposed_state.selected_objects.assign(selected.begin(), selected.end());
    proposed_state.selected_notes.clear();
    const bool explicit_selection = !selected.empty() || options.kiai_head || options.recipe;
    auto staged = document_history_t::from(history.document, 1);
    auto diagnostics = stage_selection_spans(staged, proposed_state, options, *boundaries);
    if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    diagnostics = stage_selection_timing(staged, proposed_state, options.timing_locked);
    if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    if (!explicit_selection || !proposed_state.selected_objects.empty()) {
        const auto deleted = dispatch_editor_action(staged, proposed_state, delete_editor_selection_action_t{});
        if (!deleted)
            return {.diagnostics = deleted.diagnostics};
    }
    if (staged.undo.empty()) return {.accepted = true};
    auto transaction = document_command_transaction_t(history);
    transaction.adopt_document(std::move(staged.document));
    return commit_editor_edit(history, state, transaction,
                              {.label = options.label,
                               .retain_commit_diagnostics = true,
                               .view_policy = editor_edit_view_policy_t::omit},
                              [&](editor_interaction_state_t& published) {
                                  proposed_state.selected_notes.clear();
                                  proposed_state.selected_objects.clear();
                                  published = std::move(proposed_state);
                              });
}

auto delete_selection_all(document_history_t& history, editor_interaction_state_t& state,
                          const selection_delete_options_t& options)
    -> editor_interaction_result_t {
    auto result = delete_selection_all_impl(history, state, options);
    result.ensure_view(history, state);
    return result;
}

auto create_cut_chart_clipboard(document_history_t& history, editor_interaction_state_t& state,
                                selection_delete_options_t options)
    -> chart_clipboard_outcome_t {
    if (options.recipe)
        return std::unexpected(std::string{"SV recipes cannot be copied to the chart clipboard."});
    auto result =
        chart_clipboard_t::from(history.document, state, history.document.revision, options.kiai_head);
    if (!result)
        return result;
    options.label = "Cut selection";
    const auto deleted = delete_selection_all(history, state, options);
    if (!deleted)
        return std::unexpected(deleted.diagnostics.empty()
                                   ? std::string{"The selection could not be cut."}
                                   : deleted.diagnostics.front().message);
    return result;
}

namespace {
auto exact_rows(rational_t value, std::uint64_t resolution) -> std::optional<std::uint64_t> {
    if (!value.denominator)
        return std::nullopt;
    const auto reduced = rows_to_fraction(value.numerator, value.denominator);
    if (resolution % reduced.denominator)
        return std::nullopt;
    const auto scale = resolution / reduced.denominator;
    if (reduced.numerator > std::numeric_limits<std::uint64_t>::max() / scale)
        return std::nullopt;
    return reduced.numerator * scale;
}

auto paste_position(const measure_index_t& index, std::uint64_t row)
    -> std::optional<local_position_t> {
    if (row >= index.total_rows) return std::nullopt;
    const auto next = std::ranges::upper_bound(index.entries, row, {},
                                               &measure_index_entry_t::absolute_begin_row);
    if (next == index.entries.begin()) return std::nullopt;
    const auto& measure = *std::prev(next);
    return local_position_t{measure.id, row - measure.absolute_begin_row};
}

auto place_note(note_t note, const chart_clipboard_object_t& object,
                const chart_document_t& chart, const measure_index_t& index,
                local_position_t position, std::uint64_t row) -> std::optional<note_t> {
    const auto* layout = layout_at(chart, index, position);
    const auto duration = exact_rows(object.duration, chart.divisions_per_standard_measure);
    if (!layout || object.lane_index >= layout->lanes.size() || !duration)
        return std::nullopt;
    note.position = position;
    note.lane = layout->lanes[object.lane_index].id;
    if (note.end) {
        if (*duration > std::numeric_limits<std::uint64_t>::max() - row) return std::nullopt;
        note.end = paste_position(index, row + *duration);
        if (!note.end || !*duration) return std::nullopt;
    } else if (*duration) {
        return std::nullopt;
    }
    return note;
}

auto place_bgm(bgm_event_t event, const chart_clipboard_object_t& object,
               const chart_document_t& chart, const measure_index_t& index,
               local_position_t position, std::uint64_t row) -> std::optional<bgm_event_t> {
    const auto duration = exact_rows(object.duration, chart.divisions_per_standard_measure);
    if (object.lane_index >= chart.bgm_source_lanes.size() || !duration) return std::nullopt;
    event.position = position;
    event.source_lane = chart.bgm_source_lanes[object.lane_index].id;
    if (event.end) {
        if (*duration > std::numeric_limits<std::uint64_t>::max() - row) return std::nullopt;
        event.end = paste_position(index, row + *duration);
        if (!event.end || !*duration) return std::nullopt;
    } else if (*duration) {
        return std::nullopt;
    }
    return event;
}

auto place_timing_object(const chart_clipboard_object_t& object, const chart_document_t& chart,
                         const measure_index_t& index, local_position_t position,
                         std::uint64_t row) -> std::optional<chart_clipboard_payload_t> {
    const auto duration = exact_rows(object.duration, chart.divisions_per_standard_measure);
    if (!duration) return std::nullopt;
    auto payload = object.payload;
    if (auto* warp = std::get_if<warp_event_t>(&payload)) {
        if (!*duration || *duration > std::numeric_limits<std::uint64_t>::max() - row)
            return std::nullopt;
        const auto end = paste_position(index, row + *duration);
        if (!end) return std::nullopt;
        warp->span = {position, *end};
        return payload;
    }
    if (*duration) return std::nullopt;
    std::visit([&position](auto& value) {
        if constexpr (requires { value.position; }) value.position = position;
    }, payload);
    return payload;
}

auto place_object(const chart_clipboard_object_t& object, const chart_document_t& chart,
                  const measure_index_t& index, std::uint64_t anchor)
    -> std::optional<chart_clipboard_payload_t> {
    const auto offset = exact_rows(object.offset, chart.divisions_per_standard_measure);
    if (!offset || *offset > std::numeric_limits<std::uint64_t>::max() - anchor)
        return std::nullopt;
    const auto row = anchor + *offset;
    const auto position = paste_position(index, row);
    if (!position) return std::nullopt;
    if (const auto* note = std::get_if<note_t>(&object.payload)) {
        const auto placed = place_note(*note, object, chart, index, *position, row);
        if (!placed) return std::nullopt;
        return *placed;
    }
    if (const auto* event = std::get_if<bgm_event_t>(&object.payload)) {
        const auto placed = place_bgm(*event, object, chart, index, *position, row);
        if (!placed) return std::nullopt;
        return *placed;
    }
    return place_timing_object(object, chart, index, *position, row);
}

template <typename Payload>
auto append_interval(std::vector<lane_occupant_t>& intervals, const Payload& payload,
                     const measure_index_t& index, bool proposed) -> bool {
    // Timing objects are checked by whole-document validation, not by lane intervals.
    if constexpr (!std::is_same_v<Payload, note_t> && !std::is_same_v<Payload, bgm_event_t>) {
        return true;
    } else {
    const auto row = index.row_of(payload.position);
    if (!row) return false;
    lane_occupant_t interval{.begin = *row, .end = *row, .proposed = proposed};
    if constexpr (std::is_same_v<Payload, note_t>) {
        interval.lane = lane_occupancy_key_t::from(payload.lane);
        if (payload.end) {
            const auto end = index.row_of(*payload.end);
            if (!end || *end < *row) return false;
            interval.end = *end;
        }
    } else {
        interval.lane = lane_occupancy_key_t::from(payload.source_lane);
    }
    intervals.push_back(interval);
    return true;
    }
}

auto collect_existing_intervals(std::vector<lane_occupant_t>& intervals,
    const chart_document_t& chart, const measure_index_t& index) -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            if (!append_interval(intervals, note, index, false)) return false;
        for (const auto& event : measure.bgm_events)
            if (!append_interval(intervals, event, index, false)) return false;
    }
    return true;
}


auto has_paste_collision(const chart_document_t& chart, const chart_paste_proposal_t& proposal,
                   const measure_index_t& index) -> bool {
    std::vector<lane_occupant_t> intervals;
    if (!collect_existing_intervals(intervals, chart, index)) return true;
    for (const auto& object : proposal.report.objects) {
        const auto appended = std::visit([&](const auto& payload) {
            return append_interval(intervals, payload, index, true);
        }, object);
        if (!appended) return true;
    }
    const auto occupancy = lane_occupancy_t::from(std::move(intervals), lane_occupancy_order_t::paste);
    return occupancy.first_collision(lane_occupancy_pairs_t::proposed) != nullptr;
}

void append_paste_object(chart_document_t& chart, chart_clipboard_payload_t object) {
    std::visit([&chart](auto payload) {
        using payload_t = decltype(payload);
        if constexpr (std::is_same_v<payload_t, note_t> || std::is_same_v<payload_t, bgm_event_t>) {
            const auto measure = chart.find_measure(payload.position.measure);
            if constexpr (std::is_same_v<payload_t, note_t>)
                measure->notes.push_back(std::move(payload));
            else measure->bgm_events.push_back(std::move(payload));
        } else if constexpr (std::is_same_v<payload_t, bpm_event_t>) {
            chart.bpms.push_back(std::move(payload));
        } else if constexpr (std::is_same_v<payload_t, pause_event_t>) {
            chart.pauses.push_back(std::move(payload));
        } else if constexpr (std::is_same_v<payload_t, warp_event_t>) {
            chart.warps.push_back(std::move(payload));
        } else if constexpr (std::is_same_v<payload_t, scroll_event_t>) {
            chart.scrolls.push_back(std::move(payload));
        } else if constexpr (std::is_same_v<payload_t, speed_event_t>) {
            chart.speeds.push_back(std::move(payload));
        } else if constexpr (std::is_same_v<payload_t, kiai_event_t>) {
            chart.kiai.push_back(std::move(payload));
        } else {
            chart.timing_anchors.push_back(std::move(payload));
        }
    }, std::move(object));
}
} // namespace

namespace {
// Gives a pasted stem point its own retained event in the original channel. Returns the reason
// when the target chart has no such channel or the channel already has a pulse at that row.
auto attach_stem(const chart_document_t& chart, const chart_clipboard_object_t& source,
                 chart_clipboard_payload_t& payload, std::uint64_t& id,
                 std::vector<chart_paste_stem_event_t>& events) -> std::optional<std::string> {
    const auto channel = std::ranges::find(chart.sound_channels, source.stem->channel, &sound_channel_t::id);
    if (channel == chart.sound_channels.end())
        return "The target chart has no BMSON sound channel for these stem points.";
    if (id == std::numeric_limits<std::uint64_t>::max()) return "Object IDs are exhausted.";
    sound_channel_event_t event{.id = object_id_t{++id}, .continuation = source.stem->continuation,
                                .playable = source.stem->playable};
    std::optional<std::string> problem;
    std::visit([&](auto& value) {
        using value_t = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<value_t, note_t>) {
            event.position = value.position;
            event.end = value.end;
            event.lane = value.lane;
            value.sound_channel_event = event.id;
        } else if constexpr (std::is_same_v<value_t, bgm_event_t>) {
            event.position = value.position;
            value.sound_channel_event = event.id;
        } else {
            problem = "Only notes and BGM markers can be stem points.";
        }
    }, payload);
    if (problem) return problem;
    const auto taken = [&](const auto& other) { return other.position == event.position; };
    if (std::ranges::any_of(channel->events, taken) ||
        std::ranges::any_of(events, [&](const auto& staged) {
            return staged.channel == channel->id && staged.event.position == event.position;
        }))
        return "Another event in this BMSON sound channel occupies the target row.";
    events.push_back({channel->id, event});
    return std::nullopt;
}

void append_stem_events(chart_document_t& chart, const std::vector<chart_paste_stem_event_t>& events) {
    for (const auto& staged : events) {
        const auto channel = std::ranges::find(chart.sound_channels, staged.channel, &sound_channel_t::id);
        if (channel != chart.sound_channels.end()) channel->events.push_back(staged.event);
    }
}
} // namespace

namespace {

// Whether the stem's existing audio is still sounding at `row`; nullopt when the audio length or
// the timing is unknown, in which case pasted points are left as copied.
auto stem_audio_playing(const song_document_t& document, const chart_document_t& chart,
                        const sound_channel_t& channel, const timing_index_t& timing, std::uint64_t row)
    -> std::optional<bool> {
    const auto asset = document.find_audio_asset(channel.source_clip.source);
    if (asset == nullptr || asset->stream.sample_rate == 0) return std::nullopt;
    auto source_end = channel.source_clip.end_frame;
    if (asset->stream.frame_count && (!source_end || *asset->stream.frame_count < *source_end))
        source_end = asset->stream.frame_count;
    if (!source_end) return std::nullopt;
    const auto derived = derive_sound_channel_slices(chart, channel, asset->stream.sample_rate, source_end);
    if (!derived) return std::nullopt;
    const auto after = std::ranges::upper_bound(derived.slices, row, {}, &derived_sound_slice_t::absolute_row);
    if (after == derived.slices.begin()) return false;
    const auto& slice = *std::prev(after);
    if (slice.source_begin_frame >= *source_end) return false;
    const auto now = timing.seconds_at(row).seconds;
    const auto then = timing.seconds_at(slice.absolute_row).seconds;
    if (!now || !then) return std::nullopt;
    const auto elapsed = (to_long_double(*now) -
                          to_long_double(*then));
    const auto position = static_cast<long double>(slice.source_begin_frame) +
                          elapsed * static_cast<long double>(asset->stream.sample_rate);
    return position < static_cast<long double>(*source_end);
}

// Pasted stem points inside the ongoing audio simply continue it. Pasted where the stem is silent
// (ended, or not started) the first point must restart the sample, so it becomes a reset; a run
// that straddles an existing point of the channel would half-overlap that audio, and is rejected.
auto apply_stem_run_rules(const song_document_t& document, const chart_document_t& chart,
                          const measure_index_t& index, std::vector<chart_paste_stem_event_t>& events)
    -> std::optional<std::string> {
    const auto timing = timing_index_t::from(chart);
    if (!timing.value) return std::nullopt;
    std::vector<object_id_t> done;
    for (const auto& lead : events) {
        if (std::ranges::find(done, lead.channel) != done.end()) continue;
        done.push_back(lead.channel);
        const auto channel = std::ranges::find(chart.sound_channels, lead.channel, &sound_channel_t::id);
        if (channel == chart.sound_channels.end()) continue;
        std::vector<chart_paste_stem_event_t*> run;
        for (auto& event : events)
            if (event.channel == lead.channel) run.push_back(&event);
        const auto row_of = [&](const chart_paste_stem_event_t* event) {
            return index.row_of(event->event.position).value_or(0);
        };
        std::ranges::sort(run, {}, row_of);
        const auto first = row_of(run.front());
        const auto last = row_of(run.back());
        const auto playing = stem_audio_playing(document, chart, *channel, *timing.value, first);
        if (!playing || *playing) continue;
        const auto overlaps = std::ranges::any_of(channel->events, [&](const auto& existing) {
            const auto row = index.row_of(existing.position);
            return row && *row > first && *row <= last;
        });
        if (overlaps)
            return "These stem points would overlap the part of the stem that already plays.";
        run.front()->event.continuation = false;
    }
    return std::nullopt;
}

} // namespace

namespace {
auto missing_paste_lanes(const chart_clipboard_t& clipboard, const chart_document_t& chart,
                        const measure_index_t& index, std::uint64_t anchor) -> std::size_t {
    std::size_t missing{};
    for (const auto& object : clipboard.objects) {
        if (std::holds_alternative<bgm_event_t>(object.payload)) {
            missing += object.lane_index >= chart.bgm_source_lanes.size();
            continue;
        }
        if (!std::holds_alternative<note_t>(object.payload)) continue;
        const auto offset = exact_rows(object.offset, chart.divisions_per_standard_measure);
        if (!offset || *offset > std::numeric_limits<std::uint64_t>::max() - anchor) continue;
        const auto position = paste_position(index, anchor + *offset);
        if (!position) continue;
        const auto* layout = layout_at(chart, index, *position);
        missing += !layout || object.lane_index >= layout->lanes.size();
    }
    return missing;
}
}

namespace {
// Splits the target chart at every pasted bar line, keeping each object's row, and returns the
// index the pasted objects are placed against.
auto add_paste_bar_lines(chart_document_t& chart, const chart_clipboard_t& clipboard, std::uint64_t anchor,
                         chart_paste_report_t& report) -> result_t<measure_index_t, std::string> {
    for (const auto& offset : clipboard.bar_lines) {
        const auto rows = exact_rows(offset, chart.divisions_per_standard_measure);
        if (!rows || *rows > std::numeric_limits<std::uint64_t>::max() - anchor)
            return std::unexpected(std::string{"A clipboard bar line cannot be represented exactly."});
        report.bar_line_rows.push_back(anchor + *rows);
    }
    if (auto diagnostics = add_bar_lines(chart, report.bar_line_rows); !diagnostics.empty())
        return std::unexpected(std::move(diagnostics.front().message));
    auto index = measure_index_t::from(chart);
    if (!index) return std::unexpected(std::string{"The chart positions are invalid."});
    return std::move(*index);
}

auto place_paste_objects(chart_document_t& chart, const measure_index_t& index,
    const chart_clipboard_t& clipboard, std::uint64_t row, chart_paste_proposal_t& proposal)
    -> result_t<void, std::string> {
    auto id = id_ceiling_t<object_id_t>::from(chart).maximum;
    for (const auto& source : clipboard.objects) {
        auto object = place_object(source, chart, index, row);
        if (!object) return std::unexpected("A clipboard position, duration or lane cannot be represented exactly.");
        if (id == std::numeric_limits<std::uint64_t>::max()) return std::unexpected("Object IDs are exhausted.");
        std::visit([&id](auto& payload) { payload.id = object_id_t{++id}; }, *object);
        if (source.stem) {
            if (auto problem = attach_stem(chart, source, *object, id, proposal.stem_events))
                return std::unexpected(std::move(*problem));
        }
        proposal.report.objects.push_back(*object);
    }
    return {};
}

} // namespace

auto chart_paste_proposal_t::from(const proposal_read_scope_t<chart_stamp_t>& scope,
    const chart_clipboard_t& clipboard, local_position_t anchor)
    -> result_t<chart_paste_proposal_t, chart_paste_diagnostic_t> {
    const auto& document = scope.source_document();
    const auto chart_id = scope.captured_stamp().chart;
    const auto stamp = chart_stamp_t::from(document, chart_id);
    chart_paste_proposal_t result;
    if (document.id != clipboard.source_song) return std::unexpected(chart_paste_diagnostic_t::from("Clipboard resources belong to another project.", stamp, std::move(result.report)));
    const auto count = clipboard.objects.size() + clipboard.bar_lines.size();
    if (count == 0 || count > maximum_objects)
        return std::unexpected(chart_paste_diagnostic_t::from("The clipboard is empty or exceeds its object limit.", stamp, std::move(result.report)));
    auto proposed = document;
    const auto chart = proposed.find_chart(chart_id);
    if (chart == nullptr) return std::unexpected(chart_paste_diagnostic_t::from("The selected chart no longer exists.", stamp, std::move(result.report)));
    const auto original = measure_index_t::from(*chart);
    const auto row = original ? original->row_of(anchor) : std::nullopt;
    if (!row || !chart->divisions_per_standard_measure) return std::unexpected(chart_paste_diagnostic_t::from("The paste anchor is invalid.", stamp, std::move(result.report)));
    auto indexed = add_paste_bar_lines(*chart, clipboard, *row, result.report);
    if (!indexed)
        return std::unexpected(chart_paste_diagnostic_t::from(std::move(indexed.error()), stamp, std::move(result.report)));
    result.report.missing_lane_count = missing_paste_lanes(clipboard, *chart, *indexed, *row);
    if (result.report.missing_lane_count)
        return std::unexpected(chart_paste_diagnostic_t::from(std::to_string(result.report.missing_lane_count) + " clipboard objects have unavailable target lanes.", stamp, std::move(result.report)));
    auto placed = place_paste_objects(*chart, *indexed, clipboard, *row, result);
    if (!placed)
        return std::unexpected(chart_paste_diagnostic_t::from(std::move(placed.error()), stamp,
            std::move(result.report)));
    if (has_paste_collision(*chart, result, *indexed)) {
        return std::unexpected(chart_paste_diagnostic_t::from("Paste collides with existing chart objects.", stamp, std::move(result.report)));
    }
    if (const auto problem = apply_stem_run_rules(document, *chart, *indexed, result.stem_events))
        return std::unexpected(chart_paste_diagnostic_t::from(*problem, stamp, std::move(result.report)));
    for (const auto& object : result.report.objects) append_paste_object(*chart, object);
    append_stem_events(*chart, result.stem_events);
    const auto diagnostics = validate_document(proposed);
    const auto error = std::ranges::find(diagnostics, diagnostic_severity_t::error, &diagnostic_t::severity);
    if (error != diagnostics.end()) return std::unexpected(chart_paste_diagnostic_t::from(error->message, stamp, std::move(result.report)));
    return result;
}

auto chart_paste_proposal_t::propose(const song_document_t& document, chart_id_t chart,
    const chart_clipboard_t& clipboard, local_position_t anchor, std::uint64_t expected_revision)
    -> chart_paste_outcome_t {
    auto captured = chart_stamp_t::from(document, chart);
    captured.document.revision = expected_revision;
    auto valid = revision_policy.validate(captured.document, document, false);
    if (!valid) {
        chart_paste_outcome_t rejected;
        rejected.diagnostics.push_back(chart_paste_diagnostic_t::from(
            std::move(valid.error().diagnostic.message), captured, {}));
        return rejected;
    }
    const proposal_read_scope_t<chart_stamp_t> scope{document, captured};
    return proposal_t<chart_paste_proposal_t>::from(captured, from(scope, clipboard, anchor));
}

auto chart_paste_proposal_t::revise(const proposal_read_scope_t<chart_stamp_t>& scope,
    const chart_clipboard_t& clipboard, local_position_t anchor) const -> chart_paste_outcome_t {
    const auto captured = chart_stamp_t::from(scope.source_document(), scope.captured_stamp().chart);
    return proposal_t<chart_paste_proposal_t>::from(captured, from(scope, clipboard, anchor));
}

auto chart_paste_proposal_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope) &&
    -> diagnosed_t<std::optional<std::string>> {
    auto& transaction = scope.transaction();
    const auto& stamp = scope.captured_stamp();
    if (stamp.document.project != transaction.current_document().id)
        return {.diagnostics = {error(diagnostic_code_t::collision,
            "Clipboard resources belong to another project.")}};
    if (report.objects.empty() && report.bar_line_rows.empty()) return {};
    if (!transaction.source_chart(stamp.chart))
        return {.diagnostics = {error(diagnostic_code_t::collision, "The selected chart no longer exists.")}};
    auto& proposed = transaction.document();
    const auto chart = proposed.find_chart(stamp.chart);
    // Same revision, same splits: the new measure IDs match the ones the objects name.
    if (auto diagnostics = add_bar_lines(*chart, report.bar_line_rows); !diagnostics.empty())
        return {.diagnostics = std::move(diagnostics)};
    for (auto& object : report.objects) append_paste_object(*chart, std::move(object));
    append_stem_events(*chart, stem_events);
    return {.value = "Paste chart objects"};
}

auto paste_chart_clipboard(document_history_t& history, chart_id_t chart_id,
                          const chart_clipboard_t& clipboard, local_position_t anchor) -> document_command_result_t {
    const auto captured = chart_stamp_t::from(history.document, chart_id);
    const proposal_read_scope_t<chart_stamp_t> scope{history.document, captured};
    auto prepared = chart_paste_proposal_t::from(scope, clipboard, anchor);
    if (!prepared) {
        auto transaction = document_command_transaction_t(history);
        transaction.outcome().diagnostics.push_back(std::move(static_cast<diagnostic_t&>(prepared.error())));
        return transaction.finish_without_commit();
    }
    return apply_proposal(history, proposal_t<chart_paste_proposal_t>::from(captured, std::move(*prepared)));
}
} // namespace wormedit
