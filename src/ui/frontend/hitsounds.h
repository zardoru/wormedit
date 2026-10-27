#pragma once
#include <wormedit/core/osu_hitsample.h>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui {
struct hitsound_note_view_t {
    osu_sample_set_t normal_set{};
    osu_sample_set_t addition_set{};
    osu_sample_set_t resolved_normal_set{};
    osu_sample_set_t resolved_addition_set{};
    std::uint8_t additions{};
    std::int64_t custom_index{};
    std::int64_t resolved_custom_index{};
    std::int64_t volume{};
    std::int64_t resolved_volume{};
    std::string_view filename;

    [[nodiscard]] static auto from(const osu_hitsample_t& sample, osu_sample_context_t context,
                                    std::string_view filename) -> hitsound_note_view_t {
        const auto normal = sample.normal_set == osu_sample_set_t::automatic ? context.normal_set : sample.normal_set;
        return {.normal_set = sample.normal_set, .addition_set = sample.addition_set,
            .resolved_normal_set = normal,
            .resolved_addition_set = sample.addition_set == osu_sample_set_t::automatic ? normal : sample.addition_set,
            .additions = static_cast<std::uint8_t>(sample.additions()), .custom_index = sample.custom_index,
            .resolved_custom_index = sample.custom_index == 0 ? context.custom_index : sample.custom_index,
            .volume = sample.volume, .resolved_volume = sample.volume == 0 ? context.volume : sample.volume,
            .filename = filename};
    }

    [[nodiscard]] auto details() const -> std::vector<std::string> {
        const auto& names = osu_sample_set_names;
        auto set_name = [&](osu_sample_set_t requested, osu_sample_set_t resolved) {
            auto name = std::string{names[static_cast<std::size_t>(requested)]};
            if (requested == osu_sample_set_t::automatic) name += std::string{" → "} + names[static_cast<std::size_t>(resolved)];
            return name;
        };
        std::vector<std::string> lines{
            "Hitsounds · normal set: " + set_name(normal_set, resolved_normal_set) +
                " · addition set: " + set_name(addition_set, resolved_addition_set),
            "Additions:" + std::string{additions & 2 ? " whistle" : ""} +
                (additions & 4 ? " finish" : "") + (additions & 8 ? " clap" : "") +
                (additions == 0 ? " none" : ""),
            "Custom index: " + std::to_string(resolved_custom_index) + (custom_index == 0 ? " (inherited)" : "") +
                " · volume: " + std::to_string(resolved_volume) + "%" + (volume == 0 ? " (inherited)" : "")};
        if (!filename.empty()) lines.push_back("Custom sample: " + std::string{filename});
        return lines;
    }
};

struct hitsound_palette_view_t {
    bool visible{};
    hitsound_values_t values;
    [[nodiscard]] static auto from(bool visible, std::span<const osu_hitsample_t> samples)
        -> hitsound_palette_view_t {
        return {.visible = visible, .values = hitsound_values_t::from(samples)};
    }
};
}
