#include "text_util.h"
#include <wormedit/core/bms36.h>
#include <wormedit/core/channel_map_editor.h>

#include <wormedit/core/command.h>
#include <wormedit/core/lane_profile_switch.h>

#include <algorithm>
#include <array>
#include <map>
#include <memory>
#include <ranges>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace wormedit {

struct channel_map_proposal_payload_t {
    song_id_t song;
    chart_id_t chart;
    object_id_t segment;
    std::uint64_t revision{};
    std::vector<channel_map_replacement_t> replacements;
    std::vector<channel_map_conflict_t> conflicts;
    std::size_t conflict_count{};
    std::size_t affected_note_count{};
};

class channel_map_proposal_access_t {
  public:
    static void seal(channel_map_proposal_t& proposal) {
        proposal.canonical_ = std::make_shared<const channel_map_proposal_payload_t>(
            channel_map_proposal_payload_t{
                proposal.song, proposal.chart, proposal.layout_segment, proposal.revision,
                proposal.replacements, proposal.conflicts, proposal.conflict_count,
                proposal.affected_note_count});
    }

    [[nodiscard]] static auto canonical(const channel_map_proposal_t& proposal)
        -> std::shared_ptr<const channel_map_proposal_payload_t> {
        return proposal.canonical_;
    }
};

namespace {

constexpr std::size_t maximum_conflict_examples = 64;

auto fail(channel_map_proposal_t& proposal, channel_map_issue_t issue, std::string message)
    -> void {
    proposal.issue = issue;
    proposal.message = std::move(message);
}

auto fail_apply(channel_map_issue_t issue, std::string message, std::uint64_t revision)
    -> channel_map_apply_result_t {
    return {.command = {.revision = revision, .diagnostics = {}},
            .payload = {.issue = issue, .message = std::move(message)}};
}

auto base36_digit(char byte) -> bool {
    return bms36_value(static_cast<unsigned char>(byte)) >= 0;
}



auto channel_matches_family(std::string_view channel, std::size_t slot) -> bool {
    if (channel.size() != 2) return false;
    switch (slot) {
    case 0: return (channel[0] == '1' || channel[0] == '2') && channel[1] != '0';
    case 1: return (channel[0] == '3' || channel[0] == '4') && channel[1] != '0';
    case 2: return (channel[0] == '5' || channel[0] == '6') && channel[1] != '0';
    case 3: return (channel[0] == 'D' || channel[0] == 'E') &&
                   channel[1] >= '1' && channel[1] <= '9';
    default: return false;
    }
}

auto normalize_channel(std::optional<std::string>& channel, std::size_t slot) -> bool {
    if (!channel) return true;
    if (channel->size() != 2 || !base36_digit((*channel)[0]) || !base36_digit((*channel)[1]))
        return false;
    for (auto& character : *channel) character = text_util::ascii_upper(character);
    return channel_matches_family(*channel, slot);
}

auto normalize_map(bms_channel_map_t& channels) -> bool {
    std::array<std::optional<std::string>*, 4> slots{
        &channels.visible, &channels.invisible, &channels.long_note, &channels.mine};
    for (std::size_t index = 0; index < slots.size(); ++index)
        if (!normalize_channel(*slots[index], index)) return false;
    return true;
}

auto map_slots(const bms_channel_map_t& channels)
    -> std::array<const std::optional<std::string>*, 4> {
    return {&channels.visible, &channels.invisible, &channels.long_note, &channels.mine};
}



auto same_export_channel(const std::optional<std::string>& left,
                         const std::optional<std::string>& right) -> bool {
    if (!left || !right) return left.has_value() == right.has_value();
    return text_util::ascii_upper(*left) == text_util::ascii_upper(*right);
}

auto replacement_map(const channel_map_proposal_t& proposal)
    -> std::unordered_map<std::uint64_t, const bms_channel_map_t*> {
    std::unordered_map<std::uint64_t, const bms_channel_map_t*> result;
    for (const auto& replacement : proposal.replacements)
        result.emplace(replacement.lane.value, &replacement.channels);
    return result;
}

auto note_channel_slot(note_kind_t kind) -> std::optional<std::size_t> {
    switch (kind) {
    case note_kind_t::tap: return 0;
    case note_kind_t::invisible: return 1;
    case note_kind_t::hold:
    case note_kind_t::roll: return 2;
    case note_kind_t::mine: return 3;
    case note_kind_t::lift:
    case note_kind_t::fake: return std::nullopt;
    }
    return std::nullopt;
}

auto source_channel(std::string_view field) -> std::optional<std::string_view> {
    constexpr std::string_view long_note_prefix{"long-note start "};
    if (field.starts_with(long_note_prefix)) {
        field.remove_prefix(long_note_prefix.size());
    } else if (field.size() < 8 || field[2] != ' ' || !field.substr(3).starts_with("slot ")) {
        return std::nullopt;
    }
    if (field.size() < 2) return std::nullopt;
    return field.substr(0, 2);
}

auto source_note_channel_slot(const note_t& note, const lane_definition_t& lane)
    -> std::optional<std::size_t> {
    if (const auto channel = source_channel(note.provenance.source_field)) {
        const std::array channels{&lane.bms_channels.visible, &lane.bms_channels.invisible,
                                  &lane.bms_channels.long_note, &lane.bms_channels.mine};
        for (std::size_t index = 0; index < channels.size(); ++index)
            if (*channels[index] && text_util::ascii_upper(**channels[index]) == text_util::ascii_upper(std::string{*channel}))
                return index;
        return std::nullopt;
    }
    return note_channel_slot(note.kind);
}

auto same_channel_map(const bms_channel_map_t& left, const bms_channel_map_t& right) -> bool {
    const auto left_slots = map_slots(left);
    const auto right_slots = map_slots(right);
    for (std::size_t index = 0; index < left_slots.size(); ++index)
        if (!same_export_channel(*left_slots[index], *right_slots[index])) return false;
    return true;
}

auto count_affected_notes(const chart_document_t& chart, const lane_layout_segment_t& segment,
                          const std::unordered_map<std::uint64_t,
                              const bms_channel_map_t*>& replacements,
                          bool& missing_export_channel) -> std::size_t {
    std::size_t count{};
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if (active_lane_layout_segment(chart, note.position) != &segment) continue;
            const auto replacement = replacements.find(note.lane.value);
            const auto slot = note_channel_slot(note.kind);
            if (replacement == replacements.end() || !slot) continue;
            const auto lane = std::ranges::find(segment.layout.lanes, note.lane,
                                                &lane_definition_t::id);
            if (lane == segment.layout.lanes.end()) continue;
            const auto old_channel = map_slots(lane->bms_channels)[*slot];
            const auto new_channel = map_slots(*replacement->second)[*slot];
            if (same_export_channel(*old_channel, *new_channel)) continue;
            if (!*new_channel) missing_export_channel = true;
            else ++count;
        }
    }
    return count;
}

auto find_segment(const chart_document_t& chart, object_id_t segment)
    -> std::vector<lane_layout_segment_t>::const_iterator {
    return std::ranges::find(chart.lane_layouts, segment, &lane_layout_segment_t::id);
}

auto append_conflicts(const lane_layout_t& layout, channel_map_proposal_t& proposal) -> bool {
    std::map<std::string, std::string> first_owner;
    for (const auto& lane : layout.lanes) {
        auto checked_map = lane.bms_channels;
        if (!normalize_map(checked_map)) return false;
        const auto slots = map_slots(lane.bms_channels);
        for (const auto* slot : slots) {
            if (!*slot) continue;
            const auto normalized = text_util::ascii_upper(**slot);
            const auto [owner, inserted] = first_owner.emplace(normalized, lane.name);
            if (inserted) continue;
            ++proposal.conflict_count;
            if (proposal.conflicts.size() < maximum_conflict_examples)
                proposal.conflicts.push_back({normalized, owner->second, lane.name});
        }
    }
    return true;
}

auto branch_scope_node(bms_control_kind_t kind) -> bool {
    return kind == bms_control_kind_t::if_group || kind == bms_control_kind_t::if_branch ||
           kind == bms_control_kind_t::else_if_branch || kind == bms_control_kind_t::else_branch ||
           kind == bms_control_kind_t::switch_block || kind == bms_control_kind_t::case_branch ||
           kind == bms_control_kind_t::default_branch;
}

void collect_branch_orders(const bms_control_node_t& node, bool nested,
                           std::unordered_set<std::size_t>& orders) {
    const bool in_branch = nested || branch_scope_node(node.kind);
    if (in_branch && node.kind == bms_control_kind_t::raw_statement)
        orders.insert(node.source_order);
    for (const auto& child : node.children) collect_branch_orders(child, in_branch, orders);
}

auto branch_note(note_kind_t kind) -> bool {
    return kind == note_kind_t::tap || kind == note_kind_t::hold ||
           kind == note_kind_t::roll || kind == note_kind_t::invisible ||
           kind == note_kind_t::mine;
}

auto branch_channel_changed(const chart_document_t& chart,
                            const lane_layout_segment_t& segment,
                            const std::unordered_map<std::uint64_t,
                                const bms_channel_map_t*>& replacements) -> bool {
    if (!chart.bms_control_tree) return false;
    std::unordered_set<std::size_t> branch_orders;
    collect_branch_orders(*chart.bms_control_tree, false, branch_orders);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if ((!note.branch && !branch_orders.contains(note.provenance.source_order)) ||
                !branch_note(note.kind) || active_lane_layout_segment(chart, note.position) != &segment)
                continue;
            const auto found = replacements.find(note.lane.value);
            const auto lane = std::ranges::find(segment.layout.lanes, note.lane,
                                                &lane_definition_t::id);
            if (found == replacements.end() || lane == segment.layout.lanes.end()) continue;
            const auto has_source_identity = source_channel(note.provenance.source_field).has_value();
            const auto slot = source_note_channel_slot(note, *lane);
            if (has_source_identity && !slot &&
                !same_channel_map(lane->bms_channels, *found->second)) return true;
            if (slot && !same_export_channel(*map_slots(lane->bms_channels)[*slot],
                                             *map_slots(*found->second)[*slot])) return true;
        }
    }
    return false;
}

auto same_proposal(const channel_map_proposal_t& proposal,
                   const channel_map_proposal_payload_t& canonical) -> bool {
    return proposal.song == canonical.song && proposal.chart == canonical.chart &&
           proposal.layout_segment == canonical.segment && proposal.revision == canonical.revision &&
           proposal.replacements == canonical.replacements &&
           proposal.conflicts == canonical.conflicts &&
           proposal.conflict_count == canonical.conflict_count &&
           proposal.affected_note_count == canonical.affected_note_count;
}

auto overlay_replacements(lane_layout_t& layout,
                          std::span<const channel_map_replacement_t> replacements) -> bool {
    for (const auto& replacement : replacements) {
        const auto lane = std::ranges::find(layout.lanes, replacement.lane,
                                            &lane_definition_t::id);
        if (lane == layout.lanes.end()) return false;
        lane->bms_channels = replacement.channels;
    }
    return true;
}

auto same_revision_document(const document_history_t& history,
                            const channel_map_proposal_payload_t& canonical) -> bool {
    return history.document.id == canonical.song &&
           history.document.revision == canonical.revision;
}

} // namespace

auto propose_bms_channel_map_edit(const song_document_t& song,
                                  std::uint64_t expected_revision, chart_id_t chart_id,
                                  object_id_t segment_id,
                                  std::span<const channel_map_replacement_t> requested)
    -> channel_map_proposal_t {
    channel_map_proposal_t proposal;
    proposal.song = song.id;
    proposal.chart = chart_id;
    proposal.layout_segment = segment_id;
    proposal.revision = expected_revision;
    if (song.revision != expected_revision) {
        fail(proposal, channel_map_issue_t::stale_revision,
             "The document changed before channel-map preview.");
        return proposal;
    }
    const auto chart = find_chart(song, chart_id);
    if (chart == nullptr) {
        fail(proposal, channel_map_issue_t::chart_not_found, "The selected chart no longer exists.");
        return proposal;
    }
    if (chart->source_family != format_family_t::bms) {
        fail(proposal, channel_map_issue_t::unsupported_format,
             "Custom BMS channel maps are available only on BMS-family charts.");
        return proposal;
    }
    const auto segment = find_segment(*chart, segment_id);
    if (segment == chart->lane_layouts.end()) {
        fail(proposal, channel_map_issue_t::layout_segment_not_found,
             "The selected layout segment no longer exists.");
        return proposal;
    }
    if (requested.empty()) {
        fail(proposal, channel_map_issue_t::invalid_replacement,
             "At least one lane channel map must be replaced.");
        return proposal;
    }
    proposal.replacements.assign(requested.begin(), requested.end());
    std::ranges::sort(proposal.replacements, {}, &channel_map_replacement_t::lane);
    if (std::ranges::adjacent_find(proposal.replacements, {},
                                   &channel_map_replacement_t::lane) !=
        proposal.replacements.end()) {
        fail(proposal, channel_map_issue_t::invalid_replacement,
             "A lane may appear only once in a channel-map replacement batch.");
        return proposal;
    }
    for (auto& replacement : proposal.replacements) {
        if (std::ranges::find(segment->layout.lanes, replacement.lane,
                              &lane_definition_t::id) == segment->layout.lanes.end()) {
            fail(proposal, channel_map_issue_t::invalid_replacement,
                 "A replacement names a lane outside the selected layout segment.");
            return proposal;
        }
        if (!normalize_map(replacement.channels)) {
            fail(proposal, channel_map_issue_t::invalid_channel,
                 "Channel values must use their BMS category's two-character base-36 range.");
            return proposal;
        }
    }
    auto candidate = segment->layout;
    if (!overlay_replacements(candidate, proposal.replacements)) {
        fail(proposal, channel_map_issue_t::invalid_replacement,
             "A replacement lane disappeared during preview.");
        return proposal;
    }
    if (!append_conflicts(candidate, proposal)) {
        fail(proposal, channel_map_issue_t::invalid_channel,
             "Every existing lane channel must use its BMS category's base-36 range.");
        return proposal;
    }
    const auto replacements = replacement_map(proposal);
    bool missing_export_channel{};
    proposal.affected_note_count = count_affected_notes(*chart, *segment, replacements,
                                                        missing_export_channel);
    if (missing_export_channel) {
        fail(proposal, channel_map_issue_t::missing_export_channel,
             "A channel used by an existing note cannot be removed.");
        return proposal;
    }
    if (branch_channel_changed(*chart, *segment, replacements)) {
        fail(proposal, channel_map_issue_t::unsupported_branch_channel,
             "Branch-local BMS notes must retain their source channel identity.");
        return proposal;
    }
    if (proposal.conflict_count != 0) {
        fail(proposal, channel_map_issue_t::duplicate_channel,
             "Channel values must be unique across every lane and channel category.");
        return proposal;
    }
    channel_map_proposal_access_t::seal(proposal);
    return proposal;
}

auto apply_bms_channel_map_edit(document_history_t& history,
                                const channel_map_proposal_t& proposal)
    -> channel_map_apply_result_t {
    if (proposal.issue != channel_map_issue_t::none)
        return fail_apply(proposal.issue, proposal.message, history.document.revision);
    const auto canonical = channel_map_proposal_access_t::canonical(proposal);
    if (!canonical || !same_proposal(proposal, *canonical))
        return fail_apply(channel_map_issue_t::invalid_replacement,
                          "Channel-map preview data changed after proposal.",
                          history.document.revision);
    if (!same_revision_document(history, *canonical))
        return fail_apply(channel_map_issue_t::stale_revision,
                          "The document changed after channel-map preview.",
                          history.document.revision);
    const auto verified = propose_bms_channel_map_edit(
        history.document, canonical->revision, canonical->chart, canonical->segment,
        canonical->replacements);
    if (!verified || !same_proposal(verified, *canonical))
        return fail_apply(verified.issue == channel_map_issue_t::none
                              ? channel_map_issue_t::stale_revision : verified.issue,
                          verified.message.empty() ? "The chart changed after preview."
                                                   : verified.message,
                          history.document.revision);
    auto transaction = document_command_transaction_t::from(history);
    auto& proposed = transaction.copy_document();
    auto chart = std::ranges::find(proposed.charts, canonical->chart, &chart_document_t::id);
    auto segment = std::ranges::find(chart->lane_layouts, canonical->segment,
                                     &lane_layout_segment_t::id);
    if (!overlay_replacements(segment->layout, canonical->replacements))
        return fail_apply(channel_map_issue_t::layout_segment_not_found,
                          "The selected lane disappeared during apply.",
                          history.document.revision);
    auto command = transaction.commit("Edit BMS channel map");
    const auto applied = static_cast<bool>(command);
    return {.command = std::move(command),
            .payload = {.issue = applied ? channel_map_issue_t::none : channel_map_issue_t::invalid_replacement,
                        .message = applied ? std::string{} : "The channel-map edit failed validation."}};
}

} // namespace wormedit
