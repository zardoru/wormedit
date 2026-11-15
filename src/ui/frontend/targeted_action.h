#pragma once
#include <cstdint>

// A command aimed at one specific Compose document state. The frontend refuses a targeted action whose
// target no longer matches the state it would act on, so a late command cannot hit another chart.
struct worm_ui_compose_target_t {
    std::uint64_t project{}, chart{}, revision{};
    auto operator==(const worm_ui_compose_target_t&) const -> bool = default;
    [[nodiscard]] explicit operator bool() const { return project && chart; }
};

// Every UI action that needs a target. Add new ones here and to the table in frontend.cpp; the target
// check and the input gate then come from the table, not from the handler.
enum class worm_ui_targeted_action_t {
    bookmark_chooser,
    sound_picker,
    project_export,
    dock_open_layers,
    dock_create_layer,
    dock_toggle_sounds,
};
