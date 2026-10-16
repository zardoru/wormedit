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
    chart_stamp_t stamp;
    object_id_t segment;
    std::vector<channel_map_replacement_t> replacements;
    channel_map_report_t report;
};

namespace {

constexpr std::size_t maximum_conflict_examples = 64;

auto is_base36_digit(char byte) -> bool {
    return bms36_value(static_cast<unsigned char>(byte)) >= 0;
}

auto is_channel_in_family(std::string_view channel, std::size_t slot) -> bool {
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
    if (channel->size() != 2 || !is_base36_digit((*channel)[0]) || !is_base36_digit((*channel)[1]))
        return false;
    for (auto& character : *channel) character = text_util::ascii_upper(character);
    return is_channel_in_family(*channel, slot);
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

auto export_channel_equal(const std::optional<std::string>& left,
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

auto channel_map_equal(const bms_channel_map_t& left, const bms_channel_map_t& right) -> bool {
    const auto left_slots = map_slots(left);
    const auto right_slots = map_slots(right);
    for (std::size_t index = 0; index < left_slots.size(); ++index)
        if (!export_channel_equal(*left_slots[index], *right_slots[index])) return false;
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
            const auto lane = segment.layout.find(note.lane);
            if (lane == nullptr) continue;
            const auto old_channel = map_slots(lane->bms_channels)[*slot];
            const auto new_channel = map_slots(*replacement->second)[*slot];
            if (export_channel_equal(*old_channel, *new_channel)) continue;
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
            ++proposal.report.conflict_count;
            if (proposal.report.conflicts.size() < maximum_conflict_examples)
                proposal.report.conflicts.push_back({normalized, owner->second, lane.name});
        }
    }
    return true;
}

void collect_branch_orders(const bms_control_node_t& node, bool nested,
                           std::unordered_set<std::size_t>& orders) {
    const bool in_branch = nested || is_branch_scope(node.kind);
    if (in_branch && node.kind == bms_control_kind_t::raw_statement)
        orders.insert(node.source_order);
    for (const auto& child : node.children) collect_branch_orders(child, in_branch, orders);
}

auto is_branch_note(note_kind_t kind) -> bool {
    return kind == note_kind_t::tap || kind == note_kind_t::hold ||
           kind == note_kind_t::roll || kind == note_kind_t::invisible ||
           kind == note_kind_t::mine;
}

auto is_branch_channel_changed(const chart_document_t& chart,
                            const lane_layout_segment_t& segment,
                            const std::unordered_map<std::uint64_t,
                                const bms_channel_map_t*>& replacements) -> bool {
    if (!chart.bms_control_tree) return false;
    std::unordered_set<std::size_t> branch_orders;
    collect_branch_orders(*chart.bms_control_tree, false, branch_orders);
    for (const auto& measure : chart.measures) {
        for (const auto& note : measure.notes) {
            if ((!note.branch && !branch_orders.contains(note.provenance.source_order)) ||
                !is_branch_note(note.kind) || active_lane_layout_segment(chart, note.position) != &segment)
                continue;
            const auto found = replacements.find(note.lane.value);
            const auto lane = segment.layout.find(note.lane);
            if (found == replacements.end() || lane == nullptr) continue;
            const auto has_source_identity = source_channel(note.provenance.source_field).has_value();
            const auto slot = source_note_channel_slot(note, *lane);
            if (has_source_identity && !slot &&
                !channel_map_equal(lane->bms_channels, *found->second)) return true;
            if (slot && !export_channel_equal(*map_slots(lane->bms_channels)[*slot],
                                             *map_slots(*found->second)[*slot])) return true;
        }
    }
    return false;
}

auto is_proposal_current(const chart_stamp_t& stamp, const channel_map_proposal_t& proposal,
                          const channel_map_proposal_payload_t& canonical) -> bool {
    return stamp == canonical.stamp &&
           proposal.layout_segment == canonical.segment &&
           proposal.replacements == canonical.replacements &&
           proposal.report == canonical.report;
}

auto overlay_replacements(lane_layout_t& layout,
                          std::span<const channel_map_replacement_t> replacements) -> bool {
    for (const auto& replacement : replacements) {
        const auto lane = layout.find(replacement.lane);
        if (lane == nullptr) return false;
        lane->bms_channels = replacement.channels;
    }
    return true;
}

} // namespace

auto channel_map_proposal_t::propose(const song_document_t& song,
    std::uint64_t expected_revision, chart_id_t chart_id, object_id_t segment_id,
    std::span<const channel_map_replacement_t> requested) -> channel_map_proposal_outcome_t {
    channel_map_proposal_outcome_t result;
    auto stamp = chart_stamp_t::from(song, chart_id);
    stamp.document.revision = expected_revision;
    auto captured = revision_policy.validate(stamp.document, song, false);
    if (!captured) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::stale_revision,
            std::move(captured.error().diagnostic.message), stamp, {}));
        return result;
    }
    channel_map_proposal_t seed;
    seed.layout_segment = segment_id;
    const proposal_read_scope_t<chart_stamp_t> scope{song, stamp};
    return seed.revise(scope, requested);
}

auto channel_map_proposal_t::revise(const proposal_read_scope_t<chart_stamp_t>& scope,
    std::span<const channel_map_replacement_t> requested) const -> channel_map_proposal_outcome_t {
    const auto& song = scope.source_document();
    const auto& stamp = scope.captured_stamp();
    const auto segment_id = layout_segment;
    channel_map_proposal_outcome_t result;
    channel_map_proposal_t proposal;
    proposal.layout_segment = segment_id;
    const auto chart = song.find_chart(stamp.chart);
    if (chart == nullptr) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::chart_not_found,
            "The selected chart no longer exists.", stamp, std::move(proposal.report)));
        return result;
    }
    if (chart->source_family != format_family_t::bms) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::unsupported_format,
            "Custom BMS channel maps are available only on BMS-family charts.", stamp, std::move(proposal.report)));
        return result;
    }
    const auto segment = find_segment(*chart, segment_id);
    if (segment == chart->lane_layouts.end()) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::layout_segment_not_found,
            "The selected layout segment no longer exists.", stamp, std::move(proposal.report)));
        return result;
    }
    if (requested.empty()) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::invalid_replacement,
            "At least one lane channel map must be replaced.", stamp, std::move(proposal.report)));
        return result;
    }
    proposal.replacements.assign(requested.begin(), requested.end());
    std::ranges::sort(proposal.replacements, {}, &channel_map_replacement_t::lane);
    if (std::ranges::adjacent_find(proposal.replacements, {},
                                   &channel_map_replacement_t::lane) !=
        proposal.replacements.end()) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::invalid_replacement,
            "A lane may appear only once in a channel-map replacement batch.", stamp, std::move(proposal.report)));
        return result;
    }
    for (auto& replacement : proposal.replacements) {
        if (segment->layout.find(replacement.lane) == nullptr) {
            result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::invalid_replacement,
                "A replacement names a lane outside the selected layout segment.", stamp, std::move(proposal.report)));
            return result;
        }
        if (!normalize_map(replacement.channels)) {
            result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::invalid_channel,
                "Channel values must use their BMS category's two-character base-36 range.", stamp, std::move(proposal.report)));
            return result;
        }
    }
    auto candidate = segment->layout;
    if (!overlay_replacements(candidate, proposal.replacements)) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::invalid_replacement,
            "A replacement lane disappeared during preview.", stamp, std::move(proposal.report)));
        return result;
    }
    if (!append_conflicts(candidate, proposal)) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::invalid_channel,
            "Every existing lane channel must use its BMS category's base-36 range.", stamp, std::move(proposal.report)));
        return result;
    }
    const auto replacements = replacement_map(proposal);
    bool missing_export_channel{};
    proposal.report.affected_note_count = count_affected_notes(*chart, *segment, replacements,
                                                        missing_export_channel);
    if (missing_export_channel) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::missing_export_channel,
            "A channel used by an existing note cannot be removed.", stamp, std::move(proposal.report)));
        return result;
    }
    if (is_branch_channel_changed(*chart, *segment, replacements)) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::unsupported_branch_channel,
            "Branch-local BMS notes must retain their source channel identity.", stamp, std::move(proposal.report)));
        return result;
    }
    if (proposal.report.conflict_count != 0) {
        result.diagnostics.push_back(channel_map_diagnostic_t::from(channel_map_issue_t::duplicate_channel,
            "Channel values must be unique across every lane and channel category.", stamp, std::move(proposal.report)));
        return result;
    }
    proposal.canonical_ = std::make_shared<const channel_map_proposal_payload_t>(
        channel_map_proposal_payload_t{stamp, proposal.layout_segment, proposal.replacements, proposal.report});
    result.value = proposal_t<channel_map_proposal_t>{.stamp = stamp, .payload = std::move(proposal)};
    return result;
}

auto channel_map_proposal_t::validate_capture(const chart_stamp_t& stamp) const
    -> proposal_apply_payload_t<channel_map_issue_t> {
    if (!canonical_ || !is_proposal_current(stamp, *this, *canonical_))
        return {.issue = channel_map_issue_t::invalid_replacement,
                .message = "Channel-map preview data changed after proposal."};
    return {};
}

auto channel_map_proposal_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope) const
    -> diagnosed_t<std::optional<std::string>, proposal_apply_payload_t<channel_map_issue_t>> {
    auto& transaction = scope.transaction();
    const auto& canonical = *canonical_;
    const auto verified = revise(scope, canonical.replacements);
    if (!verified.value || !is_proposal_current(verified.value->stamp, verified.value->payload, canonical)) {
        const auto view = channel_map_preview_view_t::from(verified);
        return {.diagnostics = {
            .issue = view.diagnostic ? view.diagnostic->issue : channel_map_issue_t::stale_revision,
            .message = view.diagnostic && !view.diagnostic->message.empty()
                ? view.diagnostic->message : stale_action_message("Channel-map edit")}};
    }
    auto& proposed = transaction.document();
    auto chart = proposed.find_chart(canonical.stamp.chart);
    auto segment = std::ranges::find(chart->lane_layouts, canonical.segment, &lane_layout_segment_t::id);
    if (!overlay_replacements(segment->layout, canonical.replacements))
        return {.diagnostics = {.issue = channel_map_issue_t::layout_segment_not_found,
                               .message = "The selected lane disappeared during apply."}};
    return {.value = "Edit BMS channel map"};
}

} // namespace wormedit
