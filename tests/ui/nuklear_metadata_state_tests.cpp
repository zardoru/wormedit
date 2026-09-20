#include "ui/nuklear/metadata_state.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>

TEST_CASE("Nuklear metadata state resets every retained editing buffer",
          "[unit][frontend][metadata]") {
    auto& state = worm_ui_nuklear_metadata_state();
    state.buffer_revision = 9;
    state.buffer_chart_id = 7;
    state.buffer_has_chart = true;
    state.song_buffers_valid = true;
    state.title[0] = 'x';
    state.search_tags.push_back({});
    state.search_tag_pointers.push_back(state.search_tags.front().data());
    state.selected_format_field = 3;
    state.format_target_valid = true;
    state.format_name.assign(8, 'n');
    state.format_value.assign(8, 'v');

    worm_ui_nuklear_reset_metadata_state();

    CHECK(state.buffer_revision == std::numeric_limits<std::uint64_t>::max());
    CHECK(state.buffer_chart_id == 0);
    CHECK_FALSE(state.buffer_has_chart);
    CHECK_FALSE(state.song_buffers_valid);
    CHECK(state.title[0] == '\0');
    CHECK(state.search_tags.empty());
    CHECK(state.search_tag_pointers.empty());
    CHECK(state.selected_format_field == std::numeric_limits<std::size_t>::max());
    CHECK_FALSE(state.format_target_valid);
    CHECK(state.format_name.empty());
    CHECK(state.format_value.empty());
}
