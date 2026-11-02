#pragma once
#include <wormedit/core/video_vocabulary.h>
#include <wormedit/core/storyboard_vocabulary.h>
#include "ui/frontend/storyboard_transport.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::ui {
enum class storyboard_video_audio_t { none, song, song_and_samples };
struct storyboard_video_settings_t {
    std::string filename{"storyboard.mp4"};
    media::video_profile_t profile{media::video_profile_t::mp4_h264_aac};
    std::uint32_t width{1920}, height{1080}, fps{60};
    storyboard_video_audio_t audio{storyboard_video_audio_t::song};
    storyboard_conditional_layer_t layers{storyboard_conditional_layer_t::pass};
    bool selection{}, replace_existing{};
    auto operator==(const storyboard_video_settings_t&) const -> bool = default;
};
struct storyboard_video_capability_t {
    std::uint32_t width{}, height{};
    bool available{};
    std::string reason;
    auto operator==(const storyboard_video_capability_t&) const -> bool = default;
};
struct storyboard_video_export_view_t {
    bool open{}, available{}, probing{}, running{}, cancelling{}, can_start{};
    std::uint64_t generation{}, completed{}, total{}, estimated_bytes{}, free_bytes{};
    double estimated_seconds{}, elapsed_seconds{}, remaining_seconds{};
    storyboard_video_settings_t settings;
    std::optional<storyboard_loop_region_t> selection;
    std::vector<storyboard_video_capability_t> capabilities;
    std::vector<std::string> blockers;
    std::string status;
    bool source_clock_with_cuts{};
    auto operator==(const storyboard_video_export_view_t&) const -> bool = default;
};
enum class storyboard_video_action_kind_t { none, open, close, configure, start, cancel };
struct storyboard_video_export_action_t {
    storyboard_video_action_kind_t kind{storyboard_video_action_kind_t::none};
    std::uint64_t generation{};
    storyboard_video_settings_t settings;
    auto operator==(const storyboard_video_export_action_t&) const -> bool = default;
};
}
