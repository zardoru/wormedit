#pragma once

#include <wormedit/core/storyboard_gizmo.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wormedit::ui {

struct storyboard_gizmo_target_t {
    std::string op;
    osbx_transform_kind_t kind{osbx_transform_kind_t::translate};
    visual_affine_t world_side;
    std::vector<double> value;
    std::optional<std::string> key;
    std::int64_t key_time_ms{};
    auto operator==(const storyboard_gizmo_target_t&) const -> bool = default;
};

class storyboard_gizmo_view_t {
  public:
    std::string node, reason;
    bool can_begin{true}, is_staged{};
    std::vector<osbx_transform_kind_t> missing;
    void reset_frame() { node.clear(); reason.clear(); target_count_ = 0; missing.clear(); can_begin = true; is_staged = false; }
    [[nodiscard]] auto targets() const -> std::span<const storyboard_gizmo_target_t> {
        return std::span{target_storage_}.first(target_count_);
    }
    auto append_target() -> storyboard_gizmo_target_t& {
        if (target_count_ == target_storage_.size()) target_storage_.emplace_back();
        return target_storage_[target_count_++];
    }
    auto operator==(const storyboard_gizmo_view_t& other) const -> bool {
        return node == other.node && reason == other.reason && can_begin == other.can_begin && is_staged == other.is_staged &&
            missing == other.missing && std::ranges::equal(targets(), other.targets());
    }
  private:
    // No stored borrowed span: copying this view copies its owned slots. Reset
    // hides the active prefix without destroying nested strings/value capacity.
    std::vector<storyboard_gizmo_target_t> target_storage_;
    std::size_t target_count_{};
};

} // namespace wormedit::ui
