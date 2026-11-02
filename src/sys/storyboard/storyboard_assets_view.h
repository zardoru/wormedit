#pragma once

#include "ui/frontend/storyboard_assets.h"
#include <wormedit/core/command.h>
#include <wormedit/core/osbx_document.h>
#include <filesystem>

namespace wormedit::ui { struct storyboard_view_t; }
namespace wormedit::sys {
class storyboard_scene_presenter_t;

// Authored image choices come from the existing inspector projection, even
// with no node selection. Prepared nodes borrow the shared scene owner.
class storyboard_assets_projection_t {
  public:
    void refill(ui::storyboard_view_t&, const song_document_t&, const std::filesystem::path&,
        const osbx_document_t* prepared_scene = nullptr);
    [[nodiscard]] auto prepare_thumbnails(ui::storyboard_view_t&, storyboard_scene_presenter_t&)
        -> std::span<ui::player_preview_visual_layer_view_t* const>;
  private:
    const song_document_t* document_{};
    std::optional<document_stamp_t> stamp_;
    storyboard_id_t storyboard_;
    std::uint64_t session_{}, prepared_{};
    std::filesystem::path directory_;
    std::vector<ui::player_preview_visual_layer_view_t*> thumbnail_views_;
};

void build_storyboard_image_users(std::vector<std::string>& users, std::string_view image,
    const osbx_document_t&, const osbx_document_t* prepared_scene = nullptr);
[[nodiscard]] auto validate_storyboard_image_removal(const storyboard_document_t&, std::string_view image,
    const osbx_document_t* prepared_scene = nullptr) -> result_t<void, error_t<failure_code_t>>;
[[nodiscard]] auto storyboard_resource_image_file(const song_document_t&, resource_id_t,
    const std::filesystem::path& directory) -> result_t<std::string, error_t<failure_code_t>>;

} // namespace wormedit::sys
