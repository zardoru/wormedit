#include "ui/nuklear/metadata/metadata_state.h"
#include "ui/nuklear/chrome/text_field.h"

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string_view>

TEST_CASE("Nuklear text fields select their complete value on a double-click",
          "[unit][frontend][metadata]") {
    const auto selection = worm_ui_nuklear_double_click_selection(true, true, 12);
    REQUIRE(selection);
    CHECK(selection->start == 0);
    CHECK(selection->end == 12);
}

TEST_CASE("Nuklear text-field selection ignores non-double-click and multiline input",
          "[unit][frontend][metadata]") {
    CHECK_FALSE(worm_ui_nuklear_double_click_selection(true, false, 12));
    CHECK_FALSE(worm_ui_nuklear_double_click_selection(false, true, 12));
    CHECK_FALSE(worm_ui_nuklear_double_click_selection(true, true, 0));
}

TEST_CASE("Nuklear metadata state resets every retained editing buffer",
          "[unit][frontend][metadata]") {
    auto& state = worm_ui_nuklear_metadata_state();
    state.buffer_revision = 9;
    state.buffer_chart_id = 7;
    state.buffer_has_chart = true;
    state.song_buffers_valid = true;
    state.title[0] = 'x';
    state.search_tags.push_back({});
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
    CHECK(picked->value().path == "/selected.wav");
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

TEST_CASE("Metadata rationals parse exact integers, fractions, decimals, and units",
          "[unit][frontend][metadata]") {
    const auto parse = [](std::string_view text, bool units = false) {
        return worm_ui_nuklear_parse_metadata_rational(text, units);
    };
    CHECK(parse("3") == worm_ui_nuklear_metadata_rational_t{3, 1});
    CHECK(parse("-1/3") == worm_ui_nuklear_metadata_rational_t{-1, 3});
    CHECK(parse("+0.125") == worm_ui_nuklear_metadata_rational_t{125, 1000});
    CHECK(parse("1.50") == worm_ui_nuklear_metadata_rational_t{150, 100});
    CHECK(parse("  -125 ms  ", true) == worm_ui_nuklear_metadata_rational_t{-125, 1000});
    CHECK(parse("12.5ms", true) == worm_ui_nuklear_metadata_rational_t{125, 10000});
    CHECK(parse("0.5 s", true) == worm_ui_nuklear_metadata_rational_t{5, 10});
    CHECK(parse("1 s", false) == std::nullopt);
    CHECK(parse("5 kg", true) == std::nullopt);
}

TEST_CASE("Metadata rational parsing accepts signed limits and rejects malformed overflow",
          "[unit][frontend][metadata]") {
    const auto minimum = std::numeric_limits<std::int64_t>::min();
    const auto maximum = std::numeric_limits<std::int64_t>::max();
    CHECK(worm_ui_nuklear_parse_metadata_rational("-9223372036854775808", false) ==
          worm_ui_nuklear_metadata_rational_t{minimum, 1});
    CHECK(worm_ui_nuklear_parse_metadata_rational("9223372036854775807", false) ==
          worm_ui_nuklear_metadata_rational_t{maximum, 1});
    CHECK(worm_ui_nuklear_parse_metadata_rational("9223372036854775808", false) ==
          std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("-9223372036854775809", false) ==
          std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("1/18446744073709551616", false) ==
          std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("1/18446744073709552ms", true) ==
          std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("1/0", false) == std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("1/-2", false) == std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("1/", false) == std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("/2", false) == std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("1.2.3", false) == std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("--1", false) == std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("", false) == std::nullopt);
    CHECK(worm_ui_nuklear_parse_metadata_rational("1.12345678901234567890", false) ==
          std::nullopt);
}

TEST_CASE("Metadata rational formatting preserves the exact numerator and denominator",
          "[unit][frontend][metadata]") {
    const std::array values{
        worm_ui_nuklear_metadata_rational_t{3, 1},
        worm_ui_nuklear_metadata_rational_t{-125, 1000},
        worm_ui_nuklear_metadata_rational_t{150, 100},
        worm_ui_nuklear_metadata_rational_t{2, 4},
        worm_ui_nuklear_metadata_rational_t{std::numeric_limits<std::int64_t>::min(), 1},
        worm_ui_nuklear_metadata_rational_t{std::numeric_limits<std::int64_t>::min(), 100},
    };
    for (const auto value : values) {
        const auto formatted = worm_ui_nuklear_format_metadata_rational(value);
        CHECK(worm_ui_nuklear_parse_metadata_rational(formatted, false) == value);
    }
    CHECK(worm_ui_nuklear_format_metadata_rational({3, 1}) == "3");
    CHECK(worm_ui_nuklear_format_metadata_rational({-125, 1000}) == "-0.125");
    CHECK(worm_ui_nuklear_format_metadata_rational({150, 100}) == "1.50");
    CHECK(worm_ui_nuklear_format_metadata_rational({2, 4}) == "2/4");
}

TEST_CASE("Metadata rational action text keeps signed and unsigned wire integers exactly",
          "[unit][frontend][metadata]") {
    worm_ui_nuklear_metadata_state_t state;
    struct example_t {
        std::int64_t numerator;
        std::uint64_t denominator;
        std::string_view numerator_text;
        std::string_view denominator_text;
    };
    const std::array examples{
        example_t{0, 1, "0", "1"},
        example_t{-125, 1000, "-125", "1000"},
        example_t{2, 4, "2", "4"},
        example_t{std::numeric_limits<std::int64_t>::min(),
                  std::numeric_limits<std::uint64_t>::max(),
                  "-9223372036854775808", "18446744073709551615"},
        example_t{std::numeric_limits<std::int64_t>::max(), 1,
                  "9223372036854775807", "1"},
    };
    for (const auto& example : examples) {
        const auto wire = state.rational_action_text(example.numerator, example.denominator);
        CHECK(std::string_view{wire.numerator} == example.numerator_text);
        CHECK(std::string_view{wire.denominator} == example.denominator_text);
    }
}

TEST_CASE("Metadata rational action text owns its values after staging storage changes",
          "[unit][frontend][metadata]") {
    worm_ui_nuklear_reset_metadata_state();
    auto& state = worm_ui_nuklear_metadata_state();
    wormedit::ui::workspace_action_t action{};
    {
        const auto wire = state.rational_action_text(-125, 1000);
        action.metadata_audio_offset_numerator_text = wire.numerator;
        action.metadata_audio_offset_denominator_text = wire.denominator;
    }
    // The action owns copies after the temporary pointer pair leaves scope.
    state.chart_name[0] = 'x';
    CHECK(std::string_view{*action.metadata_audio_offset_numerator_text} == "-125");
    CHECK(std::string_view{*action.metadata_audio_offset_denominator_text} == "1000");

    // A later action reuses the arrays and terminates even a shorter replacement.
    const auto next = state.rational_action_text(0, 1);
    CHECK(std::string_view{next.numerator} == "0");
    CHECK(std::string_view{next.denominator} == "1");
    CHECK(action.metadata_audio_offset_numerator_text == "-125");
    CHECK(action.metadata_audio_offset_denominator_text == "1000");
    worm_ui_nuklear_reset_metadata_state();
}

TEST_CASE("Tag grip insertion moves draft strings without losing duplicate or hidden tags",
          "[unit][frontend-state][metadata][tag-grips]") {
    worm_ui_nuklear_reset_metadata_state();
    auto& state = worm_ui_nuklear_metadata_state();
    state.tag_buffers_valid = true;
    state.search_tags.resize(66);
    state.search_tags[0][0] = 'a';
    state.search_tags[1][0] = 'b';
    state.search_tags[65][0] = 'z';
    REQUIRE(worm_ui_nuklear_begin_metadata_tag_drag(0, 10, 10));
    state.tag_drag.moved = true;
    worm_ui_nuklear_target_metadata_tag_drag(2, true);
    REQUIRE(worm_ui_nuklear_finish_metadata_tag_drag());
    CHECK(state.search_tags[0][0] == 'b');
    CHECK(state.search_tags[2][0] == 'a');
    CHECK(state.search_tags[65][0] == 'z');
    CHECK(state.search_tags.size() == 66);
}

TEST_CASE("Tag grips cancel or reject stale and stationary draft drops",
          "[unit][frontend-state][metadata][tag-grips]") {
    worm_ui_nuklear_reset_metadata_state();
    auto& state = worm_ui_nuklear_metadata_state();
    state.tag_buffers_valid = true;
    state.search_tags.resize(3);
    REQUIRE(worm_ui_nuklear_begin_metadata_tag_drag(2, 10, 10));
    worm_ui_nuklear_target_metadata_tag_drag(0, false);
    CHECK_FALSE(worm_ui_nuklear_finish_metadata_tag_drag());
    REQUIRE(worm_ui_nuklear_begin_metadata_tag_drag(2, 10, 10));
    state.tag_drag.moved = true;
    worm_ui_nuklear_target_metadata_tag_drag(0, false);
    worm_ui_nuklear_cancel_metadata_tag_drag();
    CHECK_FALSE(worm_ui_nuklear_finish_metadata_tag_drag());
    CHECK_FALSE(worm_ui_nuklear_begin_metadata_tag_drag(3, 10, 10));
}

TEST_CASE("File dialog cancellation is a present quiet completion", "[unit][frontend][metadata]") {
    wormedit::ui::file_dialog_slot_t slot;
    const auto generation = slot.begin();
    REQUIRE(generation);
    slot.complete(*generation, "/canceled.wav", {}, true);
    const auto result = slot.take();
    REQUIRE(result);
    REQUIRE(*result);
    CHECK(result->value().canceled);
    CHECK(result->value().path == "/canceled.wav");
    CHECK_FALSE(slot.take());
}

TEST_CASE("File dialog failure retains completion metadata and wins over cancellation",
          "[unit][frontend][metadata]") {
    wormedit::ui::file_dialog_slot_t slot;
    const auto generation = slot.begin();
    REQUIRE(generation);
    slot.complete(*generation, "/failed.wav", "Picker failed.", true);
    slot.complete(*generation, "/ignored.wav", {}, false);
    const auto result = slot.take();
    REQUIRE(result);
    REQUIRE_FALSE(*result);
    CHECK(result->error().message == "Picker failed.");
    CHECK(result->error().choice.path == "/failed.wav");
    CHECK(result->error().choice.canceled);
}

namespace {
void check_clean_inheritance_source(const metadata_inheritance_draft_t& draft, const wormedit::ui::metadata_view_t& view) {
    const auto overrides = draft.overrides();
    CHECK_FALSE(overrides.value(wormedit::inherited_metadata_field_t::title));
    CHECK(overrides.value(wormedit::inherited_metadata_field_t::artist) == std::optional<std::string>{""});
    CHECK_FALSE(draft.is_changed(view));
}
}
TEST_CASE("Metadata inheritance draft distinguishes local empty values and returning to inheritance", "[frontend-state][metadata-inheritance]") {
    using field_t = wormedit::inherited_metadata_field_t;
    wormedit::ui::metadata_view_t view{};
    view.document_revision = 7;
    view.chart_id = wormedit::chart_id_t{9};
    view.inheritance = {{.field = field_t::title, .label = "Title", .inherited = "Shared title"},
        {.field = field_t::artist, .label = "Artist", .inherited = "Shared artist", .override_value = ""}};
    metadata_inheritance_draft_t draft;
    draft.synchronize(view);
    check_clean_inheritance_source(draft, view);
    draft.row_for(field_t::title).inherit = false;
    CHECK(draft.overrides().value(field_t::title) == std::optional<std::string>{"Shared title"});
    CHECK(draft.is_changed(view));
    draft.row_for(field_t::artist).inherit = true;
    CHECK_FALSE(draft.overrides().value(field_t::artist));
    draft.reset(view);
    check_clean_inheritance_source(draft, view);
}
TEST_CASE("Metadata inheritance drafts survive frames and reset at chart revision boundaries", "[frontend-state][metadata-inheritance]") {
    using field_t = wormedit::inherited_metadata_field_t;
    wormedit::ui::metadata_view_t view{};
    view.document_revision = 7;
    view.chart_id = wormedit::chart_id_t{9};
    view.inheritance = {{.field = field_t::title, .label = "Title", .inherited = "Shared title"}};
    metadata_inheritance_draft_t draft;
    draft.synchronize(view);
    draft.row_for(field_t::title).inherit = false;
    draft.row_for(field_t::title).text[0] = 'L';
    draft.row_for(field_t::title).text[1] = '\0';
    draft.synchronize(view);
    CHECK(draft.overrides().value(field_t::title) == std::optional<std::string>{"L"});
    ++view.document_revision;
    draft.synchronize(view);
    CHECK(draft.row_for(field_t::title).inherit);
    CHECK(std::string_view{draft.row_for(field_t::title).text.data()} == "Shared title");
    view.chart_id = wormedit::chart_id_t{10};
    view.inheritance.front().inherited.assign(wormedit::default_metadata_edit_limits.maximum_chart_text_bytes + 1, 'x');
    draft.synchronize(view);
    CHECK_FALSE(draft.row_for(field_t::title).can_override);
    CHECK_FALSE(draft.overrides().value(field_t::title));
}

TEST_CASE("Saved audio path refresh preserves unrelated pending metadata edits", "[frontend-state][metadata-inheritance]") {
    using field_t = wormedit::inherited_metadata_field_t;
    wormedit::ui::metadata_view_t view{};
    view.document_revision = 7;
    view.chart_id = wormedit::chart_id_t{9};
    view.inheritance = {{.field = field_t::title, .label = "Title", .inherited = "Shared title"},
        {.field = field_t::music, .label = "Music file", .inherited = "old.ogg", .override_value = "old.ogg"}};
    metadata_inheritance_draft_t draft;
    draft.synchronize(view);
    draft.row_for(field_t::title).inherit = false;
    draft.row_for(field_t::title).text[0] = 'L';
    draft.row_for(field_t::title).text[1] = '\0';
    view.inheritance.back().inherited = "assets/music.ogg";
    view.inheritance.back().override_value = "assets/music.ogg";
    draft.synchronize(view);
    CHECK(draft.overrides().value(field_t::title) == std::optional<std::string>{"L"});
    CHECK(draft.overrides().value(field_t::music) == std::optional<std::string>{"assets/music.ogg"});
    CHECK(draft.is_changed(view));
    draft.row_for(field_t::title).inherit = true;
    CHECK_FALSE(draft.is_changed(view));
}
