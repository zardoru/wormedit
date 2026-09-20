#pragma once

#include "ui/frontend.h"

#include <wormedit/core/document.h>
#include <wormedit/core/player_preview.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace wormedit {
class document_history_t;
namespace visual {
class image_job_t;
class video_frame_job_t;
} // namespace visual
} // namespace wormedit

namespace wormedit::sys {

// Owns the revision caches and borrowed scalar storage behind one player-preview
// view. It deliberately has no Nuklear or SDL renderer dependency, so another
// frontend can consume the same WormUiPlayerPreviewView boundary.
class player_preview_presenter_t {
  public:
    player_preview_presenter_t();
    ~player_preview_presenter_t();

    player_preview_presenter_t(player_preview_presenter_t&&) noexcept;
    auto operator=(player_preview_presenter_t&&) noexcept -> player_preview_presenter_t&;
    player_preview_presenter_t(const player_preview_presenter_t&) = delete;
    auto operator=(const player_preview_presenter_t&) -> player_preview_presenter_t& = delete;

    void populate(WormUiPlayerPreviewView& view, const document_history_t& history,
                  std::optional<chart_id_t> selected_chart, bool visible,
                  player_preview_skin_t skin, std::uint64_t sample_frame, std::uint32_t sample_rate,
                  const std::filesystem::path& project_directory
#if defined(WORMEDIT_HAS_IMAGE_DECODER)
                  ,
                  visual::image_job_t& image_job
#endif
#if defined(WORMEDIT_HAS_VIDEO_DECODER)
                  ,
                  visual::video_frame_job_t& video_job
#endif
    );

  private:
    struct impl_t;
    std::unique_ptr<impl_t> impl_;
};

} // namespace wormedit::sys
