#include <wormedit/core/chart_compare.h>
#include "sys/compare_view.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <ranges>
#include <utility>

namespace {

auto make_chart(std::uint64_t chart_id, std::uint64_t measure_id, std::uint64_t layout_id,
                std::uint64_t lane_id, std::uint64_t divisions) -> wormedit::chart_document_t {
    wormedit::chart_document_t chart;
    chart.id = wormedit::chart_id_t{chart_id};
    chart.divisions_per_standard_measure = divisions;
    chart.measures = {{.id = wormedit::measure_id_t{measure_id},
                       .definition = {{1, 1}, wormedit::meter_t{4, 4}}}};
    chart.lane_layouts = {{.id = wormedit::object_id_t{layout_id},
                           .position = {wormedit::measure_id_t{measure_id}, 0},
                           .layout = {.profile_id = "compare",
                                      .lanes = {{.id = wormedit::lane_id_t{lane_id},
                                                 .name = "lane",
                                                 .role = wormedit::lane_role_t::playable,
                                                 .stage = wormedit::stage_group_t::left,
                                                 .display_order = 0}}}}};
    return chart;
}

auto note(std::uint64_t id, std::uint64_t measure, std::uint64_t row, std::uint64_t lane)
    -> wormedit::note_t {
    return {.id = wormedit::object_id_t{id},
            .position = {wormedit::measure_id_t{measure}, row},
            .lane = wormedit::lane_id_t{lane}};
}

auto song_with_two_resolutions() -> wormedit::song_document_t {
    wormedit::song_document_t song;
    song.id = wormedit::song_id_t{1};
    song.revision = 17;
    song.charts = {make_chart(1, 11, 101, 1, 960), make_chart(2, 22, 102, 2, 480)};
    song.charts[0].measures[0].notes.push_back(note(201, 11, 240, 1));
    song.charts[1].measures[0].notes.push_back(note(202, 22, 120, 2));
    return song;
}

auto compare(const wormedit::song_document_t& song, wormedit::chart_compare_request_t request = {})
    -> wormedit::chart_comparison_result_t {
    if (request.charts.empty()) {
        request.charts = {wormedit::chart_id_t{1}, wormedit::chart_id_t{2}};
    }
    return wormedit::chart_comparison_t::from(song, request);
}

} // namespace

TEST_CASE("Compare view selects a safe default pair for zero through three difficulties",
          "[unit][core][chart-compare][compare-view]") {
    using wormedit::chart_id_t;
    const std::array<chart_id_t, 0> none{};
    const std::array<chart_id_t, 1> one{chart_id_t{1}};
    const std::array<chart_id_t, 2> two{chart_id_t{1}, chart_id_t{2}};
    const std::array<chart_id_t, 3> three{chart_id_t{1}, chart_id_t{2}, chart_id_t{3}};
    const std::array<chart_id_t, 0> no_selection{};
    const std::array<chart_id_t, 2> selected_three{chart_id_t{3}, chart_id_t{2}};
    CHECK_FALSE(wormedit::sys::choose_compare_chart_pair(none, std::nullopt, no_selection));
    CHECK_FALSE(wormedit::sys::choose_compare_chart_pair(one, chart_id_t{1}, no_selection));
    CHECK((wormedit::sys::choose_compare_chart_pair(two, chart_id_t{1}, no_selection) ==
           std::pair{chart_id_t{1}, chart_id_t{2}}));
    CHECK((wormedit::sys::choose_compare_chart_pair(three, chart_id_t{1}, selected_three) ==
           std::pair{chart_id_t{1}, chart_id_t{3}}));
    CHECK((wormedit::sys::choose_compare_chart_pair(three, std::nullopt, no_selection) ==
           std::pair{chart_id_t{1}, chart_id_t{2}}));
}

TEST_CASE("chart comparison aligns exact musical rows across different resolutions",
          "[unit][core][chart-compare]") {
    auto song = song_with_two_resolutions();
    const auto equal = compare(song);
    REQUIRE(equal.ok());
    CHECK(equal.value.document_revision == 17);
    CHECK(equal.value.charts.size() == 2);
    CHECK(equal.value.differing_rows.empty());

    song.charts[1].measures[0].notes[0].position.row = 180;
    const auto different = compare(song);
    REQUIRE(different.ok());
    REQUIRE(different.value.differing_rows.size() == 2);
    CHECK((different.value.differing_rows[0].musical_position == wormedit::rational_t{1, 4}));
    CHECK((different.value.differing_rows[1].musical_position == wormedit::rational_t{3, 8}));
    REQUIRE(different.value.differing_rows[0].panes.size() == 2);
    REQUIRE(different.value.differing_rows[0].panes[0].local_position.has_value());
    REQUIRE(different.value.differing_rows[0].panes[1].local_position.has_value());
    CHECK(different.value.differing_rows[0].panes[0].local_position->row == 240);
    CHECK(different.value.differing_rows[0].panes[1].local_position->row == 120);
    CHECK(different.value.differing_rows[0].panes[0].differs);
    CHECK(different.value.differing_rows[0].panes[1].differs);
}

TEST_CASE("chart comparison options include sound layers labels and layout transitions",
          "[unit][core][chart-compare]") {
    auto song = song_with_two_resolutions();
    song.charts[0].measures[0].notes[0].sound = wormedit::resource_id_t{1};
    song.charts[1].measures[0].notes[0].sound = wormedit::resource_id_t{2};
    song.charts[0].measures[0].notes[0].semantic_layers = {wormedit::layer_id_t{3}};
    song.charts[1].measures[0].notes[0].semantic_layers = {wormedit::layer_id_t{4}};
    song.charts[0].semantic_layers = {
        {.id = wormedit::layer_id_t{3}, .name = "melody", .color = {255, 0, 0, 255}}};
    song.charts[1].semantic_layers = {
        {.id = wormedit::layer_id_t{4}, .name = "rhythm", .color = {0, 0, 255, 255}}};
    song.charts[0].measures[0].notes[0].label = "left";
    song.charts[1].measures[0].notes[0].label = "right";
    CHECK(compare(song).value.differing_rows.empty());

    wormedit::chart_compare_request_t request;
    request.include_sound_assignments = true;
    CHECK(compare(song, request).value.differing_rows.size() == 1);
    request.include_sound_assignments = false;
    request.include_semantic_layers = true;
    CHECK(compare(song, request).value.differing_rows.size() == 1);
    request.include_semantic_layers = false;
    request.include_labels = true;
    CHECK(compare(song, request).value.differing_rows.size() == 1);

    song.charts[0].measures[0].notes[0].sound.reset();
    song.charts[1].measures[0].notes[0].sound.reset();
    song.charts[0].measures[0].notes[0].semantic_layers.clear();
    song.charts[1].measures[0].notes[0].semantic_layers.clear();
    song.charts[0].measures[0].notes[0].label.reset();
    song.charts[1].measures[0].notes[0].label.reset();
    song.charts[1].lane_layouts.push_back(
        {.id = wormedit::object_id_t{103},
         .position = {wormedit::measure_id_t{22}, 240},
         .layout = {.profile_id = "changed",
                    .lanes = {{.id = wormedit::lane_id_t{2},
                               .name = "lane",
                               .role = wormedit::lane_role_t::playable,
                               .stage = wormedit::stage_group_t::right,
                               .display_order = 0}}}});
    request = {};
    const auto layout_difference = compare(song, request);
    REQUIRE(layout_difference.ok());
    REQUIRE(layout_difference.value.differing_rows.size() == 1);
    CHECK((layout_difference.value.differing_rows[0].musical_position == wormedit::rational_t{1, 2}));
    request.compare_layouts = false;
    CHECK(compare(song, request).value.differing_rows.empty());
}

TEST_CASE("chart comparison emits hold parts and rejects invalid or over-cap requests atomically",
          "[unit][core][chart-compare][bounds]") {
    auto song = song_with_two_resolutions();
    song.charts[0].measures[0].notes.clear();
    song.charts[1].measures[0].notes.clear();
    auto first_hold = note(301, 11, 240, 1);
    first_hold.kind = wormedit::note_kind_t::hold;
    first_hold.end = wormedit::local_position_t{wormedit::measure_id_t{11}, 720};
    auto second_hold = note(302, 22, 120, 2);
    second_hold.kind = wormedit::note_kind_t::hold;
    second_hold.end = wormedit::local_position_t{wormedit::measure_id_t{22}, 300};
    song.charts[0].measures[0].notes.push_back(first_hold);
    song.charts[1].measures[0].notes.push_back(second_hold);
    const auto hold_difference = compare(song);
    REQUIRE(hold_difference.ok());
    REQUIRE(hold_difference.value.differing_rows.size() == 2);
    CHECK(hold_difference.value.differing_rows[1].panes[0].notes.front().part ==
          wormedit::chart_compare_note_part_t::hold_tail);

    wormedit::chart_compare_request_t capped;
    capped.maximum_difference_rows = 1;
    const auto rejected = compare(song, capped);
    CHECK_FALSE(rejected.ok());
    CHECK(rejected.value.differing_rows.empty());

    capped = {};
    capped.maximum_input_note_endpoints = 3;
    const auto endpoints_rejected = compare(song, capped);
    CHECK_FALSE(endpoints_rejected.ok());
    CHECK(endpoints_rejected.value.differing_rows.empty());

    auto labeled = song_with_two_resolutions();
    labeled.charts[0].measures[0].notes[0].label = "too long";
    capped = {};
    capped.include_labels = true;
    capped.maximum_signature_text_bytes = 2;
    const auto text_rejected = compare(labeled, capped);
    CHECK_FALSE(text_rejected.ok());
    CHECK(text_rejected.value.differing_rows.empty());

    song.charts[1].measures[0].notes[0].end = song.charts[1].measures[0].notes[0].position;
    const auto invalid_hold = compare(song);
    CHECK_FALSE(invalid_hold.ok());
    CHECK(invalid_hold.value.differing_rows.empty());
}

TEST_CASE("chart comparison reports layout duration and unrepresentable pane positions",
          "[unit][core][chart-compare]") {
    auto song = song_with_two_resolutions();
    song.charts[0].measures[0].notes.clear();
    song.charts[1].measures[0].notes.clear();
    song.charts[1].measures[0].definition = {{1, 2}, wormedit::meter_t{1, 2}};
    const auto result = compare(song);
    REQUIRE(result.ok());
    REQUIRE(result.value.differing_rows.size() == 1);
    CHECK((result.value.differing_rows[0].musical_position == wormedit::rational_t{1, 2}));
    REQUIRE(result.value.differing_rows[0].panes[0].local_position.has_value());
    CHECK(result.value.differing_rows[0].panes[0].local_position->row == 480);
    CHECK_FALSE(result.value.differing_rows[0].panes[1].local_position.has_value());
}

TEST_CASE("chart comparison keeps fifty-thousand-note equal difficulties bounded",
          "[unit][core][chart-compare][stress]") {
    auto song = song_with_two_resolutions();
    song.charts[0].measures[0].notes.clear();
    song.charts[1].measures[0].notes.clear();
    song.charts[0].divisions_per_standard_measure = 100'000;
    song.charts[1].divisions_per_standard_measure = 100'000;
    song.charts[0].measures[0].notes.reserve(50'000);
    song.charts[1].measures[0].notes.reserve(50'000);
    for (std::uint64_t index = 0; index < 50'000; ++index) {
        song.charts[0].measures[0].notes.push_back(note(1'000 + index, 11, index * 2, 1));
        song.charts[1].measures[0].notes.push_back(note(100'000 + index, 22, index * 2, 2));
    }
    wormedit::chart_compare_request_t request;
    request.maximum_input_note_endpoints = 100'000;
    const auto equal = compare(song, request);
    REQUIRE(equal.ok());
    CHECK(equal.value.differing_rows.empty());

    song.charts[1].measures[0].notes.back().position.row += 1;
    const auto changed = compare(song, request);
    REQUIRE(changed.ok());
    CHECK(changed.value.differing_rows.size() == 2);
}

TEST_CASE("diagnosed chart comparison preserves failure metadata",
          "[unit][core][chart-compare][result]") {
    const auto song = song_with_two_resolutions();
    const wormedit::chart_compare_request_t request{.charts = {wormedit::chart_id_t{1}}};
    const auto result = wormedit::chart_comparison_t::from(song, request);
    REQUIRE_FALSE(result.ok());
    CHECK(result.value.document_revision == song.revision);
    CHECK(result.value.charts == request.charts);
    CHECK(result.value.differing_rows.empty());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == wormedit::diagnostic_code_t::invalid_resolution);
}

TEST_CASE("diagnosed chart comparison preserves warning success and feedback",
          "[unit][core][chart-compare][result]") {
    const wormedit::chart_comparison_result_t result{
        .value = {.document_revision = 17, .charts = {}, .differing_rows = {}},
        .diagnostics = {{.severity = wormedit::diagnostic_severity_t::warning,
                         .code = wormedit::diagnostic_code_t::approximated_feature,
                         .message = "approximation", .object = std::nullopt}}};
    CHECK(result.ok());
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().severity == wormedit::diagnostic_severity_t::warning);
    CHECK(result.value.document_revision == 17);
}
