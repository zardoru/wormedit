#include <wormedit/audio/waveform.h>
#include <wormedit/core/editor_scene.h>
#include <wormedit/core/timing.h>
#include <wormedit/core/interaction_queries.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace {
using namespace wormedit;

// Deliberately opt-in: run a Release build with [.performance]. Data generation,
// decoder setup and correctness assertions are outside each measured interval.
// Each phase reports the maximum of five fresh runs, not a cache-hit average.
// Baselines below are milliseconds measured on 2026-10-02: GCC 14.2 Release,
// AMD EPYC 9V74 cloud VM (5 vCPUs), with no concurrent build. Budgets are 4x.
// This is CPU preparation/analysis only, not a UI/GPU or real-device benchmark.
template <typename Work, typename Verify>
void check_budget(std::string_view phase, double budget_ms, Work work, Verify verify) {
    double maximum_ms{};
    for (int run = 0; run < 5; ++run) {
        const auto start = std::chrono::steady_clock::now();
        const auto result = work();
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        maximum_ms = std::max(maximum_ms, elapsed);
        verify(result);
    }
    std::cout << phase << ": max-of-5=" << maximum_ms << " ms; budget="
              << budget_ms << " ms\n";
    CAPTURE(phase, maximum_ms, budget_ms);
    CHECK(maximum_ms <= budget_ms);
}

auto large_chart() -> chart_document_t {
    chart_document_t chart;
    chart.id = chart_id_t{1};
    chart.divisions_per_standard_measure = 1000;
    lane_layout_t layout{.profile_id = "performance-8k", .display_name = "Performance 8K"};
    for (std::uint32_t lane = 0; lane < 8; ++lane)
        layout.lanes.push_back({.id = lane_id_t{lane + 1},
                                .name = std::to_string(lane + 1), .display_order = lane});
    chart.lane_layouts.push_back({.id = object_id_t{50'001},
                                  .position = {measure_id_t{1}, 0}, .layout = std::move(layout)});
    for (std::uint64_t measure = 0; measure < 400; ++measure) {
        measure_chunk_t chunk{.id = measure_id_t{measure + 1}};
        for (std::uint64_t note = 0; note < 125; ++note)
            chunk.notes.push_back({.id = object_id_t{measure * 125 + note + 1},
                                    .position = {chunk.id, note * 8},
                                    .lane = lane_id_t{note % 8 + 1}});
        chart.measures.push_back(std::move(chunk));
        // One timing event per measure; at 160 BPM the chart lasts 600 seconds.
        chart.bpms.push_back({.id = object_id_t{50'002 + measure},
                              .position = {measure_id_t{measure + 1}, 0},
                              .beats_per_minute = {160, 1}});
    }
    return chart;
}

void verify_hit_index(const std::variant<editor_hit_test_index_t, diagnostic_list_t>& result) {
    REQUIRE(std::holds_alternative<editor_hit_test_index_t>(result));
    CHECK(std::get<editor_hit_test_index_t>(result).note_spans.size() == 50'000);
}

void verify_timing_index(const timing_build_outcome_t& result) {
    REQUIRE(result.value);
    CHECK(result.value->total_rows == 400'000);
    const auto end = result.value->seconds_at(400'000);
    REQUIRE(end);
    CHECK(*end.seconds == (signed_rational_t{600, 1}));
}

void verify_scene(const editor_field_scene_view_result_t& result) {
    REQUIRE(result);
    CHECK(result.scene->note_endpoints.size() >= 500);
    CHECK(result.scene->note_endpoints.size() <= 625);
}

void check_chart_budgets(const chart_document_t& chart) {
    check_budget("hit-test index", 4.0 * 6.54204,
        [&] { return build_editor_hit_test_index(chart, 1); }, verify_hit_index);
    check_budget("timing index", 4.0 * 0.145229,
        [&] { return timing_index_t::from(chart); }, verify_timing_index);

    const auto built = build_editor_field_scene_index(chart, 1);
    REQUIRE(std::holds_alternative<editor_field_scene_index_t>(built));
    const auto& index = std::get<editor_field_scene_index_t>(built);
    REQUIRE(index.notes.size() == 50'000);
    const editor_interaction_view_model_t view{
        .chart = chart.id, .active_lane = lane_id_t{1},
        .cursor = {measure_id_t{201}, 0}, .active_snap_denominator = 4,
        .document_revision = 1, .pixels_per_standard_measure = 192.0,
        .viewport_scroll_pixels = 200.0 * 192.0, .viewport_height_pixels = 768.0};
    check_budget(
        "scene composition", 4.0 * 0.099119,
        [&] {
            return editor_field_scene_view_t::from(chart, index, view,
                                                   {.field_width_pixels = 640.0});
        },
        verify_scene);
}

void verify_waveform_geometry(const audio::waveform_t& waveform) {
    constexpr std::uint64_t frames = 600ULL * audio::output_sample_rate;
    CHECK(waveform.total_frames == frames);
    REQUIRE(waveform.bins.size() == (frames + 255) / 256);
    REQUIRE_FALSE(waveform.coarser_levels.empty());
    REQUIRE(waveform.coarser_levels.back().bins.size() == 1);
    CHECK(waveform.coarser_levels.back().bins.front().frame_count == frames);
    CHECK(waveform.bins.front().absolute_peak[0] == 0.25F);
}

void verify_waveform(const audio::waveform_analysis_outcome_t& result) {
    REQUIRE(result.value().status == audio::waveform_analysis_phase_t::generated);
    REQUIRE(audio::waveform_analysis_t::find_waveform(result));
    verify_waveform_geometry(*audio::waveform_analysis_t::find_waveform(result));
}

void check_waveform_budget() {
    constexpr std::uint64_t frames = 600ULL * audio::output_sample_rate;
    audio::decoder_t decoder;
    {
        std::vector<float> pcm(frames * audio::output_channel_count);
        for (std::size_t sample = 0; sample < pcm.size(); sample += 2) {
            const auto value = (sample / 2 % 100 < 50) ? 0.25F : -0.25F;
            pcm[sample] = value;
            pcm[sample + 1] = -value;
        }
        REQUIRE(decoder.open_pcm(pcm));
    }
    check_budget("waveform pyramid", 4.0 * 126.289,
        [&] {
            // Fresh caches force complete decoding/analysis on every run.
            audio::decoded_block_cache_t blocks;
            audio::waveform_cache_t waveforms;
            return audio::waveform_analysis_t::from(decoder, blocks, waveforms,
                                           {"performance:600s", "generated-square-v1"}, 256);
        },
        verify_waveform);
}
} // namespace

TEST_CASE("large document performance budgets", "[.performance]") {
    const auto chart = large_chart();
    REQUIRE(chart.measures.size() == 400);
    check_chart_budgets(chart);
    check_waveform_budget();
}
