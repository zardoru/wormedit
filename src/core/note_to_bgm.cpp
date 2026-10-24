#include "bmson_sound_channel_editor_internal.h"
#include "chart_object_ids.h"
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/core/bms_program_edit.h>

#include <wormedit/core/bmson_sound_channel_editor.h>
#include <wormedit/core/lane_profile_switch.h>

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace wormedit {
namespace {


struct group_key_t {
    bool is_group{};
    std::uint64_t id{};
    auto operator<=>(const group_key_t&) const = default;
};

struct candidate_t {
    const note_t* note{};
    std::size_t ordinal{};
    std::uint64_t absolute_row{};
    std::optional<group_key_t> group;
    auto operator==(const candidate_t&) const -> bool = default;
};

using resource_keys_t = std::unordered_map<std::uint64_t, group_key_t>;
using occupancy_t = std::pair<std::uint64_t, std::uint64_t>;
using group_lane_counts_t = std::map<group_key_t, std::map<std::uint64_t, std::size_t>>;
struct lane_ref_t {
    std::uint64_t id{};
    std::size_t order{};
};
struct allocation_state_t {
    std::set<occupancy_t> occupied;
    std::vector<lane_ref_t> lanes;
    std::unordered_map<std::uint64_t, std::size_t> lane_index;
    std::map<group_key_t, std::vector<std::uint64_t>> preferences;
    std::uint64_t largest_id{};
    std::size_t next_order{};
    bool order_exhausted{};
};
struct candidates_t {
    std::vector<candidate_t> candidates;
    std::map<group_key_t, std::size_t> group_first_ordinal;
    auto operator==(const candidates_t&) const -> bool = default;
};

auto make_resource_keys(std::span<const resource_entry_t> resources) -> resource_keys_t {
    resource_keys_t keys;
    keys.reserve(resources.size());
    for (const auto& resource : resources)
        keys.emplace(resource.id.value, resource.sound_group
                                            ? group_key_t{true, resource.sound_group->value}
                                            : group_key_t{false, resource.id.value});
    return keys;
}

auto event_group(const bgm_event_t& event,
                 const std::unordered_map<std::uint64_t, group_key_t>& resource_keys)
    -> std::optional<group_key_t> {
    if (!event.sound) return std::nullopt;
    const auto found = resource_keys.find(event.sound->value);
    return found == resource_keys.end() ? std::optional<group_key_t>{{false, event.sound->value}}
                                        : std::optional<group_key_t>{found->second};
}

auto note_group(const note_t& note,
                const std::unordered_map<std::uint64_t, group_key_t>& resource_keys)
    -> std::optional<group_key_t> {
    if (!note.sound) return std::nullopt;
    const auto found = resource_keys.find(note.sound->value);
    return found == resource_keys.end() ? std::optional<group_key_t>{{false, note.sound->value}}
                                        : std::optional<group_key_t>{found->second};
}

auto collect_candidates(const chart_document_t& chart, std::span<const object_id_t> ids,
                        const resource_keys_t& resource_keys,
                        const wormedit::measure_lookup_t& measure_entries)
    -> result_t<candidates_t, diagnostic_t> {
    std::unordered_set<std::uint64_t> requested;
    requested.reserve(ids.size());
    for (const auto id : ids) {
        if (!id || !requested.insert(id.value).second)
            return std::unexpected(
                diagnostic_t{diagnostic_severity_t::error,
                             diagnostic_code_t::duplicate_id,
                             "note move to BGM has an invalid or duplicate note ID",
                             {}});
    }
    std::vector<candidate_t> candidates;
    candidates.reserve(ids.size());
    std::map<group_key_t, std::size_t> group_first_ordinal;
    std::size_t ordinal{};
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (!requested.contains(note.id.value)) {
                ++ordinal;
                continue;
            }
            const auto absolute = measure_entries.row_of(note.position);
            if (!absolute)
                return std::unexpected(diagnostic_t{diagnostic_severity_t::error,
                                                    diagnostic_code_t::row_out_of_range,
                                                    "a selected note is outside measure geometry",
                                                    {}});
            if (note.sound_channel_event)
                return std::unexpected(diagnostic_t{
                    diagnostic_severity_t::error, diagnostic_code_t::unsupported_feature,
                    "retained linked notes must use the BMSON stem reassignment route", note.id});
            auto group = note_group(note, resource_keys);
            if (group && !group_first_ordinal.contains(*group))
                group_first_ordinal.emplace(*group, ordinal);
            candidates.push_back({&note, ordinal++, *absolute, group});
        }
    }
    if (candidates.size() != ids.size())
        return std::unexpected(diagnostic_t{diagnostic_severity_t::error,
                                            diagnostic_code_t::invalid_id,
                                            "note move to BGM names an unknown note",
                                            {}});
    return candidates_t{std::move(candidates), std::move(group_first_ordinal)};
}

auto order_candidates(std::vector<candidate_t>& candidates, std::span<const object_id_t> ids,
                      const std::map<group_key_t, std::size_t>& group_first_ordinal, bool manual) -> void {
    std::unordered_map<std::uint64_t, std::size_t> caller_order;
    if (manual) {
        caller_order.reserve(ids.size());
        for (std::size_t i = 0; i < ids.size(); ++i) caller_order.emplace(ids[i].value, i);
    }
    const auto cohort_order = [&](const candidate_t& candidate) {
        return candidate.group ? group_first_ordinal.at(*candidate.group) : candidate.ordinal;
    };
    std::ranges::stable_sort(candidates, [&](const auto& left, const auto& right) {
        if (manual) return caller_order.at(left.note->id.value) < caller_order.at(right.note->id.value);
        const auto left_cohort = cohort_order(left);
        const auto right_cohort = cohort_order(right);
        return std::tie(left_cohort, left.absolute_row, left.ordinal) <
               std::tie(right_cohort, right.absolute_row, right.ordinal);
    });
}

auto rank_group_preferences(allocation_state_t& state, const group_lane_counts_t& counts) -> void {
    for (const auto& [group, lane_counts] : counts) {
        std::vector<std::pair<std::uint64_t, std::size_t>> ranked;
        for (const auto& [lane, count] : lane_counts)
            if (state.lane_index.contains(lane)) ranked.emplace_back(lane, count);
        std::ranges::stable_sort(ranked, [&](const auto& left, const auto& right) {
            if (left.second != right.second) return left.second > right.second;
            return state.lane_index.at(left.first) < state.lane_index.at(right.first);
        });
        for (const auto& [lane, count] : ranked) { (void)count; state.preferences[group].push_back(lane); }
    }
}

auto prepare_allocation_state(const chart_document_t& chart, const resource_keys_t& resource_keys,
                              const wormedit::measure_lookup_t& measure_entries)
    -> result_t<allocation_state_t, diagnostic_t> {
    allocation_state_t state;
    group_lane_counts_t counts;
    for (const auto& measure : chart.measures)
        for (const auto& event : measure.bgm_events) {
            const auto absolute = measure_entries.row_of(event.position);
            if (!absolute)
                return std::unexpected(
                    diagnostic_t{diagnostic_severity_t::error,
                                 diagnostic_code_t::row_out_of_range,
                                 "an existing BGM event is outside measure geometry",
                                 {}});
            state.occupied.emplace(*absolute, event.source_lane.value);
            if (const auto group = event_group(event, resource_keys))
                ++counts[*group][event.source_lane.value];
        }
    for (const auto& lane : chart.bgm_source_lanes)
        state.lanes.push_back({lane.id.value, lane.source_order});
    std::ranges::stable_sort(state.lanes, {}, &lane_ref_t::order);
    for (std::size_t i = 0; i < state.lanes.size(); ++i)
        state.lane_index.emplace(state.lanes[i].id, i);
    rank_group_preferences(state, counts);
    state.largest_id = id_ceiling_t<object_id_t>::from(chart).maximum;
    for (const auto& lane : state.lanes) {
        if (lane.order == std::numeric_limits<std::size_t>::max())
            state.order_exhausted = true;
        else
            state.next_order = std::max(state.next_order, lane.order + 1);
    }
    return state;
}

auto choose_free_lane(allocation_state_t& state, const candidate_t& candidate,
                      std::optional<object_id_t> manual) -> std::optional<std::uint64_t> {
    const auto free = [&](std::uint64_t lane) {
        return !state.occupied.contains({candidate.absolute_row, lane});
    };
    if (manual) {
        const auto target = state.lane_index.find(manual->value);
        const auto start = target == state.lane_index.end() ? std::size_t{} : target->second;
        for (auto i = start; i < state.lanes.size(); ++i) if (free(state.lanes[i].id)) return state.lanes[i].id;
        for (auto i = start; i-- > 0;) if (free(state.lanes[i].id)) return state.lanes[i].id;
        return std::nullopt;
    }
    if (candidate.group)
        for (const auto lane : state.preferences[*candidate.group]) if (free(lane)) return lane;
    for (const auto& lane : state.lanes) if (free(lane.id)) return lane.id;
    return std::nullopt;
}

auto append_planned_lane(allocation_state_t& state, note_to_bgm_allocation_t& plan,
                         std::size_t existing_lane_count, note_to_bgm_policy_t policy)
    -> std::optional<object_id_t> {
    const auto lane_count = existing_lane_count + plan.new_lanes.size();
    if (!policy.allow_new_lanes || lane_count >= policy.max_lanes || lane_count >= 1'000'000 ||
        state.largest_id == std::numeric_limits<std::uint64_t>::max() || state.order_exhausted)
        return std::nullopt;
    const object_id_t id{++state.largest_id};
    const auto order = state.next_order;
    state.order_exhausted = order == std::numeric_limits<std::size_t>::max();
    if (!state.order_exhausted) ++state.next_order;
    plan.new_lanes.push_back({id, "BGM " + std::to_string(lane_count + 1), order});
    state.lanes.push_back({id.value, order});
    state.lane_index.emplace(id.value, state.lanes.size() - 1);
    return id;
}

auto allocate_candidates(const chart_document_t& chart, std::vector<candidate_t> candidates,
                         allocation_state_t state, std::optional<object_id_t> manual,
                         note_to_bgm_policy_t policy) -> note_to_bgm_allocation_outcome_t {
    note_to_bgm_allocation_outcome_t plan{};
    plan.value.assignments.reserve(candidates.size());
    plan.value.retained_event_ids.reserve(candidates.size());
    for (const auto& candidate : candidates) {
        auto lane = choose_free_lane(state, candidate, manual);
        if (!lane) {
            const auto added =
                append_planned_lane(state, plan.value, chart.bgm_source_lanes.size(), policy);
            if (!added) {
                note_to_bgm_allocation_outcome_t rejected{};
                rejected.diagnostics.push_back(
                    error(diagnostic_code_t::collision,
                          "no BGM lane is available for every selected note"));
                return rejected;
            }
            lane = added->value;
            if (candidate.group)
                state.preferences[*candidate.group].push_back(*lane);
        }
        state.occupied.emplace(candidate.absolute_row, *lane);
        plan.value.assignments.push_back({candidate.note->id, object_id_t{*lane}});
        plan.value.retained_event_ids.push_back(candidate.note->id);
        if (candidate.group && std::ranges::find(state.preferences[*candidate.group], *lane) ==
                                   state.preferences[*candidate.group].end())
            state.preferences[*candidate.group].push_back(*lane);
    }
    return plan;
}

auto plan_allocation(const chart_document_t& chart, std::span<const resource_entry_t> resources,
                     std::span<const object_id_t> ids, std::optional<object_id_t> manual_lane,
                     note_to_bgm_policy_t policy) -> note_to_bgm_allocation_outcome_t {
    note_to_bgm_allocation_outcome_t result{};
    if (ids.empty() || ids.size() > 1'000'000) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "note move to BGM needs 1 to 1000000 notes"));
        return result;
    }
    const auto built = measure_index_t::from(chart);
    if (!built) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                                           "BGM allocation requires valid measure geometry"));
        return result;
    }
    const auto& geometry = *built;
    const auto measure_entries = measure_lookup_t::from(geometry);
    const auto resource_keys = make_resource_keys(resources);
    auto collected = collect_candidates(chart, ids, resource_keys, measure_entries);
    if (!collected) {
        result.diagnostics.push_back(std::move(collected.error()));
        return result;
    }
    order_candidates(collected->candidates, ids, collected->group_first_ordinal,
                     manual_lane.has_value());
    auto prepared = prepare_allocation_state(chart, resource_keys, measure_entries);
    if (!prepared) {
        result.diagnostics.push_back(std::move(prepared.error()));
        return result;
    }
    return allocate_candidates(chart, std::move(collected->candidates), std::move(*prepared),
                               manual_lane, policy);
}

auto apply_plan(chart_document_t& chart, const note_to_bgm_allocation_t& plan) -> bool {
    for (const auto& lane : plan.new_lanes)
        chart.bgm_source_lanes.push_back({.id = lane.id, .name = lane.name, .source_order = lane.source_order});
    std::unordered_map<std::uint64_t, object_id_t> destinations;
    destinations.reserve(plan.assignments.size());
    for (const auto& assignment : plan.assignments) destinations.emplace(assignment.note.value, assignment.lane);
    std::vector<note_t> converted;
    converted.reserve(plan.assignments.size());
    for (auto& measure : chart.measures) {
        for (const auto& note : measure.notes)
            if (destinations.contains(note.id.value)) converted.push_back(note);
        std::erase_if(measure.notes, [&](const auto& note) { return destinations.contains(note.id.value); });
    }
    if (converted.size() != plan.assignments.size()) return false;
    std::unordered_map<std::uint64_t, note_t> notes;
    notes.reserve(converted.size());
    for (auto& note : converted) notes.emplace(note.id.value, std::move(note));
    std::unordered_map<std::uint64_t, std::size_t> measure_index;
    for (std::size_t i = 0; i < chart.measures.size(); ++i)
        measure_index.emplace(chart.measures[i].id.value, i);
    for (const auto& assignment : plan.assignments) {
        const auto found_note = notes.find(assignment.note.value);
        if (found_note == notes.end()) return false;
        const auto& note = found_note->second;
        bgm_event_t event{.id = note.id, .position = note.position, .source_lane = assignment.lane,
                          .sound = note.sound, .sound_channel_event = std::nullopt,
                          .structural_slot = 0, .branch = note.branch, .provenance = note.provenance,
                          .end = note.end};
        event.semantic_layers = note.semantic_layers;
        const auto destination = measure_index.find(note.position.measure.value);
        if (destination == measure_index.end()) return false;
        chart.measures[destination->second].bgm_events.push_back(std::move(event));
    }
    return true;
}

struct selected_note_sets_t {
    std::vector<object_id_t> ordinary;
    std::vector<object_id_t> bmson_linked;
};

auto partition_selected_notes(const chart_document_t& chart, std::span<const object_id_t> ids)
    -> result_t<selected_note_sets_t, diagnostic_t> {
    std::unordered_set<std::uint64_t> requested;
    requested.reserve(ids.size());
    for (const auto id : ids)
        if (!id || !requested.insert(id.value).second)
            return std::unexpected(diagnostic_t{diagnostic_severity_t::error,
                                                diagnostic_code_t::duplicate_id,
                                                "note move to BGM has duplicate or invalid IDs",
                                                {}});
    std::unordered_set<std::uint64_t> linked;
    std::unordered_set<std::uint64_t> found;
    found.reserve(ids.size());
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if (requested.contains(note.id.value)) {
                found.insert(note.id.value);
                if (note.sound_channel_event && chart.source_family == format_family_t::bmson)
                    linked.insert(note.id.value);
            }
    if (found.size() != requested.size())
        return std::unexpected(diagnostic_t{diagnostic_severity_t::error,
                                            diagnostic_code_t::invalid_id,
                                            "note move to BGM names an unknown note",
                                            {}});
    selected_note_sets_t sets;
    for (const auto id : ids) {
        if (linked.contains(id.value))
            sets.bmson_linked.push_back(id);
        else
            sets.ordinary.push_back(id);
    }
    return sets;
}

auto reassign_bmson_notes(document_command_transaction_t& transaction, chart_id_t chart,
                          std::span<const object_id_t> ids) -> diagnostic_list_t {
    for (const auto id : ids) {
        auto step = stage_assign_bmson_sound_event(transaction, chart, id, std::nullopt);
        if (!step.value || !step.ok())
            return std::move(step.diagnostics);
        if (!step.value->changed)
            return {
                error(diagnostic_code_t::invalid_id, "a BMSON stem note could not be reassigned")};
    }
    return {};
}

auto apply_ordinary_plan(chart_document_t& chart, std::span<const resource_entry_t> resources,
                         std::span<const object_id_t> ids,
                         std::optional<object_id_t> preferred_lane, note_to_bgm_policy_t policy)
    -> diagnostic_list_t {
    if (ids.empty())
        return {};
    const auto plan = plan_allocation(chart, resources, ids, preferred_lane, policy);
    if (!plan.diagnostics.empty())
        return plan.diagnostics;
    if (!apply_plan(chart, plan.value))
        return {
            error(diagnostic_code_t::invalid_id, "the BGM allocation plan could not be applied")};
    return {};
}

} // namespace

auto note_to_bgm_allocation_t::from(const chart_document_t& chart,
                                    std::span<const resource_entry_t> resources,
                                    std::span<const object_id_t> notes,
                                    std::optional<object_id_t> preferred_lane,
                                    note_to_bgm_policy_t policy)
    -> note_to_bgm_allocation_outcome_t {
    return plan_allocation(chart, resources, notes, preferred_lane, policy);
}

namespace {
auto stage_scoped_notes_to_bgm(document_command_transaction_t& transaction, chart_id_t difficulty,
    std::span<const object_id_t> notes, std::optional<object_id_t> preferred_lane, note_to_bgm_policy_t policy)
    -> attempt_t<document_edit_effect_t> {
    if (notes.empty() || notes.size() > 4096) return {.diagnostics = {error(diagnostic_code_t::invalid_id,
        "Scoped BGM conversion requires 1 to 4096 notes.")}};
    const auto* source = transaction.read_chart(difficulty);
    for (const auto id : notes) {
        const auto* note = source ? source->find_note(id) : nullptr;
        if (!note || note->label || note->osu_hitsample ||
            (note->kind != note_kind_t::tap && note->kind != note_kind_t::hold))
            return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                "Scoped BGM conversion cannot discard note annotations or special note kinds.", id)}};
    }
    const auto* program = transaction.current_document().find_bms_program(difficulty);
    auto plan = plan_allocation(*source, program->prepared().resources, notes, preferred_lane, policy);
    if (!plan.diagnostics.empty()) return {.diagnostics = std::move(plan.diagnostics)};
    for (auto& lane : plan.value.new_lanes) {
        const auto created = transaction.stage_bms_bgm_column(difficulty);
        if (!created.value || !created.ok()) return {.diagnostics = created.diagnostics};
        for (auto& assignment : plan.value.assignments)
            if (assignment.lane == lane.id) assignment.lane = *created.value;
        const auto& column = transaction.current_document().find_bms_program(difficulty)->prepared().bgm_columns.back();
        lane = {.id = column.id, .name = column.name, .source_order = column.source_order};
    }
    return transaction.stage_bms_native_objects(difficulty, notes, bms_existing_edit_kind_t::update,
        [&](chart_document_t& chart) -> attempt_t<document_edit_effect_t> {
            if (!apply_plan(chart, plan.value)) return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                "Scoped BGM conversion could not apply its typed allocation.")}};
            return {.value = document_edit_effect_t{.changed = true, .command_label = "Move notes to BGM"}};
        });
}
} // namespace

auto stage_move_notes_to_bgm(document_command_transaction_t& transaction, chart_id_t chart_id,
                             std::span<const object_id_t> notes,
                             std::optional<object_id_t> preferred_lane, note_to_bgm_policy_t policy)
    -> attempt_t<document_edit_effect_t> {
    return transaction.stage([&](song_document_t&) -> attempt_t<document_edit_effect_t> {
        if (transaction.has_bms_program(chart_id)) return stage_scoped_notes_to_bgm(transaction, chart_id, notes, preferred_lane, policy);
        if (notes.empty() || notes.size() > 4096)
            return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                          "note move to BGM needs 1 to 4096 notes")}};
        return transaction.stage(
            chart_id, "note move to BGM refers to an unknown chart",
            [&](chart_document_t& chart,
                song_document_t& document) -> attempt_t<document_edit_effect_t> {
                const auto selection = partition_selected_notes(chart, notes);
                if (!selection)
                    return {.diagnostics = {selection.error()}};
                auto diagnostics =
                    reassign_bmson_notes(transaction, chart_id, selection->bmson_linked);
                if (!diagnostics.empty())
                    return {.diagnostics = std::move(diagnostics)};
                diagnostics = apply_ordinary_plan(chart, document.resources, selection->ordinary,
                                                  preferred_lane, policy);
                if (!diagnostics.empty())
                    return {.diagnostics = std::move(diagnostics)};
                return {.value = document_edit_effect_t{
                            .changed = true,
                            .command_label =
                                notes.size() == 1 ? "Move note to BGM" : "Move notes to BGM"}};
            });
    });
}

auto move_notes_to_bgm(document_history_t& history, chart_id_t chart_id,
                       std::span<const object_id_t> notes,
                       std::optional<object_id_t> preferred_lane, note_to_bgm_policy_t policy)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    return transaction.finish_execution(
        stage_move_notes_to_bgm(transaction, chart_id, notes, preferred_lane, policy));
}

namespace {

struct located_bgm_t {
    measure_chunk_t* measure{};
    std::vector<bgm_event_t>::iterator event;
};

auto find_bgm(chart_document_t& chart, object_id_t id) -> std::optional<located_bgm_t> {
    for (auto& measure : chart.measures) {
        const auto found = std::ranges::find(measure.bgm_events, id, &bgm_event_t::id);
        if (found != measure.bgm_events.end())
            return located_bgm_t{&measure, found};
    }
    return std::nullopt;
}

auto is_blocked_by_note(const chart_document_t& chart, const measure_index_t& index, lane_id_t lane,
                        std::uint64_t begin, std::uint64_t finish) -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (note.lane != lane) continue;
            const auto other_begin = index.row_of(note.position);
            const auto other_end = note.end ? index.row_of(*note.end) : other_begin;
            if (!other_begin || !other_end || (*other_begin <= finish && begin <= *other_end))
                return true;
        }
    }
    return false;
}

auto is_editable_lane_at(const chart_document_t& chart, local_position_t position,
                      lane_id_t lane) -> bool {
    const auto* segment = active_lane_layout_segment(chart, position);
    if (!segment) return false;
    return std::ranges::any_of(segment->layout.lanes, [lane](const auto& item) {
        return item.id == lane && item.is_editable();
    });
}

auto is_layout_covering_hold(const chart_document_t& chart, const measure_index_t& index,
                        const bgm_event_t& event, lane_id_t lane,
                        std::uint64_t begin, std::uint64_t finish) -> bool {
    if (!event.end || !is_editable_lane_at(chart, event.position, lane) ||
        !is_editable_lane_at(chart, *event.end, lane)) return false;
    for (const auto& segment : chart.lane_layouts) {
        const auto row = index.row_of(segment.position);
        if (row && *row > begin && *row <= finish && !is_editable_lane_at(chart, segment.position, lane))
            return false;
    }
    return true;
}

// Validate a restored tap/hold against inclusive note intervals and lane-layout transitions.
auto is_valid_restore_span(const chart_document_t& chart, const bgm_event_t& event,
                        lane_id_t lane) -> bool {
    if (!event.end) {
        return std::ranges::none_of(chart.measures, [&](const auto& measure) {
            return std::ranges::any_of(measure.notes, [&](const auto& note) {
                return note.lane == lane && note.position == event.position;
            });
        });
    }
    const auto built = measure_index_t::from(chart);
    if (!built) return false;
    const auto& index = *built;
    const auto begin = index.row_of(event.position);
    const auto finish = index.row_of(*event.end);
    return begin && finish && *finish > *begin &&
           !is_blocked_by_note(chart, index, lane, *begin, *finish) &&
           is_layout_covering_hold(chart, index, event, lane, *begin, *finish);
}

auto convert_bgm_event(chart_document_t& chart, object_id_t id, lane_id_t lane) -> bool {
    const auto located = find_bgm(chart, id);
    if (!located) return false;
    const auto event = *located->event;
    const auto position = event.position;
    const auto end = event.end;
    if (!is_valid_restore_span(chart, event, lane)) return false;
    note_t note{.id = event.id, .position = position, .lane = lane,
                .kind = end ? note_kind_t::hold : note_kind_t::tap,
                .end = end, .sound = event.sound};
    note.semantic_layers = located->event->semantic_layers;
    note.branch = located->event->branch;
    note.provenance = located->event->provenance;
    located->measure->bgm_events.erase(located->event);
    const auto destination = chart.find_measure(position.measure);
    if (destination == nullptr) return false;
    destination->notes.push_back(std::move(note));
    return true;
}

auto restore_bgm_events(document_command_transaction_t& transaction, chart_document_t& chart,
                        std::span<const object_id_t> events, lane_id_t lane)
    -> attempt_t<document_edit_effect_t> {
    for (const auto id : events) {
        const auto located = find_bgm(chart, id);
        if (!located)
            return {.diagnostics = {
                        error(diagnostic_code_t::invalid_id, "BGM move names an unknown event")}};
        if (located->event->sound_channel_event && chart.source_family == format_family_t::bmson) {
            auto step = stage_assign_bmson_sound_event(transaction, chart.id, id, lane);
            if (!step.value || !step.ok())
                return {.diagnostics = std::move(step.diagnostics)};
            if (!step.value->changed)
                return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                              "the event could not move to that lane")}};
        } else if (!convert_bgm_event(chart, id, lane)) {
            return {.diagnostics = {
                        error(diagnostic_code_t::collision,
                              "the target lane conflicts with the event row or hold span")}};
        }
    }
    return {.value = document_edit_effect_t{.changed = true,
                                            .command_label = events.size() == 1
                                                                 ? "Move BGM event to lane"
                                                                 : "Move BGM events to lane"}};
}
} // namespace

auto stage_move_bgm_events_to_lane(document_command_transaction_t& transaction, chart_id_t chart_id,
    std::span<const object_id_t> events, lane_id_t lane) -> attempt_t<document_edit_effect_t> {
    return transaction.stage([&](song_document_t&) -> attempt_t<document_edit_effect_t> {
        if (events.empty() || events.size() > 4096 || !lane)
            return {.diagnostics = {error(diagnostic_code_t::invalid_id, "BGM move needs 1 to 4096 events and a lane")}};
        if (transaction.has_bms_program(chart_id))
            return transaction.stage_bms_native_objects(chart_id, events, bms_existing_edit_kind_t::update,
                [&](chart_document_t& chart) -> attempt_t<document_edit_effect_t> {
                    for (const auto id : events) {
                        const auto* event = chart.find_bgm_event(id);
                        if (!event || event->sound_channel_event) return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                            "Scoped BGM restoration cannot detach retained stem points.", id)}};
                    }
                    return restore_bgm_events(transaction, chart, events, lane);
                });
        return transaction.stage(chart_id, "BGM move refers to an unknown chart",
            [&](chart_document_t& chart, song_document_t&) { return restore_bgm_events(transaction, chart, events, lane); });
    });
}

auto move_bgm_events_to_lane(document_history_t& history, chart_id_t chart_id,
                             std::span<const object_id_t> events, lane_id_t lane)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    return transaction.finish_execution(
        stage_move_bgm_events_to_lane(transaction, chart_id, events, lane));
}

auto move_selected_objects_to_lane(document_history_t& history,
                                   const editor_interaction_state_t& state,
                                   std::span<const object_id_t> notes,
                                   std::span<const object_id_t> events, lane_id_t lane)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    auto staged = transaction.stage([&](song_document_t&) -> attempt_t<document_edit_effect_t> {
        if (notes.size() > 1'000'000 || events.size() > 4096 || !lane)
            return {.diagnostics = {
                        error(diagnostic_code_t::invalid_id,
                              "lane assignment exceeds the selection limit or has no lane")}};
        if (notes.empty() && events.empty())
            return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                                          "lane assignment needs at least one selected object")}};
        bool changed{};
        if (!notes.empty()) {
            auto selection = state;
            selection.selected_notes.assign(notes.begin(), notes.end());
            std::erase_if(selection.selected_objects, [](const auto& object) {
                return object.kind == editor_selection_object_kind_t::note;
            });
            auto assigned = stage_assign_note_lanes(transaction, selection, lane);
            if (!assigned.value || !assigned.ok())
                return {.diagnostics = std::move(assigned.diagnostics)};
            changed = assigned.value->changed;
        }
        if (!events.empty()) {
            auto moved = stage_move_bgm_events_to_lane(transaction, state.chart, events, lane);
            if (!moved.value || !moved.ok())
                return {.diagnostics = std::move(moved.diagnostics)};
            changed = changed || moved.value->changed;
        }
        return {.value = document_edit_effect_t{
                    .changed = changed, .command_label = "Assign selected objects to lane"}};
    });
    return transaction.finish_execution(std::move(staged),
                                        command_noop_policy_t::accept_without_diagnostics);
}

namespace {
auto selected_conversion_notes(const editor_interaction_state_t& state) -> std::vector<object_id_t> {
    auto ids = state.selected_notes;
    for (const auto& object : state.selected_objects)
        if (object.kind == editor_selection_object_kind_t::note) ids.push_back(object.id);
    std::ranges::sort(ids, {}, &object_id_t::value);
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}

auto first_conversion_note(const chart_document_t& chart, std::span<const object_id_t> ids)
    -> object_id_t {
    using key_t = std::tuple<std::size_t, std::uint64_t, std::uint64_t, std::uint64_t>;
    std::optional<key_t> first;
    for (std::size_t i = 0; i < chart.measures.size(); ++i)
        for (const auto& note : chart.measures[i].notes) {
            if (std::ranges::find(ids, note.id) == ids.end()) continue;
            const key_t key{i, note.position.row, note.lane.value, note.id.value};
            if (!first || key < *first) first = key;
        }
    return first ? object_id_t{std::get<3>(*first)} : object_id_t{};
}

void finish_note_conversion_selection(editor_interaction_state_t& state,
                                       std::span<const object_id_t> moving, bool successive) {
    const auto moved = [&](object_id_t id) { return std::ranges::find(moving, id) != moving.end(); };
    std::erase_if(state.selected_notes, moved);
    std::erase_if(state.selected_objects, [&](const auto& object) {
        return object.kind == editor_selection_object_kind_t::note && moved(object.id);
    });
    if (!successive)
        for (const auto id : moving)
            state.selected_objects.push_back({editor_selection_object_kind_t::bgm_event, id});
}
} // namespace

auto move_editor_selection_to_bgm(document_history_t& history,
                                  editor_interaction_state_t& state,
                                  std::optional<object_id_t> lane, bool successive)
    -> selected_note_to_bgm_result_t {
    auto moving = selected_conversion_notes(state);
    if (moving.empty()) return {};
    selected_note_to_bgm_result_t result{.payload = {.handled = true}};
    if (state.is_pending()) {
        result.command = document_command_transaction_t::rejection(history.document.revision, diagnostic_code_t::invalid_id,
                                   "note move to BGM has stale input or an active gesture");
        return result;
    }
    const auto chart = history.document.find_chart(state.chart);
    if (chart == nullptr) {
        result.command = document_command_transaction_t::rejection(
            history.document.revision, diagnostic_code_t::invalid_id,
            "note move to BGM refers to an unknown chart");
        return result;
    }
    const auto selected = partition_selected_notes(*chart, moving);
    if (!selected) {
        result.command = {.revision = history.document.revision, .diagnostics = {selected.error()}};
        return result;
    }
    if (successive) moving = {first_conversion_note(*chart, moving)};
    result.command = move_notes_to_bgm(history, state.chart, moving, lane);
    if (!result.command.applied) return result;
    finish_note_conversion_selection(state, moving, successive);
    result.payload.selection_exhausted =
        state.selected_notes.empty() && state.selected_objects.empty();
    return result;
}

} // namespace wormedit
