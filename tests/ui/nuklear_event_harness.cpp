#include "nuklear_event_harness.h"
#include "sys/resources/catalog_view.h"
#include <wormedit/core/workspace.h>

#include "ui/nuklear/compose_panel.h"
#include "ui/nuklear/audio/player_preview.h"
#include "ui/nuklear/project/setup_state.h"
#include "ui/nuklear/timing/timing_stage_editor.h"

#include <algorithm>
#include <array>
#include <string>

namespace {

class modifier_scope_t {
  public:
    explicit modifier_scope_t(SDL_Keymod modifiers) : previous_{SDL_GetModState()} {
        SDL_SetModState(modifiers);
    }
    ~modifier_scope_t() { SDL_SetModState(previous_); }
  private:
    SDL_Keymod previous_;
};

void initialize_compose_view(wormedit::ui::workspace_view_t& workspace, int width, int height) {
    workspace.sound_kinds.refill(wormedit::sys::sound_kind_view);
    workspace.new_project_layouts = wormedit::sys::new_project_layout_view;
    workspace.playback_rates.refill(wormedit::sys::playback_rate_view);
    workspace.bms_encodings.refill(wormedit::sys::bms_encoding_view);
    workspace.compose.timing_lanes.refill(wormedit::sys::timing_lane_view);
    workspace.compose.timing_lane_captions.refill(wormedit::sys::timing_caption_view);
    workspace.compose.note_palette.refill(wormedit::sys::note_palette_view);
    workspace.setup.output_families.refill(wormedit::sys::output_family_view);
    workspace.metadata.format_families.refill(wormedit::sys::metadata_family_view);
    workspace.transport_rate_id = wormedit::audio::playback_rate_id_t::normal;
    workspace.audio_slices.sound_kinds.refill(wormedit::sys::sound_kind_view);
    workspace.project_loaded = true;
    workspace.project_id.value = 1;
    workspace.selected_chart_id.value = 1;
    workspace.document_revision = 1;
    workspace.project_title = "Event harness chart";
    workspace.artist = "Test artist";
    workspace.selected_difficulty = "Normal";
    workspace.status_text = "Ready";
    workspace.git_hash = "test";
    workspace.active_tab = WORM_UI_WORKSPACE_TAB_COMPOSE;
    workspace.tab_heading = "Compose";
    for (const auto& tab : wormedit::workspace_tabs) {
        const auto index = static_cast<std::size_t>(tab.kind);
        workspace.tabs[index] = {
            .label = tab.kind == wormedit::workspace_tab_t::timing ? std::string_view{} : tab.label,
            .enabled = wormedit::is_workspace_tab_enabled(tab.kind),
            .id = tab.kind};
    }

    auto& timing = workspace.timing;
    timing.scene_available = true;
    timing.chart_id = wormedit::chart_id_t{1};
    timing.document_revision = 1;
    timing.total_rows = 16;
    timing.viewport_width_pixels = static_cast<double>(width);
    timing.viewport_height_pixels = static_cast<double>(height);

    auto& compose = workspace.compose;
    compose.visible = true;
    compose.scene_available = true;
    compose.project_id.value = 1;
    compose.chart_id.value = 1;
    compose.document_revision = 1;
    compose.viewport_width_pixels = static_cast<double>(width);
    compose.viewport_height_pixels = static_cast<double>(height);
    compose.pixels_per_standard_measure = 128.0;
    compose.playable_field_left_pixels = 40.0;
    compose.playable_field_right_pixels = static_cast<double>(width) - 40.0;
    compose.timing_document_begin_fraction = 0.0;
    compose.timing_document_end_fraction = 1.0;
}

auto workspace_view_valid(const wormedit::ui::workspace_view_t& view) -> bool {
    return view.active_tab <= WORM_UI_WORKSPACE_TAB_COMPARE_VALIDATE &&
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
    if (sdl_initialized_) SDL_QuitSubSystem(SDL_INIT_EVENTS);
}

auto nuklear_event_harness_t::frame(std::span<const SDL_Event> events, SDL_Keymod modifiers)
    -> workspace_action_snapshot_t {
    if (!initialized_) return workspace_action_snapshot_t{{}};
    const modifier_scope_t modifier_scope{modifiers};
    worm_ui_frontend_begin_input();
    for (auto event : events) {
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP)
            event.key.mod = modifiers;
        (void)worm_ui_frontend_handle_event(&event);
    }
    worm_ui_frontend_end_input();
    const wormedit::ui::testing::icon_widget_recording_scope_t recording{icon_widgets_};
    auto action = worm_ui_frontend_compose(&view_);
    drawn_text_.resize(worm_ui_frontend_copy_drawn_text_for_testing(nullptr, 0));
    (void)worm_ui_frontend_copy_drawn_text_for_testing(drawn_text_.data(), drawn_text_.size());
    const auto text_count = std::min<std::size_t>(
        worm_ui_frontend_copy_drawn_text_bounds_for_testing(nullptr, 0), 4096);
    drawn_text_bounds_.resize(text_count);
    (void)worm_ui_frontend_copy_drawn_text_bounds_for_testing(
        drawn_text_bounds_.data(), drawn_text_bounds_.size());
    worm_ui_frontend_render(width_, height_);
    return workspace_action_snapshot_t{action};
}

auto nuklear_event_harness_t::run_script(std::span<const event_step_t> steps)
    -> std::vector<workspace_action_snapshot_t> {
    std::vector<workspace_action_snapshot_t> actions;
    actions.reserve(steps.size());
    for (const auto& step : steps) actions.push_back(frame(step.events, step.modifiers));
    return actions;
}

auto nuklear_event_harness_t::drawn_text_center(std::string_view sought) const
    -> std::optional<std::pair<float, float>> {
    std::optional<std::pair<float, float>> center;
    for (const auto& item : drawn_text_bounds_) {
        if (std::string_view{item.text} != sought) continue;
        center = std::pair{item.x + item.width * 0.5f, item.y + item.height * 0.5f};
    }
    return center;
}

struct workspace_action_snapshot_t::storage_t {
    wormedit::ui::workspace_action_t action;
    explicit storage_t(const wormedit::ui::workspace_action_t& source) : action{source} {}
};

workspace_action_snapshot_t::workspace_action_snapshot_t(const wormedit::ui::workspace_action_t& action)
    : storage_{std::make_shared<storage_t>(action)} {}

auto workspace_action_snapshot_t::get() const -> const wormedit::ui::workspace_action_t& {
    return storage_->action;
}

namespace wormedit::test {
auto mouse_button(bool down, float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    event.button.windowID = 0;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.down = down;
    event.button.clicks = 1;
    event.button.x = x;
    event.button.y = y;
    return event;
}

auto mouse_motion(float x, float y) -> SDL_Event {
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = 0;
    event.motion.x = x;
    event.motion.y = y;
    return event;
}

auto click_at(nuklear_event_harness_t& harness, float x, float y, bool twice)
    -> std::vector<workspace_action_snapshot_t> {
    std::array<event_step_t, 2> script{};
    script[0].events = {mouse_motion(x, y), mouse_button(true, x, y)};
    script[1].events = {mouse_motion(x, y), mouse_button(false, x, y)};
    script[0].events[1].button.clicks = twice ? 2 : 1;
    script[1].events[1].button.clicks = twice ? 2 : 1;
    return harness.run_script(script);
}

} // namespace wormedit::test

auto nuklear_event_harness_t::widget_center(std::string_view tooltip) const
    -> std::pair<float, float> {
    return icon_widgets_.center_of(tooltip);
}

auto nuklear_event_harness_t::hover_widget(std::string_view tooltip)
    -> workspace_action_snapshot_t {
    const auto [x, y] = widget_center(tooltip);
    const auto event = wormedit::test::mouse_motion(x, y);
    return frame(std::span{&event, 1});
}

auto nuklear_event_harness_t::click_widget(std::string_view tooltip)
    -> std::vector<workspace_action_snapshot_t> {
    const auto [x, y] = widget_center(tooltip);
    return wormedit::test::click_at(*this, x, y);
}
