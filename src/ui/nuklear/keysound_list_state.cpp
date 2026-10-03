#include "ui/nuklear/keysound_list_state.h"

#include <algorithm>
#include <cctype>

namespace {

auto fold(char value) -> char {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

auto contains_folded(std::string_view text, std::string_view query) -> bool {
    const auto found = std::ranges::search(text, query, [](char a, char b) {
        return fold(a) == fold(b);
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
                              std::string_view query) -> bool {
    return query.empty() || contains_folded(slot, query) || contains_folded(name, query);
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
