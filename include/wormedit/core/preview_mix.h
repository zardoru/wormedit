#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace wormedit {
// Live playback monitoring only; offline audio export retains the authored mix.
enum class mix_bus_t : std::uint8_t { music, bgm, keysounds, hitsounds };
struct mix_bus_definition_t {
    mix_bus_t id;
    std::string_view title;
    std::string_view description;
};
inline constexpr std::array mix_bus_catalog{
    mix_bus_definition_t{mix_bus_t::music, "Music", "Song audio"},
    mix_bus_definition_t{mix_bus_t::bgm, "BGM", "BGM lane keysounds and stems"},
    mix_bus_definition_t{mix_bus_t::keysounds, "Keysounds", "Playable note keysounds"},
    mix_bus_definition_t{mix_bus_t::hitsounds, "Hitsounds", "osu! hitsounds"}};
inline constexpr std::size_t mix_bus_count = mix_bus_catalog.size();
inline constexpr std::uint8_t maximum_mix_level = 100;

struct mix_strip_t {
    std::uint8_t level{maximum_mix_level};
    bool muted{};
    bool solo{};
    auto operator==(const mix_strip_t&) const -> bool = default;
};

// Category buses under one master. Solo is additive: any soloed bus silences the others.
struct preview_mix_t {
    std::array<mix_strip_t, mix_bus_count> buses{};
    std::uint8_t master{maximum_mix_level};
    [[nodiscard]] auto is_valid() const -> bool;
    [[nodiscard]] auto is_default() const -> bool { return *this == preview_mix_t{}; }
    [[nodiscard]] auto bus(mix_bus_t id) const -> const mix_strip_t& { return buses[static_cast<std::size_t>(id)]; }
    [[nodiscard]] auto bus(mix_bus_t id) -> mix_strip_t& { return buses[static_cast<std::size_t>(id)]; }
    [[nodiscard]] auto has_solo() const -> bool;
    // Muted, or left out while another bus is soloed.
    [[nodiscard]] auto is_silenced(mix_bus_t id) const -> bool;
    // Linear factor for the bus, including the master level.
    [[nodiscard]] auto gain(mix_bus_t id) const -> float;
    auto operator==(const preview_mix_t&) const -> bool = default;
};
} // namespace wormedit
