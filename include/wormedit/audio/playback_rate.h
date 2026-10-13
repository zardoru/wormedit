#pragma once
#include <wormedit/core/measure.h>
#include <array>
#include <optional>
#include <string_view>
namespace wormedit::audio {
enum class playback_rate_id_t { tenth, fifth, quarter, third, half, three_quarters, four_fifths, nine_tenths, normal };
struct playback_rate_descriptor_t {
    playback_rate_id_t id;
    rational_t rate;
    std::string_view label;
};
inline constexpr std::array playback_rate_catalog{
    playback_rate_descriptor_t{playback_rate_id_t::tenth, {1,10}, "0.1×"},
    playback_rate_descriptor_t{playback_rate_id_t::fifth, {1,5}, "0.2×"},
    playback_rate_descriptor_t{playback_rate_id_t::quarter, {1,4}, "0.25×"},
    playback_rate_descriptor_t{playback_rate_id_t::third, {1,3}, "0.33×"},
    playback_rate_descriptor_t{playback_rate_id_t::half, {1,2}, "0.5×"},
    playback_rate_descriptor_t{playback_rate_id_t::three_quarters, {3,4}, "0.75×"},
    playback_rate_descriptor_t{playback_rate_id_t::four_fifths, {4,5}, "0.8×"},
    playback_rate_descriptor_t{playback_rate_id_t::nine_tenths, {9,10}, "0.9×"},
    playback_rate_descriptor_t{playback_rate_id_t::normal, {1,1}, "1×"},
};
inline constexpr auto playback_rate_count = static_cast<std::size_t>(playback_rate_id_t::normal) + 1;
consteval auto make_playback_rates() -> std::array<rational_t, playback_rate_count> {
    std::array<rational_t, playback_rate_count> rates{};
    for (std::size_t i = 0; i < rates.size(); ++i) rates[i] = playback_rate_catalog[i].rate;
    return rates;
}
consteval auto is_playback_rate_catalog_exhaustive() -> bool {
    if (playback_rate_catalog.size() != playback_rate_count) return false;
    for (std::size_t i = 0; i < playback_rate_count; ++i)
        if (static_cast<std::size_t>(playback_rate_catalog[i].id) != i) return false;
    return true;
}
static_assert(is_playback_rate_catalog_exhaustive());
// Legacy index-based API remains until the sys/UI catalog-id wave.
inline constexpr auto playback_rates = make_playback_rates();
[[nodiscard]] auto playback_rate(std::size_t index) noexcept -> std::optional<rational_t>;
[[nodiscard]] auto playback_rate_label(std::size_t index) noexcept -> std::string_view;
[[nodiscard]] auto playback_rate_index(rational_t rate) noexcept -> std::optional<std::size_t>;
[[nodiscard]] inline auto playback_rate(playback_rate_id_t id) noexcept -> std::optional<rational_t> {
    return playback_rate(static_cast<std::size_t>(id));
}
[[nodiscard]] inline auto playback_rate_label(playback_rate_id_t id) noexcept -> std::string_view {
    return playback_rate_label(static_cast<std::size_t>(id));
}
[[nodiscard]] inline auto playback_rate_id(rational_t rate) noexcept -> std::optional<playback_rate_id_t> {
    const auto index = playback_rate_index(rate);
    return index ? std::optional{static_cast<playback_rate_id_t>(*index)} : std::nullopt;
}
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
