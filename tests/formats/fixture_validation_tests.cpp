#include "fixture_encoding.h"
#include "../support/bms_profile_fixtures.h"
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
using wormedit::test::explicit_fixture_profile;

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
    CHECK_FALSE(wormedit::diagnostic_queries::has_errors(wormedit::validate_document(history.document)));
}

void check_public_profile_refusal(const std::filesystem::path& path) {
    const auto source = read_fixture(path);
    const auto parsed = wormedit::parse_bms_bytes(std::as_bytes(std::span{source.data(), source.size()}),
        {.file_extension = path.extension().string()});
    CHECK_FALSE(parsed.value.song);
    REQUIRE(parsed.value.encoding.original_bytes);
    CHECK(parsed.value.encoding.original_bytes->size() == source.size());
    CHECK(std::ranges::any_of(parsed.diagnostics, [](const auto& entry) {
        return entry.code == wormedit::bms_parse_diagnostic_code_t::unsupported_channel;
    }));
}

void check_positive_fixture(const std::filesystem::path& path) {
    if (path.filename() == "custom-channel.bme") {
        check_public_profile_refusal(path);
        return;
    }
    auto document = parse_positive_fixture(path);
    const auto diagnostics = wormedit::validate_document(document);
    for (const auto& diagnostic : diagnostics) INFO(diagnostic.message);
    REQUIRE_FALSE(wormedit::diagnostic_queries::has_errors(diagnostics));
    REQUIRE_FALSE(document.read_charts().empty());
    for (const auto* chart : document.read_charts()) check_fixture_tap_command(document, *chart);
}

} // namespace

TEST_CASE("fixture_validation positive charts validate authoring or request an explicit profile",
          "[formats][fixture-validation][project-open]") {
    const auto fixtures = positive_chart_fixtures();
    REQUIRE_FALSE(fixtures.empty());
    for (const auto& path : fixtures) {
        DYNAMIC_SECTION(std::filesystem::relative(path, WORMEDIT_FIXTURE_DIR).string()) {
            check_positive_fixture(path);
        }
    }
}
