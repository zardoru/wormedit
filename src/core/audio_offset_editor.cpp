#include "../core/checked_math.h"
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/measure_editor.h>

#include <algorithm>
#include <limits>
#include <numeric>
#include <ranges>
#include <utility>

namespace wormedit {
namespace {

constexpr auto maximum_i64_magnitude =
    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
constexpr auto minimum_i64_magnitude = maximum_i64_magnitude + 1U;

using checked_math::magnitude;

using checked_math::checked_multiply;

using checked_math::checked_signed_multiply;

using checked_math::checked_add;

auto add_frame_delta(signed_rational_t offset, std::int64_t frames, std::uint32_t sample_rate)
    -> std::optional<signed_rational_t> {
    if (offset.denominator == 0 || sample_rate == 0)
        return std::nullopt;
    if (frames == 0)
        return offset;
    const auto divisor = std::gcd(offset.denominator, static_cast<std::uint64_t>(sample_rate));
    const auto offset_scale = static_cast<std::uint64_t>(sample_rate) / divisor;
    const auto frame_scale = offset.denominator / divisor;
    const auto denominator = checked_multiply(offset.denominator, offset_scale);
    const auto scaled_offset = checked_signed_multiply(offset.numerator, offset_scale);
    const auto scaled_frames = checked_signed_multiply(frames, frame_scale);
    if (!denominator || !scaled_offset || !scaled_frames)
        return std::nullopt;
    const auto numerator = checked_add(*scaled_offset, *scaled_frames);
    if (!numerator)
        return std::nullopt;
    const auto reduction = std::gcd(magnitude(*numerator), *denominator);
    if (reduction == 0)
        return std::nullopt;
    const auto reduced_magnitude = magnitude(*numerator) / reduction;
    const auto reduced_numerator = *numerator < 0 && reduced_magnitude == minimum_i64_magnitude
                                       ? std::numeric_limits<std::int64_t>::min()
                                   : *numerator < 0 ? -static_cast<std::int64_t>(reduced_magnitude)
                                                    : static_cast<std::int64_t>(reduced_magnitude);
    return signed_rational_t{reduced_numerator, *denominator / reduction};
}

} // namespace

auto audio_offset_drag_t::from(const song_document_t& document, chart_id_t chart,
                               std::uint32_t sample_rate, bool timing_locked)
    -> audio_offset_drag_outcome_t {
    audio_offset_drag_outcome_t result;
    if (timing_locked) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::unsupported_feature,
                  "audio offset dragging is disabled while timing is locked"));
        return result;
    }
    const auto* selected = document.find_chart(chart);
    if (!chart || selected == nullptr) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "audio offset drag refers to an unknown chart"));
        return result;
    }
    if (sample_rate == 0 || selected->beat_zero_audio_offset_seconds.denominator == 0) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_rational,
                  "audio offset drag requires a valid offset and sample rate"));
        return result;
    }
    return {.value =
                audio_offset_drag_t{
                    .stamp = chart_stamp_t::from(document, chart),
                    .sample_rate = sample_rate,
                    .original_offset_seconds = selected->beat_zero_audio_offset_seconds,
                    .proposed_offset_seconds = selected->beat_zero_audio_offset_seconds,
                },
            .diagnostics = {}};
}

auto audio_offset_drag_t::revise(const song_document_t& document, std::int64_t delta_frames,
                                 bool timing_locked) const -> audio_offset_drag_outcome_t {
    audio_offset_drag_outcome_t result;
    const auto& drag = *this;
    if (timing_locked) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::unsupported_feature,
                  "audio offset dragging is disabled while timing is locked"));
        return result;
    }
    if (!drag.stamp.document.is_current(document, document_identity_policy_t::revision_only))
        return {.value = std::nullopt, .diagnostics = {error(diagnostic_code_t::invalid_id, stale_action_message("Audio offset drag"))}};
    const auto* selected = document.find_chart(drag.stamp.chart);
    if (selected == nullptr ||
        selected->beat_zero_audio_offset_seconds != drag.original_offset_seconds) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_id,
                                           "audio offset drag expired after the document changed"));
        return result;
    }
    const auto proposed =
        add_frame_delta(drag.original_offset_seconds, delta_frames, drag.sample_rate);
    if (!proposed) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_rational,
                  "audio offset frame delta exceeds exact rational bounds"));
        return result;
    }
    auto updated = drag;
    updated.delta_frames = delta_frames;
    updated.proposed_offset_seconds = *proposed;
    return {.value = std::move(updated), .diagnostics = {}};
}

auto apply_audio_offset_edit(document_history_t& history, chart_id_t chart,
                             signed_rational_t offset_seconds, bool timing_locked)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history);
    return apply_audio_offset_edit(transaction, chart, offset_seconds, timing_locked);
}

auto apply_audio_offset_edit(document_command_transaction_t& transaction,
                             chart_id_t chart, signed_rational_t offset_seconds, bool timing_locked)
    -> document_command_result_t {
    if (!transaction.is_ready()) return transaction.finish_without_commit();
    auto& result = transaction.outcome;
    if (timing_locked) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::unsupported_feature,
                  "audio offset editing is disabled while timing is locked"));
        return transaction.finish_without_commit();
    }
    if (offset_seconds.denominator == 0) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                           "audio offset must be an exact signed rational"));
        return transaction.finish_without_commit();
    }
    auto& proposed = transaction.copy_document();
    const auto selected = proposed.find_chart(chart);
    if (!chart || selected == nullptr) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "audio offset edit refers to an unknown chart"));
        return transaction.finish_without_commit();
    }
    if (selected->beat_zero_audio_offset_seconds == offset_seconds)
        return transaction.finish_without_commit();
    selected->beat_zero_audio_offset_seconds = offset_seconds;
    if (!extend_native_charts_to_audio_end(proposed)) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                                           "Offset edit would exceed the audio chart measure limits"));
        return transaction.finish_without_commit();
    }
    return transaction.commit("Set audio offset");
}

auto apply_audio_offset_drag(document_history_t& history, const audio_offset_drag_t& drag,
                              bool timing_locked) -> document_command_result_t {
    auto transaction = document_command_transaction_t::from(history, drag.stamp,
        "Audio offset drag", document_identity_policy_t::revision_only);
    if (!transaction.is_ready()) return transaction.finish_without_commit();
    const auto* selected = transaction.source_chart(drag.stamp.chart);
    if (selected == nullptr ||
        selected->beat_zero_audio_offset_seconds != drag.original_offset_seconds)
        return transaction.reject(diagnostic_code_t::invalid_id,
                                  "audio offset drag no longer matches the selected chart");
    return apply_audio_offset_edit(transaction, drag.stamp.chart, drag.proposed_offset_seconds,
                                   timing_locked);
}

} // namespace wormedit
