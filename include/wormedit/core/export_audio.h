#pragma once
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace wormedit {
enum class export_audio_codec_t { mp3, ogg, wav };
inline constexpr std::array<std::uint32_t, 14> mp3_bitrates_kbps{
    32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};

struct export_audio_settings_t {
    export_audio_codec_t codec{export_audio_codec_t::mp3};
    // Zero selects min(source bitrate, 192 kbps), or 192 when unknown.
    std::uint32_t mp3_bitrate_kbps{};
    float ogg_quality{0.4F};
    [[nodiscard]] auto is_valid() const -> bool {
        return codec >= export_audio_codec_t::mp3 && codec <= export_audio_codec_t::wav &&
            (!mp3_bitrate_kbps || std::ranges::find(mp3_bitrates_kbps, mp3_bitrate_kbps) != mp3_bitrates_kbps.end()) &&
            std::isfinite(ogg_quality) && ogg_quality >= -0.1F && ogg_quality <= 1;
    }
    [[nodiscard]] auto extension() const -> std::string_view {
        switch (codec) {
        case export_audio_codec_t::mp3: return ".mp3";
        case export_audio_codec_t::ogg: return ".ogg";
        case export_audio_codec_t::wav: return ".wav";
        }
        return {};
    }
    [[nodiscard]] auto bitrate_for(std::uint32_t source_bits_per_second) const -> std::uint32_t {
        if (mp3_bitrate_kbps) return mp3_bitrate_kbps;
        const auto cap = source_bits_per_second ? std::min(192U, source_bits_per_second / 1000) : 192U;
        auto chosen = mp3_bitrates_kbps.front();
        for (const auto rate : mp3_bitrates_kbps) if (rate <= cap) chosen = rate;
        return chosen;
    }
    auto operator==(const export_audio_settings_t&) const -> bool = default;
};
}
