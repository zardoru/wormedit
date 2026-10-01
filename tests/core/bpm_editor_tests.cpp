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
    auto history = make_document_history(song);
    REQUIRE(apply_bpm_edit(history, 1, update_bpm_event_action_t{
        chart_id_t{2}, object_id_t{10}, {measure_id_t{3}, 0}, {240, 1}}));
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
    auto history = make_document_history(bpm_song());
    const auto begun = begin_beat_interval_drag(history.document, chart_id_t{2},
                                                object_id_t{10},
                                                local_position_t{measure_id_t{3}, 480}, 48'000);
    if (!begun) return std::nullopt;
    const auto frame = update_beat_interval_drag(history.document, *begun.drag, 49'000);
    if (!frame) return std::nullopt;
    const auto snapped = quantize_bpm(frame.drag->proposed_bpm, step);
    if (!snapped) return std::nullopt;
    const auto update = update_beat_interval_drag_to_bpm(history.document, *frame.drag, *snapped);
    if (!update) return std::nullopt;
    auto snapped_drag = *update.drag;
    snapped_drag.snap_active = true;
    snapped_drag.snap_step = step;
    if (!commit_beat_interval_drag(history, snapped_drag)) return std::nullopt;
    return history.document.charts.front().bpms.front().beats_per_minute;
}

auto rejects_locked_anchor_snap() -> bool {
    auto song = bpm_song();
    song.charts.front().timing_anchors.push_back({.id = object_id_t{11},
        .position = {measure_id_t{3}, 720}, .source_sample_frame = 72'000, .locked = true});
    if (!validate_document(song).empty()) return false;
    auto history = make_document_history(std::move(song));
    const auto begun = begin_beat_interval_drag(history.document, chart_id_t{2}, object_id_t{10},
        {measure_id_t{3}, 240}, 48'000);
    if (!begun) return false;
    const auto frame_fit = update_beat_interval_drag(history.document, *begun.drag, 24'000);
    if (!frame_fit) return false;
    if (update_beat_interval_drag_to_bpm(history.document, *frame_fit.drag, {100, 1}))
        return false;
    auto unsafe = *frame_fit.drag;
    unsafe.proposed_bpm = {100, 1};
    unsafe.proposed_sample_frame = 28'800;
    unsafe.snap_active = true;
    unsafe.snap_step = {50, 1};
    const auto revision = history.document.revision;
    const auto original_bpm = history.document.charts.front().bpms.front().beats_per_minute;
    const auto committed = commit_beat_interval_drag(history, unsafe);
    return !committed && history.document.revision == revision &&
           history.document.charts.front().bpms.front().beats_per_minute == original_bpm;
}

} // namespace

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
    auto history = make_document_history(bpm_song());
    const auto beat = local_position_t{measure_id_t{3}, 480};
    const auto begun =
        begin_beat_interval_drag(history.document, chart_id_t{2}, object_id_t{10}, beat, 48'000);
    REQUIRE(begun);
    CHECK(begun.drag->original_sample_frame == 48'000);
    const auto proposed = update_beat_interval_drag(history.document, *begun.drag, 96'000);
    REQUIRE(proposed);
    CHECK(proposed.drag->proposed_bpm == signed_rational_t{60, 1});
    CHECK(history.document.revision == 4);
    CHECK(history.document.charts.front().bpms.front().beats_per_minute ==
          signed_rational_t{120, 1});

    const auto committed = commit_beat_interval_drag(history, *proposed.drag);
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

    const auto stale = commit_beat_interval_drag(history, *proposed.drag);
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
    auto history = make_document_history(std::move(song));
    const auto insert = apply_bpm_edit(
        history, 4, insert_bpm_event_action_t{chart_id_t{2}, {measure_id_t{3}, 0}, {90, 2}});
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
        history, 5,
        update_bpm_event_action_t{chart_id_t{2}, inserted.id, {measure_id_t{3}, 0}, {180, 2}});
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

    const auto no_op =
        apply_bpm_edit(history, 6,
                       update_bpm_event_action_t{chart_id_t{2}, inserted.id, updated.position,
                                                 updated.beats_per_minute});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 6);

    REQUIRE(apply_bpm_edit(history, 6, delete_bpm_event_action_t{chart_id_t{2}, inserted.id}));
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
    auto history = make_document_history(std::move(song));

    const auto inserted = apply_bpm_edit(
        history, 4, insert_bpm_event_action_t{chart_id_t{2}, {measure_id_t{3}, 0}, {90, 1}});
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

    const auto updated =
        apply_bpm_edit(history, 5,
                       update_bpm_event_action_t{
                           chart_id_t{2}, chart.bpms.back().id, {measure_id_t{3}, 0}, {75, 1}});
    REQUIRE(updated);
    CHECK(history.document.charts.front().bpms.back().provenance.source_order == 13);
    const auto updated_timing = build_timing_index(history.document.charts.front());
    REQUIRE(updated_timing);
    const auto updated_one_beat = timing_at(*updated_timing.index, 240);
    REQUIRE(updated_one_beat);
    CHECK(updated_one_beat.seconds == signed_rational_t{4, 5});
}

TEST_CASE("BPM editor rejects stale IDs positions and invalid exact values atomically",
          "[unit][core][bpm-editor][negative]") {
    auto history = make_document_history(bpm_song());
    const auto unchanged_revision = history.document.revision;
    const auto unchanged_bpm_count = history.document.charts.front().bpms.size();

    const auto stale = apply_bpm_edit(
        history, 3, insert_bpm_event_action_t{chart_id_t{2}, {measure_id_t{3}, 240}, {100, 1}});
    CHECK_FALSE(stale.applied);
    CHECK(has_code(stale, diagnostic_code_t::invalid_id));

    const auto unknown_chart = apply_bpm_edit(
        history, 4, insert_bpm_event_action_t{chart_id_t{99}, {measure_id_t{3}, 240}, {100, 1}});
    CHECK_FALSE(unknown_chart.applied);
    CHECK(has_code(unknown_chart, diagnostic_code_t::invalid_id));

    const auto unknown_event = apply_bpm_edit(
        history, 4,
        update_bpm_event_action_t{chart_id_t{2}, object_id_t{99}, {measure_id_t{3}, 0}, {100, 1}});
    CHECK_FALSE(unknown_event.applied);
    CHECK(has_code(unknown_event, diagnostic_code_t::invalid_id));

    const auto unknown_delete =
        apply_bpm_edit(history, 4, delete_bpm_event_action_t{chart_id_t{2}, object_id_t{99}});
    CHECK_FALSE(unknown_delete.applied);
    CHECK(has_code(unknown_delete, diagnostic_code_t::invalid_id));

    for (const auto position :
         {local_position_t{measure_id_t{99}, 0}, local_position_t{measure_id_t{3}, 960}}) {
        const auto invalid_position = apply_bpm_edit(
            history, 4, insert_bpm_event_action_t{chart_id_t{2}, position, {100, 1}});
        CHECK_FALSE(invalid_position.applied);
        CHECK(has_code(invalid_position, diagnostic_code_t::row_out_of_range));
    }

    for (const auto value :
         {signed_rational_t{0, 1}, signed_rational_t{-1, 1}, signed_rational_t{120, 0}}) {
        const auto invalid_bpm = apply_bpm_edit(
            history, 4, insert_bpm_event_action_t{chart_id_t{2}, {measure_id_t{3}, 240}, value});
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
    auto duplicate_history = make_document_history(duplicate_song);
    const auto duplicate =
        apply_bpm_edit(duplicate_history, 4,
                       update_bpm_event_action_t{
                           chart_id_t{2}, object_id_t{10}, {measure_id_t{3}, 480}, {100, 1}});
    CHECK_FALSE(duplicate.applied);
    CHECK(has_code(duplicate, diagnostic_code_t::duplicate_id));
    CHECK(duplicate_history.document.charts.front().bpms.size() == 2);
    CHECK(duplicate_history.undo.empty());

    auto history = make_document_history(bpm_song());
    const auto event_capped = apply_bpm_edit(
        history, 4, insert_bpm_event_action_t{chart_id_t{2}, {measure_id_t{3}, 240}, {100, 1}},
        {.maximum_bpm_events = 1});
    CHECK_FALSE(event_capped.applied);
    CHECK(has_code(event_capped, diagnostic_code_t::invalid_resolution));

    const auto objects_capped = apply_bpm_edit(
        history, 4, insert_bpm_event_action_t{chart_id_t{2}, {measure_id_t{3}, 240}, {100, 1}},
        {.maximum_chart_objects = 1});
    CHECK_FALSE(objects_capped.applied);
    CHECK(has_code(objects_capped, diagnostic_code_t::invalid_resolution));
    CHECK(history.document.charts.front().bpms.size() == 1);
    CHECK(history.undo.empty());
}
