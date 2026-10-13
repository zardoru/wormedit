#include <wormedit/core/marker_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <ranges>

using namespace wormedit;

namespace {

auto marker_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{.id = chart_id_t{2},
                    .divisions_per_standard_measure = 960,
                    .measures = {{.id = measure_id_t{3}}},
                    .bpms = {{.id = object_id_t{10},
                              .position = {measure_id_t{3}, 0},
                              .beats_per_minute = {120, 1}}},
                    .kiai = {{.id = object_id_t{11},
                              .position = {measure_id_t{3}, 240},
                              .enabled = true,
                              .retained_effect_bits = 9,
                              .provenance = {.family = format_family_t::osu_mania,
                                             .source_order = 7}}},
                    .bookmarks = {{.id = object_id_t{12},
                                   .label = "Old marker",
                                   .position = local_position_t{measure_id_t{3}, 120}}},
                    .sections = {{.id = object_id_t{13},
                                  .label = "Verse",
                                  .beat_span = position_span_t{{measure_id_t{3}, 240},
                                                               {measure_id_t{3}, 480}}}}}},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("marker editor inserts beat and audio markers and sections with chart-wide IDs",
          "[unit][core][marker-editor]") {
    auto history = document_history_t::from(marker_song());
    const auto beat_marker = apply_marker_edit(
        history, 4,
        insert_bookmark_action_t{.chart = chart_id_t{2},
                                 .label = "",
                                 .anchor = marker_anchor_t::beat,
                                 .position = local_position_t{measure_id_t{3}, 0}});
    REQUIRE(beat_marker);
    const auto inserted_bookmark = history.document.charts.front().bookmarks.back();
    CHECK(inserted_bookmark.id == object_id_t{14});
    CHECK(inserted_bookmark.label.empty());
    CHECK(inserted_bookmark.anchor == marker_anchor_t::beat);
    CHECK(inserted_bookmark.position == local_position_t{measure_id_t{3}, 0});
    CHECK_FALSE(inserted_bookmark.audio_milliseconds.has_value());
    CHECK(history.document.revision == 5);
    CHECK(is_document_dirty(history));

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bookmarks.size() == 1);
    CHECK_FALSE(is_document_dirty(history));
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().bookmarks.back().id == inserted_bookmark.id);
    CHECK(is_document_dirty(history));

    const auto audio_marker =
        apply_marker_edit(history, 5,
                          insert_bookmark_action_t{.chart = chart_id_t{2},
                                                   .label = "Audio cue",
                                                   .anchor = marker_anchor_t::audio_time,
                                                   .audio_milliseconds = -250});
    REQUIRE(audio_marker);
    const auto inserted_audio_bookmark = history.document.charts.front().bookmarks.back();
    CHECK(inserted_audio_bookmark.id == object_id_t{15});
    CHECK(inserted_audio_bookmark.audio_milliseconds == -250);
    CHECK_FALSE(inserted_audio_bookmark.position.has_value());

    const auto audio_section = apply_marker_edit(
        history, 6,
        insert_section_action_t{.chart = chart_id_t{2},
                                .label = "Intro",
                                .anchor = marker_anchor_t::audio_time,
                                .audio_millisecond_span =
                                    std::pair<std::int64_t, std::int64_t>{-500, 1'000}});
    REQUIRE(audio_section);
    CHECK(history.document.charts.front().sections.back().id == object_id_t{16});
    CHECK(history.document.charts.front().sections.back().audio_millisecond_span ==
          std::pair<std::int64_t, std::int64_t>{-500, 1'000});

    const auto kiai =
        apply_marker_edit(history, 7,
                          insert_kiai_event_action_t{.chart = chart_id_t{2},
                                                     .position = {measure_id_t{3}, 720},
                                                     .enabled = false,
                                                     .retained_effect_bits = 13});
    REQUIRE(kiai);
    const auto inserted_kiai = history.document.charts.front().kiai.back();
    CHECK(inserted_kiai.id == object_id_t{17});
    CHECK(inserted_kiai.enabled == false);
    CHECK(inserted_kiai.retained_effect_bits == 13);
    CHECK(inserted_kiai.provenance.source_order == 8);
    CHECK(validate_document(history.document).empty());

    auto empty_section_history = document_history_t::from(marker_song());
    const auto empty_section = apply_marker_edit(
        empty_section_history, 4,
        insert_section_action_t{.chart = chart_id_t{2},
                                .label = "",
                                .beat_span =
                                    position_span_t{{measure_id_t{3}, 0}, {measure_id_t{3}, 120}}});
    REQUIRE(empty_section);
    CHECK(empty_section_history.document.charts.front().sections.back().label.empty());
}

TEST_CASE("marker updates retain identity and kiai source data and no-op cleanly",
          "[unit][core][marker-editor]") {
    auto history = document_history_t::from(marker_song());
    const auto bookmark_update =
        apply_marker_edit(history, 4,
                          update_bookmark_action_t{.chart = chart_id_t{2},
                                                   .bookmark = object_id_t{12},
                                                   .label = "Audio marker",
                                                   .anchor = marker_anchor_t::audio_time,
                                                   .audio_milliseconds = 1'250});
    REQUIRE(bookmark_update);
    const auto& bookmark = history.document.charts.front().bookmarks.front();
    CHECK(bookmark.id == object_id_t{12});
    CHECK(bookmark.label == "Audio marker");
    CHECK(bookmark.anchor == marker_anchor_t::audio_time);
    CHECK(bookmark.audio_milliseconds == 1'250);
    CHECK_FALSE(bookmark.position.has_value());

    const auto section_update = apply_marker_edit(
        history, 5,
        update_section_action_t{
            .chart = chart_id_t{2},
            .section = object_id_t{13},
            .label = "Chorus",
            .anchor = marker_anchor_t::beat,
            .beat_span = position_span_t{{measure_id_t{3}, 480}, {measure_id_t{3}, 720}}});
    REQUIRE(section_update);
    CHECK(history.document.charts.front().sections.front().id == object_id_t{13});
    CHECK(history.document.charts.front().sections.front().label == "Chorus");

    const auto kiai_update =
        apply_marker_edit(history, 6,
                          update_kiai_event_action_t{.chart = chart_id_t{2},
                                                     .event = object_id_t{11},
                                                     .position = {measure_id_t{3}, 360},
                                                     .enabled = false});
    REQUIRE(kiai_update);
    const auto updated_kiai = history.document.charts.front().kiai.front();
    CHECK(updated_kiai.id == object_id_t{11});
    CHECK(updated_kiai.position == local_position_t{measure_id_t{3}, 360});
    CHECK_FALSE(updated_kiai.enabled);
    CHECK(updated_kiai.retained_effect_bits == 9);
    CHECK(updated_kiai.provenance.family == format_family_t::osu_mania);
    CHECK(updated_kiai.provenance.source_order == 7);

    const auto no_op =
        apply_marker_edit(history, 7,
                          update_kiai_event_action_t{.chart = chart_id_t{2},
                                                     .event = object_id_t{11},
                                                     .position = updated_kiai.position,
                                                     .enabled = updated_kiai.enabled});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 7);
    CHECK(history.undo.size() == 3);

    REQUIRE(undo_document_command(history));
    const auto undone_kiai = history.document.charts.front().kiai.front();
    CHECK(undone_kiai.position == local_position_t{measure_id_t{3}, 240});
    CHECK(undone_kiai.enabled);
    CHECK(undone_kiai.retained_effect_bits == 9);
    REQUIRE(redo_document_command(history));
    CHECK(history.document.charts.front().kiai.front().position ==
          local_position_t{measure_id_t{3}, 360});
}

TEST_CASE("audio-time marker CRUD preserves exact signed millisecond bounds",
          "[unit][core][marker-editor][audio-time]") {
    auto history = document_history_t::from(marker_song());
    REQUIRE(apply_marker_edit(history, 4,
                              insert_bookmark_action_t{.chart = chart_id_t{2},
                                                       .label = "Preroll",
                                                       .anchor = marker_anchor_t::audio_time,
                                                       .audio_milliseconds = INT64_MIN}));
    const auto bookmark_id = history.document.charts.front().bookmarks.back().id;
    REQUIRE(apply_marker_edit(
        history, 5,
        insert_section_action_t{.chart = chart_id_t{2},
                                .label = "Audio span",
                                .anchor = marker_anchor_t::audio_time,
                                .audio_millisecond_span =
                                    std::pair<std::int64_t, std::int64_t>{-1'500, 2'500}}));
    const auto section_id = history.document.charts.front().sections.back().id;

    REQUIRE(apply_marker_edit(history, 6,
                              update_bookmark_action_t{.chart = chart_id_t{2},
                                                       .bookmark = bookmark_id,
                                                       .label = "Postroll",
                                                       .anchor = marker_anchor_t::audio_time,
                                                       .audio_milliseconds = INT64_MAX}));
    REQUIRE(apply_marker_edit(
        history, 7,
        update_section_action_t{.chart = chart_id_t{2},
                                .section = section_id,
                                .label = "Expanded audio span",
                                .anchor = marker_anchor_t::audio_time,
                                .audio_millisecond_span =
                                    std::pair<std::int64_t, std::int64_t>{INT64_MIN, INT64_MAX}}));
    CHECK(history.document.charts.front().bookmarks.back().audio_milliseconds == INT64_MAX);
    CHECK(history.document.charts.front().sections.back().audio_millisecond_span ==
          std::pair<std::int64_t, std::int64_t>{INT64_MIN, INT64_MAX});

    REQUIRE(apply_marker_edit(history, 8, delete_bookmark_action_t{chart_id_t{2}, bookmark_id}));
    REQUIRE(apply_marker_edit(history, 9, delete_section_action_t{chart_id_t{2}, section_id}));
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().sections.back().id == section_id);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bookmarks.back().id == bookmark_id);
}

TEST_CASE("marker editor deletes bookmark section and kiai events atomically and undoably",
          "[unit][core][marker-editor]") {
    auto history = document_history_t::from(marker_song());
    REQUIRE(
        apply_marker_edit(history, 4, delete_bookmark_action_t{chart_id_t{2}, object_id_t{12}}));
    CHECK(history.document.charts.front().bookmarks.empty());
    CHECK(validate_document(history.document).empty());

    REQUIRE(apply_marker_edit(history, 5, delete_section_action_t{chart_id_t{2}, object_id_t{13}}));
    CHECK(history.document.charts.front().sections.empty());
    CHECK(validate_document(history.document).empty());

    REQUIRE(
        apply_marker_edit(history, 6, delete_kiai_event_action_t{chart_id_t{2}, object_id_t{11}}));
    CHECK(history.document.charts.front().kiai.empty());
    CHECK(validate_document(history.document).empty());
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().kiai.front().id == object_id_t{11});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().sections.front().id == object_id_t{13});
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().bookmarks.front().id == object_id_t{12});
    CHECK_FALSE(is_document_dirty(history));
}

TEST_CASE("marker editor rejects stale IDs anchors positions ranges labels and caps atomically",
          "[unit][core][marker-editor][negative]") {
    auto history = document_history_t::from(marker_song());
    const auto stale = apply_marker_edit(
        history, 3,
        insert_bookmark_action_t{.chart = chart_id_t{2},
                                 .label = "stale",
                                 .position = local_position_t{measure_id_t{3}, 120}});
    CHECK_FALSE(stale);
    CHECK(has_code(stale, diagnostic_code_t::invalid_id));

    const auto unknown = apply_marker_edit(
        history, 4,
        update_bookmark_action_t{.chart = chart_id_t{2},
                                 .bookmark = object_id_t{99},
                                 .label = "missing",
                                 .position = local_position_t{measure_id_t{3}, 120}});
    CHECK_FALSE(unknown);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));

    const auto wrong_type =
        apply_marker_edit(history, 4, delete_kiai_event_action_t{chart_id_t{2}, object_id_t{12}});
    CHECK_FALSE(wrong_type);
    CHECK(has_code(wrong_type, diagnostic_code_t::invalid_id));

    const auto both_bookmark_anchors = apply_marker_edit(
        history, 4,
        insert_bookmark_action_t{.chart = chart_id_t{2},
                                 .anchor = marker_anchor_t::beat,
                                 .position = local_position_t{measure_id_t{3}, 120},
                                 .audio_milliseconds = 300});
    CHECK_FALSE(both_bookmark_anchors);
    CHECK(has_code(both_bookmark_anchors, diagnostic_code_t::invalid_id));

    const auto missing_bookmark_value = apply_marker_edit(
        history, 4,
        insert_bookmark_action_t{.chart = chart_id_t{2}, .anchor = marker_anchor_t::audio_time});
    CHECK_FALSE(missing_bookmark_value);
    CHECK(has_code(missing_bookmark_value, diagnostic_code_t::invalid_id));

    const auto both_section_anchors = apply_marker_edit(
        history, 4,
        insert_section_action_t{
            .chart = chart_id_t{2},
            .label = "Mixed",
            .anchor = marker_anchor_t::beat,
            .beat_span = position_span_t{{measure_id_t{3}, 120}, {measure_id_t{3}, 240}},
            .audio_millisecond_span = std::pair<std::int64_t, std::int64_t>{0, 1}});
    CHECK_FALSE(both_section_anchors);
    CHECK(has_code(both_section_anchors, diagnostic_code_t::invalid_id));

    const auto bad_beat_range = apply_marker_edit(
        history, 4,
        insert_section_action_t{
            .chart = chart_id_t{2},
            .label = "Backwards",
            .beat_span = position_span_t{{measure_id_t{3}, 240}, {measure_id_t{3}, 120}}});
    CHECK_FALSE(bad_beat_range);
    CHECK(has_code(bad_beat_range, diagnostic_code_t::invalid_hold));

    const auto bad_audio_range = apply_marker_edit(
        history, 4,
        insert_section_action_t{.chart = chart_id_t{2},
                                .label = "Backwards",
                                .anchor = marker_anchor_t::audio_time,
                                .audio_millisecond_span =
                                    std::pair<std::int64_t, std::int64_t>{500, 100}});
    CHECK_FALSE(bad_audio_range);
    CHECK(has_code(bad_audio_range, diagnostic_code_t::invalid_hold));

    const auto bad_position = apply_marker_edit(
        history, 4,
        insert_kiai_event_action_t{
            .chart = chart_id_t{2}, .position = {measure_id_t{99}, 0}, .enabled = true});
    CHECK_FALSE(bad_position);
    CHECK(has_code(bad_position, diagnostic_code_t::row_out_of_range));

    const auto bad_utf8 = apply_marker_edit(
        history, 4,
        insert_section_action_t{.chart = chart_id_t{2},
                                .label = std::string("bad\xc0\xaf", 5),
                                .beat_span =
                                    position_span_t{{measure_id_t{3}, 0}, {measure_id_t{3}, 120}}});
    CHECK_FALSE(bad_utf8);
    CHECK(has_code(bad_utf8, diagnostic_code_t::unsupported_feature));

    const auto event_capped = apply_marker_edit(
        history, 4,
        insert_bookmark_action_t{.chart = chart_id_t{2},
                                 .label = "cap",
                                 .position = local_position_t{measure_id_t{3}, 240}},
        {.maximum_bookmarks = 1});
    CHECK_FALSE(event_capped);
    CHECK(has_code(event_capped, diagnostic_code_t::invalid_resolution));

    const auto text_capped = apply_marker_edit(
        history, 4,
        insert_bookmark_action_t{.chart = chart_id_t{2},
                                 .label = "too long",
                                 .position = local_position_t{measure_id_t{3}, 240}},
        {.maximum_label_bytes = 3});
    CHECK_FALSE(text_capped);
    CHECK(has_code(text_capped, diagnostic_code_t::unsupported_feature));

    CHECK(history.document.revision == 4);
    CHECK(history.document.charts.front().bookmarks.size() == 1);
    CHECK(history.document.charts.front().sections.size() == 1);
    CHECK(history.document.charts.front().kiai.size() == 1);
    CHECK(history.undo.empty());
    CHECK(history.redo.empty());
}

TEST_CASE("marker editor rejects ambiguous chart-wide IDs and object allocation limits",
          "[unit][core][marker-editor][bounds]") {
    auto duplicate_song = marker_song();
    duplicate_song.charts.front().bookmarks.push_back(
        {.id = object_id_t{13},
         .label = "collision",
         .position = local_position_t{measure_id_t{3}, 0}});
    auto duplicate_history = document_history_t::from(std::move(duplicate_song));
    const auto duplicate = apply_marker_edit(
        duplicate_history, 4,
        update_section_action_t{.chart = chart_id_t{2},
                                .section = object_id_t{13},
                                .label = "Renamed",
                                .beat_span =
                                    position_span_t{{measure_id_t{3}, 0}, {measure_id_t{3}, 120}}});
    CHECK_FALSE(duplicate);
    CHECK(has_code(duplicate, diagnostic_code_t::duplicate_id));
    CHECK(duplicate_history.undo.empty());

    auto history = document_history_t::from(marker_song());
    const auto objects_capped = apply_marker_edit(
        history, 4,
        insert_section_action_t{.chart = chart_id_t{2},
                                .label = "Blocked",
                                .beat_span =
                                    position_span_t{{measure_id_t{3}, 0}, {measure_id_t{3}, 120}}},
        {.maximum_chart_objects = 1});
    CHECK_FALSE(objects_capped);
    CHECK(has_code(objects_capped, diagnostic_code_t::invalid_resolution));
    CHECK(history.document.charts.front().sections.size() == 1);
    CHECK(history.undo.empty());
}

namespace {

auto kiai_spans(const document_history_t& history) -> std::vector<kiai_span_t> {
    const auto& chart = history.document.charts.front();
    const auto index = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(index));
    return kiai_spans_of(chart, std::get<measure_index_t>(index));
}

auto edit_kiai(document_history_t& history, kiai_span_edit_t edit) -> document_command_result_t {
    return apply_marker_edit(history, history.document.revision,
                             edit_kiai_span_action_t{.chart = chart_id_t{2}, .edit = edit});
}

} // namespace

TEST_CASE("kiai span edit creates a separate area with fresh events", "[unit][core][marker-editor][kiai]") {
    auto history = document_history_t::from(marker_song());
    REQUIRE(edit_kiai(history, {.rows = {{20, 100}}}));
    const auto spans = kiai_spans(history);
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].begin_row == 20);
    CHECK(spans[0].end_row == 100);
    CHECK(spans[1].head == object_id_t{11});
    CHECK(spans[1].open_ended);
    CHECK(history.document.charts.front().kiai.size() == 3);
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().kiai.size() == 1);
}

TEST_CASE("kiai span edit merges overlaps and drops the swallowed events",
          "[unit][core][marker-editor][kiai]") {
    auto history = document_history_t::from(marker_song());
    REQUIRE(edit_kiai(history, {.rows = {{20, 100}}}));
    // Extend the new area over the open one: everything becomes one open area.
    const auto first = kiai_spans(history).front();
    REQUIRE(edit_kiai(history, {.head = first.head, .rows = {{20, 300}}}));
    const auto spans = kiai_spans(history);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].begin_row == 20);
    CHECK(spans[0].open_ended);
    CHECK(history.document.charts.front().kiai.size() == 1);
    CHECK(history.document.charts.front().kiai.front().id == first.head);
}

TEST_CASE("dragging a kiai head or tail keeps the event identity and effect bits",
          "[unit][core][marker-editor][kiai]") {
    auto history = document_history_t::from(marker_song());
    REQUIRE(edit_kiai(history, {.head = object_id_t{11}, .rows = {{240, 600}}}));
    auto spans = kiai_spans(history);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].end_row == 600);
    CHECK_FALSE(spans[0].open_ended);
    const auto tail = spans[0].tail;
    REQUIRE(edit_kiai(history, {.head = object_id_t{11}, .rows = {{100, 700}}}));
    spans = kiai_spans(history);
    REQUIRE(spans.size() == 1);
    CHECK(spans[0].begin_row == 100);
    CHECK(spans[0].end_row == 700);
    CHECK(spans[0].head == object_id_t{11});
    CHECK(spans[0].tail == tail);
    const auto& events = history.document.charts.front().kiai;
    CHECK(std::ranges::find(events, object_id_t{11}, &kiai_event_t::id)->retained_effect_bits == 9);
}

TEST_CASE("kiai span edit deletes an area and rejects bad input without a revision",
          "[unit][core][marker-editor][kiai]") {
    auto history = document_history_t::from(marker_song());
    REQUIRE(edit_kiai(history, {.head = object_id_t{11}}));
    CHECK(kiai_spans(history).empty());
    CHECK(history.document.charts.front().kiai.empty());

    history = document_history_t::from(marker_song());
    const auto revision = history.document.revision;
    CHECK_FALSE(edit_kiai(history, {.rows = {{100, 100}}}));
    CHECK_FALSE(edit_kiai(history, {.rows = {{100, 5000}}}));
    CHECK_FALSE(edit_kiai(history, {.head = object_id_t{99}, .rows = {{1, 2}}}));
    CHECK_FALSE(edit_kiai(history, {}));
    CHECK(history.document.revision == revision);
    CHECK(history.document.charts.front().kiai.size() == 1);
    // Re-applying the area it already has is a no-op: no revision, no history entry.
    const auto noop = edit_kiai(history, {.head = object_id_t{11}, .rows = {{240, 960}}});
    CHECK(noop.diagnostics.empty());
    CHECK(history.document.revision == revision);
}
