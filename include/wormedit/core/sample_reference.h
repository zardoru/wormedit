#pragma once

#include <wormedit/core/bms36.h>
#include <wormedit/core/id.h>

#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace wormedit {

inline constexpr std::string_view typed_lua_osu_sample_origin = "Lua.osu_sample";

// Shared asset geometry, also carried by immutable declared-audio receipts.
struct audio_stream_info_t {
    std::uint32_t sample_rate{};
    std::uint32_t channels{};
    std::optional<std::uint64_t> frame_count;
    bool seekable{false};
    auto operator==(const audio_stream_info_t&) const -> bool = default;
};

struct bms_wav_reference_t {
    std::string index;
    std::optional<chart_id_t> source_chart;
    [[nodiscard]] auto is_valid() const -> bool {
        return index.size() == 2 && bms36_value(index[0]) >= 0 && bms36_value(index[1]) >= 0 &&
            (bms36_value(index[0]) != 0 || bms36_value(index[1]) != 0) &&
            (!source_chart || static_cast<bool>(*source_chart));
    }
    auto operator==(const bms_wav_reference_t&) const -> bool = default;
};

struct audio_file_reference_t {
    std::string declared_name;
    std::string path;
    std::string content_hash;
    std::optional<asset_id_t> asset;
    audio_stream_info_t stream;
    [[nodiscard]] auto is_valid() const -> bool;
    auto operator==(const audio_file_reference_t&) const -> bool = default;
};

// osu_sample writes the existing osu_hitsample_t effective owner directly.
// Only source intent with a distinct preservation promise is retained here.
using sample_reference_t = std::variant<bms_wav_reference_t, audio_file_reference_t>;
struct sample_reference_binding_t {
    sample_reference_t reference;
    resource_id_t bound_sound;
    asset_id_t bound_asset;
    [[nodiscard]] auto is_valid() const -> bool {
        return std::visit([](const auto& value) { return value.is_valid(); }, reference);
    }
    auto operator==(const sample_reference_binding_t&) const -> bool = default;
};
} // namespace wormedit
