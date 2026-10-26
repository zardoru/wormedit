#include "nuklear_event_harness.h"
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <vector>

namespace {
void expect_input_options(bool value) { CHECK(value); }
struct player_typing_t {
    nuklear_event_harness_t harness{1280, 1000};
    std::string name{"mBMplay"}, path;
    player_typing_t() {
        harness.view().options = {.visible = true, .revision = 82, .category = WORM_UI_OPTIONS_PLAYERS,
            .players = {{name, path, "\"<filename>\"", "-s <measure>", "-t"},
                        {"uBMplay", "", "", "", ""}, {"o2play", "", "", "", ""}}};
        (void)harness.frame();
    }
    void focus(const char* label) {
        const auto point = harness.drawn_text_center(label);
        REQUIRE(point);
        (void)wormedit::test::click_at(harness, point->first, point->second + 31);
    }
    void type(const char* chunk, WormUiOptionsField field) {
        std::vector<std::string> characters;
        for (const auto character : std::string_view{chunk}) characters.emplace_back(1, character);
        std::vector<SDL_Event> events;
        for (const auto& character : characters) {
            SDL_Event key{};
            key.type = SDL_EVENT_KEY_DOWN;
            key.key.key = SDL_GetKeyFromName(character.c_str());
            key.key.down = true;
            events.push_back(key);
            SDL_Event text{};
            text.type = SDL_EVENT_TEXT_INPUT;
            text.text.text = character.c_str();
            events.push_back(text);
            key.type = SDL_EVENT_KEY_UP;
            key.key.down = false;
            events.push_back(key);
        }
        const auto frame = harness.frame(events);
        const auto& action = frame.get().options;
        REQUIRE(action.kind == WORM_UI_OPTIONS_EDIT_FIELD);
        expect_input_options(action.field == field);
        auto& value = field == WORM_UI_OPTIONS_FIELD_PLAYER_NAME ? name : path;
        value = action.text;
        auto& view = harness.view().options;
        view.players[0].name = name;
        view.players[0].path = path;
        ++view.revision;
        expect_input_options(harness.frame().get().options.kind == WORM_UI_OPTIONS_NONE);
    }
};
}
TEST_CASE("Input defaults expose DFJK without accepting preferences", "[event-harness][default-lane-keys]") {
    nuklear_event_harness_t harness{1280, 1000};
    harness.view().options = {.visible = true, .revision = 81, .category = WORM_UI_OPTIONS_INPUT};
    (void)harness.frame();
    const auto heading = harness.drawn_text_center("Default lane keys");
    REQUIRE(heading);
    (void)wormedit::test::click_at(harness, heading->first, heading->second);
    const auto preset = harness.drawn_text_center("DFJK (4K)");
    REQUIRE(preset);
    bool selected = false;
    for (const auto& frame : wormedit::test::click_at(harness, preset->first, preset->second)) {
        const auto& action = frame.get().options;
        if (action.kind != WORM_UI_OPTIONS_EDIT_FIELD) continue;
        selected = true;
        expect_input_options(action.field == WORM_UI_OPTIONS_FIELD_DEFAULT_LANE_DFJK);
        expect_input_options(action.lane_count == 4 && action.revision == 81);
    }
    expect_input_options(selected);
}

TEST_CASE("Player fields retain cursor position through successive draft revisions", "[event-harness][external-player]") {
    player_typing_t fixture;
    fixture.focus("Name");
    fixture.type("T258", WORM_UI_OPTIONS_FIELD_PLAYER_NAME);
    fixture.focus("Player or wrapper path");
    fixture.type("/usr/", WORM_UI_OPTIONS_FIELD_PLAYER_PATH);
    fixture.type("bin", WORM_UI_OPTIONS_FIELD_PLAYER_PATH);
    fixture.type("/true", WORM_UI_OPTIONS_FIELD_PLAYER_PATH);
    expect_input_options(fixture.path == "/usr/bin/true");
}

TEST_CASE("Shortcut settings filter command names and chords without changing bindings",
          "[event-harness][shortcuts]") {
    nuklear_event_harness_t harness{1280, 1000};
    harness.view().options = {.visible = true, .revision = 83, .category = WORM_UI_OPTIONS_INPUT,
        .shortcut_rows = {{"open", "Open chart", "Ctrl+O", false},
                          {"save", "Save project", "Ctrl+S", false}}};
    (void)harness.frame();
    const auto label = harness.drawn_text_center("Find shortcut by command or chord");
    REQUIRE(label);
    (void)wormedit::test::click_at(harness, label->first, label->second + 30);
    SDL_Event text{};
    text.type = SDL_EVENT_TEXT_INPUT;
    text.text.text = "cTrL+o";
    const auto result = harness.frame({&text, 1});
    expect_input_options(result.get().options.kind == WORM_UI_OPTIONS_NONE);
    expect_input_options(harness.drawn_text().find("Open chart") != std::string::npos);
    expect_input_options(harness.drawn_text().find("Save project") == std::string::npos);
}
