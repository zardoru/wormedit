#include <wormedit/audio/time_stretch.h>

// WSOLA algorithm: Werner Verhelst and Marc Roelands, ICASSP 1993, vol. 2,
// pp. 554–557, "An overlap-add technique based on waveform similarity (WSOLA)
// for high quality time-scale modification of speech".
// https://doi.org/10.1109/ICASSP.1993.319366
// This streaming implementation was written for wormedit.

#include <algorithm>
#include <cmath>

namespace wormedit::audio {

time_stretch_t::time_stretch_t(std::uint32_t sample_rate)
    : window_(std::max<std::size_t>(2, sample_rate / 50 / 2 * 2)),
      hop_(window_ / 2), search_(sample_rate * 8ULL / 1000),
      capacity_(std::max<std::size_t>(16384, window_ * 16)),
      input_(capacity_ * 2), mid_(capacity_ * 2), previous_mid_(hop_), overlap_(hop_ * 2), pending_(hop_ * 2) {}

auto time_stretch_t::reset(rational_t rate) noexcept -> bool {
    if (!playback_rate_index(rate)) return false;
    rate_ = rate;
    input_begin_ = input_end_ = hop_count_ = produced_ = final_output_frames_ = 0;
    pending_begin_ = pending_end_ = 0;
    finished_ = false;
    std::fill(overlap_.begin(), overlap_.end(), 0.0F);
    return true;
}

auto time_stretch_t::next_source_position() const noexcept -> long double {
    // Keep the nominal clock independent of correlation offsets: feeding an
    // offset into the next search would accumulate tempo drift on periodic audio.
    return static_cast<long double>(hop_count_) * hop_ * rate_.numerator / rate_.denominator;
}

auto time_stretch_t::input_frames_needed(std::size_t output_frames) const noexcept -> std::size_t {
    if (finished_) return 0;
    const auto pending = pending_end_ - pending_begin_;
    if (output_frames <= pending) return 0;
    const auto hops = (output_frames - pending + hop_ - 1) / hop_;
    const auto last_position = static_cast<long double>(hop_count_ + hops - 1) * hop_ *
                               rate_.numerator / rate_.denominator;
    const auto required = std::ceil(last_position) + search_ + window_ + 1;
    if (required <= input_end_) return 0;
    return static_cast<std::size_t>(std::min<long double>(required - input_end_,
        capacity_ - static_cast<std::size_t>(input_end_ - input_begin_)));
}

auto time_stretch_t::sample(std::uint64_t frame, unsigned channel) const noexcept -> float {
    if (frame < input_begin_ || frame >= input_end_) return 0.0F;
    return input_[(frame % capacity_) * 2 + channel];
}

auto time_stretch_t::correlation(std::uint64_t candidate, double previous_energy) const noexcept -> double {
    if (candidate < input_begin_ || candidate >= input_end_) return -2.0;
    const auto first = static_cast<std::size_t>(candidate % capacity_);
    const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(hop_, input_end_ - candidate));
    double dot{}, energy{};
    for (std::size_t frame = 0; frame < count; ++frame) {
        const auto current = mid_[first + frame];
        dot += previous_mid_[frame] * current;
        energy += current * current;
    }
    return previous_energy > 1e-20 && energy > 1e-20 ? dot / std::sqrt(previous_energy * energy) : -2.0;
}

auto time_stretch_t::best_candidate(std::uint64_t predicted) noexcept -> std::uint64_t {
    double previous_energy{};
    for (std::size_t frame = 0; frame < hop_; ++frame) {
        const auto mid = (static_cast<double>(overlap_[frame * 2]) + overlap_[frame * 2 + 1]) * 0.5;
        previous_mid_[frame] = mid;
        previous_energy += mid * mid;
    }
    if (previous_energy <= 1e-20) return predicted;
    const auto first = std::max(input_begin_, predicted > search_ ? predicted - search_ : 0);
    const auto last = finished_ ? predicted + search_ : std::min(predicted + search_, input_end_ - window_);
    auto best = predicted;
    auto best_score = correlation(best, previous_energy);
    for (auto candidate = first; candidate <= last; ++candidate) {
        const auto score = correlation(candidate, previous_energy);
        if (score > best_score + 1e-12) { best = candidate; best_score = score; }
    }
    return best;
}

auto time_stretch_t::generate() noexcept -> bool {
    if (finished_ && produced_ >= final_output_frames_) return false;
    const auto predicted = static_cast<std::uint64_t>(std::round(next_source_position()));
    if (!finished_ && predicted + search_ + window_ >= input_end_) return false;
    // At EOF sample() zero-pads the last window, while final_output_frames_
    // limits delivery to the exact rational duration.
    const auto candidate = hop_count_ == 0 ? 0 : best_candidate(predicted);
    const auto frames = finished_ ? std::min<std::uint64_t>(hop_, final_output_frames_ - produced_) : hop_;
    for (std::size_t frame = 0; frame < hop_; ++frame) {
        const auto weight = static_cast<float>(frame) / static_cast<float>(hop_);
        for (unsigned channel = 0; channel < 2; ++channel) {
            const auto current = sample(candidate + frame, channel);
            pending_[frame * 2 + channel] = hop_count_ == 0 ? current :
                overlap_[frame * 2 + channel] * (1.0F - weight) + current * weight;
            overlap_[frame * 2 + channel] = sample(candidate + hop_ + frame, channel);
        }
    }
    pending_begin_ = 0;
    pending_end_ = static_cast<std::size_t>(frames);
    ++hop_count_;
    const auto next = static_cast<std::uint64_t>(std::floor(next_source_position()));
    input_begin_ = std::min(input_end_, next > search_ ? next - search_ : 0);
    return true;
}

auto time_stretch_t::process(std::span<const float> input, std::span<float> output) noexcept
    -> time_stretch_result_t {
    if (input.size() % 2 != 0 || output.size() % 2 != 0) return {};
    const auto available = capacity_ - static_cast<std::size_t>(input_end_ - input_begin_);
    const auto accepted = finished_ ? 0 : std::min(input.size() / 2, available);
    for (std::size_t frame = 0; frame < accepted; ++frame) {
        const auto slot = static_cast<std::size_t>((input_end_ + frame) % capacity_);
        for (unsigned channel = 0; channel < 2; ++channel)
            input_[slot * 2 + channel] = input[frame * 2 + channel];
        const auto mid = (static_cast<double>(input[frame * 2]) + input[frame * 2 + 1]) * 0.5;
        // Mirror the circular mid-channel buffer so each correlation scans a
        // contiguous window without per-sample wraparound or stereo conversion.
        mid_[slot] = mid_[slot + capacity_] = mid;
    }
    input_end_ += accepted;
    std::size_t written{};
    while (written < output.size() / 2) {
        if (pending_begin_ == pending_end_ && !generate()) break;
        const auto frames = std::min(output.size() / 2 - written, pending_end_ - pending_begin_);
        std::copy_n(pending_.data() + pending_begin_ * 2, frames * 2, output.data() + written * 2);
        written += frames;
        pending_begin_ += frames;
        produced_ += frames;
    }
    return {accepted, written};
}

void time_stretch_t::finish() noexcept {
    finished_ = true;
    final_output_frames_ = output_frames_for(input_end_, rate_).value_or(produced_);
}

auto time_stretch_t::complete() const noexcept -> bool {
    return finished_ && produced_ >= final_output_frames_;
}

auto time_stretch_t::buffered_source_frames() const noexcept -> std::uint64_t {
    const auto presented = static_cast<std::uint64_t>(static_cast<long double>(produced_) *
                                                     rate_.numerator / rate_.denominator);
    return input_end_ > presented ? input_end_ - presented : 0;
}

} // namespace wormedit::audio
