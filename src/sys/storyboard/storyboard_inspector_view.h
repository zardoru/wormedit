#pragma once

#include <wormedit/core/osbx_edit.h>
#include <algorithm>
#include <wormedit/core/osbx_evaluator.h>
#include "ui/frontend/storyboard_inspector.h"

namespace wormedit::ui { struct storyboard_view_t; }

namespace wormedit::sys {

[[nodiscard]] auto storyboard_node_edit_reason(const storyboard_document_t&, std::string_view node_id) -> std::string;
[[nodiscard]] auto storyboard_transform_label(osbx_transform_kind_t) -> std::string_view;

// The shared scene presenter supplies its evaluated selected-node instance;
// this projection never builds a second world-matrix evaluator or runs Lua.
void build_storyboard_inspector_view(ui::storyboard_view_t&, const song_document_t&,
    storyboard_time_t, const osbx_node_frame_t* evaluated = nullptr,
    const osbx_document_t* prepared_scene = nullptr);
[[nodiscard]] auto plan_storyboard_inspector_edit(const song_document_t&, storyboard_id_t,
    const ui::storyboard_inspector_action_t&) -> result_t<osbx_edit_change_t, failure_code_t>;

// Resolve the same authored target and expected link identity for source reads
// and publication. Descriptor bytes never cross the frontend seam.
[[nodiscard]] auto storyboard_lua_source(const song_document_t&, storyboard_id_t,
    const ui::storyboard_lua_action_t&) -> result_t<lua_source_descriptor_t, std::string>;
[[nodiscard]] auto plan_storyboard_lua_edit(const song_document_t&, storyboard_id_t,
    const ui::storyboard_lua_action_t&, const lua_source_descriptor_t* frozen = nullptr)
    -> result_t<osbx_edit_change_t, std::string>;

// One policy for numeric transforms, opacity/color and discrete property tracks.
// The caller supplies the shared snapped integer key time. Sampling the display
// value and snapping the authored key time are separate operations.
template<class Value>
[[nodiscard]] auto plan_storyboard_track_edit(const osbx_track_t<Value>& track,
    const osbx_track_target_t& target, const Value& value, std::int64_t key_time_ms,
    std::optional<std::string_view> expected_key = {})
    -> result_t<osbx_edit_change_t, failure_code_t> {
    if (!track.can_edit_key_at(key_time_ms)) return std::unexpected(failure_code_t::unavailable);
    const auto key = std::ranges::find(track.keys, key_time_ms, &osbx_key_t<Value>::time_ms);
    if (expected_key && (key == track.keys.end() || key->id != *expected_key))
        return std::unexpected(failure_code_t::stale_generation);
    if (track.keys.empty() && !track.lua)
        return osbx_set_default_t{.track = target, .value = value};
    if (key != track.keys.end())
        return osbx_set_key_value_t{.track = target, .key = key->id, .value = value};
    return osbx_add_key_t{.track = target, .time_ms = key_time_ms, .value = value};
}

} // namespace wormedit::sys
