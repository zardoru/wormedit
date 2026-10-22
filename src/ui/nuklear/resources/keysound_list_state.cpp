#include "ui/nuklear/resources/keysound_list_state.h"

#include "core/text_util.h"

#include <algorithm>
#include <unordered_set>

namespace {

auto contains_folded(std::string_view text, std::string_view query) -> bool {
    const auto found = std::ranges::search(text, query, [](char a, char b) {
        return wormedit::text_util::ascii_iequal(a, b);
    });
    return !found.empty();
}

void insert_sorted(std::vector<std::uint64_t>& ids, std::uint64_t id) {
    const auto position = std::ranges::lower_bound(ids, id);
    if (position == ids.end() || *position != id) ids.insert(position, id);
}

void erase_sorted(std::vector<std::uint64_t>& ids, std::uint64_t id) {
    const auto position = std::ranges::lower_bound(ids, id);
    if (position != ids.end() && *position == id) ids.erase(position);
}

auto index_of(std::span<const std::uint64_t> shown, std::uint64_t id) -> std::size_t {
    if (id == 0) return shown.size();
    const auto found = std::ranges::find(shown, id);
    return static_cast<std::size_t>(found - shown.begin());
}

void select_span(worm_ui_keysound_selection_t& selection, std::span<const std::uint64_t> shown,
                 std::size_t first, std::size_t last) {
    for (auto index = std::min(first, last); index <= std::max(first, last); ++index)
        insert_sorted(selection.selected, shown[index]);
}

} // namespace

auto worm_ui_keysound_matches(std::string_view slot, std::string_view name,
                              std::string_view query, std::string_view source_path) -> bool {
    return query.empty() || contains_folded(slot, query) || contains_folded(name, query) ||
           contains_folded(source_path, query);
}

auto worm_ui_resource_use_matches(std::uint64_t uses, bool missing, std::uint8_t filter) -> bool {
    switch (filter) {
    case 1: return uses != 0;
    case 2: return uses == 0;
    case 3: return missing;
    default: return true;
    }
}

auto worm_ui_keysound_is_selected(const worm_ui_keysound_selection_t& selection,
                                  std::uint64_t id) -> bool {
    return std::ranges::binary_search(selection.selected, id);
}

void worm_ui_keysound_click(worm_ui_keysound_selection_t& selection,
                            std::span<const std::uint64_t> shown, std::size_t index, bool ctrl,
                            bool shift) {
    if (index >= shown.size()) return;
    const auto id = shown[index];
    const auto anchor_index = index_of(shown, selection.anchor);
    if (shift && anchor_index < shown.size()) {
        if (!ctrl) selection.selected.clear();
        select_span(selection, shown, anchor_index, index);
        selection.primary = id;
        return;
    }
    if (ctrl) {
        if (worm_ui_keysound_is_selected(selection, id)) erase_sorted(selection.selected, id);
        else insert_sorted(selection.selected, id);
    } else {
        selection.selected.assign(1, id);
    }
    selection.anchor = id;
    selection.primary = id;
}

void worm_ui_keysound_select_all(worm_ui_keysound_selection_t& selection,
                                 std::span<const std::uint64_t> shown) {
    selection.selected.assign(shown.begin(), shown.end());
    std::ranges::sort(selection.selected);
    if (!shown.empty() && selection.primary == 0) selection.primary = shown.front();
}

void worm_ui_keysound_clear(worm_ui_keysound_selection_t& selection) {
    selection.selected.clear();
    selection.anchor = 0;
    selection.primary = 0;
}

void worm_ui_keysound_prune(worm_ui_keysound_selection_t& selection,
                            std::span<const std::uint64_t> all) {
    std::vector<std::uint64_t> known{all.begin(), all.end()};
    std::ranges::sort(known);
    std::erase_if(selection.selected,
                  [&](std::uint64_t id) { return !std::ranges::binary_search(known, id); });
    if (!std::ranges::binary_search(known, selection.anchor)) selection.anchor = 0;
    if (!std::ranges::binary_search(known, selection.primary))
        selection.primary = selection.selected.empty() ? 0 : selection.selected.front();
}

auto worm_ui_keysound_layer_state(const worm_ui_keysound_selection_t& selection,
                                  std::span<const worm_ui_keysound_mask_t> rows,
                                  unsigned layer_bit) -> worm_ui_keysound_layer_state_t {
    if (layer_bit >= 64) return worm_ui_keysound_layer_state_t::none;
    std::size_t with{};
    std::size_t counted{};
    for (const auto& row : rows) {
        if (!worm_ui_keysound_is_selected(selection, row.id)) continue;
        ++counted;
        with += (row.layer_mask >> layer_bit) & 1U;
    }
    if (counted == 0 || with == 0) return worm_ui_keysound_layer_state_t::none;
    return with == counted ? worm_ui_keysound_layer_state_t::all
                           : worm_ui_keysound_layer_state_t::some;
}

auto next_visible_keysound(std::span<const worm_ui_keysound_advance_row_t> shown,
                           std::uint64_t placed_id) -> std::uint64_t {
    const auto current = std::ranges::find(shown, placed_id, &worm_ui_keysound_advance_row_t::id);
    if (current == shown.end()) return 0;
    const auto next = std::find_if(current + 1, shown.end(), [](const auto& row) { return !row.missing; });
    return next == shown.end() ? 0 : next->id;
}

void worm_ui_keysound_group_review_suggest(
    worm_ui_keysound_group_review_t& review,
    std::span<const worm_ui_keysound_group_candidate_t> candidates,
    std::span<const wormedit::sound_group_t> existing_groups, std::uint64_t project,
    std::uint64_t chart, std::uint64_t revision) {
    review = {};
    review.project_id = project;
    review.chart_id = chart;
    review.document_revision = revision;
    review.candidates.assign(candidates.begin(), candidates.end());
    std::vector<std::string> names;
    names.reserve(candidates.size());
    for (const auto& candidate : candidates) names.push_back(candidate.name);
    const auto groups = std::vector<wormedit::sound_group_t>{existing_groups.begin(),
                                                              existing_groups.end()};
    const auto proposals = wormedit::propose_sound_groups(names, groups);
    std::unordered_set<std::uint64_t> used;
    for (const auto& proposal : proposals.groups) {
        worm_ui_keysound_group_proposal_t group{.name = proposal.name,
            .existing_group_id = proposal.existing_group ? proposal.existing_group->value : 0};
        for (const auto& member : proposal.members) {
            const auto candidate = std::ranges::find_if(candidates, [&](const auto& row) {
                return row.name == member && !used.contains(row.id);
            });
            if (candidate == candidates.end()) continue;
            group.member_ids.push_back(candidate->id);
            used.insert(candidate->id);
        }
        if (!group.member_ids.empty()) review.groups.push_back(std::move(group));
    }
    review.open = !review.groups.empty();
    if (!review.open) review.message = "No filename groups were suggested for the selected sounds.";
}

void worm_ui_keysound_group_review_set_accepted(worm_ui_keysound_group_review_t& review,
                                                std::size_t group, bool accepted) {
    if (group < review.groups.size()) review.groups[group].accepted = accepted;
}

void worm_ui_keysound_group_review_rename(worm_ui_keysound_group_review_t& review,
                                          std::size_t group, std::string name) {
    if (group >= review.groups.size()) return;
    if (name.size() > 64) name.resize(64);
    review.groups[group].name = std::move(name);
}

void worm_ui_keysound_group_review_move(worm_ui_keysound_group_review_t& review,
                                        std::uint64_t sound_id,
                                        std::optional<std::size_t> group) {
    if (group && *group >= review.groups.size()) return;
    for (auto& row : review.groups)
        std::erase(row.member_ids, sound_id);
    if (group) review.groups[*group].member_ids.push_back(sound_id);
}

void worm_ui_keysound_group_review_leave_all_ungrouped(
    worm_ui_keysound_group_review_t& review) {
    for (auto& group : review.groups) group.accepted = false;
    for (auto& group : review.groups) group.member_ids.clear();
}

auto worm_ui_keysound_group_review_confirm(worm_ui_keysound_group_review_t& review)
    -> std::vector<worm_ui_keysound_group_assignment_t> {
    std::vector<worm_ui_keysound_group_assignment_t> assignments;
    review.message.clear();
    for (const auto& group : review.groups) {
        if (!group.accepted || group.member_ids.empty()) continue;
        auto name = std::string(wormedit::text_util::trim(
            group.name, wormedit::text_util::ascii_whitespace));
        if (name.empty() || !wormedit::text_util::is_valid_bounded_text(name, 64)) {
            review.message = "Group names must contain 1 to 64 bytes.";
            return {};
        }
        assignments.push_back({.name = std::move(name),
                               .existing_group_id = group.existing_group_id,
                               .sound_ids = group.member_ids});
    }
    return assignments;
}

void worm_ui_keysound_group_review_cancel(worm_ui_keysound_group_review_t& review) {
    review = {};
}
