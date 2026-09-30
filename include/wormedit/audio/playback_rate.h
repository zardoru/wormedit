#pragma once
#include <wormedit/core/measure.h>
#include <array>
#include <optional>
#include <string_view>
namespace wormedit::audio {
inline constexpr std::array<rational_t, 9> playback_rates{{{1,10},{1,5},{1,4},{1,3},{1,2},{3,4},{4,5},{9,10},{1,1}}};
[[nodiscard]] auto playback_rate(std::size_t index) noexcept -> std::optional<rational_t>;
[[nodiscard]] auto playback_rate_label(std::size_t index) noexcept -> std::string_view;
[[nodiscard]] auto playback_rate_index(rational_t rate) noexcept -> std::optional<std::size_t>;
// Source -> output rounds up; output -> source rounds down. Overflow and unsupported rates reject.
[[nodiscard]] auto output_frames_for(std::uint64_t frames, rational_t rate) noexcept -> std::optional<std::uint64_t>;
[[nodiscard]] auto source_frames_for(std::uint64_t frames, rational_t rate) noexcept -> std::optional<std::uint64_t>;
// Carries fractional source frames across callback blocks. A rate change resets the remainder.
class source_frame_accumulator_t {
public:
    [[nodiscard]] auto advance(std::uint64_t output_frames, rational_t rate) noexcept -> std::optional<std::uint64_t>;
    void reset() noexcept { remainder_ = 0; rate_ = {1,1}; }
private:
    rational_t rate_{1,1};
    std::uint64_t remainder_{};
};
}
