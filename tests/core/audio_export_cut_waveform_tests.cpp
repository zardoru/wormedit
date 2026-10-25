#include <wormedit/audio/aligned_waveform.h>
#include <catch2/catch_test_macros.hpp>
#include <array>
#include <memory>

namespace {
void check_removed_peaks(const wormedit::audio::aligned_waveform_view_result_t& projected) {
    for (const auto& row : projected.rows) CHECK(row.absolute_peak[0] == 0.0F);
}
void check_overview_peaks(const wormedit::audio::waveform_t& waveform, const wormedit::export_cut_frame_timeline_t& frames) {
    const auto overview = wormedit::audio::waveform_overview_view_result_t::from(waveform, frames, 0, 1);
    CHECK_FALSE(overview.status.has_failure());
    for (const auto& row : overview.rows) CHECK(row.absolute_peak == 0.0F);
}
}

TEST_CASE("Waveform seam pixels exclude peaks in removed source audio", "[audio][waveform][export-cut]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    const chart_document_t chart{.id = chart_id_t{1}, .measures = {{.id = measure_id_t{2}}},
        .bpms = {{.id = object_id_t{3}, .position = {measure_id_t{2}, 0}, .beats_per_minute = {120, 1}}}};
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const std::array cuts{export_cut_t{.id = object_id_t{4}, .begin_milliseconds = 250, .end_milliseconds = 1250}};
    const auto timeline = export_cut_timeline_t::from(cuts);
    REQUIRE(timeline.ok());
    const auto axis = export_cut_axis_t::from(*timing.value, timeline.value);
    REQUIRE(axis.ok());
    waveform_t waveform;
    waveform.key.frames_per_bin = 1;
    waveform.sample_rate = 1000;
    waveform.total_frames = 2000;
    waveform.bins.resize(2000);
    for (std::size_t frame = 250; frame < 1250; ++frame)
        waveform.bins[frame].absolute_peak = {1.0F, 1.0F};
    const aligned_waveform_request_t request{.viewport_height_pixels = 2,
        .pixels_per_standard_measure = 4, .cut_axis = std::make_shared<export_cut_axis_t>(axis.value)};
    display_row_table_t table;
    REQUIRE(table.prepare(*timing.value, request));
    const auto projected = aligned_waveform_view_result_t::from(waveform, nullptr, table, request, nullptr, {});
    CHECK_FALSE(projected.rows.empty());
    check_removed_peaks(projected);
    const auto frames = export_cut_frame_timeline_t::from(timeline.value, waveform.sample_rate, waveform.total_frames);
    REQUIRE(frames.ok());
    check_overview_peaks(waveform, frames.value);
}

TEST_CASE("Waveform cuts inside a stop use physical samples even without a row gap", "[audio][waveform][export-cut][stop]") {
    using namespace wormedit;
    using namespace wormedit::audio;
    chart_document_t chart{.id = chart_id_t{1}, .measures = {{.id = measure_id_t{2}}},
        .bpms = {{.id = object_id_t{3}, .position = {measure_id_t{2}, 0}, .beats_per_minute = {120, 1}}}};
    chart.pauses.push_back({.id = object_id_t{5}, .position = {measure_id_t{2}, 0},
        .duration = {1, 1}, .source_unit = pause_unit_t::seconds});
    const auto timing = timing_index_t::from(chart);
    REQUIRE(timing.value);
    const std::array cuts{export_cut_t{.id = object_id_t{4}, .begin_milliseconds = 1, .end_milliseconds = 2}};
    const auto timeline = export_cut_timeline_t::from(cuts);
    const auto axis = export_cut_axis_t::from(*timing.value, timeline.value);
    REQUIRE(axis.ok());
    CHECK(axis.value.is_empty());
    waveform_t waveform;
    waveform.key.frames_per_bin = 1;
    waveform.sample_rate = 44100;
    waveform.total_frames = 2000;
    waveform.bins.resize(2000);
    for (std::size_t frame = 44; frame < 88; ++frame) waveform.bins[frame].absolute_peak = {1, 1};
    const aligned_waveform_request_t request{.viewport_height_pixels = 2,
        .pixels_per_standard_measure = 4, .cut_axis = std::make_shared<export_cut_axis_t>(axis.value)};
    display_row_table_t table;
    REQUIRE(table.prepare(*timing.value, request));
    const auto projected = aligned_waveform_view_result_t::from(waveform, nullptr, table, request, nullptr, {});
    CHECK_FALSE(projected.rows.empty());
    check_removed_peaks(projected);
}
