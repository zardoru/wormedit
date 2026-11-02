#pragma once

#include "ui/frontend/storyboard.h"
#include <wormedit/core/document.h>
#include <wormedit/core/osbx_edit.h>
#include <wormedit/core/snap.h>
#include <wormedit/core/timing.h>

namespace wormedit::ui {
struct storyboard_timeline_view_t;
struct storyboard_timeline_action_t;
}
namespace wormedit::sys {

// Borrows the shared chart indices; the timeline never builds a second timing
// cache. Absence is a supported millisecond-only editing mode.
struct storyboard_timeline_timing_t {
    [[nodiscard]] static auto from(const document_history_t&, std::optional<chart_id_t>,
        const editor_interaction_state_t*) -> storyboard_timeline_timing_t;
    const chart_document_t* chart{};
    const measure_index_t* measures{};
    const timing_index_t* timing{};
    snap_grid_t grid;
    [[nodiscard]] auto has_timing() const -> bool;
    [[nodiscard]] auto is_available() const -> bool;
};

class storyboard_timeline_projection_t {
  public:
    void refill(ui::storyboard_timeline_view_t&, const song_document_t&,
        const ui::storyboard_view_t&, storyboard_time_t,
        const osbx_document_t* prepared_scene = nullptr,
        const storyboard_timeline_timing_t& timing = {});
  private:
    std::optional<document_stamp_t> stamp_;
    std::uint64_t session_{}, prepared_generation_{};
    storyboard_id_t storyboard_;
    std::vector<std::string> nodes_;
    chart_id_t chart_;
    snap_grid_t grid_;
};

// Authored key snapping belongs to snap.h; this adapter only maps shared chart
// time to/from its grid. Shift chooses the canonical millisecond snap.
[[nodiscard]] auto plan_storyboard_timeline_time(storyboard_time_t,
    const storyboard_timeline_timing_t&, bool grid_enabled, bool shift)
    -> result_t<std::int64_t, failure_code_t>;
[[nodiscard]] auto storyboard_timeline_pointer_time(double milliseconds)
    -> result_t<storyboard_time_t, failure_code_t>;
[[nodiscard]] auto storyboard_timeline_key_time(const osbx_document_t&, const osbx_key_target_t&)
    -> std::optional<std::int64_t>;

[[nodiscard]] auto plan_storyboard_timeline_edit(const song_document_t&,
    storyboard_id_t, const ui::storyboard_timeline_action_t&, storyboard_time_t,
    const osbx_document_t* prepared_scene = nullptr,
    const storyboard_timeline_timing_t& timing = {})
    -> result_t<osbx_edit_change_t, failure_code_t>;

} // namespace wormedit::sys
