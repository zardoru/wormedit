#pragma once
#include "ui/frontend/storyboard_lua_links.h"
#include "ui/frontend/storyboard_video_export.h"

#include "ui/frontend/storyboard_assets.h"

#include <wormedit/core/id.h>
#include <wormedit/core/ui_layout_preferences.h>
#include "ui/frontend/timing_preview.h"
#include "ui/frontend/storyboard_hierarchy.h"
#include "ui/frontend/storyboard_inspector.h"
#include "ui/frontend/storyboard_timeline.h"
#include "ui/frontend/storyboard_gizmo.h"
#include "ui/frontend/storyboard_transport.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wormedit::ui {

enum class storyboard_kind_t { osb, osbx };
enum class storyboard_canvas_background_t { theme, black, white };
struct storyboard_entry_view_t {
    storyboard_id_t id;
    std::string name;
    storyboard_kind_t kind{storyboard_kind_t::osb};
    bool generator_owned{};
    std::optional<recipe_id_t> generator_recipe;
    std::string generator_module;
    [[nodiscard]] auto badge() const -> std::string_view {
        if (generator_owned) return kind == storyboard_kind_t::osbx ? "OSBX · Generated" : "OSB · Generated";
        return kind == storyboard_kind_t::osbx ? "OSBX" : "OSB";
    }
    [[nodiscard]] auto is_editable() const -> bool {
        return kind == storyboard_kind_t::osbx && !generator_owned;
    }
    auto operator==(const storyboard_entry_view_t&) const -> bool = default;
};
struct storyboard_selection_t {
    storyboard_id_t storyboard;
    std::vector<std::string> nodes;
    std::optional<std::string> op;
    std::vector<std::string> keys;
    auto operator==(const storyboard_selection_t&) const -> bool = default;
};
struct storyboard_viewport_t {
    double zoom{1};
    double pan_x{}, pan_y{};
    double timeline_begin_ms{};
    double timeline_pixels_per_ms{0.1};
    bool safe_area{true};
    storyboard_canvas_background_t background{storyboard_canvas_background_t::theme};
    auto operator==(const storyboard_viewport_t&) const -> bool = default;
};
struct storyboard_view_state_t {
    storyboard_selection_t selection;
    storyboard_viewport_t viewport;
    std::vector<std::string> hidden_nodes; // Editor-only, scoped to selected storyboard.
    ui_layout_preferences_t dock;
    storyboard_conditional_layer_t conditional_layer{storyboard_conditional_layer_t::pass};
    auto operator==(const storyboard_view_state_t&) const -> bool = default;
};
// SB-3 through SB-8 populate these panel slots. Their messages describe the
// current empty selection without introducing another copy of scene data.
struct storyboard_section_view_t {
    std::string_view message;
    auto operator==(const storyboard_section_view_t&) const -> bool = default;
};
struct storyboard_canvas_outline_t {
    std::string node;
    std::array<visual_point_t, 4> points;
    visual_point_t origin;
    bool group{};
    auto operator==(const storyboard_canvas_outline_t&) const -> bool = default;
};
struct storyboard_canvas_view_t {
    std::string_view message;
    bool available{};
    double width{640}, height{480}, time_ms{};
    std::uint64_t prepared_generation{};
    std::string status;
    std::vector<player_preview_storyboard_sprite_view_t> sprites;
    std::vector<storyboard_canvas_outline_t> outlines;
    storyboard_gizmo_view_t gizmo;
    void reset_frame() {
        available = false;
        width = 640; height = 480; time_ms = 0;
        status.clear(); sprites.clear(); outlines.clear();
        gizmo.reset_frame();
    }
    auto operator==(const storyboard_canvas_view_t&) const -> bool = default;
};
struct storyboard_view_t {
    bool visible{};
    bool create_available{};
    song_id_t project;
    std::uint64_t revision{}, session_generation{};
    std::vector<storyboard_entry_view_t> storyboards;
    storyboard_view_state_t state;
    storyboard_assets_view_t assets;
    storyboard_timeline_view_t timeline;
    storyboard_hierarchy_view_t hierarchy;
    storyboard_inspector_view_t inspector;
    storyboard_canvas_view_t canvas;
    storyboard_transport_view_t transport;
    storyboard_video_export_view_t video_export;

    [[nodiscard]] auto selected() const -> const storyboard_entry_view_t* {
        const auto found = std::ranges::find(storyboards, state.selection.storyboard, &storyboard_entry_view_t::id);
        return found == storyboards.end() ? nullptr : &*found;
    }
    [[nodiscard]] auto can_delete() const -> bool {
        const auto* entry = selected();
        return entry && entry->is_editable();
    }
    void refill_state(const storyboard_view_state_t& value) {
        state = value;
        const bool has_selection = selected() != nullptr;
        hierarchy.message = has_selection ? "No nodes selected." : "Choose a storyboard.";
        assets.message = has_selection ? "No image selected." : "Choose a storyboard.";
        canvas.message = has_selection ? "Select a node in the hierarchy."
            : storyboards.empty() ? "Create a storyboard to begin." : "Choose a storyboard to begin.";
        inspector.message = has_selection ? "Select a node or key to inspect." : "Choose a storyboard.";
        timeline.message = has_selection ? "No keyframes selected." : "Choose a storyboard.";
    }
    void reset_visibility() { visible = false; }
    auto operator==(const storyboard_view_t&) const -> bool = default;
};

enum class storyboard_action_kind_t { none, select, create, erase, set_dock, select_nodes, set_viewport, hierarchy, inspector, timeline, transport, assets, lua_link, video_export, detach_generator, set_conditional_layer };
struct storyboard_action_t {
    storyboard_action_kind_t kind{storyboard_action_kind_t::none};
    song_id_t project;
    std::uint64_t revision{}, session_generation{};
    storyboard_id_t storyboard;
    ui_layout_preferences_t dock;
    std::vector<std::string> nodes;
    storyboard_viewport_t viewport;
    std::uint64_t prepared_generation{};
    storyboard_hierarchy_mutation_t hierarchy;
    storyboard_inspector_action_t inspector;
    storyboard_timeline_action_t timeline;
    storyboard_transport_action_t transport;
    storyboard_assets_action_t assets;
    storyboard_lua_action_t lua_link;
    storyboard_video_export_action_t video_export;
    recipe_id_t expected_owner;
    storyboard_conditional_layer_t conditional_layer{storyboard_conditional_layer_t::pass};
    [[nodiscard]] auto document_gesture_phase() const -> std::optional<storyboard_inspector_phase_t> {
        if (kind == storyboard_action_kind_t::inspector) return inspector.phase;
        if (kind == storyboard_action_kind_t::timeline && timeline.kind == storyboard_timeline_action_kind_t::move_keys)
            return timeline.phase;
        return {};
    }
    [[nodiscard]] auto is_document_gesture_begin() const -> bool {
        return document_gesture_phase() == storyboard_inspector_phase_t::begin;
    }
    [[nodiscard]] auto is_document_gesture_cancel() const -> bool {
        return document_gesture_phase() == storyboard_inspector_phase_t::cancel;
    }
    [[nodiscard]] static auto from(const storyboard_view_t& view, storyboard_action_kind_t kind,
                                   storyboard_id_t target = {}) -> storyboard_action_t {
        return {.kind = kind, .project = view.project, .revision = view.revision,
            .session_generation = view.session_generation, .storyboard = target, .dock = view.state.dock,
            .viewport = view.state.viewport, .prepared_generation = view.canvas.prepared_generation};
    }
    auto operator==(const storyboard_action_t&) const -> bool = default;
};

} // namespace wormedit::ui
