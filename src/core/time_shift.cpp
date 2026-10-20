#include <wormedit/core/time_shift.h>
#include "lane_occupancy.h"

#include <wormedit/core/measure_editor.h>

#include <algorithm>
#include <iterator>
#include <optional>
#include <span>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace wormedit {
namespace {


// Converts positions to absolute rows and back, applying one signed delta.
class row_shifter_t {
  public:
    row_shifter_t(const measure_index_t& index, std::int64_t delta)
        : index_(index), delta_(delta), by_measure_(measure_lookup_t::from(index)) {}

    [[nodiscard]] auto row_of(local_position_t position) const -> std::optional<std::uint64_t> {
        return by_measure_.row_of(position);
    }

    [[nodiscard]] auto shifted(local_position_t position) const
        -> std::optional<local_position_t> {
        const auto row = row_of(position);
        if (!row)
            return std::nullopt;
        std::uint64_t target{};
        if (delta_ < 0) {
            const auto magnitude = static_cast<std::uint64_t>(-(delta_ + 1)) + 1U;
            if (magnitude > *row)
                return std::nullopt;
            target = *row - magnitude;
        } else {
            target = *row + static_cast<std::uint64_t>(delta_);
            if (target < *row)
                return std::nullopt;
        }
        return index_.position_at(target);
    }

  private:
    const measure_index_t& index_;
    std::int64_t delta_;
    measure_lookup_t by_measure_;
};

struct shift_context_t {
    chart_document_t& chart;
    const row_shifter_t& shifter;
    time_shift_result_t& result;
};

// Shifts one position in place; rejects when the target leaves the chart.
auto shift_position(shift_context_t& context, local_position_t& position, object_id_t owner)
    -> bool {
    const auto target = context.shifter.shifted(position);
    if (!target) {
        reject(context.result.command, diagnostic_code_t::row_out_of_range,
               "time shift moves an object outside the materialized chart", owner);
        return false;
    }
    position = *target;
    return true;
}

auto shift_note(shift_context_t& context, note_t& note) -> bool {
    if (note.sound_channel_event) {
        reject(context.result.command, diagnostic_code_t::unsupported_feature,
               "time shift cannot move a note linked to a retained sound-channel event", note.id);
        return false;
    }
    if (!shift_position(context, note.position, note.id))
        return false;
    return !note.end || shift_position(context, *note.end, note.id);
}

auto shift_bgm(shift_context_t& context, bgm_event_t& event) -> bool {
    if (event.sound_channel_event) {
        reject(context.result.command, diagnostic_code_t::unsupported_feature,
               "time shift cannot move a BGM event linked to a retained sound-channel event",
               event.id);
        return false;
    }
    return shift_position(context, event.position, event.id);
}

// Moves objects whose chunk no longer matches their position, then restores
// row order inside every chunk that received one.
template <typename member_t>
void rehome(chart_document_t& chart, member_t member) {
    std::unordered_map<std::uint64_t, measure_chunk_t*> chunks;
    for (auto& measure : chart.measures)
        chunks.emplace(measure.id.value, &measure);
    using value_t = typename std::decay_t<decltype(std::declval<measure_chunk_t&>().*member)>::value_type;
    std::vector<std::pair<measure_chunk_t*, value_t>> moved;
    for (auto& measure : chart.measures) {
        auto& values = measure.*member;
        auto misplaced = std::ranges::stable_partition(
            values, [&](const auto& value) { return value.position.measure == measure.id; });
        for (auto& value : misplaced)
            moved.emplace_back(chunks.at(value.position.measure.value), std::move(value));
        values.erase(misplaced.begin(), misplaced.end());
    }
    std::set<measure_chunk_t*> touched;
    for (auto& [chunk, value] : moved) {
        (chunk->*member).push_back(std::move(value));
        touched.insert(chunk);
    }
    for (auto* chunk : touched)
        std::ranges::stable_sort(chunk->*member, {}, [](const auto& value) {
            return value.position.row;
        });
}

auto is_lanes_free_of_overlap(const chart_document_t& chart, const row_shifter_t& rows,
                           time_shift_result_t& result) -> bool {
    std::vector<lane_occupant_t> entries;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto begin = rows.row_of(note.position);
            if (auto entry = lane_occupant_t::from(note, begin, note.end ? rows.row_of(*note.end) : begin))
                entries.push_back(*entry);
        }
    }
    const auto occupancy = lane_occupancy_t::from(std::move(entries), lane_occupancy_order_t::head);
    if (const auto* collision = occupancy.first_collision(lane_occupancy_pairs_t::all)) {
        reject(result.command, diagnostic_code_t::collision,
               "time shift would overlap two notes on one lane", collision->note);
        return false;
    }
    return true;
}

// Validates the common preconditions on the document being edited; null means nothing to do.
auto prepare_shift(song_document_t& document, chart_id_t chart_id, std::int64_t rows,
                   time_shift_result_t& result) -> chart_document_t* {
    if (rows == 0)
        return nullptr;
    auto* chart = document.find_chart(chart_id);
    if (!chart_id || chart == nullptr) {
        reject(result.command, diagnostic_code_t::invalid_id, "time shift refers to an unknown chart");
        return nullptr;
    }
    if (chart->bms_control_tree && chart->bms_control_tree->has_control_flow()) {
        reject(result.command, diagnostic_code_t::unsupported_feature,
               "time shift is not supported on a chart with retained BMS control flow");
        return nullptr;
    }
    return chart;
}

auto build_shift_index(const chart_document_t& chart, time_shift_result_t& result)
    -> std::optional<measure_index_t> {
    auto index = measure_index_t::from(chart);
    if (!index) {
        result.command.diagnostics = std::move(index.error());
        return std::nullopt;
    }
    return std::move(*index);
}

// Puts moved notes and BGM events into the measure that now holds them, then rejects lane overlaps
// measured on the chart's final geometry.
auto settle_shifted_chart(chart_document_t& chart, time_shift_result_t& result) -> bool {
    rehome(chart, &measure_chunk_t::notes);
    rehome(chart, &measure_chunk_t::bgm_events);
    const auto index = build_shift_index(chart, result);
    if (!index)
        return false;
    const row_shifter_t rows(*index, 0);
    return is_lanes_free_of_overlap(chart, rows, result);
}

auto commit_shift(document_history_t& history, song_document_t&& proposed,
                  time_shift_result_t& result, const char* label) -> time_shift_result_t {
    if (result.payload.moved_objects == 0 || !result.command.diagnostics.empty())
        return std::move(result);
    auto transaction = document_command_transaction_t(history);
    transaction.adopt_document(std::move(proposed));
    result.command = transaction.commit(label);
    return std::move(result);
}

auto is_range_shift_supported(const chart_document_t& chart, bool include_timing,
                              time_shift_result_t& result) -> bool {
    if (chart.sound_channels.empty() && (!include_timing || chart.sv_recipes.empty()))
        return true;
    reject(result.command, diagnostic_code_t::unsupported_feature,
           "time shift is not supported with retained sound channels or SV recipes");
    return false;
}

struct selected_objects_t {
    std::vector<note_t*> notes;
    std::vector<bgm_event_t*> bgm;
};

auto collect_selection(chart_document_t& chart, const shift_selection_action_t& action,
                       time_shift_result_t& result) -> std::optional<selected_objects_t> {
    std::unordered_map<std::uint64_t, note_t*> notes;
    std::unordered_map<std::uint64_t, bgm_event_t*> bgm;
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes)
            notes.try_emplace(note.id.value, &note);
        for (auto& event : measure.bgm_events)
            bgm.try_emplace(event.id.value, &event);
    }
    selected_objects_t selected;
    std::unordered_set<std::uint64_t> seen;
    const auto take = [&](const auto& ids, const auto& table, auto& out) {
        for (const auto id : ids) {
            const auto found = table.find(id.value);
            if (!id || found == table.end() || !seen.insert(id.value).second) {
                reject(result.command, diagnostic_code_t::invalid_id,
                       "time shift names an unknown or repeated object", id);
                return false;
            }
            out.push_back(found->second);
        }
        return true;
    };
    if (!take(action.notes, notes, selected.notes) || !take(action.bgm_events, bgm, selected.bgm))
        return std::nullopt;
    return selected;
}

// Notes and BGM events that already moved; a range shift leaves them alone.
struct moved_ids_t {
    std::unordered_set<std::uint64_t> notes;
    std::unordered_set<std::uint64_t> bgm_events;

    [[nodiscard]] static auto from(const shift_selection_action_t& action) -> moved_ids_t {
        moved_ids_t moved;
        for (const auto id : action.already_moved_notes) moved.notes.insert(id.value);
        for (const auto id : action.already_moved_bgm_events) moved.bgm_events.insert(id.value);
        return moved;
    }
};

// Shift every event of a timing family whose position is at or after `cursor_row`.
template <typename events_t, typename member_t>
auto shift_timing_family(shift_context_t& context, events_t& events, member_t position_of,
                         std::uint64_t cursor_row, std::size_t& moved) -> bool {
    for (auto& event : events) {
        auto& position = position_of(event);
        const auto row = context.shifter.row_of(position);
        if (!row || *row < cursor_row)
            continue;
        if (!shift_position(context, position, event.id))
            return false;
        ++moved;
    }
    return true;
}

// Which content a range shift carries from `from_row` onward.
struct shift_range_t {
    std::uint64_t from_row{};
    // Lane layout changes sit on measure heads; a bar-line move carries them with their measure.
    bool include_layouts{true};
    const moved_ids_t* moved{};

    [[nodiscard]] auto has_moved(const note_t& note) const -> bool {
        return moved != nullptr && moved->notes.contains(note.id.value);
    }
    [[nodiscard]] auto has_moved(const bgm_event_t& event) const -> bool {
        return moved != nullptr && moved->bgm_events.contains(event.id.value);
    }
};

template <typename objects_t, typename shift_t>
auto shift_bucketed_after(shift_context_t& context, const shift_range_t& range, objects_t& objects,
                          shift_t shift) -> bool {
    for (auto& object : objects) {
        const auto row = context.shifter.row_of(object.position);
        if (!row || *row < range.from_row || range.has_moved(object))
            continue;
        if (!shift(context, object))
            return false;
        ++context.result.payload.moved_objects;
    }
    return true;
}

auto shift_content_after(shift_context_t& context, const shift_range_t& range) -> bool {
    auto& chart = context.chart;
    for (auto& measure : chart.measures) {
        if (!shift_bucketed_after(context, range, measure.notes, shift_note) ||
            !shift_bucketed_after(context, range, measure.bgm_events, shift_bgm))
            return false;
    }
    auto& moved = context.result.payload.moved_objects;
    const auto position = [](auto& e) -> local_position_t& { return e.position; };
    return shift_timing_family(context, chart.bga_events, position, range.from_row, moved) &&
           (!range.include_layouts ||
            shift_timing_family(context, chart.lane_layouts, position, range.from_row, moved));
}

// A warp moves as a whole when its begin is at or after the cursor.
auto shift_warps_after_cursor(shift_context_t& context, std::uint64_t cursor_row) -> bool {
    for (auto& warp : context.chart.warps) {
        const auto begin = context.shifter.row_of(warp.span.begin);
        if (!begin || *begin < cursor_row)
            continue;
        if (!shift_position(context, warp.span.begin, warp.id) ||
            !shift_position(context, warp.span.end, warp.id))
            return false;
        ++context.result.payload.moved_objects;
    }
    return true;
}

auto shift_timing_after_cursor(shift_context_t& context, std::uint64_t cursor_row) -> bool {
    auto& chart = context.chart;
    auto& moved = context.result.payload.moved_objects;
    const auto position = [](auto& e) -> local_position_t& { return e.position; };
    return shift_timing_family(context, chart.bpms, position, cursor_row, moved) &&
           shift_timing_family(context, chart.pauses, position, cursor_row, moved) &&
           shift_timing_family(context, chart.scrolls, position, cursor_row, moved) &&
           shift_timing_family(context, chart.speeds, position, cursor_row, moved) &&
           shift_timing_family(context, chart.kiai, position, cursor_row, moved) &&
           shift_timing_family(context, chart.timing_anchors, position, cursor_row, moved) &&
           shift_warps_after_cursor(context, cursor_row);
}

auto shift_markers_after_cursor(shift_context_t& context, std::uint64_t cursor_row,
                                const row_shifter_t& shifter, time_shift_result_t& result)
    -> bool {
    for (auto& bookmark : context.chart.bookmarks) {
        if (bookmark.anchor == marker_anchor_t::audio_time || !bookmark.position) {
            ++result.payload.audio_anchored_skipped;
            continue;
        }
        const auto row = shifter.row_of(*bookmark.position);
        if (row && *row >= cursor_row) {
            if (!shift_position(context, *bookmark.position, bookmark.id))
                return false;
            ++result.payload.moved_objects;
        }
    }
    for (auto& section : context.chart.sections) {
        if (section.anchor == marker_anchor_t::audio_time || !section.beat_span) {
            ++result.payload.audio_anchored_skipped;
            continue;
        }
        const auto row = shifter.row_of(section.beat_span->begin);
        if (row && *row >= cursor_row) {
            if (!shift_position(context, section.beat_span->begin, section.id) ||
                !shift_position(context, section.beat_span->end, section.id))
                return false;
            ++result.payload.moved_objects;
        }
    }
    return true;
}

// Everything a range shift carries, measured on the chart's current geometry.
auto shift_range(chart_document_t& chart, std::int64_t rows, const shift_range_t& range,
                 bool include_timing, time_shift_result_t& result) -> bool {
    const auto index = build_shift_index(chart, result);
    if (!index)
        return false;
    const row_shifter_t shifter(*index, rows);
    shift_context_t context{chart, shifter, result};
    return shift_content_after(context, range) &&
           shift_markers_after_cursor(context, range.from_row, shifter, result) &&
           (!include_timing || shift_timing_after_cursor(context, range.from_row));
}

auto shift_selected_objects(chart_document_t& chart, const shift_selection_action_t& action,
                            moved_ids_t& moved, time_shift_result_t& result) -> bool {
    if (action.notes.empty() && action.bgm_events.empty())
        return true;
    const auto index = build_shift_index(chart, result);
    auto selected = index ? collect_selection(chart, action, result) : std::nullopt;
    if (!selected)
        return false;
    const row_shifter_t shifter(*index, action.rows);
    shift_context_t context{chart, shifter, result};
    for (auto* note : selected->notes) {
        if (!shift_note(context, *note))
            return false;
        moved.notes.insert(note->id.value);
        ++result.payload.moved_objects;
    }
    for (auto* event : selected->bgm) {
        if (!shift_bgm(context, *event))
            return false;
        moved.bgm_events.insert(event->id.value);
        ++result.payload.moved_objects;
    }
    return true;
}

// The bar lines a move changes, and where a propagated move starts carrying content.
struct bar_line_geometry_t {
    std::vector<measure_id_t> measures;
    bool move_end{};
    std::optional<std::uint64_t> propagate_from;
};

auto earliest_bar_line(const chart_document_t& chart, std::span<const measure_id_t> bar_lines,
                       time_shift_result_t& result) -> std::optional<std::size_t> {
    std::optional<std::size_t> first;
    for (const auto id : bar_lines) {
        const auto* measure = chart.find_measure(id);
        if (measure == nullptr) {
            reject(result.command, diagnostic_code_t::invalid_id, "bar line refers to an unknown measure");
            return std::nullopt;
        }
        const auto at = static_cast<std::size_t>(measure - chart.measures.data());
        first = std::min(first.value_or(at), at);
    }
    if (!first)
        reject(result.command, diagnostic_code_t::invalid_measure,
               "propagating a shift needs a moved bar line");
    else if (*first == 0)
        reject(result.command, diagnostic_code_t::invalid_measure, "the chart's first bar line is fixed");
    return first && *first != 0 ? first : std::nullopt;
}

auto plan_bar_line_geometry(const chart_document_t& chart, const shift_selection_action_t& action,
                            time_shift_result_t& result) -> std::optional<bar_line_geometry_t> {
    if (action.bar_line_time == bar_line_time_t::keep_song_time)
        return bar_line_geometry_t{.measures = action.bar_lines, .move_end = false, .propagate_from = {}};
    const auto first = earliest_bar_line(chart, action.bar_lines, result);
    const auto index = first ? build_shift_index(chart, result) : std::nullopt;
    if (!index)
        return std::nullopt;
    bar_line_geometry_t geometry{.measures = {}, .move_end = true,
                                 .propagate_from = index->entries[*first].absolute_begin_row};
    for (auto at = *first; at < chart.measures.size(); ++at)
        geometry.measures.push_back(chart.measures[at].id);
    return geometry;
}

auto move_geometry(chart_document_t& chart, const bar_line_geometry_t& geometry, std::int64_t rows,
                   std::size_t named, time_shift_result_t& result) -> bool {
    if (geometry.measures.empty() && !geometry.move_end)
        return true;
    auto diagnostics = move_bar_lines(chart, geometry.measures, rows, geometry.move_end);
    if (!diagnostics.empty()) {
        std::ranges::move(diagnostics, std::back_inserter(result.command.diagnostics));
        return false;
    }
    result.payload.moved_objects += named;
    return true;
}

} // namespace

auto stage_shift_selection(song_document_t& document, const shift_selection_action_t& action,
                           time_shift_limits_t limits) -> time_shift_result_t {
    time_shift_result_t result;
    result.command.revision = document.revision;
    if (action.notes.size() + action.bgm_events.size() + action.bar_lines.size() >
        limits.maximum_selected_objects) {
        reject(result.command, diagnostic_code_t::invalid_resolution,
               "time shift selection exceeds its configured cap");
        return result;
    }
    auto* chart = prepare_shift(document, action.chart, action.rows, result);
    const auto geometry = chart ? plan_bar_line_geometry(*chart, action, result) : std::nullopt;
    if (!geometry || (geometry->propagate_from &&
                      !is_range_shift_supported(*chart, action.include_timing, result)))
        return result;
    auto moved = moved_ids_t::from(action);
    const shift_range_t range{.from_row = geometry->propagate_from.value_or(0),
                              .include_layouts = false, .moved = &moved};
    const auto objects = [&] {
        return shift_selected_objects(*chart, action, moved, result) &&
               (!geometry->propagate_from ||
                shift_range(*chart, action.rows, range, action.include_timing, result));
    };
    const auto bar_lines = [&] {
        return move_geometry(*chart, *geometry, action.rows, action.bar_lines.size(), result);
    };
    // Grow the chart before carrying content into the new rows; shrink it after moving content out.
    const bool moved_all = action.rows > 0 ? bar_lines() && objects() : objects() && bar_lines();
    if (moved_all && result.payload.moved_objects != 0)
        (void)settle_shifted_chart(*chart, result);
    return result;
}

auto apply_shift_selection(document_history_t& history,
                           const shift_selection_action_t& action, time_shift_limits_t limits)
    -> time_shift_result_t {
    auto proposed = history.document;
    auto result = stage_shift_selection(proposed, action, limits);
    return commit_shift(history, std::move(proposed), result, "Shift selection");
}

auto apply_shift_from_cursor(document_history_t& history,
                             const shift_from_cursor_action_t& action, time_shift_limits_t)
    -> time_shift_result_t {
    time_shift_result_t result;
    result.command.revision = history.document.revision;
    auto proposed = history.document;
    auto* chart = prepare_shift(proposed, action.chart, action.rows, result);
    if (!chart || !is_range_shift_supported(*chart, action.include_timing, result))
        return result;
    const auto index = build_shift_index(*chart, result);
    if (!index)
        return result;
    const auto cursor_row = row_shifter_t(*index, 0).row_of(action.cursor);
    if (!cursor_row) {
        reject(result.command, diagnostic_code_t::row_out_of_range, "time shift cursor is not on the chart");
        return result;
    }
    if (!shift_range(*chart, action.rows, {.from_row = *cursor_row}, action.include_timing, result) ||
        !settle_shifted_chart(*chart, result))
        return result;
    return commit_shift(history, std::move(proposed), result, "Shift from cursor");
}

} // namespace wormedit
