#include "nuklear_event_harness.h"

#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/player_preview.h"
#include "ui/nuklear/setup_state.h"
#include "ui/nuklear/timing_stage_editor.h"

namespace {

void initialize_compose_view(WormUiWorkspaceView& workspace, int width, int height) {
    workspace.project_loaded = true;
    workspace.project_title = "Event harness chart";
    workspace.artist = "Test artist";
    workspace.selected_difficulty = "Normal";
    workspace.status_text = "Ready";
    workspace.git_hash = "test";
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_COMPOSE;
    workspace.tab_heading = "Compose";
    constexpr const char* tab_names[WORM_UI_WORKSPACE_TAB_COUNT]{
        "Project", "Metadata", nullptr, "Compose", "Storyboard", "Audio / Slices",
        "Compare / Validate"};
    for (std::size_t index = 0; index < WORM_UI_WORKSPACE_TAB_COUNT; ++index)
        workspace.tabs[index] = {.label = tab_names[index], .enabled = tab_names[index] != nullptr};

    auto& timing = workspace.timing;
    timing.scene_available = true;
    timing.chart_id = 1;
    timing.document_revision = 1;
    timing.total_rows = 16;
    timing.viewport_width_pixels = static_cast<double>(width);
    timing.viewport_height_pixels = static_cast<double>(height);

    auto& compose = workspace.compose;
    compose.visible = true;
    compose.scene_available = true;
    compose.project_id = 1;
    compose.chart_id = 1;
    compose.document_revision = 1;
    compose.viewport_width_pixels = static_cast<double>(width);
    compose.viewport_height_pixels = static_cast<double>(height);
    compose.pixels_per_standard_measure = 128.0;
    compose.playable_field_left_pixels = 40.0;
    compose.playable_field_right_pixels = static_cast<double>(width) - 40.0;
    compose.timing_document_begin_fraction = 0.0;
    compose.timing_document_end_fraction = 1.0;
}

auto workspace_view_valid(const WormUiWorkspaceView& view) -> bool {
    return view.project_title && view.artist && view.selected_difficulty && view.status_text &&
        view.git_hash && view.tab_heading && view.active_tab <= WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE &&
        worm_ui_nuklear_setup_view_valid(&view.setup) &&
        worm_ui_nuklear_timing_view_valid(&view.timing) &&
        worm_ui_nuklear_compose_view_valid(&view.compose) &&
        worm_ui_nuklear_player_preview_view_valid(&view.player_preview);
}

} // namespace

nuklear_event_harness_t::nuklear_event_harness_t(int width, int height)
    : width_{width}, height_{height} {
    if (width_ <= 0 || height_ <= 0 || !SDL_Init(SDL_INIT_EVENTS)) return;
    sdl_initialized_ = true;
    initialize_compose_view(view_, width_, height_);
    initialized_ = worm_ui_frontend_init_headless(width_, height_);
    view_valid_ = workspace_view_valid(view_);
}

nuklear_event_harness_t::~nuklear_event_harness_t() {
    if (initialized_) worm_ui_frontend_shutdown();
    if (sdl_initialized_) SDL_Quit();
}

auto nuklear_event_harness_t::frame(std::span<const SDL_Event> events)
    -> WormUiWorkspaceAction {
    if (!initialized_) return {};
    worm_ui_frontend_begin_input();
    for (const auto& event : events) (void)worm_ui_frontend_handle_event(&event);
    worm_ui_frontend_end_input();
    auto action = worm_ui_frontend_compose(&view_);
    worm_ui_frontend_render(width_, height_);
    return action;
}

auto nuklear_event_harness_t::run_script(std::span<const event_step_t> steps)
    -> std::vector<WormUiWorkspaceAction> {
    std::vector<WormUiWorkspaceAction> actions;
    actions.reserve(steps.size());
    for (const auto& step : steps) actions.push_back(frame(step.events));
    return actions;
}
