#include <wormedit/core/scroll_speed_editor.h>
#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <utility>

using namespace wormedit;

namespace {

auto scroll_speed_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{3}}},
                    .bpms =
                        {
                            {.id = object_id_t{10},
                             .position = {measure_id_t{3}, 0},
                             .beats_per_minute = {120, 1}},
                        },
                    .scrolls = {{.id = object_id_t{11},
                                 .position = {measure_id_t{3}, 0},
                                 .multiplier = {2, 1},
                                 .provenance = {.source_order = 4}}},
                    .speeds = {{.id = object_id_t{12},
                                .position = {measure_id_t{3}, 0},
                                .multiplier = {3, 1},
                                .transition_beats = {0, 1},
                                .provenance = {.source_order = 7}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("scroll and speed editing is revision checked and undoable",
          "[unit][core][scroll-speed-editor]") {
    auto history = document_history_t::from(scroll_speed_song());
    const auto position = local_position_t{measure_id_t{3}, 240};

    const auto inserted_scroll = apply_scroll_speed_edit(
        history, insert_scroll_event_action_t{chart_id_t{2}, position, {-2, 1}});
    REQUIRE(inserted_scroll);
    REQUIRE(history.document.charts.front().scrolls.size() == 2);
    const auto scroll = history.document.charts.front().scrolls.back();
    CHECK(scroll.id == object_id_t{13});
    CHECK(scroll.position == position);
    CHECK(scroll.multiplier == signed_rational_t{-2, 1});
    CHECK(scroll.provenance.source_order == 5);
    CHECK(scroll.provenance.family == format_family_t::native_project);

    const auto inserted_speed = apply_scroll_speed_edit(
        history, insert_speed_event_action_t{chart_id_t{2}, position, {1, 2}, {0, 1}, false});
    REQUIRE(inserted_speed);
    REQUIRE(history.document.charts.front().speeds.size() == 2);
    const auto speed = history.document.charts.front().speeds.back();
    CHECK(speed.id == object_id_t{14});
    CHECK(speed.position == position);
    CHECK(speed.multiplier == signed_rational_t{1, 2});
    CHECK(speed.transition_beats == rational_t{0, 1});
    CHECK_FALSE(speed.transition_is_seconds);
    CHECK(speed.provenance.source_order == 8);

    const auto& chart = history.document.charts.front();
    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto curves = build_scroll_speed_index(chart, *timing.index);
    REQUIRE(curves);
    const auto arriving = scroll_speed_at(*curves.index, *timing.index, {240, 1},
                                          timing_boundary_side_t::before_delays);
    const auto effective = scroll_speed_at(*curves.index, *timing.index, {240, 1});
    REQUIRE(arriving);
    REQUIRE(effective);
    CHECK(arriving.state->effective_multiplier == signed_rational_t{6, 1});
    CHECK(effective.state->scroll_multiplier == signed_rational_t{-2, 1});
    CHECK(effective.state->speed_multiplier == signed_rational_t{1, 2});
    CHECK(effective.state->effective_multiplier == signed_rational_t{-1, 1});

    const auto update = apply_scroll_speed_edit(
        history,
        update_speed_event_action_t{chart_id_t{2}, speed.id, position, {5, 1}, {2, 1}, true});
    REQUIRE(update);
    const auto& updated = history.document.charts.front().speeds.back();
    CHECK(updated.id == speed.id);
    CHECK(updated.provenance.source_order == speed.provenance.source_order);
    CHECK(updated.position == speed.position);
    CHECK(updated.multiplier == signed_rational_t{5, 1});
    CHECK(updated.transition_beats == rational_t{2, 1});
    CHECK(updated.transition_is_seconds);

    const auto no_op = apply_scroll_speed_edit(
        history,
        update_speed_event_action_t{chart_id_t{2}, speed.id, updated.position, updated.multiplier,
                                    updated.transition_beats, updated.transition_is_seconds});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 7);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().speeds.back().multiplier == signed_rational_t{1, 2});
    REQUIRE(
        apply_scroll_speed_edit(history, delete_scroll_event_action_t{chart_id_t{2}, scroll.id}));
    CHECK(history.document.charts.front().scrolls.size() == 1);
    REQUIRE(undo_document_command(history));
    REQUIRE(history.document.charts.front().scrolls.size() == 2);
    CHECK(history.document.charts.front().scrolls.back().id == scroll.id);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().scrolls.size() == 1);
    CHECK(is_document_dirty(history));
}

TEST_CASE("same-row scroll and speed events keep source ordering and compose their effective value",
          "[unit][core][scroll-speed-editor][ordering]") {
    auto song = scroll_speed_song();
    auto& chart = song.charts.front();
    chart.scrolls = {{.id = object_id_t{20},
                      .position = {measure_id_t{3}, 240},
                      .multiplier = {2, 1},
                      .provenance = {.source_order = 10}},
                     {.id = object_id_t{21},
                      .position = {measure_id_t{3}, 240},
                      .multiplier = {-3, 1},
                      .provenance = {.source_order = 11}}};
    chart.speeds = {{.id = object_id_t{22},
                     .position = {measure_id_t{3}, 240},
                     .multiplier = {4, 1},
                     .provenance = {.source_order = 20}},
                    {.id = object_id_t{23},
                     .position = {measure_id_t{3}, 240},
                     .multiplier = {5, 1},
                     .provenance = {.source_order = 21}}};
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));

    const auto inserted = apply_scroll_speed_edit(
        history, insert_scroll_event_action_t{chart_id_t{2}, {measure_id_t{3}, 240}, {2, 1}});
    REQUIRE(inserted);
    const auto& result_chart = history.document.charts.front();
    REQUIRE(result_chart.scrolls.size() == 3);
    CHECK(result_chart.scrolls.back().provenance.source_order == 12);

    const auto timing = build_timing_index(result_chart);
    REQUIRE(timing);
    const auto curves = build_scroll_speed_index(result_chart, *timing.index);
    REQUIRE(curves);
    const auto effective = scroll_speed_at(*curves.index, *timing.index, {240, 1});
    REQUIRE(effective);
    CHECK(effective.state->scroll_multiplier == signed_rational_t{2, 1});
    CHECK(effective.state->speed_multiplier == signed_rational_t{5, 1});
    CHECK(effective.state->effective_multiplier == signed_rational_t{10, 1});
}

TEST_CASE("scroll and speed edits reject invalid IDs, invalid domains, and out-of-bounds positions",
          "[unit][core][scroll-speed-editor][negative]") {
    auto history = document_history_t::from(scroll_speed_song());
    const auto revision = history.document.revision;

    const auto unknown_chart = apply_scroll_speed_edit(
        history,
        insert_speed_event_action_t{chart_id_t{99}, {measure_id_t{3}, 120}, {1, 1}, {0, 1}, false});
    CHECK_FALSE(unknown_chart);
    CHECK(has_code(unknown_chart, diagnostic_code_t::invalid_id));
    const auto unknown_event = apply_scroll_speed_edit(
        history,
        update_scroll_event_action_t{chart_id_t{2}, object_id_t{99}, {measure_id_t{3}, 0}, {1, 1}});
    CHECK_FALSE(unknown_event);
    CHECK(has_code(unknown_event, diagnostic_code_t::invalid_id));
    const auto zero_event = apply_scroll_speed_edit(
        history, delete_speed_event_action_t{chart_id_t{2}, object_id_t{0}});
    CHECK_FALSE(zero_event);
    CHECK(has_code(zero_event, diagnostic_code_t::invalid_id));

    for (const auto position :
         {local_position_t{measure_id_t{99}, 0}, local_position_t{measure_id_t{3}, 960}}) {
        const auto invalid = apply_scroll_speed_edit(
            history, insert_scroll_event_action_t{chart_id_t{2}, position, {1, 1}});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, diagnostic_code_t::row_out_of_range));
    }
    const auto invalid_scroll = apply_scroll_speed_edit(
        history, insert_scroll_event_action_t{chart_id_t{2}, {measure_id_t{3}, 120}, {1, 0}});
    CHECK_FALSE(invalid_scroll);
    CHECK(has_code(invalid_scroll, diagnostic_code_t::invalid_rational));

    for (const auto [multiplier, duration] :
         {std::pair{signed_rational_t{-1, 1}, rational_t{1, 1}},
          std::pair{signed_rational_t{1, 0}, rational_t{1, 1}},
          std::pair{signed_rational_t{1, 1}, rational_t{1, 0}}}) {
        const auto invalid = apply_scroll_speed_edit(
            history, insert_speed_event_action_t{
                         chart_id_t{2}, {measure_id_t{3}, 120}, multiplier, duration, false});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, diagnostic_code_t::invalid_rational));
    }

    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().scrolls.size() == 1);
    CHECK(history.document.charts.front().speeds.size() == 1);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
}

TEST_CASE("scroll and speed editor enforces event and chart-wide object caps and unique IDs",
          "[unit][core][scroll-speed-editor][bounds]") {
    auto history = document_history_t::from(scroll_speed_song());
    const auto position = local_position_t{measure_id_t{3}, 120};
    const auto scroll_capped = apply_scroll_speed_edit(
        history, insert_scroll_event_action_t{chart_id_t{2}, position, {1, 1}},
        {.maximum_scroll_events = 1});
    CHECK_FALSE(scroll_capped);
    CHECK(has_code(scroll_capped, diagnostic_code_t::invalid_resolution));
    const auto speed_capped = apply_scroll_speed_edit(
        history, insert_speed_event_action_t{chart_id_t{2}, position, {1, 1}, {0, 1}, false},
        {.maximum_speed_events = 1});
    CHECK_FALSE(speed_capped);
    CHECK(has_code(speed_capped, diagnostic_code_t::invalid_resolution));
    const auto objects_capped = apply_scroll_speed_edit(
        history, insert_scroll_event_action_t{chart_id_t{2}, position, {1, 1}},
        {.maximum_chart_objects = 3});
    CHECK_FALSE(objects_capped);
    CHECK(has_code(objects_capped, diagnostic_code_t::invalid_resolution));

    auto duplicate_song = scroll_speed_song();
    duplicate_song.charts.front().scrolls.push_back(
        {.id = object_id_t{11}, .position = {measure_id_t{3}, 120}, .multiplier = {1, 1}});
    auto duplicate_history = document_history_t::from(std::move(duplicate_song));
    const auto ambiguous = apply_scroll_speed_edit(
        duplicate_history,
        update_scroll_event_action_t{chart_id_t{2}, object_id_t{11}, position, {3, 1}});
    CHECK_FALSE(ambiguous);
    CHECK(has_code(ambiguous, diagnostic_code_t::duplicate_id));
    CHECK(duplicate_history.document.charts.front().scrolls.size() == 2);
    CHECK(duplicate_history.undo.empty());

    auto cross_type_song = scroll_speed_song();
    cross_type_song.charts.front().bpms.front().id = object_id_t{11};
    auto cross_type_history = document_history_t::from(std::move(cross_type_song));
    const auto cross_type_ambiguous = apply_scroll_speed_edit(
        cross_type_history, delete_scroll_event_action_t{chart_id_t{2}, object_id_t{11}});
    CHECK_FALSE(cross_type_ambiguous);
    CHECK(has_code(cross_type_ambiguous, diagnostic_code_t::duplicate_id));
    CHECK(cross_type_history.document.charts.front().scrolls.size() == 1);
    CHECK(cross_type_history.undo.empty());
}
