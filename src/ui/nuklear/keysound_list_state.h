#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

// Selection, search and layer tri-state rules of the Compose keysound listbox.
// Pure state over resource IDs: no widget toolkit and no filesystem, so the
// iBMSC/BMSE-style click model is testable without a window.

struct worm_ui_keysound_selection_t {
    // Sorted ascending, unique.
    std::vector<std::uint64_t> selected;
    // Row last clicked (range anchor) and the row that keyboard and Paint use.
    std::uint64_t anchor{};
    std::uint64_t primary{};
};

enum class worm_ui_keysound_layer_state_t : std::uint8_t { none, some, all };

// Case-insensitive substring match of the BMS slot or the name. An empty
// query matches everything.
[[nodiscard]] auto worm_ui_keysound_matches(std::string_view slot, std::string_view name,
                                            std::string_view query) -> bool;

[[nodiscard]] auto worm_ui_keysound_is_selected(const worm_ui_keysound_selection_t& selection,
                                                std::uint64_t id) -> bool;

// Plain click selects only that row; Ctrl toggles it; Shift selects the span
// from the anchor (Ctrl+Shift adds the span to the selection). `shown` holds
// the IDs of the rows currently listed, in order. An index outside the list or
// a shift click without a listed anchor behaves like a plain click.
void worm_ui_keysound_click(worm_ui_keysound_selection_t& selection,
                            std::span<const std::uint64_t> shown, std::size_t index, bool ctrl,
                            bool shift);

void worm_ui_keysound_select_all(worm_ui_keysound_selection_t& selection,
                                 std::span<const std::uint64_t> shown);
void worm_ui_keysound_clear(worm_ui_keysound_selection_t& selection);

// Drops IDs that are no longer in the catalog (`all` sorted ascending or not).
// The primary row falls back to the first remaining selection, else none.
void worm_ui_keysound_prune(worm_ui_keysound_selection_t& selection,
                            std::span<const std::uint64_t> all);

// State of one layer bit over the selected rows: all, some or none of them
// carry it. `rows` pairs each catalog ID with its layer mask.
struct worm_ui_keysound_mask_t {
    std::uint64_t id{};
    std::uint64_t layer_mask{};
};
[[nodiscard]] auto worm_ui_keysound_layer_state(const worm_ui_keysound_selection_t& selection,
                                                std::span<const worm_ui_keysound_mask_t> rows,
                                                unsigned layer_bit)
    -> worm_ui_keysound_layer_state_t;
