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
    song_id_t song;
    chart_id_t chart;
    std::uint64_t revision{};
    lane_input_profile_t target;
    std::vector<lane_migration_mapping_t> mapping;
    std::vector<object_id_t> dropped_notes;
    std::vector<object_id_t> dropped_sound_channel_events;
    std::vector<lane_migration_collision_t> collisions;
    std::size_t collision_count{};
    std::size_t mapped_note_count{};
    std::size_t remapped_note_count{};
    std::size_t notes_in_merged_lanes{};
    std::size_t changed_bindings{};
};

class lane_migration_proposal_access_t {
  public:
    static void seal(lane_migration_proposal_t& proposal) {
        proposal.canonical_ = std::make_shared<const lane_migration_proposal_payload_t>(
            lane_migration_proposal_payload_t{
                proposal.song, proposal.chart, proposal.revision, proposal.target,
                proposal.mapping, proposal.dropped_notes, proposal.dropped_sound_channel_events,
                proposal.collisions, proposal.collision_count, proposal.mapped_note_count,
                proposal.remapped_note_count,
                proposal.notes_in_merged_lanes, proposal.changed_bindings});
    }
    static auto canonical(const lane_migration_proposal_t& proposal)
        -> std::shared_ptr<const lane_migration_proposal_payload_t> {
        return proposal.canonical_;
    }
};

namespace {

struct source_lane_rank_t {
    std::size_t rank{};
    lane_role_t role{lane_role_t::playable};
    bool initialized{};
    bool ambiguous{};
};

auto fail(lane_migration_proposal_t& proposal, lane_migration_issue_t issue,
          std::string message) -> void {
    proposal.issue = issue;
    proposal.message = std::move(message);
}

auto ordered_lanes(const lane_layout_t& layout) -> std::vector<lane_definition_t> {
    auto lanes = layout.lanes;
    std::ranges::stable_sort(lanes, {}, &lane_definition_t::display_order);
    return lanes;
}

auto target_layout_has_unique_ids(const lane_layout_t& layout) -> bool {
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
                   lane_migration_proposal_t& proposal)
    -> std::unordered_map<std::uint64_t, std::optional<lane_id_t>> {
    std::unordered_map<std::uint64_t, source_lane_rank_t> source_lanes;
    if (!collect_source_lanes(chart, source_lanes)) {
        fail(proposal, lane_migration_issue_t::missing_layout,
             "The chart has no usable lane layout.");
        return {};
    }
    const auto targets = ordered_lanes(target);
    if (targets.empty()) {
        fail(proposal, lane_migration_issue_t::invalid_target_profile,
             "The target profile has no lanes.");
        return {};
    }
    std::unordered_map<std::uint64_t, std::optional<lane_id_t>> mapping;
    if (requested.empty()) {
        for (const auto& [id, lane] : source_lanes) {
            if (lane.ambiguous) {
                fail(proposal, lane_migration_issue_t::ambiguous_default_mapping,
                     "A source lane changes display position or role; provide an explicit mapping.");
                return {};
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
                fail(proposal, lane_migration_issue_t::invalid_mapping,
                     "The explicit mapping contains an unknown or duplicate lane.");
                return {};
            }
            mapping.emplace(item.source_lane.value, item.target_lane);
        }
        if (mapping.size() != source_lanes.size()) {
            fail(proposal, lane_migration_issue_t::invalid_mapping,
                 "The explicit mapping must name every source lane.");
            return {};
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
    const auto begin = absolute_row(index, begin_position);
    const auto end = end_position ? absolute_row(index, *end_position) : begin;
    if (!begin || !end) return std::nullopt;
    return std::pair{*begin, *end};
}

auto binding_equal(const lane_binding_t& left, const lane_binding_t& right) -> bool {
    return left.key == right.key && left.lane == right.lane &&
           left.focused_stage == right.focused_stage;
}

auto changed_binding_count(const input_profile_t& before, const input_profile_t& after)
    -> std::size_t {
    const auto missing = [](const auto& left, const auto& right) {
        return static_cast<std::size_t>(std::ranges::count_if(left, [&](const auto& binding) {
            return std::ranges::none_of(right, [&](const auto& candidate) {
                return binding_equal(binding, candidate);
            });
        }));
    };
    return missing(before.bindings, after.bindings) + missing(after.bindings, before.bindings);
}

auto same_layout(const lane_layout_t& left, const lane_layout_t& right) -> bool {
    if (left.profile_id != right.profile_id || left.display_name != right.display_name ||
        left.split_stage != right.split_stage || left.split_after_column != right.split_after_column ||
        left.bmson_generic_keys_only != right.bmson_generic_keys_only ||
        left.lanes.size() != right.lanes.size()) return false;
    for (std::size_t i = 0; i < left.lanes.size(); ++i) {
        const auto& a = left.lanes[i];
        const auto& b = right.lanes[i];
        if (a.id != b.id || a.name != b.name || a.role != b.role || a.stage != b.stage ||
            a.display_order != b.display_order || a.glyph != b.glyph ||
            a.bms_channels.visible != b.bms_channels.visible ||
            a.bms_channels.invisible != b.bms_channels.invisible ||
            a.bms_channels.long_note != b.bms_channels.long_note ||
            a.bms_channels.mine != b.bms_channels.mine ||
            a.key_color != b.key_color ||
            a.midi_destination.has_value() != b.midi_destination.has_value()) return false;
        if (a.midi_destination &&
            (a.midi_destination->channel != b.midi_destination->channel ||
             a.midi_destination->note != b.midi_destination->note)) return false;
    }
    return true;
}

auto same_input(const input_profile_t& left, const input_profile_t& right) -> bool {
    if (left.profile_id != right.profile_id || left.stage_count != right.stage_count ||
        left.requires_focused_stage != right.requires_focused_stage ||
        left.bindings.size() != right.bindings.size()) return false;
    for (std::size_t i = 0; i < left.bindings.size(); ++i)
        if (!binding_equal(left.bindings[i], right.bindings[i])) return false;
    return true;
}

auto same_target(const lane_input_profile_t& left, const lane_input_profile_t& right) -> bool {
    return same_layout(left.layout, right.layout) && same_input(left.input, right.input);
}

auto same_preview(const lane_migration_proposal_t& proposal,
                  const lane_migration_proposal_payload_t& canonical) -> bool {
    return proposal.song == canonical.song && proposal.chart == canonical.chart &&
           proposal.revision == canonical.revision && same_target(proposal.target, canonical.target) &&
           proposal.mapping == canonical.mapping && proposal.dropped_notes == canonical.dropped_notes &&
           proposal.dropped_sound_channel_events == canonical.dropped_sound_channel_events &&
           proposal.collisions == canonical.collisions &&
           proposal.collision_count == canonical.collision_count &&
           proposal.mapped_note_count == canonical.mapped_note_count &&
           proposal.remapped_note_count == canonical.remapped_note_count &&
           proposal.notes_in_merged_lanes == canonical.notes_in_merged_lanes &&
           proposal.changed_bindings == canonical.changed_bindings;
}

struct migration_item_t {
    object_id_t id;
    bool sound_channel_event{};
    lane_id_t source_lane;
    lane_id_t target_lane;
    std::pair<std::uint64_t, std::uint64_t> range;
};

auto chart_measure_index(const chart_document_t& chart, lane_migration_proposal_t& proposal)
    -> std::optional<measure_index_t> {
    auto built = build_measure_index(chart);
    if (const auto* diagnostics = std::get_if<diagnostic_list_t>(&built)) {
        fail(proposal, lane_migration_issue_t::invalid_mapping,
             diagnostics->empty() ? "The chart measure geometry is invalid."
                                  : diagnostics->front().message);
        return std::nullopt;
    }
    return std::get<measure_index_t>(std::move(built));
}

auto append_mapped_note_items(const chart_document_t& chart, const measure_index_t& index,
                              const std::unordered_map<std::uint64_t,
                                  std::optional<lane_id_t>>& mapping,
                              const std::unordered_map<std::uint64_t, std::size_t>&
                                  source_lanes_by_target,
                              lane_migration_proposal_t& proposal,
                              std::vector<migration_item_t>& items) -> bool {
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            const auto destination = mapping.find(note.lane.value);
            if (destination == mapping.end()) {
                fail(proposal, lane_migration_issue_t::invalid_mapping,
                     "A note refers to a lane missing from the chart layouts.");
                return false;
            }
            if (!destination->second) {
                proposal.dropped_notes.push_back(note.id);
                if (note.sound_channel_event)
                    proposal.dropped_sound_channel_events.push_back(*note.sound_channel_event);
                continue;
            }
            const auto range = absolute_range(index, note.position, note.end);
            if (!range) {
                fail(proposal, lane_migration_issue_t::invalid_mapping,
                     "A note position cannot be resolved in the chart.");
                return false;
            }
            ++proposal.mapped_note_count;
            proposal.remapped_note_count += note.lane != *destination->second ? 1U : 0U;
            proposal.notes_in_merged_lanes +=
                source_lanes_by_target.at(destination->second->value) > 1 ? 1U : 0U;
            items.push_back({note.id, false, note.lane, *destination->second, *range});
        }
    }
    return true;
}

auto collect_linked_event_ids(const chart_document_t& chart)
    -> std::unordered_set<std::uint64_t> {
    std::unordered_set<std::uint64_t> linked;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if (note.sound_channel_event) linked.insert(note.sound_channel_event->value);
    return linked;
}

auto linked_event_lanes_match(const chart_document_t& chart,
                              const std::unordered_map<std::uint64_t,
                                  std::optional<lane_id_t>>& mapping,
                              lane_migration_proposal_t& proposal) -> bool {
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
                fail(proposal, lane_migration_issue_t::invalid_mapping,
                     "A linked semantic note and retained sound event must migrate together.");
                return false;
            }
        }
    }
    return true;
}

auto append_unlinked_event_items(const chart_document_t& chart, const measure_index_t& index,
                                 const std::unordered_map<std::uint64_t,
                                     std::optional<lane_id_t>>& mapping,
                                 const std::unordered_set<std::uint64_t>& linked,
                                 lane_migration_proposal_t& proposal,
                                 std::vector<migration_item_t>& items) -> bool {
    for (const auto& channel : chart.sound_channels) {
        for (const auto& event : channel.events) {
            if (!event.playable || linked.contains(event.id.value)) continue;
            const auto destination = mapping.find(event.lane.value);
            if (destination == mapping.end()) {
                fail(proposal, lane_migration_issue_t::invalid_mapping,
                     "A retained sound event refers to a lane missing from the chart layouts.");
                return false;
            }
            if (!destination->second) {
                proposal.dropped_sound_channel_events.push_back(event.id);
                continue;
            }
            const auto range = absolute_range(index, event.position, event.end);
            if (!range) {
                fail(proposal, lane_migration_issue_t::invalid_mapping,
                     "A retained sound event position cannot be resolved in the chart.");
                return false;
            }
            items.push_back({event.id, true, event.lane, *destination->second, *range});
        }
    }
    return true;
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
            ++proposal.collision_count;
            if (proposal.collisions.size() < maximum_examples) {
                const auto prior = active.find(*other);
                const auto prior_id = prior->second.begin()->second;
                const auto prior_event = std::ranges::find_if(expirations, [&](const auto& entry) {
                    return entry.second.source_lane == *other && entry.second.id.value == prior_id;
                });
                proposal.collisions.push_back({prior_id, item.id, item.target_lane,
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

auto branch_scope_node(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::if_group || kind == bms_control_kind_t::if_branch ||
           kind == bms_control_kind_t::else_if_branch || kind == bms_control_kind_t::else_branch ||
           kind == bms_control_kind_t::switch_block || kind == bms_control_kind_t::case_branch ||
           kind == bms_control_kind_t::default_branch;
}

void collect_branch_source_orders(const bms_control_node_t& node, bool inside_branch,
                                  std::unordered_set<std::size_t>& orders) {
    const auto in_branch = inside_branch || branch_scope_node(node.kind);
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

auto branch_note_identity_preserved(
    const chart_document_t& chart, const note_t& note,
    const std::unordered_map<std::uint64_t, std::optional<lane_id_t>>& mapping,
    const lane_layout_t& target) -> bool {
    const auto mapped = mapping.find(note.lane.value);
    if (mapped == mapping.end() || !mapped->second) return false;
    const auto* original = active_lane_layout_segment(chart, note.position);
    if (!original) return false;
    const auto source_lane = std::ranges::find(original->layout.lanes, note.lane,
                                               &lane_definition_t::id);
    const auto target_lane = std::ranges::find(target.lanes, *mapped->second,
                                               &lane_definition_t::id);
    if (source_lane == original->layout.lanes.end() || target_lane == target.lanes.end())
        return false;
    const auto source_channel = source_bms_note_channel(note);
    if (!source_channel) return false;
    const auto slot = source_channel_slot(*source_lane, note, *source_channel);
    return slot && lane_channel(*target_lane, *slot) == lane_channel(*source_lane, *slot);
}

auto branch_identity_preserved(const chart_document_t& chart,
                               const std::unordered_map<std::uint64_t, std::optional<lane_id_t>>& mapping,
                               const lane_layout_t& target) -> bool {
    if (!chart.bms_control_tree) return true;
    std::unordered_set<std::size_t> branch_orders;
    collect_branch_source_orders(*chart.bms_control_tree, false, branch_orders);
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes)
            if ((note.branch || branch_orders.contains(note.provenance.source_order)) &&
                !branch_note_identity_preserved(chart, note, mapping, target)) return false;
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

auto apply_failure(lane_migration_issue_t issue, std::string message,
                   std::uint64_t revision) -> lane_migration_apply_result_t {
    return {.issue = issue, .message = std::move(message),
            .command = {.revision = revision, .diagnostics = {}}};
}

} // namespace

auto propose_lane_migration(const song_document_t& song, std::uint64_t expected_revision,
                            chart_id_t chart_id, lane_input_profile_t target,
                            std::span<const lane_migration_mapping_t> requested)
    -> lane_migration_proposal_t {
    lane_migration_proposal_t proposal;
    proposal.song = song.id;
    proposal.chart = chart_id;
    proposal.revision = expected_revision;
    proposal.target = std::move(target);
    if (song.revision != expected_revision) {
        fail(proposal, lane_migration_issue_t::stale_revision,
             "The document changed before lane migration preview.");
        return proposal;
    }
    const auto chart = std::ranges::find(song.charts, chart_id, &chart_document_t::id);
    if (chart == song.charts.end()) {
        fail(proposal, lane_migration_issue_t::chart_not_found,
             "The selected difficulty is no longer available.");
        return proposal;
    }
    if (!target_layout_has_unique_ids(proposal.target.layout)) {
        fail(proposal, lane_migration_issue_t::invalid_target_profile,
             "The target layout must contain nonzero unique lane identities.");
        return proposal;
    }
    const auto target_diagnostics = validate_input_profile(proposal.target.layout,
                                                           proposal.target.input);
    if (!target_diagnostics.empty()) {
        fail(proposal, lane_migration_issue_t::invalid_target_profile,
             target_diagnostics.front().message);
        return proposal;
    }
    const auto mapping = build_mapping(*chart, proposal.target.layout, requested, proposal);
    if (proposal.issue != lane_migration_issue_t::none) return proposal;
    const auto index = chart_measure_index(*chart, proposal);
    if (!index) return proposal;
    std::unordered_map<std::uint64_t, std::size_t> source_lanes_by_target;
    for (const auto& [source, lane] : mapping) {
        static_cast<void>(source);
        if (lane) ++source_lanes_by_target[lane->value];
    }
    std::vector<migration_item_t> items;
    items.reserve(std::accumulate(chart->measures.begin(), chart->measures.end(), std::size_t{},
        [](std::size_t count, const auto& measure) { return count + measure.notes.size(); }));
    if (!append_mapped_note_items(*chart, *index, mapping, source_lanes_by_target,
                                  proposal, items)) return proposal;
    if (!linked_event_lanes_match(*chart, mapping, proposal)) return proposal;
    const auto linked_events = collect_linked_event_ids(*chart);
    if (!append_unlinked_event_items(*chart, *index, mapping, linked_events,
                                     proposal, items)) return proposal;
    collect_merge_collisions(items, proposal);
    std::ranges::sort(proposal.dropped_notes);
    std::ranges::sort(proposal.dropped_sound_channel_events);
    proposal.dropped_sound_channel_events.erase(
        std::unique(proposal.dropped_sound_channel_events.begin(),
                    proposal.dropped_sound_channel_events.end()),
        proposal.dropped_sound_channel_events.end());
    proposal.changed_bindings = changed_binding_count(chart->input_profile, proposal.target.input);
    if (!branch_identity_preserved(*chart, mapping, proposal.target.layout))
        fail(proposal, lane_migration_issue_t::unsupported_branch_identity,
             "The migration cannot preserve a retained BMS branch channel identity.");
    if (proposal.issue == lane_migration_issue_t::none)
        lane_migration_proposal_access_t::seal(proposal);
    return proposal;
}

auto apply_lane_migration(document_history_t& history,
                          const lane_migration_proposal_t& proposal,
                          bool confirm_drop) -> lane_migration_apply_result_t {
    if (proposal.issue != lane_migration_issue_t::none)
        return apply_failure(proposal.issue, proposal.message, history.document.revision);
    const auto canonical = lane_migration_proposal_access_t::canonical(proposal);
    if (!canonical || !same_preview(proposal, *canonical))
        return apply_failure(lane_migration_issue_t::invalid_mapping,
                             "Lane migration preview data was changed after proposal.",
                             history.document.revision);
    if (history.document.id != canonical->song || history.document.revision != canonical->revision)
        return apply_failure(lane_migration_issue_t::stale_revision,
                             "The document changed after lane migration preview.",
                             history.document.revision);
    const auto verified = propose_lane_migration(history.document, canonical->revision,
        canonical->chart, canonical->target, canonical->mapping);
    if (!verified || !same_preview(verified, *canonical))
        return apply_failure(verified.issue == lane_migration_issue_t::none
                                 ? lane_migration_issue_t::stale_revision : verified.issue,
                             verified.message.empty() ? "The source chart changed after preview."
                                                      : verified.message,
                             history.document.revision);
    if (!confirm_drop &&
        (!proposal.dropped_notes.empty() || !proposal.dropped_sound_channel_events.empty()))
        return apply_failure(lane_migration_issue_t::drop_confirmation_required,
                             "Confirm dropping the listed notes and retained sound events.",
                             history.document.revision);
    if (proposal.collision_count != 0)
        return apply_failure(lane_migration_issue_t::lane_collision,
                             "The mapping merges notes that overlap in the target lane.",
                             history.document.revision);
    const auto chart = std::ranges::find(history.document.charts, canonical->chart,
                                         &chart_document_t::id);
    std::unordered_map<std::uint64_t, std::optional<lane_id_t>> mapping;
    for (const auto& item : canonical->mapping) mapping.emplace(item.source_lane.value,
                                                              item.target_lane);
    auto proposed = history.document;
    const auto updated = apply_mapping(*chart, mapping, canonical->target);
    *std::ranges::find(proposed.charts, canonical->chart, &chart_document_t::id) = updated;
    auto command = commit_document_command(history, std::move(proposed), "Migrate chart lane profile");
    return {.issue = command ? lane_migration_issue_t::none
                             : lane_migration_issue_t::invalid_mapping,
            .message = command ? std::string{} : "The migrated chart failed document validation.",
            .command = std::move(command)};
}

} // namespace wormedit
