#include <wormedit/core/storyboard.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/bms_writer.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>

#include "formats/native_project_migrations.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
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

auto has_code(const std::vector<wormedit::native_project_diagnostic_t>& diagnostics,
              wormedit::native_project_diagnostic_code_t code) -> bool {
    return std::ranges::any_of(diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

auto fixture_path(std::string_view family, std::string_view name) -> std::filesystem::path {
    return std::filesystem::path(WORMEDIT_FIXTURE_DIR) / family / name;
}

auto rich_bmson_song() -> wormedit::song_document_t {
    const auto bytes = read_binary(fixture_path("bmson", "slicing-and-mixed-sounds.bmson"));
    auto parsed = wormedit::parse_bmson_bytes(bytes);
    if (!parsed.song.has_value()) {
        return {};
    }
    auto song = std::move(*parsed.song);
    song.revision = 42;
    song.metadata.ordered_search_tags = {"vocal", "challenge"};
    auto& chart = song.charts.front();
    chart.metadata.rating = wormedit::signed_rational_t{13, 2};
    chart.semantic_layers.push_back({
        .id = wormedit::layer_id_t{900},
        .name = "Vocals",
        .color = {255, 220, 0, 192},
        .visible = false,
        .locked = true,
    });
    for (auto& measure : chart.measures) {
        if (!measure.notes.empty()) {
            measure.notes.front().semantic_layers = {wormedit::layer_id_t{900}};
            measure.notes.front().label = "pickup";
        }
        if (!measure.bgm_events.empty())
            measure.bgm_events.front().semantic_layers = {wormedit::layer_id_t{900}};
    }
    chart.bookmarks.push_back({
        .id = wormedit::object_id_t{901},
        .label = "mix check",
        .anchor = wormedit::marker_anchor_t::audio_time,
        .audio_milliseconds = 1250,
    });
    chart.sections.push_back({
        .id = wormedit::object_id_t{902},
        .label = "verse",
        .anchor = wormedit::marker_anchor_t::beat,
        .beat_span = wormedit::position_span_t{{chart.measures.front().id, 0},
                                               {chart.measures.back().id, 0}},
    });
    song.midi_assets.push_back({
        .id = wormedit::asset_id_t{903},
        .source_path = std::filesystem::path{"midi/reference.mid"},
        .ticks_per_quarter = 480,
        .events = {{.tick = 240,
                    .channel = 2,
                    .data = {std::byte{0x90}, std::byte{0x3c}, std::byte{0x7f}}}},
    });
    if (!song.audio_assets.empty()) {
        song.generator_recipes.push_back({
            .id = wormedit::recipe_id_t{904},
            .api_version = "1",
            .source_path = std::filesystem::path{"scripts/pattern.lua"},
            .input_assets = {song.audio_assets.front().id},
            .parameters = {{.name = "density", .serialized_value = "0.75"}},
            .seed = 0x1234,
            .source_hash = "sha256:fixture",
        });
    }
    const auto parsed_storyboard =
        wormedit::parse_osb("[Events]\n"
                            "Sprite,Foreground,Centre,\"sprites/a.png\",320,240\n"
                            " L,100,2\n"
                            "  L,20,3\n"
                            "   F,0,0,250,0,1\n"
                            "   _P,0,1,2,H\n"
                            "[RetainedDirective]\n");
    song.storyboards.push_back({
        .id = wormedit::storyboard_id_t{905},
        .name = "Main",
        .retained_source_lines = {"Sprite,Background,Centre,background.png"},
        .model = parsed_storyboard.document,
    });
    const auto first_measure = chart.measures.front().id;
    chart.sv_recipes.push_back({
        .id = wormedit::recipe_id_t{10000},
        .name = "chorus scroll",
        .range = {{first_measure, 0}, {first_measure, 1}},
        .output = wormedit::sv_recipe_output_t::scrolls,
        .curve = wormedit::sv_recipe_curve_t::linear,
        .parameters = {.value_from = {3, 4}, .value_to = {2, 1}},
    });
    song.output_groups.push_back({
        .name = "BMSON release",
        .family = wormedit::format_family_t::bmson,
        .charts = {chart.id},
        .relative_output_path = std::filesystem::path{"exports/chart.bmson"},
    });
    song.quantization_presets.push_back({
        .name = "Triplet ends",
        .divisions = 3,
        .space = wormedit::quantization_space_t::authored_measure,
        .tie = wormedit::quantization_tie_t::later,
        .holds = wormedit::quantization_hold_t::start_only,
    });
    return song;
}

auto prepare_bgm_event(wormedit::song_document_t& song, bool assign_layer) -> bool {
    auto& chart = song.charts.front();
    const auto lane_id = wormedit::object_id_t{987'654'322};
    const auto audio_resource = std::ranges::find_if(song.resources, [](const auto& resource) {
        return std::ranges::any_of(resource.roles, [](const auto role) {
            return role == wormedit::resource_role_t::sample ||
                   role == wormedit::resource_role_t::bgm ||
                   role == wormedit::resource_role_t::music;
        });
    });
    if (chart.source_family == wormedit::format_family_t::bmson &&
        audio_resource == song.resources.end()) return false;
    const auto sound = chart.source_family == wormedit::format_family_t::bmson
                           ? std::optional{audio_resource->id}
                           : std::optional<wormedit::resource_id_t>{};
    if (chart.bgm_source_lanes.empty())
        chart.bgm_source_lanes.push_back({.id = lane_id, .name = "BGM"});
    const auto any_events = std::ranges::any_of(chart.measures, [](const auto& measure) {
        return !measure.bgm_events.empty();
    });
    if (!any_events && !chart.measures.empty()) {
        chart.measures.front().bgm_events.push_back({.id = wormedit::object_id_t{987'654'321},
            .position = {chart.measures.front().id, 0},
            .source_lane = chart.bgm_source_lanes.front().id, .sound = sound});
    }
    if (assign_layer && std::ranges::none_of(chart.semantic_layers, [](const auto& layer) {
            return layer.id == wormedit::layer_id_t{900};
        })) {
        chart.semantic_layers.push_back({.id = wormedit::layer_id_t{900}, .name = "BGM",
                                         .color = {20, 40, 60, 255}});
    }
    auto found_event = false;
    for (auto& measure : chart.measures)
        for (auto& event : measure.bgm_events) {
            if (!event.sound && sound) event.sound = sound;
            if (assign_layer) event.semantic_layers = {wormedit::layer_id_t{900}};
            found_event = true;
        }
    return found_event;
}

struct export_parity_result_t {
    bool matches{};
    std::string diagnostic;
};

auto bms_layer_export_matches() -> export_parity_result_t {
    const auto bytes = read_binary(fixture_path("bms", "bgm-columns-per-measure.bms"));
    auto imported = wormedit::parse_bms_bytes(bytes);
    if (!imported.song) return {.diagnostic = "BMS fixture did not parse"};
    if (!prepare_bgm_event(*imported.song, false))
        return {.diagnostic = "BMS fixture has no usable BGM event"};
    auto layered = *imported.song;
    if (!prepare_bgm_event(layered, true))
        return {.diagnostic = "Could not assign the BGM semantic layer"};
    const wormedit::bms_write_options_t normalized{
        .lane_profile_id = "bms-5k-outside-scratch",
        .output_encoding = wormedit::text_encoding_t::utf8};
    const auto plain = wormedit::write_bms(*imported.song, normalized);
    const auto tagged = wormedit::write_bms(layered, normalized);
    if (!plain.bytes || !tagged.bytes) {
        const auto& diagnostics = plain.bytes ? tagged.diagnostics : plain.diagnostics;
        return {.diagnostic = diagnostics.empty() ? "BMS writer returned no output"
                                                   : diagnostics.front().message};
    }
    return {.matches = *plain.bytes == *tagged.bytes};
}

auto bmson_layer_export_matches() -> export_parity_result_t {
    const auto bytes = read_binary(fixture_path("bmson", "slicing-and-mixed-sounds.bmson"));
    auto imported = wormedit::parse_bmson_bytes(bytes);
    if (!imported.song) return {.diagnostic = "BMSON fixture did not parse"};
    if (!prepare_bgm_event(*imported.song, false))
        return {.diagnostic = "BMSON fixture has no usable BGM event"};
    auto layered = *imported.song;
    if (!prepare_bgm_event(layered, true))
        return {.diagnostic = "Could not assign the BGM semantic layer"};
    const auto plain = wormedit::write_bmson(*imported.song);
    const auto tagged = wormedit::write_bmson(layered);
    if (!plain.json || !tagged.json) {
        const auto& diagnostics = plain.json ? tagged.diagnostics : plain.diagnostics;
        return {.diagnostic = diagnostics.empty() ? "BMSON writer returned no output"
                                                   : diagnostics.front().message};
    }
    return {.matches = *plain.json == *tagged.json};
}

auto empty_group_schema_keeps_previous_bytes() -> bool {
    const auto song = rich_bmson_song();
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes);
    CHECK(written.bytes->find("sound_groups") == std::string::npos);
    CHECK(written.bytes->find("sound_group\"") == std::string::npos);
    const auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song);
    const auto rewritten = wormedit::write_native_project(*parsed.song);
    REQUIRE(rewritten.bytes);
    CHECK(*rewritten.bytes == *written.bytes);
    return true;
}

auto populated_sound_group_round_trips() -> bool {
    auto song = rich_bmson_song();
    const auto sound = std::ranges::find_if(song.resources, [](const auto& resource) {
        return std::ranges::find(resource.roles, wormedit::resource_role_t::sample) !=
               resource.roles.end();
    });
    REQUIRE(sound != song.resources.end());
    song.sound_groups = {{.id = wormedit::sound_group_id_t{700}, .name = "Percussion"}};
    sound->sound_group = wormedit::sound_group_id_t{700};
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes);
    const auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song);
    CHECK(parsed.song->sound_groups == song.sound_groups);
    const auto parsed_sound = std::ranges::find_if(parsed.song->resources,
        [](const auto& resource) {
            return std::ranges::find(resource.roles, wormedit::resource_role_t::sample) !=
                   resource.roles.end();
        });
    REQUIRE(parsed_sound != parsed.song->resources.end());
    CHECK(parsed_sound->sound_group == wormedit::sound_group_id_t{700});
    return true;
}

auto legacy_schema3_bms_document() -> nlohmann::json {
    const std::string source = "#TITLE layers\n#PLAYER 1\n#BMP01 bg.png\n#BMP02 layer.png\n"
                              "#BMP03 layer2.png\n#00004:01\n#00007:02\n#0000A:03\n";
    const auto imported = wormedit::parse_bms_bytes(
        std::as_bytes(std::span{source.data(), source.size()}));
    if (!imported.song) return {};
    const auto written = wormedit::write_native_project(*imported.song);
    if (!written.bytes) return {};
    auto legacy = nlohmann::json::parse(*written.bytes);
    auto& events = legacy["document"]["charts"][0]["bga_events"];
    for (auto& event : events) {
        if (event["kind"] == std::uint64_t{5}) event["kind"] = std::uint64_t{2};
    }
    legacy["document"]["schema_version"] = std::uint64_t{3};
    return legacy;
}

auto schema3_migrates_layer2_by_provenance(const nlohmann::json& legacy) -> bool {
    const auto migrated = wormedit::parse_native_project(legacy.dump());
    if (!migrated.song) return false;
    const auto& events = migrated.song->charts.front().bga_events;
    return events.size() == 3 && events[1].kind == wormedit::visual_event_kind_t::layer &&
           events[2].kind == wormedit::visual_event_kind_t::layer2;
}

auto schema3_without_provenance_keeps_legacy_layer(nlohmann::json legacy) -> bool {
    auto& event = legacy["document"]["charts"][0]["bga_events"][2];
    event["provenance"]["source_field"] = "legacy visual layer";
    const auto migrated = wormedit::parse_native_project(legacy.dump());
    return migrated.song && migrated.song->charts.front().bga_events[2].kind ==
                                wormedit::visual_event_kind_t::layer;
}

auto writing_schema3_keeps_layer2_provenance(nlohmann::json legacy) -> bool {
    const auto parsed = wormedit::parse_native_project(legacy.dump());
    if (!parsed.song) return false;
    auto song = *parsed.song;
    song.schema_version = 3;
    for (auto& event : song.charts.front().bga_events) {
        if (event.provenance.source_field.starts_with("0A slot "))
            event.kind = wormedit::visual_event_kind_t::layer;
    }
    const auto written = wormedit::write_native_project(song);
    if (!written.bytes) return false;
    const auto document = nlohmann::json::parse(*written.bytes);
    if (document["document"]["charts"][0]["bga_events"][2]["kind"] != std::uint64_t{5})
        return false;
    const auto roundtrip = wormedit::parse_native_project(*written.bytes);
    return roundtrip.song && roundtrip.song->charts.front().bga_events[2].kind ==
                                 wormedit::visual_event_kind_t::layer2;
}

struct migration_precision_result_t {
    bool migrated{};
    bool kind_preserved{};
    bool family_preserved{};
};

auto migration_does_not_narrow_enum_values(const nlohmann::json& legacy)
    -> migration_precision_result_t {
    auto document = legacy.at("document");
    auto& event = document["charts"][0]["bga_events"][2];
    const auto large_kind = (std::uint64_t{1} << 32U) + 2;
    const auto large_family = (std::uint64_t{1} << 32U) + 1;
    event["kind"] = large_kind;
    event["provenance"]["family"] = large_family;
    const auto outcome = wormedit::native_project_internal::migrate_document(
        document, 4, wormedit::native_project_internal::default_migrations());
    return {
        .migrated = outcome.status ==
                    wormedit::native_project_internal::migration_status_t::migrated,
        .kind_preserved = document["charts"][0]["bga_events"][2]["kind"] == large_kind,
        .family_preserved =
            document["charts"][0]["bga_events"][2]["provenance"]["family"] == large_family,
    };
}

} // namespace

namespace {
template <typename T>
void check_bmson_condition(const T& condition) { CHECK(static_cast<bool>(condition)); }
template <typename T>
void require_bmson_condition(const T& condition) { REQUIRE(static_cast<bool>(condition)); }
} // namespace

TEST_CASE("native project deterministically round-trips rich editor state",
          "[unit][format][native-project]") {
    const auto song = rich_bmson_song();
    auto round_trip_song = song;
    REQUIRE_FALSE(round_trip_song.charts.empty());
    REQUIRE(prepare_bgm_event(round_trip_song, true));
    REQUIRE(wormedit::validate_document(round_trip_song).empty());

    const auto written = wormedit::write_native_project(round_trip_song);
    REQUIRE(written.bytes.has_value());
    CHECK_FALSE(written.has_errors());
    CHECK(written.bytes->find("\"wormedit-project\"") != std::string::npos);
    CHECK(written.bytes->find("\"generator_recipes\"") != std::string::npos);
    CHECK(written.bytes->find("\"semantic_layers\"") != std::string::npos);
    CHECK(written.bytes->find("\"sound_channel_event\"") != std::string::npos);

    const auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song.has_value());
    CHECK_FALSE(parsed.has_errors());
    CHECK(parsed.song->revision == 42);
    CHECK(parsed.song->charts.front().semantic_layers.front().name == "Vocals");
    const auto has_layered_bgm = std::ranges::any_of(
        parsed.song->charts.front().measures, [](const auto& measure) {
            return std::ranges::any_of(measure.bgm_events, [](const auto& event) {
                return event.semantic_layers == std::vector{wormedit::layer_id_t{900}};
            });
        });
    CHECK(has_layered_bgm);
    CHECK(parsed.song->midi_assets.front().events.front().data ==
          std::vector<std::byte>{std::byte{0x90}, std::byte{0x3c}, std::byte{0x7f}});
    REQUIRE_FALSE(parsed.song->generator_recipes.empty());
    CHECK(parsed.song->generator_recipes.front().source_path.generic_string() ==
          "scripts/pattern.lua");
    CHECK(parsed.song->output_groups.front().relative_output_path.generic_string() ==
          "exports/chart.bmson");
    CHECK(parsed.song->charts.front().bookmarks.back().audio_milliseconds == 1250);
    const auto& original_note = song.charts.front().measures.front().notes.front();
    const auto& parsed_note = parsed.song->charts.front().measures.front().notes.front();
    CHECK(parsed_note.sound_channel_event == original_note.sound_channel_event);
    REQUIRE(parsed.song->charts.front().sv_recipes.size() == 1);
    CHECK(parsed.song->charts.front().sv_recipes.front().id == wormedit::recipe_id_t{10000});
    CHECK(parsed.song->charts.front().sv_recipes.front().enabled);
    REQUIRE(parsed.song->charts.front().sections.back().beat_span.has_value());
    REQUIRE(song.charts.front().sections.back().beat_span.has_value());
    CHECK(parsed.song->charts.front().sections.back().beat_span->begin ==
          song.charts.front().sections.back().beat_span->begin);
    CHECK(parsed.song->charts.front().sections.back().beat_span->end ==
          song.charts.front().sections.back().beat_span->end);
    REQUIRE(parsed.song->storyboards.front().model.has_value());
    REQUIRE(parsed.song->storyboards.front().model->sprites.size() == 1);
    REQUIRE(parsed.song->storyboards.front().model->sprites.front().nodes.size() == 1);
    CHECK(parsed.song->storyboards.front().model->sprites.front().nodes.front().kind ==
          wormedit::storyboard_node_kind_t::loop);
    CHECK(parsed.song->storyboards.front()
              .model->sprites.front()
              .nodes.front()
              .children.front()
              .kind == wormedit::storyboard_node_kind_t::loop);
    CHECK(parsed.song->storyboards.front().model->retained_top_level_lines.size() == 2);
    const auto emitted_storyboard = wormedit::emit_osb(*parsed.song->storyboards.front().model);
    REQUIRE(emitted_storyboard);
    const auto reparsed_storyboard = wormedit::parse_osb(*emitted_storyboard.text);
    REQUIRE(reparsed_storyboard);
    CHECK(wormedit::storyboard_semantically_equal(*parsed.song->storyboards.front().model,
                                                  *reparsed_storyboard.document));

    const auto rewritten = wormedit::write_native_project(*parsed.song);
    REQUIRE(rewritten.bytes.has_value());
    CHECK(*rewritten.bytes == *written.bytes);

    auto legacy_links = nlohmann::json::parse(*written.bytes);
    for (auto& measure : legacy_links["document"]["charts"][0]["measures"]) {
        for (auto& note : measure["notes"]) note.erase("sound_channel_event");
        for (auto& event : measure["bgm_events"]) event.erase("sound_channel_event");
    }
    const auto parsed_legacy_links = wormedit::parse_native_project(legacy_links.dump());
    REQUIRE(parsed_legacy_links.song.has_value());
    for (const auto& measure : parsed_legacy_links.song->charts.front().measures) {
        CHECK(std::ranges::none_of(measure.notes, [](const auto& note) {
            return note.sound_channel_event.has_value();
        }));
        CHECK(std::ranges::none_of(measure.bgm_events, [](const auto& event) {
            return event.sound_channel_event.has_value();
        }));
    }
}

TEST_CASE("BMS and BMSON exporters ignore BGM semantic layer membership",
          "[unit][format][semantic-layer][export]") {
    const auto bms = bms_layer_export_matches();
    INFO(bms.diagnostic);
    CHECK(bms.matches);
    const auto bmson = bmson_layer_export_matches();
    INFO(bmson.diagnostic);
    CHECK(bmson.matches);
}

TEST_CASE("native projects round-trip flat sound groups without changing empty-group files",
          "[unit][format][native-project][sound-groups]") {
    CHECK(empty_group_schema_keeps_previous_bytes());
    CHECK(populated_sound_group_round_trips());
}

TEST_CASE("native projects round-trip quantization presets",
          "[unit][format][native-project][quantize-preset]") {
    const auto song = rich_bmson_song();
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());
    const auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->quantization_presets == song.quantization_presets);
}

TEST_CASE("native projects persist SV recipes but accept older documents without them",
          "[unit][format][native-project][sv-recipe]") {
    auto song = rich_bmson_song();
    REQUIRE(wormedit::validate_document(song).empty());
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());
    auto serialized = nlohmann::json::parse(*written.bytes);
    const auto& recipe_json = serialized["document"]["charts"][0]["sv_recipes"][0];
    CHECK(recipe_json.contains("range"));
    CHECK(recipe_json.contains("sampling"));
    CHECK_FALSE(recipe_json.contains("sampled_points"));
    CHECK_FALSE(recipe_json.contains("points"));

    auto legacy = serialized;
    legacy["document"]["charts"][0].erase("sv_recipes");
    const auto parsed_legacy = wormedit::parse_native_project(legacy.dump());
    REQUIRE(parsed_legacy.song.has_value());
    CHECK(parsed_legacy.song->charts.front().sv_recipes.empty());
    const auto migrated = wormedit::write_native_project(*parsed_legacy.song);
    REQUIRE(migrated.bytes.has_value());
    const auto migrated_json = nlohmann::json::parse(*migrated.bytes);
    CHECK(migrated_json["document"]["charts"][0]["sv_recipes"].empty());

    auto invalid_enum = serialized;
    invalid_enum["document"]["charts"][0]["sv_recipes"][0]["output"] = 255;
    const auto rejected = wormedit::parse_native_project(invalid_enum.dump());
    CHECK_FALSE(rejected.song.has_value());
    CHECK(has_code(rejected.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    auto oversized_count = serialized;
    oversized_count["document"]["charts"][0]["sv_recipes"][0]["sampling"]["fixed_point_count"] =
        std::uint64_t{1} << 40U;
    const auto rejected_count = wormedit::parse_native_project(oversized_count.dump());
    CHECK_FALSE(rejected_count.song.has_value());
    CHECK(has_code(rejected_count.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));
}

TEST_CASE("native project reads legacy storyboards without a structured model",
          "[unit][format][native-project][storyboard][compatibility]") {
    const auto song = rich_bmson_song();
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());

    auto legacy_json = nlohmann::json::parse(*written.bytes);
    legacy_json["document"]["storyboards"][0].erase("model");
    const auto parsed = wormedit::parse_native_project(legacy_json.dump());
    REQUIRE(parsed.song.has_value());
    CHECK_FALSE(parsed.song->storyboards.front().model.has_value());
    CHECK(parsed.song->storyboards.front().retained_source_lines ==
          song.storyboards.front().retained_source_lines);

    const auto migrated = wormedit::write_native_project(*parsed.song);
    REQUIRE(migrated.bytes.has_value());
    const auto reparsed = wormedit::parse_native_project(*migrated.bytes);
    REQUIRE(reparsed.song.has_value());
    CHECK_FALSE(reparsed.song->storyboards.front().model.has_value());
    CHECK(reparsed.song->storyboards.front().retained_source_lines ==
          song.storyboards.front().retained_source_lines);
    const auto rewritten = wormedit::write_native_project(*reparsed.song);
    REQUIRE(rewritten.bytes.has_value());
    CHECK(*rewritten.bytes == *migrated.bytes);

    legacy_json["document"]["storyboards"][0]["future_field"] = true;
    const auto unknown = wormedit::parse_native_project(legacy_json.dump());
    CHECK_FALSE(unknown.song.has_value());
    CHECK(
        has_code(unknown.diagnostics, wormedit::native_project_diagnostic_code_t::malformed_json));

    legacy_json["document"]["storyboards"][0].erase("future_field");
    legacy_json["document"]["storyboards"][0].erase("name");
    const auto missing_required_field = wormedit::parse_native_project(legacy_json.dump());
    CHECK_FALSE(missing_required_field.song.has_value());
    CHECK(has_code(missing_required_field.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));
}

TEST_CASE("native storyboard models validate enums, paths, and loop depth",
          "[unit][format][native-project][storyboard][negative]") {
    const auto valid_song = rich_bmson_song();
    REQUIRE(valid_song.storyboards.front().model.has_value());

    auto invalid_opcode_song = valid_song;
    invalid_opcode_song.storyboards.front()
        .model->sprites.front()
        .nodes.front()
        .children.front()
        .children.front()
        .command.opcode = static_cast<wormedit::storyboard_opcode_t>(255);
    CHECK_FALSE(wormedit::validate_document(invalid_opcode_song).empty());
    const auto invalid_opcode_write = wormedit::write_native_project(invalid_opcode_song);
    CHECK_FALSE(invalid_opcode_write.bytes.has_value());
    CHECK(has_code(invalid_opcode_write.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    auto long_path_song = valid_song;
    long_path_song.storyboards.front().model->sprites.front().path.assign(4 * 1024 + 1, 'p');
    CHECK_FALSE(wormedit::validate_document(long_path_song).empty());
    const auto long_path_write = wormedit::write_native_project(long_path_song);
    CHECK_FALSE(long_path_write.bytes.has_value());
    CHECK(has_code(long_path_write.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    auto deep_song = valid_song;
    auto* destination = &deep_song.storyboards.front().model->sprites.front().nodes;
    for (std::size_t depth = 0; depth < 128; ++depth) {
        wormedit::storyboard_node_t loop;
        loop.kind = wormedit::storyboard_node_kind_t::loop;
        loop.loop_iteration_count = 1;
        destination->push_back(std::move(loop));
        destination = &destination->back().children;
    }
    wormedit::storyboard_node_t retained;
    retained.kind = wormedit::storyboard_node_kind_t::retained_line;
    retained.retained_line = "  _P,0,1,2,H";
    destination->push_back(std::move(retained));
    CHECK_FALSE(wormedit::validate_document(deep_song).empty());
    const auto deep_write = wormedit::write_native_project(deep_song);
    CHECK_FALSE(deep_write.bytes.has_value());
    CHECK(has_code(deep_write.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));
}

TEST_CASE("native project retains BMS source bytes and complete control trees",
          "[unit][format][native-project][bms]") {
    const auto source = read_binary(fixture_path("bms", "control-flow-fixed-values.bms"));
    wormedit::bms_parse_options_t options;
    options.lane_profile_id = "bme-7k-outside-scratch";
    const auto imported = wormedit::parse_bms_bytes(source, options);
    REQUIRE(imported.song.has_value());
    REQUIRE(imported.song->charts.front().bms_control_tree.has_value());

    const auto written = wormedit::write_native_project(*imported.song);
    REQUIRE(written.bytes.has_value());
    const auto reparsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(reparsed.song.has_value());
    const auto& chart = reparsed.song->charts.front();
    REQUIRE(chart.bms_control_tree.has_value());
    CHECK(chart.bms_control_tree->children.size() ==
          imported.song->charts.front().bms_control_tree->children.size());
    REQUIRE_FALSE(chart.metadata.format_groups.empty());
    REQUIRE(chart.metadata.format_groups.front().encoding.original_bytes != nullptr);
    CHECK(*chart.metadata.format_groups.front().encoding.original_bytes == source);

    const auto rewritten = wormedit::write_native_project(*reparsed.song);
    REQUIRE(rewritten.bytes.has_value());
    CHECK(*rewritten.bytes == *written.bytes);
}

TEST_CASE("native project rejects malformed, unsupported, over-limit, and invalid payloads",
          "[unit][format][native-project][negative]") {
    const auto malformed = wormedit::parse_native_project("{");
    CHECK_FALSE(malformed.song.has_value());
    CHECK(has_code(malformed.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));

    const auto song = rich_bmson_song();
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());

    auto unsupported_json = nlohmann::json::parse(*written.bytes);
    unsupported_json["format_version"] = 2;
    const auto unsupported = wormedit::parse_native_project(unsupported_json.dump());
    CHECK_FALSE(unsupported.song.has_value());
    CHECK(has_code(unsupported.diagnostics,
                   wormedit::native_project_diagnostic_code_t::unsupported_version));

    auto unsupported_document_json = nlohmann::json::parse(*written.bytes);
    unsupported_document_json["document"]["schema_version"] = 5;
    const auto unsupported_document =
        wormedit::parse_native_project(unsupported_document_json.dump());
    CHECK_FALSE(unsupported_document.song.has_value());
    CHECK(has_code(unsupported_document.diagnostics,
                   wormedit::native_project_diagnostic_code_t::unsupported_version));

    auto invalid_json = nlohmann::json::parse(*written.bytes);
    invalid_json["document"]["charts"][0]["source_family"] = 999;
    const auto invalid = wormedit::parse_native_project(invalid_json.dump());
    CHECK_FALSE(invalid.song.has_value());
    CHECK(has_code(invalid.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    auto invalid_storyboard_kind_json = nlohmann::json::parse(*written.bytes);
    invalid_storyboard_kind_json["document"]["storyboards"][0]["model"]["sprites"][0]["nodes"][0]
                                ["kind"] = 255;
    const auto invalid_storyboard_kind =
        wormedit::parse_native_project(invalid_storyboard_kind_json.dump());
    CHECK_FALSE(invalid_storyboard_kind.song.has_value());
    CHECK(has_code(invalid_storyboard_kind.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    auto invalid_storyboard_opcode_json = nlohmann::json::parse(*written.bytes);
    invalid_storyboard_opcode_json["document"]["storyboards"][0]["model"]["sprites"][0]["nodes"][0]
                                  ["children"][0]["children"][0]["command"]["opcode"] = 255;
    const auto invalid_storyboard_opcode =
        wormedit::parse_native_project(invalid_storyboard_opcode_json.dump());
    CHECK_FALSE(invalid_storyboard_opcode.song.has_value());
    CHECK(has_code(invalid_storyboard_opcode.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    auto integer_storyboard_position_json = nlohmann::json::parse(*written.bytes);
    integer_storyboard_position_json["document"]["storyboards"][0]["model"]["sprites"][0]["x"] =
        320;
    const auto integer_storyboard_position =
        wormedit::parse_native_project(integer_storyboard_position_json.dump());
    CHECK_FALSE(integer_storyboard_position.song.has_value());
    CHECK(has_code(integer_storyboard_position.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));

    auto integer_storyboard_value_json = nlohmann::json::parse(*written.bytes);
    integer_storyboard_value_json["document"]["storyboards"][0]["model"]["sprites"][0]["nodes"][0]
                                 ["children"][0]["children"][0]["command"]["from"][0] = 0;
    const auto integer_storyboard_value =
        wormedit::parse_native_project(integer_storyboard_value_json.dump());
    CHECK_FALSE(integer_storyboard_value.song.has_value());
    CHECK(has_code(integer_storyboard_value.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));

    auto oversized_storyboard_time_json = nlohmann::json::parse(*written.bytes);
    oversized_storyboard_time_json["document"]["storyboards"][0]["model"]["sprites"][0]["nodes"][0]
                                  ["children"][0]["children"][0]["command"]["start_time_ms"] =
                                      18'446'744'073'709'551'615ULL;
    const auto oversized_storyboard_time =
        wormedit::parse_native_project(oversized_storyboard_time_json.dump());
    CHECK_FALSE(oversized_storyboard_time.song.has_value());
    CHECK(has_code(oversized_storyboard_time.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));

    auto oversized_storyboard_path_json = nlohmann::json::parse(*written.bytes);
    oversized_storyboard_path_json["document"]["storyboards"][0]["model"]["sprites"][0]["path"] =
        std::string(4 * 1024 + 1, 'p');
    const auto oversized_storyboard_path =
        wormedit::parse_native_project(oversized_storyboard_path_json.dump());
    CHECK_FALSE(oversized_storyboard_path.song.has_value());
    CHECK(has_code(oversized_storyboard_path.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    auto fractional_integer_json = nlohmann::json::parse(*written.bytes);
    fractional_integer_json["document"]["schema_version"] = 1.5;
    const auto fractional_integer = wormedit::parse_native_project(fractional_integer_json.dump());
    CHECK_FALSE(fractional_integer.song.has_value());
    CHECK(has_code(fractional_integer.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));

    auto negative_id_json = nlohmann::json::parse(*written.bytes);
    negative_id_json["document"]["id"] = -1;
    const auto negative_id = wormedit::parse_native_project(negative_id_json.dump());
    CHECK_FALSE(negative_id.song.has_value());
    CHECK(has_code(negative_id.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));

    auto unknown_field_json = nlohmann::json::parse(*written.bytes);
    unknown_field_json["document"]["future_field"] = true;
    const auto unknown_field = wormedit::parse_native_project(unknown_field_json.dump());
    CHECK_FALSE(unknown_field.song.has_value());
    CHECK(has_code(unknown_field.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));

    auto unknown_envelope_field_json = nlohmann::json::parse(*written.bytes);
    unknown_envelope_field_json["future_envelope_field"] = true;
    const auto unknown_envelope_field =
        wormedit::parse_native_project(unknown_envelope_field_json.dump());
    CHECK_FALSE(unknown_envelope_field.song.has_value());
    CHECK(has_code(unknown_envelope_field.diagnostics,
                   wormedit::native_project_diagnostic_code_t::malformed_json));

    wormedit::native_project_limits_t byte_limit;
    byte_limit.maximum_source_bytes = written.bytes->size() - 1;
    const auto over_limit = wormedit::parse_native_project(*written.bytes, byte_limit);
    CHECK_FALSE(over_limit.song.has_value());
    CHECK(has_code(over_limit.diagnostics,
                   wormedit::native_project_diagnostic_code_t::resource_limit));

    wormedit::native_project_limits_t node_limit;
    node_limit.maximum_json_nodes = 4;
    const auto too_many_nodes = wormedit::parse_native_project(*written.bytes, node_limit);
    CHECK_FALSE(too_many_nodes.song.has_value());
    CHECK(has_code(too_many_nodes.diagnostics,
                   wormedit::native_project_diagnostic_code_t::resource_limit));

    wormedit::native_project_limits_t depth_limit;
    depth_limit.maximum_nesting_depth = 1;
    const auto deep_write = wormedit::write_native_project(song, depth_limit);
    CHECK_FALSE(deep_write.bytes.has_value());
    CHECK(has_code(deep_write.diagnostics,
                   wormedit::native_project_diagnostic_code_t::resource_limit));
}

namespace {

namespace internal = wormedit::native_project_internal;

// Fake legacy shape: a valid current document stamped schema_version 1 with an extra
// `legacy_title` field that the injected step must fold away.
auto fake_legacy_fold(nlohmann::json& document) -> std::string {
    if (!document.contains("legacy_title")) {
        return "missing legacy_title";
    }
    document.erase("legacy_title");
    return {};
}

auto fake_v1_to_v2(nlohmann::json& document) -> std::string {
    document["chain"] = document.value("chain", std::string{}) + "1";
    return {};
}

auto fake_v0_to_v1_chain(nlohmann::json& document) -> std::string {
    document["chain"] = document.value("chain", std::string{}) + "0";
    return {};
}

auto failing_migration(nlohmann::json&) -> std::string {
    return "cannot upgrade";
}

auto legacy_project_bytes() -> std::string {
    const auto written = wormedit::write_native_project(rich_bmson_song());
    auto root = nlohmann::json::parse(*written.bytes);
    root["document"]["schema_version"] = 1;
    root["document"]["legacy_title"] = "old";
    return root.dump();
}

} // namespace

TEST_CASE("native project migrations: the current schema passes through the default table",
          "[unit][format][native-project][migration]") {
    CHECK(internal::default_migrations().front().from_version == 1);
    CHECK(internal::default_migrations().back().from_version == 3);
    const auto written = wormedit::write_native_project(rich_bmson_song());
    REQUIRE(written.bytes.has_value());
    auto document = nlohmann::json::parse(*written.bytes).at("document");
    REQUIRE(document.at("schema_version") == 4);
    const auto before = document;
    const auto outcome = internal::migrate_document(document, 4, internal::default_migrations());
    CHECK(outcome.status == internal::migration_status_t::unchanged);
    CHECK(document == before);
}

TEST_CASE("native project migrations: schema 1 documents gain current default fields",
          "[unit][format][native-project][migration][sv-recipe]") {
    const auto written = wormedit::write_native_project(rich_bmson_song());
    REQUIRE(written.bytes.has_value());
    auto root = nlohmann::json::parse(*written.bytes);
    auto& recipe = root["document"]["charts"][0]["sv_recipes"][0];
    recipe.erase("expression_source");
    root["document"]["schema_version"] = 1;

    const auto parsed = wormedit::parse_native_project(root.dump());
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->schema_version == 4);
    CHECK(parsed.song->charts.front().sv_recipes.front().expression_source.empty());
    const auto rewritten = wormedit::write_native_project(*parsed.song);
    REQUIRE(rewritten.bytes.has_value());

    // A schema 1 file with malformed recipe structure still rejects.
    auto malformed = root;
    malformed["document"]["charts"][0]["sv_recipes"] = 5;
    CHECK_FALSE(wormedit::parse_native_project(malformed.dump()).song.has_value());
}

TEST_CASE("native project schema 1 migration supplies empty quantization presets",
          "[unit][format][native-project][migration][quantize-preset]") {
    const auto written = wormedit::write_native_project(rich_bmson_song());
    REQUIRE(written.bytes.has_value());
    auto root = nlohmann::json::parse(*written.bytes);
    root["document"].erase("quantization_presets");
    root["document"]["schema_version"] = 1;
    const auto parsed = wormedit::parse_native_project(root.dump());
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->quantization_presets.empty());
}

TEST_CASE("native project schema 2 migration adds an empty preset list",
          "[unit][format][native-project][migration][quantize-preset]") {
    const auto written = wormedit::write_native_project(rich_bmson_song());
    REQUIRE(written.bytes.has_value());
    auto root = nlohmann::json::parse(*written.bytes);
    root["document"].erase("quantization_presets");
    root["document"]["schema_version"] = 2;

    const auto parsed = wormedit::parse_native_project(root.dump());
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->schema_version == 4);
    CHECK(parsed.song->quantization_presets.empty());
}

TEST_CASE("native project upgrades schema 2 in-memory documents without losing presets",
          "[unit][format][native-project][quantize-preset]") {
    auto song = rich_bmson_song();
    song.schema_version = 2;
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());
    const auto root = nlohmann::json::parse(*written.bytes);
    CHECK(root["document"]["schema_version"] == 4);
    const auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->quantization_presets == song.quantization_presets);
}

TEST_CASE("native schema 3 migration recovers Layer 2 only from BMS 0A provenance",
          "[unit][format][native-project][migration][bga]") {
    const auto legacy = legacy_schema3_bms_document();
    require_bmson_condition(!legacy.empty());
    check_bmson_condition(schema3_migrates_layer2_by_provenance(legacy));
    check_bmson_condition(schema3_without_provenance_keeps_legacy_layer(legacy));
    check_bmson_condition(writing_schema3_keeps_layer2_provenance(legacy));
    const auto precision = migration_does_not_narrow_enum_values(legacy);
    check_bmson_condition(precision.migrated);
    check_bmson_condition(precision.kind_preserved);
    check_bmson_condition(precision.family_preserved);
}

TEST_CASE("native project rejects preset names with embedded null bytes",
          "[unit][format][native-project][quantize-preset][negative]") {
    auto song = rich_bmson_song();
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());
    const std::string name("Grid\0hidden", 11);
    song.quantization_presets.front().name = name;
    CHECK_FALSE(wormedit::write_native_project(song).bytes.has_value());
    auto root = nlohmann::json::parse(*written.bytes);
    root["document"]["quantization_presets"][0]["name"] = name;
    CHECK_FALSE(wormedit::parse_native_project(root.dump()).song.has_value());
}

TEST_CASE("native project rejects duplicate or invalid quantization presets",
          "[unit][format][native-project][quantize-preset][negative]") {
    auto song = rich_bmson_song();
    song.quantization_presets.push_back(song.quantization_presets.front());
    const auto duplicate_write = wormedit::write_native_project(song);
    CHECK_FALSE(duplicate_write.bytes.has_value());

    const auto valid_write = wormedit::write_native_project(rich_bmson_song());
    REQUIRE(valid_write.bytes.has_value());
    auto duplicate_json = nlohmann::json::parse(*valid_write.bytes);
    auto& presets = duplicate_json["document"]["quantization_presets"];
    presets.push_back(presets.front());
    CHECK_FALSE(wormedit::parse_native_project(duplicate_json.dump()).song.has_value());

    auto valid = rich_bmson_song();
    valid.quantization_presets.front().divisions = 0;
    const auto invalid_write = wormedit::write_native_project(valid);
    CHECK_FALSE(invalid_write.bytes.has_value());
}

TEST_CASE("native project migrations: a legacy document migrates through the injected table",
          "[unit][format][native-project][migration]") {
    const std::array<internal::migration_t, 1> table{{{1, &fake_legacy_fold}}};
    const auto bytes = legacy_project_bytes();

    // The released table does not know the extra field, so the legacy document is rejected.
    const auto rejected = wormedit::parse_native_project(bytes);
    CHECK_FALSE(rejected.song.has_value());

    const auto migrated =
        internal::parse_native_project_with_migrations(bytes, {}, 2, table);
    REQUIRE(migrated.song.has_value());
    CHECK(migrated.song->schema_version == 2);
    const auto rewritten = wormedit::write_native_project(*migrated.song);
    REQUIRE(rewritten.bytes.has_value());
    CHECK(rewritten.bytes == wormedit::write_native_project(rich_bmson_song()).bytes);
}

TEST_CASE("native project migrations: steps run in order and stop at the target",
          "[unit][format][native-project][migration]") {
    const std::array<internal::migration_t, 2> table{
        {{1, &fake_v1_to_v2}, {0, &fake_v0_to_v1_chain}}};
    nlohmann::json document{{"schema_version", 0U}};
    const auto outcome = internal::migrate_document(document, 2, table);
    CHECK(outcome.status == internal::migration_status_t::migrated);
    CHECK(document.at("chain") == "01");
    CHECK(document.at("schema_version") == 2);

    nlohmann::json partial{{"schema_version", 0U}};
    CHECK(internal::migrate_document(partial, 1, table).status ==
          internal::migration_status_t::migrated);
    CHECK(partial.at("chain") == "0");
    CHECK(partial.at("schema_version") == 1);
}

TEST_CASE("native project migrations: future, malformed, and gapped versions reject",
          "[unit][format][native-project][migration][negative]") {
    const std::array<internal::migration_t, 1> table{{{1, &fake_v1_to_v2}}};
    nlohmann::json future{{"schema_version", 3U}};
    CHECK(internal::migrate_document(future, 2, table).status ==
          internal::migration_status_t::unsupported_version);
    CHECK(future.at("schema_version") == 3);

    nlohmann::json negative{{"schema_version", -1}};
    CHECK(internal::migrate_document(negative, 2, table).status ==
          internal::migration_status_t::malformed_version);
    nlohmann::json missing = nlohmann::json::object();
    CHECK(internal::migrate_document(missing, 2, table).status ==
          internal::migration_status_t::malformed_version);

    nlohmann::json gap{{"schema_version", 0U}};
    CHECK(internal::migrate_document(gap, 2, table).status ==
          internal::migration_status_t::missing_step);
}

TEST_CASE("native project migrations: a failing step publishes no document",
          "[unit][format][native-project][migration][negative]") {
    const std::array<internal::migration_t, 1> table{{{1, &failing_migration}}};
    const auto result =
        internal::parse_native_project_with_migrations(legacy_project_bytes(), {}, 2, table);
    CHECK_FALSE(result.song.has_value());
    CHECK(has_code(result.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    nlohmann::json document{{"schema_version", 1U}, {"keep", true}};
    const auto before = document;
    CHECK(internal::migrate_document(document, 2, table).status ==
          internal::migration_status_t::failed);
    CHECK(document == before);
}

TEST_CASE("native projects store lane key colors, derive them for older files, and reject unknown values",
          "[unit][format][native-project][key-color]") {
    auto song = rich_bmson_song();
    auto& layout = song.charts.front().lane_layouts.front().layout;
    layout.profile_id = "bme-7k-outside-scratch";
    layout.lanes.front().key_color = wormedit::lane_key_color_t::black;
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());
    const auto serialized = nlohmann::json::parse(*written.bytes);
    const auto& lanes_json =
        serialized["document"]["charts"][0]["lane_layouts"][0]["layout"]["lanes"];
    CHECK(lanes_json[0]["key_color"] == static_cast<int>(wormedit::lane_key_color_t::black));

    const auto round_trip = wormedit::parse_native_project(*written.bytes);
    REQUIRE(round_trip.song.has_value());
    CHECK(round_trip.song->charts.front().lane_layouts.front().layout.lanes.front().key_color ==
          wormedit::lane_key_color_t::black);

    auto legacy = serialized;
    for (auto& lane : legacy["document"]["charts"][0]["lane_layouts"][0]["layout"]["lanes"]) {
        lane.erase("key_color");
    }
    const auto parsed_legacy = wormedit::parse_native_project(legacy.dump());
    REQUIRE(parsed_legacy.song.has_value());
    const auto& derived = parsed_legacy.song->charts.front().lane_layouts.front().layout;
    CHECK(std::ranges::all_of(derived.lanes, [](const auto& lane) {
        return lane.key_color != wormedit::lane_key_color_t::pms_red;
    }));

    auto invalid = serialized;
    invalid["document"]["charts"][0]["lane_layouts"][0]["layout"]["lanes"][0]["key_color"] = 99;
    const auto rejected = wormedit::parse_native_project(invalid.dump());
    CHECK_FALSE(rejected.song.has_value());
}

TEST_CASE(
    "native projects repair old generic BMSON colors and scratch order but retain custom colors",
    "[unit][format][native-project][key-color]") {
    nlohmann::json legacy;
    for (const auto mode : {"beat-7k", "beat-14k"}) {
        auto source = std::string{R"({"version":"1.0.0","info":{"init_bpm":120,"mode_hint":")"};
        source += mode;
        source += R"("},"sound_channels":[]})";
        const auto imported =
            wormedit::parse_bmson_bytes(std::as_bytes(std::span{source.data(), source.size()}));
        require_bmson_condition(imported.song);
        const auto saved = wormedit::write_native_project(*imported.song);
        require_bmson_condition(saved.bytes);
        auto old_project = nlohmann::json::parse(*saved.bytes);
        auto& lanes_json =
            old_project["document"]["charts"][0]["lane_layouts"][0]["layout"]["lanes"];
        for (auto& lane : lanes_json) {
            const auto x = lane["id"].get<std::uint64_t>() - 100000;
            lane["display_order"] = x - 1;
            lane["key_color"] = static_cast<int>(wormedit::lane_key_color_t::generic);
        }
        std::vector<nlohmann::json> sorted_lanes(lanes_json.begin(), lanes_json.end());
        std::ranges::sort(sorted_lanes, {}, [](const auto& lane) {
            return lane["id"].template get<std::uint64_t>();
        });
        lanes_json = sorted_lanes;
        const auto repaired = wormedit::parse_native_project(old_project.dump());
        require_bmson_condition(repaired.song);
        const auto& layout = repaired.song->charts.front().lane_layouts.front().layout;
        constexpr std::uint32_t key_count = 7;
        require_bmson_condition(layout.lanes.size() == (mode == std::string_view{"beat-14k"} ? 16U : 8U));
        const auto scratch = std::ranges::find(layout.lanes, wormedit::lane_id_t{100008},
                                               &wormedit::lane_definition_t::id);
        require_bmson_condition(scratch != layout.lanes.end());
        check_bmson_condition(scratch->display_order == 0);
        check_bmson_condition(scratch->key_color == wormedit::lane_key_color_t::scratch);
        for (std::uint32_t index = 0; index < key_count; ++index) {
            const auto lane = std::ranges::find(layout.lanes, wormedit::lane_id_t{100001 + index},
                                                &wormedit::lane_definition_t::id);
            require_bmson_condition(lane != layout.lanes.end());
            check_bmson_condition(lane->display_order == index + 1);
            check_bmson_condition(lane->key_color == (index % 2 == 0 ? wormedit::lane_key_color_t::white
                                                     : wormedit::lane_key_color_t::black));
        }
        if (mode == std::string_view{"beat-14k"}) {
            const auto right_key = std::ranges::find(layout.lanes, wormedit::lane_id_t{100009},
                                                     &wormedit::lane_definition_t::id);
            const auto right_scratch = std::ranges::find(layout.lanes, wormedit::lane_id_t{100016},
                                                         &wormedit::lane_definition_t::id);
            require_bmson_condition(right_key != layout.lanes.end());
            require_bmson_condition(right_scratch != layout.lanes.end());
            check_bmson_condition(right_key->display_order == 8);
            check_bmson_condition(right_scratch->display_order == 15);
            check_bmson_condition(right_key->key_color == wormedit::lane_key_color_t::white);
        } else {
            legacy = std::move(old_project);
        }
    }

    auto authored = legacy;
    for (auto& lane : authored["document"]["charts"][0]["lane_layouts"][0]["layout"]["lanes"]) {
        if (lane["id"] == 100001) {
            lane["key_color"] = static_cast<int>(wormedit::lane_key_color_t::pms_red);
            break;
        }
    }
    const auto retained = wormedit::parse_native_project(authored.dump());
    require_bmson_condition(retained.song);
    const auto& retained_layout = retained.song->charts.front().lane_layouts.front().layout;
    const auto retained_scratch = std::ranges::find(
        retained_layout.lanes, wormedit::lane_id_t{100008}, &wormedit::lane_definition_t::id);
    const auto authored_color = std::ranges::find(
        retained_layout.lanes, wormedit::lane_id_t{100001}, &wormedit::lane_definition_t::id);
    const auto generic_color = std::ranges::find(retained_layout.lanes, wormedit::lane_id_t{100002},
                                                 &wormedit::lane_definition_t::id);
    require_bmson_condition(retained_scratch != retained_layout.lanes.end());
    require_bmson_condition(authored_color != retained_layout.lanes.end());
    require_bmson_condition(generic_color != retained_layout.lanes.end());
    check_bmson_condition(retained_scratch->display_order == 7);
    check_bmson_condition(authored_color->key_color == wormedit::lane_key_color_t::pms_red);
    check_bmson_condition(generic_color->key_color == wormedit::lane_key_color_t::generic);
}

namespace {

// A project as saved before BGM columns: one lane per `#mmm01` line, plus a
// sound-less event for a `00` slot.
auto legacy_bgm_song() -> wormedit::song_document_t {
    constexpr std::string_view source =
        "#PLAYER 1\n#TITLE Legacy BGM\n#BPM 120\n#WAV01 a.wav\n#WAV02 b.wav\n"
        "#00001:0100\n#00001:0002\n#00101:01\n";
    const auto bytes = std::vector<std::byte>(
        reinterpret_cast<const std::byte*>(source.data()),
        reinterpret_cast<const std::byte*>(source.data()) + source.size());
    auto parsed = wormedit::parse_bms_bytes(bytes, {.lane_profile_id = "bme-7k-outside-scratch"});
    REQUIRE(parsed.song.has_value());
    auto song = std::move(*parsed.song);
    auto& chart = song.charts.front();
    REQUIRE(chart.bgm_source_lanes.size() == 2);
    chart.bgm_source_lanes[0].name = "BGM source #00001 (line 6)";
    chart.bgm_source_lanes[1].name = "BGM source #00001 (line 7)";
    const auto third = wormedit::object_id_t{9001};
    chart.bgm_source_lanes.push_back(
        {.id = third, .name = "BGM source #00101 (line 8)", .source_order = 7});
    chart.measures[1].bgm_events.front().source_lane = third;
    auto zero = chart.measures[0].bgm_events.front();
    zero.id = wormedit::object_id_t{9002};
    zero.sound.reset();
    zero.position.row = chart.divisions_per_standard_measure / 2;
    zero.structural_slot = 1;
    zero.provenance.source_field = "01 slot 1 object 00";
    chart.measures[0].bgm_events.push_back(zero);
    return song;
}

} // namespace

TEST_CASE("native projects saved with per-line BGM lanes and 00 events load as columns",
          "[unit][format][native-project][bgm-columns]") {
    const auto written = wormedit::write_native_project(legacy_bgm_song());
    REQUIRE(written.bytes.has_value());
    const auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song.has_value());
    const auto& chart = parsed.song->charts.front();

    REQUIRE(chart.bgm_source_lanes.size() == 2);
    CHECK(chart.bgm_source_lanes[0].name == "BGM 1");
    CHECK(chart.bgm_source_lanes[1].name == "BGM 2");
    CHECK(chart.bgm_source_lanes[0].source_order < chart.bgm_source_lanes[1].source_order);
    REQUIRE(chart.measures[0].bgm_events.size() == 2);
    CHECK(std::ranges::none_of(chart.measures[0].bgm_events,
                               [](const auto& event) { return !event.sound.has_value(); }));
    REQUIRE(chart.measures[1].bgm_events.size() == 1);
    // The measure's only line starts again at column 1.
    CHECK(chart.measures[1].bgm_events.front().source_lane == chart.bgm_source_lanes[0].id);
    CHECK(chart.measures[0].bgm_events[1].source_lane == chart.bgm_source_lanes[1].id);

    auto again = *parsed.song;
    CHECK_FALSE(wormedit::native_project_internal::normalize_legacy_bms_bgm(again));
}

TEST_CASE("current BGM columns and non-BMS lanes pass through the load repair untouched",
          "[unit][format][native-project][bgm-columns]") {
    auto song = rich_bmson_song();
    const auto before = song.charts.front().bgm_source_lanes;
    CHECK_FALSE(wormedit::native_project_internal::normalize_legacy_bms_bgm(song));
    CHECK(song.charts.front().bgm_source_lanes.size() == before.size());
}

TEST_CASE("native projects round-trip Lua expression recipes",
          "[unit][format][native-project][sv-recipe]") {
    auto song = rich_bmson_song();
    auto& recipe = song.charts.front().sv_recipes.front();
    recipe.curve = wormedit::sv_recipe_curve_t::lua_expression;
    recipe.expression_source = "return 1 + 0.5 * math.sin(t * math.pi)";
    REQUIRE(wormedit::validate_document(song).empty());
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());
    auto serialized = nlohmann::json::parse(*written.bytes);
    CHECK(serialized["document"]["charts"][0]["sv_recipes"][0]["expression_source"] ==
          recipe.expression_source);

    const auto reparsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(reparsed.song.has_value());
    const auto& loaded = reparsed.song->charts.front().sv_recipes.front();
    CHECK(loaded.curve == wormedit::sv_recipe_curve_t::lua_expression);
    CHECK(loaded.expression_source == recipe.expression_source);
}

TEST_CASE("native projects persist sound kind and tags only when set",
          "[unit][format][native-project][sounds]") {
    auto song = rich_bmson_song();
    REQUIRE_FALSE(song.resources.empty());
    const auto plain = wormedit::write_native_project(song);
    REQUIRE(plain.bytes.has_value());
    CHECK(plain.bytes->find("\"sound_kind\"") == std::string::npos);
    CHECK(plain.bytes->find("\"tags\"") == std::string::npos);
    const auto legacy = wormedit::parse_native_project(*plain.bytes);
    REQUIRE(legacy.song.has_value());
    CHECK(legacy.song->resources.front().sound_kind == wormedit::sound_kind_t::unspecified);
    CHECK(legacy.song->resources.front().tags.empty());

    song.resources.front().sound_kind = wormedit::sound_kind_t::stem;
    song.resources.front().tags = {"drums", "Loop"};
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());
    const auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->resources.front().sound_kind == wormedit::sound_kind_t::stem);
    CHECK(parsed.song->resources.front().tags == std::vector<std::string>{"drums", "Loop"});
    CHECK(*wormedit::write_native_project(*parsed.song).bytes == *written.bytes);
}

TEST_CASE("native projects persist chart sound layer defaults only when set",
          "[unit][format][native-project][sounds]") {
    auto song = rich_bmson_song();
    REQUIRE_FALSE(song.resources.empty());
    auto& chart = song.charts.front();
    chart.semantic_layers.push_back({.id = wormedit::layer_id_t{700}, .name = "Vocal"});
    chart.semantic_layers.push_back({.id = wormedit::layer_id_t{800}, .name = "Drums"});
    const auto plain = wormedit::write_native_project(song);
    REQUIRE(plain.bytes.has_value());
    CHECK(plain.bytes->find("\"sound_layer_defaults\"") == std::string::npos);
    const auto legacy = wormedit::parse_native_project(*plain.bytes);
    REQUIRE(legacy.song.has_value());
    CHECK(legacy.song->charts.front().sound_layer_defaults.empty());

    chart.sound_layer_defaults = {{.resource = song.resources.front().id,
                                   .layers = {wormedit::layer_id_t{800}, wormedit::layer_id_t{700}}}};
    const auto written = wormedit::write_native_project(song);
    REQUIRE(written.bytes.has_value());
    const auto parsed = wormedit::parse_native_project(*written.bytes);
    REQUIRE(parsed.song.has_value());
    CHECK(parsed.song->charts.front().sound_layer_defaults == chart.sound_layer_defaults);
    CHECK(*wormedit::write_native_project(*parsed.song).bytes == *written.bytes);
}

TEST_CASE("native BMSON color repair preserves authored lane order", "[unit][format][native-project][key-color]") {
    const std::string source = R"({"version":"1.0.0","info":{"init_bpm":120,
        "mode_hint":"beat-5k"},"sound_channels":[]})";
    const auto imported = wormedit::parse_bmson_bytes(std::as_bytes(std::span{source.data(), source.size()}));
    require_bmson_condition(imported.song);
    const auto saved = wormedit::write_native_project(*imported.song);
    require_bmson_condition(saved.bytes);
    auto project = nlohmann::json::parse(*saved.bytes);
    auto& lanes = project["document"]["charts"][0]["lane_layouts"][0]["layout"]["lanes"];
    for (auto& lane : lanes) lane["key_color"] = static_cast<int>(wormedit::lane_key_color_t::generic);
    lanes[0]["display_order"] = 77;
    const auto reopened = wormedit::parse_native_project(project.dump());
    require_bmson_condition(reopened.song);
    check_bmson_condition(reopened.song->charts.front().lane_layouts.front().layout.lanes[0].display_order == 77);
}
