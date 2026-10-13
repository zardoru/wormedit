#include <wormedit/core/lane_transform.h>

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

namespace wormedit {
namespace {

// Chooses the lane a note moves to, or nullopt to leave it where it is.
using lane_chooser_t =
    std::function<std::optional<lane_id_t>(const lane_layout_t&, lane_id_t)>;



auto has_lane(const lane_layout_t* layout, lane_id_t lane) -> bool { return layout != nullptr && layout->has_lane(lane); }



auto mirror_lane(const lane_layout_t& layout, lane_id_t lane) -> std::optional<lane_id_t> {
    const auto* definition = layout.find(lane);
    if (definition == nullptr || definition->role != lane_role_t::playable)
        return std::nullopt;
    std::vector<const lane_definition_t*> stage;
    for (const auto& candidate : layout.lanes)
        if (candidate.role == lane_role_t::playable && candidate.stage == definition->stage)
            stage.push_back(&candidate);
    std::ranges::stable_sort(stage, {}, &lane_definition_t::display_order);
    const auto position = std::ranges::find(stage, definition);
    const auto reversed = stage.size() - 1 - static_cast<std::size_t>(position - stage.begin());
    return stage[reversed]->id;
}

auto is_valid_permutation(const std::vector<lane_permutation_entry_t>& permutation) -> bool {
    std::set<std::uint64_t> sources;
    std::set<std::uint64_t> targets;
    for (const auto& entry : permutation) {
        if (!entry.from || !entry.to || !sources.insert(entry.from.value).second ||
            !targets.insert(entry.to.value).second)
            return false;
    }
    return !permutation.empty() && sources == targets;
}

auto collect_selected(chart_document_t& chart, const std::vector<object_id_t>& ids,
                      document_command_result_t& result) -> std::optional<std::vector<note_t*>> {
    std::unordered_map<std::uint64_t, note_t*> by_id;
    for (auto& measure : chart.measures)
        for (auto& note : measure.notes)
            by_id.try_emplace(note.id.value, &note);
    std::unordered_set<std::uint64_t> seen;
    std::vector<note_t*> notes;
    notes.reserve(ids.size());
    for (const auto id : ids) {
        const auto found = by_id.find(id.value);
        if (!id || found == by_id.end()) {
            reject(result, diagnostic_code_t::invalid_id, "lane transform names an unknown note", id);
            return std::nullopt;
        }
        if (!seen.insert(id.value).second) {
            reject(result, diagnostic_code_t::duplicate_id,
                   "lane transform names the same note twice", id);
            return std::nullopt;
        }
        notes.push_back(found->second);
    }
    return notes;
}

struct lane_interval_t {
    std::uint64_t head{};
    std::uint64_t tail{};
    object_id_t note;
};

// Rejects overlapping or stacked notes on any lane that received a note.
auto is_lanes_free_of_overlap(const chart_document_t& chart, const measure_index_t& index,
                           const std::set<std::uint64_t>& touched,
                           document_command_result_t& result) -> bool {
    std::map<std::uint64_t, std::vector<lane_interval_t>> lanes;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!touched.contains(note.lane.value))
                continue;
            const auto head = absolute_row(index, note.position);
            const auto tail = note.end ? absolute_row(index, *note.end) : head;
            if (head && tail)
                lanes[note.lane.value].push_back({*head, *tail, note.id});
        }
    }
    for (auto& [lane, intervals] : lanes) {
        std::ranges::sort(intervals, {}, &lane_interval_t::head);
        for (std::size_t i = 1; i < intervals.size(); ++i) {
            if (intervals[i].head <= intervals[i - 1].tail) {
                reject(result, diagnostic_code_t::collision,
                       "lane transform would overlap two notes on one lane", intervals[i].note);
                return false;
            }
        }
    }
    return true;
}

// Moves one note to `target`, checking the layout at its head and tail.
auto move_note(note_t& note, lane_id_t target, const chart_document_t& chart,
               const measure_index_t& index, document_command_result_t& result) -> bool {
    const auto* tail_layout = note.end ? layout_at(chart, index, *note.end) : nullptr;
    if (!has_lane(layout_at(chart, index, note.position), target) ||
        (note.end && !has_lane(tail_layout, target))) {
        reject(result, diagnostic_code_t::unknown_lane,
               "lane transform target lane is missing from the layout at the note", note.id);
        return false;
    }
    note.lane = target;
    return true;
}

auto apply_lane_assignment(document_history_t& history, std::uint64_t expected_revision,
                           chart_id_t chart_id, const std::vector<object_id_t>& ids,
                           const lane_chooser_t& choose, const char* label,
                           lane_transform_limits_t limits) -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    auto& result = transaction.outcome;
    if (expected_revision != history.document.revision) {
        reject(result, diagnostic_code_t::invalid_id, "lane transform document revision is stale");
        return result;
    }
    if (ids.empty() || ids.size() > limits.maximum_selected_notes) {
        reject(result, diagnostic_code_t::invalid_resolution,
               ids.empty() ? "lane transform requires at least one note"
                           : "lane transform selection exceeds its configured cap");
        return result;
    }
    auto& proposed = transaction.copy_document();
    const auto chart = proposed.find_chart(chart_id);
    if (!chart_id || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "lane transform refers to an unknown chart");
        return result;
    }
    auto index = build_measure_index(*chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&index)) {
        result.diagnostics = *diagnostics;
        return result;
    }
    const auto& measures = std::get<measure_index_t>(index);
    const auto notes = collect_selected(*chart, ids, result);
    if (!notes)
        return result;
    std::set<std::uint64_t> touched;
    for (auto* note : *notes) {
        const auto* layout = layout_at(*chart, measures, note->position);
        const auto target = layout != nullptr ? choose(*layout, note->lane) : std::nullopt;
        if (!target || *target == note->lane)
            continue;
        touched.insert(note->lane.value);
        touched.insert(target->value);
        if (!move_note(*note, *target, *chart, measures, result))
            return result;
    }
    if (touched.empty() || !is_lanes_free_of_overlap(*chart, measures, touched, result))
        return result;
    return transaction.commit(label);
}

} // namespace

auto apply_lane_mirror(document_history_t& history, std::uint64_t expected_revision,
                       const mirror_lanes_action_t& action, lane_transform_limits_t limits)
    -> document_command_result_t {
    return apply_lane_assignment(history, expected_revision, action.chart, action.notes,
                                 mirror_lane, "Mirror lanes", limits);
}

auto apply_lane_remap(document_history_t& history, std::uint64_t expected_revision,
                      const remap_lanes_action_t& action, lane_transform_limits_t limits)
    -> document_command_result_t {
    if (!is_valid_permutation(action.permutation)) {
        document_command_result_t result{.revision = history.document.revision};
        reject(result, diagnostic_code_t::invalid_resolution,
               "lane remap must be a non-empty bijection over the lanes it names");
        return result;
    }
    std::unordered_map<std::uint64_t, lane_id_t> mapping;
    for (const auto& entry : action.permutation)
        mapping.emplace(entry.from.value, entry.to);
    const lane_chooser_t choose = [&mapping](const lane_layout_t&, lane_id_t lane) {
        const auto found = mapping.find(lane.value);
        return found == mapping.end() ? std::nullopt : std::optional<lane_id_t>{found->second};
    };
    return apply_lane_assignment(history, expected_revision, action.chart, action.notes, choose,
                                 "Remap lanes", limits);
}

} // namespace wormedit
