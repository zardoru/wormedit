#include <wormedit/formats/quaver.h>
#include <wormedit/formats/native_project.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/rational.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <fstream>
#include <iterator>

namespace {
auto bytes(std::string_view text) -> std::span<const std::byte> {
    return std::as_bytes(std::span(text.data(), text.size()));
}
auto fixture(std::string_view name) -> std::string {
    std::ifstream input(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "quaver" / name, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
auto notes(const wormedit::chart_document_t& chart) -> std::vector<const wormedit::note_t*> {
    std::vector<const wormedit::note_t*> values;
    for (const auto& measure : chart.measures)
        for (const auto& note : measure.notes) values.push_back(&note);
    return values;
}
void require_success(const auto& outcome) {
    for (const auto& diagnostic : outcome.diagnostics) {
        INFO(diagnostic.message);
        REQUIRE(diagnostic.severity != wormedit::diagnostic_severity_t::error);
    }
    REQUIRE(outcome.value.has_value());
}
void check_crlf(std::string_view output) {
    CHECK(output.ends_with("\r\n"));
    for (std::size_t at = 0; at < output.size(); ++at)
        if (output[at] == '\n') CHECK((at != 0 && output[at - 1] == '\r'));
}
void check_native_storage(const wormedit::song_document_t& song, std::string_view expected) {
    const auto native = wormedit::write_native_project(song);
    require_success(native);
    const auto restored = wormedit::parse_native_project(*native.value);
    require_success(restored);
    const auto exported = wormedit::write_quaver(*restored.value);
    require_success(exported);
    CHECK(*exported.value == expected);
}
auto parse_fixture(std::string_view name) -> wormedit::song_document_t {
    auto parsed = wormedit::parse_quaver_bytes(bytes(fixture(name)));
    require_success(parsed);
    return std::move(*parsed.value);
}
auto export_song(const wormedit::song_document_t& song) -> std::string {
    const auto written = wormedit::write_quaver(song);
    require_success(written);
    return *written.value;
}
void check_rejection(std::string_view input, wormedit::quaver_parse_options_t options = {}) {
    const auto parsed = wormedit::parse_quaver_bytes(bytes(input), options);
    CHECK_FALSE(parsed.value);
    CHECK_FALSE(parsed.ok());
    REQUIRE_FALSE(parsed.diagnostics.empty());
}
auto fixture_failure(std::string_view name) -> wormedit::quaver_diagnostic_code_t {
    using code_t = wormedit::quaver_diagnostic_code_t;
    if (name.find("sample") != std::string_view::npos) return code_t::invalid_sample;
    if (name.find("hold") != std::string_view::npos) return code_t::invalid_hold;
    if (name.find("version") != std::string_view::npos) return code_t::invalid_version;
    if (name.find("mode") != std::string_view::npos) return code_t::invalid_mode;
    return code_t::missing_content;
}
auto exported_yaml(std::string_view name) -> YAML::Node {
    return YAML::Load(export_song(parse_fixture(name)));
}
}
TEST_CASE("Quaver positive corpus exports deterministically and survives native storage", "[quaver]") {
    const auto* name = GENERATE("four-key-denormalized.qua", "seven-key-scratch-normalized.qua",
        "samples-layers-mines.qua", "scroll-groups-unsorted.qua");
    INFO(name);
    const auto song = parse_fixture(name);
    const auto written = export_song(song);
    check_crlf(written);
    const auto reparsed = wormedit::parse_quaver_bytes(bytes(written));
    require_success(reparsed);
    CHECK(export_song(*reparsed.value) == written);
    CHECK(notes(song.charts.front()).size() == notes(reparsed.value->charts.front()).size());
    check_native_storage(song, written);
}
TEST_CASE("Quaver rejects all negative fixtures transactionally", "[quaver]") {
    const auto* name = GENERATE("negative-no-hit-objects.qua", "negative-no-timing-points.qua",
        "negative-keysound-sample-zero.qua", "negative-keysound-sample-range.qua",
        "negative-sound-effect-sample-zero.qua", "negative-sound-effect-sample-range.qua",
        "negative-hold-equal.qua", "negative-hold-before.qua", "negative-version.qua", "negative-mode.qua");
    INFO(name);
    const auto rejected = wormedit::parse_quaver_bytes(bytes(fixture(name)));
    CHECK_FALSE(rejected.value);
    REQUIRE_FALSE(rejected.diagnostics.empty());
    CHECK(rejected.diagnostics.front().code == fixture_failure(name));
}
TEST_CASE("Quaver models BPM signatures and denormalized scroll", "[quaver]") {
    const auto song = parse_fixture("four-key-denormalized.qua");
    const auto& chart = song.charts.front();
    CHECK(chart.bpms.size() == 2);
    CHECK(chart.speeds.size() == 1);
    REQUIRE(chart.scrolls.size() == 3);
    CHECK(wormedit::to_double(chart.scrolls.back().multiplier) == 0.75);
    CHECK(chart.measures[1].definition.meter == wormedit::meter_t{3, 4});
}
TEST_CASE("Quaver models hitsound additions", "[quaver]") {
    const auto song = parse_fixture("four-key-denormalized.qua");
    const auto objects = notes(song.charts.front());
    REQUIRE(objects.size() == 5);
    CHECK(objects[0]->osu_hitsample->additions() == 0);
    CHECK(objects[1]->osu_hitsample->whistle);
    CHECK(objects[2]->osu_hitsample->finish);
    CHECK(objects[3]->osu_hitsample->clap);
    CHECK(objects[4]->osu_hitsample->additions() == 14);
}
TEST_CASE("Quaver models scratch holds and normalized scroll", "[quaver]") {
    const auto song = parse_fixture("seven-key-scratch-normalized.qua");
    const auto& chart = song.charts.front();
    REQUIRE(chart.lane_layouts.front().layout.lanes.size() == 8);
    CHECK(chart.lane_layouts.front().layout.lanes.back().role == wormedit::lane_role_t::scratch);
    const auto objects = notes(chart);
    REQUIRE(objects.size() == 3);
    CHECK(objects.back()->kind == wormedit::note_kind_t::hold);
    CHECK(objects.back()->end.has_value());
    CHECK(wormedit::to_double(chart.scrolls.back().multiplier) == 0.75);
}
TEST_CASE("Quaver models samples, hidden layers and mines", "[quaver]") {
    const auto song = parse_fixture("samples-layers-mines.qua");
    const auto& chart = song.charts.front();
    REQUIRE(chart.semantic_layers.size() == 2);
    CHECK_FALSE(chart.semantic_layers[1].visible);
    CHECK(chart.semantic_layers[1].color.blue == 255);
    CHECK(notes(chart)[0]->sound.has_value());
    CHECK(notes(chart)[1]->kind == wormedit::note_kind_t::mine);
    CHECK(chart.bookmarks[0].label == "First accent");
}
TEST_CASE("Quaver retains independent scroll groups and ignores unknown fields", "[quaver]") {
    const auto node = exported_yaml("scroll-groups-unsorted.qua");
    CHECK(node["TimingGroups"]["accent"].Tag() == "!ScrollGroup");
    CHECK(node["TimingGroups"]["accent"]["ScrollVelocities"][0]["StartTime"].as<double>() == 750.5);
    CHECK(node["TimingGroups"]["$Global"]["ScrollSpeedFactors"][0]["Multiplier"].as<double>() == 2);
    CHECK(node["HitObjects"][1]["TimingGroup"].as<std::string>() == "accent");
    CHECK_FALSE(node["UnknownFixtureField"]);
}
TEST_CASE("Quaver preserves multiple samples, volumes and bookmark colors", "[quaver]") {
    const auto node = exported_yaml("samples-layers-mines.qua");
    CHECK(node["CustomAudioSamples"][1]["UnaffectedByRate"].as<bool>());
    REQUIRE(node["HitObjects"][0]["KeySounds"].size() == 2);
    CHECK(node["HitObjects"][0]["KeySounds"][1]["Volume"].as<int>() == 60);
    CHECK(node["SoundEffects"][0]["StartTime"].as<double>() == 250.5);
    CHECK(node["Bookmarks"][1]["ColorRgb"].as<std::string>() == "255,0,64");
}
TEST_CASE("Quaver bounded YAML rejects aliases, nesting, duplicate keys and documents", "[quaver]") {
    const auto* extra = GENERATE("\nAlias: &a [*a]\n", "\nMode: Keys7\n", "\nExtra: [[[[]]]]\n", "\n---\nMode: Keys4\n");
    check_rejection(fixture("four-key-denormalized.qua") + extra, {.max_depth = 4});
}
TEST_CASE("Quaver enforces configured source and structure budgets", "[quaver]") {
    const auto valid = fixture("four-key-denormalized.qua");
    const auto choice = GENERATE(0, 1, 2, 3);
    auto options = wormedit::quaver_parse_options_t{};
    if (choice == 0) options.max_source_bytes = valid.size() - 1;
    if (choice == 1) options.max_nodes = 3;
    if (choice == 2) options.max_events = 2;
    if (choice == 3) options.max_measures = 1;
    check_rejection(valid, options);
}
TEST_CASE("Quaver rejects escaping resource paths and invalid UTF-8", "[quaver]") {
    auto source = fixture("four-key-denormalized.qua");
    source.replace(source.find("fixture.ogg"), 11, "../evil.ogg");
    check_rejection(source);
    check_rejection(std::string(1, static_cast<char>(0xff)));
}
TEST_CASE("Quaver export applies metadata and hitsound edits", "[quaver]") {
    auto song = parse_fixture("four-key-denormalized.qua");
    song.metadata.title = "Edited: # quoted YAML";
    song.charts.front().metadata.author = "New author";
    song.charts.front().measures.front().notes.front().osu_hitsample->clap = true;
    const auto reparsed = wormedit::parse_quaver_bytes(bytes(export_song(song)));
    require_success(reparsed);
    CHECK(reparsed.value->metadata.title == song.metadata.title);
    CHECK(notes(reparsed.value->charts.front())[0]->osu_hitsample->clap);
}
TEST_CASE("Quaver export rejects unsupported content and output limits", "[quaver]") {
    auto song = parse_fixture("four-key-denormalized.qua");
    CHECK_FALSE(wormedit::write_quaver(song, {.max_output_bytes = 3}).value);
    CHECK_FALSE(wormedit::write_quaver(song, {.max_events = 1}).value);
    song.charts.front().measures.front().notes.front().kind = wormedit::note_kind_t::roll;
    const auto rejected = wormedit::write_quaver(song);
    CHECK_FALSE(rejected.value);
    CHECK_FALSE(rejected.ok());
}
TEST_CASE("Quaver export reflects moved note milliseconds", "[quaver]") {
    auto song = parse_fixture("four-key-denormalized.qua");
    auto& note = song.charts.front().measures.front().notes.front();
    note.position.row += 240'000;
    const auto node = YAML::Load(export_song(song));
    CHECK(node["HitObjects"][0]["StartTime"].as<int>() == 1000);
    CHECK(node["HitObjects"][0]["Lane"].as<int>() == 1);
}
TEST_CASE("Quaver denormalized export preserves effective scroll after BPM edits", "[quaver]") {
    auto song = parse_fixture("four-key-denormalized.qua");
    song.charts.front().bpms.back().beats_per_minute = {240, 1};
    const auto node = YAML::Load(export_song(song));
    CHECK(node["TimingPoints"][1]["Bpm"].as<int>() == 240);
    CHECK(node["HitObjects"][4]["StartTime"].as<int>() == 2375);
    const auto reimport = wormedit::parse_quaver_bytes(bytes(export_song(song)));
    require_success(reimport);
    CHECK(wormedit::to_double(reimport.value->charts.front().scrolls.back().multiplier) == 0.75);
}
TEST_CASE("Quaver negative timestamps retain an explicit audio origin", "[quaver]") {
    constexpr auto source = "Mode: Keys4\nTimingPoints:\n- StartTime: -1000\n  Bpm: 120\nHitObjects:\n- StartTime: -500\n  Lane: 1\n  EndTime: 500\n";
    const auto parsed = wormedit::parse_quaver_bytes(bytes(source));
    require_success(parsed);
    CHECK(wormedit::to_double(parsed.value->charts.front().beat_zero_audio_offset_seconds) == -1);
    const auto node = YAML::Load(export_song(*parsed.value));
    CHECK(node["HitObjects"][0]["StartTime"].as<int>() == -500);
    CHECK(node["HitObjects"][0]["EndTime"].as<int>() == 500);
}
TEST_CASE("Quaver writer emits signature-only timing anchors", "[quaver]") {
    auto song = parse_fixture("four-key-denormalized.qua");
    // Add a signature-only tail bar, without introducing a BPM edit.
    auto& tail = song.charts.front().measures.back();
    tail.definition = {.length = {1, 1}, .meter = wormedit::meter_t{4, 4}};
    const auto node = YAML::Load(export_song(song));
    REQUIRE(node["TimingPoints"].size() == 3);
    CHECK(node["TimingPoints"][2]["StartTime"].as<double>() == 3000);
    CHECK_FALSE(node["TimingPoints"][2]["Signature"]);
}
TEST_CASE("Quaver validates retained scalar types before publishing", "[quaver]") {
    constexpr auto base = "Mode: Keys4\nTimingPoints: [{Bpm: 120}]\nHitObjects: [{StartTime: 500, Lane: 1}]\n";
    const auto* invalid = GENERATE("LegacyLNRendering: invalid\n",
        "CustomAudioSamples: [{Path: sample.wav, UnaffectedByRate: invalid}]\n",
        "TimingGroups: {accent: {Hidden: invalid}}\n");
    const auto parsed = wormedit::parse_quaver_bytes(bytes(std::string(base) + invalid));
    CHECK_FALSE(parsed.value);
    REQUIRE_FALSE(parsed.diagnostics.empty());
    CHECK(parsed.diagnostics.front().code == wormedit::quaver_diagnostic_code_t::invalid_yaml);
}
