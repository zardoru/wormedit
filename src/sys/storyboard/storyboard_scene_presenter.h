#pragma once

#include "ui/frontend/storyboard.h"
#include <wormedit/core/document.h>
#include <wormedit/core/storyboard.h>
#include <filesystem>
#include <memory>

namespace wormedit { struct osbx_node_frame_t; struct osbx_op_space_t; struct osbx_edit_preview_t; }
namespace wormedit::visual { class image_job_t; }
namespace wormedit::sys {

// One source/index/image owner shared by the canvas and auxiliary player.
// Its document/session/prepared binding outlives every borrowed frame view.
class storyboard_scene_presenter_t {
  public:
    storyboard_scene_presenter_t();
    ~storyboard_scene_presenter_t();
    storyboard_scene_presenter_t(storyboard_scene_presenter_t&&) noexcept;
    auto operator=(storyboard_scene_presenter_t&&) noexcept -> storyboard_scene_presenter_t&;
    // Returns whether committed media binding changed. A retained property
    // preview only replaces its storyboard geometry; images/video stay bound.
    auto begin(const song_document_t&, std::uint64_t session, const std::filesystem::path&
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        , visual::image_job_t&
#endif
        , std::shared_ptr<const osbx_edit_preview_t> preview = {}) -> bool;
    [[nodiscard]] auto refill(std::vector<ui::player_preview_storyboard_sprite_view_t>&,
        std::optional<storyboard_id_t>, storyboard_time_t, std::size_t maximum_instances,
        storyboard_conditional_layer_t = storyboard_conditional_layer_t::pass) -> diagnosed_t<bool>;
    [[nodiscard]] auto scene(storyboard_id_t) const -> const osbx_document_t*;
    [[nodiscard]] auto has_preview(storyboard_id_t) const -> bool;
    [[nodiscard]] auto node_state(storyboard_id_t, std::string_view) const -> const osbx_node_frame_t*;
    [[nodiscard]] auto op_space(storyboard_id_t, std::string_view node, std::string_view op,
        storyboard_time_t) const -> result_t<osbx_op_space_t, failure_code_t>;
    [[nodiscard]] auto node_diagnostic(storyboard_id_t, std::string_view) const -> std::string_view;
    void populate_image(ui::player_preview_visual_layer_view_t&) const;
    [[nodiscard]] auto prepare_image_thumbnail(ui::player_preview_visual_layer_view_t&,
        storyboard_id_t, std::string_view image) -> result_t<void, error_t<failure_code_t>>;
    // Frame-borrowed references into the persistent Assets view. Primary
    // canvas/player/BGA demand always precedes these optional thumbnail rows.
    void finish_images(ui::player_preview_view_t&, ui::storyboard_canvas_view_t*
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
        , visual::image_job_t&
#endif
        , std::span<ui::player_preview_visual_layer_view_t* const> thumbnails = {});
  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
};
} // namespace wormedit::sys
