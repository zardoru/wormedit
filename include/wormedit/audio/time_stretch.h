#pragma once

#include <wormedit/audio/playback_rate.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace wormedit::audio {

struct time_stretch_result_t {
    std::size_t consumed_frames{};
    std::size_t produced_frames{};
};

// Streaming stereo WSOLA. Buffers allocate only at construction; process,
// finish and reset never allocate. Input and output spans contain stereo pairs.
// Werner Verhelst and Marc Roelands, ICASSP 1993, vol. 2, pp. 554–557:
// https://doi.org/10.1109/ICASSP.1993.319366
class time_stretch_t {
public:
    explicit time_stretch_t(std::uint32_t sample_rate = 44'100);
    [[nodiscard]] auto reset(rational_t rate = {1, 1}) noexcept -> bool;
    [[nodiscard]] auto input_frames_needed(std::size_t output_frames) const noexcept -> std::size_t;
    [[nodiscard]] auto process(std::span<const float> input, std::span<float> output) noexcept
        -> time_stretch_result_t;
    void finish() noexcept;
    [[nodiscard]] auto complete() const noexcept -> bool;
    [[nodiscard]] auto buffered_source_frames() const noexcept -> std::uint64_t;
    [[nodiscard]] auto lookahead_frames() const noexcept -> std::size_t { return window_ + search_; }
    [[nodiscard]] auto fade_frames() const noexcept -> std::size_t { return window_; }
private:
    [[nodiscard]] auto sample(std::uint64_t frame, unsigned channel) const noexcept -> float;
    [[nodiscard]] auto correlation(std::uint64_t candidate, double previous_energy,
                                   std::size_t sample_stride = 1) const noexcept -> double;
    [[nodiscard]] auto search_candidates(std::uint64_t predicted, std::uint64_t first,
                                         std::uint64_t last, double previous_energy,
                                         std::size_t sample_stride, std::size_t candidate_stride)
        const noexcept -> std::uint64_t;
    [[nodiscard]] auto best_candidate(std::uint64_t predicted) noexcept -> std::uint64_t;
    [[nodiscard]] auto generate() noexcept -> bool;
    [[nodiscard]] auto next_source_position() const noexcept -> long double;
    std::size_t window_{};
    std::size_t hop_{};
    std::size_t search_{};
    std::size_t capacity_{};
    std::vector<float> input_;
    std::vector<double> mid_;
    std::vector<double> previous_mid_;
    std::vector<float> overlap_;
    std::vector<float> pending_;
    rational_t rate_{1, 1};
    std::uint64_t input_begin_{};
    std::uint64_t input_end_{};
    std::uint64_t hop_count_{};
    std::uint64_t produced_{};
    std::uint64_t final_output_frames_{};
    std::size_t pending_begin_{};
    std::size_t pending_end_{};
    bool finished_{};
};

} // namespace wormedit::audio
