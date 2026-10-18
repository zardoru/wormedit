#include <wormedit/audio/onset.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace wormedit::audio {
namespace {

using window_rms_t = result_t<double, failure_code_t>;

auto is_valid_request(const onset_request_t& request, std::uint64_t total_frames) -> bool {
    return request.begin_frame < request.end_frame && request.end_frame <= total_frames &&
        request.window_frames != 0 && request.maximum_onsets != 0 &&
        request.maximum_onsets <= maximum_onset_frames &&
        std::isfinite(request.minimum_rms) && request.minimum_rms >= 0.0 &&
        std::isfinite(request.minimum_rms_rise) && request.minimum_rms_rise > 0.0;
}

auto cache_end(const waveform_t& waveform) -> std::uint64_t {
    return waveform.key.end_frame == 0 ? waveform.total_frames : waveform.key.end_frame;
}

auto window_end(std::uint64_t begin, std::uint64_t window, std::uint64_t total)
    -> std::uint64_t {
    return begin + std::min(window, total - begin);
}

auto is_valid_rms(const waveform_bin_t& bin) -> bool {
    return std::ranges::all_of(bin.rms, [](float value) {
        return std::isfinite(value) && value >= 0.0F;
    });
}

auto read_window(const waveform_t& waveform, std::uint64_t begin, std::uint64_t end)
    -> window_rms_t {
    const auto extent = cache_end(waveform);
    const auto width = waveform.key.frames_per_bin;
    if (begin < waveform.key.begin_frame || end > extent)
        return std::unexpected(failure_code_t::not_ready);
    const auto first = (begin - waveform.key.begin_frame) / width;
    const auto last = (end - 1 - waveform.key.begin_frame) / width;
    if (last >= waveform.bins.size())
        return std::unexpected(failure_code_t::not_ready);
    std::array<long double, output_channel_count> energy{};
    for (auto index = first; index <= last; ++index) {
        const auto& bin = waveform.bins[static_cast<std::size_t>(index)];
        const auto bin_begin = waveform.key.begin_frame + index * width;
        const auto expected = std::min<std::uint64_t>(width, extent - bin_begin);
        if (bin.frame_count < expected)
            return std::unexpected(failure_code_t::not_ready);
        if (bin.frame_count > expected || !is_valid_rms(bin))
            return std::unexpected(failure_code_t::invalid_data);
        const auto overlap = std::min(end, bin_begin + expected) - std::max(begin, bin_begin);
        for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
            const auto rms = static_cast<long double>(bin.rms[channel]);
            energy[channel] += rms * rms * static_cast<long double>(overlap);
        }
    }
    const auto maximum = *std::ranges::max_element(energy);
    return static_cast<double>(std::sqrt(maximum / static_cast<long double>(end - begin)));
}

auto initial_baseline(const waveform_t& waveform, std::uint64_t first,
                      std::uint64_t window) -> window_rms_t {
    if (first == 0)
        return 0.0;
    return read_window(waveform, first - window, first);
}

auto is_qualifying_onset(double current, double previous, const onset_request_t& request,
                std::uint64_t frame, std::optional<std::uint64_t> last) -> bool {
    return frame >= request.begin_frame && current >= request.minimum_rms &&
        current - previous >= request.minimum_rms_rise &&
        (!last || frame - *last >= request.minimum_separation_frames);
}

auto scan_windows(const waveform_t& waveform, const onset_request_t& request)
    -> result_t<waveform_onset_index_t, failure_code_t> {
    auto begin = request.begin_frame - request.begin_frame % request.window_frames;
    const auto baseline = initial_baseline(waveform, begin, request.window_frames);
    if (!baseline)
        return std::unexpected(baseline.error());
    auto previous = *baseline;
    std::optional<std::uint64_t> last;
    waveform_onset_index_t result;
    while (begin < request.end_frame) {
        const auto end = window_end(begin, request.window_frames, waveform.total_frames);
        const auto rms = read_window(waveform, begin, end);
        if (!rms)
            return std::unexpected(rms.error());
        if (is_qualifying_onset(*rms, previous, request, begin, last)) {
            if (result.frames.size() == request.maximum_onsets)
                return std::unexpected(failure_code_t::capacity_exceeded);
            result.frames.push_back(begin);
            last = begin;
        }
        previous = *rms;
        begin = end;
    }
    return result;
}

} // namespace

auto waveform_onset_index_t::from(const waveform_t& waveform, onset_request_t request)
    -> result_t<waveform_onset_index_t, failure_code_t> {
    if (!is_valid_request(request, waveform.total_frames))
        return std::unexpected(failure_code_t::invalid_request);
    if (waveform.sample_rate == 0 || waveform.key.frames_per_bin == 0 ||
        waveform.key.begin_frame >= cache_end(waveform) || cache_end(waveform) > waveform.total_frames)
        return std::unexpected(failure_code_t::invalid_data);
    if (request.window_frames % waveform.key.frames_per_bin != 0 ||
        waveform.key.begin_frame % waveform.key.frames_per_bin != 0)
        return std::unexpected(failure_code_t::not_ready);
    const auto first = request.begin_frame - request.begin_frame % request.window_frames;
    const auto last = request.end_frame - 1 - (request.end_frame - 1) % request.window_frames;
    const auto coverage = read_window(waveform, first == 0 ? 0 : first - request.window_frames,
        window_end(last, request.window_frames, waveform.total_frames));
    if (!coverage)
        return std::unexpected(coverage.error());
    return scan_windows(waveform, request);
}

} // namespace wormedit::audio
