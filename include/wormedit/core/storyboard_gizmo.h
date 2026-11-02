#pragma once

#include <wormedit/core/storyboard_vocabulary.h>
#include <wormedit/core/visual_layout.h>
#include <span>
#include <vector>

namespace wormedit {

enum class storyboard_gizmo_axis_t { both, x, y };

// Pointer coordinates are logical canvas units. The evaluator supplies the
// world-side factor of the selected op, including enclosing pivot wrappers.
// This helper never evaluates a stack or changes a document.
class storyboard_gizmo_drag_t {
  public:
    [[nodiscard]] static auto from(osbx_transform_kind_t, const visual_affine_t& world_side,
        std::span<const double> value, visual_point_t pointer,
        storyboard_gizmo_axis_t axis = storyboard_gizmo_axis_t::both)
        -> result_t<storyboard_gizmo_drag_t, failure_code_t>;
    [[nodiscard]] auto revise(visual_point_t pointer, bool shift)
        -> result_t<std::vector<double>, failure_code_t>;
    auto operator==(const storyboard_gizmo_drag_t&) const -> bool = default;
  private:
    osbx_transform_kind_t kind_{};
    storyboard_gizmo_axis_t axis_{};
    visual_affine_t inverse_;
    visual_point_t start_;
    std::vector<double> value_;
    double angle_{}, turns_{};
};

} // namespace wormedit
