#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <wormedit/audio/waveform_viewport.h>

struct worm_ui_nuklear_audio_state_t {
    wormedit::audio::waveform_viewport_t waveform_viewport{};
    std::uint64_t waveform_asset_id{};
    bool waveform_pan_active{};
    float waveform_pan_anchor_x{};
    wormedit::audio::waveform_viewport_t waveform_pan_origin{};
    std::uint64_t waveform_revision{};
    bool sound_list_focused{};
    int list_move_pending{};
    bool list_audition_pending{};
    std::uint64_t properties_resource_id{}, properties_revision{}, properties_project_id{};
    char properties_name[257]{};
    int properties_kind{};
    bool offset_drag_active{};
    double offset_drag_frames_per_pixel{};
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
    char tag_filter[128]{};
    char new_tag[257]{};
    [[nodiscard]] auto matches_tags(std::span<const std::string_view> tags) const -> bool;
    // The selected resource may share an audio asset with another catalog entry.
    std::uint64_t selected_project_id{};
    std::uint64_t selected_resource_id{};
    // Only the verified application activation setter updates these source IDs.
    std::uint64_t selected_channel_id{};
    std::uint64_t selected_clip_id{};
    bool find_uses_open{};
    std::uint64_t sound_list_document_revision{};
    bool import_drop_valid{};
    float import_drop_x{}, import_drop_y{}, import_drop_width{}, import_drop_height{};
    bool sound_list_valid{};
    float sound_list_x{}, sound_list_y{}, sound_list_width{}, sound_list_height{};
};

[[nodiscard]] auto worm_ui_nuklear_audio_state() noexcept -> worm_ui_nuklear_audio_state_t&;

void worm_ui_nuklear_reset_audio_state();

// The native adapter guards text, IME, modal, and tab ownership before queuing.
[[nodiscard]] auto worm_ui_nuklear_audio_list_key_command(int direction, bool audition) noexcept -> bool;
struct WormUiAudioWaveformViewportRequest;
void worm_ui_nuklear_audio_waveform_viewport_request(WormUiAudioWaveformViewportRequest& request) noexcept;
