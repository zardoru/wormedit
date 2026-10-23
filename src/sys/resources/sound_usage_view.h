#pragma once
#include "sys/resources/resource_usage_collector.h"

#include <wormedit/core/document.h>
#include <wormedit/core/command.h>
#include <wormedit/core/keyed_cache.h>

#include <optional>
#include <vector>
#include <tuple>

namespace wormedit::sys {



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

struct sound_usage_view_t {
    song_id_t song;
    std::uint64_t revision{};
    resource_id_t resource;
    asset_id_t asset;
    bool valid{};
    std::size_t total_matching{};
    bool truncated{};
    std::vector<sound_usage_row_t> rows;

    // Projects direct resource references and chart uses of the resource's shared audio asset.
    [[nodiscard]] static auto from(const song_document_t& document, resource_id_t resource)
        -> sound_usage_view_t;
};

class sound_usage_cache_t {
  public:
    [[nodiscard]] auto view_for(const song_document_t& document, resource_id_t resource,
                                std::uint64_t session_generation = 0)
        -> const sound_usage_view_t&;
  private:
    keyed_cache_t<std::tuple<document_stamp_t, resource_id_t, std::uint64_t>, sound_usage_view_t> projection_;
};

inline constexpr std::size_t sound_usage_projection_limit = resource_usage_projection_limit;

} // namespace wormedit::sys
