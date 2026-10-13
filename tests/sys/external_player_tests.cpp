#include "sys/external_player.h"
#include <wormedit/formats/bms.h>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <fstream>
#include <span>
#include <thread>

using namespace wormedit::sys;
namespace {
struct temporary_directory_t {
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("wormedit external player " + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    temporary_directory_t() { std::filesystem::create_directories(path); }
    ~temporary_directory_t() { std::error_code error; std::filesystem::remove_all(path, error); }
};
auto read(const std::filesystem::path& path) -> std::string {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
}
auto parse(std::string_view source, wormedit::bms_parse_options_t options = {})
    -> wormedit::song_document_t {
    if (!options.lane_profile_id) options.lane_profile_id = "bme-7k-outside-scratch";
    const auto parsed = wormedit::parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}), options);
    REQUIRE(parsed.song);
    return *parsed.song;
}
auto simple_song() -> wormedit::song_document_t {
    return parse("#TITLE Preview\n#BPM 120\n#WAV01 sound file.wav\n#00111:01\n");
}
auto wait_for_log(const std::filesystem::path& path) -> bool {
    for (int attempt = 0; attempt < 200; ++attempt) {
        if (std::filesystem::exists(path)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return false;
}
}

namespace {
void check_defaults_and_missing_preferences(const std::filesystem::path& path,
    external_player_preferences_t& preferences) {
    REQUIRE(preferences.players.size() == 3);
    CHECK(preferences.players[0].here_arguments == "-s <measure> \"<filename>\"");
    CHECK(preferences.players[1].here_arguments == "-P -N<measure> \"<filename>\"");
    CHECK(load_external_player_preferences(path, preferences).empty());
 }
}

TEST_CASE("external_player expands quotes before source measure and path substitutions", "[external_player]") {
    const auto expanded = expand_external_player_arguments(
        "-s <measure> \"<filename>\" '<apppath>/player exe' \"\" literal\\path ; $(echo)",
        "/app directory", "/chart directory/<measure>.bms", 17);
    REQUIRE(expanded);
    CHECK(expanded.arguments == std::vector<std::string>{"-s", "17",
        "/chart directory/<measure>.bms", "/app directory/player exe", "", "literal\\path", ";", "$(echo)"});
    CHECK_FALSE(expand_external_player_arguments("\"unfinished", "", "", 0));
    CHECK_FALSE(expand_external_player_arguments(std::string{"bad\0arg", 7}, "", "", 0));
}

namespace {
auto check_preferences_round_trip(const std::filesystem::path& path,
    const external_player_preferences_t& preferences) -> external_player_preferences_t {
    auto loaded = default_external_player_preferences();
    REQUIRE(load_external_player_preferences(path, loaded).empty());
    CHECK(loaded == preferences);
    loaded.players[0].name = "Changed";
    REQUIRE(save_external_player_preferences(path, loaded).empty());
    CHECK(read(path).find("Changed") != std::string::npos);
    return loaded;
}
}

TEST_CASE("external_player defaults match iBMSC and preferences round trip atomically", "[external_player]") {
    temporary_directory_t directory;
    const auto path = directory.path / "external-players.conf";
    auto preferences = default_external_player_preferences();
    check_defaults_and_missing_preferences(path, preferences);
    preferences.selected = 1;
    preferences.players[1].path = "wrapper with spaces";
    preferences.players[1].start_arguments = "\"<apppath>/player.exe\" \"<filename>\"";
    REQUIRE(save_external_player_preferences(path, preferences).empty());
    auto loaded = check_preferences_round_trip(path, preferences);
    const auto before = read(path);
    loaded.selected = 100;
    CHECK_FALSE(save_external_player_preferences(path, loaded).empty());
    CHECK(read(path) == before);
    { std::ofstream malformed(path); malformed << "broken preferences"; }
    CHECK_FALSE(load_external_player_preferences(path, preferences).empty());
    CHECK(preferences.selected == 1);
}

TEST_CASE("external_player missing path opens preferences before export", "[external_player]") {
    external_player_t player;
    const auto result = player.launch({}, external_player_action_t::play_here, {}, {}, {}, 12, {});
    CHECK(result.open_preferences);
    CHECK_FALSE(result.launched);
    CHECK(result.preview_path.empty());
}

TEST_CASE("external_player rejects export and launch errors without leaving temporary files", "[external_player]") {
    temporary_directory_t directory;
    auto song = simple_song();
    external_player_t player;
    const external_player_definition_t definition{"Missing", "/nonexistent/wormedit-player", "\"<filename>\"", "", ""};
    const auto source = directory.path / "original.bms";
    { std::ofstream original(source); original << "original untouched"; }
    CHECK_FALSE(player.launch(definition, external_player_action_t::play_start, song, {999999}, source, 0, {}).launched);
    CHECK_FALSE(std::filesystem::exists(directory.path / "___wormedit_preview.bms"));
    CHECK_FALSE(player.launch(definition, external_player_action_t::play_start, song, song.charts[0].id, source, 0, {}).launched);
    CHECK_FALSE(std::filesystem::exists(directory.path / "___wormedit_preview.bms"));
    CHECK(read(source) == "original untouched");
}

TEST_CASE("external_player never overwrites or cleans up an existing preview collision", "[external_player]") {
    temporary_directory_t directory;
    const auto path = directory.path / "___wormedit_preview.bms";
    { std::ofstream existing(path); existing << "belongs to someone else"; }
    auto song = simple_song();
    {
        external_player_t player;
        const external_player_definition_t definition{"Missing", "/nonexistent/player", "", "", ""};
        const auto result = player.launch(definition, external_player_action_t::play_start, song,
            song.charts[0].id, directory.path / "original.bms", 0, {});
        CHECK_FALSE(result.launched);
    }
    CHECK(read(path) == "belongs to someone else");
}

namespace {
void check_stub_preview(const external_player_launch_result_t& result,
    const std::filesystem::path& directory, const std::filesystem::path& log,
    const std::filesystem::path& source) {
    CHECK(result.preview_path == directory / "___wormedit_preview.bme");
    CHECK(read(log) == "-s\n37\n" + result.preview_path.string() + "\n" +
        (directory / "app directory/player exe").string());
    CHECK(read(result.preview_path).find("#RANDOM") == std::string::npos);
    CHECK(read(result.preview_path).find("#00111:02") != std::string::npos);
    CHECK(read(source) == "real source stays untouched");
 }
}

namespace {
void check_stop_stub(external_player_t& player, const external_player_definition_t& definition,
                     const std::filesystem::path& log) {
    std::filesystem::remove(log);
    REQUIRE(player.launch(definition, external_player_action_t::stop, {}, {}, {}, 0, {}).launched);
    REQUIRE(wait_for_log(log));
    CHECK(read(log) == "-t");
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
}
}

#ifdef __unix__
TEST_CASE("external_player actual stub receives quoted filenames source measure and active branch", "[external_player]") {
    temporary_directory_t directory;
    const auto stub = directory.path / "logging stub.py";
    const auto log = directory.path / "arguments.txt";
    { std::ofstream script(stub); script <<
        "import sys,pathlib,time\n"
        "pathlib.Path(sys.argv[1]).write_text('\\n'.join(sys.argv[2:]))\n"
        "time.sleep(0.4)\n"; }
    const std::string template_text = "\"" + stub.string() + "\" \"" + log.string() +
        "\" -s <measure> \"<filename>\" \"<apppath>/player exe\"";
    const external_player_definition_t definition{"Stub", "/usr/bin/python3", template_text, template_text,
        "\"" + stub.string() + "\" \"" + log.string() + "\" -t"};
    auto song = parse("#TITLE Branch preview\n#BPM 120\n#WAV01 first.wav\n#WAV02 second.wav\n"
        "#RANDOM 2\n#IF 1\n#00111:01\n#ELSE\n#00111:02\n#ENDIF\n#ENDRANDOM\n",
        {.branch_choices = {{.control_source_order = 4, .selected_value = 2}}});
    const auto source = directory.path / "source <measure> with spaces.bme";
    { std::ofstream original(source); original << "real source stays untouched"; }
    external_player_t player;
    const auto result = player.launch(definition, external_player_action_t::play_here, song,
        song.charts[0].id, source, 37, directory.path / "app directory");
    INFO(result.error);
    REQUIRE(result.launched);
    REQUIRE(wait_for_log(log));
    check_stub_preview(result, directory.path, log, source);
    CHECK_FALSE(player.launch(definition, external_player_action_t::play_start, song,
        song.charts[0].id, source, 0, {}).launched);
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    const auto second = player.launch(definition, external_player_action_t::play_start, song,
        song.charts[0].id, source, 0, {});
    REQUIRE(second.launched);
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    // Replacing the owned path must prevent cleanup from deleting a foreign file.
    const auto moved = directory.path / "old owned.bme";
    std::filesystem::rename(second.preview_path, moved);
    { std::ofstream replacement(second.preview_path); replacement << "foreign replacement"; }
    player.cleanup();
    CHECK(read(second.preview_path) == "foreign replacement");
    check_stop_stub(player, definition, log);
}
#endif

TEST_CASE("external_player accepts BML source extension through the shared catalog", "[external_player][catalog-wiring]") {
    temporary_directory_t directory;
    const auto preview = directory.path / "___wormedit_preview.bml";
    { std::ofstream existing(preview); existing << "foreign preview"; }
    const auto song = simple_song();
    external_player_t player;
    const external_player_definition_t definition{"Missing", "/nonexistent/wormedit-player", "", "", ""};
    const auto result = player.launch(definition, external_player_action_t::play_start,
        song, song.charts.front().id, directory.path / "source.BML", 0, {});
    CHECK(result.preview_path == preview);
    CHECK_FALSE(result.launched);
    CHECK(read(preview) == "foreign preview");
}
