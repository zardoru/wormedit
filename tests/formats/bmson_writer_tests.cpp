#include <wormedit/formats/bmson.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string_view>
#include <tuple>
#include <vector>

namespace {

auto read_binary(const std::filesystem::path& path) -> std::vector<std::byte> {
    std::ifstream stream(path, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
    std::vector<std::byte> bytes;
    bytes.reserve(source.size());
    for (const auto character : source) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return bytes;
}

auto fixture(std::string_view name) -> wormedit::bmson_parse_result_t {
    return wormedit::parse_bmson_bytes(
        read_binary(std::filesystem::path(WORMEDIT_FIXTURE_DIR) / "bmson" / name));
}

auto parse_text(std::string_view source) -> wormedit::bmson_parse_result_t {
    return wormedit::parse_bmson_bytes(std::as_bytes(std::span{source.data(), source.size()}));
}

auto absolute(const wormedit::chart_document_t& chart, wormedit::local_position_t position)
    -> std::optional<std::uint64_t> {
    const auto index = wormedit::build_measure_index(chart);
    if (!std::holds_alternative<wormedit::measure_index_t>(index)) return std::nullopt;
    return wormedit::absolute_row(std::get<wormedit::measure_index_t>(index), position);
}

auto audio_path(const wormedit::song_document_t& song, wormedit::resource_id_t id)
    -> std::string {
    const auto resource = std::ranges::find(song.resources, id,
                                             &wormedit::resource_entry_t::id);
    if (resource == song.resources.end()) return {};
    const auto asset = std::ranges::find(song.audio_assets, resource->asset,
                                          &wormedit::audio_asset_t::id);
    if (asset == song.audio_assets.end()) return {};
    return asset->source_path.generic_string();
}

auto note_signature(const wormedit::song_document_t& song)
    -> std::vector<std::tuple<std::uint64_t, std::uint64_t, wormedit::note_kind_t,
                              std::optional<std::uint64_t>, std::string>> {
    std::vector<std::tuple<std::uint64_t, std::uint64_t, wormedit::note_kind_t,
                           std::optional<std::uint64_t>, std::string>> result;
    for (const auto& chart : song.charts) {
        for (const auto& measure : chart.measures) {
            for (const auto& note : measure.notes) {
                result.emplace_back(*absolute(chart, note.position), note.lane.value,
                                    note.kind,
                                    note.end ? absolute(chart, *note.end) : std::nullopt,
                                    note.sound ? audio_path(song, *note.sound) : std::string{});
            }
        }
    }
    std::ranges::sort(result);
    return result;
}

auto has_write_error(const wormedit::bmson_write_result_t& result,
                     wormedit::bmson_write_diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics, [&](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::error &&
               diagnostic.code == code;
    });
}

} // namespace

TEST_CASE("BMSON writer round-trips mixed sound slices, timing, layout, metadata and BGA",
          "[unit][format][bmson][writer]") {
    const auto imported = fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(imported.song.has_value());

    const auto written = wormedit::write_bmson(*imported.song);
    REQUIRE(written.json.has_value());
    CHECK_FALSE(written.has_errors());

    const auto encoded = std::as_bytes(std::span{written.json->data(), written.json->size()});
    const auto reparsed = wormedit::parse_bmson_bytes(encoded);
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.has_errors());
    CHECK(note_signature(*reparsed.song) == note_signature(*imported.song));

    const auto& before = imported.song->charts.front();
    const auto& after = reparsed.song->charts.front();
    CHECK(reparsed.song->metadata.title == imported.song->metadata.title);
    CHECK(after.metadata.name == before.metadata.name);
    CHECK(after.lane_layouts.front().layout.profile_id == before.lane_layouts.front().layout.profile_id);
    REQUIRE(after.bpms.size() == before.bpms.size());
    for (std::size_t index = 0; index < before.bpms.size(); ++index) {
        CHECK(absolute(after, after.bpms[index].position) == absolute(before, before.bpms[index].position));
        CHECK(after.bpms[index].beats_per_minute == before.bpms[index].beats_per_minute);
    }
    REQUIRE(after.pauses.size() == before.pauses.size());
    CHECK(absolute(after, after.pauses.front().position) == absolute(before, before.pauses.front().position));
    CHECK(after.pauses.front().duration == before.pauses.front().duration);
    REQUIRE(after.bga_events.size() == before.bga_events.size());
    CHECK(absolute(after, after.bga_events.front().position) == absolute(before, before.bga_events.front().position));

    CHECK(written.json->find("\"resolution\": 240") != std::string::npos);
    CHECK(written.json->find("\"mode_hint\": \"beat-7k\"") != std::string::npos);
    CHECK(written.json->find("\"lines\"") != std::string::npos);
}

TEST_CASE("BMSON writer retains standalone BGM and normalizes source-only collisions",
          "[unit][format][bmson][writer]") {
    const auto imported = fixture("published-vox-slicing.bmson");
    REQUIRE(imported.song.has_value());
    const auto& source_chart = imported.song->charts.front();
    REQUIRE(source_chart.measures.size() > 1);
    REQUIRE(source_chart.measures[0].bgm_events.empty());
    REQUIRE(source_chart.measures[1].bgm_events.size() == 1);

    const auto written = wormedit::write_bmson(*imported.song);
    REQUIRE(written.json.has_value());
    const auto reparsed = wormedit::parse_bmson_bytes(
        std::as_bytes(std::span{written.json->data(), written.json->size()}));
    REQUIRE(reparsed.song.has_value());
    const auto& output_chart = reparsed.song->charts.front();
    const auto source_events = std::ranges::count_if(source_chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    const auto output_events = std::ranges::count_if(output_chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    CHECK(output_events == source_events);
    REQUIRE(output_chart.measures[1].bgm_events.size() == 1);
    CHECK(absolute(output_chart, output_chart.measures[1].bgm_events.front().position) ==
          absolute(source_chart, source_chart.measures[1].bgm_events.front().position));
}

TEST_CASE("BMSON writer reports unsupported state and exact-number losses with typed paths",
          "[unit][format][bmson][writer]") {
    auto imported = fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(imported.song.has_value());
    auto& chart = imported.song->charts.front();
    chart.warps.push_back({.id = wormedit::object_id_t{900000},
                           .span = {chart.bpms.front().position, chart.bpms.back().position}});
    const auto warp = wormedit::write_bmson(*imported.song);
    CHECK_FALSE(warp.json.has_value());
    CHECK(has_write_error(warp, wormedit::bmson_write_diagnostic_code_t::unsupported_feature));
    CHECK(warp.diagnostics.front().json_path == "$");

    chart.warps.clear();
    chart.bpms.front().beats_per_minute = {100, 3};
    const auto number = wormedit::write_bmson(*imported.song);
    CHECK_FALSE(number.json.has_value());
    CHECK(has_write_error(number, wormedit::bmson_write_diagnostic_code_t::unrepresentable_number));
    CHECK((number.diagnostics.front().json_path == "$.info.init_bpm" ||
           number.diagnostics.front().json_path == "$.bpm_events[].bpm"));
}

TEST_CASE("BMSON writer selects a chart explicitly from multi-chart songs",
          "[unit][format][bmson][writer]") {
    auto imported = fixture("published-vox-slicing.bmson");
    REQUIRE(imported.song.has_value());
    auto second = imported.song->charts.front();
    second.id = wormedit::chart_id_t{2};
    imported.song->charts.push_back(std::move(second));

    const auto unspecified = wormedit::write_bmson(*imported.song);
    CHECK_FALSE(unspecified.json.has_value());
    CHECK(has_write_error(unspecified, wormedit::bmson_write_diagnostic_code_t::missing_chart));

    wormedit::bmson_write_options_t options;
    options.chart = wormedit::chart_id_t{2};
    const auto selected = wormedit::write_bmson(*imported.song, options);
    CHECK(selected.json.has_value());
}

TEST_CASE("BMSON writer preserves resource path spelling and can export newly authored notes",
          "[unit][format][bmson][writer]") {
    const auto imported = parse_text(R"({
      "version":"1.0.0",
      "info":{"title":"new chart","artist":"author","init_bpm":120,"mode_hint":"generic-nkeys"},
      "sound_channels":[
        {"name":"folder\\audio.wav","notes":[{"x":1,"y":0,"c":false}]},
        {"name":"folder\\audio.wav","notes":[{"x":2,"y":240,"c":false}]}
      ]
    })");
    REQUIRE(imported.song.has_value());
    const auto written = wormedit::write_bmson(*imported.song);
    REQUIRE(written.json.has_value());
    const auto reparsed = parse_text(*written.json);
    REQUIRE(reparsed.song.has_value());
    REQUIRE(reparsed.song->resources.size() == 2);
    CHECK(reparsed.song->audio_assets.size() == 1);
    CHECK(reparsed.song->resources.front().name == "folder\\audio.wav");
    CHECK(reparsed.song->audio_assets.front().source_path.generic_string() == "folder/audio.wav");

    auto authored = *imported.song;
    authored.charts.front().sound_channels.clear();
    for (auto& measure : authored.charts.front().measures) {
        for (auto& note : measure.notes) note.sound_channel_event.reset();
        for (auto& event : measure.bgm_events) event.sound_channel_event.reset();
    }
    const auto exported_authored = wormedit::write_bmson(authored);
    REQUIRE(exported_authored.json.has_value());
    const auto authored_roundtrip = parse_text(*exported_authored.json);
    REQUIRE(authored_roundtrip.song.has_value());
    CHECK(authored_roundtrip.song->charts.front().sound_channels.size() == 2);
    CHECK(note_signature(*authored_roundtrip.song) == note_signature(authored));
}

TEST_CASE("BMSON writer preserves generic 24K and 48K mode identities",
          "[unit][format][bmson][writer][layout]") {
    for (const auto mode : {"generic-24keys", "generic-48keys"}) {
        CAPTURE(mode);
        const auto x = std::string_view(mode) == "generic-24keys" ? 24 : 48;
        const auto imported = parse_text(
            "{\"version\":\"1.0.0\",\"info\":{\"init_bpm\":120,\"mode_hint\":\"" +
            std::string(mode) +
            "\"},\"sound_channels\":[{\"name\":\"a.wav\",\"notes\":[{\"x\":" +
            std::to_string(x) + " ,\"y\":0}]}]}");
        REQUIRE(imported.song.has_value());
        const auto written = wormedit::write_bmson(*imported.song);
        REQUIRE(written.json.has_value());
        CHECK(written.json->find("\"mode_hint\": \"" + std::string(mode) + "\"") !=
              std::string::npos);
        const auto reparsed = parse_text(*written.json);
        REQUIRE(reparsed.song.has_value());
        CHECK(reparsed.song->charts.front().lane_layouts.front().layout.profile_id ==
              "bmson-" + std::string(mode));
    }
}

TEST_CASE("BMSON writer flattens semantic layers without changing note gameplay types",
          "[unit][format][bmson][writer][layers]") {
    auto imported = fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(imported.song.has_value());
    auto& chart = imported.song->charts.front();
    chart.semantic_layers.push_back({
        .id = wormedit::layer_id_t{1},
        .name = "Vocal",
        .color = {255, 255, 0, 255},
    });
    REQUIRE_FALSE(chart.measures.front().notes.empty());
    chart.measures.front().notes.front().semantic_layers = {wormedit::layer_id_t{1}};
    const auto before = note_signature(*imported.song);

    const auto written = wormedit::write_bmson(*imported.song);
    REQUIRE(written.json.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(std::ranges::any_of(written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
               diagnostic.code == wormedit::bmson_write_diagnostic_code_t::lossy_transform;
    }));
    const auto reparsed = parse_text(*written.json);
    REQUIRE(reparsed.song.has_value());
    CHECK(note_signature(*reparsed.song) == before);
    CHECK(reparsed.song->charts.front().semantic_layers.empty());
}

TEST_CASE("BMSON writer omits bookmarks with a nonfatal loss diagnostic",
          "[unit][format][bmson][writer][bookmarks]") {
    auto imported = fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(imported.song.has_value());
    auto& chart = imported.song->charts.front();
    chart.bookmarks.push_back({
        .id = wormedit::object_id_t{900001},
        .label = "Drop",
        .anchor = wormedit::marker_anchor_t::beat,
        .position = chart.measures.front().notes.front().position,
    });

    const auto written = wormedit::write_bmson(*imported.song);
    REQUIRE(written.json.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(std::ranges::any_of(written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
               diagnostic.code == wormedit::bmson_write_diagnostic_code_t::lossy_transform &&
               diagnostic.message.find("bookmarks") != std::string::npos;
    }));
    const auto reparsed = parse_text(*written.json);
    REQUIRE(reparsed.song.has_value());
    CHECK(reparsed.song->charts.front().bookmarks.empty());
}

TEST_CASE("BMSON writer treats retained sound-channel events as the authoritative projection",
          "[unit][format][bmson][writer][sound-channel]") {
    auto imported = parse_text(R"({
      "version":"1.0.0",
      "info":{"init_bpm":120,"mode_hint":"generic-nkeys"},
      "sound_channels":[{"name":"a.wav","notes":[{"x":1,"y":0}]}]
    })");
    REQUIRE(imported.song.has_value());
    auto& chart = imported.song->charts.front();
    REQUIRE(chart.sound_channels.front().events.size() == 1);
    chart.sound_channels.front().events.front().position.row = 120;

    const auto written = wormedit::write_bmson(*imported.song);
    REQUIRE(written.json.has_value());
    CHECK(std::ranges::any_of(written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
               diagnostic.code == wormedit::bmson_write_diagnostic_code_t::lossy_transform;
    }));
    const auto reparsed = parse_text(*written.json);
    REQUIRE(reparsed.song.has_value());
    REQUIRE(reparsed.song->charts.front().sound_channels.front().events.size() == 1);
    CHECK(reparsed.song->charts.front().sound_channels.front().events.front().position.row == 120);
    REQUIRE(reparsed.song->charts.front().measures.front().notes.size() == 1);
    CHECK(reparsed.song->charts.front().measures.front().notes.front().position.row == 120);
}

TEST_CASE("BMSON writer keeps channel extensions attached after channel reordering",
          "[unit][format][bmson][writer][retention]") {
    auto imported = parse_text(R"({
      "version":"1.0.0",
      "info":{"init_bpm":120,"mode_hint":"generic-nkeys"},
      "sound_channels":[
        {"name":"a.wav","vendor":"A","notes":[{"x":1,"y":0}]},
        {"name":"b.wav","vendor":"B","notes":[{"x":2,"y":240}]}
      ]
    })");
    REQUIRE(imported.song.has_value());
    auto& channels = imported.song->charts.front().sound_channels;
    std::ranges::reverse(channels);

    const auto written = wormedit::write_bmson(*imported.song);
    REQUIRE(written.json.has_value());
    const auto json = nlohmann::json::parse(*written.json);
    REQUIRE(json["sound_channels"].size() == 2);
    CHECK(json["sound_channels"][0]["name"] == "b.wav");
    CHECK(json["sound_channels"][0]["vendor"] == "B");
    CHECK(json["sound_channels"][1]["name"] == "a.wav");
    CHECK(json["sound_channels"][1]["vendor"] == "A");
}

TEST_CASE("BMSON writer rejects unequal layered hold lengths and rooted backslash paths",
          "[unit][format][bmson][writer][validation]") {
    auto imported = parse_text(R"({
      "version":"1.0.0",
      "info":{"init_bpm":120,"mode_hint":"generic-nkeys"},
      "sound_channels":[
        {"name":"a.wav","notes":[{"x":1,"y":0}]},
        {"name":"b.wav","notes":[{"x":1,"y":240}]}
      ]
    })");
    REQUIRE(imported.song.has_value());
    auto& channels = imported.song->charts.front().sound_channels;
    channels[0].events[0].end = wormedit::local_position_t{
        channels[0].events[0].position.measure, 120};
    channels[1].events[0].position = channels[0].events[0].position;
    channels[1].events[0].end = wormedit::local_position_t{
        channels[0].events[0].position.measure, 240};
    const auto unequal = wormedit::write_bmson(*imported.song);
    CHECK_FALSE(unequal.json.has_value());
    CHECK(has_write_error(unequal, wormedit::bmson_write_diagnostic_code_t::duplicate_event));

    channels[1].events[0].position.row = 240;
    channels[1].events[0].end.reset();
    imported.song->audio_assets.front().source_path = std::filesystem::path{"\\root.wav"};
    const auto rooted = wormedit::write_bmson(*imported.song);
    CHECK_FALSE(rooted.json.has_value());
    CHECK(has_write_error(rooted, wormedit::bmson_write_diagnostic_code_t::unresolved_resource));
}

TEST_CASE("BMSON export bakes recipes before reporting unsupported regular scroll events",
          "[unit][format][bmson][writer][sv-recipe]") {
    const auto imported = fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(imported.song.has_value());
    auto song = *imported.song;
    auto& chart = song.charts.front();
    REQUIRE(chart.measures.size() >= 2);
    chart.sv_recipes.push_back({
        .id = wormedit::recipe_id_t{900003},
        .name = "export guard",
        .range = {{chart.measures[0].id, 0}, {chart.measures[1].id, 0}},
        .output = wormedit::sv_recipe_output_t::sv,
        .curve = wormedit::sv_recipe_curve_t::constant,
        .parameters = {.value_from = {2, 1}},
    });
    const auto written = wormedit::write_bmson(song);
    CHECK_FALSE(written.json.has_value());
    CHECK(has_write_error(written, wormedit::bmson_write_diagnostic_code_t::unsupported_feature));
}
