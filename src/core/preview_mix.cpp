#include <wormedit/core/preview_mix.h>
#include <algorithm>

namespace wormedit {
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
    constexpr auto scale = static_cast<float>(maximum_mix_level);
    return static_cast<float>(bus(id).level) / scale * static_cast<float>(master) / scale;
}
} // namespace wormedit
