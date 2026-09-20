#pragma once

#include "ui/frontend.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>

struct worm_ui_nuklear_compose_key_event_t {
    std::uint32_t keycode{};
    std::uint32_t scancode{};
    std::uint16_t modifiers{};
    bool down{};
    bool repeat{};
};

struct worm_ui_nuklear_compose_wheel_event_t {
    double delta_x{};
    double delta_y{};
    double mouse_x{};
    double mouse_y{};
    std::uint16_t modifiers{};
    bool coarse_steps{};
};

struct worm_ui_nuklear_compose_color_t {
    float r{};
    float g{};
    float b{};
    float a{};
};

struct worm_ui_nuklear_compose_state_t {
    std::array<worm_ui_nuklear_compose_key_event_t, 64> key_events{};
    std::size_t key_event_count{};
    std::array<worm_ui_nuklear_compose_wheel_event_t, WORM_UI_COMPOSE_VIEWPORT_INPUT_CAPACITY>
        wheel_events{};
    std::size_t wheel_event_count{};
    bool middle_click_pending{};
    float middle_click_x{};
    float middle_click_y{};
    bool cancel_pending{};
    bool keyboard_focus{};
    bool action_overflowed{};
    bool pointer_hold_active{};
    bool note_paint_pointer_active{};
    bool semantic_layer_paint_pointer_active{};
    bool lane_hold_active{};
    bool space_range_hold_active{};
    bool space_range_hold_moved{};
    bool note_move_candidate_active{};
    bool note_move_active{};
    double note_move_anchor_x{};
    double note_move_anchor_y{};
    float note_move_anchor_screen_x{};
    float note_move_anchor_screen_y{};
    bool area_selection_candidate_active{};
    bool area_selection_active{};
    double area_selection_anchor_x{};
    double area_selection_anchor_y{};
    float area_selection_anchor_screen_x{};
    float area_selection_anchor_screen_y{};
    WormUiComposeSelectionMode area_selection_mode{WORM_UI_COMPOSE_SELECTION_REPLACE};
    std::uint32_t lane_hold_keycode{};
    std::uint64_t lane_hold_lane_id{};
    bool navigation_key_active{};
    std::uint32_t navigation_keycode{};
    WormUiComposeNavigationAction navigation_action{WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD};
    bool pointer_navigation_active{};
    WormUiComposeNavigationAction pointer_navigation_action{
        WORM_UI_COMPOSE_NAVIGATION_CURSOR_BACKWARD};
    std::uint64_t last_chart_id{};
    std::uint64_t last_document_revision{};
    std::uint64_t label_chart_id{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t label_document_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t label_note_id{std::numeric_limits<std::uint64_t>::max()};
    std::array<char, WORM_UI_COMPOSE_NOTE_LABEL_BYTES + 1> note_label{};
    std::array<char, WORM_UI_COMPOSE_SEMANTIC_LAYER_NAME_CAPACITY> semantic_layer_name{};
    std::uint64_t semantic_layer_name_id{};
    std::uint64_t semantic_layer_name_revision{std::numeric_limits<std::uint64_t>::max()};
    std::uint64_t semantic_layer_color_id{};
    worm_ui_nuklear_compose_color_t semantic_layer_color{};
    bool semantic_layer_color_editor_visible{};
    bool inspector_visible{true};
};

[[nodiscard]] auto worm_ui_nuklear_compose_state() noexcept -> worm_ui_nuklear_compose_state_t&;

void worm_ui_nuklear_reset_compose_state();
