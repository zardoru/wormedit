#include "ui/nuklear/sound_import_state.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

namespace {

using phase_t = worm_ui_sound_import_phase_t;

auto paths(std::initializer_list<const char*> values) -> std::vector<std::string> {
    return {values.begin(), values.end()};
}

} // namespace

TEST_CASE("one wav or ogg without BGM asks stem or sound", "[unit][frontend][sounds]") {
    for (const char* file : {"/music/Drums.WAV", "/music/vox.ogg", "/music/pad.flac"}) {
        worm_ui_sound_import_state_t state;
        const auto input = paths({file});
        CHECK(worm_ui_nuklear_sound_import_add(state, input, false) == 1);
        CHECK(state.phase == phase_t::choose_kind);
        CHECK(state.items.front().kind == WORM_UI_SOUND_IMPORT_STEM);
    }
}

TEST_CASE("one mp3, or any single file when the chart has BGM, imports without a dialog",
          "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t mp3;
    const auto song = paths({"/music/song.mp3"});
    worm_ui_nuklear_sound_import_add(mp3, song, false);
    CHECK(mp3.phase == phase_t::ready);
    CHECK(mp3.items.front().kind == WORM_UI_SOUND_IMPORT_KEYSOUND);

    worm_ui_sound_import_state_t with_bgm;
    const auto hit = paths({"/music/hit.wav"});
    worm_ui_nuklear_sound_import_add(with_bgm, hit, true);
    CHECK(with_bgm.phase == phase_t::ready);
    CHECK(with_bgm.items.front().kind == WORM_UI_SOUND_IMPORT_KEYSOUND);
}

TEST_CASE("several files always show the list with stems as the default only without BGM",
          "[unit][frontend][sounds]") {
    const auto input = paths({"/s/a.wav", "/s/b.ogg", "/s/c.mp3"});
    worm_ui_sound_import_state_t state;
    CHECK(worm_ui_nuklear_sound_import_add(state, input, false) == 3);
    CHECK(state.phase == phase_t::review_list);
    CHECK(state.items[0].kind == WORM_UI_SOUND_IMPORT_STEM);
    CHECK(state.items[2].kind == WORM_UI_SOUND_IMPORT_KEYSOUND);
    CHECK(state.items[0].name == "a");

    worm_ui_sound_import_state_t with_bgm;
    worm_ui_nuklear_sound_import_add(with_bgm, input, true);
    CHECK(with_bgm.phase == phase_t::review_list);
    CHECK(with_bgm.items[0].kind == WORM_UI_SOUND_IMPORT_KEYSOUND);
}

TEST_CASE("non-audio files and duplicates are skipped with a message", "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t state;
    const auto first = paths({"/s/a.wav", "/s/readme.txt", "/s/cover.png"});
    CHECK(worm_ui_nuklear_sound_import_add(state, first, true) == 1);
    CHECK(state.message.find("2 file(s) skipped") != std::string::npos);
    const auto again = paths({"/s/a.wav"});
    CHECK(worm_ui_nuklear_sound_import_add(state, again, true) == 0);
    CHECK(state.message.find("already in the list") != std::string::npos);
    CHECK(state.items.size() == 1);

    worm_ui_sound_import_state_t none;
    const auto junk = paths({"/s/readme.txt"});
    CHECK(worm_ui_nuklear_sound_import_add(none, junk, true) == 0);
    CHECK(none.phase == phase_t::closed);
}

TEST_CASE("over-capacity imports remain reviewable and cannot be partially confirmed",
          "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t state;
    std::vector<std::string> many;
    for (int index = 0; index < 1'300; ++index) many.push_back("/s/f" + std::to_string(index) + ".wav");
    CHECK(worm_ui_nuklear_sound_import_add(state, many, true) == many.size());
    CHECK(state.items.size() == many.size());
    CHECK(state.message.find("maximum is 1295") != std::string::npos);
    CHECK_FALSE(worm_ui_nuklear_sound_import_confirm(state).has_value());
    CHECK(state.items.size() == many.size());
    CHECK(state.message.find("exceeds the 1295-file limit") != std::string::npos);
    for (std::size_t index = 0; index < 5; ++index)
        worm_ui_nuklear_sound_import_remove(state, state.items.size() - 1);
    CHECK(state.items.size() == worm_ui_sound_import_capacity);
    CHECK(worm_ui_nuklear_sound_import_confirm(state).has_value());
}

TEST_CASE("list edits keep order and ignore bad input", "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t state;
    const auto input = paths({"/s/a.wav", "/s/b.wav", "/s/c.wav"});
    worm_ui_nuklear_sound_import_add(state, input, true);
    worm_ui_nuklear_sound_import_move(state, 0, -1);
    worm_ui_nuklear_sound_import_move(state, 2, 1);
    worm_ui_nuklear_sound_import_move(state, 7, 1);
    CHECK(state.items[0].name == "a");
    worm_ui_nuklear_sound_import_move(state, 0, 1);
    CHECK(state.items[0].name == "b");
    CHECK(state.items[1].name == "a");
    worm_ui_nuklear_sound_import_set_kind(state, 1, WORM_UI_SOUND_IMPORT_SAMPLE);
    worm_ui_nuklear_sound_import_set_kind(state, 1, 9);
    worm_ui_nuklear_sound_import_set_kind(state, 99, WORM_UI_SOUND_IMPORT_STEM);
    CHECK(state.items[1].kind == WORM_UI_SOUND_IMPORT_SAMPLE);
    worm_ui_nuklear_sound_import_set_all_kinds(state, WORM_UI_SOUND_IMPORT_STEM);
    CHECK(state.items[2].kind == WORM_UI_SOUND_IMPORT_STEM);
    worm_ui_nuklear_sound_import_remove(state, 0);
    CHECK(state.items.size() == 2);
    CHECK(state.phase == phase_t::review_list);
}

TEST_CASE("confirm snapshots the list and retains it until commit success", "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t state;
    const auto input = paths({"/s/a.wav", "/s/b.ogg"});
    worm_ui_nuklear_sound_import_add(state, input, false);
    worm_ui_nuklear_sound_import_rename(state, 0, "  Kick  ");
    const auto taken = worm_ui_nuklear_sound_import_confirm(state);
    REQUIRE(taken.has_value());
    REQUIRE(taken->items.size() == 2);
    CHECK(taken->items[0].name == "Kick");
    CHECK(taken->items[0].path == "/s/a.wav");
    CHECK(taken->items[1].name == "b");
    CHECK(state.items.size() == 2);
    worm_ui_nuklear_sound_import_commit_failed(state, "revision changed");
    CHECK(state.phase == phase_t::review_list);
    CHECK(state.message == "revision changed");
    worm_ui_nuklear_sound_import_commit_succeeded(state);
    CHECK(state.phase == phase_t::closed);
    CHECK(state.items.empty());
}

TEST_CASE("confirm refuses empty names and cancel clears everything", "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t state;
    const auto input = paths({"/s/a.wav", "/s/b.wav"});
    worm_ui_nuklear_sound_import_add(state, input, false);
    worm_ui_nuklear_sound_import_rename(state, 1, "   ");
    CHECK_FALSE(worm_ui_nuklear_sound_import_confirm(state).has_value());
    CHECK(state.phase == phase_t::review_list);
    CHECK(state.items.size() == 2);
    CHECK(state.message == "Every sound needs a name.");
    worm_ui_nuklear_sound_import_cancel(state);
    CHECK(state.phase == phase_t::closed);
    CHECK(state.items.empty());
    CHECK(state.message.empty());
}

TEST_CASE("group proposals accept edits, member moves, and leave all ungrouped") {
    worm_ui_sound_import_state_t state;
    const auto input = paths({"/s/kick_01.wav", "/s/kick_02.wav", "/s/loose.wav"});
    worm_ui_nuklear_sound_import_add(state, input, true);
    const std::array existing{worm_ui_sound_import_existing_group_t{
        .id = wormedit::sound_group_id_t{21}, .name = "Drums"}};
    worm_ui_nuklear_sound_import_set_target(state, 5, 6, 7);
    worm_ui_nuklear_sound_import_suggest_groups(state, existing);
    REQUIRE(state.phase == phase_t::review_groups);
    REQUIRE(state.groups.size() == 1);
    CHECK(state.groups[0].name == "kick");
    CHECK(state.items[0].group_index == 0);

    worm_ui_nuklear_sound_import_rename_group(state, 0, " Kicks ");
    const auto snapshot = worm_ui_nuklear_sound_import_confirm(state);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->groups.size() == 1);
    CHECK(snapshot->groups[0].name == "Kicks");
    CHECK(snapshot->groups[0].member_indexes == std::vector<std::size_t>{0, 1});
    CHECK(state.project_id == 5);
    CHECK(state.chart_id == 6);
    CHECK(state.document_revision == 7);

    worm_ui_nuklear_sound_import_move_member(state, 0, std::nullopt);
    worm_ui_nuklear_sound_import_move_member(state, 2, 0);
    worm_ui_nuklear_sound_import_set_group_accepted(state, 0, false);
    CHECK_FALSE(worm_ui_nuklear_sound_import_confirm(state)->groups.size());
    worm_ui_nuklear_sound_import_leave_all_ungrouped(state);
    CHECK_FALSE(state.items[1].group_index.has_value());
    CHECK_FALSE(state.groups[0].accepted);
    const auto ungrouped = worm_ui_nuklear_sound_import_confirm(state);
    REQUIRE(ungrouped.has_value());
    CHECK(ungrouped->groups.empty());
}

TEST_CASE("existing group matches are represented as joins and group names validate") {
    worm_ui_sound_import_state_t state;
    const auto input = paths({"/s/Drums.wav"});
    worm_ui_nuklear_sound_import_add(state, input, true);
    const std::array existing{worm_ui_sound_import_existing_group_t{
        .id = wormedit::sound_group_id_t{21}, .name = "Drums"}};
    worm_ui_nuklear_sound_import_suggest_groups(state, existing);
    REQUIRE(state.groups.size() == 1);
    CHECK(state.groups[0].existing_group == wormedit::sound_group_id_t{21});
    const auto snapshot = worm_ui_nuklear_sound_import_confirm(state);
    REQUIRE(snapshot.has_value());
    REQUIRE(snapshot->groups.size() == 1);
    CHECK(snapshot->groups[0].existing_group == wormedit::sound_group_id_t{21});
    worm_ui_nuklear_sound_import_rename_group(state, 0, "");
    CHECK_FALSE(worm_ui_nuklear_sound_import_confirm(state).has_value());
    CHECK(state.message == "Group names must contain 1 to 64 bytes.");
}
