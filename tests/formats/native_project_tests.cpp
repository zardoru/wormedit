#include <wormedit/formats/bms.h>
#include <wormedit/formats/bmson.h>
#include <wormedit/formats/native_project.h>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

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
    song.storyboards.push_back({
        .id = wormedit::storyboard_id_t{905},
        .name = "Main",
        .retained_source_lines = {"Sprite,Background,Centre,background.png"},
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
    REQUIRE(parsed.song->charts.front().sections.back().beat_span.has_value());
    REQUIRE(song.charts.front().sections.back().beat_span.has_value());
    CHECK(parsed.song->charts.front().sections.back().beat_span->begin ==
          song.charts.front().sections.back().beat_span->begin);
    CHECK(parsed.song->charts.front().sections.back().beat_span->end ==
          song.charts.front().sections.back().beat_span->end);

    const auto rewritten = wormedit::write_native_project(*parsed.song);
    REQUIRE(rewritten.bytes.has_value());
    CHECK(*rewritten.bytes == *written.bytes);
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
