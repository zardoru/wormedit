#include "../core/checked_math.h"
#include <wormedit/core/audio_offset_editor.h>
#include <wormedit/core/bms_program_edit.h>
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

auto audio_offset_drag_t::propose(const song_document_t& document, chart_id_t chart,
                               std::uint32_t sample_rate, bool timing_locked)
    -> audio_offset_drag_outcome_t {
    audio_offset_drag_outcome_t result;
    if (auto locked = revision_policy.validate_lock(timing_locked, proposal_lock_order_t::before_stamp)) {
        result.diagnostics.push_back(std::move(*locked));
        return result;
    }
    const auto* selected = document.read_chart(chart);
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
                proposal_t<audio_offset_drag_t>{
                    .stamp = chart_stamp_t::from(document, chart),
                    .payload = {
                        .sample_rate = sample_rate,
                        .original_offset_seconds = selected->beat_zero_audio_offset_seconds,
                        .proposed_offset_seconds = selected->beat_zero_audio_offset_seconds,
                    }},
            .diagnostics = {}};
}

auto audio_offset_drag_t::revise(const proposal_read_scope_t<chart_stamp_t>& scope,
    std::int64_t delta_frames) const -> audio_offset_drag_outcome_t {
    audio_offset_drag_outcome_t result;
    const auto& drag = *this;
    const auto& document = scope.source_document();
    const auto& stamp = scope.captured_stamp();
    const auto* selected = document.read_chart(stamp.chart);
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
    return {.value = proposal_t<audio_offset_drag_t>{.stamp = stamp, .payload = std::move(updated)},
            .diagnostics = {}};
}

namespace {
auto winning_scoped_offset(const bms_program_document_t& program) -> std::optional<object_id_t> {
    std::optional<object_id_t> result;
    const auto projection = program.build_projection();
    if (!projection.value) return result;
    for (const auto chart : projection.value->active_charts())
        for (const auto& item : std::get<bms_chart_content_t>(program.prepared().program.find(chart)->value).items)
            if (const auto* metadata = std::get_if<bms_metadata_declaration_t>(&item.value);
                metadata && metadata->field == bms_metadata_field_t::offset) result = item.id;
    return result;
}

auto stage_scoped_audio_offset(document_command_transaction_t& transaction, chart_id_t difficulty,
    signed_rational_t offset) -> attempt_t<document_edit_effect_t> {
    const auto* program = transaction.current_document().find_bms_program(difficulty);
    const auto* chart = program->read_chart();
    if (chart && chart->beat_zero_audio_offset_seconds == offset) return {.value = document_edit_effect_t{}};
    const auto existing = winning_scoped_offset(*program);
    const auto target = transaction.bms_target(difficulty, existing);
    if (!target) return {.diagnostics = {error(diagnostic_code_t::invalid_id, target.error().message)}};
    const auto value = bms_metadata_declaration_t{.field = bms_metadata_field_t::offset, .value = offset};
    bms_program_content_edit_batch_t batch{.target = *target, .label = "Set audio offset"};
    if (existing) batch.edits.push_back(bms_program_content_update_t{.owner = {
        .scope = target->scope, .chart = target->chart, .item = *existing}, .value = value});
    else {
        const auto id = transaction.next_bms_program_item_id();
        if (!id) return {.diagnostics = {error(diagnostic_code_t::invalid_id, "Scoped offset identities are exhausted.")}};
        batch.edits.push_back(bms_program_content_insert_t{.item = {.id = *id, .value = value}});
    }
    return transaction.stage(batch);
}

struct audio_offset_edit_t {
    chart_id_t chart;
    signed_rational_t offset;
    bool is_timing_locked{};

    [[nodiscard]] auto apply(document_edit_scope_t& scope) const -> diagnosed_t<std::optional<std::string>> {
        auto& transaction = scope.transaction();
        if (is_timing_locked)
            return {.diagnostics = {error(diagnostic_code_t::unsupported_feature,
                "audio offset editing is disabled while timing is locked")}};
        if (offset.denominator == 0)
            return {.diagnostics = {error(diagnostic_code_t::invalid_rational,
                "audio offset must be an exact signed rational")}};
        if (transaction.has_bms_program(chart)) {
            auto staged = stage_scoped_audio_offset(transaction, chart, offset);
            if (!staged.value || !staged.ok()) return {.diagnostics = std::move(staged.diagnostics)};
            if (!staged.value->changed) return {};
            return {.value = std::move(staged.value->command_label)};
        }
        auto& proposed = transaction.document();
        const auto selected = proposed.find_chart(chart);
        if (!chart || selected == nullptr)
            return {.diagnostics = {error(diagnostic_code_t::invalid_id,
                "audio offset edit refers to an unknown chart")}};
        if (selected->beat_zero_audio_offset_seconds == offset) return {};
        selected->beat_zero_audio_offset_seconds = offset;
        if (!extend_native_charts_to_audio_end(proposed))
            return {.diagnostics = {error(diagnostic_code_t::invalid_measure,
                "Offset edit would exceed the audio chart measure limits")}};
        return {.value = "Set audio offset"};
    }
};
} // namespace

auto apply_audio_offset_edit(document_history_t& history, chart_id_t chart,
                             signed_rational_t offset_seconds, bool timing_locked)
    -> document_command_result_t {
    auto transaction = document_command_transaction_t(history);
    document_edit_scope_t scope{transaction};
    return apply_proposal_edit(scope,
        audio_offset_edit_t{.chart = chart, .offset = offset_seconds, .is_timing_locked = timing_locked});
}

auto audio_offset_drag_t::apply(proposal_edit_scope_t<chart_stamp_t>& scope,
                                bool timing_locked) const -> diagnosed_t<std::optional<std::string>> {
    auto& transaction = scope.transaction();
    const auto& stamp = scope.captured_stamp();
    const auto chart = stamp.chart;
    const auto* selected = transaction.source_chart(chart);
    if (selected == nullptr || selected->beat_zero_audio_offset_seconds != original_offset_seconds)
        return {.diagnostics = {error(diagnostic_code_t::invalid_id,
            "audio offset drag no longer matches the selected chart")}};
    return audio_offset_edit_t{.chart = chart, .offset = proposed_offset_seconds,
                              .is_timing_locked = timing_locked}.apply(scope);
}

} // namespace wormedit
