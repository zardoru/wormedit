#include <wormedit/core/lane_migration.h>

#include <wormedit/core/lane_profile_switch.h>

#include <algorithm>
#include <map>
#include <numeric>
#include <set>
#include <ranges>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {

struct lane_migration_proposal_payload_t {
    chart_stamp_t stamp;
    lane_input_profile_t target;
    std::vector<lane_migration_mapping_t> mapping;
    lane_migration_report_t report;
};

namespace {

struct source_lane_rank_t {
    std::size_t rank{};
    lane_role_t role{lane_role_t::playable};
    bool initialized{};
    bool ambiguous{};
};

auto ordered_lanes(const lane_layout_t& layout) -> std::vector<lane_definition_t> {
    auto lanes = layout.lanes;
    std::ranges::stable_sort(lanes, {}, &lane_definition_t::display_order);
    return lanes;
}

auto has_target_layout_unique_ids(const lane_layout_t& layout) -> bool {
    std::unordered_set<std::uint64_t> ids;
    for (const auto& lane : layout.lanes)
        if (!lane.id || !ids.insert(lane.id.value).second) return false;
    return !layout.lanes.empty();
}

auto collect_source_lanes(const chart_document_t& chart,
                          std::unordered_map<std::uint64_t, source_lane_rank_t>& lanes)
    -> bool {
    for (const auto& segment : chart.lane_layouts) {
        const auto ordered = ordered_lanes(segment.layout);
        for (std::size_t rank = 0; rank < ordered.size(); ++rank) {
            if (!ordered[rank].id) return false;
            auto& saved = lanes[ordered[rank].id.value];
            if ((rank != 0 && ordered[rank - 1].display_order == ordered[rank].display_order) ||
                (saved.initialized &&
                 (saved.rank != rank || saved.role != ordered[rank].role))) {
                saved.ambiguous = true;
            }
            if (!saved.initialized) {
                saved.rank = rank;
                saved.role = ordered[rank].role;
                saved.initialized = true;
            }
        }
    }
    return !lanes.empty();
}

auto build_mapping(const chart_document_t& chart, const lane_layout_t& target,
                   std::span<const lane_migration_mapping_t> requested,
                   proposal_t<lane_migration_proposal_t>& pending)
    -> result_t<std::unordered_map<std::uint64_t, std::optional<lane_id_t>>, lane_migration_diagnostic_t> {
    auto& proposal = pending.payload;
    std::unordered_map<std::uint64_t, source_lane_rank_t> source_lanes;
    if (!collect_source_lanes(chart, source_lanes)) {
        return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::missing_layout,
                "The chart has no usable lane layout.", pending.stamp, std::move(proposal.report)));
    }
    const auto targets = ordered_lanes(target);
    if (targets.empty()) {
        return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_target_profile,
                "The target profile has no lanes.", pending.stamp, std::move(proposal.report)));
    }
    std::unordered_map<std::uint64_t, std::optional<lane_id_t>> mapping;
    if (requested.empty()) {
        for (const auto& [id, lane] : source_lanes) {
            if (lane.ambiguous) {
                return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::ambiguous_default_mapping,
                "A source lane changes display position or role; provide an explicit mapping.", pending.stamp, std::move(proposal.report)));
            }
            mapping.emplace(id, lane.rank < targets.size()
                                    ? std::optional{targets[lane.rank].id}
                                    : std::nullopt);
        }
    } else {
        for (const auto& item : requested) {
            if (!source_lanes.contains(item.source_lane.value) ||
                mapping.contains(item.source_lane.value) ||
                (item.target_lane &&
                 !std::ranges::any_of(targets, [&](const auto& lane) {
                     return lane.id == *item.target_lane;
                 }))) {
                return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_mapping,
                "The explicit mapping contains an unknown or duplicate lane.", pending.stamp, std::move(proposal.report)));
            }
            mapping.emplace(item.source_lane.value, item.target_lane);
        }
        if (mapping.size() != source_lanes.size()) {
            return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_mapping,
                "The explicit mapping must name every source lane.", pending.stamp, std::move(proposal.report)));
        }
    }
    proposal.mapping.reserve(mapping.size());
    for (const auto& [id, lane] : source_lanes) {
        proposal.mapping.push_back({lane_id_t{id}, mapping.at(id)});
    }
    std::ranges::sort(proposal.mapping, {}, &lane_migration_mapping_t::source_lane);
    return mapping;
}

auto absolute_range(const measure_index_t& index, local_position_t begin_position,
                    std::optional<local_position_t> end_position)
    -> std::optional<std::pair<std::uint64_t, std::uint64_t>> {
    const auto begin = index.row_of(begin_position);
    const auto end = end_position ? index.row_of(*end_position) : begin;
    if (!begin || !end) return std::nullopt;
    return std::pair{*begin, *end};
}

auto changed_binding_count(const input_profile_t& before, const input_profile_t& after)
    -> std::size_t {
    const auto missing = [](const auto& left, const auto& right) {
        return static_cast<std::size_t>(std::ranges::count_if(left, [&](const auto& binding) {
            return std::ranges::none_of(right, [&](const auto& candidate) {
                return binding == candidate;
            });
        }));
    };
    return missing(before.bindings, after.bindings) + missing(after.bindings, before.bindings);
}

auto is_preview_current(const chart_stamp_t& stamp, const lane_migration_proposal_t& proposal,
                         const lane_migration_proposal_payload_t& canonical) -> bool {
    return stamp == canonical.stamp && proposal.target == canonical.target &&
           proposal.mapping == canonical.mapping && proposal.report == canonical.report;
}

struct migration_item_t {
    object_id_t id;
    bool sound_channel_event{};
    lane_id_t source_lane;
    lane_id_t target_lane;
    std::pair<std::uint64_t, std::uint64_t> range;
};

auto chart_measure_index(const chart_document_t& chart, proposal_t<lane_migration_proposal_t>& pending)
    -> result_t<measure_index_t, lane_migration_diagnostic_t> {
    auto& proposal = pending.payload;
    auto built = measure_index_t::from(chart);
    if (!built) {
        return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_mapping,
                built.error().empty() ? "The chart measure geometry is invalid."
                                  : built.error().front().message, pending.stamp, std::move(proposal.report)));
    }
    return std::move(*built);
}

auto append_mapped_note_items(const chart_document_t& chart, const measure_index_t& index,
                              const std::unordered_map<std::uint64_t,
                                  std::optional<lane_id_t>>& mapping,
                              const std::unordered_map<std::uint64_t, std::size_t>&
                                  source_lanes_by_target,
                              proposal_t<lane_migration_proposal_t>& pending,
                              std::vector<migration_item_t>& items) -> result_t<void, lane_migration_diagnostic_t> {
    auto& proposal = pending.payload;
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto destination = mapping.find(note.lane.value);
            if (destination == mapping.end()) {
                return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_mapping,
                "A note refers to a lane missing from the chart layouts.", pending.stamp, std::move(proposal.report)));
            }
            if (!destination->second) {
                proposal.report.dropped_notes.push_back(note.id);
                if (note.sound_channel_event)
                    proposal.report.dropped_sound_channel_events.push_back(*note.sound_channel_event);
                continue;
            }
            const auto range = absolute_range(index, note.position, note.end);
            if (!range) {
                return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_mapping,
                "A note position cannot be resolved in the chart.", pending.stamp, std::move(proposal.report)));
            }
            ++proposal.report.mapped_note_count;
            proposal.report.remapped_note_count += note.lane != *destination->second ? 1U : 0U;
            proposal.report.notes_in_merged_lanes +=
                source_lanes_by_target.at(destination->second->value) > 1 ? 1U : 0U;
            items.push_back({note.id, false, note.lane, *destination->second, *range});
        }
    }
    return {};
}

auto collect_linked_event_ids(const chart_document_t& chart)
    -> std::unordered_set<std::uint64_t> {
    std::unordered_set<std::uint64_t> linked;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if (note.sound_channel_event) linked.insert(note.sound_channel_event->value);
    return linked;
}

auto is_linked_event_lane_match(const chart_document_t& chart,
                              const std::unordered_map<std::uint64_t,
                                  std::optional<lane_id_t>>& mapping,
                              proposal_t<lane_migration_proposal_t>& pending) -> result_t<void, lane_migration_diagnostic_t> {
    auto& proposal = pending.payload;
    std::unordered_map<std::uint64_t, lane_id_t> notes_by_event;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if (note.sound_channel_event)
                notes_by_event.emplace(note.sound_channel_event->value, note.lane);
    for (const auto& channel : chart.sound_channels) {
        for (const auto& event : channel.events) {
            if (!event.playable) continue;
            const auto note = notes_by_event.find(event.id.value);
            if (note == notes_by_event.end()) continue;
            const auto note_target = mapping.find(note->second.value);
            const auto event_target = mapping.find(event.lane.value);
            if (event.lane != note->second || note_target == mapping.end() ||
                event_target == mapping.end() || note_target->second != event_target->second) {
                return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_mapping,
                "A linked semantic note and retained sound event must migrate together.", pending.stamp, std::move(proposal.report)));
            }
        }
    }
    return {};
}

auto append_unlinked_event_items(const chart_document_t& chart, const measure_index_t& index,
                                 const std::unordered_map<std::uint64_t,
                                     std::optional<lane_id_t>>& mapping,
                                 const std::unordered_set<std::uint64_t>& linked,
                                 proposal_t<lane_migration_proposal_t>& pending,
                                 std::vector<migration_item_t>& items) -> result_t<void, lane_migration_diagnostic_t> {
    auto& proposal = pending.payload;
    for (const auto& channel : chart.sound_channels) {
        for (const auto& event : channel.events) {
            if (!event.playable || linked.contains(event.id.value)) continue;
            const auto destination = mapping.find(event.lane.value);
            if (destination == mapping.end()) {
                return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_mapping,
                "A retained sound event refers to a lane missing from the chart layouts.", pending.stamp, std::move(proposal.report)));
            }
            if (!destination->second) {
                proposal.report.dropped_sound_channel_events.push_back(event.id);
                continue;
            }
            const auto range = absolute_range(index, event.position, event.end);
            if (!range) {
                return std::unexpected(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_mapping,
                "A retained sound event position cannot be resolved in the chart.", pending.stamp, std::move(proposal.report)));
            }
            items.push_back({event.id, true, event.lane, *destination->second, *range});
        }
    }
    return {};
}

struct active_migration_item_t {
    std::uint64_t source_lane{};
    object_id_t id;
    bool sound_channel_event{};
};

auto remove_active_lane_max(std::map<std::uint64_t, std::set<std::uint64_t>>& lanes_by_end,
                            const std::map<std::uint64_t,
                                std::multiset<std::pair<std::uint64_t, std::uint64_t>>>& active,
                            std::uint64_t lane) -> void {
    const auto found = active.find(lane);
    if (found == active.end() || found->second.empty()) return;
    const auto max_end = found->second.rbegin()->first;
    auto by_end = lanes_by_end.find(max_end);
    if (by_end == lanes_by_end.end()) return;
    by_end->second.erase(lane);
    if (by_end->second.empty()) lanes_by_end.erase(by_end);
}

auto find_active_other_lane(const std::map<std::uint64_t, std::set<std::uint64_t>>& lanes_by_end,
                            std::uint64_t source_lane, std::uint64_t begin)
    -> std::optional<std::uint64_t> {
    for (auto end = lanes_by_end.rbegin(); end != lanes_by_end.rend(); ++end) {
        if (end->first < begin) break;
        const auto first = end->second.begin();
        if (first != end->second.end() && *first != source_lane) return *first;
        if (std::next(first) != end->second.end()) return *std::next(first);
    }
    return std::nullopt;
}

auto collect_merge_collisions(std::vector<migration_item_t>& items,
                              lane_migration_proposal_t& proposal) -> void {
    constexpr std::size_t maximum_examples = 64;
    std::ranges::sort(items, [](const auto& left, const auto& right) {
        if (left.target_lane != right.target_lane) return left.target_lane < right.target_lane;
        if (left.range.first != right.range.first) return left.range.first < right.range.first;
        return left.range.second < right.range.second;
    });
    std::map<std::uint64_t, std::multiset<std::pair<std::uint64_t, std::uint64_t>>> active;
    std::map<std::uint64_t, std::set<std::uint64_t>> lanes_by_max_end;
    std::multimap<std::uint64_t, active_migration_item_t> expirations;
    std::uint64_t target_lane{};
    for (const auto& item : items) {
        if (item.target_lane.value != target_lane) {
            active.clear();
            lanes_by_max_end.clear();
            expirations.clear();
            target_lane = item.target_lane.value;
        }
        while (!expirations.empty() && expirations.begin()->first < item.range.first) {
            const auto expired = expirations.begin();
            const auto lane = expired->second.source_lane;
            remove_active_lane_max(lanes_by_max_end, active, lane);
            auto intervals = active.find(lane);
            intervals->second.erase({expired->first, expired->second.id.value});
            if (intervals->second.empty()) {
                active.erase(intervals);
            } else {
                lanes_by_max_end[intervals->second.rbegin()->first].insert(lane);
            }
            expirations.erase(expired);
        }
        const auto other = find_active_other_lane(lanes_by_max_end, item.source_lane.value,
                                                  item.range.first);
        if (other) {
            ++proposal.report.collision_count;
            if (proposal.report.collisions.size() < maximum_examples) {
                const auto prior = active.find(*other);
                const auto prior_id = prior->second.begin()->second;
                const auto prior_event = std::ranges::find_if(expirations, [&](const auto& entry) {
                    return entry.second.source_lane == *other && entry.second.id.value == prior_id;
                });
                proposal.report.collisions.push_back({object_id_t{prior_id}, item.id, item.target_lane,
                    prior_event != expirations.end() && prior_event->second.sound_channel_event,
                    item.sound_channel_event});
            }
        }
        auto& lane_intervals = active[item.source_lane.value];
        if (!lane_intervals.empty()) remove_active_lane_max(lanes_by_max_end, active,
                                                             item.source_lane.value);
        lane_intervals.insert({item.range.second, item.id.value});
        lanes_by_max_end[item.range.second].insert(item.source_lane.value);
        expirations.emplace(item.range.second, active_migration_item_t{
            item.source_lane.value, item.id, item.sound_channel_event});
    }
}

auto source_bms_note_channel(const note_t& note) -> std::optional<std::string_view> {
    constexpr std::string_view long_note_prefix{"long-note start "};
    const auto field = std::string_view{note.provenance.source_field};
    if (field.starts_with(long_note_prefix)) {
        const auto channel = field.substr(long_note_prefix.size(), 2);
        if (channel.size() == 2) return channel;
        return std::nullopt;
    }
    if (field.size() >= 8 && field[2] == ' ' && field.substr(3).starts_with("slot "))
        return field.substr(0, 2);
    return std::nullopt;
}

void collect_branch_source_orders(const bms_control_node_t& node, bool inside_branch,
                                  std::unordered_set<std::size_t>& orders) {
    const auto in_branch = inside_branch || is_branch_scope(node.kind);
    if (in_branch && node.kind == bms_control_kind_t::raw_statement)
        orders.insert(node.source_order);
    for (const auto& child : node.children)
        collect_branch_source_orders(child, in_branch, orders);
}

enum class bms_channel_slot_t { visible, invisible, long_note, mine };

auto source_channel_slot(const lane_definition_t& lane, const note_t& note,
                         std::string_view channel) -> std::optional<bms_channel_slot_t> {
    const bool long_note_endpoint = note.provenance.source_field.starts_with("long-note start ");
    const auto matches = [channel](const std::optional<std::string>& value) {
        return value && *value == channel;
    };
    if (long_note_endpoint && matches(lane.bms_channels.long_note))
        return bms_channel_slot_t::long_note;
    if (matches(lane.bms_channels.visible)) return bms_channel_slot_t::visible;
    if (matches(lane.bms_channels.invisible)) return bms_channel_slot_t::invisible;
    if (matches(lane.bms_channels.long_note)) return bms_channel_slot_t::long_note;
    if (matches(lane.bms_channels.mine)) return bms_channel_slot_t::mine;
    return std::nullopt;
}

auto lane_channel(const lane_definition_t& lane, bms_channel_slot_t slot)
    -> const std::optional<std::string>& {
    switch (slot) {
    case bms_channel_slot_t::visible: return lane.bms_channels.visible;
    case bms_channel_slot_t::invisible: return lane.bms_channels.invisible;
    case bms_channel_slot_t::long_note: return lane.bms_channels.long_note;
    case bms_channel_slot_t::mine: return lane.bms_channels.mine;
    }
    return lane.bms_channels.visible;
}

auto is_branch_note_identity_preserved(
    const chart_document_t& chart, const note_t& note,
    const std::unordered_map<std::uint64_t, std::optional<lane_id_t>>& mapping,
    const lane_layout_t& target) -> bool {
    const auto mapped = mapping.find(note.lane.value);
    if (mapped == mapping.end() || !mapped->second) return false;
    const auto* original = active_lane_layout_segment(chart, note.position);
    if (!original) return false;
    const auto source_lane = original->layout.find(note.lane);
    const auto target_lane = target.find(*mapped->second);
    if (source_lane == nullptr || target_lane == nullptr)
        return false;
    const auto source_channel = source_bms_note_channel(note);
    if (!source_channel) return false;
    const auto slot = source_channel_slot(*source_lane, note, *source_channel);
    return slot && lane_channel(*target_lane, *slot) == lane_channel(*source_lane, *slot);
}

auto is_branch_identity_preserved(const chart_document_t& chart,
                               const std::unordered_map<std::uint64_t, std::optional<lane_id_t>>& mapping,
                               const lane_layout_t& target) -> bool {
    if (!chart.bms_control_tree) return true;
    std::unordered_set<std::size_t> branch_orders;
    collect_branch_source_orders(*chart.bms_control_tree, false, branch_orders);
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if ((note.branch || branch_orders.contains(note.provenance.source_order)) &&
                !is_branch_note_identity_preserved(chart, note, mapping, target)) return false;
    return true;
}

auto apply_mapping(const chart_document_t& source,
                   const std::unordered_map<std::uint64_t, std::optional<lane_id_t>>& mapping,
                   const lane_input_profile_t& target) -> chart_document_t {
    auto result = source;
    for (auto& segment : result.lane_layouts) segment.layout = target.layout;
    result.input_profile = target.input;
    std::unordered_set<std::uint64_t> dropped_events;
    for (auto& measure : result.measures) {
        std::erase_if(measure.notes, [&](auto& note) {
            const auto lane = mapping.find(note.lane.value);
            if (lane == mapping.end() || !lane->second) {
                if (note.sound_channel_event) dropped_events.insert(note.sound_channel_event->value);
                return true;
            }
            note.lane = *lane->second;
            return false;
        });
    }
    for (auto& channel : result.sound_channels) {
        std::erase_if(channel.events, [&](auto& event) {
            if (!event.playable) return false;
            if (dropped_events.contains(event.id.value)) return true;
            const auto lane = mapping.find(event.lane.value);
            if (lane == mapping.end() || !lane->second) {
                dropped_events.insert(event.id.value);
                return true;
            }
            event.lane = *lane->second;
            return false;
        });
    }
    return result;
}

} // namespace

auto lane_migration_proposal_t::propose(const song_document_t& song, std::uint64_t expected_revision,
    chart_id_t chart_id, lane_input_profile_t target, std::span<const lane_migration_mapping_t> requested)
    -> lane_migration_proposal_outcome_t {
    auto stamp = chart_stamp_t::from(song, chart_id);
    stamp.document.revision = expected_revision;
    auto captured = revision_policy.validate(stamp.document, song, false);
    if (!captured) {
        lane_migration_proposal_outcome_t result;
        result.diagnostics.push_back(lane_migration_diagnostic_t::from(lane_migration_issue_t::stale_revision,
            std::move(captured.error().diagnostic.message), stamp, {}));
        return result;
    }
    const lane_migration_proposal_t seed;
    const proposal_read_scope_t<chart_stamp_t> scope{song, stamp};
    return seed.revise(scope, std::move(target), requested);
}

auto lane_migration_proposal_t::revise(const proposal_read_scope_t<chart_stamp_t>& scope,
    lane_input_profile_t target, std::span<const lane_migration_mapping_t> requested) const
    -> lane_migration_proposal_outcome_t {
    const auto& song = scope.source_document();
    const auto& stamp = scope.captured_stamp();
    lane_migration_proposal_outcome_t result;
    proposal_t<lane_migration_proposal_t> pending{.stamp = stamp};
    auto& proposal = pending.payload;
    proposal.target = std::move(target);
    const auto chart = song.find_chart(stamp.chart);
    if (chart == nullptr) {
        result.diagnostics.push_back(lane_migration_diagnostic_t::from(lane_migration_issue_t::chart_not_found,
            "The selected difficulty is no longer available.", pending.stamp, std::move(proposal.report)));
        return result;
    }
    if (!has_target_layout_unique_ids(proposal.target.layout)) {
        result.diagnostics.push_back(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_target_profile,
            "The target layout must contain nonzero unique lane identities.", pending.stamp, std::move(proposal.report)));
        return result;
    }
    const auto target_diagnostics = validate_input_profile(proposal.target.layout, proposal.target.input);
    if (!target_diagnostics.empty()) {
        result.diagnostics.push_back(lane_migration_diagnostic_t::from(lane_migration_issue_t::invalid_target_profile,
            target_diagnostics.front().message, pending.stamp, std::move(proposal.report)));
        return result;
    }
    auto mapping = build_mapping(*chart, proposal.target.layout, requested, pending);
    if (!mapping) {
        result.diagnostics.push_back(std::move(mapping.error()));
        return result;
    }
    auto index = chart_measure_index(*chart, pending);
    if (!index) {
        result.diagnostics.push_back(std::move(index.error()));
        return result;
    }
    std::unordered_map<std::uint64_t, std::size_t> source_lanes_by_target;
    for (const auto& [source, lane] : *mapping) {
        static_cast<void>(source);
        if (lane) ++source_lanes_by_target[lane->value];
    }
    std::vector<migration_item_t> items;
    items.reserve(std::accumulate(chart->measures.begin(), chart->measures.end(), std::size_t{},
        [](std::size_t count, const auto& measure) { return count + measure.notes.size(); }));
    auto mapped = append_mapped_note_items(*chart, *index, *mapping, source_lanes_by_target, pending, items);
    if (!mapped) {
        result.diagnostics.push_back(std::move(mapped.error()));
        return result;
    }
    auto linked = is_linked_event_lane_match(*chart, *mapping, pending);
    if (!linked) {
        result.diagnostics.push_back(std::move(linked.error()));
        return result;
    }
    const auto linked_events = collect_linked_event_ids(*chart);
    auto unlinked = append_unlinked_event_items(*chart, *index, *mapping, linked_events, pending, items);
    if (!unlinked) {
        result.diagnostics.push_back(std::move(unlinked.error()));
        return result;
    }
    collect_merge_collisions(items, proposal);
    std::ranges::sort(proposal.report.dropped_notes);
    std::ranges::sort(proposal.report.dropped_sound_channel_events);
    proposal.report.dropped_sound_channel_events.erase(
        std::unique(proposal.report.dropped_sound_channel_events.begin(), proposal.report.dropped_sound_channel_events.end()),
        proposal.report.dropped_sound_channel_events.end());
    proposal.report.changed_bindings = changed_binding_count(chart->input_profile, proposal.target.input);
    if (!is_branch_identity_preserved(*chart, *mapping, proposal.target.layout)) {
        result.diagnostics.push_back(lane_migration_diagnostic_t::from(lane_migration_issue_t::unsupported_branch_identity,
            "The migration cannot preserve a retained BMS branch channel identity.", pending.stamp, std::move(proposal.report)));
        return result;
    }
    proposal.canonical_ = std::make_shared<const lane_migration_proposal_payload_t>(
        lane_migration_proposal_payload_t{pending.stamp, proposal.target, proposal.mapping, proposal.report});
    result.value = std::move(pending);
    return result;
}

auto lane_migration_proposal_t::validate_capture(const chart_stamp_t& stamp) const
    -> proposal_apply_payload_t<lane_migration_issue_t> {
    if (!canonical_ || !is_preview_current(stamp, *this, *canonical_))
        return {.issue = lane_migration_issue_t::invalid_mapping,
                .message = "Lane migration preview data was changed after proposal."};
    return {};
}

auto lane_migration_proposal_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope, bool confirm_drop) const
    -> diagnosed_t<std::optional<std::string>, proposal_apply_payload_t<lane_migration_issue_t>> {
    auto& transaction = scope.transaction();
    const auto& canonical = *canonical_;
    const auto verified = revise(scope, canonical.target, canonical.mapping);
    if (!verified.value || !is_preview_current(verified.value->stamp, verified.value->payload, canonical)) {
        const auto view = lane_migration_preview_view_t::from(verified);
        return {.diagnostics = {
            .issue = view.diagnostic ? view.diagnostic->issue : lane_migration_issue_t::stale_revision,
            .message = view.diagnostic && !view.diagnostic->message.empty()
                ? view.diagnostic->message : stale_action_message(apply_policy.stale_label)}};
    }
    if (!confirm_drop && (!report.dropped_notes.empty() || !report.dropped_sound_channel_events.empty()))
        return {.diagnostics = {.issue = lane_migration_issue_t::drop_confirmation_required,
            .message = "Confirm dropping the listed notes and retained sound events."}};
    if (report.collision_count != 0)
        return {.diagnostics = {.issue = lane_migration_issue_t::lane_collision,
            .message = "The mapping merges notes that overlap in the target lane."}};
    const auto chart = transaction.source_chart(canonical.stamp.chart);
    std::unordered_map<std::uint64_t, std::optional<lane_id_t>> mapping;
    for (const auto& item : canonical.mapping) mapping.emplace(item.source_lane.value, item.target_lane);
    auto& proposed = transaction.document();
    const auto updated = apply_mapping(*chart, mapping, canonical.target);
    *proposed.find_chart(canonical.stamp.chart) = updated;
    return {.value = "Migrate chart lane profile"};
}

} // namespace wormedit
