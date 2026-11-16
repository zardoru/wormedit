#include <wormedit/audio/scrub_reader.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace wormedit::audio {

namespace {

// Mechanical inertia of the needle/record, and how quickly the sound fades in and out.
constexpr double speed_slew_seconds = 0.008;
constexpr double gain_slew_seconds = 0.005;
// Below this smoothed speed the sound fades out, like a record nearly at rest.
constexpr double silent_speed = 0.1;

auto slew_factor(double seconds) -> double {
    return 1.0 - std::exp(-1.0 / (seconds * static_cast<double>(output_sample_rate)));
}

auto catmull_rom(float p0, float p1, float p2, float p3, float t) -> float {
    const auto a = -p0 + 3.0F * p1 - 3.0F * p2 + p3;
    const auto b = 2.0F * p0 - 5.0F * p1 + 4.0F * p2 - p3;
    const auto c = p2 - p0;
    return p1 + 0.5F * t * (c + t * (b + t * a));
}

} // namespace

void scrub_reader_t::set_segments(std::vector<scrub_segment_t> segments) {
    segments_ = std::move(segments);
}

void scrub_reader_t::reset(double position_frames) noexcept {
    position_ = position_frames;
    speed_ = 0.0;
    gain_ = 0.0;
}

auto scrub_reader_t::is_silent() const noexcept -> bool { return gain_ < 1e-4; }

auto scrub_reader_t::fill_window(std::int64_t first, std::size_t count) -> std::size_t {
    std::fill_n(window_.begin(), count * output_channel_count, 0.0F);
    std::size_t missing = 0;
    const auto last = first + static_cast<std::int64_t>(count);
    for (const auto& segment : segments_) {
        const auto begin = static_cast<std::int64_t>(segment.timeline_start_frame);
        const auto end = begin + static_cast<std::int64_t>(segment.frame_count);
        auto at = std::max(first, begin);
        const auto stop = std::min(last, end);
        while (at < stop) {
            const auto source = segment.source_start_frame + static_cast<std::uint64_t>(at - begin);
            const auto offset = static_cast<std::size_t>(source % maximum_read_frames);
            const auto wanted = static_cast<std::size_t>(stop - at);
            auto used = std::min(wanted, maximum_read_frames - offset);
            const audio_block_key_t key{segment.source, source / maximum_read_frames};
            float* const target = window_.data() + static_cast<std::size_t>(at - first) * output_channel_count;
            const auto found = cache_.with_block(key, [&](const decoded_audio_block_t& block) {
                if (offset >= block.frame_count) return;
                used = std::min(used, block.frame_count - offset);
                const float* from = block.interleaved_stereo.data() + offset * output_channel_count;
                for (std::size_t i = 0; i < used * output_channel_count; ++i) target[i] += from[i];
            });
            if (!found) ++missing;
            at += static_cast<std::int64_t>(std::max<std::size_t>(used, 1));
        }
    }
    return missing;
}

auto scrub_reader_t::render(double target_speed, bool hold, std::span<float> interleaved_stereo)
    -> scrub_render_result_t {
    static const auto speed_alpha = slew_factor(speed_slew_seconds);
    static const auto gain_alpha = slew_factor(gain_slew_seconds);
    scrub_render_result_t result;
    const auto total = interleaved_stereo.size() / output_channel_count;
    target_speed = std::clamp(target_speed, -maximum_speed, maximum_speed);
    std::array<double, chunk_frames> positions{};
    std::array<float, chunk_frames> gains{};
    for (std::size_t done = 0; done < total;) {
        const auto count = std::min(chunk_frames, total - done);
        for (std::size_t i = 0; i < count; ++i) {
            speed_ += ((hold ? 0.0 : target_speed) - speed_) * speed_alpha;
            const auto audible = hold ? 0.0 : std::min(1.0, std::abs(speed_) / silent_speed);
            gain_ += (audible - gain_) * gain_alpha;
            positions[i] = position_;
            gains[i] = static_cast<float>(gain_);
            position_ += speed_;
        }
        const auto [low, high] = std::minmax_element(positions.begin(), positions.begin() + count);
        const auto first = static_cast<std::int64_t>(std::floor(*low)) - 1;
        const auto window_count = static_cast<std::size_t>(
            static_cast<std::int64_t>(std::floor(*high)) + 3 - first);
        result.missing_blocks += fill_window(first, std::min(window_count, window_capacity));
        for (std::size_t i = 0; i < count; ++i) {
            const auto base = std::floor(positions[i]);
            const auto fraction = static_cast<float>(positions[i] - base);
            const auto index = std::min(static_cast<std::size_t>(static_cast<std::int64_t>(base) - first),
                                        window_capacity - 3);
            for (std::size_t channel = 0; channel < output_channel_count; ++channel) {
                const auto at = [&](std::size_t offset) {
                    return window_[(index - 1 + offset) * output_channel_count + channel];
                };
                const auto sample = catmull_rom(at(0), at(1), at(2), at(3), fraction) * gains[i];
                interleaved_stereo[(done + i) * output_channel_count + channel] =
                    std::clamp(sample, -1.0F, 1.0F);
            }
        }
        done += count;
    }
    result.frames = total;
    return result;
}

void scrub_reader_t::plan_blocks(std::span<const scrub_segment_t> segments, double position_frames,
                                 std::uint64_t ahead_frames, std::uint64_t behind_frames,
                                 double direction, std::vector<audio_block_key_t>& out) {
    out.clear();
    const auto forward = direction >= 0.0;
    const auto reach_low = static_cast<double>(forward ? behind_frames : ahead_frames);
    const auto reach_high = static_cast<double>(forward ? ahead_frames : behind_frames);
    const auto low = position_frames - reach_low;
    const auto high = position_frames + reach_high;
    struct candidate_t {
        double distance;
        audio_block_key_t key;
    };
    std::vector<candidate_t> found;
    for (const auto& segment : segments) {
        const auto begin = static_cast<double>(segment.timeline_start_frame);
        const auto end = begin + static_cast<double>(segment.frame_count);
        const auto from = std::max(low, begin);
        const auto to = std::min(high, end);
        if (from >= to) continue;
        const auto source_from = segment.source_start_frame + static_cast<std::uint64_t>(from - begin);
        const auto source_to = segment.source_start_frame + static_cast<std::uint64_t>(to - begin);
        for (auto block = source_from / maximum_read_frames; block <= source_to / maximum_read_frames; ++block) {
            const auto timeline = begin + static_cast<double>(block * maximum_read_frames) -
                                  static_cast<double>(segment.source_start_frame) +
                                  static_cast<double>(maximum_read_frames) / 2.0;
            found.push_back({std::abs(timeline - position_frames), {segment.source, block}});
        }
    }
    std::ranges::sort(found, {}, &candidate_t::distance);
    for (auto& candidate : found) out.push_back(std::move(candidate.key));
}

} // namespace wormedit::audio
