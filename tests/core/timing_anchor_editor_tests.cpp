#include <wormedit/core/timing_anchor_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <ranges>

using namespace wormedit;

namespace {

auto timing_anchor_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 7,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{3}}},
                    .bpms = {{.id = object_id_t{10},
                              .position = {measure_id_t{3}, 0},
                              .beats_per_minute = {120, 1}}},
                    .timing_anchors = {{.id = object_id_t{11},
                                        .position = {measure_id_t{3}, 240},
                                        .source_sample_frame = 0,
                                        .locked = false}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

auto same_anchor(const timing_anchor_t& left, const timing_anchor_t& right) -> bool {
    return left.id == right.id && left.position == right.position &&
           left.source_sample_frame == right.source_sample_frame && left.locked == right.locked;
}

} // namespace

TEST_CASE("timing anchor insert update and delete preserve exact data through history",
          "[unit][core][timing-anchor-editor]") {
    auto history = document_history_t::from(timing_anchor_song());
    const auto coincident_position = local_position_t{measure_id_t{3}, 240};
    const auto inserted = apply_timing_anchor_edit(
        history,
        wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>{
            .chart = chart_id_t{2},
            .value = {.position = coincident_position,
                      .settings = {.source_sample_frame = std::numeric_limits<std::uint64_t>::max(),
                                   .locked = true}}});
    REQUIRE(inserted);
    REQUIRE(history.document.charts.front().timing_anchors.size() == 2);
    const auto added = history.document.charts.front().timing_anchors.back();
    CHECK(added.id == object_id_t{12});
    CHECK(added.position == coincident_position);
    CHECK(added.source_sample_frame == std::numeric_limits<std::uint64_t>::max());
    CHECK(added.locked);
    CHECK(history.document.revision == 8);
    CHECK(is_document_dirty(history));

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().timing_anchors.size() == 1);
    CHECK_FALSE(is_document_dirty(history));
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().timing_anchors.back().id == added.id);
    CHECK(is_document_dirty(history));

    const auto updated_position = local_position_t{measure_id_t{3}, 17};
    const auto update = apply_timing_anchor_edit(
        history, wormedit::update_edit_t<wormedit::timing_anchor_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = added.id,
                     .value = {.position = updated_position,
                               .settings = {.source_sample_frame = 0, .locked = false}}});
    REQUIRE(update);
    const auto updated = history.document.charts.front().timing_anchors.back();
    CHECK(updated.id == added.id);
    CHECK(updated.position == updated_position);
    CHECK(updated.source_sample_frame == 0);
    CHECK_FALSE(updated.locked);

    const auto revision_after_update = history.document.revision;
    const auto no_op = apply_timing_anchor_edit(
        history, wormedit::update_edit_t<wormedit::timing_anchor_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = added.id,
                     .value = {.position = updated.position,
                               .settings = {.source_sample_frame = updated.source_sample_frame,
                                            .locked = updated.locked}}});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == revision_after_update);
    CHECK(history.undo.size() == 2);

    REQUIRE(undo_document_command(history));
    CHECK(same_anchor(history.document.charts.front().timing_anchors.back(), added));
    REQUIRE(redo_document_command(history));
    CHECK(same_anchor(history.document.charts.front().timing_anchors.back(), updated));

    const auto deletion = apply_timing_anchor_edit(
        history, wormedit::delete_edit_t<wormedit::timing_anchor_edit_value_t>{
                     .chart = chart_id_t{2}, .id = added.id});
    REQUIRE(deletion);
    REQUIRE(history.document.charts.front().timing_anchors.size() == 1);
    REQUIRE(undo_document_command(history));
    CHECK(same_anchor(history.document.charts.front().timing_anchors.back(), updated));
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().timing_anchors.size() == 1);
    CHECK(is_document_dirty(history));
}

TEST_CASE("timing anchor edits reject invalid IDs and invalid positions atomically",
          "[unit][core][timing-anchor-editor][negative]") {
    auto history = document_history_t::from(timing_anchor_song());
    const auto original = history.document.charts.front().timing_anchors.front();
    const auto revision = history.document.revision;

    const auto unknown_chart = apply_timing_anchor_edit(
        history, wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>{
                     .chart = chart_id_t{99},
                     .value = {.position = {measure_id_t{3}, 120},
                               .settings = {.source_sample_frame = 1, .locked = false}}});
    CHECK_FALSE(unknown_chart);
    CHECK(has_code(unknown_chart, diagnostic_code_t::invalid_id));

    const auto unknown_anchor = apply_timing_anchor_edit(
        history, wormedit::update_edit_t<wormedit::timing_anchor_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = object_id_t{99},
                     .value = {.position = {measure_id_t{3}, 120},
                               .settings = {.source_sample_frame = 2, .locked = true}}});
    CHECK_FALSE(unknown_anchor);
    CHECK(has_code(unknown_anchor, diagnostic_code_t::invalid_id));
    const auto zero_anchor = apply_timing_anchor_edit(
        history, wormedit::delete_edit_t<wormedit::timing_anchor_edit_value_t>{
                     .chart = chart_id_t{2}, .id = object_id_t{0}});
    CHECK_FALSE(zero_anchor);
    CHECK(has_code(zero_anchor, diagnostic_code_t::invalid_id));

    for (const auto position :
         {local_position_t{measure_id_t{99}, 0}, local_position_t{measure_id_t{3}, 960}}) {
        const auto invalid_position = apply_timing_anchor_edit(
            history, wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>{
                         .chart = chart_id_t{2},
                         .value = {.position = position,
                                   .settings = {.source_sample_frame = 3, .locked = false}}});
        CHECK_FALSE(invalid_position);
        CHECK(has_code(invalid_position, diagnostic_code_t::row_out_of_range));
    }

    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().timing_anchors.size() == 1);
    CHECK(same_anchor(history.document.charts.front().timing_anchors.front(), original));
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
}

TEST_CASE("timing anchor IDs are unambiguous across the chart and insertion respects caps",
          "[unit][core][timing-anchor-editor][bounds]") {
    auto duplicate_song = timing_anchor_song();
    duplicate_song.charts.front().timing_anchors.push_back(
        {.id = object_id_t{11}, .position = {measure_id_t{3}, 360}});
    auto duplicate_history = document_history_t::from(std::move(duplicate_song));
    const auto ambiguous = apply_timing_anchor_edit(
        duplicate_history, wormedit::update_edit_t<wormedit::timing_anchor_edit_value_t>{
                               .chart = chart_id_t{2},
                               .id = object_id_t{11},
                               .value = {.position = {measure_id_t{3}, 480},
                                         .settings = {.source_sample_frame = 4, .locked = false}}});
    CHECK_FALSE(ambiguous);
    CHECK(has_code(ambiguous, diagnostic_code_t::duplicate_id));
    CHECK(duplicate_history.document.charts.front().timing_anchors.size() == 2);
    CHECK(duplicate_history.undo.empty());

    auto cross_type_duplicate = timing_anchor_song();
    cross_type_duplicate.charts.front().timing_anchors.front().id = object_id_t{10};
    auto cross_type_history = document_history_t::from(std::move(cross_type_duplicate));
    const auto cross_type_ambiguous = apply_timing_anchor_edit(
        cross_type_history, wormedit::delete_edit_t<wormedit::timing_anchor_edit_value_t>{
                                .chart = chart_id_t{2}, .id = object_id_t{10}});
    CHECK_FALSE(cross_type_ambiguous);
    CHECK(has_code(cross_type_ambiguous, diagnostic_code_t::duplicate_id));
    CHECK(cross_type_history.document.charts.front().timing_anchors.size() == 1);

    auto history = document_history_t::from(timing_anchor_song());
    const auto position = local_position_t{measure_id_t{3}, 120};
    const auto anchor_capped = apply_timing_anchor_edit(
        history,
        wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>{
            .chart = chart_id_t{2},
            .value = {.position = position,
                      .settings = {.source_sample_frame = 0, .locked = false}}},
        {.maximum_timing_anchors = 1});
    CHECK_FALSE(anchor_capped);
    CHECK(has_code(anchor_capped, diagnostic_code_t::invalid_resolution));

    const auto objects_capped = apply_timing_anchor_edit(
        history,
        wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>{
            .chart = chart_id_t{2},
            .value = {.position = position,
                      .settings = {.source_sample_frame = 0, .locked = false}}},
        {.maximum_chart_objects = 2});
    CHECK_FALSE(objects_capped);
    CHECK(has_code(objects_capped, diagnostic_code_t::invalid_resolution));

    const auto zero_cap =
        apply_timing_anchor_edit(history,
                                 wormedit::delete_edit_t<wormedit::timing_anchor_edit_value_t>{
                                     .chart = chart_id_t{2}, .id = object_id_t{11}},
                                 {.maximum_timing_anchors = 0});
    CHECK_FALSE(zero_cap);
    CHECK(has_code(zero_cap, diagnostic_code_t::invalid_resolution));

    auto exhausted_song = timing_anchor_song();
    exhausted_song.charts.front().timing_anchors.front().id =
        object_id_t{std::numeric_limits<std::uint64_t>::max()};
    auto exhausted_history = document_history_t::from(std::move(exhausted_song));
    const auto exhausted = apply_timing_anchor_edit(
        exhausted_history, wormedit::insert_edit_t<wormedit::timing_anchor_edit_value_t>{
                               .chart = chart_id_t{2},
                               .value = {.position = position,
                                         .settings = {.source_sample_frame = 0, .locked = false}}});
    CHECK_FALSE(exhausted);
    CHECK(has_code(exhausted, diagnostic_code_t::invalid_resolution));
    CHECK(exhausted_history.document.charts.front().timing_anchors.size() == 1);
    CHECK(exhausted_history.undo.empty());
}
