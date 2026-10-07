#include <wormedit/core/note_to_bgm.h>

#include <wormedit/core/bmson_sound_channel_editor.h>

#include <algorithm>
#include <limits>
#include <string>
#include <unordered_set>

namespace wormedit {
namespace {

auto rejected(std::uint64_t revision, diagnostic_code_t code, std::string message)
    -> document_command_result_t {
    return {.applied = false, .revision = revision,
            .diagnostics = {{diagnostic_severity_t::error, code, std::move(message), {}}}};
}

struct located_note_t {
    measure_chunk_t* measure{};
    std::vector<note_t>::iterator note;
};

auto find_note(chart_document_t& chart, object_id_t id) -> std::optional<located_note_t> {
    for (auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.notes, id, &note_t::id);
        if (found != measure.notes.end()) return located_note_t{&measure, found};
    }
    return std::nullopt;
}

auto largest_object_id(const chart_document_t& chart) -> std::uint64_t {
    std::uint64_t largest{};
    const auto scan = [&](const auto& values) {
        for (const auto& value : values) largest = std::max(largest, value.id.value);
    };
    scan(chart.bgm_source_lanes); scan(chart.lane_layouts); scan(chart.bpms); scan(chart.pauses);
    scan(chart.warps); scan(chart.scrolls); scan(chart.speeds); scan(chart.kiai);
    scan(chart.timing_anchors); scan(chart.bookmarks); scan(chart.sections); scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) scan(channel.events);
    for (const auto& measure : chart.measures) { scan(measure.notes); scan(measure.bgm_events); }
    return largest;
}

// The lane for a new event at `position`: the preferred one, else the nearest free one to its
// right, then to its left; nullopt when every lane has an event there.
auto free_lane(const chart_document_t& chart, local_position_t position,
               std::optional<object_id_t> preferred) -> std::optional<object_id_t> {
    std::unordered_set<std::uint64_t> taken;
    for (const auto& measure : chart.measures)
        for (const auto& event : measure.bgm_events)
            if (event.position == position) taken.insert(event.source_lane.value);
    std::vector<const bgm_source_lane_t*> lanes;
    for (const auto& lane : chart.bgm_source_lanes) lanes.push_back(&lane);
    std::ranges::stable_sort(lanes, {}, [](const auto* lane) { return lane->source_order; });
    auto start = static_cast<std::size_t>(0);
    if (preferred) {
        const auto found = std::ranges::find_if(lanes, [&](const auto* lane) { return lane->id == *preferred; });
        if (found != lanes.end()) start = static_cast<std::size_t>(found - lanes.begin());
    }
    for (auto at = start; at < lanes.size(); ++at)
        if (!taken.contains(lanes[at]->id.value)) return lanes[at]->id;
    for (auto at = start; at-- > 0;)
        if (!taken.contains(lanes[at]->id.value)) return lanes[at]->id;
    return std::nullopt;
}

auto add_bgm_lane(chart_document_t& chart, std::uint64_t& largest) -> std::optional<object_id_t> {
    if (largest == std::numeric_limits<std::uint64_t>::max() || chart.bgm_source_lanes.size() >= 1'000'000)
        return std::nullopt;
    std::size_t order{};
    for (const auto& lane : chart.bgm_source_lanes) order = std::max(order, lane.source_order + 1);
    const object_id_t id{++largest};
    chart.bgm_source_lanes.push_back({.id = id, .name = "BGM " + std::to_string(chart.bgm_source_lanes.size() + 1),
                                      .source_order = order});
    return id;
}

// An ordinary note becomes a BGM event at its own row, with its sound, layers and branch.
auto convert_note(chart_document_t& chart, object_id_t id, std::optional<object_id_t> preferred,
                  std::uint64_t& largest) -> bool {
    const auto located = find_note(chart, id);
    if (!located) return false;
    auto lane = free_lane(chart, located->note->position, preferred);
    if (!lane) lane = add_bgm_lane(chart, largest);
    if (!lane) return false;
    bgm_event_t event{.id = located->note->id, .position = located->note->position, .source_lane = *lane,
                      .sound = located->note->sound, .sound_channel_event = std::nullopt,
                      .structural_slot = 0, .branch = located->note->branch, .provenance = {}};
    event.semantic_layers = located->note->semantic_layers;
    const auto position = located->note->position;
    auto& measure = *located->measure;
    measure.notes.erase(located->note);
    const auto destination = std::ranges::find(chart.measures, position.measure, &measure_chunk_t::id);
    if (destination == chart.measures.end()) return false;
    destination->bgm_events.push_back(std::move(event));
    return true;
}

} // namespace

auto move_notes_to_bgm(document_history_t& history, std::uint64_t expected_revision, chart_id_t chart_id,
                       std::span<const object_id_t> notes, std::optional<object_id_t> preferred_lane)
    -> document_command_result_t {
    const auto revision = history.document.revision;
    if (expected_revision != revision)
        return rejected(revision, diagnostic_code_t::invalid_id, "note move to BGM has a stale document revision");
    if (notes.empty() || notes.size() > 4096)
        return rejected(revision, diagnostic_code_t::invalid_id, "note move to BGM needs 1 to 4096 notes");
    const auto found = std::ranges::find(history.document.charts, chart_id, &chart_document_t::id);
    if (!chart_id || found == history.document.charts.end())
        return rejected(revision, diagnostic_code_t::invalid_id, "note move to BGM refers to an unknown chart");
    // Work on a copy of the whole history so a failure leaves the real one untouched, and fold
    // the steps into one undo entry at the end.
    auto scratch = make_document_history(history.document);
    for (const auto id : notes) {
        auto& chart = *std::ranges::find(scratch.document.charts, chart_id, &chart_document_t::id);
        const auto located = find_note(chart, id);
        if (!located)
            return rejected(revision, diagnostic_code_t::invalid_id, "note move to BGM names an unknown note");
        if (located->note->sound_channel_event && chart.source_family == format_family_t::bmson) {
            const auto step = assign_bmson_sound_event(scratch, scratch.document.revision, chart_id, id, std::nullopt);
            if (!step.diagnostics.empty()) return step;
            continue;
        }
        auto largest = largest_object_id(chart);
        auto proposed = scratch.document;
        auto& proposed_chart = *std::ranges::find(proposed.charts, chart_id, &chart_document_t::id);
        if (!convert_note(proposed_chart, id, preferred_lane, largest))
            return rejected(revision, diagnostic_code_t::invalid_id, "a note could not be moved to the BGM area");
        const auto step = commit_document_command(scratch, std::move(proposed), "Move note to BGM");
        if (!step.applied) return step.diagnostics.empty() ? rejected(revision, diagnostic_code_t::invalid_id,
            "a note could not be moved to the BGM area") : step;
    }
    return commit_document_command(history, std::move(scratch.document),
                                   notes.size() == 1 ? "Move note to BGM" : "Move notes to BGM");
}

namespace {

struct located_bgm_t {
    measure_chunk_t* measure{};
    std::vector<bgm_event_t>::iterator event;
};

auto find_bgm(chart_document_t& chart, object_id_t id) -> std::optional<located_bgm_t> {
    for (auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.bgm_events, id, &bgm_event_t::id);
        if (found != measure.bgm_events.end()) return located_bgm_t{&measure, found};
    }
    return std::nullopt;
}

// An ordinary BGM event becomes a tap at its own row; false when the lane already has a note there.
auto convert_bgm_event(chart_document_t& chart, object_id_t id, lane_id_t lane) -> bool {
    const auto located = find_bgm(chart, id);
    if (!located) return false;
    const auto position = located->event->position;
    const auto occupied = std::ranges::any_of(chart.measures, [&](const auto& measure) {
        return std::ranges::any_of(measure.notes, [&](const auto& note) {
            return note.lane == lane && note.position == position;
        });
    });
    if (occupied) return false;
    note_t note{.id = located->event->id, .position = position, .lane = lane, .kind = note_kind_t::tap,
                .end = std::nullopt, .sound = located->event->sound};
    note.semantic_layers = located->event->semantic_layers;
    note.branch = located->event->branch;
    located->measure->bgm_events.erase(located->event);
    const auto destination = std::ranges::find(chart.measures, position.measure, &measure_chunk_t::id);
    if (destination == chart.measures.end()) return false;
    destination->notes.push_back(std::move(note));
    return true;
}

} // namespace

auto move_bgm_events_to_lane(document_history_t& history, std::uint64_t expected_revision, chart_id_t chart_id,
                             std::span<const object_id_t> events, lane_id_t lane) -> document_command_result_t {
    const auto revision = history.document.revision;
    if (expected_revision != revision)
        return rejected(revision, diagnostic_code_t::invalid_id, "BGM move has a stale document revision");
    if (events.empty() || events.size() > 4096 || !lane)
        return rejected(revision, diagnostic_code_t::invalid_id, "BGM move needs 1 to 4096 events and a lane");
    if (std::ranges::find(history.document.charts, chart_id, &chart_document_t::id) == history.document.charts.end())
        return rejected(revision, diagnostic_code_t::invalid_id, "BGM move refers to an unknown chart");
    auto scratch = make_document_history(history.document);
    for (const auto id : events) {
        auto& chart = *std::ranges::find(scratch.document.charts, chart_id, &chart_document_t::id);
        const auto located = find_bgm(chart, id);
        if (!located)
            return rejected(revision, diagnostic_code_t::invalid_id, "BGM move names an unknown event");
        if (located->event->sound_channel_event && chart.source_family == format_family_t::bmson) {
            const auto step = assign_bmson_sound_event(scratch, scratch.document.revision, chart_id, id, lane);
            if (!step.applied)
                return step.diagnostics.empty()
                    ? rejected(revision, diagnostic_code_t::invalid_id, "the event could not move to that lane")
                    : step;
            continue;
        }
        auto proposed = scratch.document;
        auto& proposed_chart = *std::ranges::find(proposed.charts, chart_id, &chart_document_t::id);
        if (!convert_bgm_event(proposed_chart, id, lane))
            return rejected(revision, diagnostic_code_t::collision, "that lane already has a note at the row");
        const auto step = commit_document_command(scratch, std::move(proposed), "Move BGM event to lane");
        if (!step.applied) return step.diagnostics.empty() ? rejected(revision, diagnostic_code_t::invalid_id,
            "the event could not move to that lane") : step;
    }
    return commit_document_command(history, std::move(scratch.document),
                                   events.size() == 1 ? "Move BGM event to lane" : "Move BGM events to lane");
}

auto move_selected_objects_to_lane(document_history_t& history, std::uint64_t expected_revision,
                                   const editor_interaction_state_t& state,
                                   std::span<const object_id_t> notes,
                                   std::span<const object_id_t> events, lane_id_t lane)
    -> document_command_result_t {
    const auto revision = history.document.revision;
    if (expected_revision != revision)
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "lane assignment has a stale document revision");
    if ((!notes.empty() && notes.size() > 4096) || (!events.empty() && events.size() > 4096) ||
        notes.size() + events.size() > 4096 || !lane)
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "lane assignment needs 1 to 4096 selected objects and a lane");
    if (notes.empty() && events.empty())
        return rejected(revision, diagnostic_code_t::invalid_id,
                        "lane assignment needs at least one selected object");
    auto scratch = make_document_history(history.document);
    if (!notes.empty()) {
        auto scratch_state = state;
        scratch_state.chart = state.chart;
        scratch_state.selected_notes.assign(notes.begin(), notes.end());
        std::erase_if(scratch_state.selected_objects, [](const auto& object) {
            return object.kind == editor_selection_object_kind_t::note;
        });
        const auto assigned = dispatch_editor_action(
            scratch, scratch_state,
            assign_selected_notes_to_lane_action_t{lane, false});
        if (!assigned.accepted)
            return {.applied = false, .revision = revision,
                    .diagnostics = std::move(assigned.diagnostics)};
    }
    if (!events.empty()) {
        auto moved = move_bgm_events_to_lane(
            scratch, scratch.document.revision, state.chart, events, lane);
        if (!moved.applied) {
            moved.revision = revision;
            return moved;
        }
    }
    return commit_document_command(history, std::move(scratch.document),
                                   "Assign selected objects to lane");
}

} // namespace wormedit
