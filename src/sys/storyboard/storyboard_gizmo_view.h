#pragma once

namespace wormedit::ui { struct storyboard_view_t; }
namespace wormedit::sys {
class storyboard_scene_presenter_t;

// Uses the inspector's existing authored editability/key policy and the shared
// presenter's same-pass evaluator context. No scene or matrix cache is created.
void build_storyboard_gizmo_view(ui::storyboard_view_t&, const storyboard_scene_presenter_t&);

} // namespace wormedit::sys
