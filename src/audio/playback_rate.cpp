#include <wormedit/audio/playback_rate.h>
#include <limits>
namespace wormedit::audio {
namespace {
auto convert(std::uint64_t frames, std::uint64_t multiply, std::uint64_t divide, bool ceil) noexcept -> std::optional<std::uint64_t> {
    const auto whole = frames / divide;
    const auto fraction = (frames % divide) * multiply;
    const auto tail = fraction / divide + (ceil && fraction % divide != 0 ? 1 : 0);
    if (whole > (std::numeric_limits<std::uint64_t>::max() - tail) / multiply)
        return std::nullopt;
    return whole * multiply + tail;
}
}
auto playback_rate(std::size_t index) noexcept -> std::optional<rational_t> {
    if (index >= playback_rates.size()) return std::nullopt;
    return playback_rates[index];
}
auto playback_rate_label(std::size_t index) noexcept -> std::string_view {
    return index < playback_rate_catalog.size() ? playback_rate_catalog[index].label : std::string_view{};
}
auto playback_rate_index(rational_t rate) noexcept -> std::optional<std::size_t> {
    for (std::size_t i=0; i<playback_rates.size(); ++i)
        if (playback_rates[i] == rate) return i;
    return std::nullopt;
}
auto output_frames_for(std::uint64_t frames, rational_t rate) noexcept -> std::optional<std::uint64_t> {
    if (!playback_rate_index(rate)) return std::nullopt;
    return convert(frames, rate.denominator, rate.numerator, true);
}
auto source_frames_for(std::uint64_t frames, rational_t rate) noexcept -> std::optional<std::uint64_t> {
    if (!playback_rate_index(rate)) return std::nullopt;
    return convert(frames, rate.numerator, rate.denominator, false);
}
auto source_frame_accumulator_t::advance(std::uint64_t frames, rational_t rate) noexcept -> std::optional<std::uint64_t> {
    const auto converted = source_frames_for(frames, rate);
    if (!converted) return std::nullopt;
    const auto old_remainder = rate == rate_ ? remainder_ : 0;
    const auto fraction = (frames % rate.denominator) * rate.numerator + old_remainder;
    const auto carry = fraction / rate.denominator - ((frames % rate.denominator) * rate.numerator) / rate.denominator;
    if (*converted > std::numeric_limits<std::uint64_t>::max() - carry) return std::nullopt;
    rate_ = rate;
    remainder_ = fraction % rate.denominator;
    return *converted + carry;
}
}
