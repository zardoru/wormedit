#pragma once

#include <wormedit/core/document.h>

#include <optional>
#include <vector>

namespace wormedit::sys {

enum class sound_usage_kind_t : std::uint8_t {
    note,
    bgm,
    sound_channel_event,
    sound_channel_source,
};

enum class sound_usage_scope_t : std::uint8_t { resource_reference, shared_asset_reference };

struct sound_usage_row_t {
    chart_id_t chart;
    std::string chart_name;
    sound_usage_kind_t kind{sound_usage_kind_t::note};
    sound_usage_scope_t scope{sound_usage_scope_t::resource_reference};
    std::optional<local_position_t> position;
    object_id_t object;
    object_id_t channel;
    clip_id_t clip;
    resource_id_t referenced_resource;
    asset_id_t referenced_asset;
};

struct sound_usage_projection_t {
    song_id_t song;
    std::uint64_t revision{};
    resource_id_t resource;
    asset_id_t asset;
    bool valid{};
    std::size_t total_matching{};
    bool truncated{};
    std::vector<sound_usage_row_t> rows;
};

inline constexpr std::size_t sound_usage_projection_limit = 1024;

// Projects direct resource references and chart uses of the resource's shared audio asset.
[[nodiscard]] auto project_sound_usage(const song_document_t& document, resource_id_t resource)
    -> sound_usage_projection_t;

} // namespace wormedit::sys
