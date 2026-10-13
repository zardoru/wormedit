#include "sys/audio_source_registration.h"
#include "sys/project_open.h"

#include <wormedit/audio/preview_session.h>
#include <wormedit/audio/audition_resolver.h>
#include <wormedit/core/timing.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <span>
#include <string>
#include <vector>

namespace {

using clock_t = std::chrono::steady_clock;
using namespace wormedit;
using namespace wormedit::audio;

auto frame_time(std::uint64_t frame) -> double {
    return static_cast<double>(frame) / output_sample_rate;
}

auto measure_number(const measure_index_t& measures, signed_rational_t row) -> std::size_t {
    const auto whole_row = row.numerator <= 0 || row.denominator <= 0
                               ? std::uint64_t{0}
                               : static_cast<std::uint64_t>(row.numerator / row.denominator);
    const auto found = std::ranges::upper_bound(
        measures.entries, whole_row, {}, &measure_index_entry_t::absolute_begin_row);
    return found == measures.entries.begin()
               ? 1
               : static_cast<std::size_t>(std::distance(measures.entries.begin(), found));
}

auto pump_status(preview_session_pump_status_t status) -> const char* {
    switch (status) {
    case preview_session_pump_status_t::ready: return "ready";
    case preview_session_pump_status_t::not_started: return "not_started";
    case preview_session_pump_status_t::invalid_timeline: return "invalid_timeline";
    case preview_session_pump_status_t::materialize_failed: return "materialize_failed";
    case preview_session_pump_status_t::decode_failed: return "decode_failed";
    case preview_session_pump_status_t::loop_boundary: return "unknown";
    }
    return "unknown";
}

auto decode_status(preview_pump_status_t status) -> const char* {
    switch (status) {
    case preview_pump_status_t::ready: return "ready";
    case preview_pump_status_t::stale_generation: return "stale_generation";
    case preview_pump_status_t::invalid_request: return "invalid_request";
    case preview_pump_status_t::capacity_exceeded: return "capacity_exceeded";
    case preview_pump_status_t::source_unavailable: return "source_unavailable";
    case preview_pump_status_t::decode_failed: return "decode_failed";
    }
    return "unknown";
}

auto block_failure_status(const block_decode_result_t& failure) -> const char* {
    switch (failure.status) {
    case block_decode_status_t::cache_hit: return "cache_hit";
    case block_decode_status_t::decoded: return "decoded";
    case block_decode_status_t::stale_generation: return "stale_generation";
    case block_decode_status_t::invalid_key: return "invalid_key";
    case block_decode_status_t::decoder_unavailable: return "decoder_unavailable";
    case block_decode_status_t::beyond_end: return "beyond_end";
    case block_decode_status_t::seek_failed: return "seek_failed";
    case block_decode_status_t::decode_failed: return "decode_failed";
    case block_decode_status_t::cache_capacity_exceeded: return "cache_capacity_exceeded";
    case block_decode_status_t::session_queue_full: return "session_queue_full";
    }
    return "unknown";
}

auto percentile(std::vector<double> values, double fraction) -> double {
    if (values.empty()) return 0.0;
    std::ranges::sort(values);
    const auto index = static_cast<std::size_t>(std::ceil(fraction * values.size())) - 1;
    return values[std::min(index, values.size() - 1)];
}

void print_pump(const preview_session_pump_result_t& pumped, double elapsed_ms) {
    std::cout << "pump status=" << pump_status(pumped.status);
    if (pumped.decode) {
        std::cout << " decode=" << decode_status(pumped.decode->status)
                  << " active_voices=" << pumped.decode->active_voices
                  << " overlapping_voices=" << pumped.decode->overlapping_voices
                  << " requested_blocks=" << pumped.decode->requested_blocks
                  << " decoded_blocks=" << pumped.decode->decoded_blocks
                  << " cached_blocks=" << pumped.decode->cached_blocks;
        if (pumped.decode->failure)
            std::cout << " failure=" << block_failure_status(pumped.decode->failure.value());
    }
    std::cout << " wall_ms=" << elapsed_ms;
}

struct run_result_t {
    std::vector<double> pump_ms;
    bool underrun{};
    bool complete{};
};

struct render_result_t {
    bool underrun{};
    bool complete{};
    bool stopped{};
};

auto render_elapsed(preview_session_t& session, std::span<float> scratch,
                    std::uint64_t frames_to_render, const timing_index_t& timing,
                    const measure_index_t& measures, const preview_session_pump_result_t& pumped,
                    double wall_ms) -> render_result_t {
    constexpr std::size_t callback_frames = 4'096;
    render_result_t result;
    while (frames_to_render > 0 && session.active()) {
        const auto frames = std::min<std::uint64_t>(frames_to_render, callback_frames);
        const auto before = session.timeline_frame();
        const auto samples = static_cast<std::size_t>(frames) * output_channel_count;
        const auto rendered = session.render(scratch.first(samples));
        if (rendered.status == preview_session_render_status_t::underrun) {
            const auto position = musical_position_at(
                timing, {static_cast<std::int64_t>(before), output_sample_rate});
            const auto row = position.absolute_row.value_or(signed_rational_t{0, 1});
            std::cout << "UNDERRUN frame=" << before << " seconds=" << frame_time(before)
                      << " measure=" << measure_number(measures, row)
                      << " missing_blocks=" << rendered.mix.missing_blocks
                      << " active_voices=" << (pumped.decode ? pumped.decode->active_voices : 0)
                      << " ";
            print_pump(pumped, wall_ms);
            std::cout << '\n';
            result.underrun = true;
            static_cast<void>(session.stop());
            break;
        }
        if (rendered.status == preview_session_render_status_t::complete) {
            result.complete = true;
            break;
        }
        if (rendered.status != preview_session_render_status_t::mixed) {
            std::cout << "render status=" << static_cast<int>(rendered.status)
                      << " at frame=" << before << '\n';
            result.stopped = true;
            static_cast<void>(session.stop());
            break;
        }
        frames_to_render -= frames;
    }
    return result;
}

void report_pump_failure(const preview_session_pump_result_t& pumped, double wall_ms,
                         std::size_t iteration) {
    if (pumped) return;
    std::cout << "PUMP_FAILURE iteration=" << iteration << " ";
    print_pump(pumped, wall_ms);
    std::cout << '\n';
}

void report_summary(const char* label, const run_result_t& result) {
    const auto total = std::accumulate(result.pump_ms.begin(), result.pump_ms.end(), 0.0);
    const auto maximum = result.pump_ms.empty() ? 0.0 : *std::ranges::max_element(result.pump_ms);
    std::cout << "SUMMARY mode=" << label << " result="
              << (result.underrun ? "underrun" : result.complete ? "complete" : "stopped")
              << " pumps=" << result.pump_ms.size() << " mean_pump_ms="
              << (result.pump_ms.empty() ? 0.0 : total / result.pump_ms.size())
              << " p95_pump_ms=" << percentile(result.pump_ms, 0.95)
              << " max_pump_ms=" << maximum << '\n';
}

auto run_mode(const char* label, bool charge_pump_time, const std::filesystem::path& chart_path,
              const std::filesystem::path& asset_base, const song_document_t& document,
              const preview_schedule_t& schedule, const timing_index_t& timing,
              const measure_index_t& measures,
              std::span<const preview_object_source_binding_t> bindings,
              std::uint64_t end_frame) -> run_result_t {
    decoded_source_registry_t sources;
    std::vector<preview_metronome_source_binding_t> runtime_metronome;
    static_cast<void>(sys::register_audition_sources(document, asset_base, 1, sources,
                                                     runtime_metronome));
    decoded_block_cache_t cache{preview_block_cache_limits()};
    transport_t transport{cache};
    preview_transport_bridge_t bridge{transport};
    preview_session_t session{sources, transport, bridge};
    const auto started = session.start(schedule, bindings, runtime_metronome, 0, {}, end_frame);
    REQUIRE(started);
    constexpr double cadence_seconds = 0.0167;
    constexpr std::size_t callback_frames = 4'096;
    std::vector<float> scratch(callback_frames * output_channel_count);
    double simulated_seconds{};
    std::uint64_t simulated_frames{};
    std::size_t iteration{};
    run_result_t result;
    std::cout << "\nMODE " << label << " chart=" << chart_path << " start_frame=0"
              << " schedule_events=" << schedule.playable.size() + schedule.bgm.size()
              << " bound_objects=" << bindings.size() << " end_frame=" << end_frame << '\n';

    while (session.active()) {
        const auto pump_start = clock_t::now();
        const auto pumped = session.pump();
        const auto wall_ms = std::chrono::duration<double, std::milli>(
                                 clock_t::now() - pump_start).count();
        result.pump_ms.push_back(wall_ms);
        const auto step_seconds = cadence_seconds + (charge_pump_time ? wall_ms / 1'000.0 : 0.0);
        simulated_seconds += step_seconds;
        const auto target_frame = static_cast<std::uint64_t>(std::llround(
            simulated_seconds * static_cast<double>(output_sample_rate)));
        const auto frames_to_render = target_frame - simulated_frames;
        simulated_frames = target_frame;
        const auto rendered = render_elapsed(session, scratch, frames_to_render, timing, measures,
                                             pumped, wall_ms);
        result.underrun = rendered.underrun;
        result.complete = rendered.complete;
        ++iteration;
        report_pump_failure(pumped, wall_ms, iteration);
        if (iteration > 10'000'000) {
            std::cout << "iteration safety stop\n";
            static_cast<void>(session.stop());
            break;
        }
    }
    report_summary(label, result);
    return result;
}

struct repro_chart_t {
    std::filesystem::path path;
    std::filesystem::path asset_base;
    song_document_t document;
    preview_schedule_t schedule;
    timing_index_t timing;
    measure_index_t measures;
    std::vector<preview_object_source_binding_t> bindings;
    std::vector<preview_metronome_source_binding_t> metronome;
    std::uint64_t end_frame{};
};

auto preview_end_frame(const preview_schedule_t& schedule, const timing_index_t& timing)
    -> std::uint64_t {
    auto end_frame = static_cast<std::uint64_t>(output_sample_rate);
    if (!timing.rows.empty()) {
        const auto chart_end = sample_frame_at(timing.rows.back().after_stops_seconds,
                                               output_sample_rate);
        if (chart_end) end_frame = std::max(end_frame, *chart_end.frame);
    }
    for (const auto& event : schedule.bga)
        end_frame = std::max(end_frame, event.sample_frame);
    for (const auto& event : schedule.metronome)
        end_frame = std::max(end_frame, event.sample_frame);
    return end_frame;
}

void resolve_repro_bindings(repro_chart_t& chart) {
    decoded_source_registry_t registry;
    static_cast<void>(sys::register_audition_sources(
        chart.document, chart.asset_base, 1, registry, chart.metronome));
    const auto resolved = resolve_preview_bindings(
        chart.document, chart.document.charts.front().id, chart.schedule, registry.bindings(),
        {.skip_unavailable_preview_sources = true});
    REQUIRE(resolved);
    chart.bindings = resolved.bindings;
}

auto load_repro_chart(const std::filesystem::path& path) -> repro_chart_t {
    const auto opened = sys::open_project_file(path);
    REQUIRE(opened);
    REQUIRE(opened.document);
    REQUIRE_FALSE(opened.document->charts.empty());
    auto result = repro_chart_t{};
    result.path = path;
    result.asset_base = opened.asset_base;
    result.document = *opened.document;
    const auto& chart = result.document.charts.front();
    const auto measure_build = build_measure_index(chart);
    REQUIRE(std::holds_alternative<measure_index_t>(measure_build));
    result.measures = std::get<measure_index_t>(measure_build);
    const auto timing_build = build_timing_index(chart);
    REQUIRE(timing_build);
    result.timing = *timing_build.index;
    const auto schedule_build = build_preview_schedule(
        chart, result.timing,
        {.sample_rate = output_sample_rate, .include_metronome = true});
    REQUIRE(schedule_build);
    result.schedule = *schedule_build.schedule;

    resolve_repro_bindings(result);
    result.end_frame = preview_end_frame(result.schedule, result.timing);
    return result;
}

} // namespace

TEST_CASE("reproduce chart preview cache underruns with simulated callback cadence", "[.repro][audio]") {
    const auto* configured_path = std::getenv("WORMEDIT_REPRO_CHART");
    if (configured_path == nullptr || configured_path[0] == '\0') {
        std::cout << "SKIP: set WORMEDIT_REPRO_CHART to a chart path\n";
        SUCCEED("WORMEDIT_REPRO_CHART is unset");
        return;
    }
    auto chart = load_repro_chart(configured_path);
    std::cout << "OPENED path=" << chart.path << " charts=" << chart.document.charts.size()
              << " assets=" << chart.document.audio_assets.size()
              << " selected_chart=" << chart.document.charts.front().id.value
              << " bindings=" << chart.bindings.size() << '\n';
    static_cast<void>(run_mode("fixed-16.7ms", false, chart.path, chart.asset_base,
                               chart.document, chart.schedule, chart.timing, chart.measures,
                               chart.bindings, chart.end_frame));
    static_cast<void>(run_mode("charge-pump-wall-time", true, chart.path, chart.asset_base,
                               chart.document, chart.schedule, chart.timing, chart.measures,
                               chart.bindings, chart.end_frame));
}
