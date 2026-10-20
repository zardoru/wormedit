#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace wormedit {

enum class osu_sample_set_t { automatic, normal, soft, drum };

struct osu_sample_context_t {
    osu_sample_set_t normal_set{osu_sample_set_t::normal};
    std::int64_t custom_index{1};
    std::int64_t volume{100};
};

struct osu_hitsample_t {
    bool whistle{};
    bool finish{};
    bool clap{};
    osu_sample_set_t normal_set{osu_sample_set_t::automatic};
    osu_sample_set_t addition_set{osu_sample_set_t::automatic};
    std::int64_t custom_index{};
    std::int64_t volume{};
    std::string filename;

    [[nodiscard]] auto is_valid() const -> bool {
        const auto valid_set = [](osu_sample_set_t set) {
            return set >= osu_sample_set_t::automatic && set <= osu_sample_set_t::drum;
        };
        return valid_set(normal_set) && valid_set(addition_set) && custom_index >= 0 &&
               custom_index <= INT32_MAX && volume >= 0 && volume <= 100 && filename.size() <= 4096 &&
               std::ranges::none_of(filename, [](unsigned char c) { return c < 32 || c == ':'; });
    }
    [[nodiscard]] auto additions() const -> std::int64_t {
        return (whistle ? 2 : 0) | (finish ? 4 : 0) | (clap ? 8 : 0);
    }
    auto operator==(const osu_hitsample_t&) const -> bool = default;
};

enum class hitsound_field_t { whistle, finish, clap, normal_set, addition_set };

struct hitsound_change_t {
    hitsound_field_t field{};
    // Empty toggles additions (mixed becomes on) or cycles a sample set.
    std::optional<int> value;
    [[nodiscard]] auto is_valid() const -> bool {
        return field >= hitsound_field_t::whistle && field <= hitsound_field_t::addition_set &&
               (!value || (*value >= 0 && *value <= (field < hitsound_field_t::normal_set ? 1 : 3)));
    }
    auto operator==(const hitsound_change_t&) const -> bool = default;
};

struct hitsound_values_t {
    std::array<std::optional<int>, 5> fields;
    [[nodiscard]] auto is_valid() const -> bool {
        for (std::size_t index = 0; index < fields.size(); ++index)
            if (fields[index] && (*fields[index] < 0 || *fields[index] > (index < 3 ? 1 : 3))) return false;
        return true;
    }
    [[nodiscard]] static auto from(std::span<const osu_hitsample_t> samples) -> hitsound_values_t;
    [[nodiscard]] auto next_value(hitsound_field_t field) const -> int;
    auto operator==(const hitsound_values_t&) const -> bool = default;
};

} // namespace wormedit
