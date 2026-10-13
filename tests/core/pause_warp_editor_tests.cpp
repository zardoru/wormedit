#include <wormedit/core/pause_warp_editor.h>
#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <utility>

using namespace wormedit;

namespace {

auto pause_warp_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{3}}},
                    .bpms = {{.id = object_id_t{10},
                              .position = {measure_id_t{3}, 0},
                              .beats_per_minute = {120, 1},
                              .provenance = {.source_order = 1}}},
                    .pauses = {{.id = object_id_t{20},
                                .position = {measure_id_t{3}, 240},
                                .kind = pause_kind_t::delay,
                                .duration = {500, 1},
                                .source_unit = pause_unit_t::milliseconds,
                                .provenance = {.source_order = 4}},
                               {.id = object_id_t{21},
                                .position = {measure_id_t{3}, 240},
                                .kind = pause_kind_t::stop,
                                .duration = {1, 1},
                                .source_unit = pause_unit_t::seconds,
                                .provenance = {.source_order = 7}}},
                    .warps = {{.id = object_id_t{30},
                               .span = {{measure_id_t{3}, 720}, {measure_id_t{3}, 840}},
                               .source_encoding = warp_provenance_t::negative_bpm,
                               .provenance = {.source_order = 2}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("pause and warp editing allocates IDs, preserves identity, and shares undo history",
          "[unit][core][pause-warp-editor]") {
    auto history = document_history_t::from(pause_warp_song());
    const auto position = local_position_t{measure_id_t{3}, 240};
    const auto inserted_pause = apply_pause_warp_edit(
        history, 4,
        insert_pause_event_action_t{
            chart_id_t{2}, position, pause_kind_t::stop, {1, 4}, pause_unit_t::seconds});
    REQUIRE(inserted_pause);
    const auto pause = history.document.charts.front().pauses.back();
    CHECK(pause.id == object_id_t{31});
    CHECK(pause.position == position);
    CHECK(pause.kind == pause_kind_t::stop);
    CHECK(pause.duration == signed_rational_t{1, 4});
    CHECK(pause.source_unit == pause_unit_t::seconds);
    CHECK(pause.provenance.source_order == 8);
    CHECK(pause.provenance.family == format_family_t::native_project);

    const position_span_t new_span{{measure_id_t{3}, 480}, {measure_id_t{3}, 600}};
    const auto inserted_warp = apply_pause_warp_edit(
        history, 5,
        insert_warp_event_action_t{chart_id_t{2}, new_span, warp_provenance_t::negative_stop});
    REQUIRE(inserted_warp);
    const auto warp = history.document.charts.front().warps.back();
    CHECK(warp.id == object_id_t{32});
    CHECK(warp.span.begin == new_span.begin);
    CHECK(warp.span.end == new_span.end);
    CHECK(warp.source_encoding == warp_provenance_t::negative_stop);
    CHECK(warp.provenance.source_order == 3);

    const auto updated_span = position_span_t{{measure_id_t{3}, 480}, {measure_id_t{3}, 660}};
    const auto updated = apply_pause_warp_edit(
        history, 6, update_warp_event_action_t{chart_id_t{2}, warp.id, updated_span, std::nullopt});
    REQUIRE(updated);
    const auto& updated_warp = history.document.charts.front().warps.back();
    CHECK(updated_warp.id == warp.id);
    CHECK(updated_warp.span.begin == updated_span.begin);
    CHECK(updated_warp.span.end == updated_span.end);
    CHECK(updated_warp.source_encoding == warp.source_encoding);
    CHECK(updated_warp.provenance.source_order == warp.provenance.source_order);
    CHECK(updated_warp.provenance.family == warp.provenance.family);

    const auto no_op = apply_pause_warp_edit(
        history, 7, update_warp_event_action_t{chart_id_t{2}, warp.id, updated_span, std::nullopt});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 7);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().warps.back().span.end == warp.span.end);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().warps.back().span.end == updated_span.end);
    CHECK(is_document_dirty(history));

    const auto explicit_encoding =
        apply_pause_warp_edit(history, 7,
                              update_warp_event_action_t{chart_id_t{2}, warp.id, updated_span,
                                                         warp_provenance_t::native});
    REQUIRE(explicit_encoding);
    CHECK(history.document.charts.front().warps.back().source_encoding ==
          warp_provenance_t::native);
}

TEST_CASE("same-row pause insertion gets a later source order and updates exact timing",
          "[unit][core][pause-warp-editor][ordering]") {
    auto history = document_history_t::from(pause_warp_song());
    const auto inserted = apply_pause_warp_edit(history, 4,
                                                insert_pause_event_action_t{chart_id_t{2},
                                                                            {measure_id_t{3}, 240},
                                                                            pause_kind_t::stop,
                                                                            {1, 4},
                                                                            pause_unit_t::seconds});
    REQUIRE(inserted);
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.pauses.back().provenance.source_order == 8);

    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto at_row = std::ranges::find(timing.index->rows, std::uint64_t{240},
                                          &timing_row_state_t::absolute_row);
    REQUIRE(at_row != timing.index->rows.end());
    CHECK(at_row->before_delays_seconds == signed_rational_t{1, 2});
    CHECK(at_row->event_seconds == signed_rational_t{1, 1});
    CHECK(at_row->after_stops_seconds == signed_rational_t{9, 4});
}

TEST_CASE("pause edits preserve BMS baselines, reject incoherent updates, and delete projections",
          "[unit][core][pause-warp-editor][bms]") {
    auto song = pause_warp_song();
    auto& chart = song.charts.front();
    chart.pauses = {{.id = object_id_t{40},
                     .position = {measure_id_t{3}, 240},
                     .kind = pause_kind_t::stop,
                     .duration = {48, 1},
                     .source_unit = pause_unit_t::bms_stop_ticks,
                     .provenance = {.family = format_family_t::bms,
                                    .profile = "bms-byte-parser",
                                    .source_field = "09 slot 1 STOP01",
                                    .source_order = 55}}};
    chart.bms_pause_projections = {{.event = object_id_t{40},
                                    .source_order = 55,
                                    .imported_position = {measure_id_t{3}, 240},
                                    .imported_kind = pause_kind_t::stop,
                                    .imported_duration = {48, 1},
                                    .imported_source_unit = pause_unit_t::bms_stop_ticks}};
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));

    const auto update =
        apply_pause_warp_edit(history, 4,
                              update_pause_event_action_t{chart_id_t{2},
                                                          object_id_t{40},
                                                          {measure_id_t{3}, 480},
                                                          pause_kind_t::stop,
                                                          {96, 1},
                                                          pause_unit_t::bms_stop_ticks});
    REQUIRE(update);
    REQUIRE(history.document.charts.front().bms_pause_projections.size() == 1);
    const auto& projection = history.document.charts.front().bms_pause_projections.front();
    CHECK(projection.imported_position == local_position_t{measure_id_t{3}, 240});
    CHECK(projection.imported_duration == signed_rational_t{48, 1});
    CHECK(projection.source_order == 55);

    const auto incoherent =
        apply_pause_warp_edit(history, 5,
                              update_pause_event_action_t{chart_id_t{2},
                                                          object_id_t{40},
                                                          {measure_id_t{3}, 480},
                                                          pause_kind_t::delay,
                                                          {96, 1},
                                                          pause_unit_t::seconds});
    CHECK_FALSE(incoherent);
    CHECK(has_code(incoherent, diagnostic_code_t::invalid_resolution));
    CHECK(history.document.revision == 5);
    CHECK(history.document.charts.front().pauses.front().kind == pause_kind_t::stop);

    const auto deleted = apply_pause_warp_edit(
        history, 5, delete_pause_event_action_t{chart_id_t{2}, object_id_t{40}});
    REQUIRE(deleted);
    CHECK(history.document.charts.front().pauses.empty());
    CHECK(history.document.charts.front().bms_pause_projections.empty());
}

TEST_CASE("pause and warp edits reject stale, duplicate, invalid, and over-cap requests atomically",
          "[unit][core][pause-warp-editor][negative]") {
    auto history = document_history_t::from(pause_warp_song());
    const auto initial_revision = history.document.revision;

    const auto stale = apply_pause_warp_edit(history, 3,
                                             insert_pause_event_action_t{chart_id_t{2},
                                                                         {measure_id_t{3}, 120},
                                                                         pause_kind_t::stop,
                                                                         {1, 1},
                                                                         pause_unit_t::seconds});
    CHECK_FALSE(stale);
    CHECK(has_code(stale, diagnostic_code_t::invalid_id));

    const auto unknown = apply_pause_warp_edit(
        history, initial_revision, delete_warp_event_action_t{chart_id_t{2}, object_id_t{99}});
    CHECK_FALSE(unknown);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));
    const auto zero = apply_pause_warp_edit(
        history, initial_revision, delete_pause_event_action_t{chart_id_t{2}, object_id_t{0}});
    CHECK_FALSE(zero);
    CHECK(has_code(zero, diagnostic_code_t::invalid_id));

    for (const auto position :
         {local_position_t{measure_id_t{99}, 0}, local_position_t{measure_id_t{3}, 960}}) {
        const auto invalid = apply_pause_warp_edit(
            history, initial_revision,
            insert_pause_event_action_t{
                chart_id_t{2}, position, pause_kind_t::stop, {1, 1}, pause_unit_t::seconds});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, diagnostic_code_t::row_out_of_range));
    }

    for (const auto duration : {signed_rational_t{-1, 1}, signed_rational_t{1, 0}}) {
        const auto invalid =
            apply_pause_warp_edit(history, initial_revision,
                                  insert_pause_event_action_t{chart_id_t{2},
                                                              {measure_id_t{3}, 120},
                                                              pause_kind_t::stop,
                                                              duration,
                                                              pause_unit_t::seconds});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, diagnostic_code_t::invalid_rational));
    }
    const auto invalid_unit =
        apply_pause_warp_edit(history, initial_revision,
                              insert_pause_event_action_t{chart_id_t{2},
                                                          {measure_id_t{3}, 120},
                                                          pause_kind_t::delay,
                                                          {1, 1},
                                                          pause_unit_t::bms_stop_ticks});
    CHECK_FALSE(invalid_unit);
    CHECK(has_code(invalid_unit, diagnostic_code_t::invalid_resolution));

    for (const auto span : {position_span_t{{measure_id_t{3}, 240}, {measure_id_t{3}, 240}},
                            position_span_t{{measure_id_t{3}, 480}, {measure_id_t{3}, 240}},
                            position_span_t{{measure_id_t{3}, 0}, {measure_id_t{99}, 0}}}) {
        const auto invalid = apply_pause_warp_edit(
            history, initial_revision,
            insert_warp_event_action_t{chart_id_t{2}, span, warp_provenance_t::native});
        CHECK_FALSE(invalid);
        CHECK(has_code(invalid, span.end.measure == measure_id_t{99}
                                    ? diagnostic_code_t::row_out_of_range
                                    : diagnostic_code_t::invalid_hold));
    }

    const auto pause_capped =
        apply_pause_warp_edit(history, initial_revision,
                              insert_pause_event_action_t{chart_id_t{2},
                                                          {measure_id_t{3}, 120},
                                                          pause_kind_t::stop,
                                                          {1, 1},
                                                          pause_unit_t::seconds},
                              {.maximum_pause_events = 2});
    CHECK_FALSE(pause_capped);
    CHECK(has_code(pause_capped, diagnostic_code_t::invalid_resolution));
    const auto warp_capped = apply_pause_warp_edit(
        history, initial_revision,
        insert_warp_event_action_t{chart_id_t{2},
                                   {{measure_id_t{3}, 0}, {measure_id_t{3}, 120}},
                                   warp_provenance_t::native},
        {.maximum_warp_events = 1});
    CHECK_FALSE(warp_capped);
    CHECK(has_code(warp_capped, diagnostic_code_t::invalid_resolution));
    const auto objects_capped =
        apply_pause_warp_edit(history, initial_revision,
                              insert_pause_event_action_t{chart_id_t{2},
                                                          {measure_id_t{3}, 120},
                                                          pause_kind_t::stop,
                                                          {1, 1},
                                                          pause_unit_t::seconds},
                              {.maximum_chart_objects = 4});
    CHECK_FALSE(objects_capped);
    CHECK(has_code(objects_capped, diagnostic_code_t::invalid_resolution));

    auto duplicate_song = pause_warp_song();
    duplicate_song.charts.front().pauses.push_back(
        {.id = object_id_t{20}, .position = {measure_id_t{3}, 120}, .duration = {1, 1}});
    auto duplicate_history = document_history_t::from(std::move(duplicate_song));
    const auto ambiguous =
        apply_pause_warp_edit(duplicate_history, initial_revision,
                              update_pause_event_action_t{chart_id_t{2},
                                                          object_id_t{20},
                                                          {measure_id_t{3}, 120},
                                                          pause_kind_t::stop,
                                                          {1, 1},
                                                          pause_unit_t::seconds});
    CHECK_FALSE(ambiguous);
    CHECK(has_code(ambiguous, diagnostic_code_t::duplicate_id));

    auto cross_type_song = pause_warp_song();
    cross_type_song.charts.front().bpms.front().id = object_id_t{20};
    auto cross_type_history = document_history_t::from(std::move(cross_type_song));
    const auto cross_type_ambiguous =
        apply_pause_warp_edit(cross_type_history, initial_revision,
                              delete_pause_event_action_t{chart_id_t{2}, object_id_t{20}});
    CHECK_FALSE(cross_type_ambiguous);
    CHECK(has_code(cross_type_ambiguous, diagnostic_code_t::duplicate_id));
    CHECK(cross_type_history.document.charts.front().pauses.size() == 2);
    CHECK(cross_type_history.undo.empty());

    CHECK(history.document.revision == initial_revision);
    CHECK(history.document.charts.front().pauses.size() == 2);
    CHECK(history.document.charts.front().warps.size() == 1);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
}
