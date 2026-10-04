#include "ui/nuklear/keysound_list_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

namespace {

const std::array<std::uint64_t, 6> shown{10, 20, 30, 40, 50, 60};

auto selected_ids(const worm_ui_keysound_selection_t& selection) {
    return selection.selected;
}

using ids = std::vector<std::uint64_t>;

} // namespace

TEST_CASE("keysound search matches slot or name ignoring case", "[unit][frontend][keysounds]") {
    CHECK(worm_ui_keysound_matches("0A", "Kick Drum.wav", ""));
    CHECK(worm_ui_keysound_matches("0A", "Kick Drum.wav", "drum"));
    CHECK(worm_ui_keysound_matches("0A", "Kick Drum.wav", "0a"));
    CHECK_FALSE(worm_ui_keysound_matches("0A", "Kick Drum.wav", "snare"));
    CHECK_FALSE(worm_ui_keysound_matches("", "", "x"));
}

TEST_CASE("keysound plain click selects one row and sets the paint sound",
          "[unit][frontend][keysounds]") {
    worm_ui_keysound_selection_t selection;
    worm_ui_keysound_click(selection, shown, 2, false, false);
    CHECK(selected_ids(selection) == ids{30});
    CHECK(selection.primary == 30);
    worm_ui_keysound_click(selection, shown, 4, false, false);
    CHECK(selected_ids(selection) == ids{50});
    CHECK(selection.anchor == 50);
}

TEST_CASE("keysound ctrl click toggles rows without moving other selections",
          "[unit][frontend][keysounds]") {
    worm_ui_keysound_selection_t selection;
    worm_ui_keysound_click(selection, shown, 4, false, false);
    worm_ui_keysound_click(selection, shown, 1, true, false);
    worm_ui_keysound_click(selection, shown, 3, true, false);
    CHECK(selected_ids(selection) == ids{20, 40, 50});
    CHECK(selection.primary == 40);
    worm_ui_keysound_click(selection, shown, 1, true, false);
    CHECK(selected_ids(selection) == ids{40, 50});
}

TEST_CASE("keysound shift click selects the span from the anchor in either direction",
          "[unit][frontend][keysounds]") {
    worm_ui_keysound_selection_t selection;
    worm_ui_keysound_click(selection, shown, 1, false, false);
    worm_ui_keysound_click(selection, shown, 3, false, true);
    CHECK(selected_ids(selection) == ids{20, 30, 40});
    CHECK(selection.anchor == 20);
    worm_ui_keysound_click(selection, shown, 0, false, true);
    CHECK(selected_ids(selection) == ids{10, 20});
    // Ctrl+Shift adds the span instead of replacing the selection.
    worm_ui_keysound_click(selection, shown, 4, true, true);
    CHECK(selected_ids(selection) == ids{10, 20, 30, 40, 50});
}

TEST_CASE("keysound shift click without a listed anchor acts like a plain click",
          "[unit][frontend][keysounds]") {
    worm_ui_keysound_selection_t selection;
    selection.anchor = 999;
    worm_ui_keysound_click(selection, shown, 2, false, true);
    CHECK(selected_ids(selection) == ids{30});
    CHECK(selection.anchor == 30);
    const auto before = selection;
    worm_ui_keysound_click(selection, shown, 99, false, false);
    CHECK(selected_ids(selection) == before.selected);
}

TEST_CASE("keysound select all clear and prune keep the selection consistent",
          "[unit][frontend][keysounds]") {
    worm_ui_keysound_selection_t selection;
    worm_ui_keysound_select_all(selection, shown);
    CHECK(selection.selected.size() == 6);
    CHECK(selection.primary == 10);
    worm_ui_keysound_click(selection, shown, 4, false, false);
    worm_ui_keysound_click(selection, shown, 2, true, false);
    const std::array<std::uint64_t, 3> remaining{60, 30, 10};
    worm_ui_keysound_prune(selection, remaining);
    CHECK(selected_ids(selection) == ids{30});
    CHECK(selection.anchor == 30);
    CHECK(selection.primary == 30);
    worm_ui_keysound_clear(selection);
    CHECK(selection.selected.empty());
    CHECK(selection.primary == 0);
}

TEST_CASE("keysound layer state is none some or all across the selection",
          "[unit][frontend][keysounds]") {
    const std::array<worm_ui_keysound_mask_t, 4> rows{{{10, 0b01}, {20, 0b11}, {30, 0b00}, {40, 0b10}}};
    worm_ui_keysound_selection_t selection;
    CHECK(worm_ui_keysound_layer_state(selection, rows, 0) == worm_ui_keysound_layer_state_t::none);
    selection.selected = {10, 20};
    CHECK(worm_ui_keysound_layer_state(selection, rows, 0) == worm_ui_keysound_layer_state_t::all);
    CHECK(worm_ui_keysound_layer_state(selection, rows, 1) == worm_ui_keysound_layer_state_t::some);
    selection.selected = {30};
    CHECK(worm_ui_keysound_layer_state(selection, rows, 0) == worm_ui_keysound_layer_state_t::none);
    CHECK(worm_ui_keysound_layer_state(selection, rows, 64) == worm_ui_keysound_layer_state_t::none);
}

TEST_CASE("Keysound advance follows filtered order skips missing rows and stops", "[frontend-state][keysounds]") {
    const std::array<worm_ui_keysound_advance_row_t, 4> rows{{{40, false}, {10, true}, {30, false}, {20, false}}};
    CHECK(next_visible_keysound(rows, 40) == 30);
    CHECK(next_visible_keysound(rows, 30) == 20);
    CHECK(next_visible_keysound(rows, 20) == 0);
    CHECK(next_visible_keysound(rows, 99) == 0);
    CHECK(next_visible_keysound({}, 40) == 0);
}
