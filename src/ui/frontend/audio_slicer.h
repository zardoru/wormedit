#pragma once
#include <wormedit/core/vocabulary.h>
#include <cstdint>
#include <string>
#include <vector>

namespace wormedit::ui {
struct audio_slicer_settings_t {
    std::string bpm{"120"}, division{"1"}, offset{"0"};
    auto operator==(const audio_slicer_settings_t&) const -> bool = default;
};
struct audio_slicer_action_t {
    song_id_t project;
    chart_id_t chart;
    resource_id_t resource;
    asset_id_t asset;
    std::uint64_t revision{};
    audio_slicer_settings_t settings;
};
struct audio_slicer_view_t {
    std::vector<std::uint64_t> cut_frames;
    std::string message;
    bool can_slice{};
};
}
[[nodiscard]] auto worm_ui_frontend_audio_slicer_settings() -> wormedit::ui::audio_slicer_settings_t;
