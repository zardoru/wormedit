#include "sys/storyboard/storyboard_gizmo_view.h"
#include "sys/storyboard/storyboard_scene_presenter.h"
#include "ui/frontend/storyboard.h"
#include <wormedit/core/osbx_evaluator.h>
#include <algorithm>
#include <array>

namespace wormedit::sys {
namespace {
constexpr std::array primary_kinds{osbx_transform_kind_t::translate, osbx_transform_kind_t::rotate,
    osbx_transform_kind_t::scale};

void append_target(ui::storyboard_view_t& view, const storyboard_scene_presenter_t& scenes,
    const ui::storyboard_op_view_t& op) {
    auto& gizmo = view.canvas.gizmo;
    if (!op.track.editable) {
        if (gizmo.reason.empty()) gizmo.reason = op.track.reason;
        return;
    }
    const auto space = scenes.op_space(view.state.selection.storyboard, gizmo.node, op.id, view.inspector.time);
    if (!space || !space->world_side.inverse()) {
        if (gizmo.reason.empty()) gizmo.reason = "Gizmo unavailable: singular or non-finite op space.";
        return;
    }
    auto& target = gizmo.append_target();
    target.op = op.id; target.kind = op.kind; target.world_side = space->world_side;
    target.value.assign(space->components().begin(), space->components().end());
    target.key = op.track.key; target.key_time_ms = *view.inspector.key_time_ms;
}
} // namespace

void build_storyboard_gizmo_view(ui::storyboard_view_t& view, const storyboard_scene_presenter_t& scenes) {
    auto& gizmo = view.canvas.gizmo;
    gizmo.reset_frame();
    gizmo.is_staged = scenes.has_preview(view.state.selection.storyboard);
    const auto& inspector = view.inspector;
    if (view.state.selection.nodes.size() != 1) return;
    gizmo.node = inspector.node;
    if (!inspector.editable || !inspector.key_time_ms) {
        gizmo.reason = inspector.reason;
        return;
    }
    if (!view.canvas.available) {
        gizmo.reason = view.canvas.status.empty() ? "Gizmo unavailable: scene evaluation failed." : view.canvas.status;
        return;
    }
    if (!inspector.world || !inspector.world->inverse()) {
        gizmo.reason = "Gizmo unavailable: singular or non-finite node transform.";
        return;
    }
    for (const auto kind : primary_kinds) {
        const auto op = std::ranges::find(inspector.ops, kind, &ui::storyboard_op_view_t::kind);
        if (op == inspector.ops.end()) gizmo.missing.push_back(kind);
        else append_target(view, scenes, *op);
    }
    for (const auto& op : inspector.ops)
        if (op.kind == osbx_transform_kind_t::pivot) append_target(view, scenes, op);
}
} // namespace wormedit::sys
