#include <wormedit/core/time_shift.h>

#include <algorithm>
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

struct note_interval_t {
    std::uint64_t head{};
    std::uint64_t tail{};
    object_id_t note;
};

auto is_lanes_free_of_overlap(const chart_document_t& chart, const row_shifter_t& rows,
                           time_shift_result_t& result) -> bool {
    std::map<std::uint64_t, std::vector<note_interval_t>> lanes;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto head = rows.row_of(note.position);
            const auto tail = note.end ? rows.row_of(*note.end) : head;
            if (head && tail)
                lanes[note.lane.value].push_back({*head, *tail, note.id});
        }
    }
    for (auto& [lane, intervals] : lanes) {
        std::ranges::sort(intervals, {}, &note_interval_t::head);
        for (std::size_t i = 1; i < intervals.size(); ++i) {
            if (intervals[i].head <= intervals[i - 1].tail) {
                reject(result.command, diagnostic_code_t::collision,
                       "time shift would overlap two notes on one lane", intervals[i].note);
                return false;
            }
        }
    }
    return true;
}

auto has_bms_control_flow(const chart_document_t& chart) -> bool {
    return chart.bms_control_tree.has_value() || !chart.bms_measure_length_projections.empty() ||
           !chart.bms_bpm_projections.empty() || !chart.bms_pause_projections.empty();
}

struct prepared_shift_t {
    song_document_t proposed;
    chart_document_t* chart{};
    measure_index_t index;
};

// Validates the common preconditions and clones the document for editing.
auto prepare_shift(const document_history_t& history,
                   chart_id_t chart_id, std::int64_t rows, time_shift_result_t& result)
    -> std::optional<prepared_shift_t> {
    result.command.revision = history.document.revision;
    if (rows == 0)
        return std::nullopt;
    prepared_shift_t prepared{.proposed = history.document};
    const auto chart =
        prepared.proposed.find_chart(chart_id);
    if (!chart_id || chart == nullptr) {
        reject(result.command, diagnostic_code_t::invalid_id, "time shift refers to an unknown chart");
        return std::nullopt;
    }
    if (has_bms_control_flow(*chart)) {
        reject(result.command, diagnostic_code_t::unsupported_feature,
               "time shift is not supported on a chart with retained BMS control flow");
        return std::nullopt;
    }
    auto index = measure_index_t::from(*chart);
    if (!index) {
        result.command.diagnostics = std::move(index.error());
        return std::nullopt;
    }
    prepared.chart = &*chart;
    prepared.index = std::move(*index);
    return prepared;
}

auto finish_shift(document_history_t& history, prepared_shift_t& prepared,
                  const row_shifter_t& shifter, time_shift_result_t& result, const char* label)
    -> time_shift_result_t {
    if (result.payload.moved_objects == 0)
        return std::move(result);
    rehome(*prepared.chart, &measure_chunk_t::notes);
    rehome(*prepared.chart, &measure_chunk_t::bgm_events);
    if (!is_lanes_free_of_overlap(*prepared.chart, shifter, result))
        return std::move(result);
    auto transaction = document_command_transaction_t(history);
    transaction.adopt_document(std::move(prepared.proposed));
    result.command = transaction.commit(label);
    return std::move(result);
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

} // namespace

auto apply_shift_selection(document_history_t& history,
                           const shift_selection_action_t& action, time_shift_limits_t limits)
    -> time_shift_result_t {
    time_shift_result_t result;
    if (action.notes.size() + action.bgm_events.size() > limits.maximum_selected_objects) {
        reject(result.command, diagnostic_code_t::invalid_resolution,
               "time shift selection exceeds its configured cap");
        return result;
    }
    auto prepared = prepare_shift(history, action.chart, action.rows, result);
    if (!prepared)
        return result;
    auto selected = collect_selection(*prepared->chart, action, result);
    if (!selected)
        return result;
    const row_shifter_t shifter(prepared->index, action.rows);
    shift_context_t context{*prepared->chart, shifter, result};
    for (auto* note : selected->notes) {
        if (!shift_note(context, *note))
            return result;
        ++result.payload.moved_objects;
    }
    for (auto* event : selected->bgm) {
        if (!shift_bgm(context, *event))
            return result;
        ++result.payload.moved_objects;
    }
    return finish_shift(history, *prepared, shifter, result, "Shift selection");
}

namespace {

auto shift_content_after_cursor(shift_context_t& context, std::uint64_t cursor_row) -> bool {
    auto& chart = context.chart;
    auto& moved = context.result.payload.moved_objects;
    for (auto& measure : chart.measures) {
        for (auto& note : measure.notes) {
            const auto row = context.shifter.row_of(note.position);
            if (row && *row >= cursor_row) {
                if (!shift_note(context, note))
                    return false;
                ++moved;
            }
        }
        for (auto& event : measure.bgm_events) {
            const auto row = context.shifter.row_of(event.position);
            if (row && *row >= cursor_row) {
                if (!shift_bgm(context, event))
                    return false;
                ++moved;
            }
        }
    }
    return shift_timing_family(context, chart.bga_events,
                               [](auto& e) -> local_position_t& { return e.position; },
                               cursor_row, moved) &&
           shift_timing_family(context, chart.lane_layouts,
                               [](auto& e) -> local_position_t& { return e.position; },
                               cursor_row, moved);
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

} // namespace

auto apply_shift_from_cursor(document_history_t& history,
                             const shift_from_cursor_action_t& action, time_shift_limits_t)
    -> time_shift_result_t {
    time_shift_result_t result;
    auto prepared = prepare_shift(history, action.chart, action.rows, result);
    if (!prepared)
        return result;
    if (!prepared->chart->sound_channels.empty() ||
        (action.include_timing && !prepared->chart->sv_recipes.empty())) {
        reject(result.command, diagnostic_code_t::unsupported_feature,
               "time shift is not supported with retained sound channels or SV recipes");
        return result;
    }
    const row_shifter_t shifter(prepared->index, action.rows);
    const auto cursor_row = shifter.row_of(action.cursor);
    if (!cursor_row) {
        reject(result.command, diagnostic_code_t::row_out_of_range, "time shift cursor is not on the chart");
        return result;
    }
    shift_context_t context{*prepared->chart, shifter, result};
    const bool ok = shift_content_after_cursor(context, *cursor_row) &&
                    shift_markers_after_cursor(context, *cursor_row, shifter, result) &&
                    (!action.include_timing || shift_timing_after_cursor(context, *cursor_row));
    if (!ok)
        return result;
    return finish_shift(history, *prepared, shifter, result, "Shift from cursor");
}

} // namespace wormedit
