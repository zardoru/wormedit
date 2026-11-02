#pragma once
#include <wormedit/core/storyboard_vocabulary.h>
#include <wormedit/core/lua_vocabulary.h>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace wormedit::ui {
struct storyboard_lua_target_t {
    osbx_track_target_t track;
    bool children{};
    auto operator==(const storyboard_lua_target_t&) const -> bool = default;
};
struct storyboard_lua_form_t {
    std::string module, entry_point;
    std::vector<lua_parameter_t> parameters;
    std::uint64_t seed{};
    std::int64_t begin_ms{}, end_ms{1000};
    auto operator==(const storyboard_lua_form_t&) const -> bool = default;
};
struct storyboard_lua_link_view_t {
    storyboard_lua_target_t target;
    std::string link_id;
    storyboard_lua_form_t form;
    std::string source_hash, error, reason;
    bool supported{}, editable{}, last_good{}, pending{};
    std::vector<std::int64_t> sample_times_ms;
    auto operator==(const storyboard_lua_link_view_t&) const -> bool = default;
};
enum class storyboard_lua_action_kind_t { none, link, reload, bake, unlink, fill, detach_children };
struct storyboard_lua_action_t {
    storyboard_lua_action_kind_t kind{storyboard_lua_action_kind_t::none};
    storyboard_lua_target_t target;
    std::string expected_link_id;
    storyboard_lua_form_t form;
    std::vector<std::string> selected_keys;
    auto operator==(const storyboard_lua_action_t&) const -> bool = default;
};
// Screen-space derivatives only; the inspector's stable-target table owns the
// descriptor summary. Samples have no authored key ID and cannot enter a drag.
struct storyboard_lua_band_view_t {
    double begin{}, end{};
    std::vector<double> samples;
    auto operator==(const storyboard_lua_band_view_t&) const -> bool = default;
};
} // namespace wormedit::ui
