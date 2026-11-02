#pragma once
#include "ui/frontend/storyboard.h"

struct nk_context;
struct SDL_Renderer;
namespace wormedit::ui {
[[nodiscard]] auto storyboard_assets_tab_active() -> bool;
void reset_storyboard_assets_tab();
void reset_storyboard_lua_input();
void prepare_storyboard_lua_input(const storyboard_view_t&);
[[nodiscard]] auto storyboard_lua_editor_active() -> bool;
void open_storyboard_lua_editor(const storyboard_view_t&, const storyboard_lua_target_t&);
void render_storyboard_lua_editor(nk_context*, const storyboard_view_t&, storyboard_action_t&);
[[nodiscard]] auto begin_storyboard_lua_picker_for_testing() -> std::optional<std::uint64_t>;
void complete_storyboard_lua_picker_for_testing(std::uint64_t, std::string);
void cancel_storyboard_panel_pointer();
[[nodiscard]] auto drain_storyboard_panel_cancel(storyboard_action_t&) -> bool;
void render_storyboard_generator_ownership(nk_context*, const storyboard_view_t&, storyboard_action_t&, bool input_available);
void render_storyboard_panel(nk_context*, const storyboard_view_t&, storyboard_action_t&, SDL_Renderer*, bool input_available);
} // namespace wormedit::ui
