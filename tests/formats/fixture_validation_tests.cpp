#include "fixture_encoding.h"
#include "sys/project/project_open.h"

#include <wormedit/core/interaction_dispatch.h>
#include <wormedit/core/lane_profiles.h>
#include <wormedit/formats/bms.h>
#include <wormedit/formats/stepmania.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {
using wormedit::test::explicit_fixture_encoding;

auto positive_chart_fixtures() -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(WORMEDIT_FIXTURE_DIR)) {
        const auto name = entry.path().filename().string();
        if (entry.is_regular_file() && wormedit::sys::is_supported_project_file_extension(entry.path()) &&
            !name.starts_with("negative-") && !name.starts_with("invalid-"))
            paths.push_back(entry.path());
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}



auto explicit_fixture_profile(const std::filesystem::path& path) -> std::optional<std::string> {
    static const std::map<std::string, std::string> choices{
        {"bms-5k-standard.bms", "bms-5k-outside-scratch"},
        {"bms-5k-free-zone.bms", "bms-5k-outside-scratch-free-zone"},
        {"bms-5k-foot-pedal.bms", "bms-5k-outside-scratch-foot-pedal"},
        {"bme-7k.bme", "bme-7k-outside-scratch"},
        {"bme-7k-foot-pedal.bme", "bme-7k-outside-scratch-foot-pedal"},
        {"bms-10k-standard.bms", "bms-10k-left-outside-right-outside"},
        {"bms-10k-free-zone.bms", "bms-10k-left-outside-right-outside-free-zone"},
        {"bms-10k-foot-pedal.bms", "bms-10k-left-outside-right-outside-foot-pedal"},
        {"bms-10k-scratchless.bms", "bms-10k-scratchless"},
        {"bme-14k.bme", "bme-14k-left-outside-right-outside"},
        {"bme-14k-foot-pedal.bme", "bme-14k-left-outside-right-outside-foot-pedal"},
        {"pms-5button.pms", "pms-standard-5button"},
        {"pms-9button.pms", "pms-standard-9button"},
        {"pms-bme-9button.pms", "pms-bme-style-9button"},
        {"pms-bme-18button.pms", "pms-bme-style-18button"},
        {"double-scratch-fpp.bms", "bms-double-scratch-fpp"},
        {"octave-foot-pedal.bms", "bms-octave-foot-pedal"},
        {"project2dx-7k.bms", "project2dx-7k"}};
    if (path.filename() == "x-aria-10k-hd.bme") return std::nullopt;
    const auto found = choices.find(path.filename().string());
    if (found != choices.end()) return found->second;
    if (path.extension() == ".pms") return "pms-standard-9button";
    if (path.extension() == ".bme") return "bme-7k-outside-scratch";
    return "bms-5k-outside-scratch-free-zone";
}

auto read_fixture(const std::filesystem::path& path) -> std::string {
    std::ifstream input(path, std::ios::binary);
    REQUIRE(input.good());
    return {std::istreambuf_iterator<char>{input}, {}};
}

template <typename Diagnostics>
auto checked_fixture_document(std::optional<wormedit::song_document_t>&& document,
                              const Diagnostics& diagnostics) -> wormedit::song_document_t {
    for (const auto& diagnostic : diagnostics) INFO(diagnostic.message);
    REQUIRE_FALSE(wormedit::diagnostic_queries::has_errors(diagnostics));
    REQUIRE(document);
    return std::move(*document);
}

auto parse_positive_fixture(const std::filesystem::path& path) -> wormedit::song_document_t {
    const auto extension = path.extension().string();
    const auto source = read_fixture(path);
    const auto bytes = std::as_bytes(std::span{source.data(), source.size()});
    if (extension == ".bms" || extension == ".bme" || extension == ".bml" || extension == ".pms") {
        wormedit::bms_parse_options_t options;
        options.file_extension = extension;
        options.encoding_override = explicit_fixture_encoding(path);
        options.lane_profile_id = explicit_fixture_profile(path);
        auto parsed = wormedit::parse_bms_bytes(bytes, options);
        return checked_fixture_document(std::move(parsed.value.song), parsed.diagnostics);
    }
    if (path.filename().string().starts_with("legacy-negative-")) {
        wormedit::stepmania_parse_options_t options;
        options.source_timing_syntax = path.filename() == "legacy-negative-stop.sm"
            ? wormedit::stepmania_timing_syntax_t::legacy_negative_stop
            : wormedit::stepmania_timing_syntax_t::legacy_negative_bpm;
        auto parsed = wormedit::parse_stepmania_bytes(bytes, options);
        return checked_fixture_document(std::move(parsed.value), parsed.diagnostics);
    }
    auto opened = wormedit::sys::open_project_file(path);
    INFO(opened.error);
    REQUIRE(opened.document);
    return std::move(*opened.document);
}

auto fixture_empty_tap_slot(const wormedit::chart_document_t& chart)
    -> std::pair<wormedit::lane_id_t, wormedit::local_position_t> {
    REQUIRE_FALSE(chart.measures.empty());
    REQUIRE_FALSE(chart.lane_layouts.empty());
    REQUIRE_FALSE(chart.lane_layouts.front().layout.lanes.empty());
    const auto lane = chart.lane_layouts.front().layout.lanes.front().id;
    std::set<std::uint64_t> occupied;
    for (const auto& note : chart.measures.front().notes)
        if (note.lane == lane) occupied.insert(note.position.row);
    std::uint64_t row = 0;
    while (occupied.contains(row)) ++row;
    return {lane, {chart.measures.front().id, row}};
}

void check_fixture_tap_command(const wormedit::song_document_t& document,
                               const wormedit::chart_document_t& chart) {
    const auto [lane, position] = fixture_empty_tap_slot(chart);
    auto history = wormedit::document_history_t::from(document);
    const std::array snaps{chart.divisions_per_standard_measure};
    auto setup = wormedit::editor_interaction_state_t::from(history, chart.id, lane, position,
                                                   snaps, snaps.front());
    REQUIRE(setup.state);
    const auto placed = wormedit::dispatch_editor_action(history, *setup.state,
                                                          wormedit::place_tap_action_t{position});
    for (const auto& diagnostic : placed.diagnostics) INFO(diagnostic.message);
    REQUIRE(placed.document_changed);
    CHECK(wormedit::validate_document(history.document).empty());
}

void choose_custom_fixture_authoring_layout(wormedit::song_document_t& document,
                                            const std::filesystem::path& path) {
    // PLAYER 7 and channel 1A intentionally remain unmapped. Choose geometry
    // for new authored notes explicitly; never infer a mapping for raw 1A/31.
    if (path.filename() != "custom-channel.bme") return;
    auto& chart = document.charts.front();
    REQUIRE(chart.lane_layouts.empty());
    const auto profile = wormedit::make_bme_7k_profile();
    chart.lane_layouts.push_back({.id = wormedit::object_id_t{1'000'000},
                                      .position = {chart.measures.front().id, 0},
                                      .layout = profile.layout});
    chart.input_profile = profile.input;
}

void check_positive_fixture(const std::filesystem::path& path) {
    auto document = parse_positive_fixture(path);
    const auto diagnostics = wormedit::validate_document(document);
    for (const auto& diagnostic : diagnostics) INFO(diagnostic.message);
    REQUIRE(diagnostics.empty());
    REQUIRE_FALSE(document.charts.empty());
    choose_custom_fixture_authoring_layout(document, path);
    for (const auto& chart : document.charts) check_fixture_tap_command(document, chart);
}

} // namespace

TEST_CASE("fixture_validation positive charts validate and accept tap placement",
          "[formats][fixture-validation][project-open]") {
    const auto fixtures = positive_chart_fixtures();
    REQUIRE_FALSE(fixtures.empty());
    for (const auto& path : fixtures) {
        DYNAMIC_SECTION(std::filesystem::relative(path, WORMEDIT_FIXTURE_DIR).string()) {
            check_positive_fixture(path);
        }
    }
}
