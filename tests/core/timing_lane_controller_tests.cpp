#include "../support/stale_action_assertions.h"
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/scroll_speed_editor.h>
#include <wormedit/core/timing_lane_controller.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
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

auto timing_edit_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 20,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{3}}},
                    .bpms = {{.id = object_id_t{10},
                              .position = {measure_id_t{3}, 0},
                              .beats_per_minute = {120, 1},
                              .provenance = {.family = format_family_t::osu_mania,
                                             .profile = "osu-source",
                                             .source_field = "TimingPoint",
                                             .source_spelling = "120",
                                             .source_order = 3}},
                             {.id = object_id_t{11},
                              .position = {measure_id_t{3}, 0},
                              .beats_per_minute = {121, 1},
                              .provenance = {.source_order = 4}}},
                    .pauses = {{.id = object_id_t{40},
                                .position = {measure_id_t{3}, 240},
                                .kind = pause_kind_t::stop,
                                .duration = {1, 4},
                                .source_unit = pause_unit_t::seconds,
                                .provenance = {.family = format_family_t::stepmania,
                                               .profile = "ssc-source",
                                               .source_field = "STOPS",
                                               .source_spelling = "0.25",
                                               .source_order = 8}},
                               {.id = object_id_t{41},
                                .position = {measure_id_t{3}, 300},
                                .kind = pause_kind_t::delay,
                                .duration = {500, 1},
                                .source_unit = pause_unit_t::milliseconds,
                                .provenance = {.family = format_family_t::stepmania,
                                               .profile = "ssc-source",
                                               .source_field = "DELAYS",
                                               .source_spelling = "0.5",
                                               .source_order = 9}}},
                    .warps = {{.id = object_id_t{50},
                               .span = {{measure_id_t{3}, 360}, {measure_id_t{3}, 420}},
                               .source_encoding = warp_provenance_t::negative_stop,
                               .provenance = {.family = format_family_t::stepmania,
                                              .profile = "legacy-source",
                                              .source_field = "STOPS",
                                              .source_spelling = "-0.25",
                                              .source_order = 10}}},
                    .scrolls = {{.id = object_id_t{20},
                                 .position = {measure_id_t{3}, 120},
                                 .multiplier = {-2, 3},
                                 .provenance = {.family = format_family_t::stepmania,
                                                .profile = "ssc-source",
                                                .source_field = "SCROLLS",
                                                .source_spelling = "-0.666667",
                                                .source_order = 5}},
                                {.id = object_id_t{21},
                                 .position = {measure_id_t{3}, 120},
                                 .multiplier = {2, 1},
                                 .provenance = {.source_order = 6}}},
                    .speeds = {{.id = object_id_t{30},
                                .position = {measure_id_t{3}, 180},
                                .multiplier = {2, 1},
                                .transition_beats = {1, 2},
                                .transition_is_seconds = true,
                                .provenance = {.family = format_family_t::stepmania,
                                               .profile = "ssc-source",
                                               .source_field = "SPEEDS",
                                               .source_spelling = "2=0.5s",
                                               .source_order = 7}}},
                    .kiai = {{.id = object_id_t{60},
                              .position = {measure_id_t{3}, 480},
                              .enabled = false,
                              .retained_effect_bits = 0x80,
                              .provenance = {.family = format_family_t::osu_mania,
                                             .profile = "osu-source",
                                             .source_field = "Effect",
                                             .source_spelling = "0,0",
                                             .source_order = 11}}},
                    .timing_anchors = {{.id = object_id_t{70},
                                        .position = {measure_id_t{3}, 540},
                                        .source_sample_frame = 12'345,
                                        .locked = true}}}}};
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
    return timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_pseudonote_state_t{.kind = kind, .position = pos(row), .value = std::move(value), .initial_value_source = source}, false);
}

auto has_code(const timing_lane_pseudonote_outcome_t& preview, diagnostic_code_t code) -> bool {
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
    auto history = document_history_t::from(timing_song());
    const auto initial_revision = history.document.revision;
    const auto initial_undo_count = history.undo.size();

    auto bpm = begin(history, timing_lane_kind_t::bpm, 120, timing_bpm_value_t{{150, 1}},
                     timing_lane_initial_value_source_t::effective_at_position);
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(bpm)->status == timing_lane_pseudonote_status_t::editing);
    CHECK(bpm.value.has_value());
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(bpm)->position == pos(120));
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(bpm)->initial_value_source == timing_lane_initial_value_source_t::effective_at_position);
    CHECK(history.document.revision == initial_revision);
    CHECK(history.undo.size() == initial_undo_count);
    CHECK(history.document.charts.front().measures.front().notes.empty());

    REQUIRE((bpm = timing_lane_pseudonote_preview_t::revise(std::move(bpm), history, timing_bpm_value_t{{90, 1}}, false)).value.has_value());
    const auto inserted_bpm = apply_timing_lane_pseudonote(history, bpm, false);
    REQUIRE(inserted_bpm);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(bpm)->status == timing_lane_pseudonote_status_t::committed);
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
    auto history = document_history_t::from(timing_song());
    const auto insert = [&](timing_lane_kind_t kind, std::uint64_t row, timing_lane_value_t value) {
        auto preview = begin(history, kind, row, std::move(value));
        REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::editing);
        const auto undo_count = history.undo.size();
        const auto result = apply_timing_lane_pseudonote(history, preview, false);
        REQUIRE(result);
        CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::committed);
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

TEST_CASE("existing timing lane events update in place and retain source identity and history",
          "[unit][core][timing-lane-controller][update]") {
    auto history = document_history_t::from(timing_edit_song());
    const auto check_provenance = [](const source_provenance_t& actual, format_family_t family,
                                     std::string_view profile, std::string_view field,
                                     std::string_view spelling, std::size_t source_order) {
        CHECK(actual.family == family);
        CHECK(actual.profile == profile);
        CHECK(actual.source_field == field);
        CHECK(actual.source_spelling == spelling);
        CHECK(actual.source_order == source_order);
    };
    const auto edit = [&](timing_lane_kind_t kind, object_id_t event, local_position_t position,
                          timing_lane_value_t original, timing_lane_value_t changed,
                          auto&& check_document) {
        const auto undo_count = history.undo.size();
        const auto original_revision = history.document.revision;
        auto preview =
            timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{kind, event}, false);
        REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::editing);
        REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->existing_event == event);
        CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->position == position);
        CHECK(wormedit::timing_lane_pseudonote_proposal_t::captured_stamp(preview).document.revision == original_revision);
        CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->value.index() == original.index());
        switch (kind) {
        case timing_lane_kind_t::bpm:
            CHECK(std::get<timing_bpm_value_t>(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->value).beats_per_minute ==
                  std::get<timing_bpm_value_t>(original).beats_per_minute);
            break;
        case timing_lane_kind_t::scroll:
            CHECK(std::get<timing_scroll_value_t>(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->value).multiplier ==
                  std::get<timing_scroll_value_t>(original).multiplier);
            break;
        case timing_lane_kind_t::speed: {
            const auto& actual = std::get<timing_speed_value_t>(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->value);
            const auto& expected = std::get<timing_speed_value_t>(original);
            CHECK(actual.multiplier == expected.multiplier);
            CHECK(actual.transition_beats == expected.transition_beats);
            CHECK(actual.transition_is_seconds == expected.transition_is_seconds);
            break;
        }
        case timing_lane_kind_t::stop:
        case timing_lane_kind_t::delay: {
            const auto& actual = std::get<timing_pause_value_t>(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->value);
            const auto& expected = std::get<timing_pause_value_t>(original);
            CHECK(actual.duration == expected.duration);
            CHECK(actual.source_unit == expected.source_unit);
            break;
        }
        case timing_lane_kind_t::warp: {
            const auto& actual = std::get<timing_warp_value_t>(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->value);
            const auto& expected = std::get<timing_warp_value_t>(original);
            CHECK(actual.end == expected.end);
            CHECK(actual.source_encoding == expected.source_encoding);
            break;
        }
        case timing_lane_kind_t::kiai: {
            const auto& actual = std::get<timing_kiai_value_t>(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->value);
            const auto& expected = std::get<timing_kiai_value_t>(original);
            CHECK(actual.enabled == expected.enabled);
            CHECK(actual.retained_effect_bits == expected.retained_effect_bits);
            break;
        }
        case timing_lane_kind_t::anchor: {
            const auto& actual = std::get<timing_anchor_value_t>(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->value);
            const auto& expected = std::get<timing_anchor_value_t>(original);
            CHECK(actual.source_sample_frame == expected.source_sample_frame);
            CHECK(actual.locked == expected.locked);
            break;
        }
        }
        CHECK(history.document.revision == original_revision);
        CHECK(history.undo.size() == undo_count);
        REQUIRE((preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), history, changed, false)).value.has_value());
        const auto result = apply_timing_lane_pseudonote(history, preview, false);
        REQUIRE(result);
        CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::committed);
        CHECK(history.undo.size() == undo_count + 1);
        CHECK(history.document.revision == original_revision + 1);
        check_document(history.document.charts.front(), true);

        REQUIRE(undo_document_command(history));
        check_document(history.document.charts.front(), false);
        REQUIRE(redo_document_command(history));
        check_document(history.document.charts.front(), true);
    };

    edit(timing_lane_kind_t::bpm, object_id_t{10}, pos(0), timing_bpm_value_t{{120, 1}},
         timing_bpm_value_t{{90, 1}}, [&](const chart_document_t& chart, bool changed) {
             REQUIRE(chart.bpms.size() == 2);
             const auto& event = chart.bpms.front();
             CHECK(event.id == object_id_t{10});
             CHECK(event.position == pos(0));
             CHECK(event.beats_per_minute ==
                   (changed ? signed_rational_t{90, 1} : signed_rational_t{120, 1}));
             check_provenance(event.provenance, format_family_t::osu_mania, "osu-source",
                              "TimingPoint", "120", 3);
             CHECK(chart.bpms.back().id == object_id_t{11});
             CHECK(chart.bpms.back().beats_per_minute == signed_rational_t{121, 1});
         });
    edit(timing_lane_kind_t::scroll, object_id_t{20}, pos(120), timing_scroll_value_t{{-2, 3}},
         timing_scroll_value_t{{5, 2}}, [&](const chart_document_t& chart, bool changed) {
             REQUIRE(chart.scrolls.size() == 2);
             const auto& event = chart.scrolls.front();
             CHECK(event.id == object_id_t{20});
             CHECK(event.position == pos(120));
             CHECK(event.multiplier ==
                   (changed ? signed_rational_t{5, 2} : signed_rational_t{-2, 3}));
             check_provenance(event.provenance, format_family_t::stepmania, "ssc-source", "SCROLLS",
                              "-0.666667", 5);
             CHECK(chart.scrolls.back().id == object_id_t{21});
             CHECK(chart.scrolls.back().multiplier == signed_rational_t{2, 1});
         });
    edit(timing_lane_kind_t::speed, object_id_t{30}, pos(180),
         timing_speed_value_t{{2, 1}, {1, 2}, true}, timing_speed_value_t{{3, 2}, {2, 1}, false},
         [&](const chart_document_t& chart, bool changed) {
             REQUIRE(chart.speeds.size() == 1);
             const auto& event = chart.speeds.front();
             CHECK(event.id == object_id_t{30});
             CHECK(event.position == pos(180));
             CHECK(event.multiplier ==
                   (changed ? signed_rational_t{3, 2} : signed_rational_t{2, 1}));
             CHECK(event.transition_beats == (changed ? rational_t{2, 1} : rational_t{1, 2}));
             CHECK(event.transition_is_seconds == !changed);
             check_provenance(event.provenance, format_family_t::stepmania, "ssc-source", "SPEEDS",
                              "2=0.5s", 7);
         });
    edit(timing_lane_kind_t::stop, object_id_t{40}, pos(240),
         timing_pause_value_t{{1, 4}, pause_unit_t::seconds},
         timing_pause_value_t{{3, 4}, pause_unit_t::seconds},
         [&](const chart_document_t& chart, bool changed) {
             REQUIRE(chart.pauses.size() == 2);
             const auto& event = chart.pauses.front();
             CHECK(event.id == object_id_t{40});
             CHECK(event.position == pos(240));
             CHECK(event.kind == pause_kind_t::stop);
             CHECK(event.duration == (changed ? signed_rational_t{3, 4} : signed_rational_t{1, 4}));
             CHECK(event.source_unit == pause_unit_t::seconds);
             check_provenance(event.provenance, format_family_t::stepmania, "ssc-source", "STOPS",
                              "0.25", 8);
         });
    edit(timing_lane_kind_t::delay, object_id_t{41}, pos(300),
         timing_pause_value_t{{500, 1}, pause_unit_t::milliseconds},
         timing_pause_value_t{{750, 1}, pause_unit_t::milliseconds},
         [&](const chart_document_t& chart, bool changed) {
             REQUIRE(chart.pauses.size() == 2);
             const auto& event = chart.pauses.back();
             CHECK(event.id == object_id_t{41});
             CHECK(event.position == pos(300));
             CHECK(event.kind == pause_kind_t::delay);
             CHECK(event.duration ==
                   (changed ? signed_rational_t{750, 1} : signed_rational_t{500, 1}));
             CHECK(event.source_unit == pause_unit_t::milliseconds);
             check_provenance(event.provenance, format_family_t::stepmania, "ssc-source", "DELAYS",
                              "0.5", 9);
         });
    edit(timing_lane_kind_t::warp, object_id_t{50}, pos(360),
         timing_warp_value_t{pos(420), warp_provenance_t::negative_stop},
         timing_warp_value_t{pos(450), warp_provenance_t::native},
         [&](const chart_document_t& chart, bool changed) {
             REQUIRE(chart.warps.size() == 1);
             const auto& event = chart.warps.front();
             CHECK(event.id == object_id_t{50});
             CHECK(event.span == (position_span_t{pos(360), changed ? pos(450) : pos(420)}));
             CHECK(event.source_encoding == warp_provenance_t::negative_stop);
             check_provenance(event.provenance, format_family_t::stepmania, "legacy-source",
                              "STOPS", "-0.25", 10);
         });
    edit(timing_lane_kind_t::kiai, object_id_t{60}, pos(480), timing_kiai_value_t{false, 0x80},
         timing_kiai_value_t{true, 0}, [&](const chart_document_t& chart, bool changed) {
             REQUIRE(chart.kiai.size() == 1);
             const auto& event = chart.kiai.front();
             CHECK(event.id == object_id_t{60});
             CHECK(event.position == pos(480));
             CHECK(event.enabled == changed);
             CHECK(event.retained_effect_bits == 0x80);
             check_provenance(event.provenance, format_family_t::osu_mania, "osu-source", "Effect",
                              "0,0", 11);
         });
    edit(timing_lane_kind_t::anchor, object_id_t{70}, pos(540), timing_anchor_value_t{12'345, true},
         timing_anchor_value_t{54'321, false}, [&](const chart_document_t& chart, bool changed) {
             REQUIRE(chart.timing_anchors.size() == 1);
             const auto& event = chart.timing_anchors.front();
             CHECK(event.id == object_id_t{70});
             CHECK(event.position == pos(540));
             CHECK(event.source_sample_frame == (changed ? 54'321U : 12'345U));
             CHECK(event.locked == !changed);
         });
}

TEST_CASE("existing timing edit targets reject unknown, wrong-kind, locked, and stale requests",
          "[unit][core][timing-lane-controller][update][negative]") {
    auto history = document_history_t::from(timing_edit_song());
    auto unknown = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{999}}, false);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(unknown)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));

    auto wrong_kind = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::scroll, object_id_t{10}}, false);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(wrong_kind)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(wrong_kind, diagnostic_code_t::invalid_resolution));

    auto wrong_pause_kind = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::stop, object_id_t{41}}, false);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(wrong_pause_kind)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(wrong_pause_kind, diagnostic_code_t::invalid_resolution));

    auto locked = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, true);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(locked)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(locked, diagnostic_code_t::unsupported_feature));

    auto stale = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(stale)->status == timing_lane_pseudonote_status_t::editing);
    const auto intervening =
        apply_bpm_edit(history, wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                                    .chart = chart_id_t{2},
                                    .id = object_id_t{11},
                                    .value = wormedit::bpm_edit_value_t::from(
                                        pos(0), wormedit::timing_bpm_value_t{{150, 1}})});
    REQUIRE(intervening);
    const auto revision_after_intervening = history.document.revision;
    CHECK_FALSE((stale = timing_lane_pseudonote_preview_t::revise(std::move(stale), history, timing_bpm_value_t{{90, 1}}, false)).value.has_value());
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(stale)->status == timing_lane_pseudonote_status_t::cancelled);
    test::check_stale_action_message(stale.diagnostics, "Timing pseudo-note");
    CHECK(has_code(stale, diagnostic_code_t::invalid_id));
    CHECK(history.document.revision == revision_after_intervening);
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{120, 1});

    auto stale_confirm = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::scroll, object_id_t{20}}, false);
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(stale_confirm)->status == timing_lane_pseudonote_status_t::editing);
    const auto another_change =
        apply_bpm_edit(history, wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                                    .chart = chart_id_t{2},
                                    .id = object_id_t{11},
                                    .value = wormedit::bpm_edit_value_t::from(
                                        pos(0), wormedit::timing_bpm_value_t{{151, 1}})});
    REQUIRE(another_change);
    const auto result = apply_timing_lane_pseudonote(history, stale_confirm, false);
    CHECK_FALSE(result);
    CHECK(has_code(result, diagnostic_code_t::invalid_id));
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(stale_confirm)->status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(history.document.charts.front().scrolls.front().multiplier == signed_rational_t{-2, 3});
}

TEST_CASE("pseudo-note insertion rejects locked, occupied, and stale timing edits atomically",
          "[unit][core][timing-lane-controller][negative]") {
    auto history = document_history_t::from(timing_song());
    auto locked = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_pseudonote_state_t{.kind = timing_lane_kind_t::bpm, .position = pos(120), .value = timing_bpm_value_t{{100, 1}}, .initial_value_source = timing_lane_initial_value_source_t::explicit_default}, true);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(locked)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(locked, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.charts.front().bpms.size() == 1);

    auto occupied = begin(history, timing_lane_kind_t::bpm, 0, timing_bpm_value_t{{100, 1}});
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(occupied)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(occupied, diagnostic_code_t::collision));

    // Other timing lanes may coincide; occupancy is checked within the lane's
    // event family, matching the chart model's same-row ordering rules.
    auto different_lane =
        begin(history, timing_lane_kind_t::scroll, 0, timing_scroll_value_t{{2, 1}});
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(different_lane)->status == timing_lane_pseudonote_status_t::editing);
    cancel_timing_lane_pseudonote(different_lane);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(different_lane)->status == timing_lane_pseudonote_status_t::cancelled);

    auto stale = begin(history, timing_lane_kind_t::scroll, 120, timing_scroll_value_t{{2, 1}});
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(stale)->status == timing_lane_pseudonote_status_t::editing);
    const auto intervening =
        apply_bpm_edit(history, wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                                    .chart = chart_id_t{2},
                                    .value = wormedit::bpm_edit_value_t::from(
                                        pos(60), wormedit::timing_bpm_value_t{{150, 1}})});
    REQUIRE(intervening);
    const auto revision_after_intervening = history.document.revision;
    const auto result = apply_timing_lane_pseudonote(history, stale, false);
    CHECK_FALSE(result);
    CHECK(has_code(result, diagnostic_code_t::invalid_id));
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(stale)->status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(history.document.revision == revision_after_intervening);
    CHECK(history.document.charts.front().scrolls.empty());

    auto lock_during_edit =
        begin(history, timing_lane_kind_t::scroll, 180, timing_scroll_value_t{{2, 1}});
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(lock_during_edit)->status == timing_lane_pseudonote_status_t::editing);
    CHECK_FALSE((lock_during_edit = timing_lane_pseudonote_preview_t::revise(std::move(lock_during_edit), history, timing_scroll_value_t{{3, 1}}, true)).value.has_value());
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(lock_during_edit)->status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(has_code(lock_during_edit, diagnostic_code_t::unsupported_feature));
    CHECK(history.document.charts.front().scrolls.empty());
}

TEST_CASE("invalid value types, rationals, pause units, and warp spans cannot be confirmed",
          "[unit][core][timing-lane-controller][negative]") {
    auto history = document_history_t::from(timing_song());
    auto bpm = begin(history, timing_lane_kind_t::bpm, 120, timing_bpm_value_t{{100, 1}});
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(bpm)->status == timing_lane_pseudonote_status_t::editing);
    CHECK_FALSE((bpm = timing_lane_pseudonote_preview_t::revise(std::move(bpm), history, timing_bpm_value_t{{0, 1}}, false)).value.has_value());
    CHECK_FALSE(bpm.value.has_value());
    CHECK(has_code(bpm, diagnostic_code_t::invalid_rational));
    const auto invalid_bpm = apply_timing_lane_pseudonote(history, bpm, false);
    CHECK_FALSE(invalid_bpm);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(bpm)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(history.document.charts.front().bpms.size() == 1);

    auto mismatched =
        begin(history, timing_lane_kind_t::speed, 180, timing_speed_value_t{{1, 1}, {}, false});
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(mismatched)->status == timing_lane_pseudonote_status_t::editing);
    CHECK_FALSE(
        (mismatched = timing_lane_pseudonote_preview_t::revise(std::move(mismatched), history, timing_scroll_value_t{{1, 1}}, false)).value.has_value());
    CHECK(has_code(mismatched, diagnostic_code_t::invalid_resolution));
    CHECK_FALSE(apply_timing_lane_pseudonote(history, mismatched, false));

    auto bad_pause = begin(history, timing_lane_kind_t::delay, 240,
                           timing_pause_value_t{{1, 1}, pause_unit_t::bms_stop_ticks});
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(bad_pause)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(bad_pause, diagnostic_code_t::invalid_resolution));

    auto zero_warp =
        begin(history, timing_lane_kind_t::warp, 300, timing_warp_value_t{.end = pos(300)});
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(zero_warp)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(zero_warp, diagnostic_code_t::invalid_hold));

    auto outside_warp =
        begin(history, timing_lane_kind_t::warp, 360, timing_warp_value_t{.end = pos(960)});
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(outside_warp)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(outside_warp, diagnostic_code_t::row_out_of_range));
    CHECK(history.document.charts.front().pauses.empty());
    CHECK(history.document.charts.front().warps.empty());
    CHECK(history.document.charts.front().measures.front().notes.empty());
}

TEST_CASE("existing timing pseudo-notes update every typed family in place",
          "[unit][core][timing-lane-controller][edit]") {
    auto history = document_history_t::from(timing_song());
    const auto insert = [&](timing_lane_kind_t kind, std::uint64_t row,
                            timing_lane_value_t value) -> object_id_t {
        auto preview = begin(history, kind, row, std::move(value));
        REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::editing);
        const auto result = apply_timing_lane_pseudonote(history, preview, false);
        REQUIRE(result);
        REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::committed);
        const auto& chart = history.document.charts.front();
        switch (kind) {
        case timing_lane_kind_t::bpm:
            return chart.bpms.back().id;
        case timing_lane_kind_t::scroll:
            return chart.scrolls.back().id;
        case timing_lane_kind_t::speed:
            return chart.speeds.back().id;
        case timing_lane_kind_t::stop:
        case timing_lane_kind_t::delay:
            return chart.pauses.back().id;
        case timing_lane_kind_t::warp:
            return chart.warps.back().id;
        case timing_lane_kind_t::kiai:
            return chart.kiai.back().id;
        case timing_lane_kind_t::anchor:
            return chart.timing_anchors.back().id;
        }
        FAIL("unsupported timing lane kind");
        return {};
    };

    const auto scroll = insert(timing_lane_kind_t::scroll, 60, timing_scroll_value_t{{2, 3}});
    const auto speed =
        insert(timing_lane_kind_t::speed, 120, timing_speed_value_t{{3, 2}, {1, 2}, true});
    const auto stop = insert(timing_lane_kind_t::stop, 180,
                             timing_pause_value_t{{1, 4}, pause_unit_t::bms_stop_ticks});
    const auto delay = insert(timing_lane_kind_t::delay, 240,
                              timing_pause_value_t{{125, 1}, pause_unit_t::milliseconds});
    const auto warp = insert(
        timing_lane_kind_t::warp, 300,
        timing_warp_value_t{.end = pos(420), .source_encoding = warp_provenance_t::negative_bpm});
    const auto kiai = insert(timing_lane_kind_t::kiai, 480,
                             timing_kiai_value_t{.enabled = true, .retained_effect_bits = 0x40U});
    const auto anchor =
        insert(timing_lane_kind_t::anchor, 540,
               timing_anchor_value_t{.source_sample_frame = 48'000, .locked = true});

    const auto edit = [&](timing_lane_kind_t kind, object_id_t event, timing_lane_value_t value) {
        auto preview =
            timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{kind, event}, false);
        REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::editing);
        REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->existing_event == event);
        const auto revision = history.document.revision;
        const auto undo_count = history.undo.size();
        REQUIRE((preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), history, std::move(value), false)).value.has_value());
        const auto result = apply_timing_lane_pseudonote(history, preview, false);
        REQUIRE(result);
        CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::committed);
        CHECK(history.document.revision > revision);
        CHECK(history.undo.size() == undo_count + 1);
    };

    const auto bpm_provenance = history.document.charts.front().bpms.front().provenance;
    edit(timing_lane_kind_t::bpm, object_id_t{10}, timing_bpm_value_t{{175, 2}});
    edit(timing_lane_kind_t::scroll, scroll, timing_scroll_value_t{{-5, 4}});
    edit(timing_lane_kind_t::speed, speed, timing_speed_value_t{{7, 3}, {9, 4}, false});
    edit(timing_lane_kind_t::stop, stop,
         timing_pause_value_t{{3, 8}, pause_unit_t::bms_stop_ticks});
    edit(timing_lane_kind_t::delay, delay,
         timing_pause_value_t{{250, 1}, pause_unit_t::milliseconds});
    edit(timing_lane_kind_t::warp, warp,
         timing_warp_value_t{.end = pos(450), .source_encoding = warp_provenance_t::negative_bpm});
    edit(timing_lane_kind_t::kiai, kiai,
         timing_kiai_value_t{.enabled = false, .retained_effect_bits = 0x40U});
    edit(timing_lane_kind_t::anchor, anchor,
         timing_anchor_value_t{.source_sample_frame = 96'000, .locked = false});

    const auto& chart = history.document.charts.front();
    REQUIRE(chart.bpms.front().id == object_id_t{10});
    CHECK(chart.bpms.front().beats_per_minute == signed_rational_t{175, 2});
    CHECK(chart.bpms.front().provenance.family == bpm_provenance.family);
    CHECK(chart.bpms.front().provenance.profile == bpm_provenance.profile);
    CHECK(chart.bpms.front().provenance.source_field == bpm_provenance.source_field);
    CHECK(chart.bpms.front().provenance.source_spelling == bpm_provenance.source_spelling);
    CHECK(chart.bpms.front().provenance.source_order == bpm_provenance.source_order);
    REQUIRE(chart.scrolls.front().id == scroll);
    CHECK(chart.scrolls.front().multiplier == signed_rational_t{-5, 4});
    REQUIRE(chart.speeds.front().id == speed);
    CHECK(chart.speeds.front().transition_beats == rational_t{9, 4});
    CHECK_FALSE(chart.speeds.front().transition_is_seconds);
    REQUIRE(chart.pauses[0].id == stop);
    CHECK(chart.pauses[0].source_unit == pause_unit_t::bms_stop_ticks);
    REQUIRE(chart.pauses[1].id == delay);
    CHECK(chart.pauses[1].source_unit == pause_unit_t::milliseconds);
    REQUIRE(chart.warps.front().id == warp);
    CHECK(chart.warps.front().span.end == pos(450));
    CHECK(chart.warps.front().source_encoding == warp_provenance_t::negative_bpm);
    REQUIRE(chart.kiai.front().id == kiai);
    CHECK_FALSE(chart.kiai.front().enabled);
    CHECK(chart.kiai.front().retained_effect_bits == 0x40U);
    REQUIRE(chart.timing_anchors.front().id == anchor);
    CHECK(chart.timing_anchors.front().source_sample_frame == 96'000);
    CHECK_FALSE(chart.timing_anchors.front().locked);
    CHECK(chart.measures.front().notes.empty());

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().timing_anchors.front().locked);
}

TEST_CASE("existing timing pseudo-note targets are exact, revision-bound, and no-op safe",
          "[unit][core][timing-lane-controller][edit][negative]") {
    auto history = document_history_t::from(timing_song());

    const auto coincident = apply_bpm_edit(
        history, wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                     .chart = chart_id_t{2},
                     .value = wormedit::bpm_edit_value_t::from(
                         pos(0), wormedit::timing_bpm_value_t{signed_rational_t{130, 1}})});
    REQUIRE(coincident);
    auto own_row = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(own_row)->status == timing_lane_pseudonote_status_t::editing);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(own_row)->position == pos(0));
    CHECK(std::get<timing_bpm_value_t>(wormedit::timing_lane_pseudonote_proposal_t::report(own_row)->value).beats_per_minute ==
          signed_rational_t{120, 1});

    const auto revision = history.document.revision;
    const auto undo_count = history.undo.size();
    const auto unchanged = apply_timing_lane_pseudonote(history, own_row, false);
    CHECK_FALSE(unchanged);
    CHECK(unchanged.diagnostics.empty());
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(own_row)->status == timing_lane_pseudonote_status_t::committed);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.size() == undo_count);

    auto wrong_kind = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::scroll, object_id_t{10}}, false);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(wrong_kind)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(wrong_kind, diagnostic_code_t::invalid_resolution));

    auto unknown = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{999}}, false);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(unknown)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));

    auto locked = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, true);
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(locked)->status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(locked, diagnostic_code_t::unsupported_feature));

    auto stale = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(stale)->status == timing_lane_pseudonote_status_t::editing);
    const auto intervening =
        apply_bpm_edit(history, wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                                    .chart = chart_id_t{2},
                                    .id = object_id_t{10},
                                    .value = wormedit::bpm_edit_value_t::from(
                                        pos(0), wormedit::timing_bpm_value_t{{140, 1}})});
    REQUIRE(intervening);
    CHECK_FALSE((stale = timing_lane_pseudonote_preview_t::revise(std::move(stale), history, timing_bpm_value_t{{150, 1}}, false)).value.has_value());
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(stale)->status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(has_code(stale, diagnostic_code_t::invalid_id));
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{140, 1});
}

TEST_CASE("existing timing markers move as transient previews and delete through typed history",
          "[unit][core][timing-lane-controller][move][delete]") {
    auto history = document_history_t::from(timing_edit_song());
    const auto original_revision = history.document.revision;

    auto bpm = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(bpm)->status == timing_lane_pseudonote_status_t::editing);
    REQUIRE((bpm = timing_lane_pseudonote_preview_t::revise(std::move(bpm), history, pos(720), false)).value.has_value());
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(bpm)->position == pos(720));
    CHECK(history.document.revision == original_revision);
    const auto moved = apply_timing_lane_pseudonote(history, bpm, false);
    REQUIRE(moved);
    CHECK(history.document.charts.front().bpms.front().id == object_id_t{10});
    CHECK(history.document.charts.front().bpms.front().position == pos(720));
    CHECK(history.document.charts.front().bpms.front().provenance.source_order == 3);

    auto warp = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::warp, object_id_t{50}}, false);
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(warp)->status == timing_lane_pseudonote_status_t::editing);
    CHECK_FALSE((warp = timing_lane_pseudonote_preview_t::revise(std::move(warp), history, pos(420), false)).value.has_value());
    CHECK(has_code(warp, diagnostic_code_t::invalid_hold));
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(warp)->status == timing_lane_pseudonote_status_t::editing);

    const auto before_delete = history.document.revision;
    const auto deleted = delete_existing_timing_lane_event(
        history, chart_id_t{2}, timing_lane_kind_t::stop, object_id_t{40}, false);
    REQUIRE(deleted);
    CHECK(history.document.charts.front().pauses.size() == 1);
    CHECK(history.document.charts.front().pauses.front().id == object_id_t{41});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().pauses.size() == 2);

    const auto locked = delete_existing_timing_lane_event(
        history, chart_id_t{2}, timing_lane_kind_t::stop, object_id_t{40}, true);
    CHECK_FALSE(locked);
    CHECK(has_code(locked, diagnostic_code_t::unsupported_feature));
}

TEST_CASE("timing group deletion is atomic, undoable and keeps the initial BPM",
          "[unit][core][timing-lane-controller][delete]") {
    auto history = document_history_t::from(timing_edit_song());
    const auto revision = history.document.revision;
    const std::array events{timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{11}},
                            timing_lane_event_ref_t{timing_lane_kind_t::stop, object_id_t{40}},
                            timing_lane_event_ref_t{timing_lane_kind_t::delay, object_id_t{41}}};
    const auto locked = delete_timing_lane_event_group(history, chart_id_t{2}, events, true);
    CHECK_FALSE(locked);
    CHECK(history.document.revision == revision);

    const auto initial_only = std::array{events[0]};
    CHECK_FALSE(delete_timing_lane_event_group(history, chart_id_t{2}, initial_only, false));

    const auto bpm_count = history.document.charts.front().bpms.size();
    const auto deleted = delete_timing_lane_event_group(history, chart_id_t{2}, events, false);
    REQUIRE(deleted);
    CHECK(history.document.charts.front().pauses.empty());
    CHECK(history.document.charts.front().bpms.size() == bpm_count);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().pauses.size() == 2);
}

TEST_CASE("the effective initial BPM cannot leave chart row zero",
          "[unit][core][timing-lane-controller][initial-bpm]") {
    auto history = document_history_t::from(timing_song());
    auto initial = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    const auto revision = history.document.revision;
    CHECK_FALSE((initial = timing_lane_pseudonote_preview_t::revise(std::move(initial), history, pos(120), false)).value.has_value());
    CHECK(has_code(initial, diagnostic_code_t::unsupported_feature));
    CHECK((wormedit::timing_lane_pseudonote_proposal_t::report(initial)->position == pos(0) && history.document.revision == revision));
}

TEST_CASE("the effective initial BPM can change value but cannot be deleted",
          "[unit][core][timing-lane-controller][initial-bpm]") {
    auto history = document_history_t::from(timing_song());
    auto initial = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    REQUIRE((initial = timing_lane_pseudonote_preview_t::revise(std::move(initial), history, timing_bpm_value_t{{365, 3}}, false)).value.has_value());
    REQUIRE(apply_timing_lane_pseudonote(history, initial, false));
    const auto& bpm = history.document.charts.front().bpms.front();
    CHECK((bpm.position == pos(0) && bpm.beats_per_minute == signed_rational_t{365, 3}));

    const auto before_delete = history.document.revision;
    const auto deletion = delete_existing_timing_lane_event(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{10}, false);
    CHECK_FALSE(deletion);
    CHECK((has_code(deletion, diagnostic_code_t::unsupported_feature) &&
           history.document.revision == before_delete &&
           history.document.charts.front().bpms.size() == 1));
}

TEST_CASE("coincident ordinary BPMs remain movable and deletable",
          "[unit][core][timing-lane-controller][initial-bpm]") {
    auto history = document_history_t::from(timing_edit_song());
    auto ordinary = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    REQUIRE((ordinary = timing_lane_pseudonote_preview_t::revise(std::move(ordinary), history, pos(120), false)).value.has_value());
    REQUIRE(apply_timing_lane_pseudonote(history, ordinary, false));
    CHECK(history.document.charts.front().bpms.front().position == pos(120));

    const auto ordinary_delete = delete_existing_timing_lane_event(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{10}, false);
    REQUIRE(ordinary_delete);
    CHECK((history.document.charts.front().bpms.size() == 1 &&
           history.document.charts.front().bpms.front().id == object_id_t{11} &&
           history.document.charts.front().bpms.front().position == pos(0)));
}

TEST_CASE("initial BPM follows source order when storage order differs",
          "[unit][core][timing-lane-controller][initial-bpm]") {
    auto song = timing_edit_song();
    song.charts.front().bpms.front().provenance.source_order = 9;
    auto history = document_history_t::from(std::move(song));
    auto effective = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    CHECK_FALSE((effective = timing_lane_pseudonote_preview_t::revise(std::move(effective), history, pos(120), false)).value.has_value());
    CHECK(has_code(effective, diagnostic_code_t::unsupported_feature));

    auto ordinary = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{11}}, false);
    REQUIRE((ordinary = timing_lane_pseudonote_preview_t::revise(std::move(ordinary), history, pos(120), false)).value.has_value());
    REQUIRE(apply_timing_lane_pseudonote(history, ordinary, false));
    CHECK((history.document.charts.front().bpms[0].position == pos(0) &&
           history.document.charts.front().bpms[1].position == pos(120)));
    const auto deletion = delete_existing_timing_lane_event(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{10}, false);
    CHECK_FALSE(deletion);
    CHECK(has_code(deletion, diagnostic_code_t::unsupported_feature));
}

TEST_CASE("mixed timing markers move together in one undoable command",
          "[unit][core][timing-lane-controller][group-move]") {
    auto history = document_history_t::from(timing_edit_song());
    const std::array events{timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}},
                            timing_lane_event_ref_t{timing_lane_kind_t::scroll, object_id_t{20}},
                            timing_lane_event_ref_t{timing_lane_kind_t::stop, object_id_t{40}}};
    const auto revision = history.document.revision;
    const auto undo_count = history.undo.size();
    const auto result = move_timing_lane_event_group(history, chart_id_t{2}, events, 120, false);
    REQUIRE(result);
    CHECK(history.undo.size() == undo_count + 1);
    const auto& chart = history.document.charts.front();
    CHECK((chart.bpms[0].position == pos(120) && chart.bpms[1].position == pos(0) &&
           chart.scrolls[0].position == pos(240) && chart.pauses[0].position == pos(360)));

    REQUIRE(undo_document_command(history));
    const auto& restored = history.document.charts.front();
    CHECK((restored.bpms[0].position == pos(0) &&
           restored.scrolls[0].position == pos(120) &&
           restored.pauses[0].position == pos(240)));
}

TEST_CASE("timing group row deltas cross measure boundaries exactly",
          "[unit][core][timing-lane-controller][group-move]") {
    auto history = document_history_t::from(timing_edit_song());
    history.document.charts.front().measures.push_back({.id = measure_id_t{4}});
    const std::array events{timing_lane_event_ref_t{timing_lane_kind_t::scroll,
                                                    object_id_t{20}}};
    const auto moved = move_timing_lane_event_group(
        history, chart_id_t{2}, events, 960, false);
    REQUIRE(moved);
    CHECK(history.document.charts.front().scrolls.front().position ==
          local_position_t{measure_id_t{4}, 120});
}

TEST_CASE("group moving a warp preserves its length in both directions",
          "[unit][core][timing-lane-controller][group-move]") {
    auto history = document_history_t::from(timing_edit_song());
    const std::array events{timing_lane_event_ref_t{timing_lane_kind_t::warp,
                                                    object_id_t{50}}};
    REQUIRE(move_timing_lane_event_group(history, chart_id_t{2}, events, 120, false));
    CHECK((history.document.charts.front().warps.front().span.begin == pos(480) &&
           history.document.charts.front().warps.front().span.end == pos(540)));
    REQUIRE(move_timing_lane_event_group(history, chart_id_t{2}, events, -240, false));
    CHECK((history.document.charts.front().warps.front().span.begin == pos(240) &&
           history.document.charts.front().warps.front().span.end == pos(300)));
}

TEST_CASE("warp group move rejects an end outside the chart atomically",
          "[unit][core][timing-lane-controller][group-move][negative]") {
    auto history = document_history_t::from(timing_edit_song());
    const std::array events{timing_lane_event_ref_t{timing_lane_kind_t::warp,
                                                    object_id_t{50}}};
    const auto revision = history.document.revision;
    const auto result = move_timing_lane_event_group(history, chart_id_t{2}, events, 540, false);
    CHECK_FALSE(result);
    CHECK(has_code(result, diagnostic_code_t::row_out_of_range));
    CHECK((history.document.revision == revision && history.undo.empty() &&
           history.document.charts.front().warps.front().span.begin == pos(360) &&
           history.document.charts.front().warps.front().span.end == pos(420)));
}

TEST_CASE("invalid timing group moves leave every event and history unchanged",
          "[unit][core][timing-lane-controller][group-move][negative]") {
    auto history = document_history_t::from(timing_edit_song());
    const auto revision = history.document.revision;
    const std::array fixed{timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{11}},
                           timing_lane_event_ref_t{timing_lane_kind_t::scroll, object_id_t{20}}};
    const auto fixed_result = move_timing_lane_event_group(history, chart_id_t{2}, fixed, 120, false);
    CHECK_FALSE(fixed_result);
    CHECK(has_code(fixed_result, diagnostic_code_t::unsupported_feature));

    const std::array ordinary{timing_lane_event_ref_t{timing_lane_kind_t::scroll,
                                                        object_id_t{20}}};
    const auto outside = move_timing_lane_event_group(history, chart_id_t{2}, ordinary, -121, false);
    CHECK_FALSE(outside);
    CHECK(has_code(outside, diagnostic_code_t::row_out_of_range));
    CHECK((history.document.revision == revision && history.undo.empty()));
}

TEST_CASE("timing group collisions are atomic",
          "[unit][core][timing-lane-controller][group-move][negative]") {
    auto history = document_history_t::from(timing_edit_song());
    const auto revision = history.document.revision;
    const std::array ordinary{timing_lane_event_ref_t{timing_lane_kind_t::scroll,
                                                        object_id_t{20}}};

    history.document.charts.front().scrolls.push_back(
        {.id = object_id_t{80}, .position = pos(360), .multiplier = {1, 1}});
    const auto collision = move_timing_lane_event_group(history, chart_id_t{2}, ordinary, 240, false);
    CHECK_FALSE(collision);
    CHECK(has_code(collision, diagnostic_code_t::collision));

    CHECK((history.document.revision == revision && history.undo.empty() &&
           history.document.charts.front().scrolls[0].position == pos(120)));
}

TEST_CASE("timing group moves honor the timing lock",
          "[unit][core][timing-lane-controller][group-move][negative]") {
    auto history = document_history_t::from(timing_edit_song());
    const auto revision = history.document.revision;
    const std::array ordinary{timing_lane_event_ref_t{timing_lane_kind_t::scroll,
                                                        object_id_t{20}}};
    const auto locked = move_timing_lane_event_group(history, chart_id_t{2}, ordinary, 120, true);
    CHECK_FALSE(locked);
    CHECK(has_code(locked, diagnostic_code_t::unsupported_feature));
    CHECK((history.document.revision == revision && history.undo.empty() &&
           history.document.charts.front().scrolls[0].position == pos(120) &&
           history.document.charts.front().bpms[1].position == pos(0)));
}

TEST_CASE("effective BPM at a position is the latest preceding BPM event", "[timing-lane]") {
    auto song = timing_song();
    auto& chart = song.charts.front();
    chart.measures.push_back({.id = measure_id_t{4}});
    chart.bpms.push_back({.id = object_id_t{11},
                          .position = {measure_id_t{3}, 480},
                          .beats_per_minute = {150, 1},
                          .provenance = {.source_order = 1}});
    REQUIRE(validate_document(song).empty());
    CHECK(effective_bpm_at(chart, {measure_id_t{3}, 0}) == signed_rational_t{120, 1});
    CHECK(effective_bpm_at(chart, {measure_id_t{3}, 479}) == signed_rational_t{120, 1});
    CHECK(effective_bpm_at(chart, {measure_id_t{3}, 480}) == signed_rational_t{150, 1});
    CHECK(effective_bpm_at(chart, {measure_id_t{4}, 0}) == signed_rational_t{150, 1});
}

TEST_CASE("quick insert copies the BPM in force without changing the effective tempo",
          "[unit][core][timing-lane-controller]") {
    auto history = document_history_t::from(timing_song());
    const auto position = pos(480);
    const auto inherited = effective_bpm_at(history.document.charts.front(), position);
    REQUIRE(inherited.has_value());
    auto preview = begin(history, timing_lane_kind_t::bpm, 480,
                         timing_bpm_value_t{*inherited},
                         timing_lane_initial_value_source_t::effective_at_position);
    REQUIRE(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::editing);
    REQUIRE(apply_timing_lane_pseudonote(history, preview, false));
    const auto& bpms = history.document.charts.front().bpms;
    REQUIRE(bpms.size() == 2);
    CHECK(bpms.back().position == position);
    CHECK(bpms.back().beats_per_minute == signed_rational_t{120, 1});
}

TEST_CASE("a speed span edit moves the begin row and sets the transition in one command",
          "[unit][core][timing-lane][speed]") {
    auto history = document_history_t::from(timing_edit_song());
    const auto speed = history.document.charts.front().speeds.front().id;
    const auto undo_count = history.undo.size();

    const auto edited = edit_speed_event_span(
        history, chart_id_t{2}, speed, {.position = pos(300), .transition = rational_t{3, 1}},
        false);
    REQUIRE(edited);
    CHECK(history.undo.size() == undo_count + 1);
    const auto& event = history.document.charts.front().speeds.front();
    CHECK(event.id == speed);
    CHECK(event.position == pos(300));
    CHECK(event.transition_beats == rational_t{3, 1});
    CHECK(event.multiplier == signed_rational_t{2, 1});

    const auto length_only =
        edit_speed_event_span(history, chart_id_t{2}, speed, {.transition = rational_t{0, 1}}, false);
    REQUIRE(length_only);
    CHECK(history.document.charts.front().speeds.front().position == pos(300));
    CHECK(history.document.charts.front().speeds.front().transition_beats == rational_t{0, 1});

    CHECK_FALSE(edit_speed_event_span(history, chart_id_t{2}, speed, {.transition = rational_t{1, 1}},
                                      true));
    CHECK_FALSE(edit_speed_event_span(history, chart_id_t{2}, object_id_t{9999},
                                      {.transition = rational_t{1, 1}}, false));
}

TEST_CASE("Timing proposal parameter rejection remains editable and can recover",
          "[unit][core][timing-lane-controller][proposal-stamp]") {
    auto history = document_history_t::from(timing_song());
    auto preview = begin(history, timing_lane_kind_t::scroll, 120, timing_scroll_value_t{{2, 1}});
    CHECK(preview.value.has_value());
    CHECK_FALSE((preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), history, timing_scroll_value_t{{2, 0}}, false)).value.has_value());
    CHECK_FALSE(preview.value.has_value());
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::editing);
    REQUIRE((preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), history, timing_scroll_value_t{{3, 1}}, false)).value.has_value());
    CHECK(preview.value.has_value());
}

TEST_CASE("Unchanged timing Apply stays committed and valid without an Undo record",
          "[unit][core][timing-lane-controller][proposal-stamp]") {
    auto history = document_history_t::from(timing_song());
    auto preview = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_event_ref_t{timing_lane_kind_t::bpm, object_id_t{10}}, false);
    const auto result = apply_timing_lane_pseudonote(history, preview, false);
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics.empty());
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::committed);
    CHECK(preview.value.has_value());
    CHECK(history.undo.empty());
}

TEST_CASE("Timing stamps preserve equal-revision project replacement on Apply",
          "[unit][core][timing-lane-controller][proposal-stamp]") {
    auto history = document_history_t::from(timing_song());
    auto preview = begin(history, timing_lane_kind_t::scroll, 120, timing_scroll_value_t{{2, 1}});
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::captured_stamp(preview) == chart_stamp_t::from(history.document, chart_id_t{2}));
    history.document.id = song_id_t{99};
    REQUIRE((preview = timing_lane_pseudonote_preview_t::revise(std::move(preview), history, timing_scroll_value_t{{3, 1}}, false)).value.has_value());
    CHECK(apply_timing_lane_pseudonote(history, preview, false).applied);
    CHECK(preview.value.has_value());
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Timing Apply keeps captured-stale rejection before timing lock",
          "[unit][core][timing-lane-controller][proposal-stamp]") {
    auto history = document_history_t::from(timing_song());
    auto preview = begin(history, timing_lane_kind_t::scroll, 120, timing_scroll_value_t{{2, 1}});
    ++history.document.revision;
    const auto result = apply_timing_lane_pseudonote(history, preview, true);
    REQUIRE(result.diagnostics.size() == 1);
    CHECK(result.diagnostics.front().code == diagnostic_code_t::invalid_id);
    test::check_stale_action_message(result.diagnostics, "Timing pseudo-note");
    CHECK(wormedit::timing_lane_pseudonote_proposal_t::report(preview)->status == timing_lane_pseudonote_status_t::cancelled);
    CHECK_FALSE(preview.value.has_value());
    CHECK(history.undo.empty());
}

TEST_CASE("A rejected typed timing transaction cannot publish its abandoned snapshot later",
          "[unit][core][timing-lane-controller][proposal-stamp]") {
    auto history = document_history_t::from(timing_song());
    const auto stamp = chart_stamp_t::from(history.document, chart_id_t{2});
    auto transaction = document_command_transaction_t::from(history, stamp, "Timing proposal",
        document_identity_policy_t::revision_only);
    transaction.copy_document().metadata.title = "Unpublished";
    const wormedit::insert_edit_t<wormedit::scroll_edit_value_t> edit{
        .chart = chart_id_t{2},
        .value = {.position = pos(120), .settings = {.multiplier = {2, 1}}}};
    const auto failed = apply_scroll_speed_edit(transaction, edit, {.maximum_scroll_events = 0});
    CHECK_FALSE(failed.applied);
    REQUIRE(failed.diagnostics.size() == 1);
    CHECK_FALSE(transaction.is_ready());
    const auto retried = apply_scroll_speed_edit(transaction, edit);
    CHECK_FALSE(retried.applied);
    CHECK(history.document.metadata.title.empty());
    CHECK(history.undo.empty());
}

TEST_CASE("BPM command and controller insertion retain their separate default contracts",
          "[unit][core][timing-lane-controller][edit-value-defaults]") {
    auto history = document_history_t::from(timing_song());
    const auto rejected =
        apply_bpm_edit(history, wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                                    .chart = chart_id_t{2},
                                    .value = wormedit::bpm_edit_value_t{.position = pos(240)}});
    REQUIRE_FALSE(rejected.applied);
    REQUIRE(!rejected.diagnostics.empty());
    CHECK(rejected.diagnostics.front().code == diagnostic_code_t::invalid_rational);
    CHECK(history.undo.empty());
    auto preview = timing_lane_pseudonote_preview_t::propose(history, chart_id_t{2}, wormedit::timing_lane_pseudonote_state_t{.kind = timing_lane_kind_t::bpm, .position = pos(240), .value = timing_bpm_value_t{}, .initial_value_source = timing_lane_initial_value_source_t::explicit_default}, false);
    REQUIRE(preview.value.has_value());
    REQUIRE(apply_timing_lane_pseudonote(history, preview, false).applied);
    CHECK(history.document.charts.front().bpms.back().beats_per_minute ==
          signed_rational_t{120, 1});
    CHECK(history.undo.size() == 1);
}

TEST_CASE("Rejected timing proposal retains editor data without an applicable value",
          "[unit][core][timing-lane-controller][proposal-shape]") {
    auto history = document_history_t::from(timing_song());
    auto outcome = begin(history, timing_lane_kind_t::scroll, 120, timing_scroll_value_t{{2, 1}});
    const auto stamp = outcome.value->stamp;
    outcome = timing_lane_pseudonote_preview_t::revise(
        std::move(outcome), history, timing_scroll_value_t{{2, 0}}, false);
    CHECK_FALSE(outcome.value);
    const auto view = timing_lane_pseudonote_view_t::from(outcome);
    REQUIRE(view.report);
    CHECK(*view.stamp == stamp);
    CHECK(std::get<timing_scroll_value_t>(view.report->value).multiplier == signed_rational_t{2, 0});
    CHECK(view.report->status == timing_lane_pseudonote_status_t::editing);
}
