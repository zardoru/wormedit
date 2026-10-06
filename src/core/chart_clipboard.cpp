#include <wormedit/core/chart_clipboard.h>

#include <algorithm>
#include <numeric>
#include <set>
#include <limits>
#include <tuple>

namespace wormedit {
namespace {
constexpr std::size_t maximum_objects = 32768;

auto fraction(std::uint64_t rows, std::uint64_t resolution) -> rational_t {
    const auto divisor = std::gcd(rows, resolution);
    return {rows / divisor, resolution / divisor};
}

auto layout_at(const chart_document_t& chart, const measure_index_t& index,
               local_position_t position) -> const lane_layout_t* {
    const auto row = absolute_row(index, position);
    const lane_layout_t* layout{};
    std::uint64_t latest{};
    for (const auto& segment : chart.lane_layouts) {
        const auto start = absolute_row(index, segment.position);
        if (row && start && *start <= *row && (!layout || *start >= latest)) {
            layout = &segment.layout;
            latest = *start;
        }
    }
    return layout;
}

auto note_lane(const chart_document_t& chart, const measure_index_t& index,
               const note_t& note) -> std::optional<std::size_t> {
    const auto* layout = layout_at(chart, index, note.position);
    if (!layout) return std::nullopt;
    const auto found = std::ranges::find(layout->lanes, note.lane, &lane_definition_t::id);
    if (found == layout->lanes.end()) return std::nullopt;
    return static_cast<std::size_t>(found - layout->lanes.begin());
}

auto selected_objects(const editor_interaction_state_t& state)
    -> std::set<editor_selection_object_t> {
    std::set<editor_selection_object_t> selected(state.selected_objects.begin(),
                                               state.selected_objects.end());
    for (const auto id : state.selected_notes)
        selected.insert({editor_selection_object_kind_t::note, id});
    return selected;
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
    clipboard.objects.push_back({fraction(*row, chart.divisions_per_standard_measure),
        fraction(*end - *row, chart.divisions_per_standard_measure), *lane, note,
        stem_of(chart, note.sound_channel_event)});
    return true;
}

auto append_bgm(chart_clipboard_t& clipboard, const chart_document_t& chart,
                const measure_index_t& index, const bgm_event_t& event) -> bool {
    const auto row = absolute_row(index, event.position);
    const auto lane = std::ranges::find(chart.bgm_source_lanes, event.source_lane,
                                       &bgm_source_lane_t::id);
    if (!row || lane == chart.bgm_source_lanes.end()) return false;
    clipboard.objects.push_back({fraction(*row, chart.divisions_per_standard_measure), {0, 1},
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
    clipboard.objects.push_back({fraction(*row, chart.divisions_per_standard_measure),
        fraction(*end - *row, chart.divisions_per_standard_measure), 0, event});
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
                          std::uint64_t expected_revision) -> chart_clipboard_result_t {
    if (document.revision != expected_revision) return {{}, "The document has changed."};
    const auto chart = std::ranges::find(document.charts, state.chart, &chart_document_t::id);
    if (chart == document.charts.end()) return {{}, "The selected chart no longer exists."};
    auto selected = selected_objects(state);
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
    for (auto& object : clipboard.objects) object.offset = fraction(row_of(object) - anchor, resolution);
    return {std::move(clipboard), {}};
}

auto cut_chart_selection(document_history_t& history, editor_interaction_state_t& state,
                         std::uint64_t expected_revision) -> chart_clipboard_result_t {
    auto result = copy_chart_selection(history.document, state, expected_revision);
    if (!result) return result;
    auto proposed_state = state;
    const auto selected = selected_objects(state);
    proposed_state.selected_objects.assign(selected.begin(), selected.end());
    const auto deleted = dispatch_editor_action(history, proposed_state,
                                                delete_editor_selection_action_t{});
    if (!deleted) return {{}, "The selection could not be cut."};
    state = std::move(proposed_state);
    return result;
}

namespace {
auto exact_rows(rational_t value, std::uint64_t resolution) -> std::optional<std::uint64_t> {
    if (!value.denominator) return std::nullopt;
    const auto reduced = fraction(value.numerator, value.denominator);
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
    if (!std::holds_alternative<bgm_event_t>(object.payload))
        return place_timing_object(object, chart, index, *position, row);
    if (object.lane_index >= chart.bgm_source_lanes.size()) return std::nullopt;
    if (!object.duration.denominator || object.duration.numerator) return std::nullopt;
    auto event = std::get<bgm_event_t>(object.payload);
    event.position = *position;
    event.source_lane = chart.bgm_source_lanes[object.lane_index].id;
    return event;
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
            const auto measure = std::ranges::find(chart.measures, payload.position.measure,
                                                   &measure_chunk_t::id);
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
    const auto chart = std::ranges::find(proposed.charts, chart_id, &chart_document_t::id);
    if (chart == proposed.charts.end()) return reject("The selected chart no longer exists.");
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
    if (!proposal.valid) return {.revision = history.document.revision, .diagnostics = {
        {diagnostic_severity_t::error, diagnostic_code_t::collision, std::move(proposal.error), {}}}};
    auto proposed = history.document;
    const auto chart = std::ranges::find(proposed.charts, chart_id, &chart_document_t::id);
    for (auto& object : proposal.objects) append_paste_object(*chart, std::move(object));
    append_stem_events(*chart, proposal.stem_events);
    return commit_document_command(history, std::move(proposed), "Paste chart objects");
}
} // namespace wormedit
