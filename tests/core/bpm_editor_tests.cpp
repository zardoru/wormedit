#include "../support/stale_action_assertions.h"
#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <limits>
#include <ranges>

using namespace wormedit;

namespace { void check_music_extent(bool value) { CHECK(value); } }

TEST_CASE("BPM edits extend native music charts through the audio end atomically",
          "[core][bpm-editor][audio-extent]") {
    song_document_t song{.id = song_id_t{1}, .revision = 1,
        .audio_assets = {{.id = asset_id_t{1}, .source_path = "/tmp/track.mp3",
                           .stream = {44'100, 2, 44'100 * 13, true}}},
        .resources = {{.id = resource_id_t{1}, .asset = asset_id_t{1},
                        .roles = {resource_role_t::music}}},
        .charts = {{.id = chart_id_t{2}, .source_family = format_family_t::native_project,
                    .measures = {{.id = measure_id_t{3}}},
                    .bpms = {{.id = object_id_t{10}, .position = {measure_id_t{3}, 0},
                               .beats_per_minute = {120, 1}}}}}};
    REQUIRE(extend_native_charts_to_audio_end(song));
    REQUIRE(song.charts.front().measures.size() == 7);
    auto history = document_history_t::from(song);
    REQUIRE(apply_bpm_edit(history,
                           wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                               .chart = chart_id_t{2},
                               .id = object_id_t{10},
                               .value = wormedit::bpm_edit_value_t::from(
                                   {measure_id_t{3}, 0}, wormedit::timing_bpm_value_t{{240, 1}})}));
    check_music_extent(history.document.charts.front().measures.size() == 13);
    check_music_extent(history.undo.size() == 1);
    REQUIRE(undo_document_command(history));
    check_music_extent(history.document.charts.front().measures.size() == 7);
    check_music_extent(history.document.charts.front().bpms.front().beats_per_minute == (signed_rational_t{120, 1}));
}

namespace {

auto bpm_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{3}}},
                    .bpms = {{.id = object_id_t{10},
                              .position = {measure_id_t{3}, 0},
                              .beats_per_minute = {120, 1},
                              .provenance = {.source_order = 4}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

auto has_error(const document_command_result_t& result) -> bool {
    return std::ranges::any_of(result.diagnostics, [](const auto& diagnostic) {
        return diagnostic.severity == diagnostic_severity_t::error;
    });
}

auto commit_snapped_fit(signed_rational_t step) -> std::optional<signed_rational_t> {
    auto history = document_history_t::from(bpm_song());
    const auto begun = beat_interval_drag_t::propose(history.document, chart_id_t{2},
                                                object_id_t{10},
                                                local_position_t{measure_id_t{3}, 480}, 48'000);
    if (!begun.value.has_value()) return std::nullopt;
    const auto frame = begun.value->revise(history.document, {}, 49'000);
    if (!frame.value.has_value()) return std::nullopt;
    const auto snapped = quantize_bpm(frame.value->payload.proposed_bpm, step);
    if (!snapped) return std::nullopt;
    const auto update = frame.value->revise(history.document, {}, *snapped);
    if (!update.value.has_value()) return std::nullopt;
    auto snapped_drag = *update.value;
    snapped_drag.payload.snap_active = true;
    snapped_drag.payload.snap_step = step;
    if (!apply_proposal(history, snapped_drag)) return std::nullopt;
    return history.document.charts.front().bpms.front().beats_per_minute;
}

auto rejects_locked_anchor_snap() -> bool {
    auto song = bpm_song();
    song.charts.front().timing_anchors.push_back({.id = object_id_t{11},
        .position = {measure_id_t{3}, 720}, .source_sample_frame = 72'000, .locked = true});
    if (!validate_document(song).empty()) return false;
    auto history = document_history_t::from(std::move(song));
    const auto begun = beat_interval_drag_t::propose(history.document, chart_id_t{2}, object_id_t{10},
        {measure_id_t{3}, 240}, 48'000);
    if (!begun.value.has_value()) return false;
    const auto frame_fit = begun.value->revise(history.document, {}, 24'000);
    if (!frame_fit.value.has_value()) return false;
    if (frame_fit.value->revise(history.document, {}, signed_rational_t{100, 1}).value.has_value())
        return false;
    auto unsafe = *frame_fit.value;
    unsafe.payload.proposed_bpm = {100, 1};
    unsafe.payload.proposed_sample_frame = 28'800;
    unsafe.payload.snap_active = true;
    unsafe.payload.snap_step = {50, 1};
    const auto revision = history.document.revision;
    const auto original_bpm = history.document.charts.front().bpms.front().beats_per_minute;
    const auto committed = apply_proposal(history, unsafe);
    return !committed && history.document.revision == revision &&
           history.document.charts.front().bpms.front().beats_per_minute == original_bpm;
}

} // namespace

TEST_CASE("BPM text parsing accepts exact scientific notation",
          "[unit][core][bpm-editor][numeric-text]") {
    CHECK(parse_bpm_text(" 1.00001E+07 ") == signed_rational_t{10'000'100, 1});
}

TEST_CASE("BPM text parsing is decimal-first and retains a lossless fraction",
          "[unit][core][bpm-editor][text]") {
    CHECK(parse_bpm_text("120") == signed_rational_t{120, 1});
    CHECK(parse_bpm_text(" 123.4500 ") == signed_rational_t{2469, 20});
    CHECK(parse_bpm_text("7/3") == signed_rational_t{7, 3});
    CHECK_FALSE(parse_bpm_text("0"));
    CHECK_FALSE(parse_bpm_text("-120"));
    CHECK_FALSE(parse_bpm_text("9223372036854775808"));

    const auto terminating = format_bpm_text({2469, 20});
    REQUIRE(terminating);
    CHECK(terminating->decimal == "123.45");
    CHECK(terminating->decimal_exact);
    CHECK(terminating->fraction == "2469/20");

    const auto repeating = format_bpm_text({7, 3});
    REQUIRE(repeating);
    CHECK_FALSE(repeating->decimal.empty());
    CHECK_FALSE(repeating->decimal_exact);
    CHECK(repeating->fraction == "7/3");
    CHECK(parse_bpm_text(repeating->fraction) == signed_rational_t{7, 3});
    CHECK_FALSE(format_bpm_text({0, 1}));
    CHECK_FALSE(format_bpm_text({120, 0}));

    CHECK(step_bpm_by_one({7, 3}, bpm_step_direction_t::increment) == signed_rational_t{10, 3});
    CHECK(step_bpm_by_one({7, 3}, bpm_step_direction_t::decrement) == signed_rational_t{4, 3});
    CHECK_FALSE(step_bpm_by_one({1, 1}, bpm_step_direction_t::decrement));
    CHECK_FALSE(step_bpm_by_one({std::numeric_limits<std::int64_t>::max(), 1},
                                bpm_step_direction_t::increment));
    CHECK_FALSE(step_bpm_by_one({1, std::numeric_limits<std::uint64_t>::max()},
                                bpm_step_direction_t::increment));
}

TEST_CASE("interval beat drag previews by source frame and commits one undoable BPM edit",
          "[unit][core][bpm-editor][beat-fit]") {
    auto history = document_history_t::from(bpm_song());
    const auto beat = local_position_t{measure_id_t{3}, 480};
    const auto begun =
        beat_interval_drag_t::propose(history.document, chart_id_t{2}, object_id_t{10}, beat, 48'000);
    REQUIRE(begun.value.has_value());
    CHECK(begun.value->payload.original_sample_frame == 48'000);
    const auto proposed = begun.value->revise(history.document, {}, 96'000);
    REQUIRE(proposed.value.has_value());
    CHECK(proposed.value->payload.proposed_bpm == signed_rational_t{60, 1});
    CHECK(history.document.revision == 4);
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{120, 1});

    const auto committed = apply_proposal(history, *proposed.value);
    REQUIRE(committed);
    CHECK(history.document.revision == 5);
    CHECK(history.document.charts.front().bpms.front().id == object_id_t{10});
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{60, 1});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{120, 1});
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{60, 1});

    const auto stale = apply_proposal(history, *proposed.value);
    test::check_stale_action_message(stale.diagnostics, "Beat fit");
    CHECK_FALSE(stale);
    CHECK(has_code(stale, diagnostic_code_t::invalid_id));
}

TEST_CASE("beat fit snap retains the exact selected rational step",
          "[unit][core][bpm-editor][beat-fit]") {
    CHECK(quantize_bpm({493, 2}, {1, 4}) == signed_rational_t{493, 2});
    CHECK(quantize_bpm({493, 3}, {1, 3}) == signed_rational_t{493, 3});
    CHECK(quantize_bpm({493, 5}, {1, 4}) == signed_rational_t{197, 2});
    CHECK(quantize_bpm({493, 5}, {1, 3}) == signed_rational_t{296, 3});
    CHECK_FALSE(quantize_bpm({std::numeric_limits<std::int64_t>::max(), 1},
                             {std::numeric_limits<std::int64_t>::max(),
                              std::numeric_limits<std::uint64_t>::max()}));
}

TEST_CASE("snapped beat-fit commit writes the exact fractional BPM to history",
          "[unit][core][bpm-editor][beat-fit]") {
    const auto committed = commit_snapped_fit({1, 3});
    REQUIRE(committed);
    CHECK(quantize_bpm(*committed, {1, 3}) == committed);
}

TEST_CASE("snapped beat-fit revalidates locked timing anchors at commit",
          "[unit][core][bpm-editor][beat-fit][timing-anchor]") {
    CHECK(rejects_locked_anchor_snap());
}

TEST_CASE("BPM insert update and delete are undoable and retain exact identity and source order",
          "[unit][core][bpm-editor]") {
    auto song = bpm_song();
    song.charts.front().source_family = format_family_t::bms;
    auto history = document_history_t::from(std::move(song));
    const auto insert = apply_bpm_edit(
        history, wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                     .chart = chart_id_t{2},
                     .value = wormedit::bpm_edit_value_t::from(
                         {measure_id_t{3}, 0}, wormedit::timing_bpm_value_t{{90, 2}})});
    REQUIRE(insert);
    REQUIRE(history.document.charts.front().bpms.size() == 2);
    const auto inserted = history.document.charts.front().bpms.back();
    CHECK(inserted.id == object_id_t{11});
    CHECK(inserted.position == local_position_t{measure_id_t{3}, 0});
    CHECK(inserted.beats_per_minute == signed_rational_t{90, 2});
    CHECK(inserted.provenance.source_order == 5);
    CHECK(inserted.provenance.family == format_family_t::native_project);
    CHECK(inserted.provenance.source_spelling.empty());
    CHECK(history.document.revision == 5);
    CHECK(is_document_dirty(history));

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bpms.size() == 1);
    CHECK_FALSE(is_document_dirty(history));
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().bpms.back().id == inserted.id);

    const auto update = apply_bpm_edit(
        history, wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = inserted.id,
                     .value = wormedit::bpm_edit_value_t::from(
                         {measure_id_t{3}, 0}, wormedit::timing_bpm_value_t{{180, 2}})});
    REQUIRE(update);
    const auto& updated = history.document.charts.front().bpms.back();
    CHECK(updated.id == inserted.id);
    CHECK(updated.position == inserted.position);
    CHECK(updated.beats_per_minute == signed_rational_t{180, 2});
    CHECK(updated.provenance.source_order == inserted.provenance.source_order);

    const auto timing = build_timing_index(history.document.charts.front());
    REQUIRE(timing);
    const auto after_one_beat = timing_at(*timing.index, 240);
    REQUIRE(after_one_beat);
    CHECK(after_one_beat.seconds == signed_rational_t{2, 3});

    const auto no_op = apply_bpm_edit(
        history,
        wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
            .chart = chart_id_t{2},
            .id = inserted.id,
            .value = wormedit::bpm_edit_value_t::from(
                updated.position, wormedit::timing_bpm_value_t{updated.beats_per_minute})});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 6);

    REQUIRE(apply_bpm_edit(history, wormedit::delete_edit_t<wormedit::bpm_edit_value_t>{
                                        .chart = chart_id_t{2}, .id = inserted.id}));
    CHECK(history.document.charts.front().bpms.size() == 1);
    REQUIRE(undo_document_command(history));
    REQUIRE(history.document.charts.front().bpms.size() == 2);
    CHECK(history.document.charts.front().bpms.back().id == inserted.id);
    CHECK(history.document.charts.front().bpms.back().beats_per_minute ==
          signed_rational_t{180, 2});
}

TEST_CASE("BPM same-row events use source order and remain valid for the timing index",
          "[unit][core][bpm-editor][ordering]") {
    auto song = bpm_song();
    song.charts.front().bpms.front().provenance.source_order = 10;
    song.charts.front().bpms.push_back({.id = object_id_t{11},
                                        .position = {measure_id_t{3}, 0},
                                        .beats_per_minute = {60, 1},
                                        .provenance = {.source_order = 12}});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));

    const auto inserted = apply_bpm_edit(
        history, wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                     .chart = chart_id_t{2},
                     .value = wormedit::bpm_edit_value_t::from(
                         {measure_id_t{3}, 0}, wormedit::timing_bpm_value_t{{90, 1}})});
    REQUIRE(inserted);
    const auto& chart = history.document.charts.front();
    REQUIRE(chart.bpms.size() == 3);
    CHECK(chart.bpms.back().provenance.source_order == 13);
    CHECK(chart.bpms.back().position == chart.bpms.front().position);

    const auto timing = build_timing_index(chart);
    REQUIRE(timing);
    const auto one_beat = timing_at(*timing.index, 240);
    REQUIRE(one_beat);
    CHECK(one_beat.seconds == signed_rational_t{2, 3});

    const auto updated = apply_bpm_edit(
        history, wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = chart.bpms.back().id,
                     .value = wormedit::bpm_edit_value_t::from(
                         {measure_id_t{3}, 0}, wormedit::timing_bpm_value_t{{75, 1}})});
    REQUIRE(updated);
    CHECK(history.document.charts.front().bpms.back().provenance.source_order == 13);
    const auto updated_timing = build_timing_index(history.document.charts.front());
    REQUIRE(updated_timing);
    const auto updated_one_beat = timing_at(*updated_timing.index, 240);
    REQUIRE(updated_one_beat);
    CHECK(updated_one_beat.seconds == signed_rational_t{4, 5});
}

TEST_CASE("BPM editor rejects invalid IDs positions and invalid exact values atomically",
          "[unit][core][bpm-editor][negative]") {
    auto history = document_history_t::from(bpm_song());
    const auto unchanged_revision = history.document.revision;
    const auto unchanged_bpm_count = history.document.charts.front().bpms.size();

    const auto unknown_chart = apply_bpm_edit(
        history, wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                     .chart = chart_id_t{99},
                     .value = wormedit::bpm_edit_value_t::from(
                         {measure_id_t{3}, 240}, wormedit::timing_bpm_value_t{{100, 1}})});
    CHECK_FALSE(unknown_chart.applied);
    CHECK(has_code(unknown_chart, diagnostic_code_t::invalid_id));

    const auto unknown_event = apply_bpm_edit(
        history, wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                     .chart = chart_id_t{2},
                     .id = object_id_t{99},
                     .value = wormedit::bpm_edit_value_t::from(
                         {measure_id_t{3}, 0}, wormedit::timing_bpm_value_t{{100, 1}})});
    CHECK_FALSE(unknown_event.applied);
    CHECK(has_code(unknown_event, diagnostic_code_t::invalid_id));

    const auto unknown_delete =
        apply_bpm_edit(history, wormedit::delete_edit_t<wormedit::bpm_edit_value_t>{
                                    .chart = chart_id_t{2}, .id = object_id_t{99}});
    CHECK_FALSE(unknown_delete.applied);
    CHECK(has_code(unknown_delete, diagnostic_code_t::invalid_id));

    for (const auto position :
         {local_position_t{measure_id_t{99}, 0}, local_position_t{measure_id_t{3}, 960}}) {
        const auto invalid_position =
            apply_bpm_edit(history, wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                                        .chart = chart_id_t{2},
                                        .value = wormedit::bpm_edit_value_t::from(
                                            position, wormedit::timing_bpm_value_t{{100, 1}})});
        CHECK_FALSE(invalid_position.applied);
        CHECK(has_code(invalid_position, diagnostic_code_t::row_out_of_range));
    }

    for (const auto value :
         {signed_rational_t{0, 1}, signed_rational_t{-1, 1}, signed_rational_t{120, 0}}) {
        const auto invalid_bpm = apply_bpm_edit(
            history, wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                         .chart = chart_id_t{2},
                         .value = wormedit::bpm_edit_value_t::from(
                             {measure_id_t{3}, 240}, wormedit::timing_bpm_value_t{value})});
        CHECK_FALSE(invalid_bpm.applied);
        CHECK(has_code(invalid_bpm, diagnostic_code_t::invalid_rational));
    }

    CHECK(history.document.revision == unchanged_revision);
    CHECK(history.document.charts.front().bpms.size() == unchanged_bpm_count);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
}

TEST_CASE("BPM editor rejects ambiguous stable IDs and event caps",
          "[unit][core][bpm-editor][bounds]") {
    auto duplicate_song = bpm_song();
    duplicate_song.charts.front().bpms.push_back({.id = object_id_t{10},
                                                  .position = {measure_id_t{3}, 240},
                                                  .beats_per_minute = {90, 1},
                                                  .provenance = {.source_order = 5}});
    auto duplicate_history = document_history_t::from(duplicate_song);
    const auto duplicate =
        apply_bpm_edit(duplicate_history,
                       wormedit::update_edit_t<wormedit::bpm_edit_value_t>{
                           .chart = chart_id_t{2},
                           .id = object_id_t{10},
                           .value = wormedit::bpm_edit_value_t::from(
                               {measure_id_t{3}, 480}, wormedit::timing_bpm_value_t{{100, 1}})});
    CHECK_FALSE(duplicate.applied);
    CHECK(has_code(duplicate, diagnostic_code_t::duplicate_id));
    CHECK(duplicate_history.document.charts.front().bpms.size() == 2);
    CHECK(duplicate_history.undo.empty());

    auto history = document_history_t::from(bpm_song());
    const auto event_capped =
        apply_bpm_edit(history,
                       wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                           .chart = chart_id_t{2},
                           .value = wormedit::bpm_edit_value_t::from(
                               {measure_id_t{3}, 240}, wormedit::timing_bpm_value_t{{100, 1}})},
                       {.maximum_bpm_events = 1});
    CHECK_FALSE(event_capped.applied);
    CHECK(has_code(event_capped, diagnostic_code_t::invalid_resolution));

    const auto objects_capped =
        apply_bpm_edit(history,
                       wormedit::insert_edit_t<wormedit::bpm_edit_value_t>{
                           .chart = chart_id_t{2},
                           .value = wormedit::bpm_edit_value_t::from(
                               {measure_id_t{3}, 240}, wormedit::timing_bpm_value_t{{100, 1}})},
                       {.maximum_chart_objects = 1});
    CHECK_FALSE(objects_capped.applied);
    CHECK(has_code(objects_capped, diagnostic_code_t::invalid_resolution));
    CHECK(history.document.charts.front().bpms.size() == 1);
    CHECK(history.undo.empty());
}

TEST_CASE("beat fit without a BPM change at the anchor inserts one exact BPM as one undo step",
          "[unit][core][bpm-editor][beat-fit]") {
    auto history = document_history_t::from(bpm_song());
    const auto anchor = local_position_t{measure_id_t{3}, 240};
    const auto beat = local_position_t{measure_id_t{3}, 480};
    const auto begun = beat_interval_drag_t::propose(history.document, chart_id_t{2}, anchor,
                                                       beat, 48'000);
    REQUIRE(begun.value.has_value());
    CHECK(begun.value->payload.insert_bpm);
    CHECK(begun.value->payload.original_bpm == signed_rational_t{120, 1});
    CHECK(begun.value->payload.original_sample_frame == 48'000);

    const auto proposed = begun.value->revise(history.document, {}, 96'000);
    REQUIRE(proposed.value.has_value());
    CHECK(proposed.value->payload.proposed_bpm == signed_rational_t{40, 1});
    CHECK(history.document.charts.front().bpms.size() == 1);

    const auto undo_depth = history.undo.size();
    const auto committed = apply_proposal(history, *proposed.value);
    REQUIRE(committed);
    const auto& bpms = history.document.charts.front().bpms;
    REQUIRE(bpms.size() == 2);
    const auto inserted = std::ranges::find(bpms, anchor, &bpm_event_t::position);
    REQUIRE(inserted != bpms.end());
    CHECK(inserted->beats_per_minute == signed_rational_t{40, 1});
    CHECK(history.undo.size() == undo_depth + 1);

    const auto refit = beat_interval_drag_t::propose(history.document, chart_id_t{2}, anchor,
                                                       beat, 48'000);
    REQUIRE(refit.value.has_value());
    CHECK_FALSE(refit.value->payload.insert_bpm);
    CHECK(refit.value->payload.original_sample_frame == 96'000);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bpms.size() == 1);
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{120, 1});
}

TEST_CASE("beat fit anchor reuses an existing BPM change and rejects impossible targets",
          "[unit][core][bpm-editor][beat-fit]") {
    auto history = document_history_t::from(bpm_song());
    const auto beat = local_position_t{measure_id_t{3}, 480};
    const auto begun = beat_interval_drag_t::propose(history.document, chart_id_t{2},
                                                       {measure_id_t{3}, 0}, beat, 48'000);
    REQUIRE(begun.value.has_value());
    CHECK_FALSE(begun.value->payload.insert_bpm);
    CHECK(begun.value->payload.bpm_event == object_id_t{10});

    const auto impossible = begun.value->revise(history.document, {}, 0);
    CHECK_FALSE(impossible.value.has_value());
    CHECK_FALSE(impossible.diagnostics.empty());
}

TEST_CASE("beat fit insert blocks when a locked anchor after the beat would move",
          "[unit][core][bpm-editor][beat-fit]") {
    auto song = bpm_song();
    song.charts.front().timing_anchors.push_back({.id = object_id_t{11},
        .position = {measure_id_t{3}, 720}, .source_sample_frame = 72'000, .locked = true});
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));
    const auto begun = beat_interval_drag_t::propose(history.document, chart_id_t{2},
                                                       {measure_id_t{3}, 240},
                                                       {measure_id_t{3}, 480}, 48'000);
    REQUIRE(begun.value.has_value());
    CHECK(begun.value->payload.insert_bpm);
    CHECK_FALSE(begun.value->revise(history.document, {}, 96'000).value.has_value());

    auto unsafe = *begun.value;
    unsafe.payload.proposed_sample_frame = 96'000;
    unsafe.payload.proposed_bpm = {40, 1};
    const auto revision = history.document.revision;
    const auto committed = apply_proposal(history, unsafe);
    CHECK_FALSE(committed);
    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().bpms.size() == 1);
}

TEST_CASE("beat fit keeps revision-only identity and rejects expired proposals",
          "[core][bpm-editor][beat-fit][stamp]") {
    auto history = document_history_t::from(bpm_song());
    const auto begun = beat_interval_drag_t::propose(history.document, chart_id_t{2}, object_id_t{10},
                                               {measure_id_t{3}, 480}, 48'000);
    REQUIRE(begun.value.has_value());
    const auto proposed = begun.value->revise(history.document, {}, 96'000);
    REQUIRE(proposed.value.has_value());
    history.document.id = song_id_t{99};
    REQUIRE(apply_proposal(history, *proposed.value));
    CHECK(history.undo.size() == 1);
    const auto stale = apply_proposal(history, *proposed.value);
    CHECK_FALSE(stale);
    test::check_stale_action_message(stale.diagnostics, "Beat fit");
    CHECK(history.undo.size() == 1);
}

TEST_CASE("beat fit preserves snap lock and provenance rejection priority",
          "[core][bpm-editor][beat-fit][stamp]") {
    auto history = document_history_t::from(bpm_song());
    auto drag = beat_interval_drag_t::propose(history.document, chart_id_t{2}, object_id_t{10},
                                        {measure_id_t{3}, 480}, 48'000);
    REQUIRE(drag.value.has_value());
    ++history.document.revision;
    const auto locked = apply_proposal(history, *drag.value, true);
    REQUIRE_FALSE(locked.diagnostics.empty());
    CHECK(locked.diagnostics.front().message == "beat fitting is disabled while timing is locked");
    drag.value->payload.snap_active = true;
    drag.value->payload.proposed_bpm = {0, 1};
    const auto invalid = apply_proposal(history, *drag.value, true);
    REQUIRE_FALSE(invalid.diagnostics.empty());
    CHECK(invalid.diagnostics.front().message == "BPM snap must produce a positive exact value");
    CHECK(history.undo.empty());
}

namespace {
void check_beat_fit_no_op(const document_command_result_t& result, const document_history_t& history) {
    CHECK_FALSE(result.applied);
    CHECK(result.diagnostics.empty());
    CHECK(history.undo.empty());
}
} // namespace

TEST_CASE("beat fit unchanged frame does not add a sample rate precondition",
          "[core][bpm-editor][beat-fit][stamp]") {
    auto history = document_history_t::from(bpm_song());
    auto drag = beat_interval_drag_t::propose(history.document, chart_id_t{2}, object_id_t{10},
                                        {measure_id_t{3}, 480}, 48'000);
    REQUIRE(drag.value.has_value());
    drag.value->payload.sample_rate = 0;
    const auto unchanged = drag.value->revise(history.document, {}, drag.value->payload.original_sample_frame);
    REQUIRE(unchanged.value.has_value());
    const auto no_op = apply_proposal(history, *unchanged.value);
    check_beat_fit_no_op(no_op, history);
    history.document.charts.front().bpms.front().beats_per_minute = {100, 1};
    const auto drift = apply_proposal(history, *unchanged.value);
    REQUIRE_FALSE(drift.diagnostics.empty());
    CHECK(drift.diagnostics.front().message == "beat fit BPM event changed during drag");
}

namespace {
void check_failed_beat_fit_diagnostics(const diagnostic_list_t& diagnostics) {
    REQUIRE(diagnostics.size() == 1);
    CHECK(diagnostics.front().code == diagnostic_code_t::invalid_rational);
    CHECK(diagnostics.front().message == "beat fitting requires a sample rate");
}
} // namespace

TEST_CASE("failed beat fit outcome owns diagnostics without an apply payload",
          "[core][bpm-editor][beat-fit][stamp]") {
    auto history = document_history_t::from(bpm_song());
    const auto failed = beat_interval_drag_t::propose(history.document, chart_id_t{2}, object_id_t{10},
                                                {measure_id_t{3}, 480}, 0);
    REQUIRE_FALSE(failed.value.has_value());
    check_failed_beat_fit_diagnostics(failed.diagnostics);
    CHECK(history.document.revision == 4);
    CHECK(history.undo.empty());
}

TEST_CASE("Beat fit revision keeps invalid BPM before lock and captured expiry",
          "[core][bpm-editor][beat-fit][stamp]") {
    auto history = document_history_t::from(bpm_song());
    const auto pending = beat_interval_drag_t::propose(history.document, chart_id_t{2}, object_id_t{10},
        {measure_id_t{3}, 480}, 48'000);
    REQUIRE(pending.value);
    ++history.document.revision;
    const auto rejected = pending.value->revise(history.document, {.is_timing_locked = true}, signed_rational_t{0, 1});
    REQUIRE(rejected.diagnostics.size() == 1);
    CHECK(rejected.diagnostics.front().code == diagnostic_code_t::invalid_rational);
    CHECK(rejected.diagnostics.front().message == "BPM snap must produce a positive exact value");
    CHECK_FALSE(rejected.value);
    CHECK(history.undo.empty());
}

TEST_CASE("Beat fit frame revision keeps timing lock before captured expiry",
          "[core][bpm-editor][beat-fit][stamp]") {
    auto history = document_history_t::from(bpm_song());
    const auto pending = beat_interval_drag_t::propose(history.document, chart_id_t{2}, object_id_t{10},
        {measure_id_t{3}, 480}, 48'000);
    REQUIRE(pending.value);
    ++history.document.revision;
    const auto rejected = pending.value->revise(history.document, {.is_timing_locked = true}, std::uint64_t{96'000});
    REQUIRE(rejected.diagnostics.size() == 1);
    CHECK(rejected.diagnostics.front().code == diagnostic_code_t::unsupported_feature);
    CHECK(rejected.diagnostics.front().message == "beat fitting is disabled while timing is locked");
    CHECK_FALSE(rejected.value);
    CHECK(history.undo.empty());
}
