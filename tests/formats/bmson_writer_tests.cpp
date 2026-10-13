#include <wormedit/formats/bmson.h>
#include <wormedit/core/metadata_editor.h>
#include <wormedit/core/note_to_bgm.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
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

auto packed_marker_retains_its_channel(const wormedit::chart_document_t& chart) -> bool {
    if (chart.sound_channels.size() < 2 || chart.bgm_source_lanes.size() < 2 ||
        chart.bgm_source_lanes[1].stem_channel) return false;
    const auto& channel = chart.sound_channels[1];
    for (const auto& measure : chart.measures) {
        const auto event = std::ranges::find_if(measure.bgm_events, [&](const auto& bgm) {
            return bgm.source_lane == chart.bgm_source_lanes[1].id && bgm.position.row == 480;
        });
        if (event == measure.bgm_events.end() || !event->sound_channel_event) continue;
        return std::ranges::find(channel.events, *event->sound_channel_event,
                                 &wormedit::sound_channel_event_t::id) != channel.events.end();
    }
    return false;
}

void clear_retained_channels(wormedit::chart_document_t& chart) {
    chart.sound_channels.clear();
    for (auto& lane : chart.bgm_source_lanes) lane.stem_channel.reset();
}

auto packed_events_and_empty_stem_export_correctly() -> bool {
    auto imported = parse_text(R"({
      "version":"1.0.0",
      "info":{"title":"hybrid","init_bpm":120,"mode_hint":"generic-nkeys"},
      "sound_channels":[
        {"name":"stem.wav","notes":[{"x":1,"y":0,"c":false},{"x":1,"y":240,"c":true}]},
        {"name":"packed.wav","notes":[{"x":0,"y":480,"c":false}]}
      ]
    })");
    if (!imported.song || imported.song->charts.empty()) return false;
    auto& chart = imported.song->charts.front();
    if (chart.sound_channels.size() != 2 || chart.bgm_source_lanes.size() < 2) return false;
    chart.bgm_source_lanes[0].stem_channel = chart.sound_channels[0].id;
    chart.bgm_source_lanes[1].stem_channel.reset();
    if (!packed_marker_retains_its_channel(chart)) return false;
    const auto packed_channel_name = chart.sound_channels[1].name;
    auto empty = chart.sound_channels.front();
    empty.id = wormedit::object_id_t{990001};
    empty.name = "empty stem";
    empty.events.clear();
    chart.sound_channels.push_back(std::move(empty));
    chart.bgm_source_lanes.push_back({.id = wormedit::object_id_t{990002},
        .name = "Empty stem", .source_order = 2,
        .stem_channel = chart.sound_channels.back().id});

    const auto written = wormedit::write_bmson(*imported.song);
    if (!written.json || written.has_errors()) return false;
    const auto json = nlohmann::json::parse(*written.json);
    if (!json.contains("sound_channels") || json["sound_channels"].size() != 3 ||
        json["sound_channels"][1]["name"] != packed_channel_name ||
        json["sound_channels"][1]["notes"].size() != 1 ||
        json["sound_channels"][1]["notes"][0]["x"] != 0 ||
        json["sound_channels"][1]["notes"][0]["y"] != 480 ||
        !json["sound_channels"][2]["notes"].empty()) return false;
    return std::ranges::any_of(written.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == wormedit::diagnostic_severity_t::warning &&
               diagnostic.message == "stem empty stem has no points";
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

TEST_CASE("BMSON writer persists editable info metadata through export and reload",
          "[unit][format][bmson][writer][metadata]") {
    auto imported = fixture("slicing-and-mixed-sounds.bmson");
    REQUIRE(imported.song);
    auto history = wormedit::document_history_t::from(std::move(*imported.song));
    const auto chart_id = history.document.charts.front().id;
    const wormedit::format_metadata_target_t song_target{
        std::nullopt, wormedit::format_family_t::bmson};
    const wormedit::format_metadata_target_t chart_target{
        chart_id, wormedit::format_family_t::bmson};
    const auto find_field = [](const auto& group, std::string_view name) {
        const auto found = std::ranges::find(group.ordered_fields, name,
            [](const auto& field) -> std::string_view { return field.first; });
        return static_cast<std::size_t>(std::distance(group.ordered_fields.begin(), found));
    };
    const auto& song_group = history.document.metadata.format_groups.front();
    const auto genre_index = find_field(song_group, "info.genre");
    REQUIRE(genre_index < song_group.ordered_fields.size());
    REQUIRE(wormedit::apply_metadata_edit(
        history, history.document.revision,
        wormedit::edit_format_metadata_field_action_t{
            .target = song_target,
            .index = genre_index,
            .name = "info.genre",
            .value = "\"electronic\"",
            .mode = wormedit::format_metadata_field_mode_t::replace}));
    const auto& chart_group = history.document.charts.front().metadata.format_groups.front();
    CHECK(chart_group.ordered_fields[genre_index].second == "\"electronic\"");
    const auto rank_index = find_field(chart_group, "info.judge_rank");
    const auto total_index = find_field(chart_group, "info.total");
    REQUIRE(rank_index < chart_group.ordered_fields.size());
    REQUIRE(total_index < chart_group.ordered_fields.size());
    REQUIRE(wormedit::apply_metadata_edit(
        history, history.document.revision,
        wormedit::edit_format_metadata_field_action_t{
            .target = chart_target,
            .index = rank_index,
            .name = "info.judge_rank",
            .value = "115.5",
            .mode = wormedit::format_metadata_field_mode_t::replace}));
    REQUIRE(wormedit::apply_metadata_edit(
        history, history.document.revision,
        wormedit::edit_format_metadata_field_action_t{
            .target = song_target,
            .index = total_index,
            .name = "info.total",
            .value = "220",
            .mode = wormedit::format_metadata_field_mode_t::replace}));
    const auto append_info = [&](std::string name, std::string value) {
        const auto count = history.document.charts.front().metadata.format_groups.front()
                               .ordered_fields.size();
        return wormedit::apply_metadata_edit(
            history, history.document.revision,
            wormedit::edit_format_metadata_field_action_t{
                .target = chart_target,
                .index = count,
                .name = std::move(name),
                .value = std::move(value),
                .mode = wormedit::format_metadata_field_mode_t::insert});
    };
    REQUIRE(append_info("info.subtitle", "\"A new subtitle\""));
    REQUIRE(append_info("info.subartists", "[\"music:Guest\",\"chart:Mapper\"]"));
    REQUIRE(wormedit::apply_metadata_edit(
        history, history.document.revision,
        wormedit::set_song_metadata_action_t{
            history.document.metadata.title, history.document.metadata.artist, {"catalog tag"}}));

    const auto written = wormedit::write_bmson(history.document);
    REQUIRE(written.json);
    const auto json = nlohmann::json::parse(*written.json);
    CHECK(json["info"]["subtitle"] == "A new subtitle");
    CHECK(json["info"]["genre"] == "electronic");
    CHECK(json["info"]["subartists"] ==
          nlohmann::json::array({"music:Guest", "chart:Mapper"}));
    CHECK(json["info"]["judge_rank"] == 115.5);
    CHECK(json["info"]["total"] == 220);
    const auto reloaded = parse_text(*written.json);
    REQUIRE(reloaded.song);
    const auto& fields = reloaded.song->charts.front().metadata.format_groups.front()
                             .ordered_fields;
    CHECK(std::ranges::any_of(fields, [](const auto& field) {
        return field.first == "info.subtitle" && field.second == "\"A new subtitle\"";
    }));

    const auto saved = wormedit::write_native_project(history.document);
    REQUIRE(saved.bytes);
    auto reopened = wormedit::parse_native_project(*saved.bytes);
    REQUIRE(reopened.song);
    auto resumed = wormedit::document_history_t::from(std::move(*reopened.song));
    const auto& reopened_song_group = resumed.document.metadata.format_groups.front();
    const auto reopened_genre_index = find_field(reopened_song_group, "info.genre");
    REQUIRE(reopened_genre_index < reopened_song_group.ordered_fields.size());
    REQUIRE(wormedit::apply_metadata_edit(
        resumed, resumed.document.revision,
        wormedit::edit_format_metadata_field_action_t{
            .target = song_target,
            .index = reopened_genre_index,
            .name = "info.genre",
            .value = "\"post-save genre\"",
            .mode = wormedit::format_metadata_field_mode_t::replace}));
    CHECK(resumed.document.charts.front().metadata.format_groups.front()
              .ordered_fields[reopened_genre_index].second == "\"post-save genre\"");
    const auto after_reopen = wormedit::write_bmson(resumed.document);
    REQUIRE(after_reopen.json);
    CHECK(nlohmann::json::parse(*after_reopen.json)["info"]["genre"] == "post-save genre");

    auto invalid = wormedit::document_history_t::from(history.document);
    const auto& invalid_group = invalid.document.charts.front().metadata.format_groups.front();
    const auto subtitle_index = find_field(invalid_group, "info.subtitle");
    REQUIRE(subtitle_index < invalid_group.ordered_fields.size());
    REQUIRE(wormedit::apply_metadata_edit(
        invalid, invalid.document.revision,
        wormedit::edit_format_metadata_field_action_t{
            .target = chart_target,
            .index = subtitle_index,
            .name = "info.subtitle",
            .value = "unquoted subtitle",
            .mode = wormedit::format_metadata_field_mode_t::replace}));
    const auto invalid_export = wormedit::write_bmson(invalid.document);
    CHECK_FALSE(invalid_export.json);
    REQUIRE_FALSE(invalid_export.diagnostics.empty());
    CHECK(invalid_export.diagnostics.front().json_path == "$.info.subtitle");
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
    clear_retained_channels(authored.charts.front());
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

TEST_CASE("BMSON writer retains packed events beside an empty explicit stem",
          "[unit][format][bmson][writer][stems]") {
    CHECK(packed_events_and_empty_stem_export_correctly());
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

namespace {
template <typename T> void require_bgm_tail(const T& value) { REQUIRE(static_cast<bool>(value)); }
void check_bgm_tail(bool value) { CHECK(value); }
}

TEST_CASE("BMSON writer preserves retained and authored BGM hold tails",
          "[unit][core][bmson][t423-codec]") {
    auto parsed = parse_text(R"({"version":"1.0.0","info":{"init_bpm":120,
        "mode_hint":"beat-7k","resolution":240},"sound_channels":[{"name":"hold.wav",
        "notes":[{"x":1,"y":0,"l":120,"c":false},
                 {"x":0,"y":300,"l":900,"c":false}]}]})");
    require_bgm_tail(parsed.song);
    auto& chart = parsed.song->charts.front();
    SECTION("retained channel") {}
    SECTION("authored semantic events") {
        clear_retained_channels(chart);
        for (auto& measure : chart.measures) {
            for (auto& note : measure.notes) note.sound_channel_event.reset();
            for (auto& bgm : measure.bgm_events) bgm.sound_channel_event.reset();
        }
    }
    const auto written = wormedit::write_bmson(*parsed.song);
    INFO((written.diagnostics.empty() ? "no diagnostic" : written.diagnostics.front().message));
    require_bgm_tail(written.json);
    const auto reparsed = parse_text(*written.json);
    require_bgm_tail(reparsed.song);
    const auto& output = reparsed.song->charts.front();
    const auto measure = std::ranges::find_if(output.measures,
        [](const auto& value) { return !value.bgm_events.empty(); });
    require_bgm_tail(measure != output.measures.end());
    const auto& bgm = measure->bgm_events.front();
    require_bgm_tail(bgm.end);
    check_bgm_tail(absolute(output, bgm.position) == 300);
    check_bgm_tail(absolute(output, *bgm.end) == 1200);
    check_bgm_tail(note_signature(*reparsed.song) == note_signature(*parsed.song));
}

TEST_CASE("BMSON converted playable hold roundtrips as BGM with the same source end",
          "[unit][core][bmson][t423-codec]") {
    auto parsed = parse_text(R"({"version":"1.0.0","info":{"init_bpm":120,
        "mode_hint":"beat-7k","resolution":240},"sound_channels":[{"name":"hold.wav",
        "notes":[{"x":1,"y":300,"l":900,"c":false}]}]})");
    require_bgm_tail(parsed.song);
    auto history = wormedit::document_history_t::from(*parsed.song);
    const auto& original = history.document.charts.front();
    const auto at = std::ranges::find_if(original.measures,
        [](const auto& value) { return !value.notes.empty(); });
    require_bgm_tail(at != original.measures.end());
    const std::array ids{at->notes.front().id};
    require_bgm_tail(wormedit::move_notes_to_bgm(history, history.document.revision,
                                         original.id, ids, std::nullopt));
    const auto written = wormedit::write_bmson(history.document);
    INFO((written.diagnostics.empty() ? "no diagnostic" : written.diagnostics.front().message));
    require_bgm_tail(written.json);
    const auto reparsed = parse_text(*written.json);
    require_bgm_tail(reparsed.song);
    const auto& chart = reparsed.song->charts.front();
    const auto measure = std::ranges::find_if(chart.measures,
        [](const auto& value) { return !value.bgm_events.empty(); });
    require_bgm_tail(measure != chart.measures.end());
    require_bgm_tail(measure->bgm_events.front().end);
    check_bgm_tail(absolute(chart, *measure->bgm_events.front().end) == 1200);
    check_bgm_tail(chart.sound_channels.front().events.size() == 1);
    check_bgm_tail(!chart.sound_channels.front().events.front().playable);
}
