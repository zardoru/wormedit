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
    state.external_music_path[0] = 'a';
    state.external_music_error[0] = 'e';

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
    CHECK(state.external_music_path[0] == '\0');
    CHECK(state.external_music_error[0] == '\0');
}

TEST_CASE("Metadata file picker discards stale callbacks and reset generations",
          "[unit][frontend][metadata]") {
    worm_ui_nuklear_reset_metadata_state();
    const auto first = worm_ui_nuklear_metadata_file_dialog_begin();
    REQUIRE(first);
    CHECK(worm_ui_nuklear_metadata_file_dialog_open());
    CHECK_FALSE(worm_ui_nuklear_metadata_file_dialog_begin());
    worm_ui_nuklear_metadata_file_dialog_complete(*first - 1, "/stale.wav", {}, false);
    CHECK(worm_ui_nuklear_metadata_file_dialog_open());
    worm_ui_nuklear_metadata_file_dialog_complete(*first, "/selected.wav", {}, false);
    CHECK_FALSE(worm_ui_nuklear_metadata_file_dialog_open());
    const auto picked = worm_ui_nuklear_metadata_file_dialog_take();
    REQUIRE(picked);
    CHECK(picked->path == "/selected.wav");
    CHECK_FALSE(worm_ui_nuklear_metadata_file_dialog_take());

    const auto second = worm_ui_nuklear_metadata_file_dialog_begin();
    REQUIRE(second);
    worm_ui_nuklear_reset_metadata_state();
    worm_ui_nuklear_metadata_file_dialog_complete(*second, "/late.wav", {}, false);
    CHECK_FALSE(worm_ui_nuklear_metadata_file_dialog_open());
    CHECK_FALSE(worm_ui_nuklear_metadata_file_dialog_take());
}

TEST_CASE("Metadata drop targets keep the selected output group and visible bounds",
          "[unit][frontend][metadata]") {
    worm_ui_nuklear_reset_metadata_state();
    worm_ui_nuklear_set_metadata_drop_target({WORM_UI_METADATA_DROP_PROJECT, 0},
                                             {.x = 10, .y = 20, .width = 100, .height = 25});
    worm_ui_nuklear_set_metadata_drop_target({WORM_UI_METADATA_DROP_EXPORT, 7},
                                             {.x = 10, .y = 60, .width = 100, .height = 25});
    worm_ui_nuklear_set_metadata_drop_target({WORM_UI_METADATA_DROP_OUTPUT_PATH, 9},
                                             {.x = 10, .y = 100, .width = 100, .height = 25});
    CHECK(worm_ui_nuklear_metadata_drop_target(10, 20).destination ==
          WORM_UI_METADATA_DROP_PROJECT);
    CHECK(worm_ui_nuklear_metadata_drop_target(109, 44).destination ==
          WORM_UI_METADATA_DROP_PROJECT);
    const auto output = worm_ui_nuklear_metadata_drop_target(50, 70);
    CHECK(output.destination == WORM_UI_METADATA_DROP_EXPORT);
    CHECK(output.output_group_index == 7);
    const auto output_path = worm_ui_nuklear_metadata_drop_target(50, 110);
    CHECK(output_path.destination == WORM_UI_METADATA_DROP_OUTPUT_PATH);
    CHECK(output_path.output_group_index == 9);
    CHECK(worm_ui_nuklear_metadata_drop_target(110, 70).destination == WORM_UI_METADATA_DROP_NONE);
    CHECK(worm_ui_nuklear_metadata_drop_target(50, 85).destination == WORM_UI_METADATA_DROP_NONE);
    worm_ui_nuklear_clear_metadata_drop_targets();
    CHECK(worm_ui_nuklear_metadata_drop_target(50, 70).destination == WORM_UI_METADATA_DROP_NONE);
    CHECK(worm_ui_nuklear_metadata_drop_target(50, 110).destination == WORM_UI_METADATA_DROP_NONE);
}
