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

auto magnitude(std::int64_t value) noexcept -> std::uint64_t {
    return value >= 0 ? static_cast<std::uint64_t>(value)
                      : static_cast<std::uint64_t>(-(value + 1)) + 1U;
}

auto checked_multiply(std::uint64_t left, std::uint64_t right) -> std::optional<std::uint64_t> {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left)
        return std::nullopt;
    return left * right;
}

auto checked_signed_multiply(std::int64_t value, std::uint64_t factor)
    -> std::optional<std::int64_t> {
    const auto absolute = magnitude(value);
    const auto limit = value < 0 ? minimum_i64_magnitude : maximum_i64_magnitude;
    if (absolute != 0 && factor > limit / absolute)
        return std::nullopt;
    const auto product = absolute * factor;
    if (value < 0 && product == minimum_i64_magnitude)
        return std::numeric_limits<std::int64_t>::min();
    return value < 0 ? -static_cast<std::int64_t>(product) : static_cast<std::int64_t>(product);
}

auto checked_add(std::int64_t left, std::int64_t right) -> std::optional<std::int64_t> {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        return std::nullopt;
    }
    return left + right;
}

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

auto reject_drag(diagnostic_code_t code, std::string message) -> audio_offset_drag_result_t {
    audio_offset_drag_result_t result;
    result.diagnostics.push_back(error(code, std::move(message)));
    return result;
}

} // namespace

auto begin_audio_offset_drag(const song_document_t& document, chart_id_t chart,
                             std::uint32_t sample_rate, bool timing_locked)
    -> audio_offset_drag_result_t {
    if (timing_locked) {
        return reject_drag(diagnostic_code_t::unsupported_feature,
                           "audio offset dragging is disabled while timing is locked");
    }
    const auto* selected = find_chart(document, chart);
    if (!chart || selected == nullptr) {
        return reject_drag(diagnostic_code_t::invalid_id,
                           "audio offset drag refers to an unknown chart");
    }
    if (sample_rate == 0 || selected->beat_zero_audio_offset_seconds.denominator == 0) {
        return reject_drag(diagnostic_code_t::invalid_rational,
                           "audio offset drag requires a valid offset and sample rate");
    }
    return {.drag = audio_offset_drag_t{
                .chart = chart,
                .document_revision = document.revision,
                .sample_rate = sample_rate,
                .original_offset_seconds = selected->beat_zero_audio_offset_seconds,
                .proposed_offset_seconds = selected->beat_zero_audio_offset_seconds,
            }};
}

auto update_audio_offset_drag(const song_document_t& document, const audio_offset_drag_t& drag,
                              std::int64_t delta_frames, bool timing_locked)
    -> audio_offset_drag_result_t {
    if (timing_locked) {
        return reject_drag(diagnostic_code_t::unsupported_feature,
                           "audio offset dragging is disabled while timing is locked");
    }
    const auto* selected = find_chart(document, drag.chart);
    if (document.revision != drag.document_revision || selected == nullptr ||
        selected->beat_zero_audio_offset_seconds != drag.original_offset_seconds) {
        return reject_drag(diagnostic_code_t::invalid_id,
                           "audio offset drag expired after the document changed");
    }
    const auto proposed =
        add_frame_delta(drag.original_offset_seconds, delta_frames, drag.sample_rate);
    if (!proposed) {
        return reject_drag(diagnostic_code_t::invalid_rational,
                           "audio offset frame delta exceeds exact rational bounds");
    }
    auto updated = drag;
    updated.delta_frames = delta_frames;
    updated.proposed_offset_seconds = *proposed;
    return {.drag = std::move(updated)};
}

auto apply_audio_offset_edit(document_history_t& history, std::uint64_t expected_revision,
                             chart_id_t chart, signed_rational_t offset_seconds, bool timing_locked)
    -> document_command_result_t {
    document_command_result_t result{.revision = history.document.revision};
    if (timing_locked) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::unsupported_feature,
                  "audio offset editing is disabled while timing is locked"));
        return result;
    }
    if (expected_revision != history.document.revision) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "audio offset edit document revision is stale"));
        return result;
    }
    if (offset_seconds.denominator == 0) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_rational,
                                           "audio offset must be an exact signed rational"));
        return result;
    }
    auto proposed = history.document;
    const auto selected = std::ranges::find(proposed.charts, chart, &chart_document_t::id);
    if (!chart || selected == proposed.charts.end()) {
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id, "audio offset edit refers to an unknown chart"));
        return result;
    }
    if (selected->beat_zero_audio_offset_seconds == offset_seconds)
        return result;
    selected->beat_zero_audio_offset_seconds = offset_seconds;
    if (!extend_native_charts_to_audio_end(proposed)) {
        result.diagnostics.push_back(error(diagnostic_code_t::invalid_measure,
                                           "Offset edit would exceed the audio chart measure limits"));
        return result;
    }
    return commit_document_command(history, std::move(proposed), "Set audio offset");
}

auto commit_audio_offset_drag(document_history_t& history, const audio_offset_drag_t& drag,
                              bool timing_locked) -> document_command_result_t {
    if (history.document.revision != drag.document_revision) {
        document_command_result_t result{.revision = history.document.revision};
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "audio offset drag expired before it could be committed"));
        return result;
    }
    const auto* selected = find_chart(history.document, drag.chart);
    if (selected == nullptr ||
        selected->beat_zero_audio_offset_seconds != drag.original_offset_seconds) {
        document_command_result_t result{.revision = history.document.revision};
        result.diagnostics.push_back(
            error(diagnostic_code_t::invalid_id,
                  "audio offset drag no longer matches the selected chart"));
        return result;
    }
    return apply_audio_offset_edit(history, drag.document_revision, drag.chart,
                                   drag.proposed_offset_seconds, timing_locked);
}

} // namespace wormedit
