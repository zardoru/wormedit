#pragma once

#include <cstdint>

struct worm_ui_nuklear_audio_state_t {
    bool offset_drag_active{};
    float offset_drag_anchor_x{};
    float offset_drag_width{};
    std::uint64_t offset_drag_chart_id{};
    std::uint64_t offset_drag_document_revision{};
    std::uint64_t offset_drag_asset_id{};
    std::uint64_t offset_drag_total_frames{};
    std::uint32_t offset_drag_sample_rate{};
    std::int64_t offset_drag_delta_frames{};
    bool offset_escape_pending{};
    bool offset_cancel_pending{};
    char search_text[128]{};
    // The selected resource may share an audio asset with another catalog entry.
    std::uint64_t selected_project_id{};
    std::uint64_t selected_resource_id{};
    // Only the verified application activation setter updates these source IDs.
    std::uint64_t selected_channel_id{};
    std::uint64_t selected_clip_id{};
    bool find_uses_open{};
    bool sound_list_valid{};
    float sound_list_x{}, sound_list_y{}, sound_list_width{}, sound_list_height{};
};

[[nodiscard]] auto worm_ui_nuklear_audio_state() noexcept -> worm_ui_nuklear_audio_state_t&;

void worm_ui_nuklear_reset_audio_state();
