#include <wormedit/core/preferences.h>
#include <wormedit/audio/playback_rate.h>

#include <catch2/catch_test_macros.hpp>

#include <source_location>
#include <atomic>
#include <chrono>
#include <fstream>

using namespace wormedit;
namespace {
void expect_preference(bool value, std::source_location where = std::source_location::current()) {
    INFO("preferences check caller line " << where.line());
    CHECK(value);
}
void require_preference(bool value, std::source_location where = std::source_location::current()) {
    INFO("preferences requirement caller line " << where.line());
    REQUIRE(value);
}
struct preferences_test_directory_t {
    std::filesystem::path path;
    preferences_test_directory_t() {
        static std::atomic<std::uint64_t> sequence{};
        path = std::filesystem::temp_directory_path() /
            ("wormedit-preferences-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
             "-" + std::to_string(sequence.fetch_add(1)));
        std::filesystem::create_directories(path);
    }
    ~preferences_test_directory_t() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
}

TEST_CASE("Program preferences defaults preserve existing player and minimap behavior", "[unit][core][preferences]") {
    const auto values = default_program_preferences();
    expect_preference(values.general.autosave_minutes == 5);
    expect_preference(values.general.shortcut_profile_path == "shortcuts.json");
    expect_preference(values.editor.default_snap == 4);
    expect_preference(values.audio.playback_rate == rational_t{1, 1});
    expect_preference(!values.audio.keep_pitch);
    expect_preference(!values.minimap.click_moves_paused_playhead);
    expect_preference(values.display.ui_scale_percent == 0 && values.display.theme.empty());
    expect_preference(values.players.players.size() == 3 && values.players.selected == 0);
    expect_preference(values.players.players[1].here_arguments == "-P -N<measure> \"<filename>\"");
    const auto parsed = parse_program_preferences("{}");
    expect_preference(parsed.parsed && parsed.warnings.empty() && parsed.preferences == values);
}

TEST_CASE("Program preferences round trip every section without project data", "[unit][core][preferences]") {
    auto values = default_program_preferences();
    values.general = {30, 100, false, "profiles/custom.json"};
    values.editor.default_snap = 96;
    values.audio = {{1, 3}, true};
    values.display = {"dark", 150, true};
    values.players.players[0] = {"Player \"one\"", "C:\\Music\\player.exe", "\"<filename>\"", "<measure>", "stop"};
    values.players.selected = 2;
    values.minimap.click_moves_paused_playhead = true;
    values.layout.dock_layout = "left:resources,right:inspector";
    const auto encoded = serialize_program_preferences(values);
    require_preference(encoded.has_value());
    const auto parsed = parse_program_preferences(*encoded);
    expect_preference(parsed.parsed && parsed.warnings.empty());
    expect_preference(parsed.preferences == values);
    expect_preference(encoded->find("\"version\": 1") != std::string::npos);
    expect_preference(encoded->find("\"charts\"") == std::string::npos);
}

TEST_CASE("Program preferences preserve all exact supported playback rates", "[unit][core][preferences]") {
    for (const auto rate : audio::playback_rates) {
        auto values = default_program_preferences();
        values.audio.playback_rate = rate;
        const auto encoded = serialize_program_preferences(values);
        require_preference(encoded.has_value());
        expect_preference(parse_program_preferences(*encoded).preferences.audio.playback_rate == rate);
    }
}

TEST_CASE("Program preferences replace invalid fields individually", "[unit][core][preferences]") {
    const auto parsed = parse_program_preferences(R"({"version":1,"general":{"autosave_minutes":0,"recent_files_count":101,"shortcut_profile_path":"","confirm_unsaved_close":false},"editor":{"default_snap":-1},"audio":{"playback_rate":{"numerator":2,"denominator":3},"keep_pitch":true},"display":{"ui_scale_percent":99},"minimap":{"click_moves_paused_playhead":true}})");
    expect_preference(parsed.parsed && !parsed.warnings.empty());
    const auto& value = parsed.preferences;
    expect_preference(value.general.autosave_minutes == 5 && value.general.recent_files_count == 10);
    expect_preference(value.general.shortcut_profile_path == "shortcuts.json");
    expect_preference(!value.general.confirm_unsaved_close);
    expect_preference(value.editor.default_snap == 4);
    expect_preference(value.audio.playback_rate == rational_t{1, 1} && value.audio.keep_pitch);
    expect_preference(value.display.ui_scale_percent == 0);
    expect_preference(value.minimap.click_moves_paused_playhead);
}

TEST_CASE("Program preferences ignore unknown keys with one warning", "[unit][core][preferences]") {
    const auto parsed = parse_program_preferences(R"({"other":1,"general":{"unknown":2},"editor":{"future":3},"players":{"definitions":[{"name":"Custom","other":true}]}})");
    expect_preference(parsed.parsed);
    expect_preference(parsed.warnings.size() == 1);
    expect_preference(parsed.preferences.players.players[0].name == "Custom");
}

TEST_CASE("Program preferences reject malformed roots and unsupported versions safely", "[unit][core][preferences]") {
    for (const auto text : {"{", "[]", "null", "{\"version\":2}", "{\"version\":1.0}"}) {
        const auto parsed = parse_program_preferences(text);
        expect_preference(!parsed.parsed && !parsed.warnings.empty());
        expect_preference(parsed.preferences == default_program_preferences());
    }
}

TEST_CASE("Program preferences bound bytes nesting and JSON nodes", "[unit][core][preferences]") {
    expect_preference(!parse_program_preferences(std::string(maximum_preferences_bytes + 1, ' ')).parsed);
    std::string nested = "0";
    for (unsigned depth = 0; depth < maximum_preferences_depth + 2; ++depth) nested = "[" + nested + "]";
    expect_preference(!parse_program_preferences("{\"unknown\":" + nested + "}").parsed);
    std::string nodes = "{\"unknown\":[0";
    for (unsigned index = 0; index < 65536; ++index) nodes += ",0";
    nodes += "]}";
    expect_preference(!parse_program_preferences(nodes).parsed);
}

TEST_CASE("Program preferences reject wrong types NUL and integer overflow", "[unit][core][preferences]") {
    const auto parsed = parse_program_preferences(R"({"general":{"autosave_minutes":18446744073709551615,"shortcut_profile_path":"bad\u0000path"},"display":{"theme":12},"audio":{"keep_pitch":"true"},"minimap":false,"editor":{"default_snap":3.5}})");
    expect_preference(parsed.parsed && parsed.warnings.size() >= 5);
    expect_preference(parsed.preferences == default_program_preferences());
}

TEST_CASE("Program preferences bound and repair external player entries", "[unit][core][preferences]") {
    auto parsed = parse_program_preferences(R"({"players":{"selected":64,"definitions":[]}})");
    expect_preference(parsed.parsed && parsed.preferences.players == default_external_player_preferences());
    parsed = parse_program_preferences(R"({"players":{"selected":1,"definitions":[null,{"name":"Custom","path":"/usr/bin/player"}]}})");
    expect_preference(parsed.parsed && parsed.preferences.players.players.size() == 2);
    expect_preference(parsed.preferences.players.selected == 1);
    expect_preference(parsed.preferences.players.players[0].name == "mBMplay");
    expect_preference(parsed.preferences.players.players[1].path == "/usr/bin/player");
}

TEST_CASE("Program preferences refuse serializing invalid in-memory values", "[unit][core][preferences]") {
    auto values = default_program_preferences();
    values.general.autosave_minutes = 0;
    expect_preference(!serialize_program_preferences(values));
    values = default_program_preferences();
    values.players.selected = 100;
    expect_preference(!serialize_program_preferences(values));
    values = default_program_preferences();
    values.layout.dock_layout = std::string(maximum_preference_string_bytes + 1, 'x');
    expect_preference(!serialize_program_preferences(values));
}

TEST_CASE("Program preferences files round trip atomically and reject invalid replacements", "[unit][core][preferences]") {
    const preferences_test_directory_t directory;
    const auto path = directory.path / "preferences.json";
    expect_preference(load_program_preferences(path).missing);
    auto values = default_program_preferences();
    values.minimap.click_moves_paused_playhead = true;
    require_preference(static_cast<bool>(save_program_preferences_atomic(path, values)));
    auto loaded = load_program_preferences(path);
    expect_preference(loaded.contents.parsed && !loaded.missing && loaded.contents.preferences == values);
    values.general.autosave_minutes = 0;
    expect_preference(!save_program_preferences_atomic(path, values));
    loaded = load_program_preferences(path);
    expect_preference(loaded.contents.parsed && loaded.contents.preferences.general.autosave_minutes == 5);
    expect_preference(loaded.contents.preferences.minimap.click_moves_paused_playhead);
    expect_preference(std::distance(std::filesystem::directory_iterator{directory.path}, std::filesystem::directory_iterator{}) == 1);
}

TEST_CASE("Program preferences corrupt and oversized files use defaults without overwriting", "[unit][core][preferences]") {
    const preferences_test_directory_t directory;
    const auto path = directory.path / "preferences.json";
    { std::ofstream stream(path); stream << "{corrupt"; }
    auto loaded = load_program_preferences(path);
    expect_preference(!loaded.missing && !loaded.contents.parsed && !loaded.contents.warnings.empty());
    expect_preference(loaded.contents.preferences == default_program_preferences());
    expect_preference(std::filesystem::file_size(path) == 8);
    { std::ofstream stream(path); stream << std::string(maximum_preferences_bytes + 1, ' '); }
    loaded = load_program_preferences(path);
    expect_preference(!loaded.contents.parsed && !loaded.contents.warnings.empty());
    expect_preference(std::filesystem::file_size(path) == maximum_preferences_bytes + 1);
}

TEST_CASE("Program preferences failed publication preserves destination and cleans temporary files", "[unit][core][preferences]") {
    const preferences_test_directory_t directory;
    const auto destination = directory.path / "preferences.json";
    std::filesystem::create_directory(destination);
    { std::ofstream stream(destination / "existing"); stream << "retain"; }
    auto values = default_program_preferences();
    values.general.autosave_minutes = 10;
    expect_preference(!save_program_preferences_atomic(destination, values));
    expect_preference(values.general.autosave_minutes == 10);
    expect_preference(std::filesystem::file_size(destination / "existing") == 6);
    expect_preference(std::distance(std::filesystem::directory_iterator{directory.path}, std::filesystem::directory_iterator{}) == 1);
    expect_preference(!save_file_atomic(directory.path / "too-large", "123", 2));
    expect_preference(!std::filesystem::exists(directory.path / "too-large"));
}

TEST_CASE("Program preferences migrate legacy players once and retain the legacy file", "[unit][core][preferences]") {
    const preferences_test_directory_t directory;
    const auto legacy = directory.path / "external-players.conf";
    const auto current = directory.path / "preferences.json";
    constexpr std::string_view old = R"(wormedit-external-players-v1 1 2
"One" "/tmp/player one" "\"<filename>\"" "-N<measure> \"<filename>\"" "-S"
"Two" "/tmp/player two" "start" "here" "stop"
)";
    { std::ofstream stream(legacy, std::ios::binary); stream << old; }
    const auto migrated = load_program_preferences_for_startup(current, legacy);
    expect_preference(migrated.migrated && migrated.contents.warnings.empty());
    expect_preference(migrated.contents.preferences.players.selected == 1);
    expect_preference(migrated.contents.preferences.players.players[0].path == "/tmp/player one");
    expect_preference(migrated.contents.preferences.players.players[0].start_arguments == "\"<filename>\"");
    expect_preference(std::filesystem::file_size(legacy) == old.size());
    require_preference(std::filesystem::is_regular_file(current));
    { std::ofstream stream(legacy, std::ios::binary); stream << "now corrupt"; }
    const auto second = load_program_preferences_for_startup(current, legacy);
    expect_preference(!second.migrated && second.contents.parsed && second.contents.warnings.empty());
    expect_preference(second.contents.preferences == migrated.contents.preferences);
}

TEST_CASE("Program preferences migration save failure retains accepted player values", "[unit][core][preferences]") {
    const preferences_test_directory_t directory;
    const auto legacy = directory.path / "external-players.conf";
    { std::ofstream stream(legacy, std::ios::binary); stream << "wormedit-external-players-v1 0 1\n\"Custom\" \"/tmp/player\" \"start\" \"here\" \"stop\"\n"; }
    const auto loaded = load_program_preferences_for_startup(directory.path / "missing" / "preferences.json", legacy);
    expect_preference(loaded.migrated && !loaded.contents.warnings.empty());
    expect_preference(loaded.contents.preferences.players.players[0].name == "Custom");
    expect_preference(!std::filesystem::exists(directory.path / "missing" / "preferences.json"));
}

TEST_CASE("Program preferences never replace an existing corrupt file with legacy players", "[unit][core][preferences]") {
    const preferences_test_directory_t directory;
    const auto legacy = directory.path / "external-players.conf";
    const auto current = directory.path / "preferences.json";
    { std::ofstream stream(legacy, std::ios::binary); stream << "wormedit-external-players-v1 0 1\n\"Custom\" \"/tmp/player\" \"start\" \"here\" \"stop\"\n"; }
    { std::ofstream stream(current); stream << "{bad}"; }
    const auto loaded = load_program_preferences_for_startup(current, legacy);
    expect_preference(!loaded.migrated && !loaded.contents.parsed && !loaded.contents.warnings.empty());
    expect_preference(loaded.contents.preferences == default_program_preferences());
    expect_preference(std::filesystem::file_size(current) == 5);
}

TEST_CASE("Editor preference defaults preserve existing appearance and profile direction", "[preferences]") {
    const auto defaults = default_program_preferences();
    expect_preference(defaults.editor.default_snap == 4);
    expect_preference(defaults.editor.playhead_anchor_percent == 20);
    expect_preference(defaults.editor.note_kind_palette_visible);
    expect_preference(defaults.editor.lane_key_legend_visible);
    expect_preference(defaults.editor.area_band_alpha == 48);
    for (auto direction : defaults.editor.scroll_directions) expect_preference(direction == 0);
    expect_preference(parse_program_preferences(R"({"version":1,"editor":{"default_snap":8}})").preferences.editor.area_band_alpha == 48);
}
TEST_CASE("Editor preference fields round trip and invalid individual values retain defaults", "[preferences]") {
    auto value = default_program_preferences();
    value.editor.scroll_directions = {1, 2, 1, 2, 1, 2};
    value.editor.playhead_anchor_percent = 35;
    value.editor.note_kind_palette_visible = false;
    value.editor.audition_on_click = false;
    value.editor.lane_key_legend_visible = false;
    value.editor.area_band_alpha = 0;
    const auto bytes = serialize_program_preferences(value);
    require_preference(bytes.has_value());
    expect_preference(parse_program_preferences(*bytes).preferences == value);
    const auto invalid = parse_program_preferences(R"({"version":1,"editor":{"playhead_anchor_percent":101,"area_band_alpha":256,"scroll_directions":{"dance":3,"popn":2},"note_kind_palette_visible":false}})");
    expect_preference(invalid.preferences.editor.playhead_anchor_percent == 20);
    expect_preference(invalid.preferences.editor.area_band_alpha == 48);
    expect_preference(invalid.preferences.editor.scroll_directions[2] == 0);
    expect_preference(invalid.preferences.editor.scroll_directions[1] == 2);
    expect_preference(!invalid.preferences.editor.note_kind_palette_visible);
    value.editor.scroll_directions[0] = 3;
    expect_preference(!serialize_program_preferences(value));
}

namespace {
void check_keysound_preference(std::string_view literal, std::uint32_t expected) {
    const auto parsed = wormedit::parse_program_preferences(
        "{\"audio\":{\"keysound_budget_mib\":" + std::string{literal} + "}}");
    CHECK(parsed.parsed);
    CHECK(parsed.preferences.audio.keysound_budget_mib == expected);
    const auto serialized = wormedit::serialize_program_preferences(parsed.preferences);
    REQUIRE(serialized);
    CHECK(wormedit::parse_program_preferences(*serialized).preferences.audio.keysound_budget_mib == expected);
}
}
TEST_CASE("keysound budgets default and clamp wide JSON integers before narrowing", "[preferences][budget]") {
    check_keysound_preference("-9223372036854775808", 256);
    check_keysound_preference("0", 256);
    check_keysound_preference("4294967296", 8192);
    check_keysound_preference("18446744073709551615", 8192);
    check_keysound_preference("4096", 4096);
    check_keysound_preference("8192", 8192);
}
TEST_CASE("keysound budget invalid JSON types retain the default", "[preferences][budget]") {
    for (const auto value : {"null", "true", "\"8192\"", "512.5", "[]", "{}"})
        check_keysound_preference(value, 512);
    CHECK(wormedit::parse_program_preferences("{}").preferences.audio.keysound_budget_mib == 512);
}

TEST_CASE("Move collision preference defaults to Reject and persists displacement safely", "[unit][core][preferences][bgm-move]") {
    auto values = default_program_preferences();
    expect_preference(values.editor.move_collision_policy == move_collision_policy_t::reject);
    values.editor.move_collision_policy = move_collision_policy_t::displace_to_bgm;
    const auto encoded = serialize_program_preferences(values);
    require_preference(encoded.has_value());
    expect_preference(parse_program_preferences(*encoded).preferences == values);
    const auto invalid = parse_program_preferences(R"({"editor":{"move_collision_policy":"delete"}})");
    expect_preference(!invalid.warnings.empty());
    expect_preference(invalid.preferences.editor.move_collision_policy == move_collision_policy_t::reject);
    values.editor.move_collision_policy = static_cast<move_collision_policy_t>(2);
    expect_preference(!serialize_program_preferences(values));
}

TEST_CASE("Snap-colored grid defaults on and round-trips its saved opt-out", "[preferences][snap-grid]") {
    expect_preference(parse_program_preferences(R"({"version":1,"display":{}})").preferences.display.snap_colored_grid);
    auto values = default_program_preferences();
    values.display.snap_colored_grid = false;
    const auto encoded = serialize_program_preferences(values);
    require_preference(encoded.has_value());
    expect_preference(parse_program_preferences(*encoded).preferences == values);
    for (const auto literal : {"null", "1", "\"false\"", "[]"}) {
        const auto invalid = parse_program_preferences(
            "{\"display\":{\"snap_colored_grid\":" + std::string(literal) + "}}");
        expect_preference(invalid.preferences.display.snap_colored_grid);
        expect_preference(!invalid.warnings.empty());
    }
}

TEST_CASE("Snap grid opacity defaults mild and rejects invalid saved values", "[preferences][snap-grid][opacity]") {
    expect_preference(parse_program_preferences("{}").preferences.display.snap_grid_opacity_percent == 35);
    auto values = default_program_preferences();
    for (const auto percent : {0u, 35u, 100u}) {
        values.display.snap_grid_opacity_percent = percent;
        const auto encoded = serialize_program_preferences(values);
        require_preference(encoded.has_value());
        expect_preference(parse_program_preferences(*encoded).preferences == values);
    }
    for (const auto literal : {"-1", "101", "18446744073709551615", "null", "true", "35.5", "\"50\""}) {
        const auto parsed = parse_program_preferences("{\"display\":{\"snap_grid_opacity_percent\":" + std::string(literal) + "}}");
        expect_preference(parsed.preferences.display.snap_grid_opacity_percent == 35);
        expect_preference(!parsed.warnings.empty());
    }
    values.display.snap_grid_opacity_percent = 101;
    expect_preference(!serialize_program_preferences(values));
}

TEST_CASE("Feedback scroll inversion and waveform opacity persist with safe defaults", "[preferences][feedback]") {
    const auto defaults = parse_program_preferences("{}").preferences;
    expect_preference(!defaults.editor.invert_scroll_wheel);
    expect_preference(defaults.display.field_waveform_opacity_percent == 75);
    auto values = defaults;
    values.editor.invert_scroll_wheel = true;
    values.editor.scroll_directions[0] = 2;
    values.display.field_waveform_opacity_percent = 0;
    const auto bytes = serialize_program_preferences(values);
    require_preference(bytes.has_value());
    const auto parsed = parse_program_preferences(*bytes);
    expect_preference(parsed.parsed);
    expect_preference(parsed.preferences == values);
    const auto invalid = parse_program_preferences(R"({"display":{"field_waveform_opacity_percent":101},"editor":{"invert_scroll_wheel":123}})");
    expect_preference(invalid.preferences.display.field_waveform_opacity_percent == 75);
    expect_preference(!invalid.preferences.editor.invert_scroll_wheel);
    expect_preference(!invalid.warnings.empty());
    values.display.field_waveform_opacity_percent = 101;
    expect_preference(!serialize_program_preferences(values));
}
