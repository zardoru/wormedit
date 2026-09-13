#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/timing_lane_controller.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>
#include <utility>

using namespace wormedit;

namespace {

auto timing_song() -> song_document_t {
    song_document_t song{.id = song_id_t{1},
                         .revision = 4,
                         .charts = {{.id = chart_id_t{2},
                                     .divisions_per_standard_measure = 960,
                                     .measures = {{.id = measure_id_t{3}}},
                                     .bpms = {{.id = object_id_t{10},
                                               .position = {measure_id_t{3}, 0},
                                               .beats_per_minute = {120, 1},
                                               .provenance = {.source_order = 0}}}}}};
    REQUIRE(validate_document(song).empty());
    return song;
}

auto pos(std::uint64_t row) -> local_position_t {
    return {measure_id_t{3}, row};
}

auto begin(document_history_t& history, timing_lane_kind_t kind, std::uint64_t row,
           timing_lane_value_t value,
           timing_lane_initial_value_source_t source =
               timing_lane_initial_value_source_t::explicit_default) {
    return begin_timing_lane_pseudonote(history, chart_id_t{2}, kind, pos(row), std::move(value),
                                        source, false);
}

auto has_code(const timing_lane_pseudonote_preview_t& preview, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(preview.diagnostics,
                               [code](const auto& item) { return item.code == code; });
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& item) { return item.code == code; });
}

} // namespace

TEST_CASE("timing pseudo-notes preview typed values and commit through one shared history command",
          "[unit][core][timing-lane-controller]") {
    auto history = make_document_history(timing_song());
    const auto initial_revision = history.document.revision;
    const auto initial_undo_count = history.undo.size();

    auto bpm = begin(history, timing_lane_kind_t::bpm, 120, timing_bpm_value_t{{150, 1}},
                     timing_lane_initial_value_source_t::effective_at_position);
    REQUIRE(bpm.status == timing_lane_pseudonote_status_t::editing);
    CHECK(bpm.valid);
    CHECK(bpm.position == pos(120));
    CHECK(bpm.initial_value_source == timing_lane_initial_value_source_t::effective_at_position);
    CHECK(history.document.revision == initial_revision);
    CHECK(history.undo.size() == initial_undo_count);
    CHECK(history.document.charts.front().measures.front().notes.empty());

    REQUIRE(update_timing_lane_pseudonote(history, bpm, timing_bpm_value_t{{90, 1}}, false));
    const auto inserted_bpm = confirm_timing_lane_pseudonote(history, bpm, false);
    REQUIRE(inserted_bpm);
    CHECK(bpm.status == timing_lane_pseudonote_status_t::committed);
    REQUIRE(history.document.charts.front().bpms.size() == 2);
    CHECK(history.document.charts.front().bpms.back().position == pos(120));
    CHECK(history.document.charts.front().bpms.back().beats_per_minute == signed_rational_t{90, 1});
    CHECK(history.document.charts.front().measures.front().notes.empty());
    CHECK(history.undo.size() == initial_undo_count + 1);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bpms.size() == 1);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().bpms.size() == 2);
    CHECK(history.document.charts.front().bpms.back().id == object_id_t{11});
}

TEST_CASE("each timing lane commits its own typed event without creating playable notes",
          "[unit][core][timing-lane-controller]") {
    auto history = make_document_history(timing_song());
    const auto insert = [&](timing_lane_kind_t kind, std::uint64_t row, timing_lane_value_t value) {
        auto preview = begin(history, kind, row, std::move(value));
        REQUIRE(preview.status == timing_lane_pseudonote_status_t::editing);
        const auto undo_count = history.undo.size();
        const auto result = confirm_timing_lane_pseudonote(history, preview, false);
        REQUIRE(result);
        CHECK(preview.status == timing_lane_pseudonote_status_t::committed);
        CHECK(history.undo.size() == undo_count + 1);
        CHECK(history.document.charts.front().measures.front().notes.empty());
    };

    insert(timing_lane_kind_t::scroll, 120, timing_scroll_value_t{{-2, 3}});
    insert(timing_lane_kind_t::speed, 180, timing_speed_value_t{{3, 2}, {5, 4}, true});
    insert(timing_lane_kind_t::stop, 240, timing_pause_value_t{{1, 4}, pause_unit_t::seconds});
    insert(timing_lane_kind_t::delay, 300,
           timing_pause_value_t{{125, 1}, pause_unit_t::milliseconds});
    insert(timing_lane_kind_t::warp, 360,
           timing_warp_value_t{.end = pos(420), .source_encoding = warp_provenance_t::native});
    insert(timing_lane_kind_t::kiai, 480, timing_kiai_value_t{.enabled = false});
    insert(timing_lane_kind_t::anchor, 540,
           timing_anchor_value_t{.source_sample_frame = 48'000, .locked = true});

    const auto& chart = history.document.charts.front();
    REQUIRE(chart.scrolls.size() == 1);
    CHECK(chart.scrolls.front().position == pos(120));
    CHECK(chart.scrolls.front().multiplier == signed_rational_t{-2, 3});
    REQUIRE(chart.speeds.size() == 1);
    CHECK(chart.speeds.front().multiplier == signed_rational_t{3, 2});
    CHECK(chart.speeds.front().transition_beats == rational_t{5, 4});
    CHECK(chart.speeds.front().transition_is_seconds);
    REQUIRE(chart.pauses.size() == 2);
    CHECK(chart.pauses[0].kind == pause_kind_t::stop);
    CHECK(chart.pauses[1].kind == pause_kind_t::delay);
    CHECK(chart.pauses[1].source_unit == pause_unit_t::milliseconds);
    REQUIRE(chart.warps.size() == 1);
    CHECK(chart.warps.front().span.begin == pos(360));
    CHECK(chart.warps.front().span.end == pos(420));
    REQUIRE(chart.kiai.size() == 1);
    CHECK_FALSE(chart.kiai.front().enabled);
    REQUIRE(chart.timing_anchors.size() == 1);
    CHECK(chart.timing_anchors.front().source_sample_frame == 48'000);
    CHECK(chart.timing_anchors.front().locked);
}

TEST_CASE("pseudo-note insertion rejects locked, occupied, and stale timing edits atomically",
          "[unit][core][timing-lane-controller][negative]") {
    auto history = make_document_history(timing_song());
    auto locked = begin_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, pos(120), timing_bpm_value_t{{100, 1}},
        timing_lane_initial_value_source_t::explicit_default, true);
    CHECK(locked.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(locked, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.charts.front().bpms.size() == 1);

    auto occupied = begin(history, timing_lane_kind_t::bpm, 0, timing_bpm_value_t{{100, 1}});
    CHECK(occupied.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(occupied, diagnostic_code_t::collision));

    // Other timing lanes may coincide; occupancy is checked within the lane's
    // event family, matching the chart model's same-row ordering rules.
    auto different_lane =
        begin(history, timing_lane_kind_t::scroll, 0, timing_scroll_value_t{{2, 1}});
    REQUIRE(different_lane.status == timing_lane_pseudonote_status_t::editing);
    cancel_timing_lane_pseudonote(different_lane);
    CHECK(different_lane.status == timing_lane_pseudonote_status_t::cancelled);

    auto stale = begin(history, timing_lane_kind_t::scroll, 120, timing_scroll_value_t{{2, 1}});
    REQUIRE(stale.status == timing_lane_pseudonote_status_t::editing);
    const auto intervening =
        apply_bpm_edit(history, history.document.revision,
                       insert_bpm_event_action_t{chart_id_t{2}, pos(60), {150, 1}});
    REQUIRE(intervening);
    const auto revision_after_intervening = history.document.revision;
    const auto result = confirm_timing_lane_pseudonote(history, stale, false);
    CHECK_FALSE(result);
    CHECK(has_code(result, diagnostic_code_t::invalid_id));
    CHECK(stale.status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(history.document.revision == revision_after_intervening);
    CHECK(history.document.charts.front().scrolls.empty());

    auto lock_during_edit =
        begin(history, timing_lane_kind_t::scroll, 180, timing_scroll_value_t{{2, 1}});
    REQUIRE(lock_during_edit.status == timing_lane_pseudonote_status_t::editing);
    CHECK_FALSE(update_timing_lane_pseudonote(history, lock_during_edit,
                                              timing_scroll_value_t{{3, 1}}, true));
    CHECK(lock_during_edit.status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(has_code(lock_during_edit, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.charts.front().scrolls.empty());
}

TEST_CASE("invalid value types, rationals, pause units, and warp spans cannot be confirmed",
          "[unit][core][timing-lane-controller][negative]") {
    auto history = make_document_history(timing_song());
    auto bpm = begin(history, timing_lane_kind_t::bpm, 120, timing_bpm_value_t{{100, 1}});
    REQUIRE(bpm.status == timing_lane_pseudonote_status_t::editing);
    CHECK_FALSE(update_timing_lane_pseudonote(history, bpm, timing_bpm_value_t{{0, 1}}, false));
    CHECK_FALSE(bpm.valid);
    CHECK(has_code(bpm, diagnostic_code_t::invalid_rational));
    const auto invalid_bpm = confirm_timing_lane_pseudonote(history, bpm, false);
    CHECK_FALSE(invalid_bpm);
    CHECK(bpm.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(history.document.charts.front().bpms.size() == 1);

    auto mismatched =
        begin(history, timing_lane_kind_t::speed, 180, timing_speed_value_t{{1, 1}, {}, false});
    REQUIRE(mismatched.status == timing_lane_pseudonote_status_t::editing);
    CHECK_FALSE(
        update_timing_lane_pseudonote(history, mismatched, timing_scroll_value_t{{1, 1}}, false));
    CHECK(has_code(mismatched, diagnostic_code_t::invalid_resolution));
    CHECK_FALSE(confirm_timing_lane_pseudonote(history, mismatched, false));

    auto bad_pause = begin(history, timing_lane_kind_t::delay, 240,
                           timing_pause_value_t{{1, 1}, pause_unit_t::bms_stop_ticks});
    CHECK(bad_pause.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(bad_pause, diagnostic_code_t::invalid_resolution));

    auto zero_warp =
        begin(history, timing_lane_kind_t::warp, 300, timing_warp_value_t{.end = pos(300)});
    CHECK(zero_warp.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(zero_warp, diagnostic_code_t::invalid_hold));

    auto outside_warp =
        begin(history, timing_lane_kind_t::warp, 360, timing_warp_value_t{.end = pos(960)});
    CHECK(outside_warp.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(outside_warp, diagnostic_code_t::row_out_of_range));
    CHECK(history.document.charts.front().pauses.empty());
    CHECK(history.document.charts.front().warps.empty());
    CHECK(history.document.charts.front().measures.front().notes.empty());
}
