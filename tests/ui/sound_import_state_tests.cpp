#include "ui/nuklear/sound_import_state.h"

#include <catch2/catch_test_macros.hpp>

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

TEST_CASE("the list is bounded at 1000 files", "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t state;
    std::vector<std::string> many;
    for (int index = 0; index < 1'005; ++index) many.push_back("/s/f" + std::to_string(index) + ".wav");
    CHECK(worm_ui_nuklear_sound_import_add(state, many, true) == worm_ui_sound_import_capacity);
    CHECK(state.items.size() == worm_ui_sound_import_capacity);
    CHECK(state.message.find("5 file(s) skipped") != std::string::npos);
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

TEST_CASE("confirm hands over the list in order and closes", "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t state;
    const auto input = paths({"/s/a.wav", "/s/b.ogg"});
    worm_ui_nuklear_sound_import_add(state, input, false);
    worm_ui_nuklear_sound_import_rename(state, 0, "  Kick  ");
    const auto taken = worm_ui_nuklear_sound_import_confirm(state);
    REQUIRE(taken.size() == 2);
    CHECK(taken[0].name == "Kick");
    CHECK(taken[0].path == "/s/a.wav");
    CHECK(taken[1].name == "b");
    CHECK(state.phase == phase_t::closed);
    CHECK(state.items.empty());
}

TEST_CASE("confirm refuses empty names and cancel clears everything", "[unit][frontend][sounds]") {
    worm_ui_sound_import_state_t state;
    const auto input = paths({"/s/a.wav", "/s/b.wav"});
    worm_ui_nuklear_sound_import_add(state, input, false);
    worm_ui_nuklear_sound_import_rename(state, 1, "   ");
    CHECK(worm_ui_nuklear_sound_import_confirm(state).empty());
    CHECK(state.phase == phase_t::review_list);
    CHECK(state.items.size() == 2);
    CHECK(state.message == "Every sound needs a name.");
    worm_ui_nuklear_sound_import_cancel(state);
    CHECK(state.phase == phase_t::closed);
    CHECK(state.items.empty());
    CHECK(state.message.empty());
}
