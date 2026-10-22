#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <wormedit/core/sound_group_proposal.h>
#include "ui/frontend.h"

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

struct worm_ui_keysound_filter_state_t {
    std::array<char, 64> search{};
    std::uint8_t use_filter{}; // 0 All, 1 Used, 2 Unused, 3 Missing.
    bool group_filter_active{};
    bool include_ungrouped{true};
    std::vector<std::uint64_t> group_ids;
    std::vector<std::uint64_t> collapsed_group_ids;
    bool ungrouped_collapsed{};
    std::uint8_t layer_filter_kind{};
    std::uint64_t layer_filter_id{};
    std::uint64_t layer_chart_id{};
};

struct worm_ui_keysound_group_candidate_t {
    std::uint64_t id{};
    std::string name;
};
struct worm_ui_keysound_group_proposal_t {
    std::string name;
    std::uint64_t existing_group_id{};
    bool accepted{true};
    std::vector<std::uint64_t> member_ids;
};
struct worm_ui_keysound_group_assignment_t {
    std::string name;
    std::uint64_t existing_group_id{};
    std::vector<std::uint64_t> sound_ids;
};
struct worm_ui_keysound_group_review_t {
    bool open{};
    bool open_pending{};
    bool preparation_complete{};
    bool manual{};
    bool keep_ids_together{true};
    std::uint64_t target_group_id{};
    std::array<char, 65> manual_group_name{};
    std::uint64_t project_id{};
    std::uint64_t chart_id{};
    std::uint64_t document_revision{};
    std::vector<worm_ui_keysound_group_candidate_t> candidates;
    std::vector<worm_ui_keysound_group_proposal_t> groups;
    std::string message;
};
enum class worm_ui_sound_group_review_action_t : std::uint8_t { none, confirm, cancel };

void worm_ui_keysound_group_review_suggest(
    worm_ui_keysound_group_review_t& review,
    std::span<const worm_ui_keysound_group_candidate_t> candidates,
    std::span<const wormedit::sound_group_t> existing_groups, std::uint64_t project,
    std::uint64_t chart, std::uint64_t revision);
void worm_ui_keysound_group_review_set_accepted(worm_ui_keysound_group_review_t& review,
                                                std::size_t group, bool accepted);
void worm_ui_keysound_group_review_rename(worm_ui_keysound_group_review_t& review,
                                          std::size_t group, std::string name);
void worm_ui_keysound_group_review_move(worm_ui_keysound_group_review_t& review,
                                        std::uint64_t sound_id,
                                        std::optional<std::size_t> group);
void worm_ui_keysound_group_review_leave_all_ungrouped(
    worm_ui_keysound_group_review_t& review);
[[nodiscard]] auto worm_ui_keysound_group_review_confirm(
    worm_ui_keysound_group_review_t& review)
    -> std::vector<worm_ui_keysound_group_assignment_t>;

void worm_ui_keysound_group_review_cancel(worm_ui_keysound_group_review_t& review);

enum class worm_ui_keysound_layer_state_t : std::uint8_t { none, some, all };

// Case-insensitive substring match of the BMS slot or the name. An empty
// query matches everything.
[[nodiscard]] auto worm_ui_keysound_matches(std::string_view slot, std::string_view name,
                                            std::string_view query, std::string_view source_path = {}) -> bool;

[[nodiscard]] auto worm_ui_resource_use_matches(std::uint64_t uses, bool missing,
                                                std::uint8_t filter) -> bool;

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

struct worm_ui_keysound_advance_row_t {
    std::uint64_t id{};
    bool missing{};
};
// The next available row in the dock's filtered order; zero at the end or if absent.
[[nodiscard]] auto next_visible_keysound(std::span<const worm_ui_keysound_advance_row_t> shown,
                                         std::uint64_t placed_id) -> std::uint64_t;
