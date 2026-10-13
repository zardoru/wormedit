#include <wormedit/core/rational.h>
#include <wormedit/core/chart_clipboard.h>

#include <wormedit/core/sound_slice.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/kiai_spans.h>
#include <wormedit/core/marker_editor.h>
#include <wormedit/core/sv_recipe.h>
#include <wormedit/core/timing_lane_controller.h>

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
    const auto indexed = build_measure_index(*chart);
    const auto* index = std::get_if<measure_index_t>(&indexed);
    if (!index) return std::nullopt;
    for (const auto& span : kiai_spans_of(*chart, *index)) {
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
    const auto row = absolute_row(index, note.position);
    const auto lane = note_lane(chart, index, note);
    const auto end = note.end ? absolute_row(index, *note.end) : row;
    if (!row || !lane || !end || *end < *row) return false;
    clipboard.objects.push_back({rows_to_fraction(*row, chart.divisions_per_standard_measure),
        rows_to_fraction(*end - *row, chart.divisions_per_standard_measure), *lane, note,
        stem_of(chart, note.sound_channel_event)});
    return true;
}

auto append_bgm(chart_clipboard_t& clipboard, const chart_document_t& chart,
                const measure_index_t& index, const bgm_event_t& event) -> bool {
    const auto row = absolute_row(index, event.position);
    const auto lane = std::ranges::find(chart.bgm_source_lanes, event.source_lane,
                                       &bgm_source_lane_t::id);
    const auto end = event.end ? absolute_row(index, *event.end) : row;
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
        row = absolute_row(index, event.span.begin);
        end = absolute_row(index, event.span.end);
    } else {
        row = absolute_row(index, event.position);
        end = row;
    }
    if (!row || !end || *end < *row) return false;
    clipboard.objects.push_back({rows_to_fraction(*row, chart.divisions_per_standard_measure),
        rows_to_fraction(*end - *row, chart.divisions_per_standard_measure), 0, event});
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
    return collect_timing(clipboard, chart, index, selected) && selected.empty();
}
} // namespace

auto copy_chart_selection(const song_document_t& document,
                          const editor_interaction_state_t& state,
                          std::uint64_t expected_revision, object_id_t kiai_head) -> chart_clipboard_result_t {
    if (document.revision != expected_revision) return {{}, "The document has changed."};
    const auto chart = document.find_chart(state.chart);
    if (chart == nullptr) return {{}, "The selected chart no longer exists."};
    auto selected = selected_objects(state);
    const auto boundaries = selected_kiai_boundaries(document, state, kiai_head);
    if (!boundaries) return {{}, "The selected Kiai area no longer exists."};
    selected.insert(boundaries->begin(), boundaries->end());
    if (selected.empty()) return {{}, "Select notes, BGM events or timing objects to copy."};
    if (selected.size() > maximum_objects) return {{}, "The clipboard object limit was exceeded."};
    const auto indexed = build_measure_index(*chart);
    const auto* index = std::get_if<measure_index_t>(&indexed);
    if (!index || !chart->divisions_per_standard_measure)
        return {{}, "The chart positions are invalid."};
    chart_clipboard_t clipboard{document.id, chart->id, {}};
    if (!collect(clipboard, *chart, *index, selected))
        return {{}, "The selection contains unavailable or unsupported objects."};
    // Every absolute offset originated as an integer row in this resolution.
    const auto resolution = chart->divisions_per_standard_measure;
    const auto row_of = [resolution](const auto& object) {
        return object.offset.numerator * (resolution / object.offset.denominator);
    };
    std::stable_sort(clipboard.objects.begin(), clipboard.objects.end(),
        [&row_of](const auto& left, const auto& right) { return row_of(left) < row_of(right); });
    const auto anchor = row_of(clipboard.objects.front());
    for (auto& object : clipboard.objects) object.offset = rows_to_fraction(row_of(object) - anchor, resolution);
    return {std::move(clipboard), {}};
}

namespace {
auto selection_error(std::string message) -> editor_interaction_result_t {
    return {.diagnostics = {{.severity = diagnostic_severity_t::error,
        .code = diagnostic_code_t::unsupported_feature, .message = std::move(message)}}};
}

auto selection_timing_lane(editor_selection_object_kind_t kind) -> std::optional<timing_lane_kind_t> {
    using selection_t = editor_selection_object_kind_t;
    switch (kind) {
    case selection_t::bpm_event: return timing_lane_kind_t::bpm;
    case selection_t::scroll_event: return timing_lane_kind_t::scroll;
    case selection_t::speed_event: return timing_lane_kind_t::speed;
    case selection_t::stop_event: return timing_lane_kind_t::stop;
    case selection_t::delay_event: return timing_lane_kind_t::delay;
    case selection_t::warp_event: return timing_lane_kind_t::warp;
    case selection_t::kiai_event: return timing_lane_kind_t::kiai;
    case selection_t::timing_anchor: return timing_lane_kind_t::anchor;
    default: return std::nullopt;
    }
}

auto stage_selection_spans(document_history_t& staged, editor_interaction_state_t& state,
                           const selection_delete_options_t& options,
                           const std::vector<editor_selection_object_t>& boundaries) -> diagnostic_list_t {
    if (options.timing_locked && (options.kiai_head || options.recipe))
        return selection_error("Timing is locked.").diagnostics;
    if (options.kiai_head) {
        const auto edited = apply_marker_edit(staged, staged.document.revision,
            edit_kiai_span_action_t{.chart = state.chart, .edit = {.head = options.kiai_head}});
        if (!edited) return edited.diagnostics;
        std::erase_if(state.selected_objects, [&](const auto& object) {
            return std::ranges::find(boundaries, object) != boundaries.end();
        });
    }
    if (options.recipe) {
        const auto edited = apply_sv_recipe_edit(staged, staged.document.revision,
            delete_sv_recipe_action_t{state.chart, options.recipe});
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
    const auto deleted = delete_timing_lane_event_group(staged, state.chart, timing,
                                                       staged.document.revision, timing_locked);
    if (!deleted) return deleted.diagnostics;
    state.selected_objects = std::move(other);
    return {};
}
} // namespace

auto delete_selection_all(document_history_t& history, editor_interaction_state_t& state,
                          std::uint64_t expected_revision, const selection_delete_options_t& options)
    -> editor_interaction_result_t {
    if (history.document.revision != expected_revision) return selection_error("The document has changed.");
    if (has_pending_editor_gesture(state)) return selection_error("Finish or cancel the active gesture first.");
    const auto boundaries = selected_kiai_boundaries(history.document, state, options.kiai_head);
    if (!boundaries) return selection_error("The selected Kiai area no longer exists.");
    auto proposed_state = state;
    const auto selected = selected_objects(state);
    proposed_state.selected_objects.assign(selected.begin(), selected.end());
    proposed_state.selected_notes.clear();
    const bool explicit_selection = !selected.empty() || options.kiai_head || options.recipe;
    auto staged = make_document_history(history.document, 1);
    auto diagnostics = stage_selection_spans(staged, proposed_state, options, *boundaries);
    if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    diagnostics = stage_selection_timing(staged, proposed_state, options.timing_locked);
    if (!diagnostics.empty()) return {.diagnostics = std::move(diagnostics)};
    if (!explicit_selection || !proposed_state.selected_objects.empty()) {
        const auto deleted = dispatch_editor_action(staged, proposed_state, delete_editor_selection_action_t{});
        if (!deleted) return deleted;
    }
    if (staged.undo.empty()) return {.accepted = true};
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(staged.document));
    const auto committed = transaction.commit(options.label);
    if (!committed) return {.diagnostics = committed.diagnostics};
    proposed_state.selected_notes.clear();
    proposed_state.selected_objects.clear();
    state = std::move(proposed_state);
    return {.accepted = true, .document_changed = true, .diagnostics = committed.diagnostics};
}

auto cut_chart_selection(document_history_t& history, editor_interaction_state_t& state,
                         std::uint64_t expected_revision, selection_delete_options_t options)
    -> chart_clipboard_result_t {
    if (options.recipe) return {{}, "SV recipes cannot be copied to the chart clipboard."};
    auto result = copy_chart_selection(history.document, state, expected_revision, options.kiai_head);
    if (!result) return result;
    options.label = "Cut selection";
    const auto deleted = delete_selection_all(history, state, expected_revision, options);
    if (!deleted) return {{}, deleted.diagnostics.empty() ? "The selection could not be cut."
                                                        : deleted.diagnostics.front().message};
    return result;
}

namespace {
auto exact_rows(rational_t value, std::uint64_t resolution) -> std::optional<std::uint64_t> {
    if (!value.denominator) return std::nullopt;
    const auto reduced = rows_to_fraction(value.numerator, value.denominator);
    if (resolution % reduced.denominator) return std::nullopt;
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

auto next_paste_id(const chart_document_t& chart) -> std::uint64_t {
    std::uint64_t highest{};
    const auto scan = [&highest](const auto& values) {
        for (const auto& value : values) highest = std::max(highest, value.id.value);
    };
    scan(chart.bgm_source_lanes);
    scan(chart.lane_layouts);
    scan(chart.bpms);
    scan(chart.pauses);
    scan(chart.warps);
    scan(chart.scrolls);
    scan(chart.speeds);
    scan(chart.kiai);
    scan(chart.timing_anchors);
    scan(chart.bookmarks);
    scan(chart.sections);
    scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) scan(channel.events);
    for (const auto& measure : chart.measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    return highest;
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

struct paste_interval_t {
    bool bgm{};
    std::uint64_t lane{};
    std::uint64_t begin{};
    std::uint64_t end{};
    bool proposed{};
};

template <typename Payload>
auto append_interval(std::vector<paste_interval_t>& intervals, const Payload& payload,
                     const measure_index_t& index, bool proposed) -> bool {
    // Timing objects are checked by whole-document validation, not by lane intervals.
    if constexpr (!std::is_same_v<Payload, note_t> && !std::is_same_v<Payload, bgm_event_t>) {
        return true;
    } else {
    const auto row = absolute_row(index, payload.position);
    if (!row) return false;
    paste_interval_t interval{.begin = *row, .end = *row, .proposed = proposed};
    if constexpr (std::is_same_v<Payload, note_t>) {
        interval.lane = payload.lane.value;
        if (payload.end) {
            const auto end = absolute_row(index, *payload.end);
            if (!end || *end < *row) return false;
            interval.end = *end;
        }
    } else {
        interval.bgm = true;
        interval.lane = payload.source_lane.value;
    }
    intervals.push_back(interval);
    return true;
    }
}

auto collect_existing_intervals(std::vector<paste_interval_t>& intervals,
    const chart_document_t& chart, const measure_index_t& index) -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            if (!append_interval(intervals, note, index, false)) return false;
        for (const auto& event : measure.bgm_events)
            if (!append_interval(intervals, event, index, false)) return false;
    }
    return true;
}

auto overlapping_paste_intervals(std::vector<paste_interval_t> intervals) -> bool {
    std::ranges::sort(intervals, {}, [](const auto& interval) {
        return std::tuple{interval.bgm, interval.lane, interval.begin, interval.proposed};
    });
    std::optional<std::pair<bool, std::uint64_t>> lane;
    std::optional<std::uint64_t> existing_end;
    std::optional<std::uint64_t> proposed_end;
    for (const auto& interval : intervals) {
        const auto next_lane = std::pair{interval.bgm, interval.lane};
        if (lane != next_lane) {
            lane = next_lane;
            existing_end.reset();
            proposed_end.reset();
        }
        if (proposed_end && *proposed_end >= interval.begin) return true;
        if (interval.proposed && existing_end && *existing_end >= interval.begin) return true;
        auto& end = interval.proposed ? proposed_end : existing_end;
        end = std::max(end.value_or(0), interval.end);
    }
    return false;
}

auto paste_collides(const chart_document_t& chart, const chart_paste_proposal_t& proposal,
                   const measure_index_t& index) -> bool {
    std::vector<paste_interval_t> intervals;
    if (!collect_existing_intervals(intervals, chart, index)) return true;
    for (const auto& object : proposal.objects) {
        const auto appended = std::visit([&](const auto& payload) {
            return append_interval(intervals, payload, index, true);
        }, object);
        if (!appended) return true;
    }
    return overlapping_paste_intervals(std::move(intervals));
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
    const auto timing = build_timing_index(chart);
    if (!timing || !timing.index) return std::nullopt;
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
            return absolute_row(index, event->event.position).value_or(0);
        };
        std::ranges::sort(run, {}, row_of);
        const auto first = row_of(run.front());
        const auto last = row_of(run.back());
        const auto playing = stem_audio_playing(document, chart, *channel, *timing.index, first);
        if (!playing || *playing) continue;
        const auto overlaps = std::ranges::any_of(channel->events, [&](const auto& existing) {
            const auto row = absolute_row(index, existing.position);
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

auto propose_chart_paste(const song_document_t& document, chart_id_t chart_id,
                        const chart_clipboard_t& clipboard, local_position_t anchor,
                        std::uint64_t expected_revision) -> chart_paste_proposal_t {
    chart_paste_proposal_t result{.chart = chart_id, .revision = expected_revision};
    const auto reject = [&result](std::string error) {
        result.error = std::move(error);
        return result;
    };
    if (document.revision != expected_revision) return reject("The document has changed.");
    if (document.id != clipboard.source_song) return reject("Clipboard resources belong to another project.");
    if (clipboard.objects.empty() || clipboard.objects.size() > maximum_objects)
        return reject("The clipboard is empty or exceeds its object limit.");
    auto proposed = document;
    const auto chart = proposed.find_chart(chart_id);
    if (chart == nullptr) return reject("The selected chart no longer exists.");
    const auto indexed = build_measure_index(*chart);
    const auto* index = std::get_if<measure_index_t>(&indexed);
    const auto row = index ? absolute_row(*index, anchor) : std::nullopt;
    if (!row || !chart->divisions_per_standard_measure) return reject("The paste anchor is invalid.");
    result.missing_lane_count = missing_paste_lanes(clipboard, *chart, *index, *row);
    if (result.missing_lane_count)
        return reject(std::to_string(result.missing_lane_count) + " clipboard objects have unavailable target lanes.");
    auto id = next_paste_id(*chart);
    for (const auto& source : clipboard.objects) {
        auto object = place_object(source, *chart, *index, *row);
        if (!object) return reject("A clipboard position, duration or lane cannot be represented exactly.");
        if (id == std::numeric_limits<std::uint64_t>::max()) return reject("Object IDs are exhausted.");
        std::visit([&id](auto& payload) { payload.id = object_id_t{++id}; }, *object);
        if (source.stem) {
            if (const auto problem = attach_stem(*chart, source, *object, id, result.stem_events))
                return reject(*problem);
        }
        result.objects.push_back(*object);
    }
    if (paste_collides(*chart, result, *index)) {
        result.error = "Paste collides with existing chart objects.";
        return result;
    }
    if (const auto problem = apply_stem_run_rules(document, *chart, *index, result.stem_events))
        return reject(*problem);
    for (const auto& object : result.objects) append_paste_object(*chart, object);
    append_stem_events(*chart, result.stem_events);
    const auto diagnostics = validate_document(proposed);
    const auto error = std::ranges::find(diagnostics, diagnostic_severity_t::error, &diagnostic_t::severity);
    if (error != diagnostics.end()) return reject(error->message);
    result.valid = true;
    return result;
}

auto paste_chart_clipboard(document_history_t& history, chart_id_t chart_id,
                          const chart_clipboard_t& clipboard, local_position_t anchor,
                          std::uint64_t expected_revision) -> document_command_result_t {
    auto proposal = propose_chart_paste(history.document, chart_id, clipboard, anchor, expected_revision);
    if (!proposal.valid) return document_command_transaction_t::rejection(
        history.document.revision, diagnostic_code_t::collision, std::move(proposal.error));
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    const auto chart = proposed.find_chart(chart_id);
    for (auto& object : proposal.objects) append_paste_object(*chart, std::move(object));
    append_stem_events(*chart, proposal.stem_events);
    return transaction.commit("Paste chart objects");
}
} // namespace wormedit
