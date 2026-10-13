#include <wormedit/core/chart_clipboard.h>

#include <catch2/catch_test_macros.hpp>

using namespace wormedit;

namespace {
auto clipboard_timing_song() -> song_document_t {
    song_document_t song;
    song.id = song_id_t{1};
    song.revision = 3;
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 960;
    chart.measures = {{.id = measure_id_t{1}}, {.id = measure_id_t{2}}};
    chart.bpms.push_back({.id = object_id_t{1}, .position = {measure_id_t{1}, 0},
                          .beats_per_minute = {120, 1}});
    chart.speeds.push_back({.id = object_id_t{2}, .position = {measure_id_t{1}, 240},
                            .multiplier = {2, 1}});
    chart.kiai.push_back({.id = object_id_t{3}, .position = {measure_id_t{1}, 480}, .enabled = true});
    chart.kiai.push_back({.id = object_id_t{4}, .position = {measure_id_t{1}, 720}, .enabled = false});
    song.charts.push_back(std::move(chart));
    return song;
}
} // namespace

TEST_CASE("A selected Kiai area and speed marker are copied and cut together",
          "[unit][sys][clipboard]") {
    auto history = document_history_t::from(clipboard_timing_song());
    editor_interaction_state_t state;
    state.chart = chart_id_t{1};
    state.selected_objects = {{editor_selection_object_kind_t::speed_event, object_id_t{2}}};
    std::uint64_t kiai_head = 3;

    const auto copied = copy_chart_selection(history.document, state, history.document.revision,
                                              object_id_t{kiai_head});
    REQUIRE(static_cast<bool>(copied));
    CHECK(copied.clipboard->objects.size() == 3);

    const auto undo_before = history.undo.size();
    const auto cut = cut_chart_selection(history, state, history.document.revision,
                                          {.kiai_head = object_id_t{kiai_head}});
    CHECK(cut.error.empty());
    CHECK(history.undo.size() == undo_before + 1);
    const auto& chart = history.document.charts.front();
    CHECK(chart.speeds.empty());
    CHECK(chart.kiai.empty());
    CHECK(chart.bpms.size() == 1);

    REQUIRE(undo_document_command(history));
    CHECK(history.document.charts.front().speeds.size() == 1);
    CHECK(history.document.charts.front().kiai.size() == 2);
    REQUIRE(redo_document_command(history));

    const auto pasted = paste_chart_clipboard(history, chart_id_t{1}, *copied.clipboard,
                                              {measure_id_t{2}, 0}, history.document.revision);
    CHECK(pasted.diagnostics.empty());
    const auto& after = history.document.charts.front();
    CHECK(after.speeds.size() == 1);
    CHECK(after.kiai.size() == 2);
    CHECK(after.kiai.front().enabled != after.kiai.back().enabled);
}
