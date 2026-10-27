#include <wormedit/core/preview_mix.h>
#include <algorithm>
#include <cmath>

namespace wormedit {
auto mix_level_decibels(std::uint8_t level) -> std::optional<float> {
    if (level == 0) return std::nullopt;
    constexpr auto step = -minimum_mix_decibels / static_cast<float>(maximum_mix_level);
    return static_cast<float>(level - maximum_mix_level) * step;
}
auto mix_level_gain(std::uint8_t level) -> float {
    const auto decibels = mix_level_decibels(level);
    return decibels ? std::pow(10.0F, *decibels / 20.0F) : 0.0F;
}
auto preview_mix_t::is_valid() const -> bool {
    return master <= maximum_mix_level &&
        std::ranges::all_of(buses, [](const auto& strip) { return strip.level <= maximum_mix_level; });
}
auto preview_mix_t::has_solo() const -> bool {
    return std::ranges::any_of(buses, &mix_strip_t::solo);
}
auto preview_mix_t::is_silenced(mix_bus_t id) const -> bool {
    const auto& strip = bus(id);
    return strip.muted || (has_solo() && !strip.solo);
}
auto preview_mix_t::gain(mix_bus_t id) const -> float {
    if (is_silenced(id)) return 0.0F;
    return mix_level_gain(bus(id).level) * mix_level_gain(master);
}
} // namespace wormedit
