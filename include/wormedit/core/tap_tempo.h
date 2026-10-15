#pragma once

#include <wormedit/core/rational.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace wormedit {

inline constexpr std::uint16_t maximum_tap_beats_per_measure = 32;
inline constexpr std::size_t maximum_tap_window = 4 * maximum_tap_beats_per_measure;
enum class tap_tempo_mode_t { last_four_measures, whole_session };
struct tap_tempo_options_t {
    std::uint32_t sample_rate{44'100};
    std::uint16_t beats_per_measure{4};
    tap_tempo_mode_t mode{tap_tempo_mode_t::last_four_measures};
};
enum class tap_tempo_result_t { accepted, ignored, restarted, invalid_configuration };
struct tap_tempo_estimate_t {
    double bpm{};
    rational_t rational_bpm;
    double rounded_bpm{};
    double half_bpm{};
    // Signed, potentially fractional source frame of beat zero (the first tap's
    // assigned downbeat). A negative intercept is retained rather than clamped.
    double phase_frame{};
    double residual_standard_deviation_ms{};
    std::uint64_t tap_count{};
    std::uint64_t session_tap_count{};
    // Coverage up to eight taps times a linear residual score (zero at 24 ms).
    double confidence{};
    bool stable{};
    auto operator==(const tap_tempo_estimate_t&) const -> bool = default;
};

class tap_tempo_estimator_t {
  public:
    // Zero sample rate or a meter outside 1..32 is invalid, without allocation.
    explicit tap_tempo_estimator_t(tap_tempo_options_t options = {});
    auto is_valid() const -> bool;

    void reset();
    auto tap(std::uint64_t source_frame) -> tap_tempo_result_t;
    // Also permits a caller to expire a session during silence before another tap.
    auto expire(std::uint64_t source_frame) -> bool;
    auto estimate() const -> std::optional<tap_tempo_estimate_t>;
    auto tap_count() const -> std::uint64_t { return count_; }

  private:
    struct sample_t { std::uint64_t beat{}; std::uint64_t frame{}; };
    struct fit_t { long double slope{}; long double intercept{}; long double residual{}; };
    auto fit() const -> std::optional<fit_t>;
    void accumulate(sample_t sample, int sign);
    void append(std::uint64_t source_frame, std::uint64_t beat);
    tap_tempo_options_t options_;
    std::array<sample_t, maximum_tap_window> window_{};
    std::size_t next_{};
    std::uint64_t count_{};
    std::uint64_t session_count_{};
    std::uint64_t first_frame_{};
    std::uint64_t last_frame_{};
    std::uint64_t last_beat_{};
    long double sum_i_{};
    long double sum_t_{};
    long double sum_ii_{};
    long double sum_it_{};
    long double sum_tt_{};
};

} // namespace wormedit
