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

TEST_CASE("existing timing lane events update in place and retain source identity and history",
          "[unit][core][timing-lane-controller][update]") {
    auto history = make_document_history(timing_edit_song());
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
            begin_existing_timing_lane_pseudonote(history, chart_id_t{2}, kind, event, false);
        REQUIRE(preview.status == timing_lane_pseudonote_status_t::editing);
        REQUIRE(preview.existing_event == event);
        CHECK(preview.position == position);
        CHECK(preview.document_revision == original_revision);
        CHECK(preview.value.index() == original.index());
        switch (kind) {
        case timing_lane_kind_t::bpm:
            CHECK(std::get<timing_bpm_value_t>(preview.value).beats_per_minute ==
                  std::get<timing_bpm_value_t>(original).beats_per_minute);
            break;
        case timing_lane_kind_t::scroll:
            CHECK(std::get<timing_scroll_value_t>(preview.value).multiplier ==
                  std::get<timing_scroll_value_t>(original).multiplier);
            break;
        case timing_lane_kind_t::speed: {
            const auto& actual = std::get<timing_speed_value_t>(preview.value);
            const auto& expected = std::get<timing_speed_value_t>(original);
            CHECK(actual.multiplier == expected.multiplier);
            CHECK(actual.transition_beats == expected.transition_beats);
            CHECK(actual.transition_is_seconds == expected.transition_is_seconds);
            break;
        }
        case timing_lane_kind_t::stop:
        case timing_lane_kind_t::delay: {
            const auto& actual = std::get<timing_pause_value_t>(preview.value);
            const auto& expected = std::get<timing_pause_value_t>(original);
            CHECK(actual.duration == expected.duration);
            CHECK(actual.source_unit == expected.source_unit);
            break;
        }
        case timing_lane_kind_t::warp: {
            const auto& actual = std::get<timing_warp_value_t>(preview.value);
            const auto& expected = std::get<timing_warp_value_t>(original);
            CHECK(actual.end == expected.end);
            CHECK(actual.source_encoding == expected.source_encoding);
            break;
        }
        case timing_lane_kind_t::kiai: {
            const auto& actual = std::get<timing_kiai_value_t>(preview.value);
            const auto& expected = std::get<timing_kiai_value_t>(original);
            CHECK(actual.enabled == expected.enabled);
            CHECK(actual.retained_effect_bits == expected.retained_effect_bits);
            break;
        }
        case timing_lane_kind_t::anchor: {
            const auto& actual = std::get<timing_anchor_value_t>(preview.value);
            const auto& expected = std::get<timing_anchor_value_t>(original);
            CHECK(actual.source_sample_frame == expected.source_sample_frame);
            CHECK(actual.locked == expected.locked);
            break;
        }
        }
        CHECK(history.document.revision == original_revision);
        CHECK(history.undo.size() == undo_count);
        REQUIRE(update_timing_lane_pseudonote(history, preview, changed, false));
        const auto result = confirm_timing_lane_pseudonote(history, preview, false);
        REQUIRE(result);
        CHECK(preview.status == timing_lane_pseudonote_status_t::committed);
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
    auto history = make_document_history(timing_edit_song());
    auto unknown = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{999}, false);
    CHECK(unknown.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));

    auto wrong_kind = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::scroll, object_id_t{10}, false);
    CHECK(wrong_kind.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(wrong_kind, diagnostic_code_t::invalid_resolution));

    auto wrong_pause_kind = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::stop, object_id_t{41}, false);
    CHECK(wrong_pause_kind.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(wrong_pause_kind, diagnostic_code_t::invalid_resolution));

    auto locked = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{10}, true);
    CHECK(locked.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(locked, diagnostic_code_t::unsupported_feature));

    auto stale = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{10}, false);
    REQUIRE(stale.status == timing_lane_pseudonote_status_t::editing);
    const auto intervening =
        apply_bpm_edit(history, history.document.revision,
                       update_bpm_event_action_t{chart_id_t{2}, object_id_t{11}, pos(0), {150, 1}});
    REQUIRE(intervening);
    const auto revision_after_intervening = history.document.revision;
    CHECK_FALSE(update_timing_lane_pseudonote(history, stale, timing_bpm_value_t{{90, 1}}, false));
    CHECK(stale.status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(has_code(stale, diagnostic_code_t::invalid_id));
    CHECK(history.document.revision == revision_after_intervening);
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{120, 1});

    auto stale_confirm = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::scroll, object_id_t{20}, false);
    REQUIRE(stale_confirm.status == timing_lane_pseudonote_status_t::editing);
    const auto another_change =
        apply_bpm_edit(history, history.document.revision,
                       update_bpm_event_action_t{chart_id_t{2}, object_id_t{11}, pos(0), {151, 1}});
    REQUIRE(another_change);
    const auto result = confirm_timing_lane_pseudonote(history, stale_confirm, false);
    CHECK_FALSE(result);
    CHECK(has_code(result, diagnostic_code_t::invalid_id));
    CHECK(stale_confirm.status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(history.document.charts.front().scrolls.front().multiplier == signed_rational_t{-2, 3});
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

TEST_CASE("existing timing pseudo-notes update every typed family in place",
          "[unit][core][timing-lane-controller][edit]") {
    auto history = make_document_history(timing_song());
    const auto insert = [&](timing_lane_kind_t kind, std::uint64_t row,
                            timing_lane_value_t value) -> object_id_t {
        auto preview = begin(history, kind, row, std::move(value));
        REQUIRE(preview.status == timing_lane_pseudonote_status_t::editing);
        const auto result = confirm_timing_lane_pseudonote(history, preview, false);
        REQUIRE(result);
        REQUIRE(preview.status == timing_lane_pseudonote_status_t::committed);
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
            begin_existing_timing_lane_pseudonote(history, chart_id_t{2}, kind, event, false);
        REQUIRE(preview.status == timing_lane_pseudonote_status_t::editing);
        REQUIRE(preview.existing_event == event);
        const auto revision = history.document.revision;
        const auto undo_count = history.undo.size();
        REQUIRE(update_timing_lane_pseudonote(history, preview, std::move(value), false));
        const auto result = confirm_timing_lane_pseudonote(history, preview, false);
        REQUIRE(result);
        CHECK(preview.status == timing_lane_pseudonote_status_t::committed);
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
    auto history = make_document_history(timing_song());

    const auto coincident =
        apply_bpm_edit(history, history.document.revision,
                       insert_bpm_event_action_t{chart_id_t{2}, pos(0), signed_rational_t{130, 1}});
    REQUIRE(coincident);
    auto own_row = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{10}, false);
    REQUIRE(own_row.status == timing_lane_pseudonote_status_t::editing);
    CHECK(own_row.position == pos(0));
    CHECK(std::get<timing_bpm_value_t>(own_row.value).beats_per_minute ==
          signed_rational_t{120, 1});

    const auto revision = history.document.revision;
    const auto undo_count = history.undo.size();
    const auto unchanged = confirm_timing_lane_pseudonote(history, own_row, false);
    CHECK_FALSE(unchanged);
    CHECK(unchanged.diagnostics.empty());
    CHECK(own_row.status == timing_lane_pseudonote_status_t::committed);
    CHECK(history.document.revision == revision);
    CHECK(history.undo.size() == undo_count);

    auto wrong_kind = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::scroll, object_id_t{10}, false);
    CHECK(wrong_kind.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(wrong_kind, diagnostic_code_t::invalid_resolution));

    auto unknown = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{999}, false);
    CHECK(unknown.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));

    auto locked = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{10}, true);
    CHECK(locked.status == timing_lane_pseudonote_status_t::rejected);
    CHECK(has_code(locked, diagnostic_code_t::unsupported_feature));

    auto stale = begin_existing_timing_lane_pseudonote(
        history, chart_id_t{2}, timing_lane_kind_t::bpm, object_id_t{10}, false);
    REQUIRE(stale.status == timing_lane_pseudonote_status_t::editing);
    const auto intervening =
        apply_bpm_edit(history, history.document.revision,
                       update_bpm_event_action_t{chart_id_t{2}, object_id_t{10}, pos(0), {140, 1}});
    REQUIRE(intervening);
    CHECK_FALSE(update_timing_lane_pseudonote(history, stale, timing_bpm_value_t{{150, 1}}, false));
    CHECK(stale.status == timing_lane_pseudonote_status_t::cancelled);
    CHECK(has_code(stale, diagnostic_code_t::invalid_id));
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{140, 1});
}
