#pragma once

#include <wormedit/core/id.h>
#include <wormedit/core/storyboard_vocabulary.h>
#include "ui/frontend/timing_preview.h"
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui {
struct storyboard_image_view_t {
    std::string id, file, caption;
    std::vector<std::string> users;
    player_preview_visual_layer_view_t thumbnail;
    bool missing{};
    bool thumbnail_requested{};
    std::string status, removal_reason;
    [[nodiscard]] auto is_unused() const -> bool { return users.empty(); }
    [[nodiscard]] auto label() const -> std::string_view { return file; }
    [[nodiscard]] auto image_id() const -> std::string_view { return id; }
    [[nodiscard]] auto resource_id() const -> resource_id_t { return {}; }
    [[nodiscard]] auto can_drag() const -> bool { return !missing; }
    auto operator==(const storyboard_image_view_t&) const -> bool = default;
};
struct storyboard_resource_image_view_t {
    resource_id_t resource;
    std::string name, file, caption;
    std::optional<std::string> image;
    player_preview_visual_layer_view_t thumbnail;
    bool missing{}, available{};
    bool thumbnail_requested{};
    std::string status;
    [[nodiscard]] auto label() const -> std::string_view { return name; }
    [[nodiscard]] auto image_id() const -> std::string_view { return image ? std::string_view(*image) : std::string_view{}; }
    [[nodiscard]] auto resource_id() const -> resource_id_t { return resource; }
    [[nodiscard]] auto can_drag() const -> bool { return available; }
    auto operator==(const storyboard_resource_image_view_t&) const -> bool = default;
};
struct storyboard_assets_view_t {
    std::string message, reason;
    bool editable{}, import_available{};
    osbx_layer_t spawn_layer{osbx_layer_t::background};
    std::vector<storyboard_image_view_t> images;
    std::vector<storyboard_resource_image_view_t> resources;
    [[nodiscard]] auto image(std::string_view id) const -> const storyboard_image_view_t* {
        const auto found = std::ranges::find(images, id, &storyboard_image_view_t::id);
        return found == images.end() ? nullptr : &*found;
    }
    auto operator==(const storyboard_assets_view_t&) const -> bool = default;
};
enum class storyboard_assets_action_kind_t { none, add_image, import_images, add_sprite, remove_image, set_thumbnail_demand };
struct storyboard_assets_action_t {
    storyboard_assets_action_kind_t kind{storyboard_assets_action_kind_t::none};
    std::string image;
    resource_id_t resource;
    visual_point_t position;
    osbx_layer_t layer{osbx_layer_t::background};
    std::vector<std::string> paths;
    std::vector<std::string> images;
    std::vector<resource_id_t> resources;
    auto operator==(const storyboard_assets_action_t&) const -> bool = default;
};
} // namespace wormedit::ui
