#include <wormedit/audio/presentation_clock.h>
#include <wormedit/audio/playback_rate.h>

#include <algorithm>
#include <limits>

namespace wormedit::audio {
namespace {

auto predicted_frame(const playhead_state_t& report, std::uint64_t presentation_ns,
                     std::uint32_t sample_rate) noexcept -> std::uint64_t {
    constexpr std::uint64_t maximum_age_ns = 100'000'000;
    const auto elapsed = presentation_ns > report.report_time_ns
        ? std::min(presentation_ns - report.report_time_ns, maximum_age_ns) : 0;
    const auto rate = playback_rate(report.rate_index).value_or(rational_t{1, 1});
    const auto frames = static_cast<std::uint64_t>(
        static_cast<long double>(elapsed) * sample_rate * rate.numerator /
        (1'000'000'000.0L * rate.denominator));
    const auto advance = std::min(frames,
        std::numeric_limits<std::uint64_t>::max() - report.unwrapped_frame);
    return std::min(report.unwrapped_frame + advance, report.rendered_frame);
}

auto is_same_clock(const playhead_state_t& before, const playhead_state_t& after) noexcept -> bool {
    return before.active && before.generation == after.generation &&
        before.start_frame == after.start_frame && before.rate_index == after.rate_index &&
        before.loop_end_frame == after.loop_end_frame &&
        (after.loop_end_frame > after.start_frame ||
         before.transport_generation == after.transport_generation);
}

} // namespace

auto presentation_clock_t::frame(const playhead_state_t& report,
                                 std::uint64_t presentation_ns,
                                 std::uint32_t sample_rate) noexcept -> std::uint64_t {
    if (!report.active || report.report_time_ns == 0 || presentation_ns == 0 || sample_rate == 0) {
        reset();
        return report.timeline_frame;
    }
    auto predicted = predicted_frame(report, presentation_ns, sample_rate);
    if (is_same_clock(previous_, report) && presentation_ns >= previous_time_ns_)
        predicted = std::min(report.rendered_frame, std::max(predicted, previous_frame_));
    previous_ = report;
    previous_frame_ = predicted;
    previous_time_ns_ = presentation_ns;
    if (report.loop_end_frame > report.start_frame && predicted >= report.start_frame)
        return report.start_frame + (predicted - report.start_frame) %
            (report.loop_end_frame - report.start_frame);
    return predicted;
}

} // namespace wormedit::audio
