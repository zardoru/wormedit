#pragma once
#include <wormedit/core/document.h>
#include <string>
#include <vector>

namespace wormedit::ui {
struct bga_usage_target_t {
    chart_id_t chart;
    object_id_t event;
    local_position_t position;
    visual_event_kind_t kind{};
    resource_id_t referenced_resource;
    auto operator==(const bga_usage_target_t&) const -> bool = default;
};
struct bga_usage_activation_t {
    song_id_t song;
    std::uint64_t revision{};
    resource_id_t resource;
    asset_id_t asset;
    bga_usage_target_t target;
    auto operator==(const bga_usage_activation_t&) const -> bool = default;
};
struct bga_usage_row_view_t {
    bga_usage_target_t target;
    std::string label;
    auto operator==(const bga_usage_row_view_t&) const -> bool = default;
};
struct bga_usage_view_t {
    song_id_t song;
    std::uint64_t revision{};
    resource_id_t resource;
    asset_id_t asset;
    bool valid{};
    std::size_t total_matching{};
    bool truncated{};
    std::vector<bga_usage_row_view_t> rows;
    auto operator==(const bga_usage_view_t&) const -> bool = default;
};
} // namespace wormedit::ui
