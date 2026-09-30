#include <wormedit/core/tap_tempo.h>

#include <algorithm>
#include <cmath>
#include <limits>

namespace wormedit {

tap_tempo_estimator_t::tap_tempo_estimator_t(tap_tempo_options_t options) : options_(options) {}
auto tap_tempo_estimator_t::valid() const -> bool {
    return options_.sample_rate != 0 && options_.beats_per_measure != 0 &&
        options_.beats_per_measure <= maximum_tap_beats_per_measure &&
        (options_.mode == tap_tempo_mode_t::last_four_measures ||
         options_.mode == tap_tempo_mode_t::whole_session);
}
void tap_tempo_estimator_t::reset() {
    next_ = 0;
    count_ = session_count_ = first_frame_ = last_frame_ = last_beat_ = 0;
    sum_i_ = sum_t_ = sum_ii_ = sum_it_ = sum_tt_ = 0;
}
void tap_tempo_estimator_t::accumulate(sample_t sample, int sign) {
    const auto i = static_cast<long double>(sample.beat);
    const auto t = static_cast<long double>(sample.frame - first_frame_);
    sum_i_ += sign * i;
    sum_t_ += sign * t;
    sum_ii_ += sign * i * i;
    sum_it_ += sign * i * t;
    sum_tt_ += sign * t * t;
}
void tap_tempo_estimator_t::append(std::uint64_t source_frame, std::uint64_t beat) {
    const auto capacity = static_cast<std::size_t>(options_.beats_per_measure) * 4;
    if (count_ == 0) first_frame_ = source_frame;
    if (options_.mode == tap_tempo_mode_t::last_four_measures) {
        if (count_ == capacity) accumulate(window_[next_], -1);
        else ++count_;
        window_[next_] = {beat, source_frame};
        next_ = (next_ + 1) % capacity;
    } else ++count_;
    accumulate({beat, source_frame}, 1);
    ++session_count_;
    last_frame_ = source_frame;
    last_beat_ = beat;
}
auto tap_tempo_estimator_t::fit() const -> std::optional<fit_t> {
    if (count_ < 4) return std::nullopt;
    const auto n = static_cast<long double>(count_);
    const auto denominator = n * sum_ii_ - sum_i_ * sum_i_;
    if (denominator <= 0) return std::nullopt;
    const auto slope = (n * sum_it_ - sum_i_ * sum_t_) / denominator;
    const auto intercept = (sum_t_ - slope * sum_i_) / n;
    const auto squared_error = sum_tt_ - intercept * sum_t_ - slope * sum_it_;
    const auto residual = std::sqrt(std::max(0.0L, squared_error) / (n - 2));
    if (!std::isfinite(slope) || slope <= 0 || !std::isfinite(intercept) ||
        !std::isfinite(residual)) return std::nullopt;
    return fit_t{slope, intercept, residual};
}
auto tap_tempo_estimator_t::estimate() const -> std::optional<tap_tempo_estimate_t> {
    const auto fitted = fit();
    if (!valid() || !fitted) return std::nullopt;
    const auto bpm = static_cast<double>(60.0L * options_.sample_rate / fitted->slope);
    const auto rational = rationalize_positive_double(bpm);
    if (!rational) return std::nullopt;
    const auto deviation = static_cast<double>(1000.0L * fitted->residual / options_.sample_rate);
    const auto coverage = std::min(1.0, static_cast<double>(count_) / 8.0);
    return tap_tempo_estimate_t{bpm, *rational.value, std::round(bpm), std::round(bpm * 2) / 2,
        static_cast<double>(static_cast<long double>(first_frame_) + fitted->intercept),
        deviation, count_, session_count_, coverage * std::max(0.0, 1.0 - deviation / 24.0),
        count_ >= 8 && deviation < 12.0};
}

auto tap_tempo_estimator_t::expire(std::uint64_t source_frame) -> bool {
    if (count_ == 0 || source_frame < last_frame_ ||
        source_frame - last_frame_ < static_cast<std::uint64_t>(options_.sample_rate) * 2)
        return false;
    reset();
    return true;
}
auto tap_tempo_estimator_t::tap(std::uint64_t source_frame) -> tap_tempo_result_t {
    if (!valid()) return tap_tempo_result_t::invalid_configuration;
    const auto restarted = expire(source_frame);
    if (count_ == 0) {
        append(source_frame, 0);
        return restarted ? tap_tempo_result_t::restarted : tap_tempo_result_t::accepted;
    }
    if (source_frame <= last_frame_ || session_count_ == std::numeric_limits<std::uint64_t>::max())
        return tap_tempo_result_t::ignored;
    std::uint64_t advance{1};
    if (session_count_ >= 4) {
        const auto fitted = fit();
        if (!fitted) return tap_tempo_result_t::ignored;
        const auto gap = static_cast<long double>(source_frame - last_frame_);
        const auto beats = std::max(1.0L, std::round(gap / fitted->slope));
        if (beats >= static_cast<long double>(std::numeric_limits<std::uint64_t>::max()))
            return tap_tempo_result_t::ignored;
        advance = static_cast<std::uint64_t>(beats);
        const auto interval = gap / beats;
        if (interval < fitted->slope * 0.5L || interval > fitted->slope * 1.75L)
            return tap_tempo_result_t::ignored;
    }
    if (advance > std::numeric_limits<std::uint64_t>::max() - last_beat_)
        return tap_tempo_result_t::ignored;
    append(source_frame, last_beat_ + advance);
    return tap_tempo_result_t::accepted;
}

} // namespace wormedit
