#include "timing_arithmetic.h"
#include "../core/checked_math.h"
#include <wormedit/core/sound_slice.h>

#include <algorithm>
#include <limits>
#include <numeric>
#include <tuple>

namespace wormedit {
namespace {

using checked_math::magnitude;

using checked_math::checked_multiply;

using checked_math::checked_signed_multiply;

using timing_arithmetic::subtract_exact;

auto diagnostic(sound_slice_error_code_t code, std::string message,
                std::optional<object_id_t> object = std::nullopt)
    -> sound_slice_diagnostic_t {
    return {.severity = diagnostic_severity_t::error,
            .code = code,
            .message = std::move(message),
            .object = object};
}

struct indexed_event_t {
    std::uint64_t row{};
    const sound_channel_event_t* event{};
};

struct sound_slice_build_context_t {
    const sound_channel_t& channel;
    const std::vector<indexed_event_t>& events;
    const timing_index_t& timing;
    signed_rational_t restart_time;
    std::uint32_t sample_rate;
    std::optional<std::uint64_t> source_end;
    sound_slice_options_t options;
};

auto append_sound_slice_group(sound_slice_build_context_t& context, std::size_t begin,
                              std::size_t end, sound_slice_result_t& result) -> bool {
    const auto& event = context.events[begin];
    const auto event_time = context.timing.seconds_at(event.row);
    const auto elapsed = event_time ? subtract_exact(*event_time.seconds, context.restart_time)
                                    : std::nullopt;
    const auto relative_frame = elapsed
                                    ? sample_frame_at(*elapsed, context.sample_rate,
                                                      context.options.rounding)
                                    : sample_frame_result_t{};
    if (!relative_frame) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::arithmetic_overflow,
            "sound-channel slice boundary cannot be represented in source frames", event.event->id));
        return false;
    }
    if (*relative_frame.frame >
        std::numeric_limits<std::uint64_t>::max() - context.channel.source_clip.begin_frame) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::arithmetic_overflow,
            "sound-channel slice boundary overflows the source frame range", event.event->id));
        return false;
    }
    const auto restart = std::any_of(context.events.begin() + static_cast<std::ptrdiff_t>(begin),
                                    context.events.begin() + static_cast<std::ptrdiff_t>(end),
                                    [](const auto& item) { return !item.event->continuation; });
    const auto boundary = context.channel.source_clip.begin_frame + *relative_frame.frame;
    const auto previous_end = context.source_end ? std::min(boundary, *context.source_end) : boundary;
    auto source_begin = restart ? context.channel.source_clip.begin_frame : boundary;
    // A continuation after the audio has ended plays nothing: the slice is kept, empty, at the end
    // of the source, so charts that keep marking points past the end of a sample still derive.
    const auto past_end = context.source_end.has_value() && source_begin >= *context.source_end;
    if (past_end) source_begin = *context.source_end;
    const auto previous_empty = !result.slices.empty() && context.source_end.has_value() &&
                                result.slices.back().source_begin_frame >= *context.source_end;
    if (!past_end && !previous_empty && !result.slices.empty() &&
        previous_end <= result.slices.back().source_begin_frame) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::invalid_source_range,
            "distinct sound-channel markers must derive a positive source-frame range",
            event.event->id));
        return false;
    }
    derived_sound_slice_t slice{.absolute_row = event.row,
                                .source_begin_frame = source_begin,
                                .source_end_frame = std::nullopt,
                                .restart = restart,
                                .events = {}};
    slice.events.reserve(end - begin);
    for (auto index = begin; index < end; ++index) {
        slice.events.push_back(context.events[index].event->id);
    }
    if (!result.slices.empty()) {
        result.slices.back().source_end_frame = previous_end;
    }
    if (restart) context.restart_time = *event_time.seconds;
    result.slices.push_back(std::move(slice));
    return true;
}

} // namespace

auto derive_sound_channel_slices(const chart_document_t& chart,
                                 const sound_channel_t& channel,
                                 std::uint32_t sample_rate,
                                 std::optional<std::uint64_t> source_frame_count,
                                 sound_slice_options_t options) -> sound_slice_result_t {
    sound_slice_result_t result;
    if (sample_rate == 0) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::invalid_sample_rate,
            "sound-slice derivation requires a positive decoded sample rate", channel.id));
        return result;
    }
    if (channel.events.size() > options.maximum_events) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::capacity_exceeded,
            "sound channel exceeds the configured slice-event limit", channel.id));
        return result;
    }

    auto source_end = channel.source_clip.end_frame;
    if (source_frame_count.has_value() &&
        (!source_end.has_value() || *source_frame_count < *source_end)) {
        source_end = source_frame_count;
    }
    if (source_end.has_value() && *source_end <= channel.source_clip.begin_frame) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::invalid_source_range,
            "sound channel source clip has no playable frames", channel.id));
        return result;
    }
    if (channel.events.empty()) return result;

    const auto measures = build_measure_index(chart);
    if (!std::holds_alternative<measure_index_t>(measures)) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::invalid_document,
            "sound-slice derivation requires valid measure geometry", channel.id));
        return result;
    }
    const auto& measure_index = std::get<measure_index_t>(measures);
    const auto timing = build_timing_index(chart);
    if (!timing) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::timing_failure,
            "sound-slice derivation requires a valid timing index", channel.id));
        return result;
    }

    std::vector<indexed_event_t> events;
    events.reserve(channel.events.size());
    for (const auto& event : channel.events) {
        const auto row = absolute_row(measure_index, event.position);
        if (!row.has_value()) {
            result.diagnostics.push_back(diagnostic(
                sound_slice_error_code_t::invalid_document,
                "sound-channel event lies outside the chart measure geometry", event.id));
            result.slices.clear();
            return result;
        }
        events.push_back({*row, &event});
    }
    std::ranges::stable_sort(events, [](const auto& left, const auto& right) {
        return std::tie(left.row, left.event->provenance.source_order, left.event->id.value) <
               std::tie(right.row, right.event->provenance.source_order, right.event->id.value);
    });

    const auto first_time = (*timing.index).seconds_at(events.front().row);
    if (!first_time) {
        result.diagnostics.push_back(diagnostic(
            sound_slice_error_code_t::timing_failure,
            "first sound-channel event cannot be mapped to chart time", events.front().event->id));
        return result;
    }

    sound_slice_build_context_t context{channel, events, *timing.index,
                                       *first_time.seconds, sample_rate, source_end, options};
    for (std::size_t begin = 0; begin < events.size();) {
        auto end = begin + 1;
        while (end < events.size() && events[end].row == events[begin].row) {
            ++end;
        }
        if (!append_sound_slice_group(context, begin, end, result)) {
            result.slices.clear();
            return result;
        }
        begin = end;
    }
    result.slices.back().source_end_frame = source_end;
    return result;
}

auto find_sound_slice(const sound_slice_result_t& result, object_id_t event)
    -> const derived_sound_slice_t* {
    const auto found = std::ranges::find_if(result.slices, [&](const auto& slice) {
        return std::ranges::find(slice.events, event) != slice.events.end();
    });
    return found == result.slices.end() ? nullptr : &*found;
}

} // namespace wormedit
