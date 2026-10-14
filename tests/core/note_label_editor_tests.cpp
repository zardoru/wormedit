#include <wormedit/core/note_label_editor.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <ranges>
#include <string>
#include <utility>

using namespace wormedit;

namespace {

auto label_song() -> song_document_t {
    song_document_t song{
        .id = song_id_t{1},
        .revision = 4,
        .charts = {{
            .id = chart_id_t{2},
            .divisions_per_standard_measure = 960,
            .measures = {{
                .id = measure_id_t{3},
                .notes = {{.id = object_id_t{10},
                           .position = {measure_id_t{3}, 120},
                           .lane = lane_id_t{1},
                           .label = "old"},
                          {.id = object_id_t{11},
                           .position = {measure_id_t{3}, 240},
                           .lane = lane_id_t{1}}},
            }},
            .lane_layouts =
                {{.id = object_id_t{20},
                  .position = {measure_id_t{3}, 0},
                  .layout = {.profile_id = "one-key",
                             .lanes = {{.id = lane_id_t{1}, .name = "Key", .display_order = 0}}}}},
            .input_profile = {.profile_id = "one-key",
                              .bindings = {{.key = physical_key_t::digit_1, .lane = lane_id_t{1}}}},
            .bpms = {{.id = object_id_t{21},
                      .position = {measure_id_t{3}, 0},
                      .beats_per_minute = {120, 1}}},
        }},
    };
    REQUIRE(validate_document(song).empty());
    return song;
}

auto has_code(const document_command_result_t& result, diagnostic_code_t code) -> bool {
    return std::ranges::any_of(result.diagnostics,
                               [code](const auto& diagnostic) { return diagnostic.code == code; });
}

} // namespace

TEST_CASE("note labels apply atomically to an exact selection and preserve gameplay data",
          "[unit][core][note-label-editor]") {
    auto history = document_history_t::from(label_song());
    const auto before = history.document.charts.front().measures.front().notes;
    const auto applied = apply_note_label_edit(
        history,
        {.chart = chart_id_t{2}, .notes = {object_id_t{10}, object_id_t{11}}, .label = "vocal"});
    REQUIRE(applied);
    const auto& notes = history.document.charts.front().measures.front().notes;
    CHECK(notes[0].label == "vocal");
    CHECK(notes[1].label == "vocal");
    CHECK(notes[0].position == before[0].position);
    CHECK(notes[0].lane == before[0].lane);
    CHECK(notes[0].kind == before[0].kind);
    CHECK(notes[0].provenance.family == before[0].provenance.family);
    CHECK(notes[0].provenance.profile == before[0].provenance.profile);
    CHECK(notes[0].provenance.source_field == before[0].provenance.source_field);
    CHECK(notes[0].provenance.source_spelling == before[0].provenance.source_spelling);
    CHECK(notes[0].provenance.source_order == before[0].provenance.source_order);

    const auto no_op = apply_note_label_edit(
        history,
        {.chart = chart_id_t{2}, .notes = {object_id_t{10}, object_id_t{11}}, .label = "vocal"});
    CHECK_FALSE(no_op.applied);
    CHECK(no_op.diagnostics.empty());
    CHECK(history.document.revision == 5);

    const auto cleared = apply_note_label_edit(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .label = std::nullopt});
    REQUIRE(cleared);
    CHECK_FALSE(history.document.charts.front().measures.front().notes[0].label.has_value());
    CHECK(history.document.charts.front().measures.front().notes[1].label == "vocal");
    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().measures.front().notes[0].label == "vocal");
    REQUIRE(redo_document_command(history));
    CHECK_FALSE(history.document.charts.front().measures.front().notes[0].label.has_value());
    CHECK(is_document_dirty(history));
}

TEST_CASE("note-label editing rejects unknown duplicate and malformed requests atomically",
          "[unit][core][note-label-editor][bounds]") {
    auto history = document_history_t::from(label_song());
    const auto check_unchanged = [&] {
        CHECK(history.document.revision == 4);
        CHECK(history.document.charts.front().measures.front().notes[0].label == "old");
        CHECK_FALSE(history.document.charts.front().measures.front().notes[1].label.has_value());
        CHECK(history.undo.empty());
    };

    const auto empty =
        apply_note_label_edit(history, {.chart = chart_id_t{2}, .notes = {}, .label = "new"});
    CHECK_FALSE(empty);
    CHECK(has_code(empty, diagnostic_code_t::invalid_resolution));
    const auto unknown = apply_note_label_edit(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{99}}, .label = "new"});
    CHECK_FALSE(unknown);
    CHECK(has_code(unknown, diagnostic_code_t::invalid_id));
    const auto duplicate_request = apply_note_label_edit(
        history,
        {.chart = chart_id_t{2}, .notes = {object_id_t{10}, object_id_t{10}}, .label = "new"});
    CHECK_FALSE(duplicate_request);
    CHECK(has_code(duplicate_request, diagnostic_code_t::duplicate_id));
    const auto embedded_nul =
        apply_note_label_edit(history, {.chart = chart_id_t{2},
                                        .notes = {object_id_t{10}},
                                        .label = std::string{"bad\0label", 9}});
    CHECK_FALSE(embedded_nul);
    CHECK(has_code(embedded_nul, diagnostic_code_t::unsupported_feature));
    const auto malformed_utf8 = apply_note_label_edit(
        history,
        {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .label = std::string{"\xc0\x80", 2}});
    CHECK_FALSE(malformed_utf8);
    CHECK(has_code(malformed_utf8, diagnostic_code_t::unsupported_feature));
    const auto capped = apply_note_label_edit(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .label = "12345"},
        {.maximum_label_bytes = 4});
    CHECK_FALSE(capped);
    CHECK(has_code(capped, diagnostic_code_t::unsupported_feature));
    const auto object_capped = apply_note_label_edit(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .label = "new"},
        {.maximum_chart_objects = 2});
    CHECK_FALSE(object_capped);
    CHECK(has_code(object_capped, diagnostic_code_t::invalid_resolution));
    check_unchanged();
}

TEST_CASE("note-label editing enforces aggregate bytes and chart-wide identity",
          "[unit][core][note-label-editor][bounds]") {
    auto history = document_history_t::from(label_song());
    const auto aggregate = apply_note_label_edit(
        history, {.chart = chart_id_t{2}, .notes = {object_id_t{11}}, .label = "four"},
        {.maximum_total_label_bytes = 6});
    CHECK_FALSE(aggregate);
    CHECK(has_code(aggregate, diagnostic_code_t::invalid_resolution));
    CHECK(history.document.revision == 4);

    auto ambiguous_song = label_song();
    ambiguous_song.charts.front().bpms.front().id = object_id_t{10};
    auto ambiguous_history = document_history_t::from(std::move(ambiguous_song));
    const auto ambiguous = apply_note_label_edit(
        ambiguous_history, {.chart = chart_id_t{2}, .notes = {object_id_t{10}}, .label = "new"});
    CHECK_FALSE(ambiguous);
    CHECK(has_code(ambiguous, diagnostic_code_t::duplicate_id));
    CHECK(ambiguous_history.document.charts.front().measures.front().notes[0].label == "old");
    CHECK(ambiguous_history.undo.empty());
}

TEST_CASE("note-label editing stays bounded for fifty thousand selected notes",
          "[unit][core][note-label-editor][stress]") {
    auto song = label_song();
    auto& chart = song.charts.front();
    chart.divisions_per_standard_measure = 100'000;
    auto& notes = chart.measures.front().notes;
    notes.clear();
    notes.reserve(50'000);
    std::vector<object_id_t> selected;
    selected.reserve(50'000);
    for (std::uint64_t index = 0; index < 50'000; ++index) {
        const auto id = object_id_t{1'000 + index};
        notes.push_back({.id = id, .position = {measure_id_t{3}, index * 2}, .lane = lane_id_t{1}});
        selected.push_back(id);
    }
    REQUIRE(validate_document(song).empty());
    auto history = document_history_t::from(std::move(song));

    const auto started = std::chrono::steady_clock::now();
    const auto applied = apply_note_label_edit(
        history, {.chart = chart_id_t{2}, .notes = std::move(selected), .label = "vocal"});
    const auto elapsed = std::chrono::steady_clock::now() - started;
    REQUIRE(applied);
    CHECK(history.document.charts.front().measures.front().notes.size() == 50'000);
    CHECK(history.document.charts.front().measures.front().notes.back().label == "vocal");
    const auto milliseconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    INFO("50k note-label assignment took " << milliseconds << " ms");
    if (milliseconds > 125) {
        WARN("50k note-label assignment exceeded the 125 ms interaction target on this host");
    }
    CHECK(milliseconds < 500);
}

TEST_CASE("label search is case-insensitive, ordered by chart position, and read-only",
          "[unit][core][note-label-editor][search]") {
    auto song = label_song();
    auto& notes = song.charts.front().measures.front().notes;
    notes[0].label = "Vocal Chop";
    notes[1].label = "bass";
    notes.push_back({.id = object_id_t{12},
                     .position = {measure_id_t{3}, 60},
                     .lane = lane_id_t{1},
                     .label = "VOCAL"});
    notes.push_back({.id = object_id_t{13},
                     .position = {measure_id_t{3}, 480},
                     .lane = lane_id_t{1}});
    const auto before = song;

    const auto vocal = find_notes_by_label(song.charts.front(), "vocal");
    REQUIRE(vocal.size() == 2);
    CHECK(vocal[0] == object_id_t{12});
    CHECK(vocal[1] == object_id_t{10});
    CHECK(find_notes_by_label(song.charts.front(), "ASS") == std::vector{object_id_t{11}});
    CHECK(find_notes_by_label(song.charts.front(), "missing").empty());
    CHECK(find_notes_by_label(song.charts.front(), "").empty());
    CHECK(song.charts.front().measures.front().notes.size() == before.charts.front().measures.front().notes.size());
    CHECK(song.charts.front().measures.front().notes[0].label == before.charts.front().measures.front().notes[0].label);
}
