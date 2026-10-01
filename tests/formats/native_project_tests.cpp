#include <wormedit/core/storyboard.h>
#include <wormedit/formats/bms.h>
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
            break;
        }
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
    return song;
}

} // namespace

TEST_CASE("native project deterministically round-trips rich editor state",
          "[unit][format][native-project]") {
    const auto song = rich_bmson_song();
    REQUIRE_FALSE(song.charts.empty());
    REQUIRE(wormedit::validate_document(song).empty());

    const auto written = wormedit::write_native_project(song);
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
    unsupported_document_json["document"]["schema_version"] = 2;
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

// Fake legacy v0 shape: a valid v1 document with schema_version 0 and an extra
// `legacy_title` field that the migration must fold away.
auto fake_v0_to_v1(nlohmann::json& document) -> std::string {
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
    root["document"]["schema_version"] = 0;
    root["document"]["legacy_title"] = "old";
    return root.dump();
}

} // namespace

TEST_CASE("native project migrations: version 1 passes through the default table",
          "[unit][format][native-project][migration]") {
    CHECK(internal::default_migrations().empty());
    const auto written = wormedit::write_native_project(rich_bmson_song());
    REQUIRE(written.bytes.has_value());
    auto document = nlohmann::json::parse(*written.bytes).at("document");
    const auto before = document;
    const auto outcome = internal::migrate_document(document, 1, internal::default_migrations());
    CHECK(outcome.status == internal::migration_status_t::unchanged);
    CHECK(document == before);
}

TEST_CASE("native project migrations: a v0 document migrates through the injected table",
          "[unit][format][native-project][migration]") {
    const std::array<internal::migration_t, 1> table{{{0, &fake_v0_to_v1}}};
    const auto bytes = legacy_project_bytes();

    // Without the migration the legacy document is rejected.
    const auto rejected = wormedit::parse_native_project(bytes);
    CHECK_FALSE(rejected.song.has_value());
    CHECK(has_code(rejected.diagnostics,
                   wormedit::native_project_diagnostic_code_t::unsupported_version));

    const auto migrated =
        internal::parse_native_project_with_migrations(bytes, {}, 1, table);
    REQUIRE(migrated.song.has_value());
    CHECK(migrated.song->schema_version == 1);
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
    const std::array<internal::migration_t, 1> table{{{0, &failing_migration}}};
    const auto result =
        internal::parse_native_project_with_migrations(legacy_project_bytes(), {}, 1, table);
    CHECK_FALSE(result.song.has_value());
    CHECK(has_code(result.diagnostics,
                   wormedit::native_project_diagnostic_code_t::invalid_document));

    nlohmann::json document{{"schema_version", 0U}, {"keep", true}};
    const auto before = document;
    CHECK(internal::migrate_document(document, 1, table).status ==
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
