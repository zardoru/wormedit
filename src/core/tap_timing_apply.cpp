#include "timing_event_internal.h"
#include <wormedit/core/rational.h>
#include "timing_arithmetic.h"
#include "../core/checked_math.h"
#include <wormedit/core/tap_timing_apply.h>

#include <wormedit/core/bpm_editor.h>
#include <wormedit/core/measure.h>
#include <wormedit/core/measure_editor.h>
#include <wormedit/core/timing.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <ranges>
#include <tuple>
#include <utility>

namespace wormedit {
namespace {
using wormedit::reject;

void reject(tap_timing_apply_proposal_result_t& result, diagnostic_code_t code,
            const char* message) {
    result.diagnostics.push_back(error(code, message));
}

using checked_math::magnitude;

auto selected_bpm(const tap_tempo_estimate_t& estimate, tap_bpm_choice_t choice)
    -> std::optional<signed_rational_t> {
    if (estimate.rational_bpm.numerator == 0 || estimate.rational_bpm.denominator == 0 ||
        estimate.rational_bpm.numerator >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
        return std::nullopt;
    const signed_rational_t raw{static_cast<std::int64_t>(estimate.rational_bpm.numerator),
                                estimate.rational_bpm.denominator};
    switch (choice) {
    case tap_bpm_choice_t::raw:
        return raw;
    case tap_bpm_choice_t::nearest_one:
        return quantize_bpm(raw, {1, 1});
    case tap_bpm_choice_t::nearest_half:
        return quantize_bpm(raw, {1, 2});
    }
    return std::nullopt;
}

auto nearest_even_frame(long double frame) -> std::optional<std::int64_t> {
    constexpr auto minimum = static_cast<long double>(std::numeric_limits<std::int64_t>::min());
    constexpr auto upper_exclusive = -minimum;
    if (!std::isfinite(frame) || frame < minimum || frame >= upper_exclusive) return std::nullopt;
    const auto lower = std::floor(frame);
    const auto fraction = frame - lower;
    auto rounded = lower;
    if (fraction > 0.5L || (fraction == 0.5L && std::fmod(std::abs(lower), 2.0L) == 1.0L))
        rounded += 1.0L;
    if (rounded < minimum || rounded >= upper_exclusive) return std::nullopt;
    return static_cast<std::int64_t>(rounded);
}

auto offset_frame(const chart_document_t& chart, std::uint32_t sample_rate)
    -> std::optional<long double> {
    if (chart.beat_zero_audio_offset_seconds.denominator == 0) return std::nullopt;
    const auto frame = to_long_double(chart.beat_zero_audio_offset_seconds) * sample_rate;
    return std::isfinite(frame) ? std::optional<long double>{frame} : std::nullopt;
}

auto nearest_measure_downbeat(const chart_document_t& chart, std::uint32_t sample_rate,
                              std::uint16_t,
                              const tap_tempo_estimate_t& estimate,
                              signed_rational_t bpm) -> std::optional<std::int64_t> {
    const auto reference = offset_frame(chart, sample_rate);
    if (!reference || !nearest_even_frame(estimate.phase_frame)) return std::nullopt;
    if (chart.measures.empty()) return std::nullopt;
    const auto& first = chart.measures.front().definition;
    if (!validate_measure(chart.divisions_per_standard_measure, first)) return std::nullopt;
    // Apply changes the initial segment: chart-start measure length determines
    // its bar duration in quarter beats, independent of tap-circle subdivisions.
    const auto measure_frames = static_cast<long double>(sample_rate) * 240.0L * bpm.denominator *
        first.length.numerator / first.length.denominator / bpm.numerator;
    if (!std::isfinite(measure_frames) || measure_frames <= 0.0L) return std::nullopt;
    const auto phase = static_cast<long double>(estimate.phase_frame);
    const auto grid_index = (*reference - phase) / measure_frames;
    if (!std::isfinite(grid_index)) return std::nullopt;
    const auto earlier_index = std::floor(grid_index);
    const auto earlier = phase + earlier_index * measure_frames;
    const auto later = earlier + measure_frames;
    if (!std::isfinite(earlier) || !std::isfinite(later)) return std::nullopt;
    // Equal distances select the earlier grid boundary, including negative time.
    const auto selected = *reference - earlier <= later - *reference ? earlier : later;
    return nearest_even_frame(selected);
}

auto exact_offset(std::int64_t frame, std::uint32_t sample_rate) -> signed_rational_t {
    return *timing_arithmetic::frames_to_seconds(frame, sample_rate);
}



auto next_object_id(const chart_document_t& chart) -> std::optional<object_id_t> {
    auto maximum = std::uint64_t{};
    const auto scan = [&maximum](const auto& values) {
        for (const auto& value : values) maximum = std::max(maximum, value.id.value);
    };
    scan(chart.bgm_source_lanes);
    scan(chart.lane_layouts);
    scan(chart.bpms);
    scan(chart.pauses);
    scan(chart.warps);
    scan(chart.scrolls);
    scan(chart.speeds);
    scan(chart.kiai);
    scan(chart.timing_anchors);
    scan(chart.bookmarks);
    scan(chart.sections);
    scan(chart.bga_events);
    scan(chart.sound_channels);
    for (const auto& channel : chart.sound_channels) scan(channel.events);
    for (const auto& measure : chart.measures) {
        scan(measure.notes);
        scan(measure.bgm_events);
    }
    if (maximum == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    return object_id_t{maximum + 1};
}

auto build_proposal(const song_document_t& document, chart_id_t chart_id,
                    const tap_tempo_estimate_t& estimate, std::uint32_t sample_rate,
                    std::uint16_t beats_per_measure, tap_bpm_choice_t choice,
                    tap_timing_apply_proposal_result_t& result)
    -> std::optional<tap_timing_apply_proposal_t> {
    if (!estimate.stable) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "tap timing estimate is not stable");
        return std::nullopt;
    }
    if (sample_rate == 0 || beats_per_measure == 0 || beats_per_measure > maximum_tap_beats_per_measure) {
        reject(result, diagnostic_code_t::invalid_resolution,
               "tap timing apply requires a valid sample rate and meter");
        return std::nullopt;
    }
    if (!std::isfinite(estimate.bpm) || estimate.bpm <= 0.0 ||
        !std::isfinite(estimate.phase_frame)) {
        reject(result, diagnostic_code_t::invalid_rational,
               "tap timing estimate contains an invalid BPM or phase");
        return std::nullopt;
    }
    const auto chart = document.find_chart(chart_id);
    if (!chart_id || chart == nullptr) {
        reject(result, diagnostic_code_t::invalid_id, "tap timing apply refers to an unknown chart");
        return std::nullopt;
    }
    const auto bpm = selected_bpm(estimate, choice);
    if (!bpm || bpm->numerator <= 0 || bpm->denominator == 0) {
        reject(result, diagnostic_code_t::invalid_rational,
               "tap timing BPM cannot be represented as a positive rational");
        return std::nullopt;
    }
    const auto frame = nearest_measure_downbeat(*chart, sample_rate, beats_per_measure, estimate, *bpm);
    if (!frame) {
        reject(result, diagnostic_code_t::invalid_rational,
               "tap timing phase exceeds the exact audio offset range");
        return std::nullopt;
    }
    return tap_timing_apply_proposal_t{
        .chart = chart_id,
        .document_revision = document.revision,
        .sample_rate = sample_rate,
        .beats_per_measure = beats_per_measure,
        .estimate = estimate,
        .choice = choice,
        .beats_per_minute = *bpm,
        .beat_zero_audio_offset_seconds = exact_offset(*frame, sample_rate),
    };
}

auto effective_row_zero_index(const chart_document_t& chart) -> std::optional<std::size_t> {
    if (chart.measures.empty()) return std::nullopt;
    std::optional<std::size_t> winner;
    for (std::size_t i = 0; i < chart.bpms.size(); ++i) {
        const auto& bpm = chart.bpms[i];
        if (bpm.position.measure != chart.measures.front().id || bpm.position.row != 0) continue;
        if (!winner || std::tuple{chart.bpms[*winner].provenance.source_order, *winner,
                                  chart.bpms[*winner].id.value} <
                           std::tuple{bpm.provenance.source_order, i, bpm.id.value})
            winner = i;
    }
    return winner;
}

auto insert_initial_bpm(chart_document_t& chart, signed_rational_t bpm) -> bool {
    const auto id = next_object_id(chart);
    const auto source_order = next_source_order(chart.bpms);
    if (!id || !source_order || chart.measures.empty()) return false;
    chart.bpms.push_back({.id = *id,
                          .position = {chart.measures.front().id, 0},
                          .beats_per_minute = bpm,
                          .provenance = {.source_order = *source_order}});
    return true;
}

auto apply_proposal_to_chart(chart_document_t& chart,
                             const tap_timing_apply_proposal_t& proposal) -> bool {
    const auto winner = effective_row_zero_index(chart);
    if (winner) chart.bpms[*winner].beats_per_minute = proposal.beats_per_minute;
    else if (!insert_initial_bpm(chart, proposal.beats_per_minute)) return false;
    chart.beat_zero_audio_offset_seconds = proposal.beat_zero_audio_offset_seconds;
    return true;
}

auto proposed_document(const song_document_t& document,
                      const tap_timing_apply_proposal_t& proposal) -> std::optional<song_document_t> {
    tap_timing_apply_proposal_result_t refreshed;
    const auto verified = build_proposal(document, proposal.chart, proposal.estimate,
        proposal.sample_rate, proposal.beats_per_measure, proposal.choice, refreshed);
    if (!verified || verified->beats_per_minute != proposal.beats_per_minute ||
        verified->beat_zero_audio_offset_seconds != proposal.beat_zero_audio_offset_seconds)
        return std::nullopt;
    auto proposed = document;
    const auto chart = proposed.find_chart(proposal.chart);
    if (chart == nullptr || !apply_proposal_to_chart(*chart, *verified) ||
        !extend_native_charts_to_audio_end(proposed)) return std::nullopt;
    const auto diagnostics = validate_document(proposed);
    if (std::ranges::any_of(diagnostics, [](const auto& item) {
            return item.severity == diagnostic_severity_t::error;
        })) return std::nullopt;
    if (!build_timing_index(*chart)) return std::nullopt;
    return proposed;
}

} // namespace

auto propose_tap_timing_apply(const song_document_t& document, chart_id_t chart,
                              const tap_tempo_estimate_t& estimate, std::uint32_t sample_rate,
                              std::uint16_t beats_per_measure, tap_bpm_choice_t choice,
                              bool timing_locked) -> tap_timing_apply_proposal_result_t {
    tap_timing_apply_proposal_result_t result;
    if (timing_locked) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "tap timing apply is disabled while timing is locked");
        return result;
    }
    result.proposal = build_proposal(document, chart, estimate, sample_rate, beats_per_measure,
                                     choice, result);
    return result;
}

auto build_tap_timing_preview(const song_document_t& document,
                             const tap_timing_apply_proposal_t& proposal)
    -> std::optional<chart_document_t> {
    if (proposal.document_revision != document.revision) return std::nullopt;
    const auto proposed = proposed_document(document, proposal);
    if (!proposed) return std::nullopt;
    const auto chart = proposed->find_chart(proposal.chart);
    return chart == nullptr ? std::nullopt
                                           : std::optional<chart_document_t>{*chart};
}

auto apply_tap_timing_proposal(document_history_t& history,
                               const tap_timing_apply_proposal_t& proposal, bool timing_locked)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (timing_locked) {
        reject(result, diagnostic_code_t::unsupported_feature,
               "tap timing apply is disabled while timing is locked");
        return result;
    }
    const document_stamp_t stamp{.project = history.document.id,
                                  .revision = proposal.document_revision};
    if (!stamp.is_current(history.document)) {
        reject(result, diagnostic_code_t::invalid_id, stale_action_message("Tap timing proposal"));
        return result;
    }
    auto proposed = proposed_document(history.document, proposal);
    if (!proposed) {
        reject(result, diagnostic_code_t::invalid_id,
               "tap timing proposal no longer matches a valid chart");
        return result;
    }
    auto transaction = document_command_transaction_t::from(history);
    transaction.adopt_document(std::move(*proposed));
    return transaction.commit("Apply tap timing");
}

} // namespace wormedit
