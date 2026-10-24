#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

namespace wormedit::test::bms_program {
inline constexpr std::array profile_fixtures{
        std::pair{"bms-5k-standard.bms", "bms-5k-outside-scratch"},
        std::pair{"bms-5k-free-zone.bms", "bms-5k-outside-scratch-free-zone"},
        std::pair{"bms-5k-foot-pedal.bms", "bms-5k-outside-scratch-foot-pedal"},
        std::pair{"bms-10k-standard.bms", "bms-10k-left-outside-right-outside"},
        std::pair{"bms-10k-free-zone.bms", "bms-10k-left-outside-right-outside-free-zone"},
        std::pair{"bms-10k-foot-pedal.bms", "bms-10k-left-outside-right-outside-foot-pedal"},
        std::pair{"bms-10k-scratchless.bms", "bms-10k-scratchless"},
        std::pair{"bme-7k.bme", "bme-7k-outside-scratch"},
        std::pair{"bme-7k-foot-pedal.bme", "bme-7k-outside-scratch-foot-pedal"},
        std::pair{"bme-14k.bme", "bme-14k-left-outside-right-outside"},
        std::pair{"bme-14k-foot-pedal.bme", "bme-14k-left-outside-right-outside-foot-pedal"},
        std::pair{"pms-5button.pms", "pms-standard-5button"},
        std::pair{"pms-9button.pms", "pms-standard-9button"},
        std::pair{"pms-bme-9button.pms", "pms-bme-style-9button"},
        std::pair{"pms-bme-18button.pms", "pms-bme-style-18button"},
        std::pair{"double-scratch-fpp.bms", "bms-double-scratch-fpp"},
        std::pair{"octave-foot-pedal.bms", "bms-octave-foot-pedal"},
        std::pair{"project2dx-7k.bms", "project2dx-7k"}};
}
namespace wormedit::test {
inline auto explicit_fixture_profile(const std::filesystem::path& path) -> std::optional<std::string> {
    const auto name = path.filename().string();
    if (name == "x-aria-10k-hd.bme" || name == "custom-channel.bme") return std::nullopt;
    for (const auto& [fixture, profile] : bms_program::profile_fixtures)
        if (fixture == name) return std::string{profile};
    if (path.extension() == ".pms") return "pms-standard-9button";
    if (path.extension() == ".bme") return "bme-7k-outside-scratch";
    return "bms-5k-outside-scratch-free-zone";
}
}
